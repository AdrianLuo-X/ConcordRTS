#include "RuntimeAirstrikeSystem.h"

#include "IniFile.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

constexpr std::size_t kHardMaxCallers = 4096;
constexpr std::size_t kHardMaxStrikes = 512;
constexpr std::size_t kHardMaxPlanesPerStrike = 64;
constexpr std::size_t kHardMaxTotalPlanes = 4096;
constexpr std::size_t kHardMaxProjectileReceipts = 16384;
constexpr std::size_t kHardMaxActions = 8192;
constexpr std::size_t kHardMaxText = 256;
constexpr std::size_t kHardMaxInputRecords = 8192;
constexpr int kHardMaxTimingTicks = 1000000;
constexpr int kHardCoordinate = 1000000;

RuntimeAirstrikeLimits sanitizeLimits(RuntimeAirstrikeLimits limits) {
    limits.maxCallers = std::clamp<std::size_t>(
        limits.maxCallers,
        1,
        kHardMaxCallers
    );
    limits.maxActiveStrikes = std::clamp<std::size_t>(
        limits.maxActiveStrikes,
        1,
        kHardMaxStrikes
    );
    limits.maxPlanesPerStrike = std::clamp<std::size_t>(
        limits.maxPlanesPerStrike,
        1,
        kHardMaxPlanesPerStrike
    );
    limits.maxTotalPlanes = std::clamp<std::size_t>(
        limits.maxTotalPlanes,
        1,
        kHardMaxTotalPlanes
    );
    limits.maxProjectileReceipts = std::clamp<std::size_t>(
        limits.maxProjectileReceipts,
        limits.maxTotalPlanes,
        kHardMaxProjectileReceipts
    );
    limits.maxActionsPerAdvance = std::clamp<std::size_t>(
        limits.maxActionsPerAdvance,
        2,
        kHardMaxActions
    );
    limits.maxTextLength = std::clamp<std::size_t>(
        limits.maxTextLength,
        1,
        kHardMaxText
    );
    limits.maxRecursionDepth = std::clamp(limits.maxRecursionDepth, 0, 1);
    limits.spawnDelayTicks = std::clamp(
        limits.spawnDelayTicks,
        1,
        kHardMaxTimingTicks
    );
    limits.launchSpacingTicks = std::clamp(
        limits.launchSpacingTicks,
        0,
        kHardMaxTimingTicks
    );
    limits.ingressTicks = std::clamp(
        limits.ingressTicks,
        1,
        kHardMaxTimingTicks
    );
    limits.egressTicks = std::clamp(
        limits.egressTicks,
        1,
        kHardMaxTimingTicks
    );
    limits.routeMarginCells = std::clamp(limits.routeMarginCells, 1, 1024);
    limits.terminalReceiptRetentionTicks = std::clamp(
        limits.terminalReceiptRetentionTicks,
        1,
        kHardMaxTimingTicks
    );
    return limits;
}

bool checkedAdd(int left, int right, int& output) {
    if (right < 0 || left < 0 || left > INT_MAX - right) {
        return false;
    }
    output = left + right;
    return true;
}

int clampedAdd(int left, int right) {
    int output = 0;
    return checkedAdd(left, right, output) ? output : INT_MAX;
}

int clampedSignedAdd(int left, int right) {
    const long long value = static_cast<long long>(left) + right;
    return static_cast<int>(std::clamp<long long>(value, INT_MIN, INT_MAX));
}

bool textValid(const std::string& value, std::size_t limit, bool emptyOk) {
    if ((!emptyOk && value.empty()) || value.size() > limit) {
        return false;
    }
    return value.find('\0') == std::string::npos;
}

char lowerAscii(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool equalsIgnoreCase(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAscii(left[index]) != lowerAscii(right[index])) {
            return false;
        }
    }
    return true;
}

bool validBounds(const RuntimeAirstrikeBounds& bounds) {
    return bounds.minX <= bounds.maxX
        && bounds.minY <= bounds.maxY
        && bounds.minX >= -kHardCoordinate
        && bounds.minY >= -kHardCoordinate
        && bounds.maxX <= kHardCoordinate
        && bounds.maxY <= kHardCoordinate;
}

bool validTarget(const RuntimeAirstrikeTargetSnapshot& target) {
    return target.objectId > 0
        && target.alive
        && target.visibleToCaller
        && target.targetable;
}

bool validAircraft(
    const RuntimeAirstrikeAircraftDefinition& aircraft,
    const RuntimeAirstrikeLimits& limits
) {
    return textValid(aircraft.typeId, limits.maxTextLength, false)
        && textValid(aircraft.weaponId, limits.maxTextLength, false)
        && textValid(aircraft.warheadId, limits.maxTextLength, true)
        && aircraft.exists
        && aircraft.isAircraft
        && aircraft.hasAttackWeapon;
}

std::uint64_t mixInt(std::uint64_t hash, int value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<std::uint8_t>(bits >> shift);
        hash *= 1099511628211ULL;
    }
    return hash;
}

RuntimeAirstrikeRoute makeRoute(
    int strikeId,
    int callerObjectId,
    int targetObjectId,
    RuntimeAirstrikeCell target,
    const RuntimeAirstrikeBounds& bounds,
    int slot,
    int planeCount,
    int margin
) {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mixInt(hash, strikeId);
    hash = mixInt(hash, callerObjectId);
    hash = mixInt(hash, targetObjectId);
    hash = mixInt(hash, target.x);
    hash = mixInt(hash, target.y);
    const int side = static_cast<int>(hash % 4ULL);
    const int laneNumerator = slot * 2 - (planeCount - 1);
    const int lane = laneNumerator >= 0
        ? (laneNumerator + 1) / 2
        : (laneNumerator - 1) / 2;

    RuntimeAirstrikeRoute route;
    route.target = target;
    if (side == 0 || side == 1) {
        const int laneY = std::clamp(
            clampedSignedAdd(target.y, lane),
            bounds.minY,
            bounds.maxY
        );
        route.entry = {
            side == 0 ? bounds.minX - margin : bounds.maxX + margin,
            laneY
        };
        route.exit = {
            side == 0 ? bounds.maxX + margin : bounds.minX - margin,
            laneY
        };
    } else {
        const int laneX = std::clamp(
            clampedSignedAdd(target.x, lane),
            bounds.minX,
            bounds.maxX
        );
        route.entry = {
            laneX,
            side == 2 ? bounds.minY - margin : bounds.maxY + margin
        };
        route.exit = {
            laneX,
            side == 2 ? bounds.maxY + margin : bounds.minY - margin
        };
    }
    return route;
}

RuntimeAirstrikeCallerState* findCaller(
    std::vector<RuntimeAirstrikeCallerState>& callers,
    int callerObjectId
) {
    for (RuntimeAirstrikeCallerState& caller : callers) {
        if (caller.callerObjectId == callerObjectId) {
            return &caller;
        }
    }
    return nullptr;
}

const RuntimeAirstrikeCallerState* findCaller(
    const std::vector<RuntimeAirstrikeCallerState>& callers,
    int callerObjectId
) {
    for (const RuntimeAirstrikeCallerState& caller : callers) {
        if (caller.callerObjectId == callerObjectId) {
            return &caller;
        }
    }
    return nullptr;
}

std::size_t totalPlaneCount(
    const std::vector<RuntimeAirstrikeStrikeState>& strikes
) {
    std::size_t count = 0;
    for (const RuntimeAirstrikeStrikeState& strike : strikes) {
        count += strike.planes.size();
    }
    return count;
}

const RuntimeAirstrikeProjectileReceipt* findReceiptByPlaneToken(
    const std::vector<RuntimeAirstrikeProjectileReceipt>& receipts,
    int planeToken
) {
    for (const RuntimeAirstrikeProjectileReceipt& receipt : receipts) {
        if (receipt.planeToken == planeToken) {
            return &receipt;
        }
    }
    return nullptr;
}

RuntimeAirstrikeProjectileReceipt* findReceiptByPlaneToken(
    std::vector<RuntimeAirstrikeProjectileReceipt>& receipts,
    int planeToken
) {
    for (RuntimeAirstrikeProjectileReceipt& receipt : receipts) {
        if (receipt.planeToken == planeToken) {
            return &receipt;
        }
    }
    return nullptr;
}

bool receiptMatchesPlane(
    const std::vector<RuntimeAirstrikeProjectileReceipt>& receipts,
    int strikeId,
    int planeToken
) {
    const RuntimeAirstrikeProjectileReceipt* receipt =
        findReceiptByPlaneToken(receipts, planeToken);
    return receipt != nullptr && receipt->strikeId == strikeId;
}

std::size_t reservedProjectileReceiptCount(
    const std::vector<RuntimeAirstrikeStrikeState>& strikes,
    const std::vector<RuntimeAirstrikeProjectileReceipt>& receipts
) {
    std::size_t count = receipts.size();
    for (const RuntimeAirstrikeStrikeState& strike : strikes) {
        for (const RuntimeAirstrikePlaneState& plane : strike.planes) {
            if (!receiptMatchesPlane(
                    receipts,
                    strike.strikeId,
                    plane.planeToken
                )) {
                ++count;
            }
        }
    }
    return count;
}

bool hasActionRoom(
    const std::vector<RuntimeAirstrikeAction>& actions,
    const RuntimeAirstrikeLimits& limits,
    std::size_t count
) {
    return count <= limits.maxActionsPerAdvance
        && actions.size() <= limits.maxActionsPerAdvance - count;
}

RuntimeAirstrikeAction makeAction(
    const RuntimeAirstrikeStrikeState& strike,
    RuntimeAirstrikeActionKind kind,
    int tick,
    const RuntimeAirstrikePlaneState* plane = nullptr
) {
    RuntimeAirstrikeAction action;
    action.kind = kind;
    action.tick = tick;
    action.strikeId = strike.strikeId;
    action.callerObjectId = strike.callerObjectId;
    action.targetObjectId = strike.targetObjectId;
    action.aircraftCount = static_cast<int>(strike.planes.size());
    action.elite = strike.elite;
    action.owner = strike.owner;
    action.aircraftTypeId = strike.aircraftTypeId;
    action.weaponId = strike.weaponId;
    action.warheadId = strike.warheadId;
    action.cancelReason = strike.cancelReason;
    if (plane != nullptr) {
        action.planeToken = plane->planeToken;
        action.planeSlot = plane->slot;
        action.route = plane->route;
        if (kind == RuntimeAirstrikeActionKind::DropBomb) {
            action.route.target = strike.targetCell;
        }
    }
    return action;
}

RuntimeAirstrikeCancelReason targetCancelReason(
    const std::vector<RuntimeAirstrikeTargetSnapshot>& targets,
    int objectId,
    const RuntimeAirstrikeTargetSnapshot*& found
) {
    found = nullptr;
    bool duplicate = false;
    for (const RuntimeAirstrikeTargetSnapshot& target : targets) {
        if (target.objectId != objectId) {
            continue;
        }
        if (found != nullptr) {
            duplicate = true;
            break;
        }
        found = &target;
    }
    if (found == nullptr || duplicate) {
        found = nullptr;
        return RuntimeAirstrikeCancelReason::TargetMissing;
    }
    if (!found->alive) {
        return RuntimeAirstrikeCancelReason::TargetDead;
    }
    if (!found->visibleToCaller) {
        return RuntimeAirstrikeCancelReason::TargetHidden;
    }
    if (!found->targetable) {
        return RuntimeAirstrikeCancelReason::TargetIllegal;
    }
    return RuntimeAirstrikeCancelReason::None;
}

bool planeTerminal(const RuntimeAirstrikePlaneState& plane) {
    return plane.phase == RuntimeAirstrikePlanePhase::Terminated
        || plane.phase == RuntimeAirstrikePlanePhase::Destroyed;
}

bool allPlanesTerminal(const RuntimeAirstrikeStrikeState& strike) {
    return std::all_of(
        strike.planes.begin(),
        strike.planes.end(),
        planeTerminal
    );
}

void beginCancellation(
    RuntimeAirstrikeStrikeState& strike,
    RuntimeAirstrikeCancelReason reason
) {
    if (strike.cancelled) {
        return;
    }
    strike.cancelled = true;
    strike.cancelReason = reason;
    strike.cooldownRequired = strike.everSpawned;
    for (RuntimeAirstrikePlaneState& plane : strike.planes) {
        if (plane.phase == RuntimeAirstrikePlanePhase::Pending) {
            plane.phase = RuntimeAirstrikePlanePhase::Terminated;
        } else if (plane.phase == RuntimeAirstrikePlanePhase::Ingress
                   || plane.phase == RuntimeAirstrikePlanePhase::Egress
                   || plane.phase == RuntimeAirstrikePlanePhase::Destroyed) {
            strike.cooldownRequired = true;
        }
    }
}

bool processCancelledStrike(
    RuntimeAirstrikeStrikeState& strike,
    int currentTick,
    const RuntimeAirstrikeLimits& limits,
    std::vector<RuntimeAirstrikeAction>& actions,
    bool& actionLimitReached
) {
    if (!strike.cancelActionEmitted) {
        if (!hasActionRoom(actions, limits, 1)) {
            actionLimitReached = true;
            return false;
        }
        actions.push_back(makeAction(
            strike,
            RuntimeAirstrikeActionKind::CancelStrike,
            currentTick
        ));
        strike.cancelActionEmitted = true;
    }
    for (RuntimeAirstrikePlaneState& plane : strike.planes) {
        if (plane.phase == RuntimeAirstrikePlanePhase::Ingress) {
            if (!hasActionRoom(actions, limits, 1)) {
                actionLimitReached = true;
                return false;
            }
            actions.push_back(makeAction(
                strike,
                RuntimeAirstrikeActionKind::BeginEgress,
                currentTick,
                &plane
            ));
            plane.phase = RuntimeAirstrikePlanePhase::Egress;
            plane.despawnTick = clampedAdd(currentTick, limits.egressTicks);
        }
        if (plane.phase == RuntimeAirstrikePlanePhase::Egress
            && currentTick >= plane.despawnTick) {
            if (!hasActionRoom(actions, limits, 1)) {
                actionLimitReached = true;
                return false;
            }
            actions.push_back(makeAction(
                strike,
                RuntimeAirstrikeActionKind::DespawnAircraft,
                currentTick,
                &plane
            ));
            plane.phase = RuntimeAirstrikePlanePhase::Terminated;
        }
    }
    return true;
}

bool finishStrike(
    RuntimeAirstrikeStrikeState& strike,
    int currentTick,
    const RuntimeAirstrikeLimits& limits,
    std::vector<RuntimeAirstrikeCallerState>& callers,
    std::vector<RuntimeAirstrikeAction>& actions,
    bool& actionLimitReached
) {
    if (!allPlanesTerminal(strike)) {
        return false;
    }
    if (strike.terminalTick < 0) {
        strike.terminalTick = currentTick;
    }
    RuntimeAirstrikeCallerState* caller = findCaller(
        callers,
        strike.callerObjectId
    );
    if (strike.cooldownRequired) {
        if (!hasActionRoom(actions, limits, 1)) {
            actionLimitReached = true;
            return false;
        }
        const int readyTick = clampedAdd(
            strike.terminalTick,
            strike.rechargeTicks
        );
        RuntimeAirstrikeAction cooldown = makeAction(
            strike,
            RuntimeAirstrikeActionKind::StartCallerCooldown,
            strike.terminalTick
        );
        cooldown.readyTick = readyTick;
        actions.push_back(std::move(cooldown));
        if (caller != nullptr) {
            caller->readyTick = readyTick;
        }
    }
    if (caller != nullptr) {
        caller->activeStrikeId = 0;
    }
    return true;
}

bool liveIdPresent(const std::vector<int>& ids, int id) {
    return std::binary_search(ids.begin(), ids.end(), id);
}

const RuntimeAirstrikeAircraftDefinition* uniqueAircraft(
    const std::vector<RuntimeAirstrikeAircraftDefinition>& aircraft,
    const std::string& typeId
) {
    const RuntimeAirstrikeAircraftDefinition* found = nullptr;
    for (const RuntimeAirstrikeAircraftDefinition& candidate : aircraft) {
        if (!equalsIgnoreCase(candidate.typeId, typeId)) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = &candidate;
    }
    return found;
}

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char byte : value) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool hexDecode(
    const std::string& value,
    std::size_t maximumLength,
    std::string& output
) {
    if ((value.size() & 1U) != 0 || value.size() / 2 > maximumLength) {
        return false;
    }
    std::string decoded;
    decoded.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const int high = hexDigit(value[index]);
        const int low = hexDigit(value[index + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    output = std::move(decoded);
    return true;
}

bool parseIntStrict(const std::string& value, int& output) {
    if (value.empty()) {
        return false;
    }
    int parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end) {
        return false;
    }
    output = parsed;
    return true;
}

bool readInt(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    int& output
) {
    return parseIntStrict(ini.getValue(section, key, ""), output);
}

std::uint64_t hashText(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

RuntimeAirstrikeSystemState::RuntimeAirstrikeSystemState() {
    reset();
}

RuntimeAirstrikeSystemState::RuntimeAirstrikeSystemState(
    RuntimeAirstrikeLimits limits
) {
    reset(limits);
}

void RuntimeAirstrikeSystemState::reset(RuntimeAirstrikeLimits limits) {
    limits_ = sanitizeLimits(limits);
    lastAdvancedTick_ = -1;
    nextStrikeId_ = 1;
    nextPlaneToken_ = 1;
    callers_.clear();
    strikes_.clear();
    projectileReceipts_.clear();
}

RuntimeAirstrikeRequestResult RuntimeAirstrikeSystemState::requestStrike(
    const RuntimeAirstrikeRequest& request
) {
    RuntimeAirstrikeRequestResult result;
    const auto reject = [&](RuntimeAirstrikeVeto veto, int readyTick = 0) {
        result.veto = veto;
        result.readyTick = readyTick;
        return result;
    };
    if (request.currentTick < 0
        || request.currentTick < lastAdvancedTick_
        || request.flightTicksPerCellQ16 < 0
        || request.flightTicksPerCellQ16 > 360000LL * 65536LL) {
        return reject(RuntimeAirstrikeVeto::InvalidTick);
    }
    if (request.sourceStrikeId != 0
        || request.sourcePlaneToken != 0
        || request.recursionDepth < 0
        || request.recursionDepth > limits_.maxRecursionDepth
        || request.recursionDepth != 0) {
        return reject(RuntimeAirstrikeVeto::RecursiveRequest);
    }
    if (request.callerObjectId <= 0) {
        return reject(RuntimeAirstrikeVeto::InvalidCaller);
    }
    if (!textValid(
            request.callerOwner,
            limits_.maxTextLength,
            false
        )) {
        return reject(RuntimeAirstrikeVeto::InvalidOwner);
    }
    if (request.targetObjectId != request.target.objectId
        || !validTarget(request.target)) {
        return reject(RuntimeAirstrikeVeto::InvalidTarget);
    }
    if (!validBounds(request.bounds)
        || request.target.cell.x < request.bounds.minX
        || request.target.cell.x > request.bounds.maxX
        || request.target.cell.y < request.bounds.minY
        || request.target.cell.y > request.bounds.maxY) {
        return reject(RuntimeAirstrikeVeto::InvalidBounds);
    }

    const RuntimeAirstrikeCallerState* caller = findCaller(
        callers_,
        request.callerObjectId
    );
    if (caller != nullptr && caller->activeStrikeId != 0) {
        return reject(RuntimeAirstrikeVeto::CallerBusy, caller->readyTick);
    }
    if (caller != nullptr && request.currentTick < caller->readyTick) {
        return reject(
            RuntimeAirstrikeVeto::CooldownActive,
            caller->readyTick
        );
    }

    const int aircraftCount = request.elite
        ? request.rules.eliteAircraftCount
        : request.rules.normalAircraftCount;
    const std::string& aircraftTypeId = request.elite
        ? request.rules.eliteAircraftTypeId
        : request.rules.normalAircraftTypeId;
    const int rechargeTicks = request.elite
        ? request.rules.eliteRechargeTicks
        : request.rules.normalRechargeTicks;
    if (aircraftCount <= 0 || rechargeTicks < 0) {
        return reject(RuntimeAirstrikeVeto::InvalidAircraftCount);
    }
    if (!textValid(aircraftTypeId, limits_.maxTextLength, false)) {
        return reject(RuntimeAirstrikeVeto::InvalidAircraftType);
    }
    if (!equalsIgnoreCase(aircraftTypeId, request.aircraft.typeId)) {
        return reject(RuntimeAirstrikeVeto::InvalidAircraftType);
    }
    if (!validAircraft(request.aircraft, limits_)) {
        return reject(RuntimeAirstrikeVeto::IllegalAircraftDefinition);
    }
    if (static_cast<std::size_t>(aircraftCount)
        > limits_.maxPlanesPerStrike) {
        return reject(RuntimeAirstrikeVeto::PlaneLimitReached);
    }
    if (strikes_.size() >= limits_.maxActiveStrikes) {
        return reject(RuntimeAirstrikeVeto::StrikeLimitReached);
    }
    if (caller == nullptr && callers_.size() >= limits_.maxCallers) {
        return reject(RuntimeAirstrikeVeto::CallerLimitReached);
    }
    const std::size_t existingPlanes = totalPlaneCount(strikes_);
    if (existingPlanes > limits_.maxTotalPlanes
        || static_cast<std::size_t>(aircraftCount)
            > limits_.maxTotalPlanes - existingPlanes) {
        return reject(RuntimeAirstrikeVeto::PlaneLimitReached);
    }
    const std::size_t reservedReceipts = reservedProjectileReceiptCount(
        strikes_,
        projectileReceipts_
    );
    if (reservedReceipts > limits_.maxProjectileReceipts
        || static_cast<std::size_t>(aircraftCount)
            > limits_.maxProjectileReceipts - reservedReceipts) {
        return reject(RuntimeAirstrikeVeto::ProjectileReceiptLimitReached);
    }
    if (nextStrikeId_ <= 0 || nextStrikeId_ >= INT_MAX
        || nextPlaneToken_ <= 0
        || aircraftCount > INT_MAX - nextPlaneToken_) {
        return reject(RuntimeAirstrikeVeto::IdExhausted);
    }

    RuntimeAirstrikeStrikeState strike;
    strike.strikeId = nextStrikeId_;
    strike.callerObjectId = request.callerObjectId;
    strike.owner = request.callerOwner;
    strike.targetObjectId = request.targetObjectId;
    strike.targetCell = request.target.cell;
    strike.elite = request.elite;
    strike.forceFire = request.forceFire;
    strike.aircraftTypeId = aircraftTypeId;
    strike.weaponId = request.aircraft.weaponId;
    strike.warheadId = request.aircraft.warheadId;
    strike.rechargeTicks = rechargeTicks;
    strike.requestedTick = request.currentTick;
    strike.planes.reserve(static_cast<std::size_t>(aircraftCount));

    int localNextToken = nextPlaneToken_;
    for (int slot = 0; slot < aircraftCount; ++slot) {
        const auto route = makeRoute(strike.strikeId, strike.callerObjectId,
            strike.targetObjectId, strike.targetCell, request.bounds, slot,
            aircraftCount, limits_.routeMarginCells);
        const auto flightTicks = [&](RuntimeAirstrikeCell from, RuntimeAirstrikeCell to, int fallback) {
            if (request.flightTicksPerCellQ16 == 0) return fallback;
            const double duration = std::hypot(static_cast<double>(to.x) - from.x,
                static_cast<double>(to.y) - from.y) * request.flightTicksPerCellQ16 / 65536.0;
            return static_cast<int>(std::clamp(std::ceil(duration), 1.0, static_cast<double>(INT_MAX)));
        };
        int spacing = 0;
        if (limits_.launchSpacingTicks != 0
            && slot > INT_MAX / limits_.launchSpacingTicks) {
            return reject(RuntimeAirstrikeVeto::InvalidTick);
        }
        spacing = slot * limits_.launchSpacingTicks;
        int spawnTick = 0;
        int bombTick = 0;
        int despawnTick = 0;
        int delay = 0;
        if (!checkedAdd(limits_.spawnDelayTicks, spacing, delay)
            || !checkedAdd(request.currentTick, delay, spawnTick)
            || !checkedAdd(spawnTick, flightTicks(route.entry, route.target, limits_.ingressTicks), bombTick)
            || !checkedAdd(bombTick, flightTicks(route.target, route.exit, limits_.egressTicks), despawnTick)) {
            return reject(RuntimeAirstrikeVeto::InvalidTick);
        }
        RuntimeAirstrikePlaneState plane;
        plane.planeToken = localNextToken++;
        plane.slot = slot;
        plane.spawnTick = spawnTick;
        plane.bombTick = bombTick;
        plane.despawnTick = despawnTick;
        plane.route = route;
        strike.planes.push_back(std::move(plane));
    }

    ++nextStrikeId_;
    nextPlaneToken_ = localNextToken;
    if (caller == nullptr) {
        callers_.push_back({request.callerObjectId, 0, strike.strikeId});
    } else {
        findCaller(callers_, request.callerObjectId)->activeStrikeId =
            strike.strikeId;
    }
    strikes_.push_back(strike);
    sortState();

    result.accepted = true;
    result.veto = RuntimeAirstrikeVeto::None;
    result.strikeId = strike.strikeId;
    result.actions.push_back(makeAction(
        strike,
        RuntimeAirstrikeActionKind::TargetMarked,
        request.currentTick
    ));
    return result;
}

RuntimeAirstrikeAdvanceResult RuntimeAirstrikeSystemState::advance(
    const RuntimeAirstrikeAdvanceInput& input
) {
    RuntimeAirstrikeAdvanceResult result;
    if (input.currentTick < 0
        || input.currentTick <= lastAdvancedTick_
        || input.targets.size() > kHardMaxInputRecords
        || input.destroyedPlaneTokens.size() > kHardMaxInputRecords) {
        result.inputRejected = input.currentTick < 0
            || input.targets.size() > kHardMaxInputRecords
            || input.destroyedPlaneTokens.size() > kHardMaxInputRecords;
        return result;
    }

    std::vector<int> destroyed = input.destroyedPlaneTokens;
    destroyed.erase(
        std::remove_if(destroyed.begin(), destroyed.end(), [](int token) {
            return token <= 0;
        }),
        destroyed.end()
    );
    std::sort(destroyed.begin(), destroyed.end());
    destroyed.erase(std::unique(destroyed.begin(), destroyed.end()), destroyed.end());

    for (RuntimeAirstrikeStrikeState& strike : strikes_) {
        for (RuntimeAirstrikePlaneState& plane : strike.planes) {
            if ((plane.phase == RuntimeAirstrikePlanePhase::Ingress
                 || plane.phase == RuntimeAirstrikePlanePhase::Egress)
                && std::binary_search(
                    destroyed.begin(),
                    destroyed.end(),
                    plane.planeToken
                )) {
                plane.phase = RuntimeAirstrikePlanePhase::Destroyed;
                strike.cooldownRequired = true;
            }
        }
    }

    std::vector<int> finishedStrikeIds;
    for (RuntimeAirstrikeStrikeState& strike : strikes_) {
        if (allPlanesTerminal(strike)
            && (!strike.cancelled || strike.cancelActionEmitted)) {
            if (finishStrike(
                    strike,
                    input.currentTick,
                    limits_,
                    callers_,
                    result.actions,
                    result.actionLimitReached
                )) {
                finishedStrikeIds.push_back(strike.strikeId);
            }
            continue;
        }

        if (!strike.cancelled) {
            const RuntimeAirstrikeTargetSnapshot* target = nullptr;
            const RuntimeAirstrikeCancelReason reason = targetCancelReason(
                input.targets,
                strike.targetObjectId,
                target
            );
            if (reason != RuntimeAirstrikeCancelReason::None) {
                beginCancellation(strike, reason);
            } else {
                strike.targetCell = target->cell;
            }
        }

        if (strike.cancelled) {
            processCancelledStrike(
                strike,
                input.currentTick,
                limits_,
                result.actions,
                result.actionLimitReached
            );
        } else {
            for (RuntimeAirstrikePlaneState& plane : strike.planes) {
                if (plane.phase == RuntimeAirstrikePlanePhase::Pending
                    && input.currentTick >= plane.spawnTick) {
                    if (!hasActionRoom(result.actions, limits_, 2)) {
                        result.actionLimitReached = true;
                        break;
                    }
                    result.actions.push_back(makeAction(
                        strike,
                        RuntimeAirstrikeActionKind::SpawnAircraft,
                        input.currentTick,
                        &plane
                    ));
                    result.actions.push_back(makeAction(
                        strike,
                        RuntimeAirstrikeActionKind::AssignIngressRoute,
                        input.currentTick,
                        &plane
                    ));
                    plane.phase = RuntimeAirstrikePlanePhase::Ingress;
                    strike.everSpawned = true;
                    strike.cooldownRequired = true;
                }
                if (plane.phase == RuntimeAirstrikePlanePhase::Ingress
                    && input.currentTick >= plane.bombTick) {
                    if (!hasActionRoom(result.actions, limits_, 2)) {
                        result.actionLimitReached = true;
                        break;
                    }
                    // Receipt creation and DropBomb emission are one authority
                    // transition.  Capacity was reserved when the strike was
                    // accepted, so this cannot fail halfway through a flight.
                    projectileReceipts_.push_back({
                        strike.strikeId,
                        plane.planeToken,
                        input.currentTick,
                        -1
                    });
                    result.actions.push_back(makeAction(
                        strike,
                        RuntimeAirstrikeActionKind::DropBomb,
                        input.currentTick,
                        &plane
                    ));
                    result.actions.push_back(makeAction(
                        strike,
                        RuntimeAirstrikeActionKind::BeginEgress,
                        input.currentTick,
                        &plane
                    ));
                    plane.phase = RuntimeAirstrikePlanePhase::Egress;
                }
                if (plane.phase == RuntimeAirstrikePlanePhase::Egress
                    && input.currentTick >= plane.despawnTick) {
                    if (!hasActionRoom(result.actions, limits_, 1)) {
                        result.actionLimitReached = true;
                        break;
                    }
                    result.actions.push_back(makeAction(
                        strike,
                        RuntimeAirstrikeActionKind::DespawnAircraft,
                        input.currentTick,
                        &plane
                    ));
                    plane.phase = RuntimeAirstrikePlanePhase::Terminated;
                }
            }
        }

        if (allPlanesTerminal(strike)
            && (!strike.cancelled || strike.cancelActionEmitted)
            && finishStrike(
                strike,
                input.currentTick,
                limits_,
                callers_,
                result.actions,
                result.actionLimitReached
            )) {
            finishedStrikeIds.push_back(strike.strikeId);
        }
    }

    if (!finishedStrikeIds.empty()) {
        strikes_.erase(
            std::remove_if(
                strikes_.begin(),
                strikes_.end(),
                [&](const RuntimeAirstrikeStrikeState& strike) {
                    return std::find(
                        finishedStrikeIds.begin(),
                        finishedStrikeIds.end(),
                        strike.strikeId
                    ) != finishedStrikeIds.end();
                }
            ),
            strikes_.end()
        );
    }
    lastAdvancedTick_ = input.currentTick;
    sortState();
    result.advanced = true;
    return result;
}

RuntimeAirstrikeReconcileReport RuntimeAirstrikeSystemState::reconcile(
    const RuntimeAirstrikeReconcileInput& input
) {
    RuntimeAirstrikeReconcileReport report;
    if (input.currentTick < 0
        || input.currentTick < lastAdvancedTick_
        || input.liveCallerObjectIds.size() > kHardMaxInputRecords
        || input.targets.size() > kHardMaxInputRecords
        || input.aircraft.size() > kHardMaxInputRecords
        || input.liveRootProjectiles.size() > kHardMaxInputRecords) {
        report.inputRejected = true;
        return report;
    }
    std::vector<int> liveCallers = input.liveCallerObjectIds;
    liveCallers.erase(
        std::remove_if(liveCallers.begin(), liveCallers.end(), [](int id) {
            return id <= 0;
        }),
        liveCallers.end()
    );

    std::vector<RuntimeAirstrikeProjectileIdentity> liveProjectiles;
    if (input.validateProjectileReceipts) {
        liveProjectiles = input.liveRootProjectiles;
        std::sort(
            liveProjectiles.begin(),
            liveProjectiles.end(),
            [](const auto& left, const auto& right) {
                if (left.planeToken != right.planeToken) {
                    return left.planeToken < right.planeToken;
                }
                return left.strikeId < right.strikeId;
            }
        );
        for (std::size_t index = 0; index < liveProjectiles.size(); ++index) {
            const RuntimeAirstrikeProjectileIdentity& identity =
                liveProjectiles[index];
            if (identity.strikeId <= 0
                || identity.planeToken <= 0
                || (index != 0
                    && liveProjectiles[index - 1].planeToken
                        == identity.planeToken)
                || rootProjectileReceiptStatus(
                    identity.strikeId,
                    identity.planeToken
                ) != RuntimeAirstrikeProjectileReceiptStatus::Pending) {
                report.inputRejected = true;
                return report;
            }
        }
    }
    std::sort(liveCallers.begin(), liveCallers.end());
    liveCallers.erase(
        std::unique(liveCallers.begin(), liveCallers.end()),
        liveCallers.end()
    );

    std::vector<int> finishedStrikeIds;
    for (RuntimeAirstrikeStrikeState& strike : strikes_) {
        RuntimeAirstrikeCancelReason reason = RuntimeAirstrikeCancelReason::None;
        if (input.validateCallers
            && !liveIdPresent(liveCallers, strike.callerObjectId)) {
            reason = RuntimeAirstrikeCancelReason::CallerMissing;
        }
        if (reason == RuntimeAirstrikeCancelReason::None
            && input.validateTargets) {
            const RuntimeAirstrikeTargetSnapshot* target = nullptr;
            reason = targetCancelReason(
                input.targets,
                strike.targetObjectId,
                target
            );
            if (reason == RuntimeAirstrikeCancelReason::None) {
                strike.targetCell = target->cell;
            }
        }
        if (reason == RuntimeAirstrikeCancelReason::None
            && input.validateAircraft) {
            const RuntimeAirstrikeAircraftDefinition* definition =
                uniqueAircraft(input.aircraft, strike.aircraftTypeId);
            if (definition == nullptr
                || !validAircraft(*definition, limits_)) {
                reason = RuntimeAirstrikeCancelReason::AircraftInvalid;
            }
        }
        if (reason != RuntimeAirstrikeCancelReason::None) {
            const bool wasCancelled = strike.cancelled;
            beginCancellation(strike, reason);
            if (!wasCancelled) {
                ++report.cancelledStrikes;
            }
            processCancelledStrike(
                strike,
                input.currentTick,
                limits_,
                report.actions,
                report.actionLimitReached
            );
        }
        if (allPlanesTerminal(strike)
            && (!strike.cancelled || strike.cancelActionEmitted)
            && finishStrike(
                strike,
                input.currentTick,
                limits_,
                callers_,
                report.actions,
                report.actionLimitReached
            )) {
            finishedStrikeIds.push_back(strike.strikeId);
        }
    }
    strikes_.erase(
        std::remove_if(
            strikes_.begin(),
            strikes_.end(),
            [&](const RuntimeAirstrikeStrikeState& strike) {
                return std::find(
                    finishedStrikeIds.begin(),
                    finishedStrikeIds.end(),
                    strike.strikeId
                ) != finishedStrikeIds.end();
            }
        ),
        strikes_.end()
    );

    if (input.validateCallers) {
        const std::size_t before = callers_.size();
        callers_.erase(
            std::remove_if(
                callers_.begin(),
                callers_.end(),
                [&](const RuntimeAirstrikeCallerState& caller) {
                    return caller.activeStrikeId == 0
                        && !liveIdPresent(
                            liveCallers,
                            caller.callerObjectId
                        );
                }
            ),
            callers_.end()
        );
        report.removedCallers = before - callers_.size();
    }
    if (input.validateProjectileReceipts) {
        for (RuntimeAirstrikeProjectileReceipt& receipt :
             projectileReceipts_) {
            if (receipt.terminalTick >= 0) {
                continue;
            }
            const RuntimeAirstrikeProjectileIdentity identity{
                receipt.strikeId,
                receipt.planeToken
            };
            const bool live = std::binary_search(
                liveProjectiles.begin(),
                liveProjectiles.end(),
                identity,
                [](const auto& left, const auto& right) {
                    if (left.planeToken != right.planeToken) {
                        return left.planeToken < right.planeToken;
                    }
                    return left.strikeId < right.strikeId;
                }
            );
            // A DropBomb action and its host projectile can be reconciled in
            // the same tick.  Keep that just-emitted receipt for one complete
            // tick so a save between those two host steps remains loadable.
            if (!live && receipt.launchTick < input.currentTick) {
                receipt.terminalTick = input.currentTick;
                ++report.terminatedProjectileReceipts;
            }
        }
        const std::size_t before = projectileReceipts_.size();
        projectileReceipts_.erase(
            std::remove_if(
                projectileReceipts_.begin(),
                projectileReceipts_.end(),
                [&](const RuntimeAirstrikeProjectileReceipt& receipt) {
                    return receipt.terminalTick >= 0
                        && input.currentTick >= receipt.terminalTick
                        && static_cast<long long>(input.currentTick)
                            - receipt.terminalTick
                            >= limits_.terminalReceiptRetentionTicks;
                }
            ),
            projectileReceipts_.end()
        );
        report.expiredProjectileReceipts =
            before - projectileReceipts_.size();
    }
    sortState();
    report.retainedCallers = callers_.size();
    report.retainedStrikes = strikes_.size();
    report.retainedProjectileReceipts = projectileReceipts_.size();
    return report;
}

RuntimeAirstrikeProjectileReceiptStatus
RuntimeAirstrikeSystemState::rootProjectileReceiptStatus(
    int strikeId,
    int planeToken
) const {
    if (strikeId <= 0 || planeToken <= 0) {
        return RuntimeAirstrikeProjectileReceiptStatus::NotFound;
    }
    const RuntimeAirstrikeProjectileReceipt* receipt =
        findReceiptByPlaneToken(projectileReceipts_, planeToken);
    if (receipt != nullptr) {
        if (receipt->strikeId != strikeId) {
            return RuntimeAirstrikeProjectileReceiptStatus::IdentityConflict;
        }
        return receipt->terminalTick < 0
            ? RuntimeAirstrikeProjectileReceiptStatus::Pending
            : RuntimeAirstrikeProjectileReceiptStatus::Terminal;
    }
    for (const RuntimeAirstrikeStrikeState& strike : strikes_) {
        for (const RuntimeAirstrikePlaneState& plane : strike.planes) {
            if (plane.planeToken == planeToken) {
                return strike.strikeId == strikeId
                    ? RuntimeAirstrikeProjectileReceiptStatus::NotFound
                    : RuntimeAirstrikeProjectileReceiptStatus::IdentityConflict;
            }
        }
    }
    return RuntimeAirstrikeProjectileReceiptStatus::NotFound;
}

const RuntimeAirstrikeProjectileReceipt*
RuntimeAirstrikeSystemState::findRootProjectileReceipt(
    int strikeId,
    int planeToken
) const {
    const RuntimeAirstrikeProjectileReceipt* receipt =
        findReceiptByPlaneToken(projectileReceipts_, planeToken);
    return receipt != nullptr && receipt->strikeId == strikeId
        ? receipt
        : nullptr;
}

RuntimeAirstrikeProjectileAckResult
RuntimeAirstrikeSystemState::acknowledgeRootProjectile(
    int strikeId,
    int planeToken,
    int currentTick
) {
    if (strikeId <= 0 || planeToken <= 0) {
        return RuntimeAirstrikeProjectileAckResult::InvalidIdentity;
    }
    RuntimeAirstrikeProjectileReceipt* receipt =
        findReceiptByPlaneToken(projectileReceipts_, planeToken);
    if (receipt == nullptr) {
        return rootProjectileReceiptStatus(strikeId, planeToken)
                == RuntimeAirstrikeProjectileReceiptStatus::IdentityConflict
            ? RuntimeAirstrikeProjectileAckResult::IdentityConflict
            : RuntimeAirstrikeProjectileAckResult::NotFound;
    }
    if (receipt->strikeId != strikeId) {
        return RuntimeAirstrikeProjectileAckResult::IdentityConflict;
    }
    if (receipt->terminalTick >= 0) {
        return RuntimeAirstrikeProjectileAckResult::AlreadyAcknowledged;
    }
    if (currentTick < 0
        || currentTick < lastAdvancedTick_
        || currentTick < receipt->launchTick) {
        return RuntimeAirstrikeProjectileAckResult::InvalidTick;
    }
    receipt->terminalTick = currentTick;
    return RuntimeAirstrikeProjectileAckResult::Accepted;
}

void RuntimeAirstrikeSystemState::save(std::ostream& output) const {
    std::vector<RuntimeAirstrikeCallerState> callers = callers_;
    std::vector<RuntimeAirstrikeStrikeState> strikes = strikes_;
    std::vector<RuntimeAirstrikeProjectileReceipt> projectileReceipts =
        projectileReceipts_;
    std::sort(
        callers.begin(),
        callers.end(),
        [](const auto& left, const auto& right) {
            return left.callerObjectId < right.callerObjectId;
        }
    );
    std::sort(
        strikes.begin(),
        strikes.end(),
        [](const auto& left, const auto& right) {
            return left.strikeId < right.strikeId;
        }
    );
    for (RuntimeAirstrikeStrikeState& strike : strikes) {
        std::sort(
            strike.planes.begin(),
            strike.planes.end(),
            [](const auto& left, const auto& right) {
                return left.slot < right.slot;
            }
        );
    }
    std::sort(
        projectileReceipts.begin(),
        projectileReceipts.end(),
        [](const auto& left, const auto& right) {
            return left.planeToken < right.planeToken;
        }
    );

    output << "[AirstrikeRuntime]\n";
    output << "Version=3\n";
    output << "LastAdvancedTick=" << lastAdvancedTick_ << "\n";
    output << "NextStrikeId=" << nextStrikeId_ << "\n";
    output << "NextPlaneToken=" << nextPlaneToken_ << "\n";
    output << "CallerCount=" << callers.size() << "\n";
    output << "StrikeCount=" << strikes.size() << "\n";
    output << "ProjectileReceiptCount="
           << projectileReceipts.size() << "\n\n";

    for (std::size_t index = 0; index < callers.size(); ++index) {
        const RuntimeAirstrikeCallerState& caller = callers[index];
        output << "[AirstrikeCaller" << index << "]\n";
        output << "CallerObjectId=" << caller.callerObjectId << "\n";
        output << "ReadyTick=" << caller.readyTick << "\n";
        output << "ActiveStrikeId=" << caller.activeStrikeId << "\n\n";
    }

    for (std::size_t strikeIndex = 0;
         strikeIndex < strikes.size();
         ++strikeIndex) {
        const RuntimeAirstrikeStrikeState& strike = strikes[strikeIndex];
        output << "[AirstrikeStrike" << strikeIndex << "]\n";
        output << "StrikeId=" << strike.strikeId << "\n";
        output << "CallerObjectId=" << strike.callerObjectId << "\n";
        output << "OwnerHex=" << hexEncode(strike.owner) << "\n";
        output << "TargetObjectId=" << strike.targetObjectId << "\n";
        output << "TargetX=" << strike.targetCell.x << "\n";
        output << "TargetY=" << strike.targetCell.y << "\n";
        output << "Elite=" << (strike.elite ? 1 : 0) << "\n";
        output << "ForceFire=" << (strike.forceFire ? 1 : 0) << "\n";
        output << "AircraftTypeHex="
               << hexEncode(strike.aircraftTypeId) << "\n";
        output << "WeaponHex=" << hexEncode(strike.weaponId) << "\n";
        output << "WarheadHex=" << hexEncode(strike.warheadId) << "\n";
        output << "RechargeTicks=" << strike.rechargeTicks << "\n";
        output << "RequestedTick=" << strike.requestedTick << "\n";
        output << "TerminalTick=" << strike.terminalTick << "\n";
        output << "EverSpawned=" << (strike.everSpawned ? 1 : 0) << "\n";
        output << "Cancelled=" << (strike.cancelled ? 1 : 0) << "\n";
        output << "CancelActionEmitted="
               << (strike.cancelActionEmitted ? 1 : 0) << "\n";
        output << "CooldownRequired="
               << (strike.cooldownRequired ? 1 : 0) << "\n";
        output << "CancelReason="
               << static_cast<int>(strike.cancelReason) << "\n";
        output << "PlaneCount=" << strike.planes.size() << "\n\n";

        for (std::size_t planeIndex = 0;
             planeIndex < strike.planes.size();
             ++planeIndex) {
            const RuntimeAirstrikePlaneState& plane =
                strike.planes[planeIndex];
            output << "[AirstrikePlane" << strikeIndex
                   << "_" << planeIndex << "]\n";
            output << "PlaneToken=" << plane.planeToken << "\n";
            output << "Slot=" << plane.slot << "\n";
            output << "Phase=" << static_cast<int>(plane.phase) << "\n";
            output << "SpawnTick=" << plane.spawnTick << "\n";
            output << "BombTick=" << plane.bombTick << "\n";
            output << "DespawnTick=" << plane.despawnTick << "\n";
            output << "EntryX=" << plane.route.entry.x << "\n";
            output << "EntryY=" << plane.route.entry.y << "\n";
            output << "RouteTargetX=" << plane.route.target.x << "\n";
            output << "RouteTargetY=" << plane.route.target.y << "\n";
            output << "ExitX=" << plane.route.exit.x << "\n";
            output << "ExitY=" << plane.route.exit.y << "\n\n";
        }
    }
    for (std::size_t index = 0; index < projectileReceipts.size(); ++index) {
        const RuntimeAirstrikeProjectileReceipt& receipt =
            projectileReceipts[index];
        output << "[AirstrikeProjectileReceipt" << index << "]\n";
        output << "StrikeId=" << receipt.strikeId << "\n";
        output << "PlaneToken=" << receipt.planeToken << "\n";
        output << "LaunchTick=" << receipt.launchTick << "\n";
        output << "TerminalTick=" << receipt.terminalTick << "\n\n";
    }
}

bool RuntimeAirstrikeSystemState::load(
    const IniFile& ini,
    std::string* errorMessage
) {
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (ini.getSection("AirstrikeRuntime") == nullptr) {
        reset(limits_);
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return true;
    }

    const std::size_t maximumSections = 1
        + limits_.maxCallers
        + limits_.maxActiveStrikes
        + limits_.maxTotalPlanes
        + limits_.maxProjectileReceipts;
    std::size_t airstrikeSections = 0;
    std::size_t projectileReceiptSections = 0;
    for (const std::string& section : ini.getSectionNames()) {
        if (section.rfind("Airstrike", 0) == 0) {
            ++airstrikeSections;
        }
        if (section.rfind("AirstrikeProjectileReceipt", 0) == 0) {
            ++projectileReceiptSections;
        }
    }
    if (airstrikeSections > maximumSections) {
        return fail("airstrike save exceeds serialized section limit");
    }

    int version = 0;
    int callerCount = 0;
    int strikeCount = 0;
    int projectileReceiptCount = 0;
    RuntimeAirstrikeSystemState loaded(limits_);
    if (!readInt(ini, "AirstrikeRuntime", "Version", version)
        || (version != 1 && version != 2 && version != 3)
        || !readInt(
            ini,
            "AirstrikeRuntime",
            "LastAdvancedTick",
            loaded.lastAdvancedTick_
        )
        || !readInt(
            ini,
            "AirstrikeRuntime",
            "NextStrikeId",
            loaded.nextStrikeId_
        )
        || !readInt(
            ini,
            "AirstrikeRuntime",
            "NextPlaneToken",
            loaded.nextPlaneToken_
        )
        || !readInt(ini, "AirstrikeRuntime", "CallerCount", callerCount)
        || !readInt(ini, "AirstrikeRuntime", "StrikeCount", strikeCount)
        || (version >= 2
            && !readInt(
                ini,
                "AirstrikeRuntime",
                "ProjectileReceiptCount",
                projectileReceiptCount
            ))) {
        return fail("invalid AirstrikeRuntime header");
    }
    if (loaded.lastAdvancedTick_ < -1
        || loaded.nextStrikeId_ <= 0
        || loaded.nextPlaneToken_ <= 0
        || callerCount < 0
        || strikeCount < 0
        || projectileReceiptCount < 0
        || static_cast<std::size_t>(callerCount) > limits_.maxCallers
        || static_cast<std::size_t>(strikeCount)
            > limits_.maxActiveStrikes
        || static_cast<std::size_t>(projectileReceiptCount)
            > limits_.maxProjectileReceipts
        || (version == 1 && projectileReceiptSections != 0)
        || (version >= 2
            && projectileReceiptSections
                != static_cast<std::size_t>(projectileReceiptCount))) {
        return fail("airstrike save header exceeds limits");
    }

    std::set<int> callerIds;
    loaded.callers_.reserve(static_cast<std::size_t>(callerCount));
    for (int index = 0; index < callerCount; ++index) {
        const std::string section = "AirstrikeCaller" + std::to_string(index);
        RuntimeAirstrikeCallerState caller;
        if (ini.getSection(section) == nullptr
            || !readInt(
                ini,
                section,
                "CallerObjectId",
                caller.callerObjectId
            )
            || !readInt(ini, section, "ReadyTick", caller.readyTick)
            || !readInt(
                ini,
                section,
                "ActiveStrikeId",
                caller.activeStrikeId
            )
            || caller.callerObjectId <= 0
            || caller.readyTick < 0
            || caller.activeStrikeId < 0
            || !callerIds.insert(caller.callerObjectId).second) {
            return fail("invalid or duplicate airstrike caller record");
        }
        loaded.callers_.push_back(caller);
    }

    std::set<int> strikeIds;
    std::set<int> planeTokens;
    std::size_t totalPlanes = 0;
    int maximumStrikeId = 0;
    int maximumPlaneToken = 0;
    loaded.strikes_.reserve(static_cast<std::size_t>(strikeCount));
    for (int strikeIndex = 0; strikeIndex < strikeCount; ++strikeIndex) {
        const std::string section =
            "AirstrikeStrike" + std::to_string(strikeIndex);
        RuntimeAirstrikeStrikeState strike;
        int elite = 0;
        int forceFire = 0;
        int everSpawned = 0;
        int cancelled = 0;
        int cancelActionEmitted = 0;
        int cooldownRequired = 0;
        int cancelReason = 0;
        int planeCount = 0;
        if (ini.getSection(section) == nullptr
            || !readInt(ini, section, "StrikeId", strike.strikeId)
            || !readInt(
                ini,
                section,
                "CallerObjectId",
                strike.callerObjectId
            )
            || !hexDecode(
                ini.getValue(section, "OwnerHex", ""),
                limits_.maxTextLength,
                strike.owner
            )
            || !readInt(
                ini,
                section,
                "TargetObjectId",
                strike.targetObjectId
            )
            || !readInt(ini, section, "TargetX", strike.targetCell.x)
            || !readInt(ini, section, "TargetY", strike.targetCell.y)
            || !readInt(ini, section, "Elite", elite)
            || (version >= 3 && !readInt(ini, section, "ForceFire", forceFire))
            || !hexDecode(
                ini.getValue(section, "AircraftTypeHex", ""),
                limits_.maxTextLength,
                strike.aircraftTypeId
            )
            || !hexDecode(
                ini.getValue(section, "WeaponHex", ""),
                limits_.maxTextLength,
                strike.weaponId
            )
            || !hexDecode(
                ini.getValue(section, "WarheadHex", ""),
                limits_.maxTextLength,
                strike.warheadId
            )
            || !readInt(
                ini,
                section,
                "RechargeTicks",
                strike.rechargeTicks
            )
            || !readInt(
                ini,
                section,
                "RequestedTick",
                strike.requestedTick
            )
            || !readInt(
                ini,
                section,
                "TerminalTick",
                strike.terminalTick
            )
            || !readInt(ini, section, "EverSpawned", everSpawned)
            || !readInt(ini, section, "Cancelled", cancelled)
            || !readInt(
                ini,
                section,
                "CancelActionEmitted",
                cancelActionEmitted
            )
            || !readInt(
                ini,
                section,
                "CooldownRequired",
                cooldownRequired
            )
            || !readInt(ini, section, "CancelReason", cancelReason)
            || !readInt(ini, section, "PlaneCount", planeCount)) {
            return fail("invalid airstrike strike record");
        }
        if (strike.strikeId <= 0
            || strike.callerObjectId <= 0
            || strike.targetObjectId <= 0
            || strike.targetCell.x < -kHardCoordinate
            || strike.targetCell.x > kHardCoordinate
            || strike.targetCell.y < -kHardCoordinate
            || strike.targetCell.y > kHardCoordinate
            || (elite != 0 && elite != 1)
            || (forceFire != 0 && forceFire != 1)
            || (everSpawned != 0 && everSpawned != 1)
            || (cancelled != 0 && cancelled != 1)
            || (cancelActionEmitted != 0 && cancelActionEmitted != 1)
            || (cooldownRequired != 0 && cooldownRequired != 1)
            || cancelReason < 0
            || cancelReason > static_cast<int>(
                RuntimeAirstrikeCancelReason::Reconciled
            )
            || strike.rechargeTicks < 0
            || strike.requestedTick < 0
            || strike.terminalTick < -1
            || planeCount <= 0
            || static_cast<std::size_t>(planeCount)
                > limits_.maxPlanesPerStrike
            || !textValid(strike.owner, limits_.maxTextLength, false)
            || !textValid(
                strike.aircraftTypeId,
                limits_.maxTextLength,
                false
            )
            || !textValid(strike.weaponId, limits_.maxTextLength, false)
            || !textValid(strike.warheadId, limits_.maxTextLength, true)
            || !strikeIds.insert(strike.strikeId).second) {
            return fail("airstrike strike record exceeds limits");
        }
        strike.elite = elite != 0;
        strike.forceFire = forceFire != 0;
        strike.everSpawned = everSpawned != 0;
        strike.cancelled = cancelled != 0;
        strike.cancelActionEmitted = cancelActionEmitted != 0;
        strike.cooldownRequired = cooldownRequired != 0;
        strike.cancelReason = static_cast<RuntimeAirstrikeCancelReason>(
            cancelReason
        );
        if (strike.cancelActionEmitted && !strike.cancelled) {
            return fail("airstrike cancel action without cancellation");
        }
        if (strike.cancelled
            && strike.cancelReason == RuntimeAirstrikeCancelReason::None) {
            return fail("airstrike cancellation lacks reason");
        }
        if (!strike.cancelled
            && strike.cancelReason != RuntimeAirstrikeCancelReason::None) {
            return fail("active airstrike has cancellation reason");
        }
        if (totalPlanes > limits_.maxTotalPlanes
            || static_cast<std::size_t>(planeCount)
                > limits_.maxTotalPlanes - totalPlanes) {
            return fail("airstrike total plane limit exceeded");
        }
        totalPlanes += static_cast<std::size_t>(planeCount);
        strike.planes.reserve(static_cast<std::size_t>(planeCount));
        bool observedSpawned = false;
        for (int planeIndex = 0; planeIndex < planeCount; ++planeIndex) {
            const std::string planeSection = "AirstrikePlane"
                + std::to_string(strikeIndex) + "_"
                + std::to_string(planeIndex);
            RuntimeAirstrikePlaneState plane;
            int phase = 0;
            if (ini.getSection(planeSection) == nullptr
                || !readInt(
                    ini,
                    planeSection,
                    "PlaneToken",
                    plane.planeToken
                )
                || !readInt(ini, planeSection, "Slot", plane.slot)
                || !readInt(ini, planeSection, "Phase", phase)
                || !readInt(
                    ini,
                    planeSection,
                    "SpawnTick",
                    plane.spawnTick
                )
                || !readInt(
                    ini,
                    planeSection,
                    "BombTick",
                    plane.bombTick
                )
                || !readInt(
                    ini,
                    planeSection,
                    "DespawnTick",
                    plane.despawnTick
                )
                || !readInt(
                    ini,
                    planeSection,
                    "EntryX",
                    plane.route.entry.x
                )
                || !readInt(
                    ini,
                    planeSection,
                    "EntryY",
                    plane.route.entry.y
                )
                || !readInt(
                    ini,
                    planeSection,
                    "RouteTargetX",
                    plane.route.target.x
                )
                || !readInt(
                    ini,
                    planeSection,
                    "RouteTargetY",
                    plane.route.target.y
                )
                || !readInt(
                    ini,
                    planeSection,
                    "ExitX",
                    plane.route.exit.x
                )
                || !readInt(
                    ini,
                    planeSection,
                    "ExitY",
                    plane.route.exit.y
                )) {
                return fail("invalid airstrike plane record");
            }
            if (plane.planeToken <= 0
                || plane.slot != planeIndex
                || phase < 0
                || phase > static_cast<int>(
                    RuntimeAirstrikePlanePhase::Destroyed
                )
                || plane.spawnTick < 0
                || plane.bombTick < plane.spawnTick
                || (plane.despawnTick < plane.bombTick
                    && (!strike.cancelled
                        || !strike.cancelActionEmitted
                        || !strike.everSpawned
                        || (phase != static_cast<int>(
                                RuntimeAirstrikePlanePhase::Egress)
                            && phase != static_cast<int>(
                                RuntimeAirstrikePlanePhase::Terminated)
                            && phase != static_cast<int>(
                                RuntimeAirstrikePlanePhase::Destroyed))
                        || plane.despawnTick < plane.spawnTick))
                || !planeTokens.insert(plane.planeToken).second) {
                return fail("airstrike plane record exceeds limits");
            }
            const int coordinateValues[] = {
                plane.route.entry.x,
                plane.route.entry.y,
                plane.route.target.x,
                plane.route.target.y,
                plane.route.exit.x,
                plane.route.exit.y
            };
            for (int coordinate : coordinateValues) {
                if (coordinate < -kHardCoordinate - 1024
                    || coordinate > kHardCoordinate + 1024) {
                    return fail("airstrike route coordinate exceeds limit");
                }
            }
            plane.phase = static_cast<RuntimeAirstrikePlanePhase>(phase);
            observedSpawned = observedSpawned
                || plane.phase == RuntimeAirstrikePlanePhase::Ingress
                || plane.phase == RuntimeAirstrikePlanePhase::Egress
                || plane.phase == RuntimeAirstrikePlanePhase::Destroyed
                || (plane.phase == RuntimeAirstrikePlanePhase::Terminated
                    && (!strike.cancelled || strike.everSpawned));
            if (strike.cancelled
                && plane.phase == RuntimeAirstrikePlanePhase::Pending) {
                return fail("cancelled airstrike contains pending plane");
            }
            maximumPlaneToken = std::max(
                maximumPlaneToken,
                plane.planeToken
            );
            strike.planes.push_back(plane);
        }
        if (observedSpawned && !strike.everSpawned) {
            return fail("airstrike spawned phase lacks ever-spawned marker");
        }
        if (strike.everSpawned != strike.cooldownRequired
            || (strike.everSpawned && !observedSpawned)) {
            return fail("airstrike spawned/cooldown markers are inconsistent");
        }
        maximumStrikeId = std::max(maximumStrikeId, strike.strikeId);
        loaded.strikes_.push_back(std::move(strike));
    }

    std::set<int> receiptPlaneTokens;
    loaded.projectileReceipts_.reserve(
        static_cast<std::size_t>(projectileReceiptCount)
    );
    for (int index = 0; index < projectileReceiptCount; ++index) {
        const std::string section =
            "AirstrikeProjectileReceipt" + std::to_string(index);
        RuntimeAirstrikeProjectileReceipt receipt;
        if (ini.getSection(section) == nullptr
            || !readInt(ini, section, "StrikeId", receipt.strikeId)
            || !readInt(ini, section, "PlaneToken", receipt.planeToken)
            || !readInt(ini, section, "LaunchTick", receipt.launchTick)
            || !readInt(
                ini,
                section,
                "TerminalTick",
                receipt.terminalTick
            )) {
            return fail("invalid airstrike projectile receipt record");
        }
        if (receipt.strikeId <= 0
            || receipt.planeToken <= 0
            || receipt.launchTick < 0
            || receipt.launchTick > loaded.lastAdvancedTick_
            || receipt.terminalTick < -1
            || (receipt.terminalTick >= 0
                && receipt.terminalTick < receipt.launchTick)
            || !receiptPlaneTokens.insert(receipt.planeToken).second) {
            return fail("airstrike projectile receipt exceeds limits");
        }

        const RuntimeAirstrikeStrikeState* activeStrike = nullptr;
        const RuntimeAirstrikePlaneState* activePlane = nullptr;
        for (const RuntimeAirstrikeStrikeState& strike : loaded.strikes_) {
            for (const RuntimeAirstrikePlaneState& plane : strike.planes) {
                if (plane.planeToken == receipt.planeToken) {
                    activeStrike = &strike;
                    activePlane = &plane;
                    break;
                }
            }
            if (activePlane != nullptr) {
                break;
            }
        }
        if ((activePlane != nullptr
                && (activeStrike->strikeId != receipt.strikeId
                    || receipt.launchTick < activePlane->bombTick
                    || activePlane->despawnTick < activePlane->bombTick
                    || activePlane->phase
                        == RuntimeAirstrikePlanePhase::Pending
                    || activePlane->phase
                        == RuntimeAirstrikePlanePhase::Ingress))
            || (activePlane == nullptr
                && std::any_of(
                    loaded.strikes_.begin(),
                    loaded.strikes_.end(),
                    [&](const RuntimeAirstrikeStrikeState& strike) {
                        return strike.strikeId == receipt.strikeId;
                    }
                ))) {
            return fail("airstrike projectile receipt identity mismatch");
        }
        maximumStrikeId = std::max(maximumStrikeId, receipt.strikeId);
        maximumPlaneToken = std::max(
            maximumPlaneToken,
            receipt.planeToken
        );
        loaded.projectileReceipts_.push_back(receipt);
    }

    if (reservedProjectileReceiptCount(
            loaded.strikes_,
            loaded.projectileReceipts_
        ) > limits_.maxProjectileReceipts) {
        return fail("airstrike projectile receipt reservation exceeds limit");
    }

    if (loaded.nextStrikeId_ <= maximumStrikeId
        || loaded.nextPlaneToken_ <= maximumPlaneToken) {
        return fail("airstrike next id is not ahead of saved records");
    }
    for (const RuntimeAirstrikeStrikeState& strike : loaded.strikes_) {
        const RuntimeAirstrikeCallerState* caller = findCaller(
            loaded.callers_,
            strike.callerObjectId
        );
        if (caller == nullptr || caller->activeStrikeId != strike.strikeId) {
            return fail("airstrike strike/caller link mismatch");
        }
    }
    for (const RuntimeAirstrikeCallerState& caller : loaded.callers_) {
        if (caller.activeStrikeId == 0) {
            continue;
        }
        const bool found = std::any_of(
            loaded.strikes_.begin(),
            loaded.strikes_.end(),
            [&](const RuntimeAirstrikeStrikeState& strike) {
                return strike.strikeId == caller.activeStrikeId
                    && strike.callerObjectId == caller.callerObjectId;
            }
        );
        if (!found) {
            return fail("airstrike caller references missing strike");
        }
    }

    loaded.sortState();
    *this = std::move(loaded);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

std::uint64_t RuntimeAirstrikeSystemState::stateHash() const {
    std::ostringstream output;
    save(output);
    return hashText(output.str());
}

bool RuntimeAirstrikeSystemState::callerReady(
    int callerObjectId,
    int currentTick
) const {
    const RuntimeAirstrikeCallerState* caller = findCaller(
        callers_,
        callerObjectId
    );
    return caller == nullptr
        || (caller->activeStrikeId == 0 && currentTick >= caller->readyTick);
}

int RuntimeAirstrikeSystemState::callerReadyTick(int callerObjectId) const {
    const RuntimeAirstrikeCallerState* caller = findCaller(
        callers_,
        callerObjectId
    );
    return caller == nullptr ? 0 : caller->readyTick;
}

const RuntimeAirstrikeStrikeState* RuntimeAirstrikeSystemState::findStrike(
    int strikeId
) const {
    for (const RuntimeAirstrikeStrikeState& strike : strikes_) {
        if (strike.strikeId == strikeId) {
            return &strike;
        }
    }
    return nullptr;
}

const RuntimeAirstrikeLimits& RuntimeAirstrikeSystemState::limits() const {
    return limits_;
}

const std::vector<RuntimeAirstrikeCallerState>&
RuntimeAirstrikeSystemState::callers() const {
    return callers_;
}

const std::vector<RuntimeAirstrikeStrikeState>&
RuntimeAirstrikeSystemState::strikes() const {
    return strikes_;
}

const std::vector<RuntimeAirstrikeProjectileReceipt>&
RuntimeAirstrikeSystemState::projectileReceipts() const {
    return projectileReceipts_;
}

int RuntimeAirstrikeSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

int RuntimeAirstrikeSystemState::nextStrikeId() const {
    return nextStrikeId_;
}

int RuntimeAirstrikeSystemState::nextPlaneToken() const {
    return nextPlaneToken_;
}

void RuntimeAirstrikeSystemState::sortState() {
    std::sort(
        callers_.begin(),
        callers_.end(),
        [](const auto& left, const auto& right) {
            return left.callerObjectId < right.callerObjectId;
        }
    );
    std::sort(
        strikes_.begin(),
        strikes_.end(),
        [](const auto& left, const auto& right) {
            return left.strikeId < right.strikeId;
        }
    );
    for (RuntimeAirstrikeStrikeState& strike : strikes_) {
        std::sort(
            strike.planes.begin(),
            strike.planes.end(),
            [](const auto& left, const auto& right) {
                return left.slot < right.slot;
            }
        );
    }
    std::sort(
        projectileReceipts_.begin(),
        projectileReceipts_.end(),
        [](const auto& left, const auto& right) {
            return left.planeToken < right.planeToken;
        }
    );
}

const char* runtimeAirstrikeVetoName(RuntimeAirstrikeVeto veto) {
    switch (veto) {
        case RuntimeAirstrikeVeto::None: return "none";
        case RuntimeAirstrikeVeto::InvalidTick: return "invalid-tick";
        case RuntimeAirstrikeVeto::InvalidCaller: return "invalid-caller";
        case RuntimeAirstrikeVeto::InvalidOwner: return "invalid-owner";
        case RuntimeAirstrikeVeto::RecursiveRequest: return "recursive-request";
        case RuntimeAirstrikeVeto::CallerBusy: return "caller-busy";
        case RuntimeAirstrikeVeto::CooldownActive: return "cooldown-active";
        case RuntimeAirstrikeVeto::InvalidTarget: return "invalid-target";
        case RuntimeAirstrikeVeto::InvalidBounds: return "invalid-bounds";
        case RuntimeAirstrikeVeto::InvalidAircraftCount: return "invalid-aircraft-count";
        case RuntimeAirstrikeVeto::InvalidAircraftType: return "invalid-aircraft-type";
        case RuntimeAirstrikeVeto::IllegalAircraftDefinition: return "illegal-aircraft-definition";
        case RuntimeAirstrikeVeto::CallerLimitReached: return "caller-limit-reached";
        case RuntimeAirstrikeVeto::StrikeLimitReached: return "strike-limit-reached";
        case RuntimeAirstrikeVeto::PlaneLimitReached: return "plane-limit-reached";
        case RuntimeAirstrikeVeto::IdExhausted: return "id-exhausted";
        case RuntimeAirstrikeVeto::ProjectileReceiptLimitReached:
            return "projectile-receipt-limit-reached";
    }
    return "unknown";
}

const char* runtimeAirstrikeActionName(RuntimeAirstrikeActionKind kind) {
    switch (kind) {
        case RuntimeAirstrikeActionKind::TargetMarked: return "target-marked";
        case RuntimeAirstrikeActionKind::SpawnAircraft: return "spawn-aircraft";
        case RuntimeAirstrikeActionKind::AssignIngressRoute: return "assign-ingress-route";
        case RuntimeAirstrikeActionKind::DropBomb: return "drop-bomb";
        case RuntimeAirstrikeActionKind::BeginEgress: return "begin-egress";
        case RuntimeAirstrikeActionKind::DespawnAircraft: return "despawn-aircraft";
        case RuntimeAirstrikeActionKind::CancelStrike: return "cancel-strike";
        case RuntimeAirstrikeActionKind::StartCallerCooldown: return "start-caller-cooldown";
    }
    return "unknown";
}

const char* runtimeAirstrikeCancelReasonName(
    RuntimeAirstrikeCancelReason reason
) {
    switch (reason) {
        case RuntimeAirstrikeCancelReason::None: return "none";
        case RuntimeAirstrikeCancelReason::TargetMissing: return "target-missing";
        case RuntimeAirstrikeCancelReason::TargetDead: return "target-dead";
        case RuntimeAirstrikeCancelReason::TargetHidden: return "target-hidden";
        case RuntimeAirstrikeCancelReason::TargetIllegal: return "target-illegal";
        case RuntimeAirstrikeCancelReason::CallerMissing: return "caller-missing";
        case RuntimeAirstrikeCancelReason::AircraftInvalid: return "aircraft-invalid";
        case RuntimeAirstrikeCancelReason::Reconciled: return "reconciled";
    }
    return "unknown";
}
