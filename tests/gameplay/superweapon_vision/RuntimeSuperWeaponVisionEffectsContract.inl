#pragma once
#include "simulation/superweapons/RuntimeSuperWeaponVisionEffects.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/vision/RuntimeVision.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>
namespace superweapon_vision_model_detail {
inline std::string encode(const SuperWeaponEvent& e) {
 std::ostringstream o;
 o<<e.tick<<'|'<<e.instanceId<<'|'<<e.activeToken<<'|'<<e.sourceBuildingId<<'|'<<e.soundSourceObjectId<<'|'<<e.objectId<<'|'<<e.targetObjectId<<'|'<<e.value;
 for(const auto* s:{&e.owner,&e.superWeaponId,&e.kind,&e.state,&e.reason,&e.commandVerb,&e.soundIntent,&e.evaIntent,&e.animationIntent})o<<'|'<<std::quoted(*s);
 o<<'|'<<e.targetCell.x<<'|'<<e.targetCell.y<<'|'<<e.targetCell2.x<<'|'<<e.targetCell2.y;return o.str();
}
inline bool original(bool enabled,const SuperWeaponEvent& event,RuntimeVisionSystem& vision) {
 return enabled && (event.kind=="SpyPlaneRevealArea" || event.kind=="PsychicRevealArea")
  && vision.revealArea(event.owner,event.targetCell.x,event.targetCell.y,std::max(1,event.value));
}
inline bool equal(const RuntimeVisionSystem& a,const RuntimeVisionSystem& b) {
 auto aa=a.observerOwners(),bb=b.observerOwners();std::sort(aa.begin(),aa.end());std::sort(bb.begin(),bb.end());if(aa!=bb)return false;
 if(a.serializeFireReveals(19)!=b.serializeFireReveals(19) || a.sourceMemoHitsForDiagnostics()!=b.sourceMemoHitsForDiagnostics())return false;
 for(const auto& owner:aa) {
  const auto* x=a.statsFor(owner);const auto* y=b.statsFor(owner);if((x==nullptr)!=(y==nullptr))return false;
  if(x && std::tie(x->validCells,x->visibleCells,x->exploredCells,x->visionSources,x->spySatSources,x->gapGeneratorSources,x->gapShroudedCells,x->lastUpdateTick,x->revision)!=std::tie(y->validCells,y->visibleCells,y->exploredCells,y->visionSources,y->spySatSources,y->gapGeneratorSources,y->gapShroudedCells,y->lastUpdateTick,y->revision))return false;
  RuntimeVisionGridView v,w;const bool av=a.gridViewFor(owner,v),bv=b.gridViewFor(owner,w);if(av!=bv)return false;
  if(av) {
   if(v.cellCount!=w.cellCount || v.revision!=w.revision || v.layoutEpoch!=w.layoutEpoch)return false;
   for(auto pair:std::array<std::pair<const std::uint8_t*,const std::uint8_t*>,4>{{{v.validCells,w.validCells},{v.visibleCells,w.visibleCells},{v.exploredCells,w.exploredCells},{v.gapShroudedCells,w.gapShroudedCells}}}) {
    if((pair.first==nullptr)!=(pair.second==nullptr))return false;
    if(pair.first && std::memcmp(pair.first,pair.second,v.cellCount)!=0)return false;
   }
  }
  for(const auto& subject:{std::string("Alpha"),std::string("Beta"),std::string("New"),std::string(""),std::string(" alpha ")})if(a.isOwnerSharedWithObserver(owner,subject)!=b.isOwnerSharedWithObserver(owner,subject))return false;
 }
 return true;
}
inline bool runContract() {
 try {
  int checks=0,states=0,positive=0,negative=0,probes=0;
  const auto require=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
  SuperWeaponEvent probe;const auto blank=encode(probe);
#define PROBE(F,V) {auto e=probe;e.F=V;require(encode(e)!=blank,"event encoder leaf probe");++probes;}
  PROBE(tick,3);PROBE(instanceId,3);PROBE(activeToken,3);PROBE(sourceBuildingId,3);PROBE(soundSourceObjectId,3);PROBE(objectId,3);PROBE(targetObjectId,3);PROBE(value,3);
  PROBE(owner,"x");PROBE(superWeaponId,"x");PROBE(kind,"x");PROBE(state,"x");PROBE(reason,"x");PROBE(commandVerb,"x");PROBE(soundIntent,"x");PROBE(evaIntent,"x");PROBE(animationIntent,"x");PROBE(targetCell.x,3);PROBE(targetCell.y,3);PROBE(targetCell2.x,3);PROBE(targetCell2.y,3);
#undef PROBE
  require(probes==21,"all21 event leaves observed");
  GameWorld world;for(int y=0;y<=20;++y)for(int x=0;x<=20;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;world.accessTerrainCells().push_back(c);}
  std::array<RuntimeVisionSystem,3> seeds;
  for(int i=1;i<3;++i)seeds[i].update(world,nullptr,{{"Alpha",{"Beta"}},{"Beta",{}}},17);
  RuntimeVisionGridView valid;require(seeds[1].gridViewFor("Alpha",valid)&&valid.available()&&valid.validCells[10*512+10]!=0,"real positive terrain grid qualified");
  seeds[2].revealArea("Alpha",10,10,3);seeds[2].revealMap("Beta");seeds[2].resetShroudArea("Alpha",0,0,1);seeds[2].setSourceMemoEnabledForDiagnostics(false);
  const std::array<std::string,5> kinds={"SpyPlaneRevealArea","PsychicRevealArea","spyplanerevealarea","PsychicRevealArea ","Unrelated"};
  const std::array<std::string,4> owners={"Alpha"," alpha ","New",""};const std::array<SuperWeaponCell,4> cells={{{10,10},{0,0},{-2,-2},{21,21}}};const std::array<int,5> radii={std::numeric_limits<int>::min(),0,1,3,20};
  for(const auto& seed:seeds)for(bool enabled:{false,true})for(const auto& kind:kinds)for(const auto& owner:owners)for(auto cell:cells)for(int radius:radii) {
   auto a=seed,b=seed;SuperWeaponEvent e;e.tick=19;e.instanceId=37;e.activeToken=41;e.sourceBuildingId=43;e.soundSourceObjectId=47;e.objectId=53;e.targetObjectId=59;e.value=radius;e.owner=owner;e.superWeaponId="kept-id";e.kind=kind;e.state="kept-state";e.reason="kept-reason";e.commandVerb="kept-verb";e.soundIntent="kept-sound";e.evaIntent="kept-eva";e.animationIntent="kept-animation";e.targetCell=cell;e.targetCell2={7,13};const auto image=encode(e);
   const bool x=original(enabled,e,a),y=concord::simulation::superweapons::applyRuntimeSuperWeaponVisionEvent(enabled,e,b);++states;if(x)++positive;else ++negative;
   require(x==y,"original mutation result");require(equal(a,b),"complete canonical grids/stats/owners/shared/fire/source memo state");require(encode(e)==image,"all21 borrowed event leaves immutable");
   const bool xx=original(enabled,e,a),yy=concord::simulation::superweapons::applyRuntimeSuperWeaponVisionEvent(enabled,e,b);require(xx==yy&&!xx,"duplicate reveal false");require(equal(a,b),"duplicate full state");
   a.update(world,nullptr,{{"Alpha",{"Beta"}},{"Beta",{}}},20);b.update(world,nullptr,{{"Alpha",{"Beta"}},{"Beta",{}}},20);require(equal(a,b),"subsequent canonical update/source memo continuation");
  }
  require(states==2400&&positive>0&&negative>0,"complete unfiltered positive and negative matrix");
  std::cout<<"superweapon-vision checks="<<checks<<" states="<<states<<" event-leaf-probes="<<probes<<" positive="<<positive<<" negative="<<negative<<" complete-real-vision-and-continuation=exact failures=0"<<std::endl;return true;
 }catch(const std::exception& e){std::cerr<<"superweapon-vision failure: "<<e.what()<<std::endl;return false;}
}
}
