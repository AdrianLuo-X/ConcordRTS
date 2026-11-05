#include "runtime/window/RuntimeMenuParentHost.h"
#include <array>
#include <cstdint>
#include <iostream>
static_assert(sizeof(RuntimeWindowId)==sizeof(std::uint64_t));
struct Lifetime final : RuntimeMenuParentTeardownPort {
    RuntimeWindowId borrowed{};bool parent=false,childStopped=true,badDestroy=false;
    std::array<char,32> order{};unsigned at=0,revokes=0,destroys=0;
    void revokeParentBorrow() noexcept override { order[at++]='R';if(borrowed.value)++revokes;borrowed={}; }
    void destroyParentWindow() noexcept override { order[at++]='D';badDestroy=badDestroy||borrowed.value!=0||!childStopped;if(parent)++destroys;parent=false; }
};
int main(){unsigned checks=0,failures=0;auto require=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<why<<'\n';}};
 for(bool parent:{false,true})for(bool borrowed:{false,true}){Lifetime state;state.parent=parent;state.borrowed={borrowed?19U:0U};const auto saved=state.borrowed;closeRuntimeMenuParent(state);require(state.order[0]=='R'&&state.order[1]=='D',"release before native destroy");require(!state.badDestroy&&!state.parent&&state.borrowed.value==0,"destroy witness cannot observe stale parent borrow");require(state.revokes==static_cast<unsigned>(borrowed)&&state.destroys==static_cast<unsigned>(parent),"native resources released once");closeRuntimeMenuParent(state);require(state.revokes==static_cast<unsigned>(borrowed)&&state.destroys==static_cast<unsigned>(parent),"repeat close no double ownership");require(saved.value==(borrowed?19U:0U),"copied ID value never encodes native storage");}
 Lifetime cancelled;cancelled.parent=true;cancelled.borrowed={42};cancelled.childStopped=false;cancelled.childStopped=true;closeRuntimeMenuParent(cancelled);require(!cancelled.badDestroy&&cancelled.revokes==1&&cancelled.destroys==1,"completed child cancellation precedes parent teardown");
 std::cout<<"Persistent parent shared lifetime: checks="<<checks<<" failures="<<failures<<" skipped=0 nativeHeaders=0\n";return failures?1:0;
}
