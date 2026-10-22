#include "simulation/superweapons/RuntimeChronosphereRelocation.h"

#include "world/queries/RuntimeObjectIndex.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {
// Independently transcribed from the frozen pre-S15 applyChrono statements.
// Expected behavior does not call the new implementation or its pose helper.
RuntimeChronospherePose oldPose(const WorldObjectInstance& o) {
    RuntimeChronospherePose p;
    p.x=o.x; p.y=o.y; p.cell=o.cell; p.level=o.level; p.subCell=o.subCell;
    p.onBridge=o.onBridge; p.airborne=o.airborne; p.hasRenderPosition=o.hasRenderPosition;
    p.renderMoveActive=o.renderMoveActive; p.renderOnBridge=o.renderOnBridge;
    p.renderX=o.renderX; p.renderY=o.renderY; p.renderLevel=o.renderLevel;
    return p;
}
RuntimeChronosphereRelocationResult original(
    WorldObjectInstance* object, const RuntimeChronosphereRelocationRequest& q,
    RuntimeChronosphereRelocationPort& port) {
    RuntimeChronosphereRelocationResult r; r.request=q;
    if (!object) return r;
    r.oldPose=oldPose(*object);
    object->x=q.targetX;
    object->y=q.targetY;
    object->cell=q.targetCell;
    if (port.hasTerrainLevel()) object->level=port.terrainLevel(q.targetX,q.targetY);
    if(q.arrival==RuntimeChronosphereArrival::FatalWater) {
        object->onBridge=object->renderOnBridge=false;
        object->hasRenderPosition=false;
        object->renderMoveActive=false;
    } else {
        object->hasRenderPosition=false;
        object->renderMoveActive=false;
        object->mission=object->mission.empty()?"Guard":object->mission;
    }
    r.applied=true; r.newPose=oldPose(*object); r.healthAtNotification=object->health;
    port.onRelocated(r);
    return r;
}
struct Port final : RuntimeChronosphereRelocationPort {
    bool available=true;
    int throwAt=0;
    int queries=0;
    int notifications=0;
    WorldObjectInstance* object=nullptr;
    std::vector<RuntimeChronosphereRelocationResult> events;
    std::vector<std::tuple<int,int,int,int,int,bool,bool,std::string>> trace;
    std::function<void()> nested;
    bool hasTerrainLevel() const noexcept override {return available;}
    int terrainLevel(int x,int y) override {
        ++queries;
        trace.emplace_back(0,x,y,object->x,object->cell,
            object->hasRenderPosition,object->renderMoveActive,object->mission);
        if(throwAt==1) throw std::runtime_error("original terrain failure");
        return 7;
    }
    void onRelocated(const RuntimeChronosphereRelocationResult& r) override {
        ++notifications; events.push_back(r);
        trace.emplace_back(1,r.request.targetX,r.request.targetY,object->x,object->cell,
            object->hasRenderPosition,object->renderMoveActive,object->mission);
        if(nested) nested();
        if(throwAt==2) throw std::runtime_error("original notification failure");
    }
};
auto body(const WorldObjectInstance& o) {
    return std::tuple{oldPose(o),o.instanceId,o.kind,o.mapId,o.owner,o.type,o.health,
        o.alive,o.mission,o.tag,o.facing,o.turretFacing,o.veterancy,o.veterancyExperience,
        o.crateArmorMultiplier,o.crateFirepowerMultiplier,o.crateSpeedMultiplier,
        o.transportPassengerIds,o.weaponCooldownTicksRemaining,o.submerged,o.cloaked,
        o.currentAmmo,o.movementAnimationDistance,o.tankBunkerHostId};
}
}

int main() {
    int checks=0,cases=0,failures=0,partials=0,nested=0;
    const auto need=[&](bool ok){++checks;if(!ok)++failures;};
    for(int missing=0;missing<2;++missing) for(int same=0;same<2;++same)
    for(int fatal=0;fatal<2;++fatal) for(int terrain=0;terrain<2;++terrain)
    for(int empty=0;empty<2;++empty) for(int stage=0;stage<3;++stage)
    for(int bridge=0;bridge<2;++bridge) {
        ++cases; WorldObjectInstance a;
        a.instanceId=104;a.kind=WorldObjectKind::Unit;a.type="MTNK";a.owner="Commander";
        a.mapId="frozen-original";a.x=90;a.y=91;a.cell=91090;a.level=2;a.subCell=3;
        a.health=287;a.mission=empty?"":"Move";a.alive=true;a.airborne=false;
        a.hasRenderPosition=a.renderMoveActive=true;a.onBridge=a.renderOnBridge=bridge!=0;
        a.renderX=89.125;a.renderY=90.75;a.renderLevel=8.5;a.facing=11;a.turretFacing=7;
        a.transportPassengerIds={800,901};a.veterancyExperience=33;a.currentAmmo=4;
        a.crateSpeedMultiplier=1.25;a.tag="unchanged";a.weaponCooldownTicksRemaining=8;
        WorldObjectInstance b=a;Port x,y;x.available=y.available=terrain!=0;
        x.throwAt=y.throwAt=stage;x.object=&a;y.object=&b;
        RuntimeChronosphereRelocationRequest q;
        q.objectId=104;q.oldCellX=90;q.oldCellY=91;
        q.targetX=same?90:104;q.targetY=same?91:100;q.targetCell=q.targetY*1000+q.targetX;
        q.arrival=fatal?RuntimeChronosphereArrival::FatalWater:RuntimeChronosphereArrival::Formation;
        RuntimeChronosphereRelocationResult oldResult,newResult;std::string oldError,newError;
        try{oldResult=original(missing?nullptr:&a,q,x);}catch(const std::exception& e){oldError=e.what();}
        try{newResult=commitRuntimeChronosphereRelocation(missing?nullptr:&b,q,y);}catch(const std::exception& e){newError=e.what();}
        need(body(a)==body(b));need(oldResult==newResult);need(oldError==newError);
        need(x.trace==y.trace);need(x.events==y.events);need(x.queries==y.queries);
        need(x.notifications==y.notifications);
        if(missing){need(!newResult.applied&&y.queries==0&&y.notifications==0);}
        else if(stage==1&&terrain){++partials;need(b.x==q.targetX&&b.y==q.targetY&&b.cell==q.targetCell);
            need(b.hasRenderPosition&&b.renderMoveActive&&y.notifications==0&&b.level==2);}
        else{need(y.notifications==1);need(!b.hasRenderPosition&&!b.renderMoveActive);
            need(b.subCell==3&&b.renderX==89.125&&b.renderY==90.75&&b.renderLevel==8.5);}
    }
    // Reentrant query/commit; all vector borrows end before storage replacement.
    std::vector<WorldObjectInstance> objects(2);objects[0].instanceId=104;objects[1].instanceId=900;
    objects[0].x=objects[1].x=5;objects[0].y=objects[1].y=6;objects[0].health=objects[1].health=100;
    RuntimeObjectIndex index;need(index.find(objects,104)==&objects[0]);
    RuntimeChronosphereRelocationRequest q{104,5,6,10,11,11010,RuntimeChronosphereArrival::Formation};
    Port p,other;p.available=other.available=false;p.object=&objects[0];other.object=&objects[1];
    p.nested=[&]{++nested;need(index.find(objects,104)->x==10);
        auto q2=q;q2.objectId=900;q2.targetX=12;q2.targetCell=11012;
        const auto r=commitRuntimeChronosphereRelocation(index.find(objects,900),q2,other);
        need(r.applied&&index.find(objects,900)->x==12);};
    const auto owned=commitRuntimeChronosphereRelocation(index.find(objects,104),q,p);
    need(p.notifications==1&&other.notifications==1&&owned.newPose.x==10);
    auto replacement=objects;std::reverse(replacement.begin(),replacement.end());objects.swap(replacement);
    need(index.find(objects,104)->x==10&&index.find(objects,900)->x==12);
    need(owned.oldPose.x==5&&owned.newPose.x==10&&owned.request.objectId==104);
    objects.clear();need(index.find(objects,104)==nullptr);need(owned.newPose.x==10);
    std::cout<<"Chronosphere relocation contract: cases="<<cases<<" checks="<<checks
        <<" partialTerrainFailures="<<partials<<" nested="<<nested<<" failures="<<failures<<'\n';
    return failures?1:0;
}
