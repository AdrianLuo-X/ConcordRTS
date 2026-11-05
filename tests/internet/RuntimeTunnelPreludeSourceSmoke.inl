enum class TunnelSourceMode { Complete, Zero, Failure, PartialFailure, Short, Overreported, Unavailable };
class TunnelScriptedSource final : public runtime_tunnel_prelude_detail::Source {
public:
    explicit TunnelScriptedSource(TunnelSourceMode mode) : mode_(mode) {}
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> bytes) noexcept override {
        ++calls; lastSize = bytes.size();
        if (mode_ == TunnelSourceMode::Failure || mode_ == TunnelSourceMode::PartialFailure
            || mode_ == TunnelSourceMode::Unavailable) {
            if (mode_ != TunnelSourceMode::Failure)
                std::fill_n(bytes.begin(), (std::min)(bytes.size(),std::size_t{11}), 0xa5);
            return {mode_ == TunnelSourceMode::Unavailable ? RuntimeSecureBytesStatus::Unavailable
                : RuntimeSecureBytesStatus::SystemFailure,-9,0};
        }
        for (std::size_t n=0;n!=bytes.size();++n)
            bytes[n]=mode_==TunnelSourceMode::Zero ? 0 : static_cast<std::uint8_t>(n+1);
        return {RuntimeSecureBytesStatus::Success,0,mode_==TunnelSourceMode::Short ? bytes.size()-1
            : mode_==TunnelSourceMode::Overreported ? bytes.size()+1 : bytes.size()};
    }
    std::size_t calls=0,lastSize=0;
private:
    TunnelSourceMode mode_;
};
class TunnelActualSource final : public runtime_tunnel_prelude_detail::Source {
public:
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> bytes) noexcept override {
        ++calls;lastSize=bytes.size();return fillRuntimeSecureBytes(bytes);
    }
    std::size_t calls=0,lastSize=0;
};
void testTunnelPreludeSourceContract(SmokeContext& context) {
    static_assert(runtimeInternetTcpTunnelPreludeBytes==32U);
    static_assert(noexcept(runtime_tunnel_prelude_detail::fill({},std::declval<runtime_tunnel_prelude_detail::Source&>())));
    int checks=0;
    const auto expect=[&](bool value,const std::string& reason){++checks;context.expect(value,"originalTunnelrawsource: "+reason);};
    for(const auto mode:{TunnelSourceMode::Complete,TunnelSourceMode::Zero,TunnelSourceMode::Failure,
            TunnelSourceMode::PartialFailure,TunnelSourceMode::Short,TunnelSourceMode::Overreported,TunnelSourceMode::Unavailable}){
        std::array<std::uint8_t,40> bytes{};bytes.fill(0xcc);auto output=std::span(bytes).subspan(4,runtimeInternetTcpTunnelPreludeBytes);
        TunnelScriptedSource source(mode);const bool success=mode==TunnelSourceMode::Complete||mode==TunnelSourceMode::Zero;
        expect(runtime_tunnel_prelude_detail::fill(output,source)==success,"complete andallzero succeed, errors/incomplete reject");
        bool contents=true;for(std::size_t n=0;n!=output.size();++n)
            contents=contents&&(output[n]==(mode==TunnelSourceMode::Complete ? static_cast<std::uint8_t>(n+1) : 0));
        expect(contents,"all32rawbytes exact orfailure wiped, originalzeroaccepted");
        expect(source.calls==1U&&source.lastSize==32U,"oneoriginal32byte request,no nonzero/retrypolicy added");
        expect(std::all_of(bytes.begin(),bytes.begin()+4,[](auto b){return b==0xcc;})
            &&std::all_of(bytes.end()-4,bytes.end(),[](auto b){return b==0xcc;}),"writes/wipe remaininsideborrowedspan");
    }
    {
        TunnelScriptedSource source(TunnelSourceMode::Complete);
        expect(!runtime_tunnel_prelude_detail::fill({},source)&&source.calls==0U,"emptyprivate invalidrequest neverreachesOS");
    }
    {
        std::array<std::uint8_t,40> bytes{};bytes.fill(0xcc);TunnelActualSource source;
        expect(runtime_tunnel_prelude_detail::fill(std::span(bytes).subspan(4,runtimeInternetTcpTunnelPreludeBytes),source),"actualOS fillsoriginalprelude span");
        expect(source.calls==1U&&source.lastSize==32U,"actualOS originalwidth/onecall");
        expect(std::all_of(bytes.begin(),bytes.begin()+4,[](auto b){return b==0xcc;})
            &&std::all_of(bytes.end()-4,bytes.end(),[](auto b){return b==0xcc;}),"actualOS respectsborrowedspan");
    }
    std::cout<<"Tunnel original prelude: "<<checks<<" checks, bytes=32 zeroaccepted/onecall, policy separatefromactualOS\n";
}
