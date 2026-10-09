bool runRuntimeRadiationArmorSmoke(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Radiation armor: "+why;return false;};
    const auto fixture=[&](int hz) {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        seedRuntimeLoadScratch(s,live,false);s.world=live.world;
        s.config.fixedTickHz=hz;s.config.computerAiEnabled=false;s.config.multiplayerActive=false;
        s.config.noCooldown=false;s.config.playerCountry="Americans";s.config.playerSlots.clear();
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimeSoundsEnabled=false;s.visionEnabled=false;
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear();s.terrainByCell.clear();s.terrainInfoByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        for(int y=42;y<=90;++y)for(int x=42;x<=90;++x) {
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);ensureRuntimeSpecialMechanismsInitialized(s);return box;
    };
    const auto add=[&](RuntimeWindowState& s,const char* type,RuntimeCellPoint cell,const char* owner="Americans") {
        const auto kind=std::string(type)=="GAPOWR"?RuntimeBuildKind::Building:
            (std::string(type)=="E1"||std::string(type)=="DESO")?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit;
        auto* o=addRuntimeObjectInstanceForOwner(s,owner,kind,type,cell);
        if(!o)return -1;
        o->mission="Sleep";o->weaponCooldownTicksRemaining=100000;
        o->buildAnimTicksRemaining=o->buildAnimTicksTotal=0;return o->instanceId;
    };
    struct Sample { const char* type;int centerDamage; };
    // Stock RadSite: unarmored 100%, light 50%, medium/heavy 10%, buildings 0%.
    // The miners, Chaos Drone and Terror Drone retain radiation immunity.
    constexpr Sample samples[]={{"MTNK",10},{"HTNK",10},{"LTNK",10},{"TNKD",10},{"BFRT",10},
        {"APOC",10},{"TTNK",10},{"FV",50},{"V3",50},{"MGTK",50},{"SREF",50},{"DTRUCK",50},
        {"CMIN",0},{"HARV",0},{"CAOS",0},{"DRON",0},{"E1",100},{"GAPOWR",0}};
    int cases=0,boundaries=0,saves=0,continuedTicks=0,deployments=0;
    for(int hz:{15,30,60})for(const auto& sample:samples)for(int position=0;position<3;++position) {
        auto box=fixture(hz);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        const RuntimeCellPoint cell=position==0?RuntimeCellPoint{60,60}:position==1?RuntimeCellPoint{63,64}:RuntimeCellPoint{70,60};
        const int id=add(s,sample.type,cell);if(id<0)return fail("stock unit fixture");
        const int before=findObjectById(s,id)->health;
        s.specialMechanisms.spawnRadiationArea(999,"Russians",{60,60},10,500,1,"RadSite",s.tick,true,nullptr);
        advanceRuntimeSpecialMechanisms(s);
        const auto* victim=findObjectById(s,id);const int expected=position==2?0:sample.centerDamage/(position==1?2:1);
        const int actual=before-(victim?victim->health:0);
        if(actual!=expected)return fail(std::string(sample.type)+" hz="+std::to_string(hz)+" position="+std::to_string(position)
            +" expected="+std::to_string(expected)+" actual="+std::to_string(actual));
        ++cases;
    }
    for(int hz:{15,30,60}) {
        for(const char* owner:{"Americans","Russians"}) {
            auto box=fixture(hz);auto& s=*box;const int id=add(s,"MTNK",{60,60},owner);
            const int before=findObjectById(s,id)->health;
            s.specialMechanisms.spawnRadiationArea(999,"Americans",{60,60},10,500,1,"RadSite",s.tick,true,nullptr);
            advanceRuntimeSpecialMechanisms(s);
            if(findObjectById(s,id)->health!=before-10)return fail("owner filtering");++boundaries;
        }
        for(int variant=0;variant<4;++variant) {
            auto box=fixture(hz);auto& s=*box;const int id=add(s,"MTNK",{60,60});
            if(variant<2) {
                IniFile stock,overlay;
                if(!stock.loadFromFile(live.report.rulesPath))return fail("stock rules");
                overlay.loadFromString(variant==0?"[Radiation]\nRadLevelFactor=0\n":
                    "[RadSite]\nVerses=100%,100%,100%,50%,10%,0%,0%,0%,0%,100%,100%\n");
                stock.appendFrom(overlay);if(!s.rules.loadFromIni(stock))return fail("boundary rules");
                s.specialMechanisms.reset(s.rules.getSpecialMechanismRules());
            } else if(variant==2) findMutableObjectById(s,id)->veterancy=2;
            else {findMutableObjectById(s,id)->airborne=true;findMutableObjectById(s,id)->flightLevel=4;}
            const int before=findObjectById(s,id)->health;
            s.specialMechanisms.spawnRadiationArea(999,"Russians",{60,60},10,500,9999,"RadSite",s.tick,true,nullptr);
            advanceRuntimeSpecialMechanisms(s);
            if(findObjectById(s,id)->health!=before-(variant==2?7:0))return fail("zero factor/zero verses/veteran armor/airborne");++boundaries;
        }
        for(bool multiplayer:{false,true}) {
            auto box=fixture(hz);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
            const int source=add(s,"DESO",{60,60}),target=add(s,"MTNK",{63,64});
            if(source<0||target<0)return fail("deploy fixture");
            const int initial=findObjectById(s,target)->health;
            if(!executeRuntimeSecondaryAction(s,source))return fail("actual deploy command");
            for(int n=0;n<hz*6&&s.specialMechanisms.radiationAreas().empty();++n){++s.tick;advanceRuntimeSimulation(s,false);}
            if(s.specialMechanisms.radiationAreas().size()!=1||findObjectById(s,target)->health!=initial-5)
                return fail("actual deployed pulse did not damage tank by stock armor");
            ++deployments;
            std::ostringstream text;IniFile ini;auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
            if(!writeRuntimeStateToStream(s,text,error)
                ||(multiplayer&&!writeRuntimeMultiplayerSnapshotExtension(s,text,error))
                ||!ini.loadFromString(text.str())||!materializeRuntimeLoadStateFromIni(*loaded,ini,error)
                ||(multiplayer&&!materializeRuntimeMultiplayerSnapshotExtension(*loaded,ini,error)))return fail("save: "+error);
            ++saves;
            for(int n=0;n<hz*2;++n) {
                ++s.tick;advanceRuntimeSimulation(s,false);++loaded->tick;advanceRuntimeSimulation(*loaded,false);
                if(s.specialMechanisms.stateHash()!=loaded->specialMechanisms.stateHash()
                    ||findObjectById(s,target)->health!=findObjectById(*loaded,target)->health
                    ||findObjectById(s,source)->radiationEruptionCooldownTicksRemaining
                        !=findObjectById(*loaded,source)->radiationEruptionCooldownTicksRemaining)
                    return fail("continued tank damage after save");
                ++continuedTicks;
            }
            if(findObjectById(s,target)->health>=initial-5)return fail("no later radiation pulse");
        }
    }
    std::cout<<"Radiation armor passed: cases="<<cases<<" boundaries="<<boundaries<<" realDeployments="<<deployments
        <<" saves="<<saves<<" continuedTicks="<<continuedTicks<<" heavy=10 light=50 halfRadius=half immune=0\n";
    return true;
}
