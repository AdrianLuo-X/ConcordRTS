enum class LanNoiseMode { Complete, Zero, FirstOnly, SecondOnly, Failure, Partial, Short, Overreported, Unavailable };
class LanNoiseScriptedSource final : public runtime_lan_nonce_noise_detail::Source {
public:
    explicit LanNoiseScriptedSource(LanNoiseMode mode):mode_(mode){}
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> bytes) noexcept override {
        ++calls;lastSize=bytes.size();
        if(bytes.size()!=8U)return {RuntimeSecureBytesStatus::InvalidRequest,0,0};
        const std::array<std::uint32_t,2> words{
            mode_==LanNoiseMode::Zero||mode_==LanNoiseMode::SecondOnly?0U:0x89abcdefU,
            mode_==LanNoiseMode::Zero||mode_==LanNoiseMode::FirstOnly?0U:0x01234567U};
        if(mode_==LanNoiseMode::Failure)return {RuntimeSecureBytesStatus::SystemFailure,-9,0};
        std::memcpy(bytes.data(),words.data(),mode_==LanNoiseMode::Partial?4U:8U);
        if(mode_==LanNoiseMode::Partial||mode_==LanNoiseMode::Unavailable)
            return {mode_==LanNoiseMode::Unavailable?RuntimeSecureBytesStatus::Unavailable:RuntimeSecureBytesStatus::SystemFailure,-9,0};
        return {RuntimeSecureBytesStatus::Success,0,mode_==LanNoiseMode::Short?4U
            :mode_==LanNoiseMode::Overreported?9U:8U};
    }
    std::size_t calls=0,lastSize=0;
private:
    LanNoiseMode mode_;
};
class LanNoiseActualSource final : public runtime_lan_nonce_noise_detail::Source {
public:
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> bytes) noexcept override {
        ++calls;lastSize=bytes.size();return fillRuntimeSecureBytes(bytes);
    }
    std::size_t calls=0,lastSize=0;
};
void testLanNonceNoiseSource(SmokeContext& context){
    static_assert(noexcept(runtime_lan_nonce_noise_detail::acquire(std::declval<std::uint64_t&>())));
    static_assert(noexcept(runtime_lan_nonce_noise_detail::acquire(std::declval<std::uint64_t&>(),std::declval<runtime_lan_nonce_noise_detail::Source&>())));
    int checks=0;const auto expect=[&](bool value,const std::string& reason){++checks;context.expect(value,"LANoriginalnoise: "+reason);};
    for(const auto mode:{LanNoiseMode::Complete,LanNoiseMode::Zero,LanNoiseMode::FirstOnly,LanNoiseMode::SecondOnly,
            LanNoiseMode::Failure,LanNoiseMode::Partial,LanNoiseMode::Short,LanNoiseMode::Overreported,LanNoiseMode::Unavailable}){
        struct Guarded{std::uint64_t before=0x1111222233334444ULL;std::uint64_t noise=0xaaaabbbbccccddddULL;std::uint64_t after=0x5555666677778888ULL;} g;
        LanNoiseScriptedSource source(mode);const bool success=mode==LanNoiseMode::Complete||mode==LanNoiseMode::Zero
            ||mode==LanNoiseMode::FirstOnly||mode==LanNoiseMode::SecondOnly;
        expect(runtime_lan_nonce_noise_detail::acquire(g.noise,source)==success,"fulltwo-word success vs allerrors/incomplete");
        const std::uint64_t wanted=mode==LanNoiseMode::Complete?0x89abcdef01234567ULL
            :mode==LanNoiseMode::FirstOnly?0x89abcdef00000000ULL:mode==LanNoiseMode::SecondOnly?0x01234567ULL:0ULL;
        expect(g.noise==wanted,"firstnativeword high/second low,zero allowed anderror neverpartialnoise");
        expect(source.calls==1U&&source.lastSize==8U,"onefull8byte source,no addedpolicyretry");
        expect(g.before==0x1111222233334444ULL&&g.after==0x5555666677778888ULL,"onlyoriginal64bitnoise borrowed");
    }
    {
        std::uint64_t noise=0;LanNoiseActualSource source;
        expect(runtime_lan_nonce_noise_detail::acquire(noise,source),"actualOS8byte acquisition,not a wholemixednonce guarantee");
        expect(source.calls==1U&&source.lastSize==8U,"actualOSsource originaltwo-word width");
        expect(runtime_lan_nonce_noise_detail::acquire(noise),"defaultproduction source alsoactualOS");
    }
    std::cout<<"LAN nonce noise: "<<checks<<" checks, bytes=8 nativewords=2 high-then-low, zero/error policy separatefromlegacyclock-counter nonce\n";
}
