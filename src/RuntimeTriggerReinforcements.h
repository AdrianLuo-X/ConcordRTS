#pragma once
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

struct RuntimeReinforcementMember {
    std::string typeId;
    int count = 0;
};

struct RuntimeTriggerReinforcement {
    int instanceId = 0;
    std::string teamTypeId;
    std::string owner;
    int originX = 0;
    int originY = 0;
    int actionType = 0;
    int nextAttemptTick = 0;
    bool teamCounted = false;
    std::vector<RuntimeReinforcementMember> members;
    std::size_t memberIndex = 0;
    int memberProduced = 0;
    // Includes delivered casualties; delivery progress must never respawn them.
    std::vector<int> objectIds;
};

struct RuntimeTriggerReinforcements {
    int nextInstanceId = 1;
    std::map<int, RuntimeTriggerReinforcement> pending;
    friend void swap(RuntimeTriggerReinforcements& left, RuntimeTriggerReinforcements& right) noexcept {
        using std::swap;
        swap(left.nextInstanceId, right.nextInstanceId);
        left.pending.swap(right.pending);
    }
};

inline std::string serializeRuntimeTriggerReinforcements(const RuntimeTriggerReinforcements& state) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "TriggerReinforcements 1 " << state.nextInstanceId << ' ' << state.pending.size() << '\n';
    for (const auto& [id, job] : state.pending) {
        out << id << ' ' << std::quoted(job.teamTypeId) << ' ' << std::quoted(job.owner) << ' '
            << job.originX << ' ' << job.originY << ' ' << job.actionType << ' '
            << job.nextAttemptTick << ' ' << job.teamCounted << ' '
            << job.memberIndex << ' ' << job.memberProduced << ' ' << job.members.size();
        for (const auto& member : job.members) out << ' ' << std::quoted(member.typeId) << ' ' << member.count;
        out << ' ' << job.objectIds.size();
        for (int objectId : job.objectIds) out << ' ' << objectId;
        out << '\n';
    }
    return out.str();
}

// Decode into a temporary so a rejected save/snapshot cannot partially change
// the live queue. Limits bound untrusted input; they do not truncate a team.
inline bool deserializeRuntimeTriggerReinforcements(
    const std::string& text, RuntimeTriggerReinforcements& result, std::string& error
) {
    const auto fail = [&]() { error = "invalid pending trigger reinforcement state"; return false; };
    if (text.size() > 32U * 1024U * 1024U) return fail();
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    RuntimeTriggerReinforcements staged;
    std::string magic;
    int schema = 0;
    std::size_t count = 0;
    if (!(in >> magic >> schema >> staged.nextInstanceId >> count)
        || magic != "TriggerReinforcements" || schema != 1 || staged.nextInstanceId < 1
        || count > 4096) return fail();
    std::set<int> allObjects;
    for (std::size_t index = 0; index < count; ++index) {
        RuntimeTriggerReinforcement job;
        std::size_t members = 0, objects = 0;
        int counted = 0;
        if (!(in >> job.instanceId >> std::quoted(job.teamTypeId) >> std::quoted(job.owner)
                >> job.originX >> job.originY >> job.actionType >> job.nextAttemptTick >> counted
                >> job.memberIndex >> job.memberProduced >> members)
            || job.instanceId <= 0 || job.instanceId >= staged.nextInstanceId
            || job.teamTypeId.empty() || job.teamTypeId.size() > 256
            || job.owner.empty() || job.owner.size() > 256
            || job.originX < 0 || job.originY < 0 || job.originX > 1023 || job.originY > 1023
            || job.nextAttemptTick < 0 || (counted != 0 && counted != 1)
            || members == 0 || members > 4096 || job.memberIndex > members
            || (job.memberIndex == members && (job.actionType != 107 || job.memberProduced != 0))
            || job.memberProduced < 0) return fail();
        job.teamCounted = counted != 0;
        std::int64_t delivered = 0;
        for (std::size_t memberIndex = 0; memberIndex < members; ++memberIndex) {
            RuntimeReinforcementMember member;
            if (!(in >> std::quoted(member.typeId) >> member.count)
                || member.typeId.empty() || member.typeId.size() > 256 || member.count <= 0) return fail();
            if (memberIndex < job.memberIndex) delivered += member.count;
            job.members.push_back(std::move(member));
        }
        if (job.memberIndex < members && job.memberProduced >= job.members[job.memberIndex].count) return fail();
        delivered += job.memberProduced;
        if (!(in >> objects) || objects > 1000000 || static_cast<std::int64_t>(objects) != delivered
            || job.teamCounted != (objects != 0)) return fail();
        for (std::size_t objectIndex = 0; objectIndex < objects; ++objectIndex) {
            int id = 0;
            if (!(in >> id) || id <= 0 || !allObjects.insert(id).second) return fail();
            job.objectIds.push_back(id);
        }
        if (!staged.pending.emplace(job.instanceId, std::move(job)).second) return fail();
    }
    in >> std::ws;
    if (!in.eof()) return fail();
    result = std::move(staged);
    error.clear();
    return true;
}
