    if (std::getenv("CONCORD_COMMAND_SMOKE_SUPPORT_RECHARGE_ONLY") != nullptr) {
        const auto captureRecharge = [&](RuntimeWindowState& state, const std::string& id, const std::string& stage) {
            if (root.empty() || (id != "NukeSpecial" && id != "IronCurtainSpecial"
                    && id != "AmericanParaDropSpecial" && id != "LightningStormSpecial")) return true;
            constexpr int width=800, height=600;
            state.activeBuildTab=RuntimeBuildKind::Defense;
            const RECT sidebar=runtimeSidebarRect({0,0,width,height});
            for (int page=0;page<runtimeBuildPageCount(state,sidebar);++page) {
                runtimeSetActiveBuildPage(state,page);
                const auto entries=runtimeVisibleSuperWeaponsOnBuildPage(state,sidebar);
                if (std::any_of(entries.begin(),entries.end(),[&](const auto* entry) {
                        return entry->superWeaponId==id;
                    })) break;
            }
            std::vector<std::uint32_t> pixels;
            if (!drawRuntimeStateOffscreenAtCell(state,{90,90},width,height,error,"",false,true,-1,&pixels,true)) return false;
            RuntimeBackBuffer buffer;
            HDC screen=GetDC(nullptr);
            const bool created=ensureRuntimeBackBuffer(buffer,screen,width,height);
            if(screen) ReleaseDC(nullptr,screen);
            if(!created) return false;
            std::copy(pixels.begin(),pixels.end(),static_cast<std::uint32_t*>(buffer.pixels));
            SetBkMode(buffer.memoryDc,TRANSPARENT);
            drawRuntimeSidebar(buffer.memoryDc,{0,0,width,height},state);
            drawRuntimeSuperWeaponTimerList(buffer.memoryDc,{0,0,sidebar.left,height},state);
            GdiFlush();
            const bool written=writeRuntimeSmokeBmp(root+"/"+id+"-"+stage+".bmp",buffer.pixels,width,height,error);
            releaseRuntimeBackBuffer(buffer); return written;
        };
        for(int hz : {30,60}) for(const auto& id : expected) {
            auto storage=fixture("Americans",hz); auto& state=*storage;
            const int tank=addUnit(state,"MTNK",WorldObjectKind::Unit,90,90,"Commander");
            const int building=addUnit(state,"GAPILE",WorldObjectKind::Structure,87,87,"Commander");
            SuperWeaponTarget target{{90,90},-1};
            std::optional<SuperWeaponTarget> second;
            if(id=="ForceShieldSpecial") target={{87,87},building};
            if(id=="IronCurtainSpecial" || id=="ChronoSphereSpecial") target.objectId=tank;
            if(id=="ChronoSphereSpecial") second=SuperWeaponTarget{{98,98},-1};
            if(!fire(state,id,target,second)) return fail("recharge launch "+id+": "+error);
            const auto* snapshot=state.superWeapons.findSidebarSnapshot("Commander",id);
            const int recharge=snapshot->rechargeTicks;
            if(snapshot->remainingTicks!=recharge || snapshot->progress!=0.0
                || runtimeSuperWeaponHasReadyLabel(*snapshot)) return fail("immediate recharge "+id);
            if(hz==30 && !captureRecharge(state,id,"accepted")) return fail(error);
            step(state,1);
            snapshot=state.superWeapons.findSidebarSnapshot("Commander",id);
            if(snapshot->remainingTicks!=recharge-1) return fail("first recharge tick "+id);
            step(state,hz*2-1);
            snapshot=state.superWeapons.findSidebarSnapshot("Commander",id);
            if(id=="ForceShieldSpecial") {
                if(!snapshot->chargePaused || snapshot->remainingTicks!=recharge-1)
                    return fail("shield blackout recharge suspension");
            } else if(snapshot->remainingTicks!=recharge-hz*2) return fail("effect held recharge "+id);
            if(hz==30 && !captureRecharge(state,id,"two-seconds")) return fail(error);
            std::ostringstream saved;
            if(!writeRuntimeStateToStream(state,saved,error)) return fail("recharge save "+id+": "+error);
            IniFile ini;
            auto loaded=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded,state,false);
            if(!ini.loadFromString(saved.str()) || !materializeRuntimeLoadStateFromIni(*loaded,ini,error))
                return fail("recharge reload "+id+": "+error);
            const auto* restored=loaded->superWeapons.findSidebarSnapshot("Commander",id);
            if(!restored || restored->remainingTicks!=snapshot->remainingTicks
                || restored->chargePaused!=snapshot->chargePaused
                || loaded->superWeapons.stateHash()!=state.superWeapons.stateHash())
                return fail("recharge resume state "+id);
            const int restoredRemaining=restored->remainingTicks;
            step(state,1); step(*loaded,1);
            if(loaded->superWeapons.stateHash()!=state.superWeapons.stateHash())
                return fail("recharge continuation "+id);
            std::cout<<"Support recharge: "<<id<<" hz="<<hz<<" at-use="<<recharge
                <<" after-2s="<<restoredRemaining<<" save/resume=ok\n";
        }
        return true;
    }
