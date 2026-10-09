// Included inside the paid-team fixture, sharing its stock bases and tick loop.
{
    auto probe = makeWorld("Americans", "Hard", {"0CAD0B2C-G"}); auto& s = *probe;
    tick(s);
    auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
    if (tactics.teams.size() != 2 || tactics.production[1].typeId != "SREF"
        || tactics.production[2].typeId != "ORCA") return fail("advanced base guard did not begin both original teams" + describe(s));
    int primary = 0, secondary = 0;
    for (const auto& [id, team] : tactics.teams) (team.secondary ? secondary : primary)++;
    if (primary != 1 || secondary != 1) return fail("primary/secondary identity lost");
    auto& ai = s.computerAiByOwner.at(ownerKey);
    tactics.nextTriggerTick = 0;
    if (selectRuntimeComputerTactic(s, ai, tactics) || tactics.teams.size() != 2)
        return fail("existing Max=1 templates did not block another selection");
    tactics.nextTriggerTick = 10000000;
    std::set<int> launchedTeams;
    for (int i = 0; i < 10000 && launchedTeams.size() < 2; ++i) {
        tick(s);
        for (const auto& [id, team] : tactics.teams) if (team.launched) launchedTeams.insert(id);
    }
    if (launchedTeams.size() != 2 || ai.unitsBuilt != 6) return fail("prism/IFV guard and Harrier support did not both assemble" + describe(s));
    std::cout << "Computer cooperation: stock advanced guard and air support=yes shared factory queues=yes six paid units=yes template Max enforced=yes\n";
}
{
    auto probe = makeWorld("Russians", "Hard", {"0ECCFA7C-G"}); auto& s = *probe;
    addRuntimeObjectInstanceForOwner(s, human, RuntimeBuildKind::Building, "NACNST", {32, 12});
    tick(s);
    const auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
    if (tactics.teams.size() != 1 || tactics.teams.begin()->second.secondary
        || tactics.teams.begin()->second.teamTypeId != "06BC2DEC-G")
        return fail("same-template secondary Max=1 incorrectly vetoed the primary" + describe(s));
    std::cout << "Computer cooperation: stock same-template pair creates primary and respects secondary Max=yes\n";
}
{
    auto probe = makeWorld("Alliance", "Hard", {"0C8C645C-G"}); auto& s = *probe;
    bool launched = false;
    for (int i = 0; i < 11000 && !launched; ++i) {
        tick(s);
        auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
        if (!tactics.teams.empty()) tactics.nextTriggerTick = 10000000;
        for (const auto& [id, team] : tactics.teams) launched |= team.launched;
    }
    const auto& ai = s.computerAiByOwner.at(ownerKey);
    const auto item = runtimeComputerAiBuildItem(s, "BEAG", owner);
    if (!launched || !item || ai.unitsBuilt != 4 || ai.creditsSpent != 4 * item->cost)
        return fail("four paid Korean Black Eagles did not launch" + describe(s));
    std::set<std::pair<int,int>> pads;
    std::map<int, int> ammunition, shots;
    for (const auto& aircraft : s.world.getObjects()) if (aircraft.alive && aircraft.owner == owner && aircraft.type == "BEAG") {
        if (!pads.emplace(aircraft.dockedToObjectId, aircraft.dockIndex).second || aircraft.dockIndex < 0)
            return fail("Korean aircraft share or lost a pad");
        ammunition[aircraft.instanceId] = aircraft.currentAmmo;
    }
    bool repeated = false;
    for (int i = 0; i < 4800 && !repeated; ++i) {
        tick(s); repeated = ammunition.size() == 4;
        for (auto& [id, ammo] : ammunition) {
            const auto* aircraft = findObjectById(s, id);
            if (!aircraft) { repeated = false; continue; }
            if (aircraft->currentAmmo < ammo) ++shots[id];
            ammo = aircraft->currentAmmo; repeated &= shots[id] >= 2;
        }
    }
    if (!repeated) return fail("Korean four-plane team did not return and fly another sortie" + describe(s));
    if (runtimeComputerTacticAircraftSpawn(s, owner, *item)) return fail("fifth aircraft bypassed four occupied support pads");
    std::cout << "Computer Korea: four paid Black Eagles=yes four separate support pads=yes repeated sorties=yes no fifth unsupported plane=yes\n";
}
for (bool chrono : {false, true}) {
    const std::string triggerId = chrono ? "0C32599C-G" : "0D2769BC-G";
    const std::string weaponId = chrono ? "ChronoSphereSpecial" : "IronCurtainSpecial";
    auto probe = makeWorld(chrono ? "Americans" : "Russians", "Easy", {triggerId}); auto& s = *probe;
    s.config.superweapons = true;
    addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, "NANRCT", {15, 21});
    const auto* source = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building,
        chrono ? "GACSPH" : "NAIRON", {21, 22});
    if (!source) return fail("superweapon source unavailable");
    const int sourceId = source->instanceId;
    initializeRuntimeNavigation(s);
    bool selected = false, launchedTeam = false, fired = false, effect = false;
    bool chargedAtLaunch = false, sawAssemblyReservation = false;
    std::vector<int> teamIds;
    std::map<int, RuntimeCellPoint> startingCells;
    int launchTick = -1;
    int previousStep = -1;
    for (int i = 0; i < 24000 && !effect; ++i) {
        tick(s);
        auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
        if (!tactics.teams.empty()) {
            selected = true; tactics.nextTriggerTick = 10000000;
            sawAssemblyReservation |= runtimeComputerTacticReservesSuperWeapon(s, owner, s.rules.findSuperWeaponType(weaponId));
            for (const auto& [id, team] : tactics.teams) if (team.launched) {
                launchedTeam = true; teamIds = team.objectIds;
                const auto mission = s.triggers.teamMissions.find(team.missionInstanceId);
                if (mission != s.triggers.teamMissions.end() && previousStep != static_cast<int>(mission->second.stepIndex)) {
                    previousStep = static_cast<int>(mission->second.stepIndex);
                    std::cout << "Superweapon team diagnostic " << weaponId << " tick=" << s.tick << " step=" << previousStep;
                    for (int memberId : team.objectIds) if (const auto* member = findObjectById(s, memberId))
                        std::cout << " member=" << memberId << '@' << member->x << ',' << member->y << ':' << member->mission;
                    std::cout << '\n';
                }
            }
        }
        const auto* snapshot = s.superWeapons.findSidebarSnapshot(owner, weaponId);
        if (snapshot && snapshot->launchTick >= 0 && !fired) {
            fired = true; launchTick = snapshot->launchTick;
            const auto* rule = s.rules.findSuperWeaponType(weaponId);
            chargedAtLaunch = rule && launchTick >= static_cast<int>(rule->rechargeTimeMinutes * 60 * s.config.fixedTickHz);
            std::cout << "Superweapon launch diagnostic " << weaponId << " tick=" << s.tick << " scheduled=" << launchTick
                << " first=" << snapshot->firstTargetCell.x << ',' << snapshot->firstTargetCell.y
                << " radius=" << (rule ? rule->radius : -1) << " range=" << (rule ? rule->range : -1.0) << '\n';
        }
        if (!launchedTeam) continue;
        for (int id : teamIds) {
            const auto* member = findObjectById(s, id);
            if (!member) continue;
            startingCells.try_emplace(id, runtimeCellPointForObject(*member));
            if (chrono) effect |= fired && runtimeCellDistance(startingCells[id], runtimeCellPointForObject(*member)) > 15.0;
            else effect |= s.superWeapons.isObjectProtected(id, s.tick);
        }
    }
    if (!selected || !launchedTeam || !fired || !chargedAtLaunch || !effect || !sawAssemblyReservation)
        return fail(std::string(chrono ? "Chronosphere" : "Iron Curtain") + " paid stock team failed charged cooperation"
            + describe(s) + " selected=" + std::to_string(selected) + " launch=" + std::to_string(launchTick)
            + " effect=" + std::to_string(effect));
    const auto* aliveSource = findObjectById(s, sourceId);
    if (!aliveSource || aliveSource->owner != owner) return fail("superweapon cast without its actual owned source");
    std::cout << "Computer superweapon: " << (chrono ? "Chronosphere" : "Iron Curtain")
        << " stock team selected=yes paid real production=yes source-owned=yes full original recharge=yes reserved from generic AI=yes team effect=yes\n";
}
