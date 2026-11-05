enum class EntropySourceMode { Good, Zero, Duplicate, ZeroField, ZeroRoom,
    Fail, PartialFailure, Incomplete, Unavailable, ZeroThenGood, SevenZeroThenGood, ThreeZeroThenFail };
class MenuEntropyScriptedSource final : public runtime_internet_menu_entropy_detail::Source {
public:
    explicit MenuEntropyScriptedSource(EntropySourceMode mode) : mode_(mode) {}
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> bytes) noexcept override {
        ++calls;
        sizesValid = sizesValid && bytes.size() == 88U;
        if (bytes.size() != 88U) return {RuntimeSecureBytesStatus::InvalidRequest, 0, 0};
        if (mode_ == EntropySourceMode::Fail || mode_ == EntropySourceMode::PartialFailure
            || (mode_ == EntropySourceMode::ThreeZeroThenFail && calls == 4U)) {
            if (mode_ == EntropySourceMode::PartialFailure)
                std::fill_n(bytes.begin(), 11, static_cast<std::uint8_t>(0xa5));
            return {RuntimeSecureBytesStatus::SystemFailure, -7, 0};
        }
        if (mode_ == EntropySourceMode::Unavailable)
            return {RuntimeSecureBytesStatus::Unavailable, 38, 0};
        for (std::size_t n = 0; n != bytes.size(); ++n)
            bytes[n] = static_cast<std::uint8_t>(n + 1U);
        if (mode_ == EntropySourceMode::Zero
            || (mode_ == EntropySourceMode::ZeroThenGood && calls == 1U)
            || (mode_ == EntropySourceMode::SevenZeroThenGood && calls < 8U)
            || (mode_ == EntropySourceMode::ThreeZeroThenFail && calls < 4U))
            std::fill(bytes.begin(), bytes.end(), 0);
        if (mode_ == EntropySourceMode::Duplicate) std::copy_n(bytes.begin(), 8, bytes.begin() + 8);
        if (mode_ == EntropySourceMode::ZeroField) std::fill(bytes.begin() + 64, bytes.begin() + 72, 0);
        if (mode_ == EntropySourceMode::ZeroRoom) std::fill(bytes.begin() + 72, bytes.end(), 0);
        return {RuntimeSecureBytesStatus::Success, 0,
            mode_ == EntropySourceMode::Incomplete ? 87U : bytes.size()};
    }
    std::size_t calls = 0;
    bool sizesValid = true;
private:
    EntropySourceMode mode_;
};

void testEntropySourceContract(SmokeContext& context) {
    int checks = 0;
    const auto expect = [&](bool condition, const std::string& message) {
        ++checks; context.expect(condition, "same-owner entropy source: " + message);
    };
    RuntimeInternetMultiplayerMenuEntropy expected;
    expected.queryTransactionId = 0x0102030405060708ULL;
    expected.hostTransactionId = 0x090a0b0c0d0e0f10ULL;
    expected.joinTransactionId = 0x1112131415161718ULL;
    expected.matchRequestId = 0x191a1b1c1d1e1f20ULL;
    expected.matchActivationTransactionId = 0x2122232425262728ULL;
    expected.matchHostTransactionId = 0x292a2b2c2d2e2f30ULL;
    expected.matchJoinTransactionId = 0x3132333435363738ULL;
    expected.roomGeneration = 0x393a3b3c3d3e3f40ULL;
    expected.gameplayRandomSeed = 0x4142434445464748ULL;
    for (std::size_t n = 0; n != expected.roomIdBytes.size(); ++n)
        expected.roomIdBytes[n] = static_cast<std::uint8_t>(73U + n);
    for (const auto mode : {EntropySourceMode::Good, EntropySourceMode::ZeroThenGood,
             EntropySourceMode::SevenZeroThenGood}) {
        MenuEntropyScriptedSource source(mode);
        auto value = deterministicEntropy(); std::string error = "old error";
        expect(runtime_internet_menu_entropy_detail::generate(value, error, source), "valid candidate succeeds");
        expect(value == expected, "all nine ordered big-endian fields and room bytes match independent vector");
        expect(error.empty(), "successful commit clears previous error");
        expect(source.sizesValid && source.calls == (mode == EntropySourceMode::Good ? 1U
            : mode == EntropySourceMode::ZeroThenGood ? 2U : 8U), "one88byte request per required originalattempt");
    }
    for (const auto mode : {EntropySourceMode::Zero, EntropySourceMode::Duplicate,
             EntropySourceMode::ZeroField, EntropySourceMode::ZeroRoom, EntropySourceMode::Fail,
             EntropySourceMode::PartialFailure, EntropySourceMode::Incomplete,
             EntropySourceMode::Unavailable, EntropySourceMode::ThreeZeroThenFail}) {
        MenuEntropyScriptedSource source(mode);
        auto value = deterministicEntropy(); const auto original = value; std::string error = "old error";
        expect(!runtime_internet_menu_entropy_detail::generate(value, error, source), "unusable or failed source rejects");
        expect(value == original, "failure never publishes partial transaction or initial gameplay seed");
        const bool exhausted = mode == EntropySourceMode::Zero || mode == EntropySourceMode::Duplicate
            || mode == EntropySourceMode::ZeroField || mode == EntropySourceMode::ZeroRoom;
        expect(source.sizesValid && source.calls == (exhausted ? 8U
            : mode == EntropySourceMode::ThreeZeroThenFail ? 4U : 1U), "eight candidate retries, system failures stop immediately");
        expect(error == (exhausted
            ? "Windows secure random generator returned unusable correlation values"
            : "Windows secure random generation failed"), "original failure message and category retained");
    }
    std::cout << "Internet entropy original transaction: " << checks
              << " checks, 12sourcecases, bytes=88 fields=9+16 maxAttempts=8, provider injection separate from actualOS\n";
}
