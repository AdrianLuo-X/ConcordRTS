#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteStunState.h"
namespace concord::runtime::session_detail {
struct RuntimeGameSessionState;
const WorldObjectInstance* findObjectById(const RuntimeGameSessionState& state, int objectId);
WorldObjectInstance* findMutableObjectById(RuntimeGameSessionState& state, int objectId);
class RuntimeParasiteStunMutableHostQueries final : public RuntimeParasiteStunObjectQueries {
    RuntimeGameSessionState& state_;
public:
    explicit RuntimeParasiteStunMutableHostQueries(RuntimeGameSessionState& state) : state_(state) {}
    WorldObjectInstance* mutableObject(int objectId) const override {
        return findMutableObjectById(state_, objectId);
    }
};
class RuntimeParasiteStunReadHostQueries final : public RuntimeParasiteStunReadQueries {
    const RuntimeGameSessionState& state_;
public:
    explicit RuntimeParasiteStunReadHostQueries(const RuntimeGameSessionState& state) : state_(state) {}
    const WorldObjectInstance* object(int objectId) const override {
        return findObjectById(state_, objectId);
    }
};
}
