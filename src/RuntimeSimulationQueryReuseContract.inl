// Include after the runtime path builders, batch helpers and the existing
// RuntimePathSnapshotOccupancyContract. The standalone test uses primitives.
#include "RuntimeSimulationQueryReusePrimitiveContract.inl"
#include "RuntimePathTerrainQueryScopeContract.inl"
#include "RuntimePlannerInvisibleBuildingContract.inl"
#include "RuntimeBuildingDepartureReadOnlyQueryContract.inl"

#include <array>
#include <bit>
#include <limits>

bool runRuntimeSimulationQueryReuseContract(std::string& error) {
    if (!runRuntimeBuildingDepartureReadOnlyQueryContract(error)) return false;
    if (!runRuntimePlannerInvisibleBuildingContract(error)) return false;
    if (!runRuntimeSimulationQueryReusePrimitiveContract(error)) return false;
    const auto fail = [&](const std::string& why) {
        error = "simulation query reuse runtime: " + why;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    RuntimeSnapshotActiveStateScope active(state);
    IniFile ini;
    if (!ini.loadFromString("[VehicleTypes]\n0=CAR\n1=BOAT\n2=PLANE\n[InfantryTypes]\n0=MAN\n"
        "[CAR]\nStrength=200\nSpeed=6\nSpeedType=Track\nMovementZone=Normal\nCrusher=yes\n"
        "[BOAT]\nStrength=200\nSpeed=6\nSpeedType=FloatBeach\nMovementZone=Water\nNaval=yes\n"
        "[PLANE]\nStrength=200\nSpeed=6\nSpeedType=Winged\nMovementZone=Fly\n"
        "[MAN]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Infantry\n")
        || !state.rules.loadFromIni(ini)) return fail("fixture rules");
    state.rulesLoaded = true; state.config.fixedTickHz = 30; state.tick = 12;
    state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true; state.visionEnabled = false;
    const auto add = [&](int id, WorldObjectKind kind, const char* type, bool alive = true) {
        WorldObjectInstance value;
        value.instanceId = id; value.kind = kind; value.type = type;
        value.owner = id % 2 ? "Player" : "Enemy";
        value.alive = alive; value.health = alive ? 100 : 0; value.mission = "Guard";
        value.x = id % 13; value.y = id % 11; value.cell = runtimeCellKey(value.x, value.y);
        state.world.accessObjects().push_back(std::move(value));
    };
    add(1, WorldObjectKind::Unit, "CAR"); add(2, WorldObjectKind::Unit, "CAR");
    add(3, WorldObjectKind::Infantry, "MAN"); add(4, WorldObjectKind::Unit, "BOAT");
    add(5, WorldObjectKind::Unit, "PLANE"); add(6, WorldObjectKind::Unit, "MISSING");
    for (int history = 0; history < 384; ++history)
        add(1000 + history, WorldObjectKind::Unit, "CAR", false);

    LandType land;
    land.foot = std::bit_cast<double>(0x7ff8000000000169ULL); land.track = -0.0;
    land.wheel = -17.25; land.floatSpeed = 75.5; land.floatBeach = std::numeric_limits<double>::infinity();
    land.hover = 0.0; land.amphibious = 123.75;
    std::size_t landChecks = 0, batchChecks = 0;
    const std::array<std::string, 15> tokens{"", "Track", "Wheel", "Foot", "Infantry", "Winged", "Fly",
        "Hover", "Amphibious", "Float", "FloatBeach", "Water", "TrackWheelFloatBeachHover",
        std::string("\x80\xffHover\0x", 9), std::string(160, 'x') + "FlOaTbEaCh"};
    for (int id : {1, 3}) {
        const auto pose = *findObjectById(state, id);
        auto* unit = const_cast<UnitType*>(findRuntimeUnitType(state.rules, pose));
        if (!unit) return fail("land-profile fixture unit");
        const auto originalUnit = *unit;
        for (unsigned int flags = 0; flags < 8; ++flags) for (const auto& speed : tokens) for (const auto& zone : tokens) {
            unit->speedType = speed; unit->movementZone = zone;
            unit->naval = (flags & 1) != 0; unit->waterBound = (flags & 2) != 0; unit->crusher = (flags & 4) != 0;
            const auto before = state.world.deterministicSimulationStateHash();
            const double oldValue = [&] {
                RuntimeSimulationQueryReuseReferenceScope reference(true);
                return runtimeLandTypePercentForMover(land, state, pose);
            }();
            const double newValue = [&] {
                RuntimeSimulationQueryReuseReferenceScope reuse(false);
                return runtimeLandTypePercentForMover(land, state, pose);
            }();
            ++landChecks;
            if (std::bit_cast<std::uint64_t>(oldValue) != std::bit_cast<std::uint64_t>(newValue)
                || before != state.world.deterministicSimulationStateHash())
                return fail("land field/priority/bits/long-string mismatch at case " + std::to_string(landChecks));
        }
        *unit = originalUnit;
    }
    for (WorldObjectKind kind : {WorldObjectKind::Infantry, WorldObjectKind::Unit}) {
        WorldObjectInstance unknown = *findObjectById(state, 6); unknown.kind = kind;
        RuntimeSimulationQueryReuseReferenceScope reuse(false);
        const double value = runtimeLandTypePercentForMover(land, state, unknown);
        if (std::bit_cast<std::uint64_t>(value) != std::bit_cast<std::uint64_t>(
                kind == WorldObjectKind::Infantry ? land.foot : land.track))
            return fail("unknown unit fallback changed");
    }

    const auto compareBatch = [&](const RuntimeMovePathBatchWork& first, const RuntimeMovePathBatchWork& candidate,
                                  const WorldObjectInstance& firstObject, const WorldObjectInstance& nextObject) {
        const auto before = state.world.deterministicSimulationStateHash();
        const bool oldValue = [&] {
            RuntimeSimulationQueryReuseReferenceScope reference(true);
            return runtimeMovePathBatchCompatible(state, first, candidate, firstObject, nextObject);
        }();
        const bool newValue = [&] {
            RuntimeSimulationQueryReuseReferenceScope reuse(false);
            return runtimeMovePathBatchCompatible(state, first, candidate, firstObject, nextObject);
        }();
        ++batchChecks;
        return oldValue == newValue && before == state.world.deterministicSimulationStateHash();
    };
    // Capture with the actual production context constructor. Rebuild contexts
    // after each movement-affecting mutation: reuse is scoped to gathering,
    // before behavior advancement and before committing a queued path.
    for (int firstId : {1, 3, 4, 5, 6}) for (int nextId : {1, 2, 3, 4, 5, 6})
        for (int mutation = 0; mutation < 5; ++mutation) {
            auto firstObject = *findObjectById(state, firstId);
            auto nextObject = *findObjectById(state, nextId);
            if (mutation == 1) nextObject.deployed = true;
            if (mutation == 2) nextObject.alive = false;
            if (mutation == 3) nextObject.mission = "InsideTransport";
            if (mutation == 4) nextObject.health = 0;
            RuntimeMovePathBatchWork first;
            first.objectId = firstId; first.start = {4, 4}; first.goal = {20, 20};
            first.context = makeRuntimeMovePathApproachContext(state, firstObject, first.goal, 0, false);
            RuntimeMovePathBatchWork candidate;
            candidate.objectId = nextId; candidate.goal = {21, 19};
            candidate.context = makeRuntimeMovePathApproachContext(state, nextObject, candidate.goal, 0, false);
            if (first.context.movementConstraintHash != runtimeMovementConstraintHash(state, firstObject)
                || candidate.context.movementConstraintHash != runtimeMovementConstraintHash(state, nextObject))
                return fail("fresh production contexts do not contain the current movement constraint");
            for (int delta : {0, 12, 13, -12, -13}) for (int gate = 0; gate < 4; ++gate) {
                candidate.start = {first.start.x + delta, first.start.y - delta};
                first.allowedOccupiedObjectId = candidate.allowedOccupiedObjectId = -1;
                first.ignoredObjectIds.clear(); candidate.ignoredObjectIds.clear();
                if (gate == 1) candidate.allowedOccupiedObjectId = 1000;
                if (gate == 2) candidate.ignoredObjectIds = {1000, 1001};
                if (gate == 3) {
                    first.allowedOccupiedObjectId = candidate.allowedOccupiedObjectId = 1002;
                    first.ignoredObjectIds = {1001, 1000}; candidate.ignoredObjectIds = {1000, 1001};
                }
                if (!compareBatch(first, candidate, firstObject, nextObject))
                    return fail("batch start/host/ignored/constraint mismatch at case " + std::to_string(batchChecks));
                // This is the only mutation performed while gathering stale
                // requests in production. It must not invalidate those hashes.
                state.behaviors[nextId].pathRequestPending = false;
                if (candidate.context.movementConstraintHash != runtimeMovementConstraintHash(state, nextObject)
                    || !compareBatch(first, candidate, firstObject, nextObject))
                    return fail("pathRequestPending mutation changed a captured movement constraint");
            }
        }
    // Full runtime builders compare all 12 path-cell fields against the old
    // allocation/classification/occupancy branch, including duplicate ignored
    // IDs, buildings, gates, transport, bridge, terrain, repair and real lift.
    if (!runRuntimePathSnapshotOccupancyContract(error)) return false;
    if (!runRuntimePathTerrainQueryScopeContract(error)) return false;
    std::cout << "Simulation query reuse runtime: landBitComparisons=" << landChecks
        << " batchComparisons=" << batchChecks << " actualPathSnapshotFields/scratchCapacity/worldHash=yes\n";
    error.clear();
    return true;
}
