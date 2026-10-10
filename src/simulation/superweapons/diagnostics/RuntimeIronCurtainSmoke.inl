    if (std::getenv("CONCORD_COMMAND_SMOKE_IRON_CURTAIN_ONLY")) {
        int scenes=0, saves=0, pairedTicks=0, imageChecks=0;
        for (int hz : {15,30,60}) for (int mode=0;mode<5;++mode) {
            auto box=fixture("Russians",hz); auto& s=*box;
            RuntimeSnapshotActiveStateScope active(s);
            s.config.noCooldown=false;
            RuntimeHumanHouseAuthority authority; authority.owner="Commander";
            authority.canonicalOwner="commander";authority.country="Russians";authority.localFacade=true;
            s.humanHouseAuthorities.push_back(std::move(authority));
            const bool empty=mode==0;
            const std::string type=mode==1?"MTNK":mode==2?"HTNK":mode==3?"BSUB":"GAWEAP";
            const int target=empty?-1:addUnit(s,type,mode==4?WorldObjectKind::Structure:WorldObjectKind::Unit,
                90,90,mode==2?"Enemy":"Commander");
            const int bystander=addUnit(s,"MTNK",WorldObjectKind::Unit,94,93,"Commander");
            initializeRuntimeNavigation(s);
            const RECT client{0,0,960,660};const RECT viewport=runtimeMapViewport(client);
            centerRuntimeCameraOnCell(s,client,{90,90});
            const auto projected=runtimeProjectCellXY(90,90,0,s.camera);
            POINT point{viewport.left+projected.x-s.camera.worldOffsetX,viewport.top+projected.y-s.camera.worldOffsetY};
            auto ctx=runtimeSuperWeaponContext(s);
            if(!s.superWeapons.forceReady("Commander","IronCurtainSpecial",&ctx)
                ||(empty&&!s.superWeapons.beginTargetSelection("Commander","IronCurtainSpecial",ctx,&error))) return fail(error);
            if(empty) {
                s.mouseClientPointValid=true;s.mouseClientPoint=point;
                const auto cursor=runtimeMouseCursorFrameForPoint(s,client,point);
                if(!cursor||cursor->frameIndex<346||cursor->frameIndex>350)
                    return fail("iron empty-ground cursor");
                if(!submitRuntimeSuperWeaponTargetAtPoint(s,viewport,point))return fail("iron ground click");
            } else {
                RuntimeMultiplayerGameplayFireSuperWeaponCommand command;
                command.superWeaponId="IronCurtainSpecial";command.targets.push_back({{90,90},std::nullopt});
                if(!applyRuntimeGameplaySuperWeaponCommand(s,"Commander",command))return fail("iron ground gameplay command");
            }
            step(s);
            const auto* sidebar=s.superWeapons.findSidebarSnapshot("Commander","IronCurtainSpecial");
            if(!sidebar||sidebar->firstTargetCell!=SuperWeaponCell{90,90}
                ||sidebar->remainingTicks<=0||runtimeActiveLocalSuperWeaponTargeting(s)) return fail("iron charge/targeting/center");
            if(s.superWeapons.isObjectProtected(bystander,s.tick)
                ||(!empty&&!s.superWeapons.isObjectProtected(target,s.tick)))return fail("iron protection footprint");
            const auto blasts=std::count_if(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& fx){return fx.animId=="IRONBLST";});
            if(blasts!=1)return fail("iron invocation requires exactly one IRONBLST even on empty ground: actual="+std::to_string(blasts));
            const auto& blast=*std::find_if(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& fx){return fx.animId=="IRONBLST";});
            if(blast.cell!=RuntimeCellPoint{90,90}||blast.animationSequenceSteps<=1
                ||blast.animationFrameDelayTicks!=2*hz/15)return fail("iron native invocation anchor/timeline");
            const auto image=[&](RuntimeWindowState& world,const std::string& stage,std::vector<std::uint32_t>& pixels,bool saveImage=false){
                return drawRuntimeStateOffscreenAtCell(world,{91,91},800,600,error,
                    saveImage&&!root.empty()?root+"/"+(empty?"empty":type)+"-"+stage+"-"+std::to_string(hz)+".bmp":"",
                    false,false,-1,&pixels).has_value();
            };
            std::vector<std::uint32_t> invocation;
            if(hz==30&&empty) {step(s,4);if(!image(s,"invocation",invocation,true))return fail(error);}
            // Ordinary saves preserve protection; exact multiplayer snapshots also
            // preserve transient invocation sprites that ordinary saves discard.
            const bool exact=mode==0||mode==2;
            std::ostringstream saved;if(!writeRuntimeStateToStream(s,saved,error))return fail(error);
            if(exact&&!writeRuntimeMultiplayerSnapshotExtension(s,saved,error))return fail(error);
            IniFile ini;auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
            if(!ini.loadFromString(saved.str())||!materializeRuntimeLoadStateFromIni(*loaded,ini,error,exact)
                ||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*loaded,ini,error))
                ||loaded->superWeapons.stateHash()!=s.superWeapons.stateHash())return fail("iron full restore: "+error);
            ++saves;
            for(int t=0;t<hz;++t) {
                step(s);step(*loaded);++pairedTicks;
                if(s.superWeapons.stateHash()!=loaded->superWeapons.stateHash())return fail("iron save continuation");
                auto count=[](const auto& world){return std::count_if(world.impactEffects.begin(),world.impactEffects.end(),[](const auto& fx){return fx.animId=="IRONBLST";});};
                if((exact&&count(s)!=count(*loaded))||(!exact&&count(*loaded)!=0))
                    return fail("iron animation repeated/lost after load");
            }
            if(!empty) {
                s.impactEffects.clear();loaded->impactEffects.clear();
                const auto drawAt=[&](int tick,const std::string& stage,std::vector<std::uint32_t>& pixels){
                    s.tick=tick;return image(s,stage,pixels,hz==30);
                };
                const int baseTick=s.tick;
                std::vector<std::uint32_t> dark,light,repeat,normal,restored,mask,expired;
                if(!drawAt(baseTick,"pulse-a",dark)||!image(*loaded,"full-restored",restored)
                    ||restored!=dark)return fail(type+" full save body pixels");
                if(!drawAt(baseTick+8*hz/15,"pulse-b",light)
                    ||!drawAt(baseTick+24*hz/15,"pulse-repeat",repeat))return fail(error);
                // Compare the same draw time so authored idle animations cannot masquerade as a tint.
                s.tick=baseTick;std::ostringstream protection;s.superWeapons.save(protection);IniFile protectionIni;
                if(!protectionIni.loadFromString(protection.str()))return fail("iron protection parse");
                s.superWeapons.reset(s.rules);
                if(!image(s,"normal",normal,hz==30))return fail(error);
                if(!s.superWeapons.load(protectionIni,&error)||!image(s,"restored",restored)
                    ||restored!=dark)return fail(type+" iron body restore pixels");
                int changed=0,darker=0,pulsing=0,repeatDifferences=0;
                for(std::size_t i=0;i<dark.size();++i)if(dark[i]!=normal[i]) {
                    ++changed;bool less=true;
                    for(int shift:{0,8,16})less=less&&((dark[i]>>shift)&255U)<=((normal[i]>>shift)&255U);
                    if(less)++darker;
                    if(dark[i]!=light[i])++pulsing;
                    if(dark[i]!=repeat[i])++repeatDifferences;
                }
                if(changed<100||darker*100<changed*98)return fail(type+" iron body did not darken: "+std::to_string(darker)+"/"+std::to_string(changed));
                if(pulsing<100)return fail(type+" iron body has no pulse");
                // Other source buildings keep their authored idle animations.
                // Only the protected vehicle body must repeat, not the whole scene.
                if(mode!=4&&repeatDifferences!=0)return fail(type+" native 24-frame body pulse did not repeat: "+std::to_string(repeatDifferences));
                findMutableObjectById(s,target)->alive=false;s.superWeapons.reset(s.rules);
                if(!image(s,"mask",mask))return fail(error);
                for(std::size_t i=0;i<dark.size();++i)if(dark[i]!=normal[i]&&normal[i]==mask[i])
                    return fail(type+" iron tinted outside body");
                findMutableRuntimeObjectByIdIncludingInactive(s,target)->alive=true;
                if(!s.superWeapons.load(protectionIni,&error))return fail(error);
                // Effect lifetime uses native 750 frames, independent of host simulation rate.
                s.tick=1+50*hz;auto expiredContext=runtimeSuperWeaponContext(s);s.superWeapons.tick(expiredContext);
                if(s.superWeapons.isObjectProtected(target,s.tick))return fail("iron protection expiry");
                if(!image(s,"expired",expired,hz==30))return fail(error);
                s.superWeapons.reset(s.rules);std::vector<std::uint32_t> sameTickNormal;
                if(!image(s,"expired-normal",sameTickNormal)||expired!=sameTickNormal)return fail("iron tint persists after expiry");
                imageChecks+=4;
                std::cout<<"Iron body "<<type<<" hz="<<hz<<" darkPixels="<<darker<<" pulsing/bodyMask/fullSave/expiry PASS\n";
            } else std::cout<<"Iron empty ground hz="<<hz<<" cursor/click/charge/IRONBLST/fullSave PASS\n";
            ++scenes;
        }
        std::cout<<"Iron curtain: passed scenes="<<scenes<<" fullSaves="<<saves<<" pairedTicks="<<pairedTicks
            <<" bodyChecks="<<imageChecks<<" nativeInvocation/groundCursor/pulse/mask/expiry\n";
        return true;
    }
