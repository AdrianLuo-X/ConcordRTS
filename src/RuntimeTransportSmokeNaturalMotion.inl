// Focused ability-smoke setup: real passenger Move/Chrono and particle clocks only.
// These probes retain their original combat/outcome assertions; full motion uses the separate fixture.
bool waitForRuntimeTransportSmokeMotion(RuntimeWindowState& state,
    int hostId, int passengerId, bool aboard = true) {
    const auto diagnose = [&](const char* reason) {
        const auto* h = findRuntimeObjectByIdIncludingInactive(state, hostId);
        const auto* p = findRuntimeObjectByIdIncludingInactive(state, passengerId);
        std::cout << "Transport smoke wait failure reason=" << reason << " tick=" << state.tick
            << " aboard=" << aboard << " host=" << hostId << " passenger=" << passengerId;
        if (h) { std::cout << " hostCargo="; for (int id : h->transportPassengerIds) std::cout << id << ','; }
        if (p) std::cout << " alive=" << p->alive << " hp=" << p->health << " cell=" << p->x << ',' << p->y
            << " body=" << runtimeObjectRenderX(*p) << ',' << runtimeObjectRenderY(*p)
            << " active=" << p->renderMoveActive << " mission=" << p->mission;
        const auto intent=state.pendingTransportTargets.find(passengerId);
        if(intent!=state.pendingTransportTargets.end())std::cout << " pendingHost=" << intent->second;
        const auto found=state.behaviors.find(passengerId);
        if(found!=state.behaviors.end()) { const auto& m=found->second;
            std::cout << " kind=" << int(m.kind) << " state=" << int(m.state) << " cooldown=" << m.moveCooldownTicks
                << " remaining=" << m.renderMoveTicksRemaining << " total=" << m.renderMoveTicksTotal
                << " target=" << m.moveTarget.x << ',' << m.moveTarget.y << " path=" << m.path.size()
                << " allowedHost=" << m.pendingAllowedOccupiedObjectId << " transfer=" << int(m.transportTransferKind); }
        std::cout << '\n';
        for (const auto* actor : {h, p}) if (actor) {
            std::cout << "Transport smoke actual cell actor=" << actor->instanceId << " type=" << actor->type
                << " owner=" << actor->owner << " cell=" << actor->x << ',' << actor->y;
            const auto foundCell=state.terrainByCell.find(runtimeCellKey(actor->x,actor->y));
            if(foundCell==state.terrainByCell.end()||!foundCell->second)std::cout << " terrain=missing";
            else {const auto& c=*foundCell->second;const auto info=runtimeTerrainInfoForCell(state,c);
                std::cout << " tile=" << c.tileIndex << " sub=" << c.subTileIndex << " level=" << c.level
                    << " passable=" << info.passable << " water=" << info.water << " ramp=" << info.ramp << " bridge=" << info.bridge;}
            std::cout << '\n';
        }
        if(h&&p&&h->alive&&p->alive) {
            std::cout << "Transport smoke actual eligibility=" << runtimeObjectCanEnterTransport(state,*p,*h,true)
                << " access=";
            const auto access=runtimeTransportEntryCell(state,*p,*h);
            if(access)std::cout << access->x << ',' << access->y;else std::cout << "none";
            std::cout << '\n';
        }
        for(const auto& actor:state.world.getObjects())if(actor.alive)std::cout << "Transport smoke live body id="
            << actor.instanceId << " type=" << actor.type << " cell=" << actor.x << ',' << actor.y
            << " body=" << runtimeObjectRenderX(actor) << ',' << runtimeObjectRenderY(actor)
            << " slot=" << actor.subCell << " active=" << actor.renderMoveActive << '\n';
        return false;
    };
    for (int waitTick = 0; waitTick < std::max(1, state.config.fixedTickHz) * 60; ++waitTick) {
        const auto* host = findObjectById(state, hostId);
        const auto* passenger = findRuntimeObjectByIdIncludingInactive(state, passengerId);
        if (!host || host->health <= 0 || !passenger || passenger->health <= 0) return diagnose("actor");
        const bool contained = std::find(host->transportPassengerIds.begin(),
            host->transportPassengerIds.end(), passengerId) != host->transportPassengerIds.end();
        if (aboard ? (!passenger->alive && contained
                && !state.pendingTransportTargets.contains(passengerId))
            : (passenger->alive && !contained && !passenger->renderMoveActive
                && !state.behaviors.contains(passengerId))) return true;
        if (aboard) {
            const auto pending = state.pendingTransportTargets.find(passengerId);
            const auto move = state.behaviors.find(passengerId);
            if (pending == state.pendingTransportTargets.end() || pending->second != hostId
                || move == state.behaviors.end() || move->second.kind != RuntimeBehaviorKind::Move
                || move->second.pendingAllowedOccupiedObjectId != hostId) return diagnose("boarding authority");
        }
        RuntimeSnapshotActiveStateScope active(state);
        ++state.tick;
        ensureRuntimeSpecialMechanismsInitialized(state);
        const auto generatedTerrain = state.terrainByCell.empty()
            ? buildRuntimeTerrainLookup(state.world) : std::unordered_map<int, const TerrainCell*>{};
        const auto& terrain = state.terrainByCell.empty() ? generatedTerrain : state.terrainByCell;
        std::optional<std::unordered_map<int, int>> occupied;
        const auto occupancy = [&]() -> const std::unordered_map<int, int>& {
            if (!occupied) occupied.emplace(buildRuntimeOccupancyLookup(state));
            return *occupied;
        };
        const SpecialCellPredicate legal = [&](int x, int y, int objectId) {
            const auto* mover = findObjectById(state, objectId);
            if (mover) {
                if (const auto entry = runtimeChronoTransportDestinationAdmission(state, *mover, {x, y})) return *entry;
            }
            return mover && runtimeCellIsPassable(state, terrain, occupancy(), {x, y}, *mover, objectId);
        };
        const SpecialCellPredicate blocked = [&](int x, int y, int objectId) {
            if (const auto* mover = findObjectById(state, objectId)) {
                if (const auto entry = runtimeChronoTransportDestinationAdmission(state, *mover, {x, y})) return !*entry;
            }
            return runtimeChronoCellIsOccupied(state, occupancy(), x, y, objectId);
        };
        const auto specialSink = runtimeSpecialEventSink(state);
        const SpecialEventSink chronoSink = [&](const SpecialMechanismEvent& event) {
            specialSink(event);
            if (event.kind == "ChronoWarpIn") occupied.reset();
        };
        (void)state.specialMechanisms.tickChronoMoves(state.world.accessObjects(),
            state.tick, legal, blocked, chronoSink);
        // Match the production arrival transaction before advancing any Move.
        if (aboard) {
            const auto* arriving = findObjectById(state, passengerId);
            const auto* destinationHost = findObjectById(state, hostId);
            if (arriving && destinationHost
                && runtimeChronoTransportArrivalReady(state, *arriving, *destinationHost)
                && completeRuntimeTransportEntry(state, passengerId)) {
                eraseRuntimeBehavior(state, passengerId);
                (void)advanceRuntimeParticles(state);
                completeRuntimeWorldRefresh(state);
                continue;
            }
        }
        processRuntimePathRequests(state);
        prepareRuntimeInfantryStepReservations(state);
        auto* mover = findMutableObjectById(state, passengerId);
        auto behavior = state.behaviors.find(passengerId);
        if (!mover || behavior == state.behaviors.end()
            || behavior->second.kind != RuntimeBehaviorKind::Move) return diagnose("move behavior");
        {
            RuntimeInfantryWaterBehaviorContextScope waterMetadataContext(state);
            (void)advanceRuntimeMoveBehavior(state, *mover, behavior->second);
        }
        // Chrono and boarding transactions can replace world storage.
        mover = findMutableObjectById(state, passengerId);
        behavior = state.behaviors.find(passengerId);
        if (mover && behavior != state.behaviors.end()
            && behavior->second.state == RuntimeBehaviorState::Complete
            && (runtimeCellPointForObject(*mover) == behavior->second.moveTarget
                || runtimeTransportBoardingReadyForManifest(state, *mover, behavior->second))
            && behavior->second.moveCooldownTicks <= 0 && !mover->renderMoveActive) {
            if (state.pendingTransportTargets.contains(passengerId)) {
                // A false return may start the final real entrance segment.
                // Retain its freshly accepted body, path and clocks.
                (void)completeRuntimeTransportEntry(state, passengerId);
            } else if (!aboard) {
                eraseRuntimeBehavior(state, passengerId);
            }
        }
        (void)advanceRuntimeParticles(state);
        completeRuntimeWorldRefresh(state);
    }
    return diagnose("timeout");
}

// Keep the original mixed-passenger count: each count comes from a real accepted exit.
int unloadRuntimeTransportSmokeNaturally(RuntimeWindowState& state, int hostId,
    const std::vector<int>& passengerIds) {
    const auto* host = findObjectById(state, hostId);
    if (!host || host->transportPassengerIds != passengerIds) return -1;
    std::vector<int> health;
    for (int id : passengerIds) {
        const auto* passenger = findRuntimeObjectByIdIncludingInactive(state, id);
        if (!passenger || passenger->alive || passenger->health <= 0) return -1;
        health.push_back(passenger->health);
    }
    int accepted = 0;
    for (int id : passengerIds) {
        auto* actualHost = findMutableObjectById(state, hostId);
        if (!actualHost || actualHost->transportPassengerIds.empty()
            || actualHost->transportPassengerIds.front() != id) return -1;
        const int one = unloadRuntimeTransport(state, *actualHost);
        if (one != 1) { std::cout << "Transport smoke exit rejected passenger=" << id << " actual=" << one << '\n'; return -1; }
        if (!waitForRuntimeTransportSmokeMotion(state, hostId, id, false)) return -1;
        accepted += one;
    }
    host = findObjectById(state, hostId);
    if (!host || !host->transportPassengerIds.empty()) return -1;
    for (std::size_t n = 0; n < passengerIds.size(); ++n) {
        const auto* passenger = findObjectById(state, passengerIds[n]);
        if (!passenger || passenger->health != health[n] || passenger->airborne
            || passenger->renderMoveActive || state.behaviors.contains(passengerIds[n])) return -1;
    }
    return accepted;
}
