// Stock DRON host combat through the ordinary writer, staged loader and commit.
// Included with the other shell smoke probes; no synthetic weapon/rule changes.
bool runRuntimeParasiteAttackSaveSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) {
        error = "parasite attack save: " + why;
        std::cout << error << '\n';
        return false;
    };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->config.playerSlots.clear();
    stock->config.gameModeRulesOverride.clear();
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world,
        &stock->theater, &stock->art, &stock->rules, &stock->mapFacts);
    const auto* drone = stock->rules.findWeaponType("DroneJump");
    const auto* tankRule = stock->rules.findUnitType("Vehicle", "HTNK");
    if (!stock->report.success || !drone || !tankRule
        || drone->damage != 50 || drone->rof != 60 || tankRule->strength != 400)
        return fail("stock resources / DroneJump or HTNK oracle unavailable");

    const char* output = std::getenv("CONCORD_PARASITE_ATTACK_SAVE_OUTPUT");
    const std::filesystem::path root = output && *output ? output
        : "build/smoke/feature-set-19/04-parasite-attack-save";
    std::filesystem::create_directories(root);
    std::ofstream csv(root / "cases.csv");
    csv << "hz,case,saveTick,continuedTicks,hostHealth,nextBite,attackWitness,result\n";
    constexpr int sourceId = 101, hostId = 102, enemyId = 103, depotId = 104;
    int saves = 0, rejects = 0, continuedTicks = 0, attackCases = 0;

    const auto make = [&](int hz, bool distantTarget) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = stock->config; s.config.fixedTickHz = hz;
        s.config.playerCountry = "Russians"; s.config.noCooldown = false;
        s.config.computerAiEnabled = s.config.multiplayerActive = false;
        s.config.crates = s.config.shortGame = false;
        s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        s.report = stock->report; s.report.map.size.clear(); s.report.map.localSize.clear();
        s.report.map.width = -1;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.economy.owner = "Russians"; s.economy.credits = 10000;
        s.economy.skirmishStartApplied = true;
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        RuntimeHumanHouseAuthority authority;
        authority.owner = s.economy.owner;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
        authority.localFacade = true; s.humanHouseAuthorities.push_back(std::move(authority));
        s.structureFootprints["NADEPT"] = {3, 3, true};
        s.structureFootprints["NAPOWR"] = {2, 2, true};
        s.structureFootprints["GAPOWR"] = {2, 2, true};
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
        }
        const auto add = [&](int id, const char* type, const char* owner,
            int health, WorldObjectKind kind, int x, int y) {
            WorldObjectInstance object;
            object.instanceId = id; object.mapId = "ParasiteAttackSave" + std::to_string(id);
            object.type = type; object.owner = owner; object.kind = kind;
            object.alive = object.poweredOperational = true; object.health = health;
            object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
            object.mission = "Guard"; object.currentAmmo = -1;
            clearRuntimeRenderPosition(object);
            s.world.accessObjects().push_back(std::move(object));
        };
        add(sourceId, "DRON", "Americans", 100, WorldObjectKind::Unit, 4, 8);
        add(hostId, "HTNK", "Russians", 400, WorldObjectKind::Unit, 5, 8);
        add(enemyId, "GAPOWR", "Americans", 750, WorldObjectKind::Structure, distantTarget ? 18 : 8, 8);
        add(depotId, "NADEPT", "Russians", 1200, WorldObjectKind::Structure, 24, 24);
        add(105, "NAPOWR", "Russians", 750, WorldObjectKind::Structure, 29, 24);
        initializeRuntimeLocalProductionLanes(s.economy);
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s);
        ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeGarrisonsInitialized(s);
        ensureRuntimeGatesInitialized(s); ensureRuntimeSubcellsInitialized(s);
        ensureRuntimeRepairDepotsInitialized(s);
        return box;
    };
    const auto attach = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s);
        const auto* source = findObjectById(s, sourceId);
        const auto* host = findObjectById(s, hostId);
        if (!source || !host) return fail("parasite fixture identity");
        const auto profile = runtimeWeaponProfileForObject(s, *source, host);
        if (!profile.weapon || !launchRuntimeParasiteProjectile(s, *source, *host,
                *profile.weapon, profile) || s.projectiles.size() != 1)
            return fail("stock DRON launch");
        const auto impact = s.projectiles.front(); s.projectiles.clear();
        if (!applyRuntimeProjectileImpact(s, impact)
            || !s.specialMechanisms.parasiteLinks().contains(hostId))
            return fail("stock DRON impact");
        // Mechanical parasites bite immediately at impact tick. Complete that
        // production transaction before saving an end-of-tick relationship.
        const auto actions = s.specialMechanisms.tickParasites(s.world.getObjects(), s.rules, s.tick, {});
        if (!applyRuntimeParasiteActionsAtomically(s, actions, {}, sourceId).accepted
            || findObjectById(s, hostId)->health != 350)
            return fail("first native 50-point bite");
        return true;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false);
    };
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const auto replaceBehaviors = [](std::string& text, const std::vector<std::string>& records) {
        const auto begin = text.find("[Behaviors]");
        if (begin == std::string::npos) return false;
        const auto end = text.find("\n[", begin);
        std::ostringstream section; section << "[Behaviors]\nCount=" << records.size() << '\n';
        for (std::size_t i = 0; i < records.size(); ++i) section << i << '=' << records[i] << '\n';
        text.replace(begin, end == std::string::npos ? text.size() - begin : end - begin, section.str());
        return true;
    };
    const auto record = [](const std::vector<std::string>& fields) {
        std::ostringstream text;
        for (std::size_t i = 0; i < fields.size(); ++i) text << (i ? "|" : "") << fields[i];
        return text.str();
    };
    const auto reject = [&](RuntimeWindowState& live, const std::string& badText,
        const std::string& label, bool requireParasiteError) {
        const auto path = root / (label + "-bad.ini");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << badText; out.close();
        std::ostringstream before, after; std::string detail;
        if (!out.good() || !writeRuntimeStateToStream(live, before, detail))
            return fail(label + " prepare atomic rejection: " + detail);
        const auto persistent = runtimePersistentTransientStateHash(live);
        const auto special = live.specialMechanisms.stateHash();
        const auto selected = live.selectedObjectIds;
        const auto revision = live.resourceStateRevision;
        const bool loaded = loadRuntimeState(live, pathString(path), detail);
        const std::string rejection = detail;
        if (loaded || rejection.empty()
            || (requireParasiteError && rejection.find("invalid persisted parasite state") == std::string::npos)
            || !writeRuntimeStateToStream(live, after, detail)
            || before.str() != after.str()
            || persistent != runtimePersistentTransientStateHash(live)
            || special != live.specialMechanisms.stateHash()
            || selected != live.selectedObjectIds || revision != live.resourceStateRevision)
            return fail(label + " accepted/mutated state or wrong rejection: " + rejection);
        ++rejects;
        return true;
    };
    const auto roundTrip = [&](RuntimeWindowState& live, const std::string& label,
        std::unique_ptr<RuntimeWindowState>& restored) {
        const auto path = root / (label + ".ini");
        const auto special = live.specialMechanisms.stateHash();
        const int health = findObjectById(live, hostId)->health;
        if (!saveRuntimeState(live, pathString(path), error)) return fail(label + " write: " + error);
        restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored, live, false);
        restored->runtimePreferencePersistenceEnabled = restored->runtimeSoundsEnabled = false;
        restored->runtimeSuppressPlayerAudioFeedback = true;
        {
            RuntimeSnapshotActiveStateScope active(*restored);
            if (!loadRuntimeState(*restored, pathString(path), error)) return fail(label + " load: " + error);
        }
        const auto* host = findObjectById(*restored, hostId);
        const auto* source = findRuntimeObjectByIdIncludingInactive(*restored, sourceId);
        if (!host || !source || host->health != health || source->alive || source->health <= 0
            || source->mission != "ParasiteLimbo" || restored->tick != live.tick
            || restored->specialMechanisms.stateHash() != special
            || restored->repairDepots.stateHash() != live.repairDepots.stateHash())
            return fail(label + " relationship/health/repair authority changed");
        ++saves;
        return true;
    };

    for (int hz : {15, 30, 60}) {
        for (const std::string phase : {"queued", "firing", "chasing", "force-object"}) {
            const bool chase = phase == "chasing";
            const std::string label = std::to_string(hz) + "-" + phase;
            auto box = make(hz, chase); auto& s = *box;
            RuntimeSnapshotActiveStateScope active(s);
            if (!attach(s)) return false;
            s.selectedObjectIds = {hostId};
            issueRuntimeAttackCommand(s, *findObjectById(s, enemyId), phase == "force-object");
            if (s.lastCommand.acceptedCount != 1 || !s.behaviors.contains(hostId)
                || s.behaviors.at(hostId).kind != RuntimeBehaviorKind::Attack)
                return fail(label + " real attack order rejected");
            if (phase == "firing" || chase) {
                bool captured = false;
                for (int n = 0; n < hz * 3; ++n) {
                    step(s);
                    const auto it = s.behaviors.find(hostId);
                    if (it == s.behaviors.end()) break;
                    if ((!chase && findObjectById(s, hostId)->weaponCooldownTicksRemaining > 0)
                        || (chase && it->second.pendingAllowedOccupiedObjectId == enemyId
                            && (it->second.pathRequestPending || !it->second.path.empty()
                                || findObjectById(s, hostId)->renderMoveActive))) {
                        captured = true; break;
                    }
                }
                if (!captured) return fail(label + " no real firing/chase witness");
            }
            const int savedTick = s.tick;
            const int firstBite = s.specialMechanisms.parasiteLinks().at(hostId).nextDamageTick;
            std::unique_ptr<RuntimeWindowState> loaded;
            if (!roundTrip(s, label, loaded)) return false;
            if (!loaded->behaviors.contains(hostId)
                || loaded->behaviors.at(hostId).kind != RuntimeBehaviorKind::Attack
                || loaded->behaviors.at(hostId).targetObjectId != enemyId
                || loaded->behaviors.at(hostId).forceFire != (phase == "force-object")
                || loaded->behaviors.at(hostId).pendingAllowedOccupiedObjectId
                    != s.behaviors.at(hostId).pendingAllowedOccupiedObjectId)
                return fail(label + " attack intent/path target changed on load");

            if (phase == "queued") {
                const std::string valid = read(root / (label + ".ini"));
                IniFile ini;
                if (!ini.loadFromString(valid)) return fail(label + " saved text unavailable");
                auto fields = splitRuntimeSaveFields(ini.getValue("Behaviors", "0", ""));
                if (fields.size() < 12 || fields[0] != std::to_string(hostId))
                    return fail(label + " exact one-host behavior fixture");
                for (const auto& corruption : std::vector<std::pair<std::string, std::pair<int, std::string>>>{
                    {"limbo-target", {2, std::to_string(sourceId)}},
                    {"missing-target", {2, "999999"}}, {"self-target", {2, std::to_string(hostId)}},
                    {"foreign-path-authority", {10, std::to_string(depotId)}},
                    {"object-and-bridge", {7, "yes"}}, {"malformed-cooldown", {5, "-1"}}}) {
                    auto changed = fields; changed[corruption.second.first] = corruption.second.second;
                    std::string bad = valid;
                    if (!replaceBehaviors(bad, {record(changed)})
                        || !reject(s, bad, label + "-" + corruption.first,
                            corruption.first != "malformed-cooldown")) return false;
                }
                auto sourceAttack = fields; sourceAttack[0] = std::to_string(sourceId);
                std::string bad = valid;
                if (!replaceBehaviors(bad, {record(fields), record(sourceAttack)})
                    || !reject(s, bad, label + "-limbo-source-attack", false)) return false;
            }

            const int enemyHealth = findObjectById(s, enemyId)->health;
            bool shot = phase == "firing", restoredShot = shot;
            int paired = 0;
            for (int n = 0; n < hz * 15; ++n) {
                step(s); step(*loaded); ++paired; ++continuedTicks;
                const auto* a = findObjectById(s, hostId);
                const auto* b = findObjectById(*loaded, hostId);
                if (!a || !b || !a->alive || !b->alive
                    || !s.specialMechanisms.parasiteLinks().contains(hostId)
                    || !loaded->specialMechanisms.parasiteLinks().contains(hostId))
                    return fail(label + " continuation lost host/link");
                const auto& originalLink = s.specialMechanisms.parasiteLinks().at(hostId);
                const auto& restoredLink = loaded->specialMechanisms.parasiteLinks().at(hostId);
                if (a->health != b->health || originalLink.nextDamageTick != restoredLink.nextDamageTick
                    || originalLink.sourceObjectId != restoredLink.sourceObjectId
                    || originalLink.damage != 50 || restoredLink.damage != 50
                    || originalLink.damageIntervalTicks != hz * 4
                    || restoredLink.damageIntervalTicks != hz * 4)
                    return fail(label + " parasite damage or timing diverged after load");
                const auto* victim = findObjectById(s, enemyId);
                const auto* restoredVictim = findObjectById(*loaded, enemyId);
                if (!victim || !restoredVictim) return fail(label + " enemy disappeared");
                shot = shot || victim->health < enemyHealth || a->weaponCooldownTicksRemaining > 0;
                restoredShot = restoredShot || restoredVictim->health < enemyHealth || b->weaponCooldownTicksRemaining > 0;
                if (!chase && (victim->health != restoredVictim->health
                        || a->weaponCooldownTicksRemaining != b->weaponCooldownTicksRemaining))
                    return fail(label + " stationary combat diverged after load");
                if (s.tick >= firstBite && originalLink.nextDamageTick > firstBite && shot && restoredShot) break;
            }
            if (!shot || !restoredShot || s.specialMechanisms.parasiteLinks().at(hostId).nextDamageTick <= firstBite)
                return fail(label + " missing continued attack and subsequent parasite bite");
            csv << hz << ',' << phase << ',' << savedTick << ',' << paired << ','
                << findObjectById(s, hostId)->health << ','
                << s.specialMechanisms.parasiteLinks().at(hostId).nextDamageTick << ",1,pass\n";
            ++attackCases;
        }

        // Existing legal Move and depot authorities must still round-trip.
        auto box = make(hz, false); auto& s = *box;
        RuntimeSnapshotActiveStateScope active(s);
        if (!attach(s)) return false;
        s.selectedObjectIds = {hostId};
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, {6, 14})
            || s.lastCommand.acceptedCount != 1) return fail("ordinary move command");
        std::unique_ptr<RuntimeWindowState> loaded;
        const std::string prefix = std::to_string(hz);
        if (!roundTrip(s, prefix + "-move-control", loaded)) return false;
        csv << hz << ",move-control," << s.tick << ",0,350,"
            << s.specialMechanisms.parasiteLinks().at(hostId).nextDamageTick << ",0,pass\n";
        const auto* depot = findObjectById(s, depotId);
        if (!depot || !queueRuntimeUnitRepairCommand(s, runtimeCellPointForObject(*depot), *depot)
            || s.lastCommand.acceptedCount != 1) return fail("reserved repair command");
        if (!roundTrip(s, prefix + "-repair-reserved-control", loaded)) return false;
        csv << hz << ",repair-reserved-control," << s.tick << ",0,350,"
            << s.specialMechanisms.parasiteLinks().at(hostId).nextDamageTick << ",0,pass\n";
        std::string bad = read(root / (prefix + "-repair-reserved-control.ini"));
        const std::string attack = std::to_string(hostId) + "|Attack|" + std::to_string(enemyId)
            + "|-1|-1|0|0|no|-1|-1|-1|0";
        if (!replaceBehaviors(bad, {attack})
            || !reject(s, bad, prefix + "-attack-and-repair", false)) return false;
        const auto* relation = findRuntimeRepairDepotRelationForUnit(s.repairDepots, hostId);
        auto* host = findMutableObjectById(s, hostId);
        if (!relation || !host) return fail("reserved repair authority absent");
        host->x = relation->dockCell.x; host->y = relation->dockCell.y;
        host->onBridge = relation->dockCell.onBridge; host->cell = runtimeCellKey(host->x, host->y);
        clearRuntimeRenderPosition(*host);
        if (completeRuntimeRepairDepotArrival(s, hostId) != RuntimeRepairDepotArrivalResult::Attached)
            return fail("production dock attachment");
        eraseRuntimeBehavior(s, hostId);
        if (!roundTrip(s, prefix + "-repair-docked-control", loaded)) return false;
        csv << hz << ",repair-docked-control," << s.tick << ",0,350,"
            << s.specialMechanisms.parasiteLinks().at(hostId).nextDamageTick << ",0,pass\n";
    }
    csv.close();
    if (!csv.good()) return fail("coverage report write failed");
    std::cout << "Parasite attack save passed: " << attackCases << " stock attack cases, "
        << saves << " full saves/loads, " << rejects << " atomic bad-save rejections, "
        << continuedTicks << " paired continuation ticks; Move/reserved/docked repair controls=ok\n";
    return true;
}
