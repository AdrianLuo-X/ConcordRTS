namespace h009_private_qualification {
namespace original {
#define runtimeComputerTacticLane reference_runtimeComputerTacticLane
#define runtimeComputerTacticAircraftSpawn reference_runtimeComputerTacticAircraftSpawn
#define completeRuntimeComputerTacticUnit reference_completeRuntimeComputerTacticUnit
#define queueRuntimeComputerTacticProduction reference_queueRuntimeComputerTacticProduction
#define advanceRuntimeComputerTacticProduction reference_advanceRuntimeComputerTacticProduction
#include "RuntimeComputerTacticProductionOriginal.inl"
#undef runtimeComputerTacticLane
#undef runtimeComputerTacticAircraftSpawn
#undef completeRuntimeComputerTacticUnit
#undef queueRuntimeComputerTacticProduction
#undef advanceRuntimeComputerTacticProduction
}
std::string valueAbi(){using Spawn=RuntimeProductionSpawn;using Build=RuntimeAircraftProductionMatchInputBuild;static_assert(std::is_same_v<decltype(Build{}.input),::RuntimeAircraftProductionMatchInput>);static_assert(std::is_same_v<decltype(Build{}.error),std::optional<::RuntimeAircraftProductionMatchResult>>);std::ostringstream o;o<<"ABI "<<typeid(Spawn).name()<<':'<<sizeof(Spawn)<<'/'<<alignof(Spawn)<<':'<<offsetof(Spawn,anchor)<<','<<offsetof(Spawn,cell)<<','<<offsetof(Spawn,dockIndex)<<','<<offsetof(Spawn,aircraftDock)<<','<<offsetof(Spawn,renderX)<<','<<offsetof(Spawn,renderY)<<','<<offsetof(Spawn,level)<<'|'<<typeid(Build).name()<<':'<<sizeof(Build)<<'/'<<alignof(Build)<<':'<<offsetof(Build,input)<<','<<offsetof(Build,error);return o.str();}
std::string exercise(bool reference,int& checks){const auto require=[&](bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);};std::ostringstream image;image<<valueAbi()<<'\n';
 for(int scenario=0;scenario<8;++scenario){auto owned=std::make_unique<IndependentRecoveryHost>();auto& s=*owned;std::string error;const bool configured=configureRuntimeDetailSnapshotSmokeWorld(s,false,error);require(configured,"production private fixture "+error);RuntimeSnapshotActiveStateScope active(s);s.config.defaultMapPath="h009-private-production.map";s.config.multiplayerActive=false;s.runtimeSuppressPlayerAudioFeedback=true;for(auto& [owner,team]:s.skirmishTeamByOwner){(void)owner;team.clear();}for(auto& t:s.world.accessTerrainCells()){t.tileIndex=0;t.subTileIndex=0;}
 IniFile ini;require(ini.loadFromString(R"H9([General]
PadAircraft=H009_PLANE
[VehicleTypes]
0=SNAPSHOT_TANK
[AircraftTypes]
0=H009_PLANE
[BuildingTypes]
0=DETAIL_FACTORY
1=SNAPSHOT_LANDMARK
2=H009_AIRPORT
[SNAPSHOT_TANK]
Strength=200
Cost=100
TechLevel=1
Speed=6
SpeedType=Track
MovementZone=Normal
[DETAIL_FACTORY]
Strength=1000
Cost=100
TechLevel=1
Factory=UnitType
WeaponsFactory=yes
Foundation=1x1
Powered=yes
[SNAPSHOT_LANDMARK]
Strength=1000
[H009_PLANE]
Strength=150
Cost=80
TechLevel=1
AirportBound=yes
Dock=H009_AIRPORT
Speed=10
Ammo=3
[H009_AIRPORT]
Strength=1000
Cost=150
TechLevel=1
Factory=AircraftType
NumberOfDocks=4
Helipad=yes
Powered=yes
Foundation=2x2
)H9")&&s.rules.loadFromIni(ini),"actual producer rules");s.rulesLoaded=true;s.structureFootprints["H009_AIRPORT"]={2,2,true};rebuildRuntimeTerrainInfoCache(s);initializeRuntimeNavigation(s);
 WorldObjectInstance airport;airport.instanceId=80;airport.mapId="H009Airport";airport.kind=WorldObjectKind::Structure;airport.type="H009_AIRPORT";airport.owner="SkirmishPlayer1";airport.x=8;airport.y=8;airport.cell=runtimeCellKey(8,8);airport.health=1000;airport.alive=true;airport.poweredOperational=true;airport.mission="Guard";airport.facing=0;clearRuntimeRenderPosition(airport);s.world.accessObjects().push_back(airport);refreshRuntimeObjectOccupancyAfterLocalChange(s);
 s.triggers.computerTacticsByOwner.clear();auto& ai=s.computerAiByOwner["skirmishplayer1"];ai.initialized=true;ai.owner="SkirmishPlayer1";ai.country="Americans";ai.difficulty="Hard";ai.credits=10000;ai.creditsSpent=0;ai.productionSerial=31;ai.unitsBuilt=0;ai.lastBuildType="old";
 auto lane=reference?original::reference_runtimeComputerTacticLane:concord::runtime::session_detail::runtimeComputerTacticLane;
 auto spawn=reference?original::reference_runtimeComputerTacticAircraftSpawn:concord::runtime::session_detail::runtimeComputerTacticAircraftSpawn;
 auto complete=reference?original::reference_completeRuntimeComputerTacticUnit:concord::runtime::session_detail::completeRuntimeComputerTacticUnit;
 auto queue=reference?original::reference_queueRuntimeComputerTacticProduction:concord::runtime::session_detail::queueRuntimeComputerTacticProduction;
 auto advance=reference?original::reference_advanceRuntimeComputerTacticProduction:concord::runtime::session_detail::advanceRuntimeComputerTacticProduction;
 const bool air=scenario>=5;auto built=runtimeComputerAiBuildItem(s,air?"H009_PLANE":"SNAPSHOT_TANK",ai.owner);require(built.has_value(),"actual production catalog");auto item=*built;item.buildTicks=3;require(lane(item)==(air?2:1),"actual lane from catalog");require(runtimeComputerAiItemAllowed(s,ai,item),"actual producer eligibility");
 if(scenario==0){int id=997;const bool done=complete(s,ai,item,id);require(done,"positive ground complete "+ai.status);const auto* unit=findObjectById(s,id);require(unit&&unit->type=="SNAPSHOT_TANK"&&unit->owner==ai.owner&&ai.unitsBuilt==1,"actual ground new actor/counter");}
 else if(scenario==1){ai.credits=item.cost-1;require(!queue(s,ai,item,-1),"unaffordable rejected");require(s.triggers.computerTacticsByOwner.contains("skirmishplayer1")&&s.triggers.computerTacticsByOwner.at("skirmishplayer1").production[1].typeId.empty(),"actual owner inserted before rejected validation");}
 else if(scenario==2){require(queue(s,ai,item,-1),"actual paid queue");auto& o=s.triggers.computerTacticsByOwner.at("skirmishplayer1").production[1];require(o.teamInstanceId==-1&&o.paidCost==item.cost&&o.remainingTicks==3&&ai.creditsSpent==item.cost&&ai.productionSerial==32,"actual queue budget/order");require(!queue(s,ai,item,-1),"actual occupied lane rejects");}
 else if(scenario==3||scenario==4){RuntimeTaskForce force;force.id="H009_FORCE";force.members={{1,"SNAPSHOT_TANK"}};s.triggers.taskForces[force.id]=force;RuntimeScriptType script;script.id="H009_SCRIPT";script.steps={{49,0}};s.triggers.scripts[script.id]=script;RuntimeTeamType type;type.id="H009_TEAM";type.taskForceId=force.id;type.scriptId=script.id;type.house=ai.owner;s.triggers.teams[type.id]=type;auto& t=s.triggers.computerTacticsByOwner["skirmishplayer1"];t.nextTeamInstanceId=8;auto& team=t.teams[7];team.instanceId=7;team.teamTypeId=type.id;team.launched=true;team.missingMembers={"snapshot_tank","SNAPSHOT_TANK"};require(queue(s,ai,item,7),"actual team queued");if(scenario==4){auto* factory=findMutableObjectById(s,44);require(factory!=nullptr,"fixture factory44");factory->poweredOperational=false;require(!advance(s,ai,t)&&t.production[1].remainingTicks==3,"actual paid order pauses before countdown on lost power");}else{require(advance(s,ai,t)&&t.production[1].remainingTicks==2,"actual first countdown");require(advance(s,ai,t)&&t.production[1].remainingTicks==1,"actual second countdown");require(advance(s,ai,t)&&t.production[1].typeId.empty(),"actual completion clears order");require(team.objectIds.size()==1&&team.joiningObjectIds==team.objectIds&&team.missingMembers==std::vector<std::string>{"SNAPSHOT_TANK"}&&team.lastProgressTick==s.tick,"actual team/missing/ordered join publication");}}
 else if(scenario==5){const auto p=spawn(s,ai.owner,item);require(p&&p->anchor&&p->anchor->instanceId==80&&p->dockIndex>=0&&p->dockIndex<4,"actual standard matcher yields owned airport spawn");image<<"spawn "<<p->cell.x<<','<<p->cell.y<<','<<p->dockIndex<<','<<p->renderX<<','<<p->renderY<<','<<p->level<<'\n';int id=991;require(complete(s,ai,item,id),"actual positive aircraft complete");const auto* unit=findObjectById(s,id);require(unit&&unit->type=="H009_PLANE"&&unit->dockedToObjectId==80&&unit->dockIndex>=0&&ai.unitsBuilt==1,"actual aircraft/dock/counter");}
 else if(scenario==6){std::set<int> pads;for(int i=0;i<4;++i){int id=-1;require(complete(s,ai,item,id),"actual four supported aircraft");const auto* unit=findObjectById(s,id);require(unit&&unit->dockedToObjectId==80&&pads.insert(unit->dockIndex).second,"actual distinct support dock");}require(!spawn(s,ai.owner,item),"actual fifth lacks physical support");int denied=123;require(!complete(s,ai,item,denied)&&denied==-1&&ai.unitsBuilt==4,"actual fifth completion/output/counter rejected");}
 else {require(queue(s,ai,item,-1),"actual air paid queue");auto* host=findMutableObjectById(s,80);require(host!=nullptr,"fixture airhost80");host->poweredOperational=false;auto& t=s.triggers.computerTacticsByOwner.at("skirmishplayer1");require(!advance(s,ai,t)&&t.production[2].remainingTicks==3,"actual air unpowered host pauses paid order");}
 std::ostringstream saved;const bool wrote=writeRuntimeStateToStream(s,saved,error);require(wrote,"actual complete production save "+std::to_string(scenario)+" "+error);image<<"case "<<scenario<<'\n'<<saved.str();
 }
 return image.str();
}
int run(){try{int checks=0;const auto a=exercise(true,checks),b=exercise(false,checks);++checks;if(a!=b)throw std::runtime_error("actual original production outcomes/fullsave differ");std::cout<<valueAbi()<<'\n'<<"private-tactic-production checks="<<checks<<" actual-root-states=16 positive-ground=yes positive-air=yes four-docks=yes complete-save=exact failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
}
