#include "network/internet/private/RuntimeInternetServiceRandomBytes.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

enum class MainSourceMode { Complete, Zero, Failure, Partial, Short, Overreported, Unavailable };
class MainScriptedSource final : public runtime_service_random_detail::Source {
public:
    explicit MainScriptedSource(MainSourceMode mode):mode_(mode){}
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept override {
        ++calls;lastSize=output.size();
        if(mode_==MainSourceMode::Failure||mode_==MainSourceMode::Partial||mode_==MainSourceMode::Unavailable){
            if(mode_!=MainSourceMode::Failure)
                std::fill_n(output.begin(),(std::min)(output.size(),std::size_t{11}),0xa5);
            return {mode_==MainSourceMode::Unavailable?RuntimeSecureBytesStatus::Unavailable:RuntimeSecureBytesStatus::SystemFailure,-9,0};
        }
        for(std::size_t n=0;n!=output.size();++n)output[n]=mode_==MainSourceMode::Zero?0:static_cast<std::uint8_t>(n+1);
        return {RuntimeSecureBytesStatus::Success,0,mode_==MainSourceMode::Short?output.size()-1
            :mode_==MainSourceMode::Overreported?output.size()+1:output.size()};
    }
    std::size_t calls=0,lastSize=0;
private:
    MainSourceMode mode_;
};
int testSourcePolicy(int& failures){
    int checks=0;const auto expect=[&](bool value,const char* label){++checks;if(!value){++failures;std::cerr<<"FAIL Main source: "<<label<<'\n';}};
    static_assert(noexcept(runtime_service_random_detail::fill(nullptr,0,std::declval<runtime_service_random_detail::Source&>())));
    for(const auto mode:{MainSourceMode::Complete,MainSourceMode::Zero,MainSourceMode::Failure,MainSourceMode::Partial,
            MainSourceMode::Short,MainSourceMode::Overreported,MainSourceMode::Unavailable}){
        std::array<std::uint8_t,40> bytes{};bytes.fill(0xcc);MainScriptedSource source(mode);
        const bool success=mode==MainSourceMode::Complete||mode==MainSourceMode::Zero;
        expect(runtime_service_random_detail::fill(bytes.data()+4,32,source)==success,"rawzero succeeds,sourceerror/incomplete rejects");
        bool contents=true;for(std::size_t n=0;n!=32;++n)
            contents=contents&&(bytes[n+4]==(mode==MainSourceMode::Complete?static_cast<std::uint8_t>(n+1):0));
        expect(contents,"rawbytes exact orfailedoutput whollyzero");
        expect(source.calls==1U&&source.lastSize==32U,"one32byte sourcecall,no newnonzero/retry policy");
        expect(std::all_of(bytes.begin(),bytes.begin()+4,[](auto b){return b==0xcc;})
            &&std::all_of(bytes.end()-4,bytes.end(),[](auto b){return b==0xcc;}),"write/wipe respectsborrowedrange");
    }
    {
        std::array<std::uint8_t,16> bytes{};bytes.fill(0xcc);MainScriptedSource source(MainSourceMode::Complete);
        expect(!runtime_service_random_detail::fill(nullptr,0,source),"legacyNULL0 remainsfalse");
        expect(runtime_service_random_detail::fill(bytes.data(),0,source),"legacyvalidptr0 remainstrue");
        expect(!runtime_service_random_detail::fill(nullptr,1,source),"NULLnonzero guard");
        expect(!runtime_service_random_detail::fill(bytes.data(),static_cast<std::size_t>(0xffffffffULL)+1U,source),"overflowguard beforespan");
        expect(source.calls==0U&&std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0xcc;}),"invalid/zero consumesnothing andneverformsbadspan");
    }
    return checks;
}

int main() {
    static_assert(noexcept(runtime_service_random_detail::fillDefault(nullptr,0)));
    int checks=0,failures=0;
    const auto expect=[&](bool value,const char* text){++checks;if(!value){++failures;std::cerr<<"FAIL service callback: "<<text<<'\n';}};
    std::array<std::uint8_t,1040> bytes{};bytes.fill(0xcc);
    const bool nullZero=runtime_service_random_detail::fillDefault(nullptr,0);
    const bool pointerZero=runtime_service_random_detail::fillDefault(bytes.data()+8,0);
    expect(std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0xcc;}),"zero requests do notwrite");
    expect(!nullZero,"measuredlegacyNULL0 mustremainfalse");
    expect(pointerZero,"measuredlegacynonnull0 mustremaintrue");
    expect(!runtime_service_random_detail::fillDefault(nullptr,1),"nullnonzero rejected beforeOS");
    const auto tooLarge=static_cast<std::size_t>(0xffffffffULL)+1U;
    expect(!runtime_service_random_detail::fillDefault(bytes.data()+8,tooLarge),"ULONGoverflowrawrequest rejected beforeborrow/OS");
    expect(std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0xcc;}),"invalidrawrequest leaves storage untouched");
    for(const std::size_t size:{1U,8U,16U,32U,64U,88U,1024U}){
        bytes.fill(0xcc);
        expect(runtime_service_random_detail::fillDefault(bytes.data()+8,size),"actualsystem rawcallback succeeds");
        expect(std::all_of(bytes.begin(),bytes.begin()+8,[](auto b){return b==0xcc;})
            &&std::all_of(bytes.begin()+8+size,bytes.end(),[](auto b){return b==0xcc;}),"actualcallback staysinsideborrowedrange");
    }
    const int policyChecks=testSourcePolicy(failures);
    std::cout<<"Main callback policy: "<<policyChecks<<" checks, rawzeroaccepted/noextraretry/guardbeforeborrow failures="<<failures<<'\n';
    std::cout<<"Main current callback: "<<checks<<" checks, null0="<<nullZero<<" ptr0="<<pointerZero
             <<" guardedOverflow=1 actualOSsizes=1,8,16,32,64,88,1024 failures="<<failures<<'\n';
    return failures ? 1 : 0;
}
