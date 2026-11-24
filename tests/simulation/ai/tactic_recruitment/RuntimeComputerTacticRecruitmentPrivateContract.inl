namespace h007_private_qualification {
namespace original {
#define runtimeComputerTacticGroup reference_runtimeComputerTacticGroup
#define runtimeComputerTacticMayRecruit reference_runtimeComputerTacticMayRecruit
#include "RuntimeComputerTacticRecruitmentOriginal.inl"
#undef runtimeComputerTacticGroup
#undef runtimeComputerTacticMayRecruit
}
std::string exercise(bool reference,int& checks){
    const auto require=[&](bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);};
    auto owned=std::make_unique<IndependentRecoveryHost>();auto& s=*owned;std::string error;
    require(configureRuntimeDetailSnapshotSmokeWorld(s,false,error),"recruit fixture "+error);
    RuntimeSnapshotActiveStateScope active(s);s.config.defaultMapPath="h007-generated-plain.map";s.config.multiplayerActive=false;s.runtimeSuppressPlayerAudioFeedback=true;
    // The snapshot fixture uses authored "-" tokens. Combat expects the canonical empty no-team value.
    for(auto& [owner,team]:s.skirmishTeamByOwner){(void)owner;team.clear();}
    // A complete generated plain: TerrainCell defaults to a missing tile sentinel.
    for(auto& terrain:s.world.accessTerrainCells()){terrain.tileIndex=0;terrain.subTileIndex=0;}
    rebuildRuntimeTerrainInfoCache(s);
    require(runtimeCellIsPassable(s,s.terrainByCell,buildRuntimeOccupancyLookup(s,43,nullptr,nullptr),RuntimeCellPoint{6,6},*findObjectById(s,43)),"generated plain admits original movement command");
    require(runtimeOwnersAreHostileForAutoTarget(s,"SkirmishPlayer1","SkirmishPlayer2"),"real command fixture actors explicitly hostile");
    auto group=reference?original::reference_runtimeComputerTacticGroup:concord::runtime::session_detail::runtimeComputerTacticGroup;
    auto eligible=reference?original::reference_runtimeComputerTacticMayRecruit:concord::runtime::session_detail::runtimeComputerTacticMayRecruit;
    auto recruit=reference?original::recruitRuntimeComputerTacticMembers:concord::runtime::session_detail::recruitRuntimeComputerTacticMembers;
    auto join=reference?original::joinRuntimeComputerTacticReinforcements:concord::runtime::session_detail::joinRuntimeComputerTacticReinforcements;
    RuntimeTaskForce force;force.id="H007-force";force.group=9;s.triggers.taskForces[force.id]=force;
    RuntimeTeamType wanted;wanted.id="H007-wanted";wanted.taskForceId=force.id;wanted.group=-1;wanted.priority=2;
    require(group(s,wanted)==9,"actual taskforce authored fallback");wanted.group=4;require(group(s,wanted)==4,"actual explicitgroup wins");
    wanted.taskForceId="absent";wanted.group=-1;require(group(s,wanted)==-1,"actual absent taskforce fallback");
    RuntimeTeamType old;old.id="H007-old";old.group=1;old.priority=1;old.recruitable=false;
    s.triggers.teams[old.id]=old;s.triggers.teams[wanted.id]=wanted;
    s.triggers.computerTacticsByOwner.clear();auto& tactics=s.triggers.computerTacticsByOwner["skirmishplayer1"];
    auto& prior=tactics.teams[1];prior.instanceId=1;prior.teamTypeId=old.id;prior.objectIds={41};
    auto& target=tactics.teams[2];target.instanceId=2;target.teamTypeId=wanted.id;target.missingMembers={"SNAPSHOT_TANK"};target.launched=true;
    auto& ai=s.computerAiByOwner["skirmishplayer1"];ai.initialized=true;ai.owner="SkirmishPlayer1";ai.baseCenter={2,3};
    auto& type=s.triggers.teams.at(wanted.id);auto& oldType=s.triggers.teams.at(old.id);
    const auto* soldier=findObjectById(s,41);require(soldier!=nullptr,"existing realfixtureactor41");
    require(!eligible(s,ai,target,type,*soldier),"actual protected peer not recruitable");
    oldType.recruitable=true;type.priority=oldType.priority;require(!eligible(s,ai,target,type,*soldier),"actual equal priority rejected");
    type.priority=oldType.priority+1;type.group=4;require(!eligible(s,ai,target,type,*soldier),"actual group mismatch rejected");
    type.recruiter=true;require(eligible(s,ai,target,type,*soldier),"actual higher recruiter eligible");
    require(recruit(s,ai,tactics,target,type),"actual recruitment transfers member");
    require(target.objectIds==std::vector<int>{41} && prior.objectIds.empty(),"actual transfer old/new tables");
    require(target.joiningObjectIds==std::vector<int>{41} && target.missingMembers.empty(),"actual joining and missing mutation");
    require(target.lastProgressTick==s.tick && findObjectById(s,41)->mission=="Guard","actual tick/mission publication");
    auto* member=findMutableObjectById(s,41);member->factoryExitHostId=44;
    require(!join(s,target,type)&&target.joiningObjectIds==std::vector<int>{41},"actual active factory exit waits");
    member->factoryExitHostId=-1;member->mission="FactoryExit";
    require(!join(s,target,type)&&target.joiningObjectIds==std::vector<int>{41},"actual exact mission exit waits");
    member->mission="Guard";member->veterancy=0;type.veteranLevel=3;target.objectIds={41,43};target.missionInstanceId=991;
    RuntimeTeamMissionState mission;mission.instanceId=991;mission.scriptId="H007-script";mission.objectIds={43};mission.stepIssued=true;mission.targetObjectId=42;mission.targetCell={9,5};
    s.triggers.teamMissions[991]=mission;RuntimeScriptType script;script.id="H007-script";script.steps={{0,0}};s.triggers.scripts[script.id]=script;
    require(join(s,target,type),"actual mission attack join changed");
    require(target.joiningObjectIds.empty() && s.triggers.teamMissions.at(991).objectIds==std::vector<int>({43,41}),"actual one join mission append order");
    require(findObjectById(s,41)->veterancy==2,"actual veterancy original promotion");
    require(s.behaviors.contains(41)&&s.behaviors.at(41).kind==RuntimeBehaviorKind::Attack&&s.behaviors.at(41).targetObjectId==42,"actual positive issued attack through original command owner");
    target.joiningObjectIds={43};auto& current=s.triggers.teamMissions.at(991);current.targetObjectId=-1;current.targetCell={6,6};
    require(join(s,target,type)&&target.joiningObjectIds.empty(),"actual fallback movement join");
    require(s.behaviors.contains(43)&&s.behaviors.at(43).kind==RuntimeBehaviorKind::Move,"actual positive issued move through original owner");
    target.joiningObjectIds={999999};require(!join(s,target,type)&&target.joiningObjectIds.empty(),"actual missing joining member erased while original changed return stays false");
    std::ostringstream out;const bool saved=writeRuntimeStateToStream(s,out,error);require(saved,"actual complete save "+error);return out.str();
}
int run(){try{int checks=0;const auto reference=exercise(true,checks),production=exercise(false,checks);++checks;if(reference!=production)throw std::runtime_error("actual complete private qualification save differs original");std::cout<<"private-recruitment checks="<<checks<<" complete-save=exact positive-attack=yes positive-move=yes failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<"private recruitment: "<<e.what()<<'\n';return 1;}}
}
