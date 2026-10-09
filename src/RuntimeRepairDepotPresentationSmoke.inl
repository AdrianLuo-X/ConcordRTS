bool runRuntimeRepairDepotPresentationSmoke(RuntimeWindowState& live,std::string& error) {
    const auto fail=[&](const std::string& why){error="repair depot presentation: "+why;return false;};
    const char* output=std::getenv("CONCORD_PRISM_PRESENTATION_OUTPUT");
    const std::string root=output?output:"";
    int cases=0;
    for(const char* depot:{"GADEPT","NADEPT"})for(const char* type:{"MTNK","HTNK","FV"}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);
        s.config.fixedTickHz=30;s.config.multiplayerActive=false;s.config.computerAiEnabled=false;
        IniFile rules;if(!rules.loadFromFile(live.report.rulesPath)||!s.rules.loadFromIni(rules))return fail("stock rules");
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.mapFacts={};s.mapFactsLoaded=false;s.terrainByCell.clear();s.terrainInfoByCell.clear();
        s.economy.owner="Americans";s.economy.credits=10000;s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true;s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=true;s.runtimeSoundVolume=100;
        s.runtimeSuppressPlayerAudioFeedback=false;
        if(!loadRuntimeSoundDatabase(s))return fail("sound bank");
        for(int y=52;y<84;++y)for(int x=52;x<84;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance host;host.instanceId=1;host.kind=WorldObjectKind::Structure;host.type=depot;
        host.owner=s.economy.owner;host.alive=true;host.health=s.rules.findBuildingType(depot)->strength;
        host.x=64;host.y=64;host.cell=runtimeCellKey(64,64);host.mission="Guard";
        auto tank=host;tank.instanceId=2;tank.kind=WorldObjectKind::Unit;tank.type=type;
        tank.x=61;tank.y=64;tank.cell=runtimeCellKey(61,64);
        const auto* unit=s.rules.findUnitType("UnitType",type);
        if(!unit)return fail("native vehicle");
        tank.health=unit->strength/2;tank.facing=64;
        s.world.accessObjects()={host,tank};RuntimeSnapshotActiveStateScope scope(s);
        initializeRuntimeNavigation(s);ensureRuntimeRepairDepotsInitialized(s);
        s.selectedObjectIds={2};
        if(runtimeTargetCursorIntent(s,*findObjectById(s,1))!=RuntimeTargetCursorIntent::Enter)
            return fail(std::string(depot)+" did not share the infantry entry cursor");
        s.runtimeSoundIntentHistory.clear();s.runtimeSoundPlaybackHistory.clear();
        const int before=s.runtimeSoundEvents;
        (void)dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            runtimeCellPointForObject(*findObjectById(s,1)),findObjectById(s,1));
        if(s.lastCommand.verb!="unitRepair"||s.lastCommand.acceptedCount!=1||s.runtimeSoundEvents!=before+1
            ||s.runtimeSoundPlaybackHistory.empty())return fail(std::string(depot)+" real entry command or single voice");
        const auto& voice=s.runtimeSoundPlaybackHistory.back();
        if(voice.section!=unit->voiceMove||!voice.playable||voice.sample.empty()||voice.status!="resolved")
            return fail(std::string(type)+" used another unit's voice: "+voice.section);
        const auto* relation=findRuntimeRepairDepotRelationForUnit(s.repairDepots,2);
        if(!relation)return fail("dock reservation");
        const auto dock=relation->dockCell;
        auto* actor=findMutableObjectById(s,2);actor->x=dock.x;actor->y=dock.y;
        actor->cell=runtimeCellKey(dock.x,dock.y);actor->onBridge=dock.onBridge;clearRuntimeRenderPosition(*actor);
        const auto capture=[&](const std::string& phase,bool onPlatform){
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{65,65},800,600,error,
                root.empty()?"":root+"/"+depot+"-"+type+"-"+phase+".bmp",false);
            return stats&&stats->repairDepotForegroundObjects==(onPlatform?1:0);
        };
        if(!capture("enter",true))return fail("entering vehicle remained under platform: "+error);
        (void)completeRuntimeRepairDepotArrival(s,2);
        if(findObjectById(s,2)->dockedToObjectId!=1||!capture("docked",true))return fail("docked foreground");
        const int hp=findObjectById(s,2)->health;
        for(int step=0;step<120&&findObjectById(s,2)->health==hp;++step){++s.tick;(void)advanceRuntimeRepairDepots(s);}
        if(findObjectById(s,2)->health<=hp)return fail("native repair pulse missing");
        std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored,s,false);
        if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())
            ||!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("docked load: "+error);
        const auto restoredStats=drawRuntimeStateOffscreenAtCell(*restored,{65,65},800,600,error,"",false);
        if(!restoredStats||restoredStats->repairDepotForegroundObjects!=1)return fail("loaded foreground");
        (void)releaseRuntimeRepairDepotForCommand(s,2,"presentation-leave");
        if(!capture("leaving",true))return fail("departing vehicle fell under platform");
        actor=findMutableObjectById(s,2);actor->x=70;actor->y=70;actor->cell=runtimeCellKey(70,70);clearRuntimeRenderPosition(*actor);
        if(!capture("outside",false))return fail("outside vehicle kept platform depth");
        ++cases;
    }
    std::cout<<"Repair depot presentation: "<<cases<<" native depots/vehicles; real entry cursor, own playable move voice, "
        <<"enter/dock/leave/outside depth, paid repair and 6 docked reloads passed\n";
    return true;
}
