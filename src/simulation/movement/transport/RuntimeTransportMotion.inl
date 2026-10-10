// Issue 10 transport-only physical motion. Accepted adjacent transfer provenance is saved separately.
struct RuntimeTransportMotionBody {
    double x = 0.0, y = 0.0, level = 0.0;
    bool onBridge = false;
};

RuntimeTransportMotionBody runtimeTransportMotionBody(const WorldObjectInstance& object) {
    const auto slot = worldObjectSubCellOffset(object);
    return {runtimeObjectRenderX(object) + slot.x,
        runtimeObjectRenderY(object) + slot.y, runtimeObjectRenderLevel(object),
        object.hasRenderPosition ? object.renderOnBridge : object.onBridge};
}

int runtimeTransportAuthorizedBoardingHostId(const RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& destination) {
    if (!passenger.alive || passenger.health <= 0 || passenger.airborne
        || (passenger.kind != WorldObjectKind::Infantry && passenger.kind != WorldObjectKind::Unit)) return -1;
    const auto intent = state.pendingTransportTargets.find(passenger.instanceId);
    const auto move = state.behaviors.find(passenger.instanceId);
    if (intent == state.pendingTransportTargets.end() || move == state.behaviors.end()
        || move->second.kind != RuntimeBehaviorKind::Move
        || move->second.pendingAllowedOccupiedObjectId != intent->second) return -1;
    const auto* host = findObjectById(state, intent->second);
    if (!host || host->kind != WorldObjectKind::Unit || !host->alive || host->health <= 0
        || host->airborne || host->renderMoveActive || destination != runtimeCellPointForObject(*host)
        || !runtimeObjectCanEnterTransport(state, passenger, *host, true, true)) return -1;
    return host->instanceId;
}

void restoreRuntimeTransportMotionBody(WorldObjectInstance& object,
    const RuntimeTransportMotionBody& body) {
    const auto slot = worldObjectSubCellOffset(object);
    object.hasRenderPosition = true;
    object.renderMoveActive = false;
    object.renderX = body.x - slot.x;
    object.renderY = body.y - slot.y;
    object.renderLevel = body.level;
    object.renderOnBridge = body.onBridge;
}

// Select the legal access terrain, never the internal hull's water classification.
// Caller supplies the length of the actual transfer curve (including any source bend).
std::optional<int> runtimeTransportMotionTransferTicks(const RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& accessCell, double curveLength) {
    if (!std::isfinite(curveLength) || curveLength <= 0.0) return std::nullopt;
    const double factor = runtimeTerrainMovementSpeedMultiplier(state, passenger, accessCell, accessCell);
    if (!std::isfinite(factor) || factor <= 0.0) return std::nullopt;
    const double ticks = runtimeMovementBaseIntervalTicks(state, passenger) * curveLength / factor;
    if (!std::isfinite(ticks)) return std::nullopt;
    return static_cast<int>(std::clamp(std::ceil(ticks), 1.0,
        double(std::numeric_limits<int>::max() - 1)));
}


using RuntimeTransportMotionPoint = std::array<double, 2>;

bool runtimeTransportUnitBoardingPending(const RuntimeWindowState& state, int id) {
    const auto pending = state.pendingTransportTargets.find(id);
    const auto* host = pending == state.pendingTransportTargets.end()
        ? nullptr : findObjectById(state, pending->second);
    return host && host->kind == WorldObjectKind::Unit;
}

// Ordinary outside-hull approach may detour, but never exchange its accepted access cell.
bool runtimeUnitTransportApproachAllowsBodyDetour(const RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const RuntimeObjectBehavior& move) {
    const auto pending = state.pendingTransportTargets.find(passenger.instanceId);
    if (pending == state.pendingTransportTargets.end()
        || !runtimeInfantryUsesGroundSubcells(passenger)
        || !passenger.alive || passenger.health <= 0
        || move.kind != RuntimeBehaviorKind::Move || move.autoAcquired
        || move.targetObjectId >= 0 || move.pendingHasSharedGoal
        || move.transportTransferKind != RuntimeTransportTransferKind::None
        || move.pendingAllowedOccupiedObjectId < 0
        || pending->second != move.pendingAllowedOccupiedObjectId) return false;
    const auto* host = findObjectById(state, pending->second);
    if (!host || host->kind != WorldObjectKind::Unit || !host->alive || host->health <= 0
        || host->airborne || host->renderMoveActive) return false;
    const auto hull = runtimeCellPointForObject(*host);
    if (move.moveTarget == hull || std::abs(move.moveTarget.x - hull.x) > 1
        || std::abs(move.moveTarget.y - hull.y) > 1) return false;
    return runtimeTransportAuthorizedBoardingHostId(state, passenger, hull) == host->instanceId;
}

bool runtimeTransportUnloadingPending(const RuntimeWindowState& state,
    const WorldObjectInstance& host) {
    if (host.kind != WorldObjectKind::Unit || !host.alive || host.health <= 0
        || toLowerAsciiLocal(host.mission) != toLowerAsciiLocal(runtimeMissionName(RuntimeMissionIntent::Unloading))
        || host.transportPassengerIds.empty()) return false;
    const auto profile = runtimeTransportProfileForObject(state, host);
    return profile.canTransport && profile.canUnload;
}

bool runtimeTransportSavedBoardingEndpointAllowed(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    double toLevel, bool toBridge, std::optional<std::pair<double, bool>> validatedHostPose) {
    const auto move = state.behaviors.find(object.instanceId);
    if (move == state.behaviors.end() || move->second.transportTransferKind != RuntimeTransportTransferKind::Boarding
        || move->second.transportTransferHostId < 0 || move->second.moveSegmentFrom != from
        || runtimeCellPointForObject(object) != from || from == to
        || std::abs(from.x-to.x)>1 || std::abs(from.y-to.y)>1) return false;
    const int hostId = runtimeTransportAuthorizedBoardingHostId(state, object, to);
    const auto* host = hostId < 0 ? nullptr : findObjectById(state, hostId);
    if (!host || hostId != move->second.transportTransferHostId) return false;
    const auto expected = validatedHostPose.value_or(std::pair<double,bool>{
        runtimeObjectRenderLevel(*host), host->hasRenderPosition ? host->renderOnBridge : host->onBridge});
    return std::isfinite(toLevel) && std::abs(toLevel-expected.first)<=1.0e-6 && toBridge==expected.second;
}

bool runtimeTransportBoardingReadyForManifest(const RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const RuntimeObjectBehavior& move) {
    if (move.kind != RuntimeBehaviorKind::Move || move.state != RuntimeBehaviorState::Complete
        || move.transportTransferKind != RuntimeTransportTransferKind::Boarding
        || move.transportTransferHostId < 0 || move.moveCooldownTicks > 0 || passenger.renderMoveActive
        || move.renderFrom != runtimeCellPointForObject(passenger) || move.renderFrom==move.renderTo
        || std::abs(move.renderFrom.x-move.renderTo.x)>1 || std::abs(move.renderFrom.y-move.renderTo.y)>1) return false;
    const int hostId = runtimeTransportAuthorizedBoardingHostId(state, passenger, move.renderTo);
    const auto* host = hostId < 0 ? nullptr : findObjectById(state, hostId);
    if (!host || hostId != move.transportTransferHostId) return false;
    const auto a=runtimeTransportMotionBody(passenger), b=runtimeTransportMotionBody(*host);
    return std::hypot(a.x-b.x,a.y-b.y)<=1.0e-9 && std::abs(a.level-b.level)<=1.0e-9
        && a.onBridge==b.onBridge;
}

bool cancelRuntimeTransportBoardingTransfer(RuntimeWindowState& state,
    WorldObjectInstance& passenger, RuntimeObjectBehavior& move) {
    const auto body=runtimeTransportMotionBody(passenger);
    restoreRuntimeTransportMotionBody(passenger,body);
    move.transportTransferHostId=-1; move.transportTransferKind=RuntimeTransportTransferKind::None;
    move.moveCooldownTicks=move.renderMoveTicksTotal=move.renderMoveTicksRemaining=0;
    move.moveSegmentFrom={-1,-1}; move.pendingAllowedOccupiedObjectId=-1;
    replaceRuntimeBehaviorPath(move,{}); move.pathRequestPending=false;
    move.kind=RuntimeBehaviorKind::None; move.state=RuntimeBehaviorState::Blocked;
    state.pendingTransportTargets.erase(passenger.instanceId);
    releaseRuntimeInfantrySubcellReservation(state,passenger.instanceId);
    setRuntimeMissionIntent(state,passenger,RuntimeMissionIntent::Idle,"transportBoardingCancelled");
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return true;
}

void cancelRuntimeTransportBoardingForMovingHost(RuntimeWindowState& state, int hostId) {
    std::vector<int> ids;
    for (const auto& [id,target]:state.pendingTransportTargets) {
        if (target!=hostId) continue;
        const auto found=state.behaviors.find(id);
        const auto* passenger = findObjectById(state, id);
        if (found!=state.behaviors.end()
            && ((found->second.transportTransferKind==RuntimeTransportTransferKind::Boarding
                    && found->second.transportTransferHostId==hostId)
                || (passenger && runtimePersonalChronoTransportPassenger(state, *passenger)
                    && found->second.kind==RuntimeBehaviorKind::Move
                    && found->second.pendingAllowedOccupiedObjectId==hostId))) ids.push_back(id);
    }
    std::sort(ids.begin(),ids.end());
    for (int id:ids) {
        auto* passenger=findMutableObjectById(state,id);
        const auto found=state.behaviors.find(id);
        if (passenger&&found!=state.behaviors.end()) {
            if (runtimePersonalChronoTransportPassenger(state, *passenger)) {
                if (eraseRuntimeBehavior(state, id)) {
                    clearRuntimeRenderPosition(*passenger);
                    setRuntimeMissionIntent(state, *passenger, RuntimeMissionIntent::Idle, "chronoTransportHostMoving");
                    refreshRuntimeObjectOccupancyAfterLocalChange(state);
                }
                continue;
            }
            cancelRuntimeTransportBoardingTransfer(state,*passenger,found->second);
            // The accepted host command can be saved before the next cleanup tick.
            eraseRuntimeBehavior(state,id);
        }
    }
}

bool advanceRuntimeTransportBoardingSegment(RuntimeWindowState& state,
    WorldObjectInstance& passenger, RuntimeObjectBehavior& move) {
    const int hostId=runtimeTransportAuthorizedBoardingHostId(state,passenger,move.renderTo);
    const auto* host=hostId<0?nullptr:findObjectById(state,hostId);
    const auto ownSlot=worldObjectSubCellOffset(passenger);
    const auto expected=host?runtimeTransportMotionBody(*host):RuntimeTransportMotionBody{};
    const bool valid=host && hostId==move.transportTransferHostId
        && runtimeCellPointForObject(passenger)==move.renderFrom
        && std::abs(move.renderTo.x+move.renderToOffsetX+ownSlot.x-expected.x)<=1.0e-9
        && std::abs(move.renderTo.y+move.renderToOffsetY+ownSlot.y-expected.y)<=1.0e-9
        && std::abs(move.renderToLevel-expected.level)<=1.0e-9 && move.renderToOnBridge==expected.onBridge;
    if (!valid) return cancelRuntimeTransportBoardingTransfer(state,passenger,move);
    if (move.moveCooldownTicks>0) {
        if (runtimeGroundRenderStepWaitsForStoppedBody(state,passenger,move)) {
            move.state=RuntimeBehaviorState::Blocked; return false;
        }
        move.state=RuntimeBehaviorState::Cooldown;
        const bool changed=advanceRuntimeRenderMove(state,passenger,move);
        --move.moveCooldownTicks;
        if (move.moveCooldownTicks<=0) move.state=RuntimeBehaviorState::Complete;
        return changed;
    }
    move.state=RuntimeBehaviorState::Complete;
    return false;
}

// Preflight only dedicated transport transfer legs. All ordinary navigation remains unchanged.
bool runtimeTransportTransferBodiesClear(RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const RuntimeTransportMotionBody& source,
    const RuntimeTransportMotionBody& destination, int hostId) {
    (void)runtimeTickOccupancyLookup(state);
    const RuntimeTransportMotionPoint a{source.x, source.y}, b{destination.x, destination.y};
    const auto pointDistance = [](const auto& p, const auto& x, const auto& y) {
        const double dx = y[0] - x[0], dy = y[1] - x[1];
        const double length = dx * dx + dy * dy;
        const double t = length > 1.0e-18
            ? std::clamp(((p[0] - x[0]) * dx + (p[1] - x[1]) * dy) / length, 0.0, 1.0) : 0.0;
        return std::pow(p[0] - x[0] - dx * t, 2) + std::pow(p[1] - x[1] - dy * t, 2);
    };
    const auto segmentDistance = [&](const auto& x, const auto& y, const auto& z, const auto& w) {
        const double dx = y[0] - x[0], dy = y[1] - x[1], ex = w[0] - z[0], ey = w[1] - z[1];
        const double denominator = dx * ey - dy * ex;
        if (std::abs(denominator) > 1.0e-12) {
            const double zx = z[0] - x[0], zy = z[1] - x[1];
            const double t = (zx * ey - zy * ex) / denominator;
            const double u = (zx * dy - zy * dx) / denominator;
            if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0.0;
        }
        return std::min({pointDistance(x, z, w), pointDistance(y, z, w),
            pointDistance(z, x, y), pointDistance(w, x, y)});
    };
    std::unordered_set<int> seen;
    bool clear = true;
    const auto inspect = [&](const WorldObjectInstance& other) {
        if (!clear || other.instanceId == passenger.instanceId || other.instanceId == hostId
            || !seen.insert(other.instanceId).second || !runtimeAllGroundBodyIsSpatialCandidate(state, other)
            || runtimeObjectOffGroundByMagBeam(state, other.instanceId)) return;
        if (runtimeCrusherUsesCrushingMovement(state, passenger)
            && runtimeCrusherCanCrushObject(state, passenger, other, runtimeCellPointForObject(other), true)) return;
        const auto slot = worldObjectSubCellOffset(other);
        const RuntimeTransportMotionPoint current{runtimeObjectRenderX(other) + slot.x,
            runtimeObjectRenderY(other) + slot.y};
        const bool layer = other.hasRenderPosition ? other.renderOnBridge : other.onBridge;
        const bool foot = passenger.kind == WorldObjectKind::Infantry;
        const bool otherFoot = other.kind == WorldObjectKind::Infantry;
        const double minimum = foot && otherFoot ? 0.5 : (!foot && !otherFoot ? 1.0 : 0.75);
        const double squared = minimum * minimum;
        const auto check = [&](const auto& x, const auto& y, bool otherLayer) {
            const RuntimeTransportMotionPoint middle{(a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5};
            if (source.onBridge == otherLayer && segmentDistance(a,
                    source.onBridge == destination.onBridge ? b : middle, x, y) + 1.0e-9 < squared) clear = false;
            if (source.onBridge != destination.onBridge && destination.onBridge == otherLayer
                && segmentDistance(middle, b, x, y) + 1.0e-9 < squared) clear = false;
        };
        check(current, current, layer);
        if (!clear || !other.renderMoveActive) return;
        const auto found = state.behaviors.find(other.instanceId);
        if (found == state.behaviors.end() || found->second.renderMoveTicksTotal <= 0) return;
        const auto& move = found->second;
        const double progress = runtimeVehicleMoveRenderProgress(move);
        std::vector<double> cuts{progress, 1.0};
        if (move.renderFromOnBridge != move.renderToOnBridge && progress < 0.5) cuts.push_back(0.5);
        if (runtimeInfantryMoveUsesCommittedCorner(other, move)) {
            const double first = std::hypot(move.renderFromOffsetX, move.renderFromOffsetY);
            const double second = std::hypot(double(move.renderTo.x - move.renderFrom.x),
                double(move.renderTo.y - move.renderFrom.y));
            const int ticks = std::max(2, move.renderMoveTicksTotal);
            const double pivot = double(std::clamp(static_cast<int>(std::ceil(ticks * first / (first + second))),
                1, ticks - 1)) / ticks;
            if (pivot > progress) cuts.push_back(pivot);
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t n = 1; clear && n < cuts.size(); ++n) {
            auto first = runtimeGroundRenderMovePoint(other, move, cuts[n - 1]);
            auto last = runtimeGroundRenderMovePoint(other, move, cuts[n]);
            first[0] += slot.x; first[1] += slot.y; last[0] += slot.x; last[1] += slot.y;
            check(first, last, (cuts[n - 1] + cuts[n]) * 0.5 < 0.5
                ? move.renderFromOnBridge : move.renderToOnBridge);
        }
    };
    const int left = static_cast<int>(std::floor(std::min(a[0], b[0]) + 0.5)) - 1;
    const int right = static_cast<int>(std::floor(std::max(a[0], b[0]) + 0.5)) + 1;
    const int top = static_cast<int>(std::floor(std::min(a[1], b[1]) + 0.5)) - 1;
    const int bottom = static_cast<int>(std::floor(std::max(a[1], b[1]) + 0.5)) + 1;
    for (int y = std::max(0, top); clear && y <= std::min(511, bottom); ++y)
        for (int x = std::max(0, left); clear && x <= std::min(511, right); ++x)
            forEachRuntimeAllGroundBodyCandidateAtCell(state, {x, y}, inspect);
    return clear;
}

bool runtimeTransportTransferTransitionClear(RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& from,
    const RuntimeCellPoint& to, int hostId, bool boarding) {
    if (from == to || std::abs(to.x - from.x) > 1 || std::abs(to.y - from.y) > 1) return false;
    const auto legal = [&](const RuntimeCellPoint& cell) {
        return findRuntimePassableCellNear(state, passenger, cell, 0, -1).has_value();
    };
    // Only the one verified hull cell is internal. All outside and diagonal cells remain ordinary terrain.
    if (!(boarding ? legal(from) : legal(to))) return false;
    if (runtimeFoundationBlocksMovement(state, passenger, from, hostId, false, RuntimeGatePassageMode::Commit)
        || runtimeFoundationBlocksMovement(state, passenger, to, hostId, false, RuntimeGatePassageMode::Commit)) return false;
    if (from.x != to.x && from.y != to.y)
        for (const RuntimeCellPoint side : {RuntimeCellPoint{from.x, to.y}, RuntimeCellPoint{to.x, from.y}})
            if (!legal(side) || runtimeFoundationBlocksMovement(state, passenger, side, hostId,
                    false, RuntimeGatePassageMode::Commit)) return false;
    return true;
}

// A fractional stopped hull can start outside its committed navigation cell.
// Check the actual straight body line against every touched foundation tile.
bool runtimeTransportTransferFoundationClear(const RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const RuntimeTransportMotionBody& source,
    const RuntimeTransportMotionBody& destination, int hostId) {
    if (!std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.level)
        || !std::isfinite(destination.x) || !std::isfinite(destination.y) || !std::isfinite(destination.level)) return false;
    const int left = static_cast<int>(std::floor(std::min(source.x, destination.x) + 0.5));
    const int right = static_cast<int>(std::floor(std::max(source.x, destination.x) + 0.5));
    const int top = static_cast<int>(std::floor(std::min(source.y, destination.y) + 0.5));
    const int bottom = static_cast<int>(std::floor(std::max(source.y, destination.y) + 0.5));
    if (left < 0 || top < 0 || right >= 512 || bottom >= 512 || (right-left+1)*(bottom-top+1) > 25) return false;
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        double first = 0.0, last = 1.0;
        const auto clip = [&](double p, double delta, double low, double high) {
            if (std::abs(delta) <= 1.0e-12) return p >= low && p <= high;
            double a = (low - p) / delta, b = (high - p) / delta;
            if (a > b) std::swap(a, b);
            first = std::max(first, a); last = std::min(last, b);
            return first <= last;
        };
        if (clip(source.x, destination.x-source.x, x-0.5, x+0.5)
            && clip(source.y, destination.y-source.y, y-0.5, y+0.5)
            && runtimeFoundationBlocksMovement(state, passenger, {x,y}, hostId,
                false, RuntimeGatePassageMode::Commit)) return false;
    }
    return true;
}

bool beginRuntimeTransportGroundTransfer(RuntimeWindowState& state,
    WorldObjectInstance& passenger, const WorldObjectInstance& host,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    const RuntimeTransportMotionBody& source, const RuntimeTransportMotionBody& destination,
    int destinationSlot, bool boarding, const std::vector<RuntimeCellPoint>& remainingPath = {}) {
    if (!runtimeTransportTransferTransitionClear(state, passenger, from, to, host.instanceId, boarding)
        || !runtimeTransportTransferFoundationClear(state, passenger, source, destination, host.instanceId)
        || !runtimeTransportTransferBodiesClear(state, passenger, source, destination, host.instanceId)) return false;
    const auto ticks = runtimeTransportMotionTransferTicks(state, passenger, boarding ? from : to,
        std::hypot(destination.x - source.x, destination.y - source.y));
    if (!ticks) return false;
    const auto found = state.behaviors.find(passenger.instanceId);
    RuntimeObjectBehavior move = found == state.behaviors.end() ? RuntimeObjectBehavior{} : found->second;
    move.kind = RuntimeBehaviorKind::Move;
    move.state = RuntimeBehaviorState::Moving;
    move.pathRequestPending = false;
    move.moveTarget = remainingPath.empty() ? to : remainingPath.back();
    move.pendingAllowedOccupiedObjectId = host.instanceId;
    move.transportTransferHostId = host.instanceId;
    move.transportTransferKind = boarding ? RuntimeTransportTransferKind::Boarding : RuntimeTransportTransferKind::Egress;
    move.renderArrivalDistanceAfter = -1.0;
    replaceRuntimeBehaviorPath(move, remainingPath);
    releaseRuntimeInfantrySubcellReservation(state, passenger.instanceId);
    passenger.alive = true;
    passenger.airborne = false;
    passenger.dockedToObjectId = passenger.dockIndex = -1;
    passenger.mission = boarding ? "Entering" : "Move";
    // Boarding owns its original legal access cell/slot until the body reaches the hull.
    // Egress owns its real first exit cell immediately, preserving the accepted source pose.
    if (!boarding) {
        setRuntimeObjectCell(state, passenger, to, false);
        passenger.subCell = destinationSlot;
    }
    restoreRuntimeTransportMotionBody(passenger, source);
    move.moveCooldownTicks = *ticks;
    beginRuntimeRenderMove(passenger, move, from, to, source.level, source.onBridge, *ticks, &state);
    const auto slot = worldObjectSubCellOffset(passenger);
    move.renderToOffsetX = destination.x - to.x - slot.x;
    move.renderToOffsetY = destination.y - to.y - slot.y;
    move.renderToLevel = destination.level;
    move.renderToOnBridge = destination.onBridge;
    alignRuntimeFacingToRenderMove(passenger, move);
    state.behaviors[passenger.instanceId] = std::move(move);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return true;
}

bool advanceRuntimeUnitTransportBoarding(RuntimeWindowState& state,
    WorldObjectInstance& passenger, const WorldObjectInstance& host) {
    if (passenger.renderMoveActive || host.airborne || host.renderMoveActive) return false;
    if (runtimePersonalChronoTransportPassenger(state, passenger))
        return runtimeChronoTransportArrivalReady(state, passenger, host);
    const auto source = runtimeTransportMotionBody(passenger), destination = runtimeTransportMotionBody(host);
    const auto from = runtimeCellPointForObject(passenger), to = runtimeCellPointForObject(host);
    if (std::hypot(source.x - destination.x, source.y - destination.y) <= 1.0e-9
        && std::abs(source.level - destination.level) <= 1.0e-9
        && source.onBridge == destination.onBridge) return true;
    if (from == to) return false; // Caller takes a real adjacent access route; never fabricate a source.
    (void)beginRuntimeTransportGroundTransfer(state, passenger, host, from, to, source, destination, -1, true);
    return false;
}

int unloadRuntimeTransport(RuntimeWindowState&, WorldObjectInstance&);

void prepareRuntimeTransportUnloadMotion(RuntimeWindowState& state, WorldObjectInstance& host) {
    const auto previous = state.behaviors.find(host.instanceId);
    const std::optional<RuntimeObjectBehavior> accepted = previous != state.behaviors.end()
        && previous->second.kind == RuntimeBehaviorKind::Move && previous->second.moveCooldownTicks > 0
        ? std::optional<RuntimeObjectBehavior>(previous->second) : std::nullopt;
    const auto body = runtimeTransportMotionBody(host);
    const bool hadPose = host.hasRenderPosition;
    eraseRuntimeBehavior(state, host.instanceId);
    if (host.airborne && runtimeJumpjetProfile(state, host)) {
        const auto flight = state.jumpjetFlights.find(host.instanceId);
        if (flight != state.jumpjetFlights.end() && (flight->second.active || flight->second.stopped)) {
            RuntimeObjectBehavior last = accepted.value_or(RuntimeObjectBehavior{});
            last.kind = RuntimeBehaviorKind::Move;
            last.state = RuntimeBehaviorState::Moving;
            last.path.clear(); last.pathRequestPending = false;
            last.moveTarget = runtimeCellPointForObject(host);
            resumeRuntimeJumpjetSegment(state, host, last);
            state.behaviors[host.instanceId] = std::move(last);
        }
        applyRuntimeJumpjetPose(state, host);
    } else if (accepted) {
        auto last = *accepted;
        last.path.clear(); last.pathRequestPending = false;
        last.moveTarget = runtimeCellPointForObject(host);
        inheritRuntimeVehicleMoveSegment(host, last, *accepted);
        state.behaviors[host.instanceId] = std::move(last);
    } else if (hadPose) restoreRuntimeTransportMotionBody(host, body);
}

int unloadRuntimeUnitTransportMotion(RuntimeWindowState& state, WorldObjectInstance& host) {
    if (!host.alive || host.health <= 0 || host.airborne || host.renderMoveActive) return 0;
    const auto hostCell = runtimeCellPointForObject(host);
    const auto body = runtimeTransportMotionBody(host);
    int unloaded = 0;
    const auto manifest = host.transportPassengerIds;
    // The first physical exit is reserved immediately; its later outer goal is not.
    // Count accepted foot goals once, without reserving or changing any slot.
    std::map<std::pair<int, bool>, int> futureFootGoals;
    if (!runtimeAmphibiousTransport(state, host)) {
        const auto poseContext = runtimeGroundPoseContext(state);
        for (const auto& [otherId, move] : state.behaviors) {
            if (move.kind != RuntimeBehaviorKind::Move
                || move.moveTarget.x < 0 || move.moveTarget.y < 0
                || move.moveTarget.x >= 512 || move.moveTarget.y >= 512
                || std::max(std::abs(move.moveTarget.x-hostCell.x), std::abs(move.moveTarget.y-hostCell.y)) != 2) continue;
            const auto* other = findObjectById(state, otherId);
            if (!other || other->health <= 0 || !runtimeInfantryUsesGroundSubcells(*other)) continue;
            const bool atGoal = runtimeCellPointForObject(*other) == move.moveTarget;
            const bool goalBridge = atGoal ? other->onBridge
                : move.renderMoveTicksTotal > 0 && move.renderTo == move.moveTarget
                    ? move.renderToOnBridge : runtimeCellIsBridge(state, move.moveTarget.x, move.moveTarget.y);
            if (atGoal && RuntimeSubcellSystemState::isActiveSubCell(other->subCell)) continue;
            const auto reserved = state.subcells.reservations().find(otherId);
            if (reserved != state.subcells.reservations().end()
                && RuntimeSubcellSystemState::isActiveSubCell(reserved->second.subCell)
                && reserved->second.destination.x == move.moveTarget.x
                && reserved->second.destination.y == move.moveTarget.y
                && reserved->second.destination.onBridge == goalBridge) continue;
            const auto held = runtimeAdditionalGroundPoseOccupancy(*other, &poseContext);
            if (held && held->x == move.moveTarget.x && held->y == move.moveTarget.y
                && held->onBridge == goalBridge && RuntimeSubcellSystemState::isActiveSubCell(held->subCell)) continue;
            ++futureFootGoals[{runtimeCellKey(move.moveTarget.x, move.moveTarget.y), goalBridge}];
        }
    }
    for (int id : manifest) {
        auto* stored = findMutableRuntimeObjectByIdIncludingInactive(state, id);
        if (!stored || stored->alive || stored->health <= 0) continue;
        auto probe = *stored;
        probe.alive = true; probe.airborne = false; probe.mission = "Move";
        std::vector<RuntimeCellPoint> candidates;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) candidates.push_back({hostCell.x + dx, hostCell.y + dy});
        std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
            const double da = runtimeCellDistance(hostCell, a), db = runtimeCellDistance(hostCell, b);
            if (da != db) return da < db;
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        bool accepted = false;
        for (const auto& first : candidates) {
            if (!findRuntimePassableCellNear(state, probe, first, 0, -1)
                || std::abs(terrainLevelAtRuntimeCell(state, first.x, first.y) - host.level) > 1) continue;
            std::vector<int> slots{-1};
            if (probe.kind == WorldObjectKind::Infantry) slots = runtimeAvailableInfantrySubcells(state, probe, first);
            for (int slot : slots) {
                const auto offset = worldInfantrySubCellOffset(slot);
                const RuntimeTransportMotionBody destination{first.x + offset.x, first.y + offset.y,
                    double(terrainLevelAtRuntimeCell(state, first.x, first.y)), runtimeCellIsBridge(state, first.x, first.y)};
                const double hullClearance = probe.kind == WorldObjectKind::Infantry ? 0.75 : 1.0;
                if (destination.onBridge == body.onBridge
                    && std::hypot(destination.x-body.x, destination.y-body.y) + 1.0e-9 < hullClearance) continue;
                if (!runtimeTransportTransferTransitionClear(state, probe, hostCell, first, host.instanceId, false)
                    || !runtimeTransportTransferBodiesClear(state, probe, body, destination, host.instanceId)) continue;
                std::vector<RuntimeCellPoint> remainingPath;
                if (!runtimeAmphibiousTransport(state, host)) {
                    auto walking = probe;
                    walking.x = first.x; walking.y = first.y; walking.cell = runtimeCellKey(first.x, first.y);
                    walking.level = terrainLevelAtRuntimeCell(state, first.x, first.y);
                    walking.onBridge = runtimeCellIsBridge(state, first.x, first.y); walking.subCell = slot;
                    walking.hasRenderPosition = walking.renderMoveActive = false;
                    for (int dy = -2; remainingPath.empty() && dy <= 2; ++dy)
                        for (int dx = -2; remainingPath.empty() && dx <= 2; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != 2) continue;
                            const RuntimeCellPoint outer{hostCell.x + dx, hostCell.y + dy};
                            if (!findRuntimePassableCellNear(state, walking, outer, 0, -1)) continue;
                            if (runtimeInfantryUsesGroundSubcells(walking)) {
                                const auto key = std::make_pair(runtimeCellKey(outer.x, outer.y),
                                    runtimeCellIsBridge(state, outer.x, outer.y));
                                const auto claimed = futureFootGoals.find(key);
                                if (claimed != futureFootGoals.end()
                                    && runtimeAvailableInfantrySubcells(state, walking, outer).size()
                                        <= static_cast<std::size_t>(claimed->second)) continue;
                            }
                            remainingPath = findRuntimePath(state, walking, first, outer, -1);
                        }
                }
                // Commit the adjacent exit, then walk the real route to a legal radius2 endpoint.
                if (!beginRuntimeTransportGroundTransfer(state, *stored, host, hostCell, first,
                        body, destination, slot, false, remainingPath)) continue;
                auto& cargo = host.transportPassengerIds;
                cargo.erase(std::remove(cargo.begin(), cargo.end(), id), cargo.end());
                syncRuntimeTransportCargoPositions(state, *stored);
                accepted = true; ++unloaded; break;
            }
            if (accepted) break;
        }
        if (accepted) break; // Later cargo waits on real body clearance, not a fixed delay.
    }
    refreshRuntimeTransportGunnerState(state, host);
    if (host.transportPassengerIds.empty()) setRuntimeMissionIntent(state, host,
        RuntimeMissionIntent::Idle, "transportUnloadComplete");
    else setRuntimeMissionIntent(state, host, RuntimeMissionIntent::Unloading, "transportUnloadPending");
    if (unloaded) refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return unloaded;
}

bool advanceRuntimeTransportUnloading(RuntimeWindowState& state) {
    std::vector<int> ids;
    for (const auto& object : state.world.getObjects())
        if (runtimeTransportUnloadingPending(state, object)) ids.push_back(object.instanceId);
    bool changed = false;
    for (int id : ids) if (auto* host = findMutableObjectById(state, id))
        changed = unloadRuntimeTransport(state, *host) > 0 || changed;
    return changed;
}

bool runtimeTransportBoardingAccessBodyClear(RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const WorldObjectInstance& host, const RuntimeCellPoint& access) {
    auto probe = passenger;
    const bool actual = runtimeCellPointForObject(passenger) == access && !passenger.renderMoveActive;
    int slot = -1;
    if (probe.kind == WorldObjectKind::Infantry) {
        slot = actual ? probe.subCell : RuntimeSubcellSystemState::selectAvailableSubCell(
            runtimeAvailableInfantrySubcells(state, probe, access), probe.subCell);
        if (!RuntimeSubcellSystemState::isActiveSubCell(slot)) return false;
    }
    const auto offset = worldInfantrySubCellOffset(slot);
    const auto source = actual ? runtimeTransportMotionBody(passenger)
        : RuntimeTransportMotionBody{access.x + offset.x, access.y + offset.y,
            double(terrainLevelAtRuntimeCell(state, access.x, access.y)), runtimeCellIsBridge(state, access.x, access.y)};
    return runtimeTransportTransferTransitionClear(state, probe, access, runtimeCellPointForObject(host), host.instanceId, true)
        && runtimeTransportTransferFoundationClear(state, probe, source, runtimeTransportMotionBody(host), host.instanceId)
        && runtimeTransportTransferBodiesClear(state, probe, source, runtimeTransportMotionBody(host), host.instanceId);
}
