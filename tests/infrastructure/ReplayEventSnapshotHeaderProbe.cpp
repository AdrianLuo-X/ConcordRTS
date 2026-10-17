// The same source can probe the original compatibility header before extraction
// with CONCORD_REPLAY_SNAPSHOT_COMPAT_HEADER, then the narrow header afterward.
#if defined(CONCORD_REPLAY_SNAPSHOT_COMPAT_HEADER)
#include "runtime/host/RuntimeShell.h"
#else
#include "runtime/contracts/RuntimeReplayEventSnapshot.h"
#endif

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#if !defined(CONCORD_REPLAY_SNAPSHOT_COMPAT_HEADER) && (defined(_WINDOWS_) || defined(_INC_WINDOWS))
#error Narrow replay snapshot header must not transitively include windows.h
#endif

#define S12_CHECK_TYPE(member, expected) \
    static_assert(std::is_same_v<decltype(RuntimeReplayEventSnapshot::member), expected>)
static_assert(std::is_same_v<decltype(RuntimeSnapshotCell::x), int>);
static_assert(std::is_same_v<decltype(RuntimeSnapshotCell::y), int>);
S12_CHECK_TYPE(sequence, std::uint64_t);
S12_CHECK_TYPE(tick, int);
S12_CHECK_TYPE(kind, std::string);
S12_CHECK_TYPE(objectId, int);
S12_CHECK_TYPE(targetObjectId, int);
S12_CHECK_TYPE(projectileActorId, int);
S12_CHECK_TYPE(commandVerb, std::string);
S12_CHECK_TYPE(selectedCount, int);
S12_CHECK_TYPE(acceptedCount, int);
S12_CHECK_TYPE(rejectedCount, int);
S12_CHECK_TYPE(commandTargetCell, RuntimeSnapshotCell);
S12_CHECK_TYPE(missionFrom, std::string);
S12_CHECK_TYPE(missionTo, std::string);
S12_CHECK_TYPE(reason, std::string);
S12_CHECK_TYPE(weaponId, std::string);
S12_CHECK_TYPE(weaponSlot, std::string);
S12_CHECK_TYPE(projectileId, std::string);
S12_CHECK_TYPE(projectileImageId, std::string);
S12_CHECK_TYPE(warheadId, std::string);
S12_CHECK_TYPE(lifecycleState, std::string);
S12_CHECK_TYPE(spawnTick, int);
S12_CHECK_TYPE(impactTick, int);
S12_CHECK_TYPE(expiredTick, int);
S12_CHECK_TYPE(renderIntentId, std::string);
S12_CHECK_TYPE(renderIntentHash, std::uint64_t);
S12_CHECK_TYPE(trailerIntentId, std::string);
S12_CHECK_TYPE(impactIntentId, std::string);
S12_CHECK_TYPE(warheadImpactIntentId, std::string);
S12_CHECK_TYPE(sourceCell, RuntimeSnapshotCell);
S12_CHECK_TYPE(targetCell, RuntimeSnapshotCell);
S12_CHECK_TYPE(currentCell, RuntimeSnapshotCell);
S12_CHECK_TYPE(superWeaponId, std::string);
S12_CHECK_TYPE(superWeaponEvent, std::string);
S12_CHECK_TYPE(secondTargetCell, RuntimeSnapshotCell);
#undef S12_CHECK_TYPE

namespace {
// std::string-owning DTOs need not be standard-layout. Runtime member addresses
// expose the actual profile's offsets without non-standard-layout offsetof.
template<class Object, class Member>
void emitMember(const char* name, const Object& object, const Member& member) {
    const auto start = reinterpret_cast<std::uintptr_t>(std::addressof(object));
    const auto field = reinterpret_cast<std::uintptr_t>(std::addressof(member));
    std::cout << name << ".offset=" << field - start << " size=" << sizeof(Member) << '\n';
}
bool emptyCell(const RuntimeSnapshotCell& cell) { return cell.x == -1 && cell.y == -1; }
bool defaults(const RuntimeReplayEventSnapshot& e) {
    return e.sequence == 0 && e.tick == 0 && e.kind.empty()
        && e.objectId == -1 && e.targetObjectId == -1 && e.projectileActorId == -1
        && e.commandVerb.empty() && e.selectedCount == 0 && e.acceptedCount == 0 && e.rejectedCount == 0
        && emptyCell(e.commandTargetCell) && e.missionFrom.empty() && e.missionTo.empty() && e.reason.empty()
        && e.weaponId.empty() && e.weaponSlot.empty() && e.projectileId.empty()
        && e.projectileImageId.empty() && e.warheadId.empty() && e.lifecycleState.empty()
        && e.spawnTick == -1 && e.impactTick == -1 && e.expiredTick == -1
        && e.renderIntentId.empty() && e.renderIntentHash == 0 && e.trailerIntentId.empty()
        && e.impactIntentId.empty() && e.warheadImpactIntentId.empty()
        && emptyCell(e.sourceCell) && emptyCell(e.targetCell) && emptyCell(e.currentCell)
        && e.superWeaponId.empty() && e.superWeaponEvent.empty() && emptyCell(e.secondTargetCell);
}
} // namespace

int main() {
    RuntimeSnapshotCell cell;
    RuntimeReplayEventSnapshot event;
    if (!emptyCell(cell) || !defaults(event)) { std::cerr << "Replay snapshot defaults changed\n"; return 1; }
    std::cout << "RuntimeSnapshotCell.size=" << sizeof(cell) << " align=" << alignof(RuntimeSnapshotCell) << '\n';
    emitMember("cell.x", cell, cell.x); emitMember("cell.y", cell, cell.y);
    std::cout << "RuntimeReplayEventSnapshot.size=" << sizeof(event)
        << " align=" << alignof(RuntimeReplayEventSnapshot)
        << " vectorSize=" << sizeof(std::vector<RuntimeReplayEventSnapshot>) << '\n';
#if defined(_ITERATOR_DEBUG_LEVEL)
    std::cout << "iterator=" << _ITERATOR_DEBUG_LEVEL << '\n';
#else
    std::cout << "iterator=not-defined\n";
#endif
#if defined(_DEBUG)
    std::cout << "debugCRTMacro=1\n";
#else
    std::cout << "debugCRTMacro=0\n";
#endif
#define S12_EMIT(member) emitMember(#member, event, event.member)
    S12_EMIT(sequence); S12_EMIT(tick); S12_EMIT(kind); S12_EMIT(objectId);
    S12_EMIT(targetObjectId); S12_EMIT(projectileActorId); S12_EMIT(commandVerb);
    S12_EMIT(selectedCount); S12_EMIT(acceptedCount); S12_EMIT(rejectedCount);
    S12_EMIT(commandTargetCell); S12_EMIT(missionFrom); S12_EMIT(missionTo); S12_EMIT(reason);
    S12_EMIT(weaponId); S12_EMIT(weaponSlot); S12_EMIT(projectileId); S12_EMIT(projectileImageId);
    S12_EMIT(warheadId); S12_EMIT(lifecycleState); S12_EMIT(spawnTick); S12_EMIT(impactTick);
    S12_EMIT(expiredTick); S12_EMIT(renderIntentId); S12_EMIT(renderIntentHash);
    S12_EMIT(trailerIntentId); S12_EMIT(impactIntentId); S12_EMIT(warheadImpactIntentId);
    S12_EMIT(sourceCell); S12_EMIT(targetCell); S12_EMIT(currentCell);
    S12_EMIT(superWeaponId); S12_EMIT(superWeaponEvent); S12_EMIT(secondTargetCell);
#undef S12_EMIT
    std::cout << "defaults=preserved\n";
    return 0;
}
