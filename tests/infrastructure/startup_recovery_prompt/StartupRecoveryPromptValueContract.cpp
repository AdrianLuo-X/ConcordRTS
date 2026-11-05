#include "runtime/window/RuntimeStartupRecoveryPrompt.h"
#include <iostream>
#include <type_traits>
using Port=RuntimeStartupRecoveryPrompt;
using Signature=bool(*)(const Port&,const std::string&);
static_assert(sizeof(Port)==sizeof(Port::Request));
static_assert(alignof(Port)==alignof(Port::Request));
static_assert(std::is_standard_layout_v<Port>);
static_assert(std::is_trivially_destructible_v<Port>);
static_assert(std::is_aggregate_v<Port>);
static_assert(std::is_same_v<decltype(&requestRuntimeStartupRecoveryRetry),Signature>);
constexpr Port empty{};static_assert(empty.requestRetry==nullptr);
namespace{unsigned calls=0;bool response=false;const std::string* observed=nullptr;bool request(const std::string& message){++calls;observed=&message;return response;}}
int main(){unsigned checks=0,failures=0;auto require=[&](bool v){++checks;if(!v)++failures;};std::string message("A\0B",3);const auto original=message;Port port{request};require(sizeof(Port)==sizeof(Port::Request));require(empty.requestRetry==nullptr);require(!requestRuntimeStartupRecoveryRetry(empty,message));response=true;require(requestRuntimeStartupRecoveryRetry(port,message));require(observed==&message);require(calls==1);require(message==original&&message.size()==3);response=false;require(!requestRuntimeStartupRecoveryRetry(port,message));require(calls==2);require(!requestRuntimeStartupRecoveryRetry(empty,message)&&calls==2);
std::cout<<"Startup recovery standard: checks="<<checks<<" staticAssertions=7 callbackBytes="<<sizeof(Port::Request)<<" portBytes="<<sizeof(Port)<<" failures="<<failures<<" skipped=0\n";return failures?1:0;}
