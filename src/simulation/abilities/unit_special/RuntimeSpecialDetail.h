#pragma once

#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/abilities/psionic/RuntimePsychicDetectionNoSensor.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Observer-only intelligence derived from authoritative orders. Never saved
// or fed back into targeting: load/reconnect simply rebuilds these snapshots.
struct RuntimePsychicIntent {
    int objectId = -1;
    int targetObjectId = -1;
    SpecialCell destination;
    bool attack = false;
};

inline std::vector<SpecialMechanismRenderSnapshot> runtimePsychicDetectionSnapshots(
    const std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    const std::vector<RuntimePsychicIntent>& intents,
    const std::string& observer,
    const SpecialAlliancePredicate& allied,
    const std::function<bool(const WorldObjectInstance&)>& eligible = {},
    bool readOnlyInputs = false
) {
    std::vector<SpecialMechanismRenderSnapshot> snapshots;
    if (observer.empty()) return snapshots;
    const auto isAllied = [&](const std::string& left, const std::string& right) {
        return left == right || (allied && allied(left, right));
    };
    // The old generic entry keeps its original interleaved first-ID capture.
    // Deferred capture is valid only while objects/rules/alliances are fixed;
    // the opted-in Shell callbacks are pure reads of this same call's state.
    const bool deferred = readOnlyInputs && !runtimePsychicNoSensorReference();
    using ObjectIndex = std::unordered_map<int, const WorldObjectInstance*>;
    std::optional<ObjectIndex> indexOwner;
    if (!deferred) indexOwner.emplace();
    std::vector<std::pair<const WorldObjectInstance*, int>> sensors;
    for (const auto& object : objects) {
        if (!deferred) indexOwner->emplace(object.instanceId, &object);
        if (!object.alive || object.health <= 0 || !object.poweredOperational
            || object.kind != WorldObjectKind::Structure
            || !isAllied(observer, object.owner)) continue;
        const auto* type = rules.findBuildingType(object.type);
        if (type && type->psychicDetectionRadius > 0)
            sensors.emplace_back(&object, type->psychicDetectionRadius);
    }
    if (sensors.empty()) {
        runtimeObservePsychicNoSensor(deferred, objects.size(), 0,
            indexOwner ? indexOwner->size() : 0);
        return snapshots;
    }
    if (deferred) {
        indexOwner.emplace();
        for (const auto& object : objects)
            indexOwner->emplace(object.instanceId, &object);
    }
    const ObjectIndex& byId = *indexOwner;
    for (const auto& intent : intents) {
        const auto actorIt = byId.find(intent.objectId);
        if (actorIt == byId.end()) continue;
        const auto& actor = *actorIt->second;
        if (!actor.alive || actor.health <= 0 || actor.dockedToObjectId >= 0
            || actor.owner.empty() || isAllied(observer, actor.owner)
            || (eligible && !eligible(actor))) continue;
        const bool detected = std::any_of(sensors.begin(), sensors.end(),
            [&](const auto& sensor) {
                const auto dx = static_cast<std::int64_t>(actor.x) - sensor.first->x;
                const auto dy = static_cast<std::int64_t>(actor.y) - sensor.first->y;
                return dx * dx + dy * dy <= static_cast<std::int64_t>(sensor.second) * sensor.second;
            });
        if (!detected) continue;
        SpecialCell destination = intent.destination;
        double targetLevel = actor.level;
        if (intent.targetObjectId >= 0) {
            const auto targetIt = byId.find(intent.targetObjectId);
            if (targetIt == byId.end() || !targetIt->second->alive) continue;
            destination = {targetIt->second->x, targetIt->second->y};
            targetLevel = targetIt->second->level;
        }
        if (destination.x < 0 || destination.y < 0
            || (destination.x == actor.x && destination.y == actor.y)) continue;
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "psychicDetection";
        snapshot.instanceId = snapshot.objectId = actor.instanceId;
        snapshot.targetObjectId = intent.targetObjectId;
        snapshot.owner = observer;
        snapshot.sourceCell = snapshot.cell = {actor.x, actor.y};
        snapshot.targetCell = snapshot.cell2 = destination;
        snapshot.sourceX = actor.x;
        snapshot.sourceY = actor.y;
        snapshot.sourceLevel = actor.level;
        snapshot.targetX = destination.x;
        snapshot.targetY = destination.y;
        snapshot.targetLevel = targetLevel;
        snapshot.hasBeamEndpoints = true;
        snapshot.phase = intent.attack ? "Attack" : "Move";
        snapshot.color = intent.attack ? "255,80,80" : "180,100,255";
        snapshots.push_back(std::move(snapshot));
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const auto& left, const auto& right) {
        return left.objectId < right.objectId;
    });
    runtimeObservePsychicNoSensor(deferred, objects.size(), sensors.size(), byId.size());
    return snapshots;
}
