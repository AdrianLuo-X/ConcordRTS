#include "simulation/lifecycle/RuntimeObjectExitValues.h"
#include "simulation/abilities/parasites/RuntimeParasiteActionResult.h"
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
namespace lifecycle_values_model_detail {
namespace original {
enum class RuntimeObjectExitKind : std::uint8_t {
    Destroyed = 0,
    TemporalDestroyed,
    Sold,
    Consumed,
    TransformSource,
    ScriptRemoved
};

struct RuntimeObjectExitRequest {
    int objectId = -1;
    RuntimeObjectExitKind kind = RuntimeObjectExitKind::Destroyed;
    int tick = -1;
    std::string mission;
    std::string reason;
    int sourceObjectId = -1;
    std::string sourceOwner;
    RuntimeDeathWeaponCause deathWeaponCause;
    bool suppressDeathWeapon = false;
    bool destructionEffectsSpawned = false;
    // Local presentation only; pending exits are drained before save/hash.
    bool unitDeathSoundRequested = false;
    bool unitDeathSoundHandled = false;
    std::optional<RuntimeSpatialAudioPosition> unitDeathSoundPosition;
    bool suppressCrew = false;
    bool c4Explosion = false;
    std::string warheadId;
    // Module damage callbacks can mark the victim inactive before the host
    // drains its exit. Preserve that it was alive for this lethal hit.
    bool wasAliveBeforeDamage = false;
};

enum class RuntimeObjectExitQueueResult : std::uint8_t {
    Queued = 0,
    DuplicatePending,
    AlreadyFinalized,
    ObjectMissing
};

struct RuntimeParasiteActionApplyResult {
    bool accepted = true;
    bool changed = false;
};
}
namespace current=concord::runtime::session_detail;
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class T>std::string encode(const T& v){
 const auto b=[](bool x)->std::string{return x?"1|":"0|";};const auto text=[](const std::string& x){return std::to_string(x.size())+":"+x+"|";};
 std::string s=std::to_string(v.objectId)+"|"+std::to_string(static_cast<unsigned>(v.kind))+"|"+std::to_string(v.tick)+"|"+text(v.mission)+text(v.reason)+std::to_string(v.sourceObjectId)+"|"+text(v.sourceOwner);
 s+=std::to_string(v.deathWeaponCause.cascadeId)+"|"+std::to_string(v.deathWeaponCause.parentRequestId)+"|"+std::to_string(v.deathWeaponCause.parentDepth)+"|";
 s+=b(v.suppressDeathWeapon)+b(v.destructionEffectsSpawned)+b(v.unitDeathSoundRequested)+b(v.unitDeathSoundHandled)+b(v.unitDeathSoundPosition.has_value());
 if(v.unitDeathSoundPosition)s+=std::to_string(std::bit_cast<std::uint64_t>(v.unitDeathSoundPosition->x))+"|"+std::to_string(std::bit_cast<std::uint64_t>(v.unitDeathSoundPosition->y))+"|"+std::to_string(std::bit_cast<std::uint64_t>(v.unitDeathSoundPosition->level))+"|";
 return s+b(v.suppressCrew)+b(v.c4Explosion)+text(v.warheadId)+b(v.wasAliveBeforeDamage);
}
template<class T>void mutate(T& v,int field){switch(field){
 case 0:v.objectId=-902;break;
 case 1:v.kind=static_cast<decltype(v.kind)>(255);break;
 case 2:v.tick=std::numeric_limits<int>::max();break;
 case 3:v.mission=std::string(96,'m');break;
 case 4:v.reason=std::string(97,'r');break;
 case 5:v.sourceObjectId=17003;break;
 case 6:v.sourceOwner=std::string(98,'o');break;
 case 7:v.deathWeaponCause={std::numeric_limits<std::uint64_t>::max(),987654321,99999};break;
 case 8:v.suppressDeathWeapon=true;break;
 case 9:v.destructionEffectsSpawned=true;break;
 case 10:v.unitDeathSoundRequested=true;break;
 case 11:v.unitDeathSoundHandled=true;break;
 case 12:v.unitDeathSoundPosition=RuntimeSpatialAudioPosition{-0.25,23.5,std::bit_cast<double>(std::uint64_t{0x7ff8000000001234})};break;
 case 13:v.suppressCrew=true;break;
 case 14:v.c4Explosion=true;break;
 case 15:v.warheadId=std::string(99,'w');break;
 case 16:v.wasAliveBeforeDamage=true;break;
 default:throw std::runtime_error("field");}}
template<class T>std::array<std::ptrdiff_t,17> offsets(){T v;const auto at=[&](auto& m){return reinterpret_cast<const char*>(&m)-reinterpret_cast<const char*>(&v);};return {at(v.objectId),at(v.kind),at(v.tick),at(v.mission),at(v.reason),at(v.sourceObjectId),at(v.sourceOwner),at(v.deathWeaponCause),at(v.suppressDeathWeapon),at(v.destructionEffectsSpawned),at(v.unitDeathSoundRequested),at(v.unitDeathSoundHandled),at(v.unitDeathSoundPosition),at(v.suppressCrew),at(v.c4Explosion),at(v.warheadId),at(v.wasAliveBeforeDamage)};}
bool runContract(){checks=0;try{
 using C=current::RuntimeObjectExitRequest;using O=original::RuntimeObjectExitRequest;
 static_assert(std::is_same_v<std::underlying_type_t<current::RuntimeObjectExitKind>,std::uint8_t>);
 static_assert(std::is_same_v<std::underlying_type_t<current::RuntimeObjectExitQueueResult>,std::uint8_t>);
 require(sizeof(C)==sizeof(O)&&alignof(C)==alignof(O),"complete request layout");require(offsets<C>()==offsets<O>(),"all17 member offsets");
 require(sizeof(current::RuntimeObjectExitKind)==sizeof(original::RuntimeObjectExitKind),"kind width");require(sizeof(current::RuntimeObjectExitQueueResult)==sizeof(original::RuntimeObjectExitQueueResult),"queue width");
 require(sizeof(current::RuntimeParasiteActionApplyResult)==sizeof(original::RuntimeParasiteActionApplyResult)&&alignof(current::RuntimeParasiteActionApplyResult)==alignof(original::RuntimeParasiteActionApplyResult),"fullresultlayout");
 current::RuntimeParasiteActionApplyResult ca;original::RuntimeParasiteActionApplyResult oa;require(ca.accepted==oa.accepted&&ca.changed==oa.changed,"resultdefault");
 for(int mask=0;mask<4;++mask){ca.accepted=(mask&1)!=0;ca.changed=(mask&2)!=0;oa.accepted=ca.accepted;oa.changed=ca.changed;auto copied=ca;auto moved=std::move(copied);require(moved.accepted==oa.accepted&&moved.changed==oa.changed,"allresultvalues");}
 constexpr std::array kinds{current::RuntimeObjectExitKind::Destroyed,current::RuntimeObjectExitKind::TemporalDestroyed,current::RuntimeObjectExitKind::Sold,current::RuntimeObjectExitKind::Consumed,current::RuntimeObjectExitKind::TransformSource,current::RuntimeObjectExitKind::ScriptRemoved};for(unsigned i=0;i<kinds.size();++i)require(static_cast<unsigned>(kinds[i])==i,"all6knownkinds");
 constexpr std::array queues{current::RuntimeObjectExitQueueResult::Queued,current::RuntimeObjectExitQueueResult::DuplicatePending,current::RuntimeObjectExitQueueResult::AlreadyFinalized,current::RuntimeObjectExitQueueResult::ObjectMissing};for(unsigned i=0;i<queues.size();++i)require(static_cast<unsigned>(queues[i])==i,"all4queuevalues");
 for(unsigned i=0;i<256;++i){require(static_cast<unsigned>(static_cast<current::RuntimeObjectExitKind>(i))==static_cast<unsigned>(static_cast<original::RuntimeObjectExitKind>(i)),"rawkindbyte");require(static_cast<unsigned>(static_cast<current::RuntimeObjectExitQueueResult>(i))==static_cast<unsigned>(static_cast<original::RuntimeObjectExitQueueResult>(i)),"rawqueuebyte");}
 C cd;O od;const auto defaultBytes=encode(od);require(encode(cd)==defaultBytes,"all17defaultsandnested23scalars");
 for(int mode=0;mode<18;++mode){C c;O o;if(mode<17){mutate(c,mode);mutate(o,mode);require(encode(c)!=defaultBytes,"all17encoderprobes");}else for(int field=0;field<17;++field){mutate(c,field);mutate(o,field);}const auto expected=encode(o);require(encode(c)==expected,"allfields");auto copied=c;C assigned;assigned=c;require(encode(copied)==expected&&encode(assigned)==expected,"ownedcopyandassignment");c.mission="changed";c.reason="changed";c.sourceOwner="changed";c.warheadId="changed";c.unitDeathSoundPosition.reset();require(encode(copied)==expected&&encode(assigned)==expected,"copyindependent");auto moved=std::move(copied);C moveAssigned;moveAssigned=std::move(assigned);require(encode(moved)==expected&&encode(moveAssigned)==expected,"moves preserveallownedvalues");}
 std::cout<<"lifecycle-values checks="<<checks<<" request-fields=17 kind-values=6 queue-values=4 apply-fields=2 raw-enum-bytes=512 request-bytes="<<sizeof(C)<<" request-align="<<alignof(C)<<" original-layout-default-copy-move-and-owned-values=exact failures=0"<<std::endl;return true;
 }catch(const std::exception& e){std::cerr<<"lifecycle-values failure: "<<e.what()<<std::endl;return false;}}
} // namespace lifecycle_values_model_detail
