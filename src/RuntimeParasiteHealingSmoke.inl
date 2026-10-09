// Production launch, passive healing and depot transactions, using stock rules.
bool runRuntimeParasiteHealingSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->config.playerSlots.clear();
    stock->config.gameModeRulesOverride.clear();
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world,
        &stock->theater, &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) { error = "parasite/healing stock resources unavailable"; return false; }
    const auto* droneWeapon = stock->rules.findWeaponType("DroneJump");
    const auto* apocalypse = stock->rules.findUnitType("Vehicle", "APOC");
    if (!droneWeapon || !apocalypse || droneWeapon->damage != 50 || droneWeapon->rof != 60
        || apocalypse->strength != 800 || !apocalypse->selfHealing) {
        error = "parasite/healing original-rule oracle changed"; return false;
    }
    constexpr int sourceId = 101, targetId = 102, depotId = 103;
    int cases = 0, bites = 0, heals = 0, repairPulses = 0, savedChecks = 0;
    const auto fail = [&](const std::string& message) {
        error = "parasite/healing: " + message;
        std::cout << error << '\n'; return false;
    };
    const auto make = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.crates = s.config.shortGame = false;
        s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        s.report = stock->report; s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "Russians";
        s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        RuntimeHumanHouseAuthority authority; authority.owner = s.economy.owner;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
        authority.localFacade = true; s.humanHouseAuthorities.push_back(std::move(authority));
        s.structureFootprints["NADEPT"] = {3, 3, true};
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world);
        const auto add = [&](int id, const char* type, const char* owner, int hp,
                             WorldObjectKind kind, int x, int y) {
            WorldObjectInstance object; object.instanceId = id; object.type = type; object.owner = owner;
            object.kind = kind; object.alive = object.poweredOperational = true;
            object.health = hp; object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
            object.mission = "Guard"; object.currentAmmo = -1;
            s.world.accessObjects().push_back(object);
        };
        add(sourceId, "DRON", "Americans", 100, WorldObjectKind::Unit, 3, 8);
        add(targetId, "APOC", "Russians", 800, WorldObjectKind::Unit, 4, 8);
        add(depotId, "NADEPT", "Russians", 1200, WorldObjectKind::Structure, 12, 12);
        ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeGarrisonsInitialized(s);
        ensureRuntimeGatesInitialized(s); ensureRuntimeSubcellsInitialized(s); ensureRuntimeRepairDepotsInitialized(s);
        return box;
    };
    const auto attach = [&](RuntimeWindowState& s) {
        const auto* source = findObjectById(s, sourceId); const auto* target = findObjectById(s, targetId);
        if (!source || !target) return fail("launch fixture object missing");
        const RuntimeWeaponProfile profile = runtimeWeaponProfileForObject(s, *source, target);
        const auto* weapon = profile.weapon;
        if (!weapon) return fail("weapon selection " + source->type + " -> " + target->type
            + " domain=" + std::to_string(static_cast<int>(runtimeTargetDomainForObject(s, *target))));
        if (!launchRuntimeParasiteProjectile(s, *source, *target, *weapon, profile)
            || s.projectiles.size() != 1) return fail("launch " + weapon->id + ": " + s.simulationStatus);
        const auto impact = s.projectiles.front(); s.projectiles.clear();
        if (!applyRuntimeProjectileImpact(s, impact) || !s.specialMechanisms.parasiteLinks().contains(targetId))
            return fail("impact " + impact.weaponId + ": " + s.simulationStatus);
        return true;
    };
    const auto bite = [&](RuntimeWindowState& s) {
        const auto actions = s.specialMechanisms.tickParasites(s.world.getObjects(), s.rules, s.tick, {});
        const auto result = applyRuntimeParasiteActionsAtomically(s, actions, {}, sourceId);
        return result.accepted;
    };
    // Exercise the production parasite authority validator directly. Objects
    // use its published 39-field schema; other domains are deliberately absent.
    // Full-game save/load has separate fixture requirements and tests.
    const auto persisted = [&](RuntimeWindowState& s, bool expected, const std::string& label,
                               const std::string& extra = std::string()) {
        std::ostringstream out; s.specialMechanisms.save(out);
        out << "\n[Objects]\nCount=" << s.world.getObjects().size() << '\n';
        int index = 0;
        for (const auto& object : s.world.getObjects()) {
            std::vector<std::string> fields(39, "0");
            fields[1] = std::to_string(object.instanceId); fields[20] = std::to_string(object.veterancy);
            fields[23] = std::to_string(object.dockedToObjectId); fields[24] = std::to_string(object.dockIndex);
            fields[25] = runtimeBoolText(object.airborne); fields[26] = std::to_string(object.flightLevel);
            out << index++ << '=';
            for (std::size_t i = 0; i < fields.size(); ++i) out << (i ? "|" : "") << fields[i];
            out << '\n';
        }
        out << "\n[Behaviors]\nCount=" << s.behaviors.size() << '\n'; index = 0;
        for (const auto& [id, behavior] : s.behaviors)
            out << index++ << '=' << id << '|' << runtimeBehaviorKindName(behavior.kind) << '|'
                << behavior.targetObjectId << '|' << behavior.moveTarget.x << '|' << behavior.moveTarget.y
                << '|' << behavior.weaponCooldownTicks << '|' << behavior.moveCooldownTicks << '|'
                << runtimeBoolText(behavior.attackBridge) << '|' << behavior.attackBridgeCell.x << '|'
                << behavior.attackBridgeCell.y << '|' << behavior.pendingAllowedOccupiedObjectId << '|'
                << behavior.moveIntervalRemainderQ16 << '|' << runtimeBoolText(behavior.forceFire)
                << '|' << runtimeBoolText(behavior.attackGround) << '\n';
        out << extra;
        IniFile ini; std::string detail; std::unordered_map<int, int> stuns;
        RuntimeProjectileSaveCandidate projectiles; projectiles.nextActorId = s.nextProjectileActorId;
        if (!ini.loadFromString(out.str())) return fail("saved fixture parse failed");
        const bool accepted = validateRuntimeSavedParasiteState(s, ini, 14, s.specialMechanisms,
            s.world.getObjects(), projectiles, s.tick, s.repairDepots, s.factoryDeliveries, stuns, detail);
        ++savedChecks;
        return accepted == expected || fail("saved " + label + " expected=" + std::to_string(expected) + " " + detail);
    };
    const auto dock = [&](RuntimeWindowState& s) {
        s.selectedObjectIds = {targetId};
        const auto* depot = findObjectById(s, depotId);
        if (!depot || !queueRuntimeUnitRepairCommand(s, runtimeCellPointForObject(*depot), *depot)
            || s.lastCommand.acceptedCount != 1 || !s.specialMechanisms.parasiteLinks().contains(targetId)) return false;
        if (!persisted(s, true, "reserved repair")) return false;
        const auto* relation = findRuntimeRepairDepotRelationForUnit(s.repairDepots, targetId);
        auto* target = findMutableObjectById(s, targetId);
        if (!relation || !target) return false;
        // Place the fixture at the command's reserved physical dock. The actual
        // production arrival transaction must still accept and attach it.
        target->x = relation->dockCell.x; target->y = relation->dockCell.y;
        target->cell = runtimeCellKey(target->x, target->y); target->onBridge = relation->dockCell.onBridge;
        clearRuntimeRenderPosition(*target);
        if (completeRuntimeRepairDepotArrival(s, targetId) != RuntimeRepairDepotArrivalResult::Attached)
            return fail("repair physical arrival: " + s.simulationStatus);
        // The simulation caller removes completed Move records after the
        // arrival transaction. Finish that caller step before testing a save.
        eraseRuntimeBehavior(s, targetId);
        return s.specialMechanisms.parasiteLinks().contains(targetId)
            && persisted(s, true, "docked repair");
    };
    for (int hz : {15, 30, 60}) {
        {
            auto box = make(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            if (!attach(s) || !bite(s) || !persisted(s, true, "stationary baseline")) return fail("save baseline failed");
            RuntimeObjectBehavior move; move.kind = RuntimeBehaviorKind::Move; move.moveTarget = {8, 8};
            s.behaviors[targetId] = move;
            if (!persisted(s, true, "ordinary move")) return false;
            s.behaviors[sourceId] = move;
            if (!persisted(s, false, "limbo source move")) return false;
            s.behaviors.erase(sourceId);
            s.behaviors[targetId].kind = RuntimeBehaviorKind::Attack;
            s.behaviors[targetId].targetObjectId = sourceId;
            if (!persisted(s, false, "host attack authority")) return false;
            s.behaviors[targetId] = move; s.behaviors[targetId].pendingAllowedOccupiedObjectId = depotId;
            if (!persisted(s, false, "unrelated allowed occupied host")) return false;
            s.behaviors[targetId] = move;
            if (!persisted(s, false, "transport overlap", "\n[TransportManifests]\nCount=1\n0=103|102\n")) return false;
        }
        {
            auto box = make(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            auto* source = findMutableObjectById(s, sourceId); source->type = "SQD"; source->health = 200;
            source->submerged = true;
            auto* target = findMutableObjectById(s, targetId); target->type = "DEST"; target->health = 600;
            // Squid LandTargeting=1 rejects a ship standing on the land test
            // fixture. Give the naval-only save guard its proper water domain.
            for (auto& [key, terrain] : s.terrainInfoByCell) {
                (void)key; terrain.kind = RuntimeTerrainKind::Water; terrain.water = true;
                terrain.authoredLandType = terrain.movementLandType = RuntimeTerrainLandType::Water;
            }
            if (!attach(s) || !persisted(s, true, "paralyzed squid baseline")) return fail("squid save baseline failed");
            RuntimeObjectBehavior move; move.kind = RuntimeBehaviorKind::Move; move.moveTarget = {8, 8};
            s.behaviors[targetId] = move;
            if (!persisted(s, false, "paralyzed squid host move")) return false;
        }
        {
            auto box = make(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            if (!attach(s)) return fail("stock launch/impact failed hz=" + std::to_string(hz));
            int expected = 800, localBites = 0, localHeals = 0;
            for (s.tick = 0; s.tick <= hz * 8; ++s.tick) {
                const int before = findObjectById(s, targetId)->health;
                if (!bite(s)) return fail("bite rejected");
                const int damage = before - findObjectById(s, targetId)->health;
                const int expectedDamage = s.tick % (hz * 4) == 0 ? 50 : 0;
                if (damage != expectedDamage) return fail("native 50 damage / 60 frame cadence diverged");
                expected -= damage; localBites += damage > 0;
                const int preHeal = findObjectById(s, targetId)->health;
                (void)advanceRuntimeSelfHealing(s);
                const int recovered = findObjectById(s, targetId)->health - preHeal;
                if (recovered < 0 || recovered > 1) return fail("passive heal amount diverged");
                expected += recovered; localHeals += recovered;
                const auto* source = findRuntimeObjectByIdIncludingInactive(s, sourceId);
                if (!s.specialMechanisms.parasiteLinks().contains(targetId) || !source || source->health != 100
                    || source->alive || source->mission != "ParasiteLimbo"
                    || findObjectById(s, targetId)->health != expected) return fail("passive self-healing removed live parasite");
            }
            if (localBites != 3 || localHeals == 0 || expected >= 750) return fail("insufficient attack/self-heal coexistence coverage");
            bites += localBites; heals += localHeals; ++cases;
            std::cout << "Parasite healing hz=" << hz << " bites=" << localBites << " passiveHeals=" << localHeals
                << " hostHp=" << expected << " parasitePreserved=yes\n";
        }
        for (bool completes : {false, true}) {
            auto box = make(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            if (!attach(s) || !bite(s) || !dock(s)) return fail("infected vehicle repair command/arrival failed");
            auto* target = findMutableObjectById(s, targetId);
            RuntimeRepairDepotAction zero;
            zero.kind = RuntimeRepairDepotActionKind::Repair; zero.hostId = depotId; zero.unitId = targetId;
            zero.dockIndex = target->dockIndex; zero.owner = runtimeRepairDepotCanonicalOwnerId(s, target->owner);
            zero.healthBefore = zero.healthAfter = target->health;
            const auto linkHash = s.specialMechanisms.stateHash(); const int balance = s.economy.credits;
            if (!applyRuntimeRepairDepotActions(s, {}) || !applyRuntimeRepairDepotActions(s, {zero})
                || s.specialMechanisms.stateHash() != linkHash || s.economy.credits != balance)
                return fail("empty/zero-health repair cleared parasite");
            auto invalid = zero; invalid.healedHitPoints = 8; invalid.healthAfter += 8;
            invalid.creditCost = balance + 1;
            if (applyRuntimeRepairDepotActions(s, {invalid}) || s.specialMechanisms.stateHash() != linkHash
                || s.economy.credits != balance || findObjectById(s, targetId)->health != zero.healthBefore)
                return fail("unaffordable repair batch was not rejected atomically");
            s.economy.credits = 0;
            for (s.tick = 1; s.tick <= hz * 3; ++s.tick) (void)advanceRuntimeRepairDepots(s);
            if (!s.specialMechanisms.parasiteLinks().contains(targetId) || s.economy.credits != 0
                || findObjectById(s, targetId)->health != zero.healthBefore)
                return fail("unfunded actual depot timer healed or removed parasite");
            if (completes) findMutableObjectById(s, targetId)->health = 799;
            const int before = findObjectById(s, targetId)->health;
            s.economy.credits = 10000;
            const int deadline = s.tick + hz * 3; bool healed = false;
            for (; s.tick <= deadline; ++s.tick) {
                (void)advanceRuntimeRepairDepots(s);
                if (findObjectById(s, targetId)->health > before) { healed = true; break; }
            }
            if (!healed || s.specialMechanisms.parasiteLinks().contains(targetId) || s.economy.credits >= 10000)
                return fail("funded actual repair pulse did not remove parasite");
            (void)drainRuntimeObjectExitQueue(s);
            const auto* source = findRuntimeObjectByIdIncludingInactive(s, sourceId);
            if (source && (source->alive || source->health > 0)) return fail("repair did not destroy mechanical parasite");
            if (completes && (findObjectById(s, targetId)->health != 800
                || findRuntimeRepairDepotRelationForUnit(s.repairDepots, targetId) != nullptr
                || findObjectById(s, targetId)->dockedToObjectId >= 0)) return fail("repair+undock batch did not complete");
            ++cases; ++repairPulses;
            std::cout << "Parasite repair hz=" << hz << " completes=" << completes
                << " noMoneyPreserved=yes zeroHealPreserved=yes atomicReject=yes fundedRemoval=yes\n";
        }
    }
    std::cout << "Parasite healing result cases=" << cases << " bites=" << bites << " passiveHeals=" << heals
        << " repairPulses=" << repairPulses << " savedChecks=" << savedChecks << " result=PASS\n";
    return true;
}
