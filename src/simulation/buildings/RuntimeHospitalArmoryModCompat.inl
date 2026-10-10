int runRuntimeHospitalArmoryModCompatibilitySmokeTest(
    const std::string& rulesPath
) {
    const auto fail = [](const std::string& message) {
        std::cerr << "Hospital/Armory runtime integration smoke failed: "
                  << message << "\n";
        return 1;
    };

    IniFile rulesIni;
    RulesDatabase rules;
    if (!rulesIni.loadFromFile(rulesPath) || !rules.loadFromIni(rulesIni)) {
        return fail("could not load Mod fixture rules");
    }

    // The multiplayer snapshot and authoritative state paths both mix this
    // exact TechTree representation. Prove that the service-use Ammo value is
    // not merely present in raw content, but changes the canonical rules hash.
    std::ifstream rulesInput(rulesPath, std::ios::binary);
    std::string alteredRulesText(
        (std::istreambuf_iterator<char>(rulesInput)),
        std::istreambuf_iterator<char>()
    );
    const std::size_t hospitalSection = alteredRulesText.find("[MODHOSP]");
    const std::size_t hospitalAmmo = hospitalSection == std::string::npos
        ? std::string::npos
        : alteredRulesText.find("Ammo=2", hospitalSection);
    if (hospitalAmmo == std::string::npos) {
        return fail("could not isolate Hospital Ammo fingerprint fixture");
    }
    alteredRulesText.replace(hospitalAmmo, 6U, "Ammo=3");
    IniFile alteredIni;
    RulesDatabase alteredRules;
    if (!alteredIni.loadFromString(alteredRulesText)
        || !alteredRules.loadFromIni(alteredIni)) {
        return fail("could not load altered fingerprint rules");
    }
    const std::uint64_t rulesHash = runtimeMultiplayerSnapshotTechTreeHash(
        analyzeTechTree(rules, "Player"),
        true
    );
    const std::uint64_t alteredRulesHash =
        runtimeMultiplayerSnapshotTechTreeHash(
            analyzeTechTree(alteredRules, "Player"),
            true
        );
    if (rulesHash == alteredRulesHash) {
        return fail("service Ammo did not change multiplayer TechTree hash");
    }

    auto ownedState = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& state = *ownedState;
    state.config.fixedTickHz = 30;
    state.config.defaultMapPath = pathString(
        std::filesystem::path(rulesPath).parent_path()
            / "hospital_armory_mod_map.map"
    );
    state.config.playerCountry = "Player";
    state.config.startingCredits = 2000;
    state.rules = rules;
    state.rulesLoaded = true;
    state.runtimeSuppressPlayerAudioFeedback = true;
    for (const std::string& type : {
            "MODHOSP", "MODARM", "MODARMDEFAULT", "MODPLAIN", "MODBOTH"
        }) {
        state.structureFootprints[type] = {1, 1, true};
    }
    const auto ensureFlatFixtureTerrain = [&state]() {
        // A header-only fixture produces MapQuery cells whose hasTerrain bit
        // is intentionally false. The synthetic terrain below is the test's
        // authoritative replacement, so keep MapQuery from vetoing it.
        state.mapFacts = {};
        state.mapFactsLoaded = false;
        state.world.accessTerrainCells().clear();
        state.terrainInfoByCell.clear();
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 24; ++x) {
                TerrainCell terrain;
                terrain.x = x;
                terrain.y = y;
                terrain.cellIndex512 = (y * 512) + x;
                state.world.accessTerrainCells().push_back(terrain);
                state.terrainInfoByCell.emplace(
                    runtimeCellKey(x, y),
                    RuntimeTerrainInfo{}
                );
            }
        }
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    };
    ensureFlatFixtureTerrain();
    state.economy.owner = "Player";
    initializeRuntimeEconomy(state);
    state.economy.owner = "Player";

    const auto add = [&state](
        RuntimeBuildKind kind,
        const std::string& type,
        const RuntimeCellPoint& cell
    ) {
        return addRuntimeObjectInstanceForOwner(
            state,
            "Player",
            kind,
            type,
            cell
        );
    };
    WorldObjectInstance* hospital = add(
        RuntimeBuildKind::Building,
        "MODHOSP",
        {10, 10}
    );
    const int hospitalId = hospital == nullptr ? -1 : hospital->instanceId;
    WorldObjectInstance* armory = add(
        RuntimeBuildKind::Building,
        "MODARM",
        {15, 10}
    );
    const int armoryId = armory == nullptr ? -1 : armory->instanceId;
    WorldObjectInstance* plain = add(
        RuntimeBuildKind::Building,
        "MODPLAIN",
        {20, 10}
    );
    const int plainId = plain == nullptr ? -1 : plain->instanceId;
    WorldObjectInstance* wounded = add(
        RuntimeBuildKind::Infantry,
        "MODINF",
        {6, 10}
    );
    const int woundedId = wounded == nullptr ? -1 : wounded->instanceId;
    WorldObjectInstance* trainee = add(
        RuntimeBuildKind::Infantry,
        "MODNOTRAIN",
        {12, 12}
    );
    const int traineeId = trainee == nullptr ? -1 : trainee->instanceId;
    if (hospital == nullptr || armory == nullptr || plain == nullptr
        || wounded == nullptr || trainee == nullptr) {
        return fail("could not create runtime fixture objects");
    }
    for (int id : {hospitalId, armoryId, plainId}) {
        WorldObjectInstance* structure = findMutableObjectById(state, id);
        if (structure != nullptr) {
            structure->mission = "Guard";
            structure->buildAnimTicksTotal = 0;
            structure->buildAnimTicksRemaining = 0;
        }
    }
    wounded = findMutableObjectById(state, woundedId);
    trainee = findMutableObjectById(state, traineeId);
    hospital = findMutableObjectById(state, hospitalId);
    armory = findMutableObjectById(state, armoryId);
    plain = findMutableObjectById(state, plainId);
    if (wounded == nullptr || trainee == nullptr || hospital == nullptr
        || armory == nullptr || plain == nullptr
        || hospital->currentAmmo != 2 || armory->currentAmmo != 2
        || plain->currentAmmo != -1) {
        return fail("runtime creation did not initialize service charges");
    }
    wounded->health = 25;
    trainee->veterancy = 0;
    trainee->veterancyExperience = 77;

    state.selectedObjectIds = {woundedId};
    if (runtimeTargetCursorIntent(state, *hospital)
            != RuntimeTargetCursorIntent::Enter
        || !queueRuntimeTargetAbility(state, *hospital)) {
        return fail("Hospital did not expose/queue the production Enter path");
    }
    const auto pendingHeal = state.behaviors.find(woundedId);
    if (pendingHeal == state.behaviors.end()
        || !runtimeBehaviorIsPendingInfantryService(
            state,
            woundedId,
            pendingHeal->second
        )) {
        return fail("Hospital entry did not create target-linked authority");
    }

    // Save while the soldier is en route. The v13+ orphan-entry validator
    // must recognize this intent and reconstruct it without an extra side map.
    const std::filesystem::path pendingSavePath =
        "runtime_hospital_armory_pending_smoke.ini";
    std::string errorMessage;
    if (!saveRuntimeState(state, pathString(pendingSavePath), errorMessage)
        || !loadRuntimeState(
            state,
            pathString(pendingSavePath),
            errorMessage
        )) {
        return fail("pending service save/load failed: " + errorMessage);
    }
    const auto restoredPending = state.behaviors.find(woundedId);
    wounded = findMutableObjectById(state, woundedId);
    hospital = findMutableObjectById(state, hospitalId);
    if (restoredPending == state.behaviors.end()
        || !runtimeBehaviorIsPendingInfantryService(
            state,
            woundedId,
            restoredPending->second
        )
        || wounded == nullptr || wounded->health != 25
        || hospital == nullptr || hospital->currentAmmo != 2) {
        return fail("pending service authority or values changed on load");
    }

    // The deliberately tiny fixture has no IsoMapPack payload. Runtime saves
    // persist simulation authority, not the map's immutable terrain payload,
    // so restore the synthetic flat test terrain after the load boundary.
    ensureFlatFixtureTerrain();

    std::string serviceCompletionDiagnostic;
    const auto completeQueuedService = [
        &state,
        &serviceCompletionDiagnostic
    ](int infantryId) {
        auto behavior = state.behaviors.find(infantryId);
        WorldObjectInstance* infantry = findMutableObjectById(
            state,
            infantryId
        );
        if (behavior == state.behaviors.end() || infantry == nullptr) {
            serviceCompletionDiagnostic = "missing behavior or infantry";
            return false;
        }
        setRuntimeObjectCell(state, *infantry, behavior->second.moveTarget);
        clearRuntimeRenderPosition(*infantry);
        behavior->second.path.clear();
        behavior->second.pathRequestPending = false;
        behavior->second.moveCooldownTicks = 0;
        if (!completeRuntimeInfantryService(state, infantryId)) {
            const WorldObjectInstance* target = findObjectById(
                state,
                behavior->second.targetObjectId
            );
            std::ostringstream diagnostic;
            diagnostic << "pending="
                       << runtimeBehaviorIsPendingInfantryService(
                              state,
                              infantryId,
                              behavior->second
                          )
                       << " actor=" << infantry->x << "," << infantry->y
                       << " moveTarget=" << behavior->second.moveTarget.x
                       << "," << behavior->second.moveTarget.y
                       << " targetId=" << behavior->second.targetObjectId
                       << " terrain=" << state.world.getTerrainCells().size();
            if (target != nullptr) {
                diagnostic << " distance="
                           << runtimeCellDistanceToObjectFootprint(
                                  state,
                                  runtimeCellPointForObject(*infantry),
                                  *target
                              )
                           << " exit="
                           << findRuntimePassableCellNear(
                                  state,
                                  *infantry,
                                  {target->x - 1, target->y - 1},
                                  4
                              ).has_value();
            } else {
                diagnostic << " target=missing";
            }
            serviceCompletionDiagnostic = diagnostic.str();
            return false;
        }
        eraseRuntimeBehavior(state, infantryId);
        return true;
    };
    if (!completeQueuedService(woundedId)) {
        return fail(
            "Hospital queued arrival could not complete: "
            + serviceCompletionDiagnostic
        );
    }
    wounded = findMutableObjectById(state, woundedId);
    hospital = findMutableObjectById(state, hospitalId);
    if (wounded == nullptr || hospital == nullptr
        || state.behaviors.contains(woundedId)
        || wounded->health != 100
        || hospital->currentAmmo != 1
        || runtimeCellDistanceToObjectFootprint(
            state,
            runtimeCellPointForObject(*wounded),
            *hospital
        ) <= 0.001) {
        std::ostringstream diagnostic;
        diagnostic << "Hospital enter/heal/exit lifecycle diverged"
                   << " behavior=" << state.behaviors.contains(woundedId);
        if (wounded != nullptr) {
            diagnostic << " health=" << wounded->health
                       << " pos=" << wounded->x << "," << wounded->y
                       << " subcell=" << wounded->subCell
                       << " mission=" << wounded->mission;
        }
        if (hospital != nullptr) {
            diagnostic << " ammo=" << hospital->currentAmmo
                       << " hostPos=" << hospital->x << "," << hospital->y;
        }
        diagnostic << " status=" << state.simulationStatus;
        return fail(diagnostic.str());
    }

    trainee = findMutableObjectById(state, traineeId);
    armory = findMutableObjectById(state, armoryId);
    state.selectedObjectIds = {traineeId};
    if (trainee == nullptr || armory == nullptr
        || runtimeTargetCursorIntent(state, *armory)
            != RuntimeTargetCursorIntent::Enter
        || !queueRuntimeTargetAbility(state, *armory)) {
        return fail("Armory did not expose/queue the production Enter path");
    }
    if (!completeQueuedService(traineeId)) {
        return fail(
            "Armory queued arrival could not complete: "
            + serviceCompletionDiagnostic
        );
    }
    trainee = findMutableObjectById(state, traineeId);
    armory = findMutableObjectById(state, armoryId);
    plain = findMutableObjectById(state, plainId);
    if (trainee == nullptr || armory == nullptr
        || plain == nullptr
        || state.behaviors.contains(traineeId)
        || trainee->veterancy != 2
        || trainee->veterancyExperience != 0
        || armory->currentAmmo != 1
        || runtimeCellDistanceToObjectFootprint(
            state,
            runtimeCellPointForObject(*trainee),
            *armory
        ) <= 0.001) {
        return fail("Armory enter/Elite/exit lifecycle diverged");
    }

    // Exhaustion is visible as NoEnter and consumes no command authority.
    wounded->health = 30;
    hospital->currentAmmo = 0;
    state.selectedObjectIds = {woundedId};
    if (runtimeTargetCursorIntent(state, *hospital)
            != RuntimeTargetCursorIntent::NoEnter
        || !queueRuntimeTargetAbility(state, *hospital)
        || state.behaviors.contains(woundedId)
        || wounded->health != 30 || hospital->currentAmmo != 0) {
        return fail("exhausted Hospital did not block without side effects");
    }

    // An ordinary Ammo-authored structure remains outside this system.
    const WorldObjectInstance plainBefore = *plain;
    if (runtimeTargetCursorIntent(state, *plain)
            != RuntimeTargetCursorIntent::NoAction
        || queueRuntimeTargetAbility(state, *plain)
        || plain->currentAmmo != plainBefore.currentAmmo
        || wounded->health != 30) {
        return fail("unset Hospital/Armory fields changed ordinary behavior");
    }

    // Restore a finite value and verify both UI consumers use service Ammo.
    hospital->currentAmmo = 1;
    state.selectedObjectIds = {hospitalId};
    const RuntimeUiStateSnapshot ui = materializeRuntimeUiStateSnapshot(state);
    const RuntimeCargoPipDisplay pips = runtimeCargoPipDisplayForObject(
        state,
        *hospital
    );
    if (toLowerAsciiLocal(ui.selectedPipScale) != "ammo"
        || ui.selectedAmmoCapacity != 2
        || ui.selectedAmmoCurrent != 1
        || ui.selectedPipTotal != 2
        || ui.selectedPipFilled != 1
        || !pips.available || pips.totalUnits != 2
        || pips.filledUnits != 1) {
        return fail("service Ammo UI pips diverged from authority");
    }

    const std::filesystem::path completedSavePath =
        "runtime_hospital_armory_mod_compat_smoke.ini";
    const std::uint64_t savedWorldHash =
        state.world.deterministicSimulationStateHash();
    const int savedWoundedHealth = wounded->health;
    const int savedTraineeVeterancy = trainee->veterancy;
    const int savedTraineeExperience = trainee->veterancyExperience;
    const int savedHospitalAmmo = hospital->currentAmmo;
    const int savedArmoryAmmo = armory->currentAmmo;
    if (!saveRuntimeState(
            state,
            pathString(completedSavePath),
            errorMessage
        )) {
        return fail("completed service save failed: " + errorMessage);
    }
    wounded->health = 1;
    trainee->veterancy = 0;
    trainee->veterancyExperience = 99;
    hospital->currentAmmo = 0;
    armory->currentAmmo = 0;
    if (!loadRuntimeState(
            state,
            pathString(completedSavePath),
            errorMessage
        )) {
        return fail("completed service load failed: " + errorMessage);
    }
    wounded = findMutableObjectById(state, woundedId);
    trainee = findMutableObjectById(state, traineeId);
    hospital = findMutableObjectById(state, hospitalId);
    armory = findMutableObjectById(state, armoryId);
    if (wounded == nullptr || trainee == nullptr || hospital == nullptr
        || armory == nullptr
        || wounded->health != savedWoundedHealth
        || trainee->veterancy != savedTraineeVeterancy
        || trainee->veterancyExperience != savedTraineeExperience
        || hospital->currentAmmo != savedHospitalAmmo
        || armory->currentAmmo != savedArmoryAmmo
        || state.world.deterministicSimulationStateHash() != savedWorldHash) {
        return fail("completed service statistics did not round-trip exactly");
    }

    std::cout << "Hospital/Armory runtime integration smoke: "
                 "cursor=yes queue=yes heal=full elite=yes exit=yes "
                 "pendingSave=yes completedSave=yes UI=yes hash=yes\n";
    return 0;
}
