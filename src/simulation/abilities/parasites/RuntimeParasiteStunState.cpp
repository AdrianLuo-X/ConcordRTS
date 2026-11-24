#include "RuntimeParasiteStunState.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <ostream>
#include <vector>
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
using runtime_authority_hash::runtimeStableHashMix;
using runtime_authority_hash::runtimeStableHashMixString;
bool RuntimeParasiteStunState::isActive(int objectId, const int& currentTick) const {
    const auto stun = untilTickByObject_.find(objectId);
    return stun != untilTickByObject_.end() && currentTick < stun->second;
}
void RuntimeParasiteStunState::applyEjectionStun(const WorldObjectInstance& source,
    const int& currentTick, const SpecialMechanismAction& action) {
            if (action.stunTicks > 0) {
                untilTickByObject_[source.instanceId] =
                    std::max(
                        currentTick,
                        action.tick
                    ) + action.stunTicks;
            } else {
                untilTickByObject_.erase(
                    source.instanceId
                );
            }
}
void RuntimeParasiteStunState::advanceExpired(const int& currentTick, const RuntimeParasiteStunObjectQueries& queries, bool& changed) {
    for (auto stun = untilTickByObject_.begin();
         stun != untilTickByObject_.end();) {
        if (currentTick < stun->second) {
            ++stun;
            continue;
        }
        WorldObjectInstance* object = queries.mutableObject(stun->first);
        if (object != nullptr && object->alive
            && object->mission == "ParasiteStunned") {
            object->mission = "Guard";
        }
        stun = untilTickByObject_.erase(stun);
        changed = true;
    }

}

RuntimeParasiteStunState::Snapshot RuntimeParasiteStunState::saveSnapshot(const RuntimeParasiteStunReadQueries& queries, const int& currentTick) const {
        std::vector<std::pair<int, int>> parasiteStuns;
        parasiteStuns.reserve(
            untilTickByObject_.size()
        );
        for (const auto& [objectId, untilTick] :
                untilTickByObject_) {
            const WorldObjectInstance* object = queries.object(objectId);
            if (object != nullptr && object->alive
                && object->health > 0 && untilTick > currentTick) {
                parasiteStuns.emplace_back(objectId, untilTick);
            }
        }
        std::sort(parasiteStuns.begin(), parasiteStuns.end());
        return parasiteStuns;
}

RuntimeParasiteStunState::Snapshot RuntimeParasiteStunState::persistentSnapshot(const RuntimeParasiteStunReadQueries& queries, const int& currentTick) const {
    std::vector<std::pair<int, int>> parasiteStuns;
    parasiteStuns.reserve(
        untilTickByObject_.size()
    );
    for (const auto& [objectId, untilTick] :
            untilTickByObject_) {
        const WorldObjectInstance* object = queries.object(objectId);
        if (object != nullptr && object->alive && object->health > 0
            && object->mission == "ParasiteStunned"
            && untilTick > currentTick) {
            parasiteStuns.emplace_back(objectId, untilTick);
        }
    }
    std::sort(parasiteStuns.begin(), parasiteStuns.end());
    return parasiteStuns;
}

void RuntimeParasiteStunState::writeSaveSection(std::ostream& output, const Snapshot& parasiteStuns) {
        output << "\n[ParasiteStuns]\n";
        output << "Schema=1\n";
        output << "Count=" << parasiteStuns.size() << "\n";
        for (std::size_t index = 0; index < parasiteStuns.size(); ++index) {
            output << index << "=" << parasiteStuns[index].first << "|"
                   << parasiteStuns[index].second << "\n";
        }
}

void RuntimeParasiteStunState::mixPersistentHash(std::uint64_t& hash, const Snapshot& parasiteStuns) {
    runtimeStableHashMixString(hash, "ParasiteStuns/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(parasiteStuns.size())
    );
    for (const auto& [objectId, untilTick] : parasiteStuns) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, untilTick);
    }
}

void RuntimeParasiteStunState::mixAuthorityHash(std::uint64_t& hash) const {
        std::vector<std::pair<int, int>> sorted(
            untilTickByObject_.begin(),
            untilTickByObject_.end()
        );
        std::sort(sorted.begin(), sorted.end());
        runtimeStableHashMix(hash, static_cast<long long>(sorted.size()));
        for (const auto& [key, value] : sorted) {
            runtimeStableHashMix(hash, key);
            runtimeStableHashMix(hash, value);
        }
}
