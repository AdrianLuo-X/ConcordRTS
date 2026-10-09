bool runRuntimeMastermindOverloadSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "mastermind overload: " + message; return false; };
    const auto& original = live.rules.getSpecialMechanismRules();
    const auto* weapon = live.rules.findWeaponType("MultipleMindControlTank");
    if (weapon == nullptr || !weapon->infiniteMindControl || weapon->damage != 3
        || original.overloadCount != std::vector<int>{3, 6, 10, 50}
        || original.overloadDamage != std::vector<int>{0, 50, 100, 500}
        || original.overloadFrames != std::vector<int>{30, 60, 60, 60})
        return fail("original safe capacity or overload table missing");
    IniFile customized;
    customized.loadFromString("[CombatDamage]\nOverloadCount=2,5\nOverloadDamage=0,7\nOverloadFrames=1,2\n");
    RulesDatabase customRules;
    customRules.loadFromIni(customized);
    if (customRules.getSpecialMechanismRules().overloadCount != std::vector<int>{2, 5}
        || customRules.getSpecialMechanismRules().overloadDamage != std::vector<int>{0, 7}
        || customRules.getSpecialMechanismRules().overloadFrames != std::vector<int>{1, 2})
        return fail("configured overload table was ignored");
    IniFile malformed;
    malformed.loadFromString("[CombatDamage]\nOverloadCount=3,6\nOverloadDamage=0,50junk\nOverloadFrames=1,2\n");
    RulesDatabase malformedRules;
    malformedRules.loadFromIni(malformed);
    if (malformedRules.getSpecialMechanismRules().overloadCount != original.overloadCount
        || malformedRules.getSpecialMechanismRules().overloadDamage != original.overloadDamage
        || malformedRules.getWarnings().empty()) return fail("malformed overload table was not rejected");
    const auto makeProbe = [&](int hz) {
        auto probe = std::make_unique<RuntimeWindowState>();
        probe->config = live.config; probe->config.fixedTickHz = hz;
        probe->rules = live.rules; probe->rulesLoaded = true;
        probe->art = live.art; probe->artLoaded = true;
        probe->economy.owner = "YuriCountry";
        probe->economy.skirmishStartApplied = true;
        probe->triggers.bootstrapComplete = true;
        probe->runtimeSuppressPlayerAudioFeedback = true;
        probe->runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            probe->world.accessTerrainCells().push_back(cell);
        }
        addRuntimeObjectInstance(*probe, RuntimeBuildKind::Unit, "MIND", {6, 6});
        initializeRuntimeNavigation(*probe);
        ensureRuntimeSpecialMechanismsInitialized(*probe);
        // An idle original capture manager also advances its safe-tier timer.
        advanceRuntimeSpecialMechanisms(*probe);
        return probe;
    };
    const auto addTargets = [&](RuntimeWindowState& probe, int count, bool capture) {
        std::vector<int> ids;
        const int controller = probe.world.getObjects().front().instanceId;
        for (int i = 0; i < count; ++i) {
            auto* target = addRuntimeObjectInstanceForOwner(probe, "Americans", RuntimeBuildKind::Infantry,
                "E2", {7 + i % 6, 2 + i / 6});
            if (target == nullptr) return std::vector<int>{};
            target->mission = "Sleep";
            target->weaponCooldownTicksRemaining = 100000;
            ids.push_back(target->instanceId);
            if (capture) {
                std::string reason;
                if (!probe.specialMechanisms.tryMindControl(probe.world.accessObjects(), probe.rules,
                        controller, ids.back(), weapon->damage, false, true, false, probe.tick,
                        nullptr, nullptr, reason)) return std::vector<int>{};
            }
        }
        return ids;
    };
    const auto advanceTo = [&](RuntimeWindowState& probe, int tick) {
        while (probe.tick < tick) { ++probe.tick; advanceRuntimeSpecialMechanisms(probe); }
    };
    int cases = 0;
    {
        auto storage = makeProbe(30);
        auto& probe = *storage;
        const int id = probe.world.getObjects().front().instanceId;
        const auto targets = addTargets(probe, 3, true);
        if (targets.size() != 3U) return fail("custom overload fixture missing");
        std::ostringstream links;
        probe.specialMechanisms.save(links);
        IniFile retained;
        retained.loadFromString(links.str());
        IniFile timerReset;
        timerReset.loadFromString("[SpecialMindControlOverload]\nCount=0\n");
        retained.appendFrom(timerReset);
        probe.specialMechanisms.reset(customRules.getSpecialMechanismRules());
        if (!probe.specialMechanisms.load(retained, error)) return fail("custom control links failed to load");
        const auto first = probe.specialMechanisms.tickMindControlOverload(probe.world.getObjects(), probe.rules, 0, 30);
        const auto duplicate = probe.specialMechanisms.tickMindControlOverload(probe.world.getObjects(), probe.rules, 0, 30);
        const auto early = probe.specialMechanisms.tickMindControlOverload(probe.world.getObjects(), probe.rules, 5, 30);
        const auto second = probe.specialMechanisms.tickMindControlOverload(probe.world.getObjects(), probe.rules, 6, 30);
        if (first.size() != 1U || first.front().controllerObjectId != id || first.front().damage != 7
            || !first.front().playSound || !duplicate.empty() || !early.empty()
            || second.size() != 1U || second.front().playSound)
            return fail("custom cadence, single-tick idempotency or overload voice recurrence changed");
    }
    for (int hz : {30, 60}) for (int count : {3, 4, 6, 7, 10, 11, 51}) {
        auto storage = makeProbe(hz);
        auto& probe = *storage;
        const int id = probe.world.getObjects().front().instanceId;
        const auto targets = addTargets(probe, count, true);
        if (targets.size() != static_cast<std::size_t>(count)) return fail("capture fixture failed");
        const int firstPulse = 31 * hz / 15;
        advanceTo(probe, firstPulse - 1);
        if (findObjectById(probe, id)->health != 500) return fail("damage before original safe-tier delay");
        std::ostringstream saved;
        probe.specialMechanisms.save(saved);
        IniFile ini;
        ini.loadFromString(saved.str());
        RuntimeSpecialMechanismState restored;
        restored.reset(original);
        if (!restored.load(ini, error) || restored.stateHash() != probe.specialMechanisms.stateHash())
            return fail("pending overload timer failed to round trip: " + error);
        probe.specialMechanisms = std::move(restored);
        advanceTo(probe, firstPulse);
        const int pulse = count <= 3 ? 0 : count <= 6 ? 50 : count <= 10 ? 100 : 500;
        const auto* controller = findRuntimeObjectByIdIncludingInactive(probe, id);
        if (controller == nullptr || controller->health != 500 - pulse)
            return fail("wrong first pulse for count=" + std::to_string(count));
        if (pulse == 500) {
            if (controller->alive || !probe.specialMechanisms.mindControlLinks().empty())
                return fail("overload death did not release its control links");
            for (int targetId : targets) {
                const auto* target = findObjectById(probe, targetId);
                if (target == nullptr || !target->alive || target->owner != "Americans")
                    return fail("overload death harmed or retained a captive");
            }
        } else if (pulse > 0) {
            const int nextPulse = firstPulse + 61 * hz / 15;
            advanceTo(probe, nextPulse - 1);
            if (findObjectById(probe, id)->health != 500 - pulse) return fail("overload drained every fixed tick");
            advanceTo(probe, nextPulse);
            if (findObjectById(probe, id)->health != 500 - 2 * pulse) return fail("periodic overload stopped");
            for (std::size_t i = 3; i < targets.size(); ++i)
                probe.specialMechanisms.releaseMindControlByTarget(probe.world.accessObjects(), targets[i],
                    probe.tick, nullptr, "overload recovery test");
            advanceTo(probe, nextPulse + 122 * hz / 15);
            if (findObjectById(probe, id)->health != 500 - 2 * pulse
                || probe.specialMechanisms.mindControlLinks().size() != 3U)
                return fail("returning to three controls did not stop damage");
        } else {
            advanceTo(probe, 200 * hz / 15);
            if (findObjectById(probe, id)->health != 500) return fail("three safe controls took damage");
        }
        ++cases;
    }
    {
        auto storage = makeProbe(30);
        auto& probe = *storage;
        const int id = probe.world.getObjects().front().instanceId;
        const auto targets = addTargets(probe, 4, true);
        findMutableObjectById(probe, targets.back())->alive = false;
        advanceTo(probe, 250);
        if (findObjectById(probe, id)->health != 500 || probe.specialMechanisms.mindControlLinks().size() != 3U)
            return fail("a dead captive still contributed to overload");
        for (int version : {1, 2}) {
            IniFile oldSave;
            oldSave.loadFromString("[SpecialMechanisms]\nVersion=" + std::to_string(version)
                + "\n[SpecialMindControl]\nCount=0\n[SpecialParasitePending]\nCount=0\n"
                  "[SpecialParasites]\nCount=0\n[SpecialParasiteResolved]\nCount=0\n");
            RuntimeSpecialMechanismState compatible;
            compatible.reset(original);
            if (!compatible.load(oldSave, error)) return fail("old saves stopped loading: " + error);
        }
        std::ostringstream stable;
        probe.specialMechanisms.save(stable);
        IniFile invalid;
        invalid.loadFromString(stable.str());
        IniFile corrupt;
        corrupt.loadFromString("[SpecialMindControlOverload]\nCount=1\n0=1|-1|yes\n");
        invalid.appendFrom(corrupt);
        const auto hash = probe.specialMechanisms.stateHash();
        if (probe.specialMechanisms.load(invalid, error) || probe.specialMechanisms.stateHash() != hash)
            return fail("invalid overload save was accepted or partially applied");
    }
    {
        auto storage = makeProbe(30);
        auto& probe = *storage;
        const int id = probe.world.getObjects().front().instanceId;
        const auto targets = addTargets(probe, 4, false);
        if (targets.size() != 4U) return fail("fourth-target player fixture missing");
        const auto captured = [&](int targetId) {
            const auto* target = findObjectById(probe, targetId);
            return target != nullptr && target->alive && target->owner == "YuriCountry";
        };
        for (std::size_t i = 0; i < targets.size(); ++i) {
            auto* target = findMutableObjectById(probe, targets[i]);
            setRuntimeObjectCell(probe, *target, {7 + static_cast<int>(i), 6});
            applySelection(probe, {id}, false, false);
            issueRuntimeAttackCommand(probe, *target);
            int attempts = 0;
            while (!captured(targets[i]) && attempts++ < 300) {
                ++probe.tick;
                advanceRuntimeSimulation(probe);
            }
            if (!captured(targets[i]))
                return fail("player attack failed to capture target " + std::to_string(i + 1));
        }
        for (int i = 0; i < 240; ++i) { ++probe.tick; advanceRuntimeSimulation(probe); }
        const auto* controller = findObjectById(probe, id);
        if (controller == nullptr || !controller->alive || controller->health >= 500
            || probe.specialMechanisms.mindControlLinks().size() != 4U)
            return fail("real fourth-target command did not keep control and progressively lose health: health="
                + std::to_string(controller == nullptr ? -1 : controller->health)
                + " controlled=" + std::to_string(probe.specialMechanisms.mindControlLinks().size()));
    }
    {
        auto storage = makeProbe(30);
        auto& probe = *storage;
        const int id = probe.world.getObjects().front().instanceId;
        auto* controller = findMutableObjectById(probe, id);
        controller->type = "YAPSYT"; controller->kind = WorldObjectKind::Structure;
        const auto targets = addTargets(probe, 4, false);
        for (int i = 0; i < 4; ++i) {
            std::string reason;
            const bool accepted = probe.specialMechanisms.tryMindControl(probe.world.accessObjects(), probe.rules,
                id, targets[i], 3, false, false, false, probe.tick, nullptr, nullptr, reason);
            if (accepted != (i < 3)) return fail("finite Psychic Tower capacity changed");
        }
        advanceTo(probe, 250);
        if (findObjectById(probe, id)->health != 500) return fail("finite mind control incorrectly overloaded");
    }
    error.clear();
    std::cout << "Mastermind overload: " << cases << " stock tier/tick-rate cases, pending save, recovery, death release, fourth-target player command, finite tower=ok\n";
    return true;
}
