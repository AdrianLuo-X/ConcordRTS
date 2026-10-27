// Current-product real TCP peers; adapted diagnostic recipe, never old game implementation.
// Rendezvous files carry only readiness/port; setup/commands/save/hash use production LAN.
namespace path_candidate_network {
namespace fs = std::filesystem;
constexpr int SaveTick=60;
int finalTick() {
 static const int value=[] {
  const char* configured=std::getenv("CONCORD_PATH_CANDIDATE_NETWORK_TICKS");
  if(!configured)return 240;
  std::size_t used=0;const std::string text(configured);const int ticks=std::stoi(text,&used);
  if(used!=text.size()||ticks<120||ticks>600||ticks%20!=0)throw std::invalid_argument("private network ticks must be120..600 in steps of20");
  return ticks;
 }();return value;
}
constexpr std::uint64_t Seed=0x3141592653589793ULL;
void check(bool value,const std::string& error){if(!value) throw std::runtime_error(error);}
fs::path projectRoot(){
 const auto root=fs::current_path();
 check(fs::is_regular_file(root/"CMakeLists.txt") && fs::is_regular_file(root/"src/runtime/host/RuntimeShell.cpp"),"run the diagnostic from the project root");
 return root;
}
fs::path recordPath(const fs::path& path){
 const auto output=fs::absolute(path).lexically_normal(); std::string normalized=output.generic_string();
 std::transform(normalized.begin(),normalized.end(),normalized.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
 std::string allowed=(projectRoot()/"build/smoke/path-candidate").lexically_normal().generic_string()+"/";
 std::transform(allowed.begin(),allowed.end(),allowed.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
 check(normalized.starts_with(allowed),"path is outside build/smoke/path-candidate");return output;
}
void textFile(const fs::path& path,const std::string& text){
 std::string error;check(runtimeWriteFileAtomically(path,[&](std::ostream& out,std::string& e){out<<text;if(!out){e="S37 output write failed";return false;}return true;},error),error);
}
int marker(const fs::path& path){std::ifstream in(path);int tick=-1;if(in)in>>tick;return tick;}
std::string hex(std::uint64_t value){std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<value;return out.str();}
void initializeActualMap(RuntimeWindowState& state,const RuntimeConfig& config){
 state.config=config;state.runtimePreferencePersistenceEnabled=false;applyRuntimeConfigPreferences(state);
 RuntimeLoadingWindowState loading;
 check(prepareRuntimeLoadingMapStartPlan(loading,state),"original map/start plan failed: "+state.report.errorMessage);
 loadRuntimeSoundDatabase(state);ensureRuntimeLightingInitialized(state);ensureRuntimeParticlesInitialized(state);
 check(initializeRuntimeSkirmishStart(state,&loading.mapStartAssignments,&loading.mapStartAssignments),"original native skirmish start failed: "+state.economy.skirmishStatus);
 check(initializeRuntimeTriggers(state),"original triggers failed: "+state.triggers.status);
 check(state.report.success&&state.theaterLoaded&&state.artLoaded&&state.rulesLoaded,"original assets did not load");
}
struct ProductionCheckpoint {RuntimeProductionLaneOrder order;int credits=0;};
void writeProductionRows(const RuntimeWindowState& state,std::ostream& out){
 std::vector<const RuntimeHumanHouseAuthority*> houses;for(const auto& house:state.humanHouseAuthorities)houses.push_back(&house);
 std::sort(houses.begin(),houses.end(),[](const auto* left,const auto* right){return std::tie(left->canonicalOwner,left->slotIndex)<std::tie(right->canonicalOwner,right->slotIndex);});
 for(const auto* house:houses){const auto* economy=runtimeHumanHouseEconomy(state,house->owner);check(economy,"native human house economy missing");
  for(const auto lane:runtimeProductionLanes){const auto& value=economy->localProductionLanes.lanes[runtimeProductionLaneIndex(lane)];
   auto row=[&](const char* phase,std::size_t index,const RuntimeProductionLaneOrder* order){
    out<<state.tick<<'\t'<<house->canonicalOwner<<'\t'<<house->slotIndex<<'\t'<<runtimeProductionLaneName(lane)<<'\t'<<economy->credits<<'\t'<<economy->completedBuildings<<'\t'<<economy->skirmishStartObjectId<<'\t'<<economy->localProductionLanes.nextSerial<<'\t'<<value.onHold<<'\t'<<runtimeProductionLanesStateHash(economy->localProductionLanes)<<'\t'<<phase<<'\t'<<index;
    if(order)out<<'\t'<<order->serial<<'\t'<<order->itemKey<<'\t'<<order->cost<<'\t'<<order->totalTicks<<'\t'<<order->remainingTicks<<'\t'<<order->creditsSpent<<'\t'<<order->creditRemainderMicros<<'\t'<<order->zeroCostProgressMicros<<'\t'<<order->rateDivisionRemainder<<'\t'<<order->rateDivisionDenominator<<'\t'<<order->requiresPlacement;
    else out<<"\t0\t-\t0\t0\t0\t0\t0\t0\t0\t0\t0";out<<'\n';
   };
   row("lane",0,nullptr);std::size_t index=0;for(const auto& order:value.queued)row("queued",index++,&order);if(value.active)row("active",0,&*value.active);if(value.ready)row("ready",0,&*value.ready);
  }
 }
}
void verifyNativeProductionProgress(const RuntimeWindowState& state,const RuntimeHumanHouseAuthority& house,std::map<std::string,ProductionCheckpoint>& checkpoints){
 const auto* economy=runtimeHumanHouseEconomy(state,house.owner);check(economy,"native production proof has no house economy");const auto& lane=economy->localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Structures)];
 const auto expected=house.slotIndex==0?"GAPOWR":"NAPOWR";const auto yardType=house.slotIndex==0?"GACNST":"NACNST";
 const bool originalMcvAlive=std::any_of(state.world.getObjects().begin(),state.world.getObjects().end(),[&](const auto& object){return object.instanceId==economy->skirmishStartObjectId&&object.alive;});
 const bool nativeYard=std::any_of(state.world.getObjects().begin(),state.world.getObjects().end(),[&](const auto& object){return runtimeMultiplayerCanonicalIdentifier(object.type)==runtimeMultiplayerCanonicalIdentifier(yardType)&&runtimeStructureProvidesFactoryForOwner(state,object,house.owner,"BuildingType");});
 check(!originalMcvAlive&&nativeYard&&economy->completedBuildings>=1,"tick "+std::to_string(state.tick)+" "+house.canonicalOwner+" original MCV did not produce its native construction yard/factory");
 check(lane.active.has_value()&&!lane.onHold,"tick "+std::to_string(state.tick)+" "+house.canonicalOwner+" native Structures lane is not actively producing");const auto& order=*lane.active;
 check(runtimeProductionItemKeysMatch(order.itemKey,expected)&&order.serial>0&&order.requiresPlacement&&order.cost>0&&order.totalTicks>0&&order.remainingTicks>0&&order.remainingTicks<order.totalTicks&&order.creditsSpent>0&&economy->credits<state.config.startingCredits,"tick "+std::to_string(state.tick)+" "+house.canonicalOwner+" expected native power-plant order has no paid production progress");
 const auto previous=checkpoints.find(house.canonicalOwner);if(previous!=checkpoints.end()){const auto& earlier=previous->second;
  check(order.serial==earlier.order.serial&&order.itemKey==earlier.order.itemKey&&order.cost==earlier.order.cost&&order.totalTicks==earlier.order.totalTicks&&order.remainingTicks<earlier.order.remainingTicks&&order.creditsSpent>earlier.order.creditsSpent&&economy->credits<earlier.credits,"native production did not advance exactly the same order from checkpoint55 to Save60");}
 checkpoints[house.canonicalOwner]={order,economy->credits};
}
struct Evidence {
 RuntimeWindowState& state;fs::path dir;std::string role;std::ofstream hashes,objects,commands,production,paths;std::vector<double> times;std::map<std::string,ProductionCheckpoint> productionCheckpoints;int last=-1;
 Evidence(RuntimeWindowState& world,fs::path path,std::string peer):state(world),dir(std::move(path)),role(std::move(peer)),
  hashes(dir/(role+"-ticks.tsv")),objects(dir/(role+"-objects.tsv")),commands(dir/(role+"-commands.tsv")),production(dir/(role+"-production.tsv")),paths(dir/(role+"-paths.tsv")){
  check(hashes&&objects&&commands&&production&&paths,"private evidence files could not open");
  paths<<"tick\tid\ttargetX\ttargetY\tpending\tpendingGoalX\tpendingGoalY\tpath\n";
  hashes<<"tick\tauthoritativeHash\tworldHash\tobjects\tprojectiles\treplayEvents\tcommandsApplied\tcommandsNoOp\n";
  objects<<"tick\tid\ttype\towner\tkind\tx\ty\tlevel\thealth\talive\tfacing\tturretFacing\tmission\trenderX\trenderY\trenderLevel\tanimationDistance\tweaponCooldown\n"<<std::setprecision(17);
  commands<<"tick\tpeer\tframeSequence\tcommandIndex\tcommandType\tcanonicalPayloadHex\tnativeApplied\n";
  production<<"tick\towner\tslot\tlane\tcredits\tcompletedBuildings\tstartObjectId\tnextSerial\tonHold\tlaneHash\tphase\torderIndex\tserial\titem\tcost\ttotalTicks\tremainingTicks\tcreditsSpent\tcreditRemainderMicros\tzeroCostProgressMicros\trateDivisionRemainder\trateDivisionDenominator\trequiresPlacement\n";
 }
 void observe(){
  check(state.tick>last&&(last<0||state.tick==last+1),"completed tick was skipped/repeated/backward");std::string error;
  const auto identity=runtimeMultiplayerSnapshotSmokeIdentity(state,static_cast<std::uint64_t>(state.tick)+1U,"s37-tick-"+std::to_string(state.tick),RuntimeMultiplayerSnapshotKind::Save);
  RuntimeMultiplayerSnapshotMemoryImage image;
  check(identity.authoritativeStateHash&&exportRuntimeMultiplayerSnapshotV15(state,identity,image,error),"tick "+std::to_string(state.tick)+" complete original snapshot export: "+error);
  const auto stem=role+"-tick-"+std::to_string(state.tick);
  if(state.tick==0||state.tick==SaveTick||state.tick==finalTick())check(writeRuntimeMultiplayerSnapshotFile((dir/(stem+".snapshot.ini")).string(),image.bytes,error),error);
  if(state.tick==0||state.tick==SaveTick||state.tick==finalTick())check(writeRuntimeReplayEventSnapshotsToFile(state,(dir/(stem+".events.txt")).string(),error),error);
  const auto& events=materializeRuntimeReplayEventSnapshots(state);
  hashes<<state.tick<<'\t'<<identity.authoritativeStateHash<<'\t'<<state.world.deterministicSimulationStateHash()<<'\t'<<state.world.getObjects().size()<<'\t'<<state.projectiles.size()<<'\t'<<events.size()<<'\t'<<state.multiplayerGameplayCommandsApplied<<'\t'<<state.multiplayerGameplayCommandsNoOp<<'\n';
  for(const auto& o:state.world.getObjects())objects<<state.tick<<'\t'<<o.instanceId<<'\t'<<o.type<<'\t'<<o.owner<<'\t'<<static_cast<int>(o.kind)<<'\t'<<o.x<<'\t'<<o.y<<'\t'<<o.level<<'\t'<<o.health<<'\t'<<o.alive<<'\t'<<o.facing<<'\t'<<o.turretFacing<<'\t'<<o.mission<<'\t'<<o.renderX<<'\t'<<o.renderY<<'\t'<<o.renderLevel<<'\t'<<o.movementAnimationDistance<<'\t'<<o.weaponCooldownTicksRemaining<<'\n';
  std::vector<int> ids;for(const auto& [id,behavior]:state.behaviors){(void)behavior;ids.push_back(id);}std::sort(ids.begin(),ids.end());
  for(const int id:ids){const auto& b=state.behaviors.at(id);paths<<state.tick<<'\t'<<id<<'\t'<<b.moveTarget.x<<'\t'<<b.moveTarget.y<<'\t'<<b.pathRequestPending<<'\t'<<b.pendingPathGoal.x<<'\t'<<b.pendingPathGoal.y;for(const auto cell:b.path)paths<<'\t'<<cell.x<<','<<cell.y;paths<<'\n';}paths.flush();check(static_cast<bool>(paths),"path sequence write failed");
  writeProductionRows(state,production);hashes.flush();objects.flush();commands.flush();production.flush();check(hashes&&objects&&commands&&production,"evidence write failed");
  check(state.multiplayerGameplayCommandsNoOp==0,"fixed native recipe produced a gameplay no-op at tick "+std::to_string(state.tick));
  if(state.tick==55||state.tick==SaveTick){check(state.humanHouseAuthorities.size()==2,"native production proof requires the fixed two human houses");for(const auto& house:state.humanHouseAuthorities)verifyNativeProductionProgress(state,house,productionCheckpoints);}
  last=state.tick;textFile(dir/(role+".observed"),std::to_string(state.tick));
 }
 void applied(std::span<const RuntimeMultiplayerLockstepApplyCommand> batch){
  check(state.multiplayerGameplayAuthorityLog.size()>=batch.size(),"original per-command authority result missing");std::size_t resultIndex=state.multiplayerGameplayAuthorityLog.size()-batch.size();
  for(const auto& item:batch){const bool nativeApplied=state.multiplayerGameplayAuthorityLog[resultIndex++].starts_with("applied ");std::vector<std::uint8_t> bytes;std::string error;check(encodeRuntimeMultiplayerGameplayCommand(item.command,bytes,error),error);
   commands<<item.tick<<'\t'<<item.peerId<<'\t'<<item.frameSequence<<'\t'<<item.commandIndex<<'\t'<<static_cast<int>(runtimeMultiplayerGameplayCommandType(item.command))<<'\t';
   constexpr char digits[]="0123456789abcdef";for(const auto b:bytes)commands<<digits[b>>4U]<<digits[b&15U];commands<<'\t'<<nativeApplied<<'\n';}
 }
};
// Same production attach validation/restoration/callbacks. Wrappers only observe outputs.
void attachObserved(RuntimeWindowState& state,RuntimeMultiplayerRuntimeBridge& bridge,RuntimeMultiplayerRuntimeAttachmentState& attachment,Evidence& evidence){
 std::string error;check(restoreRuntimeMultiplayerLobbySnapshot(state,bridge,error),error);completeRuntimeWorldRefresh(state);
 check(!attachment.attached&&!state.multiplayerBridge,"world already attached");const bool participationValid=validateRuntimeMultiplayerRuntimeParticipation(state,bridge,error);check(participationValid,error+" phase="+std::to_string(static_cast<int>(bridge.lan().phase()))+" bridge="+bridge.fatalReason());
 attachment.state=&state;attachment.bridge=&bridge;state.multiplayerBridge=&bridge;
 state.multiplayerIntentRouter.setCaptureCallback([&bridge](const RuntimeMultiplayerGameplayCommand& command,std::string& e){return bridge.captureGameplayCommand(command,e);});
 refreshRuntimeMultiplayerPlayerIntentAuthority(state);auto callbacks=makeRuntimeMultiplayerRuntimeCallbacks(state,attachment);auto apply=callbacks.applyCommands;
 callbacks.applyCommands=[apply,&evidence](std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,std::string& e){if(!apply(batch,e))return false;evidence.applied(batch);return true;};
 auto advance=callbacks.advanceSimulation;callbacks.advanceSimulation=[advance,&evidence,&state](int tick){advance(tick);evidence.times.push_back(state.perf.lastSimulationMs);evidence.observe();};
 check(bridge.attachRuntime(std::move(callbacks),error),error);attachment.attached=true;refreshRuntimeMultiplayerPlayerIntentAuthority(state);
}
// Immutable input recipe uses the real native opening MCV and rules clock:
// DeployAll0, combat Move30, power-plant Queue50, combat Move80, Stop(final-40).
// Queue50 follows retail MCV rotation/transform (~39 ticks), without editing
// initial facing/ROT, foundation, prerequisites, build clocks or any game state.
std::optional<RuntimeMultiplayerGameplayCommand> fixedCommand(const RuntimeWindowState& state,bool host,int tick){
 if(tick!=0&&tick!=30&&tick!=50&&tick!=80&&tick!=finalTick()-40)return std::nullopt;
 RuntimeMultiplayerGameplayCommand command;
 if(tick==0){const auto* mcv=findObjectById(state,state.economy.skirmishStartObjectId);const auto* unit=mcv?findRuntimeUnitType(state.rules,*mcv):nullptr;
  check(mcv&&mcv->alive&&mcv->kind==WorldObjectKind::Unit&&runtimeCanonicalHumanHouseOwner(mcv->owner)==runtimeCanonicalHumanHouseOwner(state.economy.owner)&&unit&&runtimeUnitTypeIsMcv(state,*unit)&&runtimeMultiplayerCanonicalIdentifier(unit->deploysInto)==runtimeMultiplayerCanonicalIdentifier(host?"GACNST":"NACNST"),"original own startup MCV/DeploysInto is unavailable");
  RuntimeMultiplayerGameplaySecondaryActionCommand deploy;deploy.action=RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll;deploy.objectIds={mcv->instanceId};command.body=std::move(deploy);
 }else if(tick==50){RuntimeMultiplayerGameplayQueueProductionCommand queue;queue.typeId=host?"GAPOWR":"NAPOWR";command.body=std::move(queue);}
 else{
  std::vector<std::int32_t> actors;
  for(const auto& object:state.world.getObjects()){const auto* unit=object.alive&&object.kind==WorldObjectKind::Unit?findRuntimeUnitType(state.rules,object):nullptr;
   if(unit&&runtimeCanonicalHumanHouseOwner(object.owner)==runtimeCanonicalHumanHouseOwner(state.economy.owner)&&!runtimeUnitTypeIsMcv(state,*unit)&&unit->deploysInto.empty()&&!unit->harvester&&!unit->primaryWeapon.empty())actors.push_back(object.instanceId);}
  std::sort(actors.begin(),actors.end());check(!actors.empty(),"actual startup has no owned non-deploying combat vehicle");actors.resize(1);
  if(tick==finalTick()-40){RuntimeMultiplayerGameplayStopCommand stop;stop.objectIds=actors;command.body=std::move(stop);}
  else{const auto* actor=findObjectById(state,actors.front());check(actor,"native selected combat vehicle vanished");
   RuntimeMultiplayerGameplayTargetOrderCommand move;move.order=RuntimeMultiplayerGameplayTargetOrderKind::Move;move.objectIds=actors;// Authored neutral landmarks require the original queue (>18 cells),
   // unlike the former short four-cell greedy-only input. No path gate changes.
   const auto landmark=std::find_if(state.world.getObjects().begin(),state.world.getObjects().end(),[&](const auto& o){return o.alive&&o.type==(tick==80?"CAMACH":"CAAIRP");});
   check(landmark!=state.world.getObjects().end(),"actual map landmark missing");
   move.target=RuntimeMultiplayerGameplayCellTarget{{landmark->x,landmark->y}};command.body=std::move(move);}
 }
 return command;
}
void author(RuntimeWindowState& state,RuntimeMultiplayerRuntimeBridge& bridge,int tick,std::ofstream& out){
 auto command=fixedCommand(state,bridge.localPeerIsHost(),tick);if(!command)return;std::string error;
 check(bridge.captureGameplayCommand(*command,error),"fixed native input rejected: "+error);out<<tick<<'\t'<<static_cast<int>(runtimeMultiplayerGameplayCommandType(*command))<<'\n';out.flush();
}
RuntimeMultiplayerLobbyHostCreateOptions lobbyOptions(std::uint64_t fingerprint){
 RuntimeMultiplayerLobbyHostCreateOptions lobby;lobby.slotCount=8;lobby.hostMember.peerId="s37-host";lobby.hostMember.displayName="S37 Host";lobby.hostMember.role=RuntimeMultiplayerRoomMemberRole::Host;
 lobby.map={"multimd/2peaks.map",fingerprint};lobby.rules.gameMode="battle";lobby.rules.rulesOverride="mpbattlemd.ini";lobby.rules.randomSeed=Seed;lobby.rules.fixedTickHz=30;
 lobby.rules.startingCredits=10000;lobby.rules.startingUnits=10;lobby.rules.techLevel=10;lobby.rules.gameSpeed=4;lobby.rules.shortGame=false;lobby.rules.mcvRepacks=false;lobby.rules.crates=false;lobby.rules.superweapons=true;return lobby;
}
int run(){
 const auto option=[](const char* name){const auto* value=std::getenv(name);check(value&&*value,std::string("missing private option ")+name);return std::string(value);};
 const auto role=option("CONCORD_PATH_CANDIDATE_NETWORK_ROLE"),phase=option("CONCORD_PATH_CANDIDATE_NETWORK_PHASE");const bool host=role=="host";
 check(host||role=="client","invalid role");check(phase=="full"||phase=="export"||phase=="restore","invalid phase");
 auto dir=recordPath(option("CONCORD_PATH_CANDIDATE_NETWORK_DIR")),savePath=recordPath(option("CONCORD_PATH_CANDIDATE_NETWORK_SAVE")),configPath=recordPath(option("CONCORD_PATH_CANDIDATE_NETWORK_CONFIG"));(void)projectRoot();
 fs::create_directories(dir);check(!fs::exists(dir/(role+"-ticks.tsv"))&&!fs::exists(dir/"timing-only-summary.txt"),"output exists; choose a fresh S37 run directory");
 RuntimeConfigOverrides overrides;overrides.configPath=configPath.string();overrides.resourceRootPath=(projectRoot()/"RedAlert2").string();overrides.localDataRootPath=(projectRoot()/"local_data").string();overrides.defaultMapPath=(projectRoot()/"local_data/resources/mix_exports/multimd/2peaks.map").string();
 auto base=loadRuntimeConfig(overrides);RuntimeMultiplayerCompatibility compatibility;std::string error;check(makeRuntimeMultiplayerMenuCompatibility(base,compatibility,error),error);
 RuntimeMultiplayerContentComponent map;check(loadRuntimeMultiplayerContentComponentFile(base.defaultMapPath,"multimd/2peaks.map",map,error),error);std::uint64_t mapFingerprint=0;check(runtimeMultiplayerContentFingerprint({map},mapFingerprint,error),error);
 RuntimeMultiplayerResumeSave saved;if(phase=="restore"){check(loadRuntimeMultiplayerResumeSave(savePath,saved,error),"read original full Save: "+error);check(saved.compatibility==compatibility&&saved.setup.map.contentFingerprint==mapFingerprint,"save compatibility/map changed");}
 RuntimeMultiplayerLobbyFlow flow;const std::string roomId="s37-native-world-room";
 if(host){RuntimeMultiplayerLanHostOptions network;network.enableDiscovery=false;network.sessionPort=0;network.sessionBindScope=RuntimeTcpBindScope::LoopbackIpv4;network.roomName="S37 actual native world";
  network.session.roomId=roomId;network.session.hostPeerId="s37-host";network.session.hostDisplayName="S37 Host";network.session.compatibility=compatibility;network.session.stateHashPeriodTicks=1;
  check(flow.startHost(network,lobbyOptions(mapFingerprint),error,phase=="restore"?&saved:nullptr),error);textFile(dir/"port.txt",std::to_string(flow.bridge()->lan().sessionPort()));
 }else{int port=-1;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(port<=0&&std::chrono::steady_clock::now()<deadline){port=marker(dir/"port.txt");if(port<=0)Sleep(1);}check(port>0&&port<=65535,"actual host loopback port unavailable");
  RuntimeMultiplayerLanJoinOptions join;join.peerId="s37-client";join.displayName="S37 Client";join.compatibility=compatibility;join.room.roomId=roomId;join.room.roomName="S37 actual native world";join.room.hostName="S37 Host";join.room.hostAddress="127.0.0.1";join.room.sessionPort=static_cast<std::uint16_t>(port);join.room.contentFingerprint=hex(compatibility.contentFingerprint);join.room.protocolVersion=runtimeLanProtocolVersion;join.room.maximumPlayers=8;join.room.playerCount=1;check(flow.join(join,error),error);
 }
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120+finalTick());bool edited=phase=="restore",started=false,readyRequested=false;std::uint64_t publishedFinalRevision=0;
 while(flow.bridge()->lan().phase()!=RuntimeMultiplayerLanPhase::Running){check(std::chrono::steady_clock::now()<deadline,"real room admission/start timed out");auto result=flow.poll(1);check(result.succeeded,result.error);if(result.matchStarted||flow.bridge()->lan().phase()==RuntimeMultiplayerLanPhase::Running)break;
  if(!flow.canonicalSetupPublished()||flow.lobby().setup().members.size()!=2)continue;
  if(host&&!edited){auto edit=flow.setSlotOptions(0,"americans","red","-","medium",1,error);check(edit.accepted&&error.empty(),"host slot: "+error);edit=flow.setSlotOptions(1,"russians","blue","-","medium",2,error);check(edit.accepted&&error.empty(),"client slot: "+error);edited=true;}
  // Fix authored Ready input at the final canonical setup. The rendezvous
  // carries only the revision; all setup/ready/gameplay still uses real TCP.
  if(host&&edited&&publishedFinalRevision==0){publishedFinalRevision=flow.lobby().setup().revision;textFile(dir/"final-setup-revision.txt",std::to_string(publishedFinalRevision));}
  const int finalRevision=marker(dir/"final-setup-revision.txt");
  if(finalRevision<=0||flow.lobby().setup().revision!=static_cast<std::uint64_t>(finalRevision))continue;
  if(!flow.localMapVerified())check(flow.verifyLocalMap(base.defaultMapPath,error),error);
  // The original resume transfer is asynchronous. A verified map/setup does
  // not prove the complete saved image is ready; wait through the same API
  // that production setReady uses, without changing its deadline or checks.
  if(!flow.resumeReady(error))continue;
  const auto& members=flow.bridge()->lan().session().members();
  const auto local=std::find_if(members.begin(),members.end(),[&](const auto& member){return member.peerId==(host?"s37-host":"s37-client");});
  if(local!=members.end()&&!local->ready&&!readyRequested){const auto ready=flow.setReady(true);check(ready.succeeded,"ready rejected: "+ready.reason);readyRequested=true;}
  if(host&&!started&&flow.canStart(error)){const auto start=flow.startMatch(phase=="restore"?saved.descriptor.identity.snapshotTick+1:1);check(start.succeeded,"match start rejected: "+start.reason);started=true;}
 }
 RuntimeMultiplayerLaunchConfigResult launch;check(flow.makeLaunchConfig(base,launch,error),error);auto bridge=flow.takeRunningBridge(error);check(bridge&&bridge->beginPreAttachMaintenance(error),error);
 auto world=std::make_unique<RuntimeWindowState>();RuntimeSnapshotActiveStateScope active(*world);auto loadingBegin=std::chrono::steady_clock::now();initializeActualMap(*world,launch.config);double loadingMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-loadingBegin).count();
 // Original lan() synchronization handoff joins loading maintenance. Retain
 // its terminal state/cause before the normal participation validator runs.
 const bool maintenanceRunningBeforeHandoff=bridge->preAttachMaintenanceRunning();const auto maintenancePumps=bridge->preAttachMaintenancePumpCount();
 const auto handoffPhase=bridge->lan().phase();std::ostringstream handoff;
 handoff<<"phase="<<static_cast<int>(handoffPhase)<<" maintenanceWasRunning="<<maintenanceRunningBeforeHandoff<<" maintenancePumps="<<maintenancePumps<<" fatal="<<bridge->fatal()<<" reason="<<bridge->fatalReason()<<'\n';
 textFile(dir/(role+"-pre-attach.txt"),handoff.str());
 RuntimeMultiplayerRuntimeAttachmentState attachment;Evidence evidence(*world,dir,role);attachObserved(*world,*bridge,attachment,evidence);evidence.observe();std::ofstream authored(dir/(role+"-authored.tsv"));authored<<"captureCompletedTick\tcommandType\n";author(*world,*bridge,world->tick,authored);textFile(dir/(role+".input-ready"),std::to_string(world->tick));
 const int target=phase=="export"?SaveTick:finalTick();bool saveProposed=false,saveCommitted=phase=="restore";RuntimeMultiplayerSnapshotMemoryImage saveImage;
 while(true){check(std::chrono::steady_clock::now()<deadline,"actual world/save/hash proof timed out");const int before=world->tick;const bool peerReady=marker(dir/(host?"client.input-ready":"host.input-ready"))>=before;
  auto pump=bridge->pump(host&&before<target&&peerReady&&(before!=SaveTick||saveCommitted),1);check(pump.succeeded,"original bridge failed at tick "+std::to_string(world->tick)+": "+pump.error);
  if(world->tick!=before){check(world->tick==before+1,"pump committed more than one tick");author(*world,*bridge,world->tick,authored);textFile(dir/(role+".input-ready"),std::to_string(world->tick));}
  if(host&&phase!="restore"&&world->tick==SaveTick&&peerReady&&!saveProposed){auto identity=runtimeMultiplayerSnapshotSmokeIdentity(*world,1000,"s37-save-60",RuntimeMultiplayerSnapshotKind::Save);check(exportRuntimeMultiplayerSnapshotV15(*world,identity,saveImage,error),"original Save export: "+error);
   RuntimeMultiplayerSaveLoadProposal proposal;proposal.timelineEpoch=bridge->lan().session().timelineEpoch();proposal.proposalId=1000;proposal.kind=RuntimeMultiplayerSaveLoadKind::Save;proposal.proposerPeerId="s37-host";proposal.descriptor=saveImage.descriptor;proposal.tick=identity.snapshotTick;proposal.snapshotId=identity.snapshotToken;proposal.snapshotFingerprint=saveImage.descriptor.snapshotFingerprint;auto proposed=bridge->proposeSaveLoad(proposal,saveImage.bytes);check(proposed.succeeded,"original Save proposal: "+proposed.reason);saveProposed=true;}
  for(const auto& handshake:bridge->lan().session().saveLoadHandshakes())if(handshake.proposal.proposalId==1000&&handshake.status==RuntimeMultiplayerSaveLoadStatus::Committed)saveCommitted=true;
  if(host&&saveCommitted&&phase!="restore"&&!fs::exists(savePath))check(!saveImage.bytes.empty()&&writeRuntimeMultiplayerSnapshotFile(savePath.string(),saveImage.bytes,error),"committed original Save write: "+error);
  bool complete=false;for(const auto& frame:bridge->lan().session().stateHashFrames())if(frame.tick==target&&frame.complete&&!frame.desync)complete=true;
  check(world->tick<=target,"world passed fixed target");if(world->tick==target&&complete&&saveCommitted){textFile(dir/(role+".completed"),std::to_string(target));if(marker(dir/(host?"client.completed":"host.completed"))==target)break;}Sleep(1);
 }
 // Both peers must have left their final network pump before either closes.
 // The exact final hash is already complete; the following is only teardown.
 textFile(dir/(role+".close-ready"),std::to_string(target));
 auto closingDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(marker(dir/(host?"client.close-ready":"host.close-ready"))!=target){check(std::chrono::steady_clock::now()<closingDeadline,"peer did not reach bounded coordinated teardown");Sleep(1);}
 std::ostringstream summary;summary<<std::setprecision(17)<<"role="<<role<<" phase="<<phase<<" fixedFinalTick="<<world->tick<<" seed="<<Seed<<" sizeofLongDouble="<<sizeof(long double)<<" compiler="
#ifdef __clang__
 <<"clang-cl"
#else
 <<"msvc"
#endif
 <<" actualMap="<<base.defaultMapPath<<" mapFingerprint="<<mapFingerprint<<" protocolFingerprint="<<compatibility.protocolFingerprint<<" contentFingerprint="<<compatibility.contentFingerprint<<" loadingMs="<<loadingMs<<" observedTickCount="<<evidence.times.size()+1<<" saveCommitted="<<saveCommitted<<" initialHashConsensus="<<bridge->initialStateHashVerified()<<'\n';
 textFile(dir/(role+"-summary.txt"),summary.str());std::ostringstream times;times<<std::setprecision(17);for(double time:evidence.times)times<<time<<'\n';textFile(dir/(role+"-tick-times-ms.txt"),times.str());
 if(const auto* mode=std::getenv("CONCORD_PATH_CANDIDATE_MODE");mode&&std::string_view(mode)=="parallel")
  check(static_cast<bool>(world->pathCandidateRunner),"real parallel peer never entered the candidate queue");
 if(world->pathCandidateRunner){const auto& stats=world->pathCandidateRunner->stats();
  check(stats.adopted>0&&stats.mismatches==0&&stats.synchronousFallbacks==0,"real peer did not adopt candidate tasks cleanly");
  textFile(dir/(role+"-candidate-tasks.txt"),"adopted="+std::to_string(stats.adopted)+" submitted="+std::to_string(stats.submitted)+"\n");}
 bridge->detachRuntime();world->multiplayerIntentRouter.setCaptureCallback({});world->multiplayerBridge=nullptr;attachment.attached=false;bridge->lan().close();std::cout<<"S37 actual-product two-process proof passed: "<<summary.str();return 0;
}
// Actual map resources and production loading/reset/save/replay entries. Each
// map session is destroyed (stop/join) before loading the next real map.
int lifecycle() {
 const auto* configured=std::getenv("CONCORD_PATH_CANDIDATE_LIFECYCLE_OUTPUT");check(configured&&*configured,"private lifecycle output required");
 const auto dir=recordPath(configured);check(fs::create_directory(dir),"lifecycle output must be new");
 RuntimeConfigOverrides overrides;overrides.configPath=(dir.parent_path()/"lifecycle-private.ini").string();
 overrides.resourceRootPath=(projectRoot()/"RedAlert2").string();overrides.localDataRootPath=(projectRoot()/"local_data").string();
 auto base=loadRuntimeConfig(overrides);std::string error;std::size_t comparisons=0,adopted=0,replaySteps=0,replayCommands=0,replayResets=0;
 std::ofstream trace(dir/"lifecycle-authority.tsv");trace<<"map\ttick\troute\toracleRoute\tfullStandaloneHash\n";
 for(const std::string name:{"2peaks.map","xvalley.map"}) {
  std::cerr<<"S37 lifecycle map="<<name<<" start"<<std::endl;
  auto config=base;config.defaultMapPath=(projectRoot()/"local_data/resources/mix_exports/multimd"/name).string();
  config.computerAiEnabled=false;config.shortGame=false;config.crates=false;config.superweapons=false;
  // A real ordinary game: one local human, one idle hostile computer house.
  config.multiplayerActive=false;config.simulationRandomSeed=Seed;config.startingUnits=10;
  config.playerSlots.resize(8); // Authored ordinary eight-seat fixture input, never an assumed default.
  for(auto& slot:config.playerSlots)slot.enabled=false;
  config.playerSlots[0].enabled=true;config.playerSlots[0].human=true;config.playerSlots[0].local=true;config.playerSlots[0].country="Americans";config.playerSlots[0].startPosition=1;config.playerSlots[0].team="-";
  config.playerSlots[1].enabled=true;config.playerSlots[1].human=false;config.playerSlots[1].local=false;config.playerSlots[1].country="Russians";config.playerSlots[1].startPosition=2;config.playerSlots[1].team="-";
  const auto make=[&](int workers){auto world=std::make_unique<RuntimeWindowState>();world->pathCandidateRunnerChecked=true;
   RuntimeSnapshotActiveStateScope active(*world);initializeActualMap(*world,config);
   completeRuntimeWorldRefresh(*world);
   world->runtimeSoundsEnabled=false;world->runtimeSuppressPlayerAudioFeedback=true;
   world->pathCandidateRunnerChecked=true;
   if(workers>=0)world->pathCandidateRunner=std::make_unique<PathGoalCandidateRunner>(static_cast<std::size_t>(workers),true,concord::tasks::ExecutorConfig{},PathGoalCandidateMode::Parallel);
   return world;};
  std::array<std::unique_ptr<RuntimeWindowState>,5> states;states[0]=make(-1);states[1]=make(4);
  // Fixed legitimate input coordinates come from the actual startup MCV/yard,
  // not an arbitrary packed terrain record (which can be outside playable land).
  std::array<RuntimeCellPoint,2> destinations{};bool haveHome=false,haveAway=false;
  for(const auto& object:states[0]->world.getObjects()){
   if(!object.alive)continue;const auto* unit=object.kind==WorldObjectKind::Unit?findRuntimeUnitType(states[0]->rules,object):nullptr;
   const bool mcv=unit&&runtimeUnitTypeIsMcv(*states[0],*unit);
   const bool yard=object.kind==WorldObjectKind::Structure&&runtimeStructureProvidesFactoryForOwner(*states[0],object,object.owner,"BuildingType");
   if(!mcv&&!yard)continue;const bool own=runtimeCanonicalHumanHouseOwner(object.owner)==runtimeCanonicalHumanHouseOwner(states[0]->economy.owner);
   if(own&&!haveHome){destinations[0]=runtimeCellPointForObject(object);haveHome=true;}
   if(!own&&!haveAway){destinations[1]=runtimeCellPointForObject(object);haveAway=true;}
  }check(haveHome&&haveAway,"actual map lacks two native MCV/yard destinations");
  const auto hash=[&](RuntimeWindowState& world){RuntimeSnapshotActiveStateScope active(world);error.clear();const auto value=runtimeMatchReplayStateHash(world,error);check(value&&!error.size(),"lifecycle full authority: "+error);return value;};
  // Continuous/shadow-free reference and exact Replay Reset share route0.
  // Ordinary v14 deliberately rebuilds paths/poses/observations; compare its
  // formal route2 against a genuinely synchronous load of the same file4.
  const auto compare=[&](){for(std::size_t route=0;route<states.size();++route)if(states[route]){
   const std::size_t oracle=(route==2||route==4)?4U:0U;check(static_cast<bool>(states[oracle]),"matching workflow oracle unavailable");const auto expected=hash(*states[oracle]);
   const auto actual=hash(*states[route]);trace<<name<<'\t'<<states[route]->tick<<'\t'<<route<<'\t'<<oracle<<'\t'<<actual<<'\n';
   if(actual!=expected){std::cerr<<"lifecycle mismatch "<<runtimeMultiplayerSnapshotAuthorityDifferenceSummary(*states[oracle],*states[route])<<std::endl;
    for(const std::size_t which:{oracle,route}){RuntimeSnapshotActiveStateScope scope(*states[which]);std::string bytes,why;if(writeRuntimeMatchReplayWorld(*states[which],bytes,why))textFile(dir/(name+"-failure-route-"+std::to_string(which)+".ini"),bytes);}
   }
   check(actual==expected,"actual map/restore/replay authority mismatch "+name+" tick="+std::to_string(states[route]->tick)+" route="+std::to_string(route));if(route!=oracle)++comparisons;}};
  compare();
  RuntimeMatchReplayHeader fullHeader;fullHeader.initialTick=states[0]->tick;fullHeader.fixedTickHz=config.fixedTickHz;fullHeader.compatibility=37;fullHeader.initialHash=hash(*states[0]);fullHeader.config=serializeRuntimeMatchReplayConfig(states[0]->config);
  {RuntimeSnapshotActiveStateScope active(*states[0]);check(writeRuntimeMatchReplayWorld(*states[0],fullHeader.initialState,error),"real initial replay writer: "+error);}
  std::vector<RuntimeMatchReplayRecord> fullRecords;
  for(int tick=1;tick<=240;++tick){
   for(auto& owned:states)if(owned){auto& world=*owned;RuntimeSnapshotActiveStateScope active(world);
    // Actual player command and original path queue; alternate distant target
    // cells come from the authored terrain rather than fabricated map data.
    if(tick==1||tick==41||tick==81){std::vector<const WorldObjectInstance*> actors;
     for(const auto& object:world.world.getObjects())if(object.alive&&object.kind==WorldObjectKind::Unit&&runtimeCanonicalHumanHouseOwner(object.owner)==runtimeCanonicalHumanHouseOwner(world.economy.owner)){
      const auto* unit=findRuntimeUnitType(world.rules,object);if(unit&&!runtimeUnitTypeIsMcv(world,*unit)&&!unit->harvester)actors.push_back(&object);}
     std::sort(actors.begin(),actors.end(),[](const auto* a,const auto* b){return a->instanceId<b->instanceId;});check(!actors.empty(),"actual lifecycle map lacks moving actors");
     const auto goal=destinations[tick==41?0U:1U];world.selectedObjectIds={actors.front()->instanceId};
     RuntimeMultiplayerGameplayCommand command;RuntimeMultiplayerGameplayTargetOrderCommand move;move.order=RuntimeMultiplayerGameplayTargetOrderKind::Move;move.objectIds={actors.front()->instanceId};move.target=RuntimeMultiplayerGameplayCellTarget{{goal.x,goal.y}};command.body=std::move(move);
     const bool accepted=applyRuntimeMultiplayerGameplayCommand(world,world.economy.owner,command);
     if(!accepted||world.lastCommand.acceptedCount!=1){std::string why;const bool validated=prevalidateRuntimeMultiplayerGameplayCommand(world,world.economy.owner,command,why);
      std::cerr<<"lifecycle rejection map="<<name<<" tick="<<world.tick<<" owner="<<world.economy.owner<<" actor="<<actors.front()->instanceId<<" target="<<goal.x<<','<<goal.y<<" apiAccepted="<<accepted<<" count="<<world.lastCommand.acceptedCount<<" nativePrevalidation="<<validated<<" reason="<<why<<" status="<<world.simulationStatus<<std::endl;}
     check(accepted&&world.lastCommand.acceptedCount==1,"actual lifecycle move was rejected");
     if(owned.get()==states[0].get()){RuntimeMatchReplayRecord record;record.kind=RuntimeMatchReplayRecordKind::Command;record.tick=world.tick;record.owner=world.economy.owner;record.command=command;record.applied=accepted;fullRecords.push_back(std::move(record));}}

    ++world.tick;advanceRuntimeSimulation(world,false);completeRuntimeWorldRefresh(world);check(!world.skirmishMatchFinished,"actual lifecycle match ended before fixed boundary");
   }
   compare();
   {RuntimeMatchReplayRecord step;step.kind=RuntimeMatchReplayRecordKind::Step;step.tick=states[0]->tick;step.stateHash=hash(*states[0]);fullRecords.push_back(std::move(step));}
   if(tick==60){auto& original=*states[0];RuntimeSnapshotActiveStateScope active(original);
    const auto save=dir/(name+"-save60.ini");check(saveRuntimeState(original,save.string(),error),"real local save: "+error);
    std::string replay;check(writeRuntimeMatchReplayWorld(original,replay,error),"real replay write: "+error);const auto configText=serializeRuntimeMatchReplayConfig(original.config);const auto expected=hash(original);
    std::ostringstream recording(std::ios::binary);RuntimeMatchReplayWriter writer;RuntimeMatchReplayHeader header;
    header.initialTick=original.tick;header.fixedTickHz=original.config.fixedTickHz;header.compatibility=37;header.initialHash=expected;header.config=configText;header.initialState=replay;
    check(writer.begin(recording,header,error),error);RuntimeMatchReplayRecord reset;reset.kind=RuntimeMatchReplayRecordKind::Reset;reset.tick=original.tick;reset.stateHash=expected;reset.config=configText;reset.state=replay;check(writer.append(reset,error),error);fullRecords.push_back(reset);
    auto finish=reset;finish.kind=RuntimeMatchReplayRecordKind::Finish;finish.config.clear();finish.state.clear();check(writer.append(finish,error),error);textFile(dir/(name+"-reset.creplay"),recording.str());
    std::istringstream encoded(recording.str(),std::ios::binary);RuntimeMatchReplayReader reader;RuntimeMatchReplayHeader decoded;RuntimeMatchReplayRecord decodedReset;check(reader.begin(encoded,decoded,error)&&reader.next(decodedReset,error)&&decodedReset==reset,"real replay reader Reset: "+error);
    states[2]=make(2);{RuntimeSnapshotActiveStateScope scope(*states[2]);check(loadRuntimeState(*states[2],save.string(),error),"real local load: "+error);}
    states[3]=make(1);{RuntimeSnapshotActiveStateScope scope(*states[3]);check(restoreRuntimeMatchReplayWorld(*states[3],decodedReset.config,decodedReset.state,decodedReset.tick,decodedReset.stateHash,error),"real replay Reset: "+error);}
    states[4]=make(-1);{RuntimeSnapshotActiveStateScope scope(*states[4]);check(loadRuntimeState(*states[4],save.string(),error),"real synchronous ordinary load: "+error);}
    check(!states[4]->pathCandidateRunner&&states[4]->pathCandidateRunnerChecked,"ordinary load oracle must remain truly synchronous");
    textFile(dir/(name+"-ordinary-load-baseline.txt"),"sourceHash="+std::to_string(hash(original))+" syncLoadHash="+std::to_string(hash(*states[4]))+" parallelLoadHash="+std::to_string(hash(*states[2]))+"\ncontract=existing-v14-normalization; compare-matching-workflows-with-all-domains\n");
    compare();
   }
  }
  // Genuine streamed replay: original codec + original command dispatcher,
  // fixed step and state Reset. The removed recorder/viewer UI is not restored.
  RuntimeMatchReplayRecord finish;finish.kind=RuntimeMatchReplayRecordKind::Finish;finish.tick=states[0]->tick;finish.stateHash=hash(*states[0]);fullRecords.push_back(finish);
  std::ostringstream recording(std::ios::binary);RuntimeMatchReplayWriter fullWriter;check(fullWriter.begin(recording,fullHeader,error),error);for(const auto& record:fullRecords)check(fullWriter.append(record,error),error);
  const auto file=dir/(name+"-full.creplay");textFile(file,recording.str());
  std::istringstream preflight(recording.str(),std::ios::binary);RuntimeMatchReplayHeader validated;std::uint64_t stepCount=0,commandCount=0;
  check(validateRuntimeMatchReplay(preflight,validated,stepCount,commandCount,error)&&validated==fullHeader&&stepCount==240&&commandCount==3,"complete replay preflight: "+error);
  auto played=make(2);RuntimeSnapshotActiveStateScope active(*played);std::istringstream stream(recording.str(),std::ios::binary);RuntimeMatchReplayReader reader;RuntimeMatchReplayHeader decoded;
  check(reader.begin(stream,decoded,error)&&restoreRuntimeMatchReplayWorld(*played,decoded.config,decoded.initialState,decoded.initialTick,decoded.initialHash,error),"real replay initial: "+error);
  std::ofstream replayTrace(dir/(name+"-replay.tsv"));replayTrace<<"kind\ttick\tfullHash\n";
  while(!reader.finished()){
   RuntimeMatchReplayRecord record;check(reader.next(record,error),"real replay next: "+error);
   switch(record.kind){
   case RuntimeMatchReplayRecordKind::Command:check(played->tick==record.tick&&applyRuntimeMultiplayerGameplayCommand(*played,record.owner,record.command)==record.applied,"real replay command result differs");++replayCommands;break;
   case RuntimeMatchReplayRecordKind::Step:check(record.tick==played->tick+1,"real replay step ordering");++played->tick;advanceRuntimeSimulation(*played,record.lightweight);completeRuntimeWorldRefresh(*played);++replaySteps;break;
   case RuntimeMatchReplayRecordKind::Reset:check(restoreRuntimeMatchReplayWorld(*played,record.config,record.state,record.tick,record.stateHash,error),"real replay Reset: "+error);++replayResets;break;
   case RuntimeMatchReplayRecordKind::Finish:check(played->tick==record.tick,"real replay Finish tick");break;
   default:check(false,"unexpected record in authored real replay");
   }
   const auto actual=hash(*played);replayTrace<<static_cast<int>(record.kind)<<'\t'<<played->tick<<'\t'<<actual<<'\n';
   if(record.stateHash)check(actual==record.stateHash,"real replay complete authority differs tick="+std::to_string(played->tick));
  }
  replayTrace.close();check(static_cast<bool>(replayTrace)&&played->tick==240&&hash(*played)==hash(*states[0]),"real replay full continuation incomplete");
  check(played->pathCandidateRunner&&played->pathCandidateRunner->stats().adopted>0&&played->pathCandidateRunner->executorStats().inFlightJobs==0,"real replay did not consume complete parallel results");adopted+=static_cast<std::size_t>(played->pathCandidateRunner->stats().adopted);
  for(const std::size_t route:{1U,2U,3U}){check(states[route]&&states[route]->pathCandidateRunner,"lifecycle runner lost during load");const auto& stats=states[route]->pathCandidateRunner->stats();check(stats.adopted>0&&stats.unavailable==0,"actual lifecycle route did not adopt complete tasks");adopted+=static_cast<std::size_t>(stats.adopted);}
 }trace.close();check(static_cast<bool>(trace),"lifecycle trace write failed");
 textFile(dir/"summary.txt","maps=2peaks.map,xvalley.map realSessionDestruction=yes save=14 replay=1 reset=real completeAuthorityPairs="+std::to_string(comparisons)+" adopted="+std::to_string(adopted)+" replaySteps="+std::to_string(replaySteps)+" replayCommands="+std::to_string(replayCommands)+" replayResets="+std::to_string(replayResets)+"\n");
 std::cout<<"S37 actual map switch/save/load/replay passed pairs="<<comparisons<<" adopted="<<adopted<<'\n';return 0;
}

}