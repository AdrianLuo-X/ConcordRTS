// Included after the shared multiplayer snapshot fixture and identity helpers.
// This covers the whole v15 writer, staged loader and authority-only commit
// with live world identities behind every newly persistent detail record.


bool runRuntimeDetailSnapshotSmokeProbe(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "nonempty detail snapshot: " + why; return false;
    };
    auto source = std::make_unique<RuntimeWindowState>();
    auto destination = std::make_unique<RuntimeWindowState>();
    if (!configureRuntimeDetailSnapshotSmokeWorld(*source, false, error)
        || !configureRuntimeDetailSnapshotSmokeWorld(*destination, true, error)) return false;
    RuntimeMultiplayerSnapshotMemoryImage image;
    RuntimeMultiplayerSnapshotIdentity identity;
    {
        RuntimeSnapshotActiveStateScope scope(*source);
        // The native sale renderer is tested separately. Here retain its
        // exact timer in an established multiplayer authority/house fixture.
        auto selling = *findObjectById(*source, 44);
        selling.instanceId = 49;
        selling.x = 15; selling.y = 15; selling.cell = runtimeCellKey(15,15);
        selling.alive = false; selling.mission = "Sold"; selling.rubble = false;
        selling.buildAnimTicksTotal = 96; selling.buildAnimTicksRemaining = 47;
        source->world.accessObjects().push_back(selling);
        auto* shooter = findMutableObjectById(*source, 41);
        const auto* target = findObjectById(*source, 42);
        if (shooter == nullptr || target == nullptr) return fail("live shooter/target missing");
        shooter->harvestAutoSearchSuspended = true;
        shooter->hasBeenCaptured = true;
        shooter->factoryExitHostId = 44;
        shooter->radiationEruptionCooldownTicksRemaining = 73;
        const auto profile = runtimeWeaponProfileForObject(*source, *shooter, target);
        if (profile.weapon == nullptr || beginRuntimeCombatBurst(*source, *shooter,
                *target, *profile.weapon, profile, 1.0) != 1) return fail("real burst creation");
        RuntimeObjectBehavior attack;
        attack.kind = RuntimeBehaviorKind::Attack;
        attack.state = RuntimeBehaviorState::Attacking;
        attack.targetObjectId = target->instanceId;
        source->behaviors[shooter->instanceId] = attack;
        RuntimeMultiplayerGameplayTargetOrderCommand queued;
        queued.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
        queued.objectIds = {41};
        queued.target = RuntimeMultiplayerGameplayCellTarget{{6, 5}};
        if (!source->playerCommands.append("skirmishplayer1", queued)
            || !applyRuntimePlayerPrimaryFactory(*source, "SkirmishPlayer1", 44)
            || !applyRuntimePlayerRallyPoint(*source, "SkirmishPlayer1", 44, {10, 8}))
            return fail("queue/primary factory/rally creation");
        source->playerCommands.following[43] = {"skirmishplayer1", 41, {2, 2}};
        if (source->combatDetails.bursts.empty() || source->combatDetails.fireCursors.empty()
            || source->projectiles.empty()) return fail("combat records unexpectedly empty");
        source->projectiles.front().homingInitialized = true;
        source->projectiles.front().homingYaw = 0.625;
        source->projectiles.front().homingPitch = -0.375;
        source->computerMiningByOwner["skirmishplayer3"] = {12, 450, 3, 7, 2, 4};
        auto& tactics = source->triggers.computerTacticsByOwner["skirmishplayer3"];
        tactics.nextTriggerTick = 200; tactics.selectionSerial = 1; tactics.nextTeamInstanceId = 2;
        RuntimeComputerTacticTeam team; team.instanceId = 1; team.triggerId = "SNAPSHOT_AI_TRIGGER";
        team.teamTypeId = "SNAPSHOT_AI_TEAM"; team.targetOwner = "SkirmishPlayer1";
        team.objectIds = {45, 46}; team.joiningObjectIds = {46}; team.missingMembers = {"SNAPSHOT_TANK"};
        team.createdTick = 1; team.lastProgressTick = source->tick; team.launched = true;
        team.missionInstanceId = source->triggers.nextTeamMissionInstanceId;
        tactics.teams[1] = team;
        tactics.production[1] = {1, "SNAPSHOT_TANK", 211, 100, 600};
        tactics.history[team.triggerId] = {40.0, 1, 0, 0};
        issueRuntimeTriggerTeamOrder(*source, source->triggers.teams.at(team.teamTypeId),
            {45}, team.triggerId, team.instanceId);
        findMutableObjectById(*source, 46)->factoryExitHostId = 47;
        source->computerAiByOwner.at("skirmishplayer3").credits -= 100;
        source->computerAiByOwner.at("skirmishplayer3").creditsSpent += 100;
        identity = runtimeMultiplayerSnapshotSmokeIdentity(*source, 51,
            "detail-nonempty-v15", RuntimeMultiplayerSnapshotKind::Load);
        if (identity.authoritativeStateHash == 0
            || !exportRuntimeMultiplayerSnapshotV15(*source, identity, image, error))
            return fail("full export: " + error);
    }
    {
        RuntimeSnapshotActiveStateScope scope(*destination);
        const auto before = runtimeMultiplayerAuthoritativeStateHash(*destination, error);
        RuntimeMultiplayerSnapshotStagedState staged;
        if (!stageRuntimeMultiplayerSnapshot(*destination, image.bytes,
                image.descriptor, staged, error)) {
            const std::string stageError = error;
            // The loader's generic difference labels compare with the live
            // destination, whose new command state is deliberately empty.
            // On failure, compare a detached materialization with the source
            // as well so expected source/destination differences cannot hide
            // the actual round-trip defect.
            auto diagnostic = std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*diagnostic, *source, source->visionEnabled);
            IniFile diagnosticIni;
            std::string diagnosticError;
            const std::string snapshotText(image.bytes.begin(), image.bytes.end());
            if (diagnosticIni.loadFromString(snapshotText)
                && materializeRuntimeLoadStateFromIni(*diagnostic, diagnosticIni, diagnosticError)
                && materializeRuntimeMultiplayerSnapshotHouses(*diagnostic, diagnosticIni, diagnosticError)
                && materializeRuntimeMultiplayerSnapshotExtension(*diagnostic, diagnosticIni, diagnosticError))
                return fail("full stage: " + stageError + "; differences-from-source="
                    + runtimeMultiplayerSnapshotAuthorityDifferenceSummary(*source, *diagnostic));
            return fail("full stage: " + stageError + "; source diagnostic=" + diagnosticError);
        }
        if (runtimeMultiplayerAuthoritativeStateHash(*destination, error) != before
            || !destination->combatDetails.bursts.empty()
            || !destination->playerCommands.queues.empty()) return fail("stage mutated live state");
        commitRuntimeMultiplayerSnapshotAuthority(*destination, std::move(staged));
        std::ostringstream tacticBefore, tacticAfter;
        writeRuntimeComputerTactics(tacticBefore, *source);
        writeRuntimeComputerTactics(tacticAfter, *destination);
        if (tacticBefore.str() != tacticAfter.str()
            || !destination->triggers.computerTacticsByOwner.contains("skirmishplayer3")
            || destination->triggers.computerTacticsByOwner.at("skirmishplayer3").teams.at(1).joiningObjectIds != std::vector<int>{46}
            || destination->computerAiByOwner.at("skirmishplayer3").credits != 6400)
            return fail("nonempty computer team, paid replacement or pending join diverged");
        if (runtimeMultiplayerAuthoritativeStateHash(*destination, error) != identity.authoritativeStateHash
            || destination->combatDetails.deterministicHash() != source->combatDetails.deterministicHash()
            || destination->playerCommands.serialize() != source->playerCommands.serialize()
            || destination->projectiles.size() != source->projectiles.size()
            || !destination->projectiles.front().homingInitialized
            || destination->projectiles.front().homingYaw != 0.625
            || destination->projectiles.front().homingPitch != -0.375
            || !destination->computerMiningByOwner.contains("skirmishplayer3")
            || destination->computerMiningByOwner.at("skirmishplayer3").storedResourceValue != 450
            || destination->computerMiningByOwner.at("skirmishplayer3").resourceTrips != 7
            || !destination->combatDetails.hasPendingBurst(41)
            || !findObjectById(*destination, 41)->harvestAutoSearchSuspended
            || !findObjectById(*destination, 41)->hasBeenCaptured
            || findObjectById(*destination, 41)->factoryExitHostId != 44
            || findObjectById(*destination, 41)->radiationEruptionCooldownTicksRemaining != 73
            || !destination->playerCommands.following.contains(43)
            || !destination->playerCommands.rallyPoints.contains(44)
            || destination->playerCommands.following.at(43).targetId != 41
            || destination->playerCommands.rallyPoints.at(44).cell != RuntimeMultiplayerGameplayCell{10, 8})
            return fail("nonempty combat/player commands diverged after commit: " + error);
        const auto selling = std::find_if(destination->world.getObjects().begin(), destination->world.getObjects().end(),
            [](const auto& object) { return object.instanceId == 49; });
        if (selling == destination->world.getObjects().end() || !runtimeStructureIsSelling(*selling)
            || selling->buildAnimTicksTotal != 96 || selling->buildAnimTicksRemaining != 47)
            return fail("sold structure reverse phase lost after multiplayer commit");
    }
    error.clear();
    return true;
}
