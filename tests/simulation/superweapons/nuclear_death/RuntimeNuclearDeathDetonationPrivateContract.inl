namespace h012_private_qualification {
namespace original {
#define detonateRuntimeNuclearDeathWeapon reference_detonateRuntimeNuclearDeathWeapon
#include "RuntimeNuclearDeathDetonationOriginal.inl"
#undef detonateRuntimeNuclearDeathWeapon
}
std::string valueAbi(){RuntimeProjectileEffect p;RuntimeProjectileTargetPosition t;RuntimeVoxelDebrisState v;std::ostringstream o;
o<<"ABI "<<typeid(p).name()<<':'<<sizeof(p)<<'/'<<alignof(decltype(p))<<" lifecycle="<<sizeof(RuntimeProjectileLifecycleState)<<'/'<<alignof(RuntimeProjectileLifecycleState)<<" position="<<typeid(t).name()<<':'<<sizeof(t)<<'/'<<alignof(decltype(t))<<" voxel="<<typeid(v).name()<<':'<<sizeof(v)<<'/'<<alignof(decltype(v));
const auto off=[](const auto& owner,const auto& member){return reinterpret_cast<std::uintptr_t>(&member)-reinterpret_cast<std::uintptr_t>(&owner);};
o<<"|voxelDebris="<<off(p,p.voxelDebris);
o<<"|homingInitialized="<<off(p,p.homingInitialized);
o<<"|homingYaw="<<off(p,p.homingYaw);
o<<"|homingPitch="<<off(p,p.homingPitch);
o<<"|actorId="<<off(p,p.actorId);
o<<"|parentActorId="<<off(p,p.parentActorId);
o<<"|generation="<<off(p,p.generation);
o<<"|attackerObjectId="<<off(p,p.attackerObjectId);
o<<"|targetObjectId="<<off(p,p.targetObjectId);
o<<"|sourceAnchorObjectId="<<off(p,p.sourceAnchorObjectId);
o<<"|owner="<<off(p,p.owner);
o<<"|fromCell="<<off(p,p.fromCell);
o<<"|toCell="<<off(p,p.toCell);
o<<"|currentCell="<<off(p,p.currentCell);
o<<"|hasSourcePosition="<<off(p,p.hasSourcePosition);
o<<"|sourceX="<<off(p,p.sourceX);
o<<"|sourceY="<<off(p,p.sourceY);
o<<"|sourceZ="<<off(p,p.sourceZ);
o<<"|sourceScreenOffsetX="<<off(p,p.sourceScreenOffsetX);
o<<"|sourceScreenOffsetY="<<off(p,p.sourceScreenOffsetY);
o<<"|sourceNativeFixedX="<<off(p,p.sourceNativeFixedX);
o<<"|sourceNativeFixedY="<<off(p,p.sourceNativeFixedY);
o<<"|hasTargetPosition="<<off(p,p.hasTargetPosition);
o<<"|targetX="<<off(p,p.targetX);
o<<"|targetY="<<off(p,p.targetY);
o<<"|targetZ="<<off(p,p.targetZ);
o<<"|fromLevel="<<off(p,p.fromLevel);
o<<"|toLevel="<<off(p,p.toLevel);
o<<"|currentLevel="<<off(p,p.currentLevel);
o<<"|attackerFacing="<<off(p,p.attackerFacing);
o<<"|targetFacing="<<off(p,p.targetFacing);
o<<"|spawnTick="<<off(p,p.spawnTick);
o<<"|impactTick="<<off(p,p.impactTick);
o<<"|elapsedTicks="<<off(p,p.elapsedTicks);
o<<"|damage="<<off(p,p.damage);
o<<"|veteranDamageMultiplier="<<off(p,p.veteranDamageMultiplier);
o<<"|spawnedMissileDamage="<<off(p,p.spawnedMissileDamage);
o<<"|spawnedMissileBaseDamage="<<off(p,p.spawnedMissileBaseDamage);
o<<"|totalTicks="<<off(p,p.totalTicks);
o<<"|remainingTicks="<<off(p,p.remainingTicks);
o<<"|lifecycle="<<off(p,p.lifecycle);
o<<"|lifecycleState="<<off(p,p.lifecycleState);
o<<"|renderIntent="<<off(p,p.renderIntent);
o<<"|weaponId="<<off(p,p.weaponId);
o<<"|weaponSlot="<<off(p,p.weaponSlot);
o<<"|projectileId="<<off(p,p.projectileId);
o<<"|projectileImageId="<<off(p,p.projectileImageId);
o<<"|warheadId="<<off(p,p.warheadId);
o<<"|weaponAnimId="<<off(p,p.weaponAnimId);
o<<"|impactAnimId="<<off(p,p.impactAnimId);
o<<"|trailerAnimId="<<off(p,p.trailerAnimId);
o<<"|trailerIntentId="<<off(p,p.trailerIntentId);
o<<"|warheadImpactIntentId="<<off(p,p.warheadImpactIntentId);
o<<"|reportSoundId="<<off(p,p.reportSoundId);
o<<"|arcing="<<off(p,p.arcing);
o<<"|vertical="<<off(p,p.vertical);
o<<"|acceleration="<<off(p,p.acceleration);
o<<"|high="<<off(p,p.high);
o<<"|veryHigh="<<off(p,p.veryHigh);
o<<"|shadow="<<off(p,p.shadow);
o<<"|animates="<<off(p,p.animates);
o<<"|aa="<<off(p,p.aa);
o<<"|ag="<<off(p,p.ag);
o<<"|cluster="<<off(p,p.cluster);
o<<"|bright="<<off(p,p.bright);
o<<"|laser="<<off(p,p.laser);
o<<"|houseColorLaser="<<off(p,p.houseColorLaser);
o<<"|electricBolt="<<off(p,p.electricBolt);
o<<"|alternateElectricColor="<<off(p,p.alternateElectricColor);
o<<"|electricColor="<<off(p,p.electricColor);
o<<"|shrapnel="<<off(p,p.shrapnel);
o<<"|shrapnelReleased="<<off(p,p.shrapnelReleased);
o<<"|laserDuration="<<off(p,p.laserDuration);
o<<"|laserThickness="<<off(p,p.laserThickness);
o<<"|cellSpread="<<off(p,p.cellSpread);
o<<"|percentAtMax="<<off(p,p.percentAtMax);
o<<"|affectsAllies="<<off(p,p.affectsAllies);
o<<"|affectsEnemies="<<off(p,p.affectsEnemies);
o<<"|particleId="<<off(p,p.particleId);
o<<"|progress="<<off(p,p.progress);
o<<"|currentX="<<off(p,p.currentX);
o<<"|currentY="<<off(p,p.currentY);
o<<"|currentZ="<<off(p,p.currentZ);
o<<"|speedCellsPerTick="<<off(p,p.speedCellsPerTick);
o<<"|rot="<<off(p,p.rot);
o<<"|deathWeaponCascadeId="<<off(p,p.deathWeaponCascadeId);
o<<"|deathWeaponRequestId="<<off(p,p.deathWeaponRequestId);
o<<"|deathWeaponCascadeDepth="<<off(p,p.deathWeaponCascadeDepth);
o<<"|launchSourceKind="<<off(p,p.launchSourceKind);
o<<"|launchSourceType="<<off(p,p.launchSourceType);
o<<"|launchSourceAuthoredAircraft="<<off(p,p.launchSourceAuthoredAircraft);
o<<"|launchSourceValidated="<<off(p,p.launchSourceValidated);
o<<"|airstrikeStrikeId="<<off(p,p.airstrikeStrikeId);
o<<"|airstrikePlaneToken="<<off(p,p.airstrikePlaneToken);
o<<"|particleDamagePulse="<<off(p,p.particleDamagePulse);
o<<"|suppressWarheadParticleSpawn="<<off(p,p.suppressWarheadParticleSpawn);
o<<"|position="<<off(t,t.cell)<<','<<off(t,t.x)<<','<<off(t,t.y)<<','<<off(t,t.level)<<"|voxel=";o<<"enabled="<<off(v,v.enabled)<<',';o<<"framesLeft="<<off(v,v.framesLeft)<<',';o<<"frameRemainder="<<off(v,v.frameRemainder)<<',';o<<"frameCount="<<off(v,v.frameCount)<<',';o<<"bounces="<<off(v,v.bounces)<<',';o<<"elasticity="<<off(v,v.elasticity)<<',';o<<"position="<<off(v,v.position)<<',';o<<"velocity="<<off(v,v.velocity)<<',';o<<"orientation="<<off(v,v.orientation)<<',';o<<"spin="<<off(v,v.spin)<<',';return o.str();}
std::string exercise(bool reference,const std::string& mapPath,int& checks){const auto require=[&](bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);};std::ostringstream image;image<<valueAbi()<<'\n';for(int scenario=0;scenario<6;++scenario){auto box=std::make_unique<IndependentRecoveryHost>();auto& s=*box;std::string error;const bool ready=configureRuntimeDetailSnapshotSmokeWorld(s,false,error);require(ready,"private scene setup "+error);RuntimeSnapshotActiveStateScope scope(s);
s.config.defaultMapPath=mapPath;s.config.multiplayerActive=false;s.config.computerAiEnabled=false;s.config.gameMode="Battle";s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;s.tick=100;s.config.fixedTickHz=scenario==3?15:scenario==5?60:30;s.economy.owner="Alpha";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;s.skirmishModeRulesLoaded=true;s.skirmishModeAlliesAllowed=true;s.skirmishTeamByOwner={{"alpha","1"},{"beta","2"}};
IniFile rules;require(rules.loadFromString(R"N12([VehicleTypes]
0=SNAPSHOT_TANK
[BuildingTypes]
0=SNAPSHOT_LANDMARK
[WeaponTypes]
0=H012_PAYLOAD
[ProjectileTypes]
0=H012_MISSILE
[Warheads]
0=H012_NUKE
[SNAPSHOT_TANK]
Primary=H012_PAYLOAD
Strength=200
Armor=heavy
Speed=6
SpeedType=Track
MovementZone=Normal
[SNAPSHOT_LANDMARK]
Strength=1000
Armor=wood
Foundation=1x1
[H012_PAYLOAD]
Damage=30
ROF=10
Range=5
Projectile=H012_MISSILE
Warhead=H012_NUKE
[H012_MISSILE]
Image=NKMSLDN
AG=yes
AA=no
[H012_NUKE]
CellSpread=5
PercentAtMax=1
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
AffectsAllies=yes
AffectsEnemies=yes
)N12")&&s.rules.loadFromIni(rules),"real producer rules");s.rulesLoaded=true;s.particles.reset(s.config.fixedTickHz);require(s.particles.alignEmptyStateTick(s.tick),"empty particle owner configured for scene clock");s.particlesInitialized=true;require(s.rules.findWarheadType("H012_NUKE")!=nullptr&&s.rules.findWeaponType("H012_PAYLOAD")!=nullptr,"typed warhead/weapon owners loaded wh="+std::to_string(s.rules.findWarheadType("H012_NUKE")!=nullptr)+" weapon="+std::to_string(s.rules.findWeaponType("H012_PAYLOAD")!=nullptr));
s.world.accessObjects().clear();s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();s.impactEffects.clear();s.pendingObjectExits.clear();s.deathWeapons.reset();for(auto& t:s.world.accessTerrainCells()){t.tileIndex=0;t.subTileIndex=0;}s.structureFootprints["SNAPSHOT_LANDMARK"]={1,1,true};rebuildRuntimeTerrainInfoCache(s);initializeRuntimeNavigation(s);
const auto add=[&](const std::string& owner,RuntimeBuildKind kind,const std::string& type,const RuntimeCellPoint& cell){auto* p=addRuntimeObjectInstanceForOwner(s,owner,kind,type,cell);require(p!=nullptr,"actual object producer "+type);return p->instanceId;};
const int sourceId=add("Alpha",RuntimeBuildKind::Building,"SNAPSHOT_LANDMARK",{5,5}),friendId=add("Alpha",RuntimeBuildKind::Unit,"SNAPSHOT_TANK",{6,5}),enemyId=add("Beta",RuntimeBuildKind::Unit,"SNAPSHOT_TANK",{7,5});refreshRuntimeObjectOccupancyAfterLocalChange(s);
auto* source=findMutableObjectById(s,sourceId);require(source!=nullptr,"actual source identity");source->alive=false;source->health=0;if(scenario==4){source->hasRenderPosition=true;source->renderX=5.25;source->renderY=5.5;}
RuntimeProjectileEffect p;p.actorId=8768;p.attackerObjectId=sourceId;p.targetObjectId=enemyId;p.sourceAnchorObjectId=sourceId;p.owner="Alpha";p.weaponId="H012_PAYLOAD";p.projectileId="H012_MISSILE";p.projectileImageId="NKMSLDN";p.warheadId="H012_NUKE";p.damage=30;p.cellSpread=5;p.percentAtMax=1;p.ag=true;p.affectsAllies=false;p.affectsEnemies=false;p.deathWeaponCascadeId=1;p.deathWeaponRequestId=1;p.launchSourceValidated=true;p.launchSourceKind=static_cast<int>(WorldObjectKind::Structure);p.launchSourceType="SNAPSHOT_LANDMARK";p.lifecycle=RuntimeProjectileLifecycleState::Traveling;p.lifecycleState="Traveling";p.sourceScreenOffsetX=9;p.sourceScreenOffsetY=-7;p.sourceNativeFixedX=8;p.sourceNativeFixedY=-5;p.totalTicks=87;p.remainingTicks=31;p.elapsedTicks=6;p.progress=0.25;p.speedCellsPerTick=3.5;
if(scenario==0)p.deathWeaponRequestId=0;else if(scenario==1)p.parentActorId=4;else if(scenario==2)p.generation=1;
s.actionSummary.projectilesInFlight=91;const auto before=serializeRuntimeProjectileEffect(p);const auto center=runtimeProjectileTargetPosition(s,*source);const int friendlyHealth=findObjectById(s,friendId)->health,enemyHealth=findObjectById(s,enemyId)->health;
const bool changed=reference?original::reference_detonateRuntimeNuclearDeathWeapon(s,p,*source):detonateRuntimeNuclearDeathWeapon(s,p,*source);
if(scenario<3){require(!changed&&serializeRuntimeProjectileEffect(p)==before,"rejected projectile output untouched");require(s.actionSummary.projectilesInFlight==91&&findObjectById(s,friendId)->health==friendlyHealth&&findObjectById(s,enemyId)->health==enemyHealth,"rejected state no impact/counter mutation");}
else{require(changed,"actual positive synchronous impact");require(findObjectById(s,friendId)->health==friendlyHealth-30&&findObjectById(s,enemyId)->health==enemyHealth-30,"actual friendly/enemy damage through retained impact owner");require(s.projectiles.empty()&&s.actionSummary.projectilesInFlight==0,"no retained traveling actor/current count");require(p.lifecycle==RuntimeProjectileLifecycleState::Expired&&p.lifecycleState=="Expired"&&p.totalTicks==1&&p.remainingTicks==0&&p.elapsedTicks==1&&p.progress==1.0&&p.speedCellsPerTick==0.0,"exact synchronous lifecycle/ticks");require(p.spawnTick==s.tick&&p.impactTick==s.tick,"original same tick");require(p.targetObjectId==-1&&p.sourceAnchorObjectId==-1&&p.fromCell==center.cell&&p.toCell==center.cell&&p.currentCell==center.cell,"original center identity");require(p.fromLevel==center.level&&p.toLevel==center.level&&p.currentLevel==center.level&&p.sourceX==center.x&&p.targetX==center.x&&p.currentX==center.x&&p.sourceY==center.y&&p.targetY==center.y&&p.currentY==center.y&&p.sourceZ==center.level&&p.targetZ==center.level&&p.currentZ==center.level,"actual original owned target position");require(p.hasSourcePosition&&p.hasTargetPosition&&p.sourceScreenOffsetX==0&&p.sourceScreenOffsetY==0&&p.sourceNativeFixedX==0&&p.sourceNativeFixedY==0&&p.affectsAllies&&p.affectsEnemies,"original geometry/source defaults and affiliation");}
std::ostringstream full;const bool saved=writeRuntimeStateToStream(s,full,error);require(saved,"actual complete save "+error);image<<"scenario="<<scenario<<" projectile="<<serializeRuntimeProjectileEffect(p)<<"\n"<<full.str();}
return image.str();}
int run(const std::string& mapPath){try{int checks=0;const auto before=exercise(true,mapPath,checks),after=exercise(false,mapPath,checks);++checks;if(before!=after)throw std::runtime_error("original/projectile/fullRoot save differ");std::cout<<valueAbi()<<'\n'<<"private-nuclear-death checks="<<checks<<" actual-root-states=12 positive-friendly-enemy=yes three-rates=yes full-projectile-and-save=exact failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
}
