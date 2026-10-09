    struct NavalCase { const char* country; const char* trigger; const char* team; int members; };
    for (const NavalCase test : {
        NavalCase{"Americans", "0D087D8C-G", "0CA1E67C-G", 3},
        NavalCase{"Russians", "0ECCB55C-G", "0CA18BAC-G", 3},
        NavalCase{"YuriCountry", "06C551AC-G", "06C5992C-G", 2},
        NavalCase{"Russians", "0C32D84C-G", "0EC2038C-G", 4}}) {
        auto storage = makeWorld(test.country, "Hard", {test.trigger}); auto& s = *storage;
        RuntimeSnapshotActiveStateScope scope(s);
        // Two land banks separated by a navigable sea. The stock factories
        // and all unit rules, prices and build times remain in use.
        for (auto& [cell, terrain] : s.terrainInfoByCell) {
            const int x = cell % 1000;
            terrain.water = x >= 16 && x <= 31;
            terrain.kind = terrain.water ? RuntimeTerrainKind::Water : RuntimeTerrainKind::Land;
        }
        s.pathTerrainCache.valid = s.pathTerrainCache.dynamicValid = false;
        const std::string yard = std::string(test.country) == "YuriCountry" ? "YAYARD"
            : std::string(test.country) == "Russians" ? "NAYARD" : "GAYARD";
        auto* factory = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, yard, {17, 11});
        if (!factory) return fail("naval factory unavailable");
        const int factoryId = factory->instanceId;
        auto* target = addRuntimeObjectInstanceForOwner(s, human, RuntimeBuildKind::Building, "GAYARD", {29, 32});
        if (!target) return fail("enemy naval factory unavailable");
        const int targetId = target->instanceId;
        const int initialHealth = target->health;
        std::map<int, int> enemyFactories;
        for (const auto& object : s.world.getObjects()) if (object.alive && object.owner == human) {
            const auto* building = findRuntimeBuildingTypeById(s.rules, object.type);
            if (building && !building->factory.empty()) enemyFactories[object.instanceId] = object.health;
        }
        refreshRuntimeObjectOccupancyAfterLocalChange(s); updateRuntimeVisionState(s);
        const auto& type = s.triggers.teams.at(test.team);
        int expectedCost = 0;
        for (const auto& member : s.triggers.taskForces.at(type.taskForceId).members) {
            const auto item = runtimeComputerAiBuildItem(s, member.unitId, owner);
            if (!item || !runtimeComputerAiItemAllowed(s, s.computerAiByOwner.at(ownerKey), *item))
                return fail(std::string(test.country) + " original naval prerequisite unavailable: " + member.unitId);
            expectedCost += member.count * item->cost;
        }
        bool launched = false, fired = false, damage = false, leftFactory = false;
        std::set<int> produced;
        int firstPaidTick = -1;
        int lastStep = -2;
        for (int frame = 0; frame < 22000; ++frame) {
            tick(s);
            auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
            if (!tactics.teams.empty()) tactics.nextTriggerTick = 10000000;
            if (!tactics.production[1].typeId.empty() && firstPaidTick < 0) firstPaidTick = s.tick;
            for (const auto& [id, team] : tactics.teams) if (team.teamTypeId == test.team) {
                launched |= team.launched;
                const auto mission = s.triggers.teamMissions.find(team.missionInstanceId);
                if (mission != s.triggers.teamMissions.end() && static_cast<int>(mission->second.stepIndex) != lastStep) {
                    lastStep = static_cast<int>(mission->second.stepIndex);
                    std::cout << "Naval script " << test.team << " tick=" << s.tick << " step=" << lastStep
                        << " target=" << mission->second.targetCell.x << ',' << mission->second.targetCell.y;
                    for (int objectId : team.objectIds) if (const auto* member = findObjectById(s, objectId))
                        std::cout << ' ' << member->type << '@' << member->x << ',' << member->y << ':' << member->mission;
                    std::cout << '\n';
                }
                for (int objectId : team.objectIds) {
                    const auto* member = findObjectById(s, objectId);
                    if (!member) continue;
                    produced.insert(objectId);
                    if (!team.launched && member->factoryExitHostId >= 0 && member->factoryExitHostId != factoryId)
                        return fail("naval member emerged from a land factory");
                    leftFactory |= member->y > 20;
                    const auto behavior = s.behaviors.find(objectId);
                    fired |= behavior != s.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Attack;
                }
            }
            for (const auto& [victimId, health] : enemyFactories) {
                const auto* victim = findRuntimeObjectByIdIncludingInactive(s, victimId);
                damage |= victim && (!victim->alive || victim->health < health);
            }
            if (launched && produced.size() == static_cast<std::size_t>(test.members) && damage && leftFactory) break;
        }
        const auto& ai = s.computerAiByOwner.at(ownerKey);
        if (!launched || !damage || !leftFactory || firstPaidTick < 0
            || produced.size() != static_cast<std::size_t>(test.members)
            || ai.creditsSpent < expectedCost || s.triggers.unitsCreated != 0) {
            std::ostringstream details; details << std::string(test.country) << " naval " << test.team
                << " launched=" << launched << " count=" << produced.size() << " moved=" << leftFactory
                << " damage=" << damage << " attack=" << fired << " spent=" << ai.creditsSpent << describe(s);
            for (const auto& [id, mission] : s.triggers.teamMissions)
                details << " mission=" << mission.scriptId << ':' << mission.stepIndex
                    << " target=" << mission.targetCell.x << ',' << mission.targetCell.y;
            std::ostringstream saved; std::string saveError;
            if (writeRuntimeStateToStream(s, saved, saveError)) {
                std::ofstream out(std::filesystem::path("build/smoke/gameplay/05-probe") / (std::string("naval-") + test.team + ".ini"));
                out << saved.str();
            }
            return fail(details.str());
        }
        std::cout << "Computer naval tactics: " << test.country << ' ' << test.team
            << " paid stock members=yes real shipyard production=yes sea movement=yes enemy factory attacked=yes\n";
    }
