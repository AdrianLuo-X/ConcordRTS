    if (std::getenv("CONCORD_COMMAND_SMOKE_FORCE_SHIELD_ONLY") != nullptr) {
        for (const int hz : {15,30,60}) {
            auto storage=fixture("Americans",hz); auto& s=*storage;
            s.config.noCooldown=false;
            // The shared rendering fixture has no command authority registry.
            // Register its local facade before testing the gameplay dispatcher.
            RuntimeHumanHouseAuthority authority;
            authority.owner="Commander"; authority.canonicalOwner="commander";
            authority.country="Americans"; authority.localFacade=true;
            s.humanHouseAuthorities.push_back(std::move(authority));
            auto context=runtimeSuperWeaponContext(s);
            if (!s.superWeapons.forceReady("Commander","ForceShieldSpecial",&context))
                return fail("empty shield ready");
            RuntimeMultiplayerGameplayFireSuperWeaponCommand command;
            command.superWeaponId="ForceShieldSpecial";
            command.targets.push_back({{100,102},std::nullopt});
            if (!runtimeGameplaySuperWeaponReadyForCommand(s,"Commander",command)
                || !applyRuntimeGameplaySuperWeaponCommand(s,"Commander",command))
                return fail("empty shield gameplay command");
            step(s,1);
            if (!s.superWeapons.isOwnerBlackout("Commander",s.tick)
                || runtimeActiveLocalSuperWeaponTargeting(s)!=nullptr)
                return fail("empty shield did not finish selection/start blackout");
            for (const auto& object:s.world.getObjects())
                if (s.superWeapons.isObjectProtected(object.instanceId,s.tick))
                    return fail("empty shield protected a remote source building");
            const auto* snapshot=s.superWeapons.findSidebarSnapshot("Commander","ForceShieldSpecial");
            if (!snapshot || snapshot->firstTargetCell!=SuperWeaponCell{100,102}
                || snapshot->remainingTicks<=0) return fail("empty shield charge/center");
            std::ostringstream saved;
            if (!writeRuntimeStateToStream(s,saved,error)) return fail(error);
            IniFile ini; auto loaded=std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*loaded,s,false);
            if (!ini.loadFromString(saved.str()) || !materializeRuntimeLoadStateFromIni(*loaded,ini,error)
                || loaded->superWeapons.stateHash()!=s.superWeapons.stateHash())
                return fail("empty shield full save: "+error);
            step(s,1); step(*loaded,1);
            if (loaded->superWeapons.stateHash()!=s.superWeapons.stateHash())
                return fail("empty shield save continuation");
            std::cout << "Force shield empty ground: hz=" << hz
                << " gameplay-command=accepted protected=0 charge=consumed blackout=yes full-save=ok\n";
        }
        for (const std::string type : {"GAWEAP","NAWEAP","YAWEAP"}) {
            auto storage = fixture(); auto& s = *storage;
            s.config.noCooldown=false;
            const int building = addUnit(s,type,WorldObjectKind::Structure,90,90,"Commander");
            const int tank = addUnit(s,"MTNK",WorldObjectKind::Unit,90,94,"Commander");
            const int enemy = addUnit(s,"NAHAND",WorldObjectKind::Structure,88,92,"Enemy");
            initializeRuntimeNavigation(s);
            const RECT clientRect{0,0,960,660};
            const RECT viewport=runtimeMapViewport(clientRect);
            centerRuntimeCameraOnCell(s,clientRect,{90,90});
            std::optional<POINT> groundPoint;
            SuperWeaponCell groundCell;
            for (int y=86;y<=94 && !groundPoint;++y) for (int x=86;x<=94 && !groundPoint;++x) {
                if ((x==90 && y==90) || std::abs(x-90)+std::abs(y-90)>4) continue;
                const auto projected=runtimeProjectCellXY(x,y,0,s.camera);
                const POINT point{viewport.left+projected.x-s.camera.worldOffsetX,
                    viewport.top+projected.y-s.camera.worldOffsetY};
                const POINT actual=approximateCellFromScreenPoint(s,viewport,point);
                if (pointInRectInclusive(viewport,point) && actual.x==x && actual.y==y
                    && runtimeCursorObjectAtPoint(s,viewport,point)==nullptr) {
                    groundPoint=point; groundCell={x,y};
                }
            }
            if (!groundPoint) return fail(type+" has no test ground pixel");
            auto context=runtimeSuperWeaponContext(s);
            if (!s.superWeapons.forceReady("Commander","ForceShieldSpecial",&context)
                || !s.superWeapons.beginTargetSelection("Commander","ForceShieldSpecial",context,&error))
                return fail("shield selection: "+error);
            s.mouseClientPointValid=true; s.mouseClientPoint=*groundPoint;
            const auto cursor=runtimeMouseCursorFrameForPoint(s,clientRect,*groundPoint);
            if (!cursor || cursor->frameIndex<450 || cursor->frameIndex>459 || cursor->atlasId!="mouse.sha")
                return fail(type+" ground cursor rejected");
            if (!submitRuntimeSuperWeaponTargetAtPoint(s,viewport,*groundPoint)
                || s.superWeapons.stateOf("Commander","ForceShieldSpecial")!=SuperWeaponRuntimeState::Launching
                || s.superWeapons.findSidebarSnapshot("Commander","ForceShieldSpecial")->firstTargetCell!=groundCell)
                return fail(type+" ground click rejected or moved center");
            step(s,60);
            if (!s.superWeapons.isObjectProtected(building,s.tick)
                || s.superWeapons.isObjectProtected(tank,s.tick)
                || s.superWeapons.isObjectProtected(enemy,s.tick)) return fail("shield target classification");
            // Compare the persistent body colour after the one-shot invocation.
            s.impactEffects.clear();
            const auto image = [&](const std::string& stage, std::vector<std::uint32_t>& pixels) {
                return drawRuntimeStateOffscreenAtCell(s,{91,91},800,600,error,
                    root.empty()?"":root+"/"+type+"-"+stage+".bmp",false,false,-1,&pixels).has_value();
            };
            std::vector<std::uint32_t> protectedPixels, normalPixels, restoredPixels, expiredPixels;
            if (!image("protected",protectedPixels)) return fail(error);
            std::ostringstream saved;
            s.superWeapons.save(saved);
            IniFile ini;
            if (!ini.loadFromString(saved.str())) return fail("shield save parse");
            s.superWeapons.reset(s.rules);
            if (!image("normal",normalPixels)) return fail(error);
            if (!s.superWeapons.load(ini,&error) || !s.superWeapons.isObjectProtected(building,s.tick))
                return fail("shield save restore: "+error);
            if (!image("restored",restoredPixels) || restoredPixels != protectedPixels)
                return fail("shield colour changed after save restore");
            const auto blue = [](std::uint32_t p) {
                const int b=p&255U, g=(p>>8U)&255U, r=(p>>16U)&255U;
                return b>r+24 && b>g+24;
            };
            int differences=0, blueDifferences=0;
            for (std::size_t i=0;i<protectedPixels.size();++i) if (protectedPixels[i]!=normalPixels[i]) {
                ++differences;
                if (blue(protectedPixels[i])) ++blueDifferences;
            }
            if (differences<1000 || blueDifferences*100<differences*98)
                return fail(type+" did not receive blue body shading: "+std::to_string(blueDifferences)
                    +"/"+std::to_string(differences));
            // A body-only mask catches accidental tinting of the rectangle,
            // terrain, neighbouring enemy building or friendly vehicle.
            findMutableObjectById(s,building)->alive=false;
            s.superWeapons.reset(s.rules);
            std::vector<std::uint32_t> withoutBuilding;
            if (!image("mask",withoutBuilding)) return fail(error);
            for (std::size_t i=0;i<protectedPixels.size();++i)
                if (protectedPixels[i]!=normalPixels[i] && normalPixels[i]==withoutBuilding[i])
                    return fail("shield tinted a pixel outside its building");
            auto* hiddenBuilding = findMutableRuntimeObjectByIdIncludingInactive(s,building);
            if (!hiddenBuilding) return fail("missing building after mask comparison");
            hiddenBuilding->alive=true;
            if (!s.superWeapons.load(ini,&error)) return fail(error);
            step(s,1200);
            if (s.superWeapons.isObjectProtected(building,s.tick)) return fail("shield did not expire");
            if (!image("expired",expiredPixels)) return fail(error);
            int blueAfter=0;
            for (std::size_t i=0;i<protectedPixels.size();++i)
                if (protectedPixels[i]!=normalPixels[i] && blue(expiredPixels[i])) ++blueAfter;
            if (blueAfter*4>=blueDifferences) return fail("blue body persisted after protection ended");
            std::cout << "Force shield " << type << " nativeBody=blue pixels=" << blueDifferences
                << " ground/enemy/vehicle=unchanged save=identical expiry=normal"
                << " groundCursor/click=accepted cell=" << groundCell.x << "," << groundCell.y << "\n";
        }
        return true;
    }
