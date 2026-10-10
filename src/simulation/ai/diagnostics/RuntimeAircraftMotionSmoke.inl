#include "simulation/ai/diagnostics/RuntimeAircraftMotionIntegrationSmoke.inl"

bool runRuntimeAircraftMotionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& detail){error="Aircraft motion: "+detail;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("original resources unavailable");
    const bool diagnostic=std::getenv("CONCORD_AIRCRAFT_MOTION_DIAGNOSTIC")!=nullptr;
    const char* output=std::getenv("CONCORD_AIRCRAFT_MOTION_OUTPUT");
    const std::array<RuntimeCellPoint,10> offsets{{{24,7},{7,24},{-7,24},{-24,7},
        {-24,-7},{-7,-24},{7,-24},{24,-7},{24,0},{24,24}}};
    int cases=0,totalStops=0;double worstDeviation=0;
    for(const auto* type:{"ORCA","BEAG"})for(int hz:{15,30,60})for(const auto delta:offsets) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=config;s.config.fixedTickHz=hz;s.config.noCooldown=false;
        s.config.computerAiEnabled=false;s.config.shortGame=false;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false;s.rules=assets->rules;s.art=assets->art;
        s.rulesLoaded=s.artLoaded=true;s.economy.owner="Americans";
        for(int y=1;y<64;++y)for(int x=1;x<64;++x){TerrainCell c;
            c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=c.subTileIndex=0;
            s.world.accessTerrainCells().push_back(c);}
        WorldObjectInstance plane;plane.instanceId=100;plane.type=type;plane.owner="Americans";
        plane.kind=WorldObjectKind::Unit;plane.authoredAircraft=true;plane.alive=true;
        plane.x=plane.y=32;plane.cell=runtimeCellKey(32,32);plane.airborne=true;
        plane.level=plane.flightLevel=6;plane.health=200;plane.mission="Move";
        plane.facing=runtimeFacingFromDelta(delta.x,delta.y);
        s.world.accessObjects().push_back(plane);initializeRuntimeNavigation(s);
        const RuntimeCellPoint goal{32+delta.x,32+delta.y};
        RuntimeObjectBehavior behavior;behavior.kind=RuntimeBehaviorKind::Move;
        behavior.moveTarget=goal;s.behaviors.emplace(100,behavior);
        auto* p=findMutableObjectById(s,100);
        const double length=std::hypot(delta.x,delta.y);
        // Both original types have Speed=14 -> 35 leptons/rule frame.
        const int nominal=static_cast<int>(std::ceil(length*256.0*hz/(35.0*15.0)));
        std::ofstream trace;
        if(output && hz==30 && delta.x==24 && delta.y==7) {
            trace.open(std::filesystem::path(output)/(std::string(type)+"-trajectory.csv"));
            trace<<"tick,x,y,facing,step,deviation\n";
        }
        double previousX=32,previousY=32,maxDeviation=0;int stops=0,ticks=0;bool arrived=false;
        for(;ticks<4000;++ticks){++s.tick;
            advanceRuntimeMoveBehavior(s,*p,s.behaviors.at(100));
            if(!diagnostic&&ticks==8) {
                const auto flight=serializeRuntimeActiveAircraftFlights(s);
                const double x=p->renderX,y=p->renderY;
                advanceRuntimeMoveBehavior(s,*p,s.behaviors.at(100));
                if(flight!=serializeRuntimeActiveAircraftFlights(s)||x!=p->renderX||y!=p->renderY)
                    return fail("same simulation tick advanced flight twice");
            }
            const double x=p->hasRenderPosition?p->renderX:p->x;
            const double y=p->hasRenderPosition?p->renderY:p->y;
            const double step=std::hypot(x-previousX,y-previousY);
            const double deviation=std::abs(delta.x*(y-32)-delta.y*(x-32))/length;
            maxDeviation=std::max(maxDeviation,deviation);stops+=step<1e-8?1:0;
            if(trace)trace<<s.tick<<','<<std::setprecision(12)<<x<<','<<y<<','<<p->facing<<','<<step<<','<<deviation<<'\n';
            previousX=x;previousY=y;
            if(std::hypot(x-goal.x,y-goal.y)<1e-6){arrived=true;++ticks;break;}
        }
        if(!arrived)return fail(std::string(type)+" never arrived");
        ++cases;totalStops+=stops;worstDeviation=std::max(worstDeviation,maxDeviation);
        std::cout<<"Aircraft motion: "<<type<<" hz="<<hz<<" delta="<<delta.x<<','<<delta.y
            <<" ticks="<<ticks<<" nominal="<<nominal<<" stops="<<stops<<" deviation="<<maxDeviation<<'\n';
        if(!diagnostic && (stops>0 || maxDeviation>0.001 || std::abs(ticks-nominal)>1))
            return fail("flight must remain on its straight course and consume only its travel time");
    }
    std::cout<<"Aircraft motion: "<<(diagnostic?"baseline":"passed")<<" cases="<<cases
        <<" stops="<<totalStops<<" worstDeviation="<<worstDeviation<<'\n';
    return diagnostic||runRuntimeAircraftMotionIntegrationSmoke(*assets,error);
}
