enum class LanSecureMode { Good, Zero, ThreeZeroThenGood, ZeroThenGood, Fail,
    PartialFailure, ShortSuccess, Unavailable, TwoZeroThenFail };
class LanScriptedSecureSource final : public runtime_lan_menu_secure_detail::Source {
public:
    explicit LanScriptedSecureSource(LanSecureMode mode) : mode_(mode) {}
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> bytes) noexcept override {
        ++calls; lastSize = bytes.size();
        if (mode_ == LanSecureMode::Fail || mode_ == LanSecureMode::PartialFailure
            || (mode_ == LanSecureMode::TwoZeroThenFail && calls == 3U)) {
            if (mode_ == LanSecureMode::PartialFailure)
                std::fill_n(bytes.begin(), (std::min)(bytes.size(), std::size_t{3}), 0xa5);
            return {RuntimeSecureBytesStatus::SystemFailure, -9, 0};
        }
        if (mode_ == LanSecureMode::Unavailable) return {RuntimeSecureBytesStatus::Unavailable, 38, 0};
        const bool zero = mode_ == LanSecureMode::Zero
            || (mode_ == LanSecureMode::ThreeZeroThenGood && calls < 4U)
            || (mode_ == LanSecureMode::ZeroThenGood && calls == 1U)
            || (mode_ == LanSecureMode::TwoZeroThenFail && calls < 3U);
        for (std::size_t n = 0; n != bytes.size(); ++n)
            bytes[n] = zero ? 0 : static_cast<std::uint8_t>(n + 1U);
        return {RuntimeSecureBytesStatus::Success, 0,
            mode_ == LanSecureMode::ShortSuccess ? bytes.size() - 1U : bytes.size()};
    }
    std::size_t calls = 0, lastSize = 0;
private:
    LanSecureMode mode_;
};
class LanActualSystemSecureSource final : public runtime_lan_menu_secure_detail::Source {
public:
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept override {
        ++calls; return fillRuntimeSecureBytes(output);
    }
    std::size_t calls = 0;
};

void testLanSecureSourceContract(SmokeContext& context) {
    int checks = 0;
    const auto expect = [&](bool condition, const std::string& reason) {
        ++checks; context.expect(condition, "LAN same-owner secure source: " + reason);
    };
    for (const auto mode : {LanSecureMode::Good, LanSecureMode::ZeroThenGood, LanSecureMode::ThreeZeroThenGood}) {
        LanScriptedSecureSource source(mode); std::uint64_t value = 0xaabbccddeeff0011ULL;
        std::string error = "sentinel";
        expect(runtime_lan_menu_secure_detail::generateU64(value, error, source), "nonzero candidate succeeds");
        expect(value == 0x0102030405060708ULL && error.empty(), "original8byte bigendian decode and successful commit");
        expect(source.lastSize == 8U && source.calls == (mode == LanSecureMode::Good ? 1U
            : mode == LanSecureMode::ZeroThenGood ? 2U : 4U), "nonzero policy uses originalfourtry bound");
    }
    for (const auto mode : {LanSecureMode::Zero, LanSecureMode::Fail, LanSecureMode::PartialFailure,
             LanSecureMode::ShortSuccess, LanSecureMode::Unavailable, LanSecureMode::TwoZeroThenFail}) {
        LanScriptedSecureSource source(mode); const std::uint64_t original = 0xaabbccddeeff0011ULL;
        std::uint64_t value = original; std::string error;
        expect(!runtime_lan_menu_secure_detail::generateU64(value, error, source), "failure or allzero rejects U64");
        expect(value == original, "failedcandidate never publishes correlation value");
        expect(source.lastSize == 8U && source.calls == (mode == LanSecureMode::Zero ? 4U
            : mode == LanSecureMode::TwoZeroThenFail ? 3U : 1U), "systemfailure stops and zero retries stay bounded");
        expect(error == (mode == LanSecureMode::Zero ? "secure random generator returned only zero"
            : "Windows secure random generation failed"), "originalWindows U64 errors retained");
    }
    for (const auto mode : {LanSecureMode::Good, LanSecureMode::Zero}) {
        LanScriptedSecureSource source(mode); std::string value = "sentinel", error = "old";
        expect(runtime_lan_menu_secure_detail::generateId("peer-", value, error, source), "one16byte IDdraw succeeds includingzero");
        expect(value == (mode == LanSecureMode::Good ? "peer-0102030405060708090a0b0c0d0e0f10"
            : "peer-00000000000000000000000000000000"), "originalhex32 packing,zeroallowed separatelyfromU64");
        expect(error.empty() && source.calls == 1U && source.lastSize == 16U, "ID policy has exactlyone16byte draw");
    }
    for (const auto mode : {LanSecureMode::Fail, LanSecureMode::PartialFailure,
             LanSecureMode::ShortSuccess, LanSecureMode::Unavailable}) {
        LanScriptedSecureSource source(mode); std::string value = "identifier-sentinel", error;
        expect(!runtime_lan_menu_secure_detail::generateId("peer-", value, error, source), "sourcefailure rejectsID");
        expect(value == "identifier-sentinel" && error == "Windows secure random generation failed", "ID failureisatomic withoriginalerror");
        expect(source.calls == 1U && source.lastSize == 16U, "ID systemfailure neverretries");
    }
    for (const auto& prefix : {std::string(""), std::string("bad prefix"), std::string(32U, 'x')}) {
        LanScriptedSecureSource source(LanSecureMode::Good); std::string value = "unchanged", error;
        expect(!runtime_lan_menu_secure_detail::generateId(prefix, value, error, source), "originalinvalidprefix rejects");
        expect(value == "unchanged" && source.calls == 0U && error == "secure identifier prefix is invalid", "prefixvalidation precedes anysourcecall");
    }
    {
        LanScriptedSecureSource source(LanSecureMode::Good); std::string error;
        expect(!runtime_lan_menu_secure_detail::fill({}, error, source)
            && error == "secure random request is empty" && source.calls == 0U, "emptyrequest keepsoriginalguard/noOSread");
    }
    {
        std::array<std::uint8_t, 12> bytes{}; bytes.fill(0xcc);
        LanScriptedSecureSource source(LanSecureMode::PartialFailure); std::string error;
        expect(!runtime_lan_menu_secure_detail::fill(std::span(bytes).subspan(2, 8), error, source), "directpartialsourcefailure rejects");
        expect(std::all_of(bytes.begin() + 2, bytes.begin() + 10, [](auto b){return b == 0;}), "failedborrowed output wiped");
        expect(bytes[0] == 0xcc && bytes[1] == 0xcc && bytes[10] == 0xcc && bytes[11] == 0xcc, "failurewipe preservescanaries");
    }
    {
        LanActualSystemSecureSource source; std::uint64_t value = 0; std::string error;
        expect(runtime_lan_menu_secure_detail::generateU64(value, error, source)
            && value != 0U && error.empty() && source.calls >= 1U && source.calls <= 4U,
            "actualsystem source throughsameU64algorithm separatefrominjectedvectors");
    }
    std::cout << "LAN secure original policies: " << checks
              << " checks, U64=8bytes/nonzero4tries ID=16bytes/onecall/zeroallowed, scriptedpolicy+actualOS\n";
}
