#include "runtime/performance/RuntimeProcessPerformanceScope.h"
#include <array>
#include <cstddef>
#include <iostream>
#include <type_traits>
#include <vector>

using Policy=RuntimeProcessPowerPolicy;
using Error=RuntimeProcessPolicyWriteError;
struct Reply {bool success=true;Error error=Error::Other;};
struct Port final:RuntimeProcessPerformancePort {
    Policy policy{1,0,5};
    bool getPresent=true,setPresent=true,readResult=true,seedCorrect=true;
    std::size_t reads=0;
    mutable std::size_t availability=0,errorQueries=0;
    Error last=Error::Other;
    std::vector<Reply> replies;
    std::vector<Policy> writes;
    Port(){writes.reserve(32);replies.reserve(32);}
    bool available()const noexcept override{++availability;return getPresent&&setPresent;}
    bool read(Policy& value)noexcept override{
        ++reads;seedCorrect=seedCorrect&&(value==Policy{1,0,0});
        if(!readResult)return false;value=policy;return true;
    }
    bool write(const Policy& value)noexcept override{
        const auto index=writes.size();writes.push_back(value);
        const auto reply=index<replies.size()?replies[index]:Reply{};last=reply.error;
        if(reply.success)policy=value;return reply.success;
    }
    Error lastWriteError()const noexcept override{++errorQueries;return last;}
};
static_assert(!std::is_copy_constructible_v<RuntimeProcessPerformanceScope>);
static_assert(!std::is_move_constructible_v<RuntimeProcessPerformanceScope>);
static_assert(std::is_nothrow_destructible_v<RuntimeProcessPerformanceScope>);
static_assert(sizeof(Policy)==12);
int main(){
    std::size_t checks=0,failures=0,cases=0;
    const auto need=[&](bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"Process policy failure: "<<label<<'\n';}};
    constexpr std::array<std::uint32_t,6> bits{0,1,4,5,0x80000000u,0xffffffffu};
    for(const auto control:bits)for(const auto state:bits){
        ++cases;Port port;const Policy previous{1,control,state};port.policy=previous;
        const Policy requested{1,control|5u,state&~5u};const bool changes=requested!=previous;
        {
            RuntimeProcessPerformanceScope scope(port);
            need(port.availability==1&&port.reads==1&&port.seedCorrect,"one capability/read and original input version seed");
            need(scope.appliedMask()==5,"full requested mask is observable even on no-op");
            need(port.policy==requested,"preserve all unknown bits and clear only requested state bits");
            need(port.writes.size()==(changes?1u:0u),"no write for already requested policy");
            if(changes)need(port.writes.front()==requested,"complete original request value");
            need(scope.restore(),"explicit restore succeeds");need(port.policy==previous,"complete previous policy restored");
            need(scope.appliedMask()==(changes?0u:5u),"no-op restore retains historical appliedMask");
            const auto count=port.writes.size();need(scope.restore()&&port.writes.size()==count,"repeated successful restore is idempotent");
            need(port.errorQueries==0,"no last-error reads for success/no-op/restore");
        }
        need(port.policy==previous&&port.writes.size()==(changes?2u:0u),"destructor neither duplicates restore nor changes no-op");
    }
    for(int missing=0;missing<3;++missing){
        ++cases;Port port;port.getPresent=missing==1;port.setPresent=missing==0;const auto previous=port.policy;
        {RuntimeProcessPerformanceScope scope(port);need(scope.appliedMask()==0&&scope.restore(),"missing function is clean no-op");}
        need(port.reads==0&&port.writes.empty()&&port.errorQueries==0&&port.policy==previous,"missing get/set performs no OS operation");
    }
    for(int failure=0;failure<3;++failure){
        ++cases;Port port;if(failure==0)port.readResult=false;else port.policy.version=failure==1?0u:2u;const auto previous=port.policy;
        {RuntimeProcessPerformanceScope scope(port);need(scope.appliedMask()==0&&scope.restore(),"read failure or bad version is no-op");}
        need(port.reads==1&&port.seedCorrect&&port.writes.empty()&&port.errorQueries==0&&port.policy==previous,"read/version guard before every write/error lookup");
    }
    for(const auto reason:{Error::InvalidParameter,Error::NotSupported}){
        ++cases;Port port;const Policy previous{1,0x80,0x85};port.policy=previous;port.replies={{false,reason},{true,Error::Other}};
        {RuntimeProcessPerformanceScope scope(port);
            need(scope.appliedMask()==1,"only documented error permits execution-only fallback");
            need(port.writes.size()==2&&port.writes[0]==Policy{1,0x85,0x80}&&port.writes[1]==Policy{1,0x81,0x84},"fallback starts from original complete previous policy");
            need(port.errorQueries==1&&scope.restore(),"one original error query, successful fallback restore");need(scope.appliedMask()==0,"changed fallback clears mask on restore");}
        need(port.policy==previous&&port.writes.size()==3&&port.writes.back()==previous,"full original policy restored after fallback");
    }
    {
        ++cases;Port port;port.policy={1,1,0};const auto previous=port.policy;port.replies={{false,Error::InvalidParameter}};
        {RuntimeProcessPerformanceScope scope(port);need(scope.appliedMask()==1&&port.writes.size()==1,"fallback no-op does not invoke second setter");need(scope.restore()&&scope.appliedMask()==1,"fallback no-op keeps historical mask");}
        need(port.policy==previous&&port.writes.size()==1&&port.errorQueries==1,"fallback no-op destructor does not restore unwritten policy");
    }
    {
        ++cases;Port port;const auto previous=port.policy;port.replies={{false,Error::Other}};
        {RuntimeProcessPerformanceScope scope(port);need(scope.appliedMask()==0&&scope.restore(),"other setter error never falls back");}
        need(port.policy==previous&&port.writes.size()==1&&port.errorQueries==1,"unrelated setter failure leaves previous policy");
    }
    {
        ++cases;Port port;const auto previous=port.policy;port.replies={{false,Error::InvalidParameter},{false,Error::NotSupported}};
        {RuntimeProcessPerformanceScope scope(port);need(scope.appliedMask()==0&&scope.restore(),"both apply failures remain no-op");}
        need(port.policy==previous&&port.writes.size()==2&&port.errorQueries==1,"fallback failure not queried or restored a second time");
    }
    for(const bool explicitRetry:{true,false}){
        ++cases;Port port;const auto previous=port.policy;port.replies={{true,Error::Other},{false,Error::Other},{true,Error::Other}};
        {RuntimeProcessPerformanceScope scope(port);need(scope.appliedMask()==5,"retry scenario applied");
            need(!scope.restore()&&scope.appliedMask()==5,"failed restore retains active ownership and mask");
            need(port.errorQueries==0&&port.policy!=previous,"failed restore does not query stale OS error or claim restored");
            if(explicitRetry)need(scope.restore()&&scope.appliedMask()==0,"explicit retry clears ownership only after success");}
        need(port.policy==previous&&port.writes.size()==3&&port.writes[1]==previous&&port.writes[2]==previous,"same full previous policy retried by explicit call or destructor");
    }
    {
        ++cases;Port port;const auto previous=port.policy;{RuntimeProcessPerformanceScope scope(port);need(scope.appliedMask()==5,"destructor-only scenario applied");}
        need(port.policy==previous&&port.writes.size()==2,"destructor restores original without explicit close");
    }
    {
        ++cases;Port port;const auto previous=port.policy;port.replies={{true,Error::Other},{false,Error::Other}};
        {RuntimeProcessPerformanceScope scope(port);need(scope.appliedMask()==5,"failed destructor fixture applied");}
        need(port.writes.size()==2&&port.policy!=previous&&port.errorQueries==0,"destructor restore failure remains actual failure and is never hidden by a third retry");
    }
    {
        ++cases;Port port;const auto previous=port.policy;
        const auto early=[&](){RuntimeProcessPerformanceScope scope(port);return scope.appliedMask();};
        need(early()==5&&port.policy==previous&&port.writes.size()==2,"early return releases session-local policy");
    }
    std::cout<<"Process performance policy contract: checks="<<checks<<" cases="<<cases<<" failures="<<failures
        <<" skipped=0 maskPairs=36 perInstancePorts=1 globalFakeContext=0 nativeCalls=not-run\n";
    return failures==0?0:1;
}
