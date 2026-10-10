bool runRuntimeSquidPunchSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "squid punch: " + why; return false; };
    const char* output = std::getenv("CONCORD_SQUID_PUNCH_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    std::optional<TerrainCell> water;
    for (const auto& cell : live.world.getTerrainCells())
        if (runtimeCachedTerrainInfoForCell(live, cell).water) { water = cell; break; }
    if (!water) return fail("original water tile missing");
    int cases = 0, frames = 0, saves = 0, continuationFrames = 0, discharges = 0;
    for (const int hz : {15, 30, 60}) for (const std::string targetType : {"DLPH", "SQD"})
        for (const int rank : {0, 1, 2}) for (const bool noCooldown : {false, true}) {
        auto owned = std::make_unique<RuntimeWindowState>(); auto& s = *owned;
        seedRuntimeLoadScratch(s, live, false);
        s.config.fixedTickHz = hz; s.config.noCooldown = noCooldown;
        IniFile stock;
        if (!stock.loadFromFile(live.report.rulesPath) || !s.rules.loadFromIni(stock))
            return fail("original rules missing");
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFactsLoaded = false; s.terrainInfoByCell.clear(); s.terrainByCell.clear();
        s.economy.owner = "Russians"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 48; y <= 80; ++y) for (int x = 48; x <= 80; ++x) {
            auto cell = *water; cell.x = x; cell.y = y; cell.level = 0;
            cell.cellIndex512 = y * 512 + x; s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance squid;
        squid.instanceId = 1; squid.kind = WorldObjectKind::Unit; squid.type = "SQD";
        squid.owner = "Russians"; squid.health = 200; squid.alive = true; squid.submerged = true;
        squid.x = 63; squid.y = 64; squid.cell = runtimeCellKey(63, 64);
        squid.mission = "Guard"; squid.veterancy = rank;
        auto target = squid; target.instanceId = 2; target.type = targetType;
        target.owner = "Americans"; target.veterancy = 0; target.x = 64; target.cell = runtimeCellKey(64, 64);
        target.health = rank == 2 ? 1 : s.rules.findUnitType("UnitType", targetType)->strength;
        s.world.accessObjects() = {squid, target}; initializeRuntimeNavigation(s);
        RuntimeSnapshotActiveStateScope active(s);
        ensureRuntimeSpecialMechanismsInitialized(s);
        s.selectedObjectIds = {1}; issueRuntimeAttackCommand(s, *findObjectById(s, 2));
        RuntimeMapRenderContext context;
        context.rules = &s.rules; context.art = &s.art; context.fixedTickHz = hz;
        int lastShot = -1, firstShot = -1, shotCount = 0;
        std::unordered_set<int> phases;
        const std::string label = targetType + " hz=" + std::to_string(hz) + " rank="
            + std::to_string(rank) + " noCooldown=" + std::to_string(noCooldown);
        for (int step = 0; step < hz * 20; ++step) {
            ++s.tick; advanceRuntimeWeaponCooldowns(s);
            const auto behavior = s.behaviors.find(1);
            if (behavior != s.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Attack)
                (void)advanceRuntimeAttackBehavior(s, *findMutableObjectById(s, 1), behavior->second);
            if (!s.projectiles.empty()) {
                const auto shots = std::move(s.projectiles); s.projectiles.clear();
                for (const auto& shot : shots) {
                    if (shot.weaponId != (rank == 2 ? "SquidPunchE" : "SquidPunch"))
                        return fail(label + " wrong organic weapon " + shot.weaponId);
                    if (!applyRuntimeProjectileImpact(s, shot)) return fail(label + " impact failed");
                    ++shotCount; ++discharges;
                }
                lastShot = s.tick; if (firstShot < 0) firstShot = s.tick;
                // A stopped shot and a lethal shot must still finish their authored action.
                if (rank == 0) issueRuntimeStopCommand(s);
            }
            if (lastShot < 0) continue;
            const auto* actual = findObjectById(s, 1);
            if (!actual || !s.specialMechanisms.parasiteLinks().empty()) return fail(label + " entered grapple");
            const int age = (s.tick - lastShot) * 15 / hz;
            context.animationTick = s.tick;
            const int rendered = runtimeObjectShpFrameForGoldenCapture(*actual, context, 296);
            const int direction = ((actual->facing * 8 + 128) / 256) % 8;
            // Retail 0x74141A starts counter=31; 0x7363C9 decrements it;
            // 0x73C64B draws StartFiringFrame + direction*16 + counter/2.
            if (age < 32) {
                const int phase = (31 - age) / 2;
                const int expected = 168 + direction * 16 + phase;
                if (rendered != expected) return fail(label + " age=" + std::to_string(age)
                    + " expected=" + std::to_string(expected) + " actual=" + std::to_string(rendered));
                phases.insert(phase); ++frames;
            } else if (rendered >= 168) return fail(label + " firing did not finish");
            if (s.tick - firstShot == 10 * hz / 15) {
                std::ostringstream saved; IniFile ini;
                if (!writeRuntimeStateToStream(s, saved, error)
                    || !writeRuntimeMultiplayerSnapshotExtension(s, saved, error)
                    || !ini.loadFromString(saved.str())) return fail(label + " save: " + error);
                auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
                if (!materializeRuntimeLoadStateFromIni(*restored, ini, error)
                    || !materializeRuntimeMultiplayerSnapshotExtension(*restored, ini, error))
                    return fail(label + " reload: " + error);
                for (int delta = 0; delta <= 32 * hz / 15; ++delta) {
                    context.animationTick = s.tick + delta;
                    if (runtimeObjectShpFrameForGoldenCapture(*actual, context, 296)
                        != runtimeObjectShpFrameForGoldenCapture(*findObjectById(*restored, 1), context, 296))
                        return fail(label + " saved action changed at delta=" + std::to_string(delta));
                    ++continuationFrames;
                }
                ++saves; context.animationTick = s.tick;
                if (hz == 15 && rank == 0 && !noCooldown && targetType == "DLPH") {
                    const int before = findObjectById(*restored,1)->spriteFiringStartTick;
                    for (const std::string& record : std::vector<std::string>{"Schema=2\nCount=0\n", "Schema=1\nCount=-1\n",
                            "Schema=1\nCount=1\n0=1|-1\n", "Schema=1\nCount=1\n0=999|0\n",
                            "Schema=1\nCount=2\n0=1|0\n1=1|0\n",
                            "Schema=1\nCount=1\n0=1|" + std::to_string(s.tick+1) + "\n"}) {
                        IniFile invalid; std::string rejected;
                        if (!invalid.loadFromString("[SpriteFiringPresentation]\n" + record)
                            || loadRuntimeSpriteFirings(*restored,invalid,rejected)
                            || findObjectById(*restored,1)->spriteFiringStartTick != before)
                            return fail("malformed presentation was accepted or changed existing pose");
                    }
                    // Neither a presentation restore nor an old save without it changes gameplay authority.
                    const auto authority = restored->world.deterministicSimulationStateHash();
                    findMutableObjectById(*restored,1)->spriteFiringStartTick = -1;
                    IniFile legacy; legacy.loadFromString("[RuntimeSave]\nVersion=14\n");
                    if (!loadRuntimeSpriteFirings(*restored,legacy,error)
                        || findObjectById(*restored,1)->spriteFiringStartTick != -1
                        || restored->world.deterministicSimulationStateHash() != authority)
                        return fail("legacy presentation or gameplay hash changed");
                    std::cout << "Squid punch persistence: six malformed records rejected atomically; legacy and gameplay hash passed\n";
                }
            }
            if (!root.empty() && hz == 30 && rank == 0 && !noCooldown
                && (s.tick - firstShot) % 16 == 0 && age < 32) {
                const auto stats = drawRuntimeStateOffscreenAtCell(s, {64,64}, 800, 600, error,
                    root + "/" + targetType + "-age-" + std::to_string(age) + ".bmp", false);
                if (!stats || stats->squidGrappleDrawn) return fail(label + " render: " + error);
            }
            const auto* victim = findObjectById(s, 2);
            if (age >= 32 && (rank == 0 || !victim || victim->health <= 0)) break;
            if (step == hz * 20 - 1) return fail(label + " attack did not finish");
        }
        if (firstShot < 0 || phases.size() != 16 || (rank == 1 && shotCount < 2))
            return fail(label + " missing full sequence or repeated real shots");
        std::cout << "Squid punch: " << label << " shots=" << shotCount << " complete native action passed\n";
        ++cases;
    }
    std::cout << "Squid punch: " << cases << " real attack cases, " << discharges << " discharges, "
        << frames << " native frame comparisons, " << saves << " saves with exact snapshot extensions, "
        << continuationFrames << " restored frame comparisons passed\n";
    return true;
}
