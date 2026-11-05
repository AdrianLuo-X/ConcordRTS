enum class ServiceNonceMode { Complete, LastByte, Zero, Failure, PartialFailure, Short, Overreported, Unavailable };
class ServiceNonceScriptedSource final : public runtime_service_nonce_detail::Source {
public:
    explicit ServiceNonceScriptedSource(ServiceNonceMode mode) : mode_(mode) {}
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept override {
        ++calls; lastSize = output.size();
        if (mode_ == ServiceNonceMode::Failure || mode_ == ServiceNonceMode::PartialFailure
            || mode_ == ServiceNonceMode::Unavailable) {
            if (mode_ != ServiceNonceMode::Failure)
                std::fill_n(output.begin(), (std::min)(output.size(), std::size_t{11}), 0xa5);
            return {mode_ == ServiceNonceMode::Unavailable ? RuntimeSecureBytesStatus::Unavailable
                : RuntimeSecureBytesStatus::SystemFailure, -9, 0};
        }
        std::fill(output.begin(), output.end(), 0);
        if (mode_ == ServiceNonceMode::LastByte) output.back() = 1;
        if (mode_ == ServiceNonceMode::Complete || mode_ == ServiceNonceMode::Short
            || mode_ == ServiceNonceMode::Overreported)
            for (std::size_t n = 0; n != output.size(); ++n) output[n] = static_cast<std::uint8_t>(n + 1);
        return {RuntimeSecureBytesStatus::Success, 0,
            mode_ == ServiceNonceMode::Short ? output.size() - 1
                : mode_ == ServiceNonceMode::Overreported ? output.size() + 1 : output.size()};
    }
    std::size_t calls = 0, lastSize = 0;
private:
    ServiceNonceMode mode_;
};
class ServiceNonceActualSource final : public runtime_service_nonce_detail::Source {
public:
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept override {
        ++calls; lastSize = output.size(); return fillRuntimeSecureBytes(output);
    }
    std::size_t calls = 0, lastSize = 0;
};
void testServiceNonceSourceContract(SmokeContext& smoke) {
    static_assert(runtimeInternetServiceNonceBytes == 32U);
    static_assert(sizeof(RuntimeInternetServiceNonce) == 32U);
    static_assert(noexcept(runtime_service_nonce_detail::fill(
        std::declval<RuntimeInternetServiceNonce&>(), std::declval<runtime_service_nonce_detail::Source&>())));
    struct Guarded { std::array<std::uint8_t, 4> before; RuntimeInternetServiceNonce nonce;
        std::array<std::uint8_t, 4> after; };
    const auto fresh = [] { Guarded g;g.before.fill(0xcc);g.nonce.fill(0xbb);g.after.fill(0xdd);return g; };
    const auto guards = [](const Guarded& g) { return std::all_of(g.before.begin(),g.before.end(),[](auto b){return b==0xcc;})
        && std::all_of(g.after.begin(),g.after.end(),[](auto b){return b==0xdd;}); };
    int checks = 0;
    const auto expect = [&](bool value, const std::string& reason) { ++checks;smoke.expect(value,"sameoriginal nonce32 source: "+reason); };
    RuntimeInternetServiceNonce expected{};
    for (std::size_t n = 0; n != expected.size(); ++n) expected[n] = static_cast<std::uint8_t>(n + 1);
    for (const auto mode : {ServiceNonceMode::Complete, ServiceNonceMode::LastByte}) {
        auto g = fresh(); ServiceNonceScriptedSource source(mode);
        expect(runtime_service_nonce_detail::fill(g.nonce, source), "fullraw32 or lastbyteonly is nonzero");
        auto wanted = expected; if(mode == ServiceNonceMode::LastByte){wanted.fill(0);wanted.back()=1;}
        expect(g.nonce == wanted, "all32rawbytes retained with no truncation/reorder");
        expect(guards(g) && source.calls == 1U && source.lastSize == 32U, "one32byte draw keepscanaries");
    }
    for (const auto mode : {ServiceNonceMode::Zero, ServiceNonceMode::Failure, ServiceNonceMode::PartialFailure,
             ServiceNonceMode::Short, ServiceNonceMode::Overreported, ServiceNonceMode::Unavailable}) {
        auto g = fresh(); ServiceNonceScriptedSource source(mode);
        expect(!runtime_service_nonce_detail::fill(g.nonce, source), "zero/sourcefailure/incomplete never accepted");
        expect(std::all_of(g.nonce.begin(),g.nonce.end(),[](auto b){return b==0;}), "failure nonceoutput whollyzero");
        expect(guards(g), "failurewrite staysinsideoriginalalias");
        expect(source.calls == 1U && source.lastSize == 32U, "no addedcaller retries or predictablefallback");
    }
    {
        auto g = fresh(); ServiceNonceActualSource source;
        expect(runtime_service_nonce_detail::fill(g.nonce, source), "actualsystem source fillsoriginalnonce");
        expect(std::any_of(g.nonce.begin(),g.nonce.end(),[](auto b){return b!=0;}), "originalnonzero check retained");
        expect(source.calls == 1U && source.lastSize == 32U, "actualcall requestsoriginal32bytes");
        expect(guards(g), "actualOS borrowedspan bounded");
    }
    std::cout << "Service original nonce: " << checks
              << " checks, bytes=32 nonzero/noexcept/onecall, injectedpolicy separatefromactualOS\n";
}
