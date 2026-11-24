#pragma once
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"
#include <string>
#include <vector>
struct WorldObjectInstance;
struct MindControlOverloadDamageEvent;
struct RuntimeSpatialAudioPosition;
namespace concord::simulation::psionic {
// The two original fields stay live in the owning rules object for this call.
struct RuntimeMindControlOverloadRulesView {
    const std::string& c4Warhead;
    const std::string& masterMindOverloadDeathSound;
};
class RuntimeMindControlOverloadQueries {
public:
    virtual ~RuntimeMindControlOverloadQueries()=default;
    virtual std::vector<MindControlOverloadDamageEvent> collectPulses()=0;
    virtual RuntimeMindControlOverloadRulesView rulesView()=0;
    virtual WorldObjectInstance* findController(int objectId)=0;
    virtual bool ignoresDamage(const WorldObjectInstance&)=0;
    virtual bool protectionInitialized()=0;
    virtual bool isProtected(int objectId,int tick)=0;
    virtual int damageAgainst(const WorldObjectInstance&,int damage,const std::string& warhead)=0;
    virtual RuntimeSpatialAudioPosition spatialPosition(const WorldObjectInstance&)=0;
};
class RuntimeMindControlOverloadEffects {
public:
    virtual ~RuntimeMindControlOverloadEffects()=default;
    virtual void revealAfterDamage(WorldObjectInstance&,const std::string& reason)=0;
    virtual void emitSound(const std::string& token,RuntimeSpatialAudioPosition)=0;
    // Translate this one command to the existing Destroyed exit request, with
    // its original mission/reason/default fields. Do not drain inside it.
    virtual void queueDestroyedExit(int objectId,int tick)=0;
    virtual bool drainExits()=0;
};
// Reuse the original pulse kernel; this owns only its response decisions.
// Borrow controller storage, rule fields, tick and sink synchronously. Helpers
// may change their values, but must preserve the original borrowed storage.
// Exceptions preserve the original partial effects; there is no rollback/retry.
bool advanceRuntimeMindControlOverloadEffects(const int& tick,
    const SpecialEventSink& sink,RuntimeMindControlOverloadQueries& queries,
    RuntimeMindControlOverloadEffects& effects);
}
