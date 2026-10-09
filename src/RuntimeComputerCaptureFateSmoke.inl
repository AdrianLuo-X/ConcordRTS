{
    const auto& rules = stock->rules.getComputerTacticsRules();
    if (rules.captureNormal != std::array<int, 4>{75, 5, 5, 15}
        || rules.captureLowMoneyMark != 2000 || rules.captureWoundedMark != 0.25
        || runtimeComputerCaptureChoice(rules, 1000, true, 0.1, 20, 0) != 2
        || runtimeComputerCaptureChoice(rules, 3000, true, 0.1, 30, 0) != 3
        || runtimeComputerCaptureChoice(rules, 3000, false, 0.1, 30, 0) != 2
        || runtimeComputerCaptureChoice(rules, 3000, false, 1.0, 30, 0) != 1)
        return fail("original capture probabilities or low-money/power/wounded priority changed");
}
for (int choice : {1, 2, 3, 4, 5}) {
    auto storage = makeWorld("YuriCountry", "Easy", {}); auto& s = *storage;
    RuntimeSnapshotActiveStateScope scope(s);
    auto* grinder = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, "YAGRND", {25, 28});
    if (!grinder) return fail("capture-fate grinder unavailable");
    const int grinderId = grinder->instanceId;
    auto* reactor = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, "YAPOWR", {18, 28});
    if (!reactor) return fail("capture-fate reactor unavailable");
    const int reactorId = reactor->instanceId;
    for (int frame = 0; frame < 180; ++frame) {
        const auto* readyGrinder = findObjectById(s, grinderId);
        const auto* readyReactor = findObjectById(s, reactorId);
        if (readyGrinder && readyReactor && readyGrinder->buildAnimTicksRemaining == 0
            && readyReactor->buildAnimTicksRemaining == 0 && readyGrinder->poweredOperational) break;
        tick(s);
    }
    const auto* master = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Unit, "MIND", {23, 20});
    if (!master) return fail("capture-fate mastermind unavailable");
    const int masterId = master->instanceId;
    auto* victim = addRuntimeObjectInstanceForOwner(s, human, RuntimeBuildKind::Infantry, "E2", {25, 20});
    if (!victim) return fail("capture-fate victim unavailable");
    const int victimId = victim->instanceId;
    victim->mission = "Sleep"; victim->weaponCooldownTicksRemaining = 100000;
    RuntimeTeamType type; type.id = "CaptureFateProbe"; type.name = type.id;
    type.house = owner; type.scriptId = "CaptureFateHold"; type.mindControlDecision = choice;
    s.triggers.teams[type.id] = type;
    RuntimeScriptType script; script.id = type.scriptId; script.steps.push_back({5, 100000});
    s.triggers.scripts[script.id] = script;
    auto& tactics = s.triggers.computerTacticsByOwner[ownerKey]; tactics.nextTriggerTick = 10000000;
    RuntimeComputerTacticTeam team; team.instanceId = 1; team.teamTypeId = type.id;
    team.triggerId = "capture-probe"; team.launched = true; team.objectIds = {masterId};
    team.missionInstanceId = s.triggers.nextTeamMissionInstanceId; tactics.nextTeamInstanceId = 2;
    tactics.teams[1] = team;
    issueRuntimeTriggerTeamOrder(s, type, {masterId}, team.triggerId, 1);
    initializeRuntimeNavigation(s); updateRuntimeVisionState(s);
    if (!issueRuntimeTeamAttack(s, {masterId}, *findObjectById(s, victimId)))
        return fail("mind-control shot was not ordered");
    for (int i = 0; i < 1200 && !s.triggers.computerTacticsByOwner.at(ownerKey).captureDecisions.contains(victimId); ++i) tick(s);
    const auto& decisions = s.triggers.computerTacticsByOwner.at(ownerKey).captureDecisions;
    if (!decisions.contains(victimId) || decisions.at(victimId).decision != choice)
        return fail("actual mind-control shot did not use its team's fate " + std::to_string(choice));
    if (choice == 1) {
        const auto& joined = s.triggers.computerTacticsByOwner.at(ownerKey).teams.at(1);
        if (std::find(joined.objectIds.begin(), joined.objectIds.end(), victimId) == joined.objectIds.end())
            return fail("captured soldier did not join controller's team");
    } else if (choice == 2 || choice == 3) {
        std::cout << "Capture entry begins: choice=" << choice << " victim=" << victimId
            << " grinding=" << s.grinding.relations().size() << " transport=" << s.pendingTransportTargets.contains(victimId) << '\n';
        std::ostringstream saved; if (!writeRuntimeStateToStream(s, saved, error)) return false;
        IniFile ini; ini.loadFromString(saved.str());
        auto resumed = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*resumed, s, s.visionEnabled);
        if (!materializeRuntimeLoadStateFromIni(*resumed, ini, error)) return false;
        for (auto* world : {&s, resumed.get()}) {
            RuntimeSnapshotActiveStateScope resumeScope(*world);
            const int credits = world->computerAiByOwner.at(ownerKey).credits;
            bool entered = false;
            for (int frame = 0; frame < 2400 && !entered; ++frame) {
                tick(*world);
                const auto* captured = findRuntimeObjectByIdIncludingInactive(*world, victimId);
                const auto* host = findObjectById(*world, reactorId);
                entered = choice == 2 ? captured && !captured->alive && captured->mission == "InsideGrinder"
                    : host && std::find(host->transportPassengerIds.begin(), host->transportPassengerIds.end(), victimId)
                        != host->transportPassengerIds.end();
            }
            if (!entered || (choice == 2 && world->computerAiByOwner.at(ownerKey).credits <= credits)) {
                std::ostringstream snapshot; std::string detail;
                if (writeRuntimeStateToStream(*world, snapshot, detail)) {
                    std::ofstream out("build/smoke/gameplay/capture-fate-failed.ini"); out << snapshot.str();
                }
                const auto* captured = findRuntimeObjectByIdIncludingInactive(*world, victimId);
                return fail("captured soldier did not physically enter/refund fate " + std::to_string(choice) + describe(*world)
                    + " entered=" + std::to_string(entered) + " credits=" + std::to_string(credits) + ":"
                    + std::to_string(world->computerAiByOwner.at(ownerKey).credits) + " grind=" + std::to_string(world->grinding.relations().size())
                    + " matchFinished=" + std::to_string(world->skirmishMatchFinished) + " save=" + detail
                    + (captured ? " victim=" + captured->mission + ":" + captured->owner + ":" + std::to_string(captured->health)
                        + "@" + std::to_string(captured->x) + "," + std::to_string(captured->y) : " victim absent"));
            }
        }
    } else if (choice == 4 && !s.behaviors.contains(victimId)) {
        return fail("captured soldier received no real hunt order");
    } else if (choice == 5) {
        // The normal house AI can recruit a free captured soldier later in
        // the same tick. DecideUnitFate(5) itself must leave an order intact.
        if (!issueRuntimeTeamMove(s, {victimId}, {29, 19}) || !s.behaviors.contains(victimId))
            return fail("capture no-order continuation unavailable");
        const auto before = s.behaviors.at(victimId);
        const std::string missionBefore = findObjectById(s, victimId)->mission;
        s.triggers.computerTacticsByOwner.at(ownerKey).captureDecisions.erase(victimId);
        s.computerAiByOwner.at(ownerKey).nextThinkTick = s.tick;
        (void)advanceRuntimeComputerCaptureFates(s, ownerKey);
        const auto after = s.behaviors.find(victimId);
        if (after == s.behaviors.end() || after->second.kind != before.kind
            || after->second.moveTarget.x != before.moveTarget.x || after->second.moveTarget.y != before.moveTarget.y
            || findObjectById(s, victimId)->mission != missionBefore)
            return fail("do-nothing capture override replaced the current order");
    }
    std::cout << "Computer captured-unit fate: " << choice
        << " real mind-control shot=yes authored decision=yes physical outcome=yes"
        << ((choice == 2 || choice == 3) ? " travel save/resume=yes" : "") << '\n';
}
