bool runRuntimeDefenseFacingSmoke(RuntimeWindowState& live, std::string& error) {
    std::string scene;
    const auto fail = [&](const std::string& why) { error = "Defense facing: " + scene + ": " + why; return false; };
    int cases = 0, saved = 0;
    for (const char* type : {"GTGCAN", "NALASR", "NAFLAK", "NASAM", "YAGGUN"})
        for (int hz : {15, 30, 60}) for (int mode = 0; mode != 6; ++mode) {
            scene = std::string(type) + " hz=" + std::to_string(hz) + " mode=" + std::to_string(mode);
            auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
            seedRuntimeLoadScratch(s, live, false);
            s.config.fixedTickHz = hz; s.config.noCooldown = false;
            s.config.computerAiEnabled = false; s.visionEnabled = false;
            s.runtimeSuppressPlayerAudioFeedback = true; s.runtimeSoundsEnabled = false;
            s.economy.owner = "Americans";
            s.behaviors.clear(); s.projectiles.clear(); s.impactEffects.clear();
            s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
            s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
            for (int y = 45; y <= 83; ++y) for (int x = 45; x <= 83; ++x) {
                TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y); cell.tileIndex=0;
                s.world.accessTerrainCells().push_back(cell);
            }
            initializeRuntimeNavigation(s);
            for (int n = 0; n != 3; ++n)
                addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "GAPOWR", {48 + n*4, 48});
            auto* source = addRuntimeObjectInstance(s, RuntimeBuildKind::Defense, type, {64,64});
            if (!source) return fail(std::string(type) + " missing");
            const int id = source->instanceId;
            const bool antiAir = std::string(type) == "NAFLAK" || std::string(type) == "NASAM";
            auto* target = addRuntimeObjectInstanceForOwner(s, "Russians", RuntimeBuildKind::Unit,
                antiAir ? "DISK" : "MTNK", {68,61});
            if (!target) return fail("target missing");
            const int targetId = target->instanceId;
            for (auto& object : s.world.accessObjects()) {
                object.buildAnimTicksRemaining=object.buildAnimTicksTotal=0; object.mission="Guard";
            }
            target->health = 100000;
            if (antiAir) { target->airborne=true; target->flightLevel=target->level=6; clearRuntimeRenderPosition(*target); }
            rebuildRuntimeEconomyPower(s);
            source = findMutableObjectById(s,id);
            source->facing = source->turretFacing = normalizeRuntimeFacing(runtimeFacingFromDelta(4,-3)+128);
            const auto fire = [&]() {
                applySelection(s,{id},false,false);
                issueRuntimeAttackCommand(s,*findMutableObjectById(s,targetId));
                if (s.lastCommand.acceptedCount != 1) return false;
                const auto before = s.projectiles.size();
                for (int step = 0; step != hz*30 && s.projectiles.size() == before; ++step) {
                    ++s.tick; advanceRuntimeWeaponCooldowns(s);
                    advanceRuntimeAttackBehavior(s,*findMutableObjectById(s,id),s.behaviors.at(id));
                    finishRuntimeTurretSoundActivities(s,s.world);
                }
                return s.projectiles.size() > before;
            };
            if (!fire()) return fail(std::string(type)+" failed initial attack, mode="+std::to_string(mode));
            source=findMutableObjectById(s,id); target=findMutableObjectById(s,targetId);
            const int last = source->turretFacing;
            if (last < 0 || last == source->facing) return fail("attack did not establish a distinct direction");
            if (mode == 3) issueRuntimeStopCommand(s);
            else if (mode == 4) issueRuntimeGuardCommand(s);
            else {
                if (mode == 0 || mode == 5) target->alive=false;
                if (mode == 5) s.behaviors.at(id).autoAcquired=true;
                if (mode == 1) s.behaviors.at(id).targetObjectId=876543;
                if (mode == 2) target->owner=source->owner;
                for (int step=0; step!=hz*5 && s.behaviors.at(id).state!=RuntimeBehaviorState::Complete; ++step) {
                    ++s.tick; advanceRuntimeWeaponCooldowns(s); advanceRuntimeCombatBursts(s);
                    advanceRuntimeAttackBehavior(s,*source,s.behaviors.at(id));
                    if (source->turretFacing!=last) return fail("target loss started a return turn");
                }
                if (s.behaviors.at(id).state != RuntimeBehaviorState::Complete) return fail("ended attack kept returning");
                s.behaviors.erase(id);
            }
            if (source->turretFacing != last) return fail("ending the order reset the turret immediately");
            for (int step=0; step!=hz*5; ++step) {
                ++s.tick; advanceRuntimeIdleWeaponFacings(s); finishRuntimeTurretSoundActivities(s,s.world);
                if (source->turretFacing != last || source->turretFacingClock.active()
                    || !source->activeTurretSoundEvent.empty()) return fail("idle turret returned or kept turning audio");
            }
            if (mode == 0) {
                std::ostringstream text;
                if (!writeRuntimeStateToStream(s,text,error)) return fail(error);
                IniFile ini; if (!ini.loadFromString(text.str())) return fail("save parsing");
                auto loaded=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded,s,false);
                if (!materializeRuntimeLoadStateFromIni(*loaded,ini,error)) return fail(error);
                ++loaded->tick; advanceRuntimeIdleWeaponFacings(*loaded);
                if (findObjectById(*loaded,id)->turretFacing!=last) return fail("save/load reset facing");
                ++saved;
            }
            target->alive=true; target->owner="Russians";
            setRuntimeObjectCell(s,*target,{60,67}); clearRuntimeRenderPosition(*target);
            invalidateRuntimeObjectProjectionCache(s);
            if (!fire() || findObjectById(s,id)->turretFacing==last) return fail("next target did not turn/fire");
            ++cases;
        }
    // Mobile turrets keep their existing alignment with the chassis.
    auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
    seedRuntimeLoadScratch(s,live,false); s.world.accessObjects().clear(); s.behaviors.clear();
    WorldObjectInstance tank; tank.kind=WorldObjectKind::Unit; tank.type="MTNK";
    tank.instanceId=1; tank.alive=true; tank.health=400; tank.facing=64; tank.turretFacing=192;
    s.world.accessObjects().push_back(tank);
    for (int tick=0; tick!=s.config.fixedTickHz*15 && s.world.getObjects()[0].turretFacing>=0; ++tick) {
        ++s.tick; advanceRuntimeIdleWeaponFacings(s);
    }
    if (s.world.getObjects()[0].turretFacing!=-1) return fail("mobile turret return changed");
    std::cout << "Defense facing: passed cases=" << cases << " saved=" << saved
              << " targetDeath/disappearance/invalid/Stop/Guard/autoAcquire=held nextAttack=turns mobileReturn=preserved\n";
    return true;
}
