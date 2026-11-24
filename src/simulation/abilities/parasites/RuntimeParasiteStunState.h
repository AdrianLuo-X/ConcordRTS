#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteValues.h"
#include <unordered_map>
#include <utility>
#include <cstdint>
#include <iosfwd>
#include <vector>
struct WorldObjectInstance;
class RuntimeParasiteStunObjectQueries {
public:
    virtual ~RuntimeParasiteStunObjectQueries() = default;
    virtual WorldObjectInstance* mutableObject(int objectId) const = 0;
};
class RuntimeParasiteStunReadQueries {
public:
    virtual ~RuntimeParasiteStunReadQueries() = default;
    virtual const WorldObjectInstance* object(int objectId) const = 0;
};
class RuntimeParasiteStunState {
public:
    using Container = std::unordered_map<int, int>;
    using Snapshot = std::vector<std::pair<int, int>>;
    bool isActive(int objectId, const int& currentTick) const;
    void applyEjectionStun(const WorldObjectInstance& source, const int& currentTick,
        const SpecialMechanismAction& action);
    void advanceExpired(const int& currentTick, const RuntimeParasiteStunObjectQueries& queries, bool& changed);
    Snapshot saveSnapshot(const RuntimeParasiteStunReadQueries& queries, const int& currentTick) const;
    Snapshot persistentSnapshot(const RuntimeParasiteStunReadQueries& queries, const int& currentTick) const;
    static void writeSaveSection(std::ostream& output, const Snapshot& parasiteStuns);
    static void mixPersistentHash(std::uint64_t& hash, const Snapshot& parasiteStuns);
    void mixAuthorityHash(std::uint64_t& hash) const;
    const Container& entries() const noexcept { return untilTickByObject_; }
    void restoreValidated(Container&& values) noexcept(noexcept(untilTickByObject_ = std::move(values))) {
        untilTickByObject_ = std::move(values);
    }
    void swap(RuntimeParasiteStunState& other) noexcept { untilTickByObject_.swap(other.untilTickByObject_); }
    friend void swap(RuntimeParasiteStunState& left, RuntimeParasiteStunState& right) noexcept { left.swap(right); }
private:
    Container untilTickByObject_;
};
