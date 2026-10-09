bool runRuntimeStockEngineerCursorSmoke(RuntimeWindowState& stock,std::string& error) {
    auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
    s.config=stock.config;s.config.playerSlots.clear();s.config.multiplayerActive=false;
    s.rules=stock.rules;s.rulesLoaded=true;s.art=stock.art;s.artLoaded=true;
    s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
    s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
    WorldObjectInstance engineer;engineer.instanceId=10;engineer.kind=WorldObjectKind::Infantry;
    engineer.owner=s.economy.owner;engineer.alive=true;engineer.health=75;engineer.mission="Guard";
    engineer.x=2;engineer.y=8;engineer.cell=runtimeCellKey(2,8);
    WorldObjectInstance target;target.instanceId=20;target.kind=WorldObjectKind::Structure;
    target.alive=true;target.health=1;target.mission="Guard";target.x=8;target.y=8;target.cell=runtimeCellKey(8,8);
    s.world.accessObjects()={engineer,target};s.selectedObjectIds={10};
    for(const char* type:{"ENGINEER","SENGINEER","YENGINEER"})for(bool neutral:{false,true}) {
        auto* actor=findMutableObjectById(s,10);auto* building=findMutableObjectById(s,20);
        if(!actor||!building){error="stock engineer cursor fixture missing";return false;}
        actor->type=type;building->type=neutral?"CAPRS03":"GAPOWR";
        building->owner=neutral?"Neutral":s.economy.owner;
        building->health=runtimeMaxHealthForObject(s,*building);
        if(runtimeTargetCursorIntent(s,*building)!=RuntimeTargetCursorIntent::NoEnter) {
            error=std::string(type)+" full-health native building has attack cursor";return false;
        }
        (void)dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            runtimeCellPointForObject(*building),building);
        if(!s.behaviors.empty()||!s.pendingEngineerCaptures.empty()||!s.pendingOccupyTargets.empty()) {
            error=std::string(type)+" forbidden native building click queued an action";return false;
        }
    }
    std::cout<<"Engineer stock cursor: 6 three-faction/full-health building cases, NoEnter and actual UI click rejection passed\n";
    return true;
}

bool runRuntimeEngineerEntrySmokeProbe(std::string& errorMessage) {
    IniFile rulesIni;
    IniFile artIni;
    if (!rulesIni.loadFromString(
            "[InfantryTypes]\n0=ENGINEER\n"
            "[BuildingTypes]\n0=TECH\n1=GARRISON\n2=DEFENSE\n"
            "3=LOCKED\n4=HYBRID\n"
            "[ENGINEER]\nStrength=100\nSpeed=6\nEngineer=yes\nPrimary=DefuseKit\nSecondary=VirtualScanner\n"
            "[DefuseKit]\nDamage=1\nROF=20\nRange=1.5\nProjectile=InvisibleAll\nSpeed=100\nWarhead=BombDisarm\nFireOnce=yes\n"
            "[VirtualScanner]\nDamage=1\nRange=5\nNeverUse=yes\nProjectile=InvisibleAll\nWarhead=SA\nSpeed=100\n"
            "[InvisibleAll]\nInviso=yes\nImage=none\nAG=yes\nAA=yes\n"
            "[BombDisarm]\nBombDisarm=yes\n"
            "[TECH]\nStrength=1000\nNeedsEngineer=yes\nCapturable=yes\n"
            "[GARRISON]\nStrength=1000\nCanBeOccupied=yes\n"
            "MaxNumberOccupants=10\nClickRepairable=no\n"
            "[DEFENSE]\nStrength=1000\nIsBaseDefense=yes\nCapturable=yes\n"
            "[LOCKED]\nStrength=1000\nCanBeOccupied=yes\n"
            "Repairable=no\nClickRepairable=no\n"
            "[HYBRID]\nStrength=1000\nCanBeOccupied=yes\n"
            "MaxNumberOccupants=10\nNeedsEngineer=yes\nCapturable=yes\n"
        )
        || !artIni.loadFromString(
            "[TECH]\nFoundation=2x2\nRemapable=no\n"
            "[GARRISON]\nFoundation=2x2\nRemapable=no\n"
            "[DEFENSE]\nFoundation=2x2\nRemapable=no\n"
            "[LOCKED]\nFoundation=2x2\n"
            "[HYBRID]\nFoundation=2x2\nRemapable=no\n"
        )) {
        errorMessage = "could not parse engineer fixtures";
        return false;
    }

    const auto makeState = [&](
        const std::string& type, const std::string& targetOwner,
        int health, const std::string& engineerOwner = "PlayerA"
    ) {
        auto storage = std::make_unique<RuntimeWindowState>();
        RuntimeWindowState& state = *storage;
        state.rulesLoaded = state.rules.loadFromIni(rulesIni);
        state.artLoaded = state.art.loadFromIni(artIni);
        state.config.fixedTickHz = 30;
        state.config.defaultMapPath = "engineer-entry-smoke";
        state.economy.owner = "PlayerA";
        state.economy.credits = 1000;
        state.economy.incomeCooldownTicks = 1000000;
        state.economy.skirmishStartApplied = true;
        state.triggers.bootstrapComplete = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.neutralRemapOwners.insert("mapcivilians");
        state.structureFootprints[type] = {2, 2, true};
        for (int y = 0; y < 14; ++y) {
            for (int x = 0; x < 18; ++x) {
                TerrainCell cell;
                cell.x = x;
                cell.y = y;
                cell.cellIndex512 = y * 512 + x;
                cell.tileIndex = 0;
                state.world.accessTerrainCells().push_back(cell);
                state.terrainInfoByCell.emplace(
                    runtimeCellKey(x, y), RuntimeTerrainInfo{}
                );
            }
        }
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        WorldObjectInstance target;
        target.instanceId = 20;
        target.kind = WorldObjectKind::Structure;
        target.type = type;
        target.owner = targetOwner;
        target.health = health;
        target.x = 8;
        target.y = 6;
        target.cell = runtimeCellKey(8, 6);
        target.mission = "Guard";
        state.world.accessObjects().push_back(target);
        WorldObjectInstance engineer;
        engineer.instanceId = 10;
        engineer.kind = WorldObjectKind::Infantry;
        engineer.type = "ENGINEER";
        engineer.owner = engineerOwner;
        engineer.health = 100;
        engineer.x = 2;
        engineer.y = 6;
        engineer.cell = runtimeCellKey(2, 6);
        engineer.mission = "Guard";
        state.world.accessObjects().push_back(engineer);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        state.selectedObjectIds = {10};
        return storage;
    };

    struct EntryCase {
        const char* type;
        const char* owner;
        int health;
        RuntimeEngineerAction action;
        const char* engineerOwner = "PlayerA";
    };
    const EntryCase cases[] = {
        {"TECH", "Neutral", 1000, RuntimeEngineerAction::Capture},
        {"TECH", "Neutral", 200, RuntimeEngineerAction::Capture},
        {"TECH", "PlayerB", 1000, RuntimeEngineerAction::Capture},
        {"TECH", "PlayerA", 1000, RuntimeEngineerAction::None},
        {"TECH", "PlayerA", 200, RuntimeEngineerAction::Repair},
        {"GARRISON", "Neutral", 1000, RuntimeEngineerAction::None},
        {"GARRISON", "Neutral", 200, RuntimeEngineerAction::Repair},
        {"GARRISON", "MapCivilians", 200, RuntimeEngineerAction::Repair},
        {"GARRISON", "PlayerA", 1000, RuntimeEngineerAction::None},
        {"GARRISON", "PlayerA", 200, RuntimeEngineerAction::Repair},
        {"GARRISON", "PlayerB", 200, RuntimeEngineerAction::None},
        {"DEFENSE", "Neutral", 1000, RuntimeEngineerAction::None},
        {"DEFENSE", "Neutral", 200, RuntimeEngineerAction::Repair},
        {"DEFENSE", "PlayerA", 1000, RuntimeEngineerAction::None},
        {"DEFENSE", "PlayerA", 200, RuntimeEngineerAction::Repair},
        {"DEFENSE", "PlayerB", 200, RuntimeEngineerAction::None},
        {"LOCKED", "Neutral", 200, RuntimeEngineerAction::None},
        {"HYBRID", "Neutral", 1000, RuntimeEngineerAction::Capture},
        {"TECH", "Neutral", 1000, RuntimeEngineerAction::Capture, "PlayerB"},
        {"DEFENSE", "PlayerB", 200, RuntimeEngineerAction::Repair, "PlayerB"}
    };
    int physicalEntryStarts=0;
    for (const EntryCase& entry : cases) {
        auto storage = makeState(
            entry.type, entry.owner, entry.health, entry.engineerOwner
        );
        RuntimeWindowState& state = *storage;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        const auto fail = [&](const std::string& message) {
            errorMessage = std::string(entry.type) + "/" + entry.owner
                + "/" + std::to_string(entry.health) + ": " + message;
            return false;
        };
        const WorldObjectInstance* target = findObjectById(state, 20);
        const WorldObjectInstance* engineer = findObjectById(state, 10);
        if (!state.rulesLoaded || !state.artLoaded || target == nullptr
            || engineer == nullptr
            || runtimeEngineerActionForStructure(state, *engineer, *target)
                != entry.action) {
            return fail("wrong engineer action");
        }
        const RuntimeTargetCursorIntent expectedCursor = entry.action
                == RuntimeEngineerAction::Capture
            ? RuntimeTargetCursorIntent::Capture
            : entry.action == RuntimeEngineerAction::Repair
                ? RuntimeTargetCursorIntent::EngineerRepair
                : RuntimeTargetCursorIntent::NoEnter;
        if (runtimeTargetCursorIntent(state, *target) != expectedCursor) {
            return fail("cursor disagrees with engineer action");
        }
        const bool queued = queueRuntimeTargetAbility(state, *target);
        if (entry.action == RuntimeEngineerAction::None) {
            if (queued || !state.behaviors.empty()
                || !state.pendingEngineerCaptures.empty()
                || !state.pendingOccupyTargets.empty()) {
                return fail("unavailable entry queued movement or occupation");
            }
            issueRuntimeAttackCommand(state,*target);
            if(state.lastCommand.acceptedCount!=0||!state.behaviors.empty())
                return fail("unavailable entry fell through to a DefuseKit attack");
            continue;
        }
        if (!queued || !state.pendingEngineerActions.contains(10)
            || state.pendingEngineerActions.at(10) != entry.action) {
            return fail("entry did not queue its requested action");
        }
        if (completeRuntimeEngineerCapture(state, 10)) {
            return fail("engineer was consumed before entering");
        }
        bool sawPhysicalEntryStart=false;
        for (int step = 0; step < 360 && findObjectById(state, 10); ++step) {
            const auto* before=findObjectById(state,10);
            const auto beforeSlot=worldObjectSubCellOffset(*before);
            const bool outside=runtimeCellDistanceToObjectFootprint(state,
                runtimeCellPointForObject(*before),*findObjectById(state,20))>0.001;
            std::array<double,2> expectedStart{runtimeObjectRenderX(*before)+beforeSlot.x,
                runtimeObjectRenderY(*before)+beforeSlot.y};
            const auto prior=state.behaviors.find(10);
            if(before->renderMoveActive && prior!=state.behaviors.end() && prior->second.moveCooldownTicks>0)
                expectedStart={prior->second.renderTo.x+prior->second.renderToOffsetX+beforeSlot.x,
                    prior->second.renderTo.y+prior->second.renderToOffsetY+beforeSlot.y};
            ++state.tick;
            advanceRuntimeSimulation(state);
            const auto* after=findObjectById(state,10);
            if(!after) continue;
            const auto afterSlot=worldObjectSubCellOffset(*after);
            const double bodyX=runtimeObjectRenderX(*after)+afterSlot.x;
            const double bodyY=runtimeObjectRenderY(*after)+afterSlot.y;
            if(outside && runtimeCellDistanceToObjectFootprint(state,runtimeCellPointForObject(*after),
                    *findObjectById(state,20))<=0.001) {
                const auto entering=state.behaviors.find(10);
                if(after->subCell!=-1 || !after->renderMoveActive || entering==state.behaviors.end()
                    || entering->second.kind!=RuntimeBehaviorKind::Move
                    || entering->second.pendingAllowedOccupiedObjectId!=20
                    || entering->second.renderMoveTicksRemaining!=entering->second.renderMoveTicksTotal
                    || std::abs(bodyX-expectedStart[0])>1e-9 || std::abs(bodyY-expectedStart[1])>1e-9) {
                    std::ostringstream details;details<<std::setprecision(17)
                        <<"authorized slotless entry changed the physical source body at tick="<<state.tick
                        <<" expected="<<expectedStart[0]<<','<<expectedStart[1]
                        <<" actual="<<bodyX<<','<<bodyY<<" slot="<<after->subCell;
                    return fail(details.str());
                }
                sawPhysicalEntryStart=true;++physicalEntryStarts;
            }
            // Service permission never makes another ground body's space free.
            for(const auto& other:state.world.getObjects()) {
                if(!other.alive || other.airborne || other.instanceId==10
                    || (other.kind!=WorldObjectKind::Infantry && other.kind!=WorldObjectKind::Unit)
                    || (after->hasRenderPosition?after->renderOnBridge:after->onBridge)
                        !=(other.hasRenderPosition?other.renderOnBridge:other.onBridge)) continue;
                const auto otherSlot=worldObjectSubCellOffset(other);
                const double minimum=other.kind==WorldObjectKind::Infantry?0.5:0.75;
                if(std::hypot(bodyX-runtimeObjectRenderX(other)-otherSlot.x,
                        bodyY-runtimeObjectRenderY(other)-otherSlot.y)+1e-9<minimum)
                    return fail("authorized entry overlapped another actual ground body #"+std::to_string(other.instanceId));
            }
        }
        target = findObjectById(state, 20);
        engineer = findRuntimeObjectByIdIncludingInactive(state, 10);
        const std::string expectedOwner = entry.action
                == RuntimeEngineerAction::Capture
            ? entry.engineerOwner : entry.owner;
        if (!sawPhysicalEntryStart || target == nullptr || target->owner != expectedOwner
            || target->health != 1000 || engineer == nullptr || engineer->alive
            || !state.deathWeapons.hasTriggered(10)
            || state.behaviors.contains(10)
            || state.pendingEngineerCaptures.contains(10)
            || state.pendingEngineerActions.contains(10)) {
            std::ostringstream details;details<<"entry did not complete with correct health/owner/consumption: "
                <<state.interactionStatus<<"; "<<state.simulationStatus<<" physicalEntry="<<sawPhysicalEntryStart;
            if(engineer) details<<" alive="<<engineer->alive<<" cell="<<engineer->x<<','<<engineer->y
                <<" slot="<<engineer->subCell<<" body="<<runtimeObjectRenderX(*engineer)+worldObjectSubCellOffset(*engineer).x
                <<','<<runtimeObjectRenderY(*engineer)+worldObjectSubCellOffset(*engineer).y;
            const auto waiting=state.behaviors.find(10);
            if(waiting!=state.behaviors.end()) {
                const auto& move=waiting->second;details<<" state="<<static_cast<int>(move.state)
                    <<" goal="<<move.moveTarget.x<<','<<move.moveTarget.y
                    <<" allowed="<<move.pendingAllowedOccupiedObjectId<<" blocked="<<move.blockedTicks
                    <<" pending="<<move.pathRequestPending<<" cooldown="<<move.moveCooldownTicks
                    <<" ticks="<<move.renderMoveTicksRemaining<<'/'<<move.renderMoveTicksTotal<<" path="<<move.path.size();
                if(engineer && !move.path.empty()) details<<" front="<<move.path.front().x<<','<<move.path.front().y
                    <<" bodyBlocked="<<runtimeInfantryBodySegmentAdmissionBlocked(state,*engineer,
                        runtimeCellPointForObject(*engineer),move.path.front());
            }
            return fail(details.str());
        }
    }

    // A valid repair order becomes invalid while the engineer is approaching.
    // Cancel before the next step into the structure, preserving the engineer.
    auto lateStorage = makeState("GARRISON", "Neutral", 200);
    RuntimeWindowState& late = *lateStorage;
    RuntimeSnapshotActiveStateScope snapshotScope(late);
    if (!queueRuntimeTargetAbility(late, *findObjectById(late, 20))) {
        errorMessage = "could not queue late full-health repair probe";
        return false;
    }
    for (int step = 0; step < 3; ++step) {
        ++late.tick;
        advanceRuntimeSimulation(late);
    }
    findMutableObjectById(late, 20)->health = 1000;
    for (int step = 0; step < 20; ++step) {
        ++late.tick;
        advanceRuntimeSimulation(late);
    }
    const WorldObjectInstance* survivor = findObjectById(late, 10);
    const WorldObjectInstance* repaired = findObjectById(late, 20);
    if (survivor == nullptr || repaired == nullptr
        || runtimeCellDistanceToObjectFootprint(
            late, runtimeCellPointForObject(*survivor), *repaired
        ) <= 0.0
        || repaired->owner != "Neutral" || repaired->health != 1000
        || late.behaviors.contains(10)
        || late.pendingEngineerCaptures.contains(10)
        || late.pendingEngineerActions.contains(10)
        || late.deathWeapons.hasTriggered(10)) {
        errorMessage = "repair restored to full health still entered or consumed engineer";
        return false;
    }
    if (!runRuntimeOwnerRemapPolicyContractCheck(errorMessage)) {
        return false;
    }
    std::cout << "Engineer entry: 20 ownership/health cases passed; "
        "full-health approach cancelled; capture remap pixels verified; "
        << physicalEntryStarts << " real slotless body-continuity entries passed\n";
    return true;
}
