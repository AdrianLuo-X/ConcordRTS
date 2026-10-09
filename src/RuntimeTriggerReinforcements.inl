bool advanceRuntimeTriggerReinforcements(RuntimeWindowState& state) {
    RuntimePerfTraceScope originalMapTriggerPhaseTrace("advanceRuntimeTriggerReinforcements", state.tick);
    bool changed = false;
    int budget = 128; // Work per advancement pass, never a team's total size.
    auto& pending = state.triggers.reinforcements.pending;
    for (auto it = pending.begin(); it != pending.end() && budget > 0;) {
        auto& job = it->second;
        if (state.tick < job.nextAttemptTick) { ++it; continue; }
        const auto* team = findRuntimeTriggerTeamById(state, job.teamTypeId);
        if (!team) { ++it; continue; }
        bool blocked = false;
        while (job.memberIndex < job.members.size() && budget-- > 0) {
            const auto& member = job.members[job.memberIndex];
            const RuntimeBuildKind kind = runtimeBuildKindForUnitTypeId(state.rules, member.typeId);
            const auto* unitType = findRuntimeUnitTypeById(state.rules, member.typeId);
            if (!unitType) { blocked = true; break; }
            WorldObjectInstance probe;
            probe.instanceId = -1; probe.kind = runtimeKindForBuildItem(kind);
            probe.owner = job.owner; probe.type = member.typeId; probe.alive = true;
            probe.health = std::max(1, unitType->strength);
            int transportId = -1;
            if (team->full && unitType->passengers <= 0) {
                for (int id : job.objectIds) {
                    const auto* transport = findObjectById(state, id);
                    if (transport && runtimeObjectCanEnterTransport(state, probe, *transport, false)) {
                        transportId = id;
                        break;
                    }
                }
            }
            std::optional<RuntimeCellPoint> cell;
            if (transportId >= 0) {
                cell = runtimeCellPointForObject(*findObjectById(state, transportId));
            } else {
                const auto offset = formationOffsetForIndex(static_cast<int>(job.objectIds.size()));
                cell = findRuntimePassableCellNear(state, probe,
                    {job.originX + offset.x, job.originY + offset.y}, 6);
                if (!cell) {
                    const int radius = std::clamp(2 + std::max(std::abs(offset.x), std::abs(offset.y)), 6, 512);
                    cell = findRuntimePassableCellNear(state, probe, {job.originX, job.originY}, radius);
                }
            }
            if (!cell) { blocked = true; break; }
            auto* created = addRuntimeObjectInstanceForOwner(state, job.owner, kind,
                member.typeId, *cell, std::nullopt, true, transportId >= 0);
            if (!created) { blocked = true; break; }
            const int createdId = created->instanceId;
            created->mission = "TriggerTeam";
            if (transportId >= 0) {
                // Initial cargo bypasses ground occupancy, then uses the same
                // manifest, gunner and service transitions as normal boarding.
                created->alive = true;
                state.pendingTransportTargets[createdId] = transportId;
                const bool savedSuppression = state.runtimeSuppressPlayerAudioFeedback;
                state.runtimeSuppressPlayerAudioFeedback = true;
                const bool entered = completeRuntimeTransportEntry(state, createdId, true);
                state.runtimeSuppressPlayerAudioFeedback = savedSuppression;
                if (!entered) {
                    // No other object is created between allocation and this
                    // transition. Roll back a rejected initial cargo entry.
                    state.pendingTransportTargets.erase(createdId);
                    state.cloakObservedHealthByObject.erase(createdId);
                    releaseRuntimeInfantrySubcellReservation(state, createdId);
                    state.world.accessObjects().pop_back();
                    invalidateRuntimeObjectProjectionCache(state);
                    blocked = true;
                    break;
                }
            }
            job.objectIds.push_back(createdId);
            if (job.actionType == 107 && created->alive) {
                ensureRuntimeSpecialMechanismsInitialized(state);
                state.specialMechanisms.beginChronoReinforcementRecovery(
                    *created, state.tick, state.config.fixedTickHz);
                runtimeSpecialEventSink(state)({"ChronoReinforcementIn", state.tick, createdId, -1, 0,
                    {created->x, created->y}, "chrono reinforcement arrived"});
            }
            if (!job.teamCounted) { job.teamCounted = true; ++state.triggers.teamsCreated; }
            ++state.triggers.unitsCreated;
            state.triggers.campaignTeamObjects[toLowerAsciiLocal(trimAsciiLocal(team->id))].insert(createdId);
            ++job.memberProduced;
            if (job.memberProduced == member.count) { ++job.memberIndex; job.memberProduced = 0; }
            changed = true;
        }
        if (job.memberIndex == job.members.size()) {
            // A fully delivered chrono team remains pending until every live
            // arrival recovers. No script command or wait timer starts early.
            const bool recovering = job.actionType == 107 && std::any_of(
                job.objectIds.begin(), job.objectIds.end(), [&](int id) {
                    return findObjectById(state, id) != nullptr
                        && state.specialMechanisms.isChronoMovementLocked(id, state.tick);
                });
            if (recovering) {
                job.nextAttemptTick = state.tick == std::numeric_limits<int>::max()
                    ? state.tick : state.tick + 1;
                state.triggers.status = "recovering chrono team " + job.teamTypeId;
                ++it;
                continue;
            }
            const auto ids = job.objectIds;
            const RuntimeTeamType completedTeam = *team;
            it = pending.erase(it);
            initializeRuntimeNavigation(state);
            issueRuntimeTriggerTeamOrder(state, completedTeam, ids);
            state.triggers.status = "delivered team " + completedTeam.id + " units=" + std::to_string(ids.size());
            changed = true;
        } else {
            const int delay = blocked ? std::max(1, state.config.fixedTickHz / 4) : 1;
            job.nextAttemptTick = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), static_cast<std::int64_t>(state.tick) + delay));
            state.triggers.status = "pending team " + job.teamTypeId + " delivered=" + std::to_string(job.objectIds.size());
            ++it;
        }
    }
    return changed;
}

bool queueRuntimeTriggerReinforcement(
    RuntimeWindowState& state,
    const RuntimeTeamType& team,
    std::optional<RuntimeCellPoint> authoredOrigin,
    int actionType
) {
    const auto taskForce = state.triggers.taskForces.find(team.taskForceId);
    if (taskForce == state.triggers.taskForces.end() || taskForce->second.members.empty()) return false;
    auto& queue = state.triggers.reinforcements;
    if (queue.pending.size() >= 4096 || queue.nextInstanceId == std::numeric_limits<int>::max()) {
        state.triggers.status = "reinforcement queue capacity reached";
        return false;
    }
    auto origin = authoredOrigin;
    if (!origin) origin = runtimeWaypointCell(state, team.waypointId);
    if (!origin) {
        const auto base = state.triggers.campaignBaseCenterByOwner.find(toLowerAsciiLocal(trimAsciiLocal(team.house)));
        if (base != state.triggers.campaignBaseCenterByOwner.end()) origin = base->second;
    }
    if (!origin) origin = runtimeDefaultTriggerSpawnCell(state);
    if (!origin) { state.triggers.status = "team " + team.id + " has no spawn cell"; return false; }
    RuntimeTriggerReinforcement job;
    job.instanceId = queue.nextInstanceId;
    job.teamTypeId = team.id; job.owner = team.house.empty() ? "Neutral" : team.house;
    job.originX = origin->x; job.originY = origin->y; job.actionType = actionType;
    job.nextAttemptTick = state.tick;
    std::int64_t total = 0;
    for (const auto& member : taskForce->second.members) {
        const auto* type = findRuntimeUnitTypeById(state.rules, member.unitId);
        if (!type || member.count <= 0) {
            state.triggers.status = "team " + team.id + " has unsupported member " + member.unitId;
            return false;
        }
        total += member.count;
        job.members.push_back({type->id, member.count});
    }
    if (total > 1000000 || job.members.size() > 4096) {
        state.triggers.status = "team " + team.id + " exceeds reinforcement save capacity";
        return false;
    }
    if (team.full) {
        // Stock sov01 TaskForce lists 12 E2 before its SAPC. Full=yes means
        // the transport must exist before its initial passengers are created.
        std::stable_partition(job.members.begin(), job.members.end(), [&](const auto& member) {
            return findRuntimeUnitTypeById(state.rules, member.typeId)->passengers > 0;
        });
    }
    queue.pending.emplace(job.instanceId, std::move(job));
    ++queue.nextInstanceId;
    advanceRuntimeTriggerReinforcements(state);
    return true;
}
