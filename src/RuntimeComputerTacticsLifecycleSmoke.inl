// Real production, casualties and continuity, using the same original bases.
{
    auto probe = makeWorld("Americans", "Hard", {"0CA6621C-G"}); auto& s = *probe;
    int teamId = -1;
    for (int i = 0; i < 9000; ++i) {
        tick(s);
        auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
        if (!tactics.teams.empty()) tactics.nextTriggerTick = 10000000;
        for (const auto& [id, team] : tactics.teams)
            if (team.launched && team.missionFinished) teamId = id;
        if (teamId >= 0) break;
    }
    auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
    if (teamId < 0 || tactics.teams.at(teamId).objectIds.size() != 6)
        return fail("stock six-infantry guard never occupied its post" + describe(s));
    const auto originalIds = tactics.teams.at(teamId).objectIds;
    const int casualty = originalIds.front();
    const auto* victim = findObjectById(s, casualty);
    if (!victim) return fail("guard casualty missing");
    const auto replacement = runtimeComputerAiBuildItem(s, victim->type, owner);
    const int oldSpent = s.computerAiByOwner.at(ownerKey).creditsSpent;
    RuntimeObjectExitRequest death; death.objectId = casualty; death.tick = s.tick;
    if (!finalizeRuntimeObjectExit(s, death).accepted) return fail("guard casualty not committed");
    for (int i = 0; i < 120 && tactics.production[0].typeId.empty(); ++i) tick(s);
    if (!replacement || tactics.production[0].typeId != replacement->typeId
        || tactics.teams.at(teamId).objectIds.size() != 5
        || s.computerAiByOwner.at(ownerKey).creditsSpent != oldSpent + replacement->cost)
        return fail("Reinforce=yes guard did not pay for its missing member" + describe(s));
    int reinforcement = -1;
    for (int i = 0; i < replacement->buildTicks + 240 && reinforcement < 0; ++i) {
        tick(s);
        for (int memberId : tactics.teams.at(teamId).objectIds)
            if (std::find(originalIds.begin(), originalIds.end(), memberId) == originalIds.end()) reinforcement = memberId;
    }
    if (reinforcement < 0 || reinforcement == casualty || findObjectById(s, casualty))
        return fail("guard reinforcement absent or reused its dead member" + describe(s));
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(s, saved, error)) return false;
    IniFile savedIni; savedIni.loadFromString(saved.str());
    auto restored = std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*restored, s, s.visionEnabled);
    if (!materializeRuntimeLoadStateFromIni(*restored, savedIni, error)) return false;
    if (restored->triggers.computerTacticsByOwner.at(ownerKey).teams.at(teamId).joiningObjectIds
        != tactics.teams.at(teamId).joiningObjectIds) return fail("new guard lost its pending join in save");
    for (auto* world : {&s, restored.get()}) {
        auto& current = world->triggers.computerTacticsByOwner.at(ownerKey);
        bool arrived = false;
        for (int i = 0; i < 1600 && !arrived; ++i) {
            tick(*world);
            const auto* member = findObjectById(*world, reinforcement);
            RuntimeTeamMissionState companions;
            for (int id : current.teams.at(teamId).objectIds) if (id != reinforcement) companions.objectIds.push_back(id);
            arrived = member && current.teams.at(teamId).joiningObjectIds.empty()
                && !world->behaviors.contains(reinforcement)
                && runtimeCellDistance(runtimeCellPointForObject(*member), runtimeTeamMissionCenter(*world, companions)) < 4.0;
        }
        if (!arrived || current.teams.at(teamId).objectIds.size() != 6
            || world->computerAiByOwner.at(ownerKey).creditsSpent != oldSpent + replacement->cost)
            return fail("replacement did not physically rejoin the same guard after save" + describe(*world));
        const auto survivors = current.teams.at(teamId).objectIds;
        for (int id : survivors) { RuntimeObjectExitRequest loss; loss.objectId = id; loss.tick = world->tick;
            if (!finalizeRuntimeObjectExit(*world, loss).accepted) return fail("guard destruction failed"); }
        for (int i = 0; i < 120; ++i) tick(*world);
        if (current.teams.contains(teamId) || !current.production[0].typeId.empty()
            || current.history.at("0CA6621C-G").failures != 1)
            return fail("annihilated guard retained an immortal refill reservation");
    }
    std::cout << "Computer reinforcement: stock guard=yes casualty replaced at actual cost=yes factory exit=yes physical rejoin=yes saved reinforcement resumes=yes annihilated team releases cap=yes\n";
}
{
    auto probe = makeWorld("Americans", "Easy", {"0D084B5C-G"}); auto& s = *probe;
    tick(s);
    auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
    tactics.nextTriggerTick = 10000000;
    int factoryId = -1;
    for (const auto& object : s.world.getObjects())
        if (object.owner == owner && object.type == "GAAIRC") factoryId = object.instanceId;
    for (int i = 0; i < 180; ++i) tick(s);
    const auto paidOrder = tactics.production[2];
    const int spent = s.computerAiByOwner.at(ownerKey).creditsSpent;
    const auto transition = applyRuntimeOwnerTransitionBatch(s, {{factoryId, owner, human}},
        RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits, "tactic factory loss smoke");
    if (!transition.applied) return fail("airport capture not committed: " + transition.error);
    for (int i = 0; i < 600; ++i) tick(s);
    if (tactics.production[2].remainingTicks != paidOrder.remainingTicks
        || s.computerAiByOwner.at(ownerKey).unitsBuilt != 0
        || s.computerAiByOwner.at(ownerKey).creditsSpent != spent)
        return fail("lost airport continued production, charged again or spawned into enemy factory");
    const auto* newFactory = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, "GAAIRC", {21, 3});
    if (!newFactory) return fail("replacement airport unavailable");
    const int replacementFactory = newFactory->instanceId;
    initializeRuntimeNavigation(s);
    for (int i = 0; i < paidOrder.remainingTicks + 600 && s.computerAiByOwner.at(ownerKey).unitsBuilt == 0; ++i) tick(s);
    int produced = 0;
    for (const auto& object : s.world.getObjects()) if (object.alive && object.owner == owner && object.type == "ORCA") {
        ++produced;
        if (object.dockedToObjectId != replacementFactory) return fail("resumed plane used captured airport");
    }
    if (produced != 1) return fail("paid plane failed to resume at replacement airport" + describe(s));
    std::cout << "Computer factory loss: captured airport pauses paid progress=yes no free or enemy production=yes actual replacement airport resumes=yes\n";
}
{
    auto probe = makeWorld("Americans", "Hard", {"0CA6621C-G"}); auto& s = *probe;
    tick(s);
    auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
    tactics.nextTriggerTick = 10000000;
    auto& original = tactics.teams.begin()->second;
    auto& originalType = s.triggers.teams.at(original.teamTypeId);
    auto candidate = originalType; candidate.id = "recruitment-priority-variant"; candidate.priority += 1;
    auto& ai = s.computerAiByOwner.at(ownerKey);
    auto* soldier = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Infantry, "E1", {15, 18});
    if (!soldier) return fail("recruitment member unavailable");
    const int soldierId = soldier->instanceId; original.objectIds.push_back(soldierId);
    RuntimeComputerTacticTeam wanted; wanted.instanceId = tactics.nextTeamInstanceId++;
    wanted.teamTypeId = candidate.id; wanted.missingMembers = {"E1"}; wanted.createdTick = wanted.lastProgressTick = s.tick;
    if (runtimeComputerTacticMayRecruit(s, ai, wanted, candidate, *soldier)) return fail("protected guard member was recruitable");
    originalType.recruitable = true;
    candidate.priority = originalType.priority;
    if (runtimeComputerTacticMayRecruit(s, ai, wanted, candidate, *soldier)) return fail("equal priority stole a guard member");
    candidate.priority = originalType.priority + 1; candidate.group = 4;
    if (runtimeComputerTacticMayRecruit(s, ai, wanted, candidate, *soldier)) return fail("different authored group bypassed Recruiter=no");
    candidate.recruiter = true;
    s.triggers.teams[candidate.id] = candidate;
    auto& receiving = tactics.teams.emplace(wanted.instanceId, wanted).first->second;
    if (!recruitRuntimeComputerTacticMembers(s, ai, tactics, receiving, candidate)
        || receiving.objectIds != std::vector<int>{soldierId}
        || std::find(original.objectIds.begin(), original.objectIds.end(), soldierId) != original.objectIds.end())
        return fail("higher-priority Recruiter failed actual transfer of a permitted member");
    std::cout << "Computer recruitment: protected members retained=yes strict priority=yes authored group restriction=yes Recruiter transfers one live member=yes\n";
}
