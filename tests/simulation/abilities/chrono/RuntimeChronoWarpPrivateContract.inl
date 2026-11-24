// Test-only frozen original handler and real production caller qualification.
namespace h02305_private_qualification {
using namespace concord::runtime::session_detail;
using h023_private_qualification::encode;
int checks=0,states=0,worldProbes=0,positiveTimeline=0,positiveBoarding=0,positiveMiner=0,silentCalls=0;
std::string where;
void require(bool v,const std::string& why){++checks;if(!v)throw std::runtime_error(where+why);}
namespace original {
void spawnRuntimeChronoWarpEffect(
    RuntimeProductionHostState& state, const SpecialMechanismEvent& event, bool playSound) {
    WorldObjectInstance* object = findMutableObjectById(state, event.objectId);
    if (object == nullptr || !object->alive) return;
    const bool reinforcement = event.kind == "ChronoReinforcementIn";
    const bool arriving = reinforcement || event.kind == "ChronoWarpIn";
    const auto boarding = arriving && !reinforcement
        ? runtimeChronoTransportDestinationAdmission(state, *object, runtimeCellPointForObject(*object))
        : std::optional<bool>{};
    const auto pendingHost = state.pendingTransportTargets.find(object->instanceId);
    const auto* boardingHost = boarding && *boarding && pendingHost != state.pendingTransportTargets.end()
        ? findObjectById(state, pendingHost->second) : nullptr;
    if (arriving) {
        // The standalone mechanism uses a packed 512-wide cell identifier;
        // runtime occupancy and saves use the canonical runtime cell key.
        object->cell = runtimeCellKey(object->x, object->y);
        object->onBridge = runtimeCellIsBridge(state, object->x, object->y);
        if(runtimeInfantryUsesGroundSubcells(*object)) {
            releaseRuntimeInfantrySubcellReservation(state,object->instanceId);
            if (boardingHost) object->subCell = -1;
            else assignRuntimeInfantrySubcellAtCurrentLocation(state,*object);
        }
        clearRuntimeRenderPosition(*object);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        invalidateRuntimeObjectProjectionCache(state);
    }
    const auto& rules = state.rules.getSpecialMechanismRules();
    RuntimeImpactEffect effect;
    effect.attackerObjectId = object->instanceId;
    effect.targetObjectId = object->instanceId;
    effect.owner = object->owner;
    effect.cell = {object->x, object->y};
    effect.level = object->level;
    effect.hasWorldPosition = true;
    const auto offset = worldObjectSubCellOffset(*object);
    effect.worldX = (object->hasRenderPosition ? object->renderX : object->x) + offset.x;
    effect.worldY = (object->hasRenderPosition ? object->renderY : object->y) + offset.y;
    effect.worldLevel = object->hasRenderPosition ? object->renderLevel : object->level;
    if (boardingHost) {
        const auto hull = runtimeTransportMotionBody(*boardingHost);
        effect.worldX = hull.x; effect.worldY = hull.y; effect.worldLevel = hull.level;
    }
    effect.facing = object->facing;
    effect.lifecycleState = reinforcement ? "ChronoReinforcementIn"
        : arriving ? "ChronoWarpIn" : "ChronoWarpOut";
    effect.renderIntent = "chronoWarp";
    // Retail TeleportLocomotionClass uses Rules::WarpOut at BOTH ends.
    // WarpIn is a separate, red animation used by chrono reinforcements.
    effect.animId = reinforcement ? rules.warpInAnim : rules.warpOutAnim;
    if (!effect.animId.empty() && toLowerAsciiLocal(effect.animId) != "none") {
        initializeRuntimeImpactAnimationTimeline(state, effect, true);
        state.impactEffects.push_back(std::move(effect));
    }
    if (playSound) {
        const UnitType* unit = findRuntimeUnitType(state.rules, *object);
        std::string sound = unit == nullptr ? std::string()
            : (arriving ? unit->chronoInSound : unit->chronoOutSound);
        if (trimAsciiLocal(sound).empty()) sound = arriving ? rules.chronoInSound : rules.chronoOutSound;
        if (!trimAsciiLocal(sound).empty()) emitRuntimeSound(state, "sound:" + trimAsciiLocal(sound), runtimeSpatialAudioPositionForObject(*object));
    }
}
// Bind only the frozen miner reference calls; original body text remains literal.
#define spawnRuntimeChronoWarpEffect h02305_private_qualification::original::spawnRuntimeChronoWarpEffect
bool teleportRuntimeHarvesterToCell(
    RuntimeProductionHostState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& target,
    int* teleportCounter) {
    if (!handleRuntimeParasiteHostEvent(
            state,
            harvester.instanceId,
            ParasiteHostEventKind::Teleporting,
            std::max(1, state.config.fixedTickHz)
        )) {
        return false;
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, harvester);
    if (unit != nullptr && !trimAsciiLocal(unit->chronoOutSound).empty()) {
        emitRuntimeSound(
            state,
            "sound:" + trimAsciiLocal(unit->chronoOutSound)
        );
    }
    spawnRuntimeChronoWarpEffect(state, {"ChronoWarpOut", state.tick, harvester.instanceId,
        -1, 0, {harvester.x, harvester.y}, "chrono miner return"}, false);
    state.behaviors.erase(harvester.instanceId);
    harvester.x = target.x;
    harvester.y = target.y;
    harvester.cell = runtimeCellKey(target.x, target.y);
    harvester.level = terrainLevelAtRuntimeCell(state, target.x, target.y);
    harvester.onBridge = runtimeCellIsBridge(state, target.x, target.y);
    clearRuntimeRenderPosition(harvester);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    invalidateRuntimeObjectProjectionCache(state);
    ++(teleportCounter ? *teleportCounter : state.economy.harvesterTeleports);
    spawnRuntimeChronoWarpEffect(state, {"ChronoWarpIn", state.tick, harvester.instanceId,
        -1, 0, {harvester.x, harvester.y}, "chrono miner return"}, false);
    if (unit != nullptr && !trimAsciiLocal(unit->chronoInSound).empty()) {
        emitRuntimeSound(
            state,
            "sound:" + trimAsciiLocal(unit->chronoInSound)
        );
    }
    return true;
}
#undef spawnRuntimeChronoWarpEffect
}
void verifyWorldEncoder(){WorldObjectInstance zero;const auto before=encode(zero);
{auto v=zero;h023_private_qualification::mutateEncoderField(v.instanceId);require(encode(v)!=before,"world field instanceId omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.kind);require(encode(v)!=before,"world field kind omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.mapId);require(encode(v)!=before,"world field mapId omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.owner);require(encode(v)!=before,"world field owner omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.type);require(encode(v)!=before,"world field type omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.health);require(encode(v)!=before,"world field health omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.x);require(encode(v)!=before,"world field x omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.y);require(encode(v)!=before,"world field y omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.cell);require(encode(v)!=before,"world field cell omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.level);require(encode(v)!=before,"world field level omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.facing);require(encode(v)!=before,"world field facing omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.turretFacing);require(encode(v)!=before,"world field turretFacing omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.bodyFacingClock);require(encode(v)!=before,"world field bodyFacingClock omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.turretFacingClock);require(encode(v)!=before,"world field turretFacingClock omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.subCell);require(encode(v)!=before,"world field subCell omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.mission);require(encode(v)!=before,"world field mission omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.tag);require(encode(v)!=before,"world field tag omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.veterancy);require(encode(v)!=before,"world field veterancy omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.veterancyExperience);require(encode(v)!=before,"world field veterancyExperience omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.crateArmorMultiplier);require(encode(v)!=before,"world field crateArmorMultiplier omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.crateFirepowerMultiplier);require(encode(v)!=before,"world field crateFirepowerMultiplier omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.crateSpeedMultiplier);require(encode(v)!=before,"world field crateSpeedMultiplier omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.crateCloakable);require(encode(v)!=before,"world field crateCloakable omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.harvestAutoSearchSuspended);require(encode(v)!=before,"world field harvestAutoSearchSuspended omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.hasBeenCaptured);require(encode(v)!=before,"world field hasBeenCaptured omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.onBridge);require(encode(v)!=before,"world field onBridge omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.authoredAircraft);require(encode(v)!=before,"world field authoredAircraft omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.alive);require(encode(v)!=before,"world field alive omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.rubble);require(encode(v)!=before,"world field rubble omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.submerged);require(encode(v)!=before,"world field submerged omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.cloaked);require(encode(v)!=before,"world field cloaked omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.cloakCooldownTicks);require(encode(v)!=before,"world field cloakCooldownTicks omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.submergeCooldownTicks);require(encode(v)!=before,"world field submergeCooldownTicks omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.deployed);require(encode(v)!=before,"world field deployed omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.deployStateTicksRemaining);require(encode(v)!=before,"world field deployStateTicksRemaining omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.prone);require(encode(v)!=before,"world field prone omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.proneTicksRemaining);require(encode(v)!=before,"world field proneTicksRemaining omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.weaponCooldownTicksRemaining);require(encode(v)!=before,"world field weaponCooldownTicksRemaining omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.spriteFiringStartTick);require(encode(v)!=before,"world field spriteFiringStartTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.radiationEruptionCooldownTicksRemaining);require(encode(v)!=before,"world field radiationEruptionCooldownTicksRemaining omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.hasRenderPosition);require(encode(v)!=before,"world field hasRenderPosition omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.renderX);require(encode(v)!=before,"world field renderX omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.renderY);require(encode(v)!=before,"world field renderY omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.renderMoveActive);require(encode(v)!=before,"world field renderMoveActive omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.renderLevel);require(encode(v)!=before,"world field renderLevel omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.renderOnBridge);require(encode(v)!=before,"world field renderOnBridge omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.movementAnimationDistance);require(encode(v)!=before,"world field movementAnimationDistance omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.buildAnimTicksTotal);require(encode(v)!=before,"world field buildAnimTicksTotal omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.buildAnimTicksRemaining);require(encode(v)!=before,"world field buildAnimTicksRemaining omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.structureRecoilStartTick);require(encode(v)!=before,"world field structureRecoilStartTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.chargeTurretStartTick);require(encode(v)!=before,"world field chargeTurretStartTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.workAnimationActive);require(encode(v)!=before,"world field workAnimationActive omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.poweredOperational);require(encode(v)!=before,"world field poweredOperational omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.powerAnimationPausedAtTick);require(encode(v)!=before,"world field powerAnimationPausedAtTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.powerAnimationPausedTicks);require(encode(v)!=before,"world field powerAnimationPausedTicks omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.factoryExitAnimationStartTick);require(encode(v)!=before,"world field factoryExitAnimationStartTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.factoryExitAnimationEndTick);require(encode(v)!=before,"world field factoryExitAnimationEndTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.factoryExitAirborne);require(encode(v)!=before,"world field factoryExitAirborne omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.factoryExitHostId);require(encode(v)!=before,"world field factoryExitHostId omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.specialCashNextTick);require(encode(v)!=before,"world field specialCashNextTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.specialCloneDeliveryObjectId);require(encode(v)!=before,"world field specialCloneDeliveryObjectId omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.transportPassengerIds);require(encode(v)!=before,"world field transportPassengerIds omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.bioReactorAnimationStartTick);require(encode(v)!=before,"world field bioReactorAnimationStartTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.bioReactorAnimationPoweredStartTick);require(encode(v)!=before,"world field bioReactorAnimationPoweredStartTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.activeGunnerMode);require(encode(v)!=before,"world field activeGunnerMode omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.activeGunnerWeaponIndex);require(encode(v)!=before,"world field activeGunnerWeaponIndex omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.activeGunnerTurretIndex);require(encode(v)!=before,"world field activeGunnerTurretIndex omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.tankBunkerHostId);require(encode(v)!=before,"world field tankBunkerHostId omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.tankBunkerOccupantId);require(encode(v)!=before,"world field tankBunkerOccupantId omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.tankBunkerAnimationStartTick);require(encode(v)!=before,"world field tankBunkerAnimationStartTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.tankBunkerLowering);require(encode(v)!=before,"world field tankBunkerLowering omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.dockedToObjectId);require(encode(v)!=before,"world field dockedToObjectId omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.dockIndex);require(encode(v)!=before,"world field dockIndex omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.airborne);require(encode(v)!=before,"world field airborne omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.flightLevel);require(encode(v)!=before,"world field flightLevel omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.aircraftDockFlightProgress);require(encode(v)!=before,"world field aircraftDockFlightProgress omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.aircraftBuildingClearanceLevel);require(encode(v)!=before,"world field aircraftBuildingClearanceLevel omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.wallConnectionMask);require(encode(v)!=before,"world field wallConnectionMask omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.garrisonHostObjectId);require(encode(v)!=before,"world field garrisonHostObjectId omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.garrisonOccupantIds);require(encode(v)!=before,"world field garrisonOccupantIds omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.garrisonFireCursor);require(encode(v)!=before,"world field garrisonFireCursor omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.currentAmmo);require(encode(v)!=before,"world field currentAmmo omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.pendingAircraftAuxSounds);require(encode(v)!=before,"world field pendingAircraftAuxSounds omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.turretSoundActivityTick);require(encode(v)!=before,"world field turretSoundActivityTick omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.activeTurretSoundEvent);require(encode(v)!=before,"world field activeTurretSoundEvent omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.pendingTurretSounds);require(encode(v)!=before,"world field pendingTurretSounds omitted");++worldProbes;}
{auto v=zero;h023_private_qualification::mutateEncoderField(v.modExtensions);require(encode(v)!=before,"world field modExtensions omitted");++worldProbes;}
}
std::string warpImage(RuntimeProductionHostState& s){RuntimeObjectBehavior external;return h02302_private_qualification::image(s,external)+encode(s.pendingTransportTargets)+encode(s.projectedObjectsCacheValid)+encode(s.runtimeSoundEvents)+encode(s.runtimeSoundIntentHistory)+encode(s.lastRuntimeSound);}
void writeAnimationFixture(const std::filesystem::path& output,const std::string& mapPath){
 RuntimeConfig cfg;cfg.localDataRootPath=output.generic_string();cfg.resourceRootPath=output.generic_string();RuntimeMapSummary m;m.path=mapPath;
 const auto loose=std::filesystem::path(runtimeStructureRootPath(cfg,m));require(!loose.empty(),"owned loose root");std::filesystem::create_directories(loose);
 const auto corpus=shp_corpus::cases();const auto p=std::find_if(corpus.begin(),corpus.end(),[](const auto& c){return c.frameIndex==0&&c.runtime.success&&!c.bytes.empty()&&c.runtime.frame.width>0&&c.runtime.frame.height>0;});require(p!=corpus.end(),"original known valid corpus frame");
 const auto path=loose/"h02305_warp.shp";require(!std::filesystem::exists(path),"fresh owned SHP output");std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(p->bytes.data()),static_cast<std::streamsize>(p->bytes.size()));require(static_cast<bool>(f),"write own original corpus bytes");
}
std::unique_ptr<h023_private_qualification::ChronoIndependentRecoveryHost> scene(const std::string& mapPath,const std::filesystem::path& output,int hz,int scenario){
 auto box=std::make_unique<h023_private_qualification::ChronoIndependentRecoveryHost>();auto& s=*box;std::string error;
 require(configureRuntimeDetailSnapshotSmokeWorld(s,false,error),"original real scene "+error);
 s.config.defaultMapPath=mapPath;s.report.map.path=mapPath;s.config.localDataRootPath=output.generic_string();s.config.resourceRootPath=output.generic_string();s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerCountry="Americans";s.config.multiplayerActive=false;s.config.computerAiEnabled=false;
 s.tick=100;s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;s.theaterLoaded=false;
 const std::string rules=R"H02305([InfantryTypes]
0=TELE_PASS
[VehicleTypes]
0=CARGO_HOST
1=TELE_MINER
[TELE_PASS]
Strength=100
Speed=6
Size=1
Teleporter=yes
Harvester=no
[CARGO_HOST]
Strength=200
Speed=6
Passengers=3
SizeLimit=6
[TELE_MINER]
Strength=500
Speed=6
Teleporter=yes
Harvester=yes
)H02305";
 IniFile ini;require(ini.loadFromString(rules)&&s.rules.loadFromIni(ini),"actual typed unit rules");s.rulesLoaded=true;
 auto& special=const_cast<SpecialMechanismRules&>(s.rules.getSpecialMechanismRules());special.warpInAnim="H02305_IN";special.warpOutAnim="H02305_OUT";special.chronoInSound=" RuleIn ";special.chronoOutSound=" RuleOut ";
 for(const auto* type:{"InfantryType","UnitType"}){auto* unit=const_cast<UnitType*>(s.rules.findUnitType(type,std::string(type)=="InfantryType"?"TELE_PASS":"TELE_MINER"));require(unit!=nullptr,"actual fixture unit type");unit->chronoInSound=" UnitIn ";unit->chronoOutSound=" UnitOut ";if(scenario==18||scenario==19){unit->chronoInSound=" \t ";unit->chronoOutSound=" \t ";}if(scenario==21){unit->chronoInSound=" none ";unit->chronoOutSound=" none ";}if(scenario==22){unit->chronoInSound=" MiXeD-In ";unit->chronoOutSound=" MiXeD-Out ";}}
 if(scenario==14)special.warpInAnim=special.warpOutAnim="none";
 if(scenario==15)special.warpInAnim=special.warpOutAnim="NoNe";
 if(scenario==16)special.warpInAnim=special.warpOutAnim="";
 if(scenario==17)special.warpInAnim=special.warpOutAnim=" none ";
 if(scenario==19)special.chronoInSound=special.chronoOutSound=" \t ";
 IniFile art;require(art.loadFromString("[H02305_IN]\nImage=h02305_warp\nRate=31\nShadow=no\n[H02305_OUT]\nImage=h02305_warp\nRate=31\nShadow=no\n")&&s.art.loadFromIni(art),"actual typed ART");s.artLoaded=true;s.renderCache=RuntimeMapRenderCache{};
 s.world.accessObjects().clear();s.behaviors.clear();s.pendingTransportTargets.clear();s.wrecks.clear();s.buildingCollapses.clear();s.projectiles.clear();s.impactEffects.clear();s.pendingObjectExits.clear();s.deathWeapons.reset();
 WorldObjectInstance p,h;p.instanceId=41;p.kind=WorldObjectKind::Infantry;p.type="TELE_PASS";p.owner="Americans";p.health=100;p.alive=true;p.x=5;p.y=6;p.level=2;p.cell=runtimeCellKey(5,6);p.mission="Guard";p.subCell=4;p.onBridge=true;p.facing=7;p.hasRenderPosition=true;p.renderX=5.25;p.renderY=6.5;p.renderLevel=2.75;p.renderMoveActive=true;
 h.instanceId=52;h.kind=WorldObjectKind::Unit;h.type="CARGO_HOST";h.owner="Americans";h.health=200;h.alive=true;h.x=5;h.y=6;h.level=2;h.cell=runtimeCellKey(5,6);h.mission="Guard";h.hasRenderPosition=true;h.renderX=8.25;h.renderY=9.75;h.renderLevel=3.5;
 if(scenario==2){p.alive=false;p.health=0;}if(scenario==3||scenario==20)p.type="UNKNOWN";if(scenario==4){p.kind=WorldObjectKind::Unit;p.type="TELE_MINER";}if(scenario==5)p.airborne=true;if(scenario==6)p.hasRenderPosition=false;if(scenario==7)p.onBridge=false;if(scenario==8)p.subCell=-1;if(scenario==13)p.kind=WorldObjectKind::Structure;
 if(scenario==9||scenario==10||scenario==11||scenario==12){p.renderMoveActive=false;RuntimeObjectBehavior b;b.kind=RuntimeBehaviorKind::Move;b.state=RuntimeBehaviorState::Idle;b.pendingAllowedOccupiedObjectId=52;b.moveTarget={5,6};s.behaviors.emplace(41,b);if(scenario!=11)s.pendingTransportTargets.emplace(41,scenario==12?999:52);if(scenario==10){h.alive=false;h.health=0;}}
 if(scenario==22)p.cell=p.y*512+p.x;
 s.world.accessObjects().push_back(p);s.world.accessObjects().push_back(h);s.specialMechanisms.reset(s.rules.getSpecialMechanismRules());s.specialMechanismsInitialized=true;s.particles.reset(hz);require(s.particles.alignEmptyStateTick(s.tick),"particle clock");s.particlesInitialized=true;s.superWeapons.reset(std::vector<SuperWeaponRule>{});require(s.superWeapons.alignEmptyStateTick(s.tick),"superweapon clock");s.superWeaponsInitialized=true;
 refreshRuntimeObjectOccupancyAfterLocalChange(s);s.projectedObjectsCacheValid=true;s.runtimeSoundEvents=0;s.runtimeSoundIntentHistory.clear();s.lastRuntimeSound.clear();return box;
}
void compareCache(const RuntimeProductionHostState& a,const RuntimeProductionHostState& b){
 require(a.renderCache.auxiliaryShpBytesByImage.size()==b.renderCache.auxiliaryShpBytesByImage.size(),"full original byte-cache key count");require(a.renderCache.auxiliaryShpSpritesByImage.size()==b.renderCache.auxiliaryShpSpritesByImage.size(),"full original frame-cache key count");
 for(const auto& [key,v]:a.renderCache.auxiliaryShpBytesByImage){const auto q=b.renderCache.auxiliaryShpBytesByImage.find(key);require(q!=b.renderCache.auxiliaryShpBytesByImage.end()&&v.bytes==q->second.bytes&&v.loadAttempted==q->second.loadAttempted&&v.loaded==q->second.loaded,"full selected byte-cache contents");}
 for(const auto& [key,v]:a.renderCache.auxiliaryShpSpritesByImage){const auto q=b.renderCache.auxiliaryShpSpritesByImage.find(key);require(q!=b.renderCache.auxiliaryShpSpritesByImage.end(),"same full frame cache key");require(h017_private_qualification::shpPixelsEqual(v.image,q->second.image)&&h017_private_qualification::shpPixelsEqual(v.shadowImage,q->second.shadowImage),"full main/shadow indexed bytes and geometry");}
}
int run(const std::string& mapPath,const std::string& outputPath){try{
 verifyWorldEncoder();h02302_private_qualification::verifyImpactEncoder();h02302_private_qualification::verifyAirborneEncoder();require(worldProbes==87&&h02302_private_qualification::effectProbes==56&&h02302_private_qualification::airProbes==12,"complete original field encoders");
 const auto output=std::filesystem::path(outputPath);std::filesystem::create_directories(output);writeAnimationFixture(output,mapPath);
 const std::array<std::string,3> kinds{"ChronoWarpOut","ChronoWarpIn","ChronoReinforcementIn"};
 for(int hz:{15,30,60})for(const auto& kind:kinds)for(int scenario=0;scenario<24;++scenario)for(bool playSound:{false,true}){
  where="hz="+std::to_string(hz)+" kind="+kind+" scenario="+std::to_string(scenario)+" sound="+std::to_string(playSound)+" ";auto old=scene(mapPath,output,hz,scenario),now=scene(mapPath,output,hz,scenario);
  std::string beforeOld,beforeNow;{RuntimeSnapshotActiveStateScope active(*old);beforeOld=warpImage(*old);}{RuntimeSnapshotActiveStateScope active(*now);beforeNow=warpImage(*now);}require(beforeOld==beforeNow,"complete equal original starting state");
  SpecialMechanismEvent event;event.kind=scenario==23?"OtherCompatibilityEvent":kind;event.objectId=scenario==1?999:41;event.tick=100;
  {RuntimeSnapshotActiveStateScope active(*old);original::spawnRuntimeChronoWarpEffect(*old,event,playSound);}{RuntimeSnapshotActiveStateScope active(*now);spawnRuntimeChronoWarpEffect(*now,event,playSound);}states+=2;
  std::string afterOld,afterNow;{RuntimeSnapshotActiveStateScope active(*old);afterOld=warpImage(*old);}{RuntimeSnapshotActiveStateScope active(*now);afterNow=warpImage(*now);}require(afterOld==afterNow,"complete realRoot/fullsave/world87/effect56/nested12/sound/cache mismatch");compareCache(*old,*now);
  const bool active=scenario!=1&&scenario!=2;const bool arriving=event.kind=="ChronoWarpIn"||event.kind=="ChronoReinforcementIn";
  if(!active)require(afterNow==beforeNow,"missing/dead guard performs no state change");
  if(active){const auto* object=findObjectById(*now,41);require(object!=nullptr,"same first object");if(arriving){require(object->cell==runtimeCellKey(object->x,object->y)&&!object->hasRenderPosition&&!now->projectedObjectsCacheValid,"canonical arrival and original pose/cache updates");}else require(now->projectedObjectsCacheValid,"outward warp preserves projection validity");
   const bool animation=scenario!=14&&scenario!=15&&scenario!=16;require(now->impactEffects.size()==static_cast<std::size_t>(animation),"original no-trim animation none guard");if(animation&&scenario!=17){require(now->impactEffects.back().animationFrameCount>0,"real original animation timeline positive");++positiveTimeline;}
   const bool sound=playSound&&scenario!=19;require(now->runtimeSoundEvents==(sound?1:0),"original optional sound/fallback count");if(!playSound)++silentCalls;
   if(scenario==21&&playSound)require(now->lastRuntimeSound=="sound:none","sound none remains an emitted intent");
   if(scenario==9&&event.kind=="ChronoWarpIn"){require(object->subCell==-1,"real admitted transport uses original boarding subcell");const auto hull=runtimeTransportMotionBody(*findObjectById(*now,52));require(now->impactEffects.back().worldX==hull.x&&now->impactEffects.back().worldY==hull.y&&now->impactEffects.back().worldLevel==hull.level,"real boarding hull wins after offset");++positiveBoarding;}
  }
 }
 for(int hz:{15,30,60})for(int variant=0;variant<6;++variant){where="miner hz="+std::to_string(hz)+" variant="+std::to_string(variant)+" ";const int scenario=variant==1?18:variant==2?19:variant==3?14:variant==4?3:0;auto old=scene(mapPath,output,hz,scenario),now=scene(mapPath,output,hz,scenario);
  for(auto* s:{old.get(),now.get()}){auto* p=findMutableObjectById(*s,41);p->kind=WorldObjectKind::Unit;p->type=variant==4?"UNKNOWN":"TELE_MINER";p->health=500;p->alive=true;clearRuntimeRenderPosition(*p);}
  int oldCounter=7,newCounter=7;bool a=false,b=false;{RuntimeSnapshotActiveStateScope active(*old);a=original::teleportRuntimeHarvesterToCell(*old,*findMutableObjectById(*old,41),{7,8},variant%2?&oldCounter:nullptr);}{RuntimeSnapshotActiveStateScope active(*now);b=teleportRuntimeHarvesterToCell(*now,*findMutableObjectById(*now,41),{7,8},variant%2?&newCounter:nullptr);}states+=2;
  require(a==b&&oldCounter==newCounter,"whole real miner result/external counter");std::string ia,ib;{RuntimeSnapshotActiveStateScope active(*old);ia=warpImage(*old);}{RuntimeSnapshotActiveStateScope active(*now);ib=warpImage(*now);}require(ia==ib,"whole original miner caller fullsave/world/effects/outside sound/order");compareCache(*old,*now);if(b){++positiveMiner;const auto* p=findObjectById(*now,41);require(p&&p->cell==runtimeCellKey(7,8),"real miner canonical target");require(now->runtimeSoundEvents==(variant==1||variant==2||variant==4?0:2),"two false inner calls never add rule fallback sounds");}
 }
 require(states==900&&positiveTimeline>0&&positiveBoarding>0&&positiveMiner>0&&silentCalls>0,"full case matrix with real positive timeline/boarding/miner and false sound required");
 std::cout<<"private-chrono-warp checks="<<checks<<" actual-root-states="<<states<<" world-probes="<<worldProbes<<" effect-probes=56 nested-probes=12 positive-timeline="<<positiveTimeline<<" positive-boarding="<<positiveBoarding<<" positive-miner="<<positiveMiner<<" false-sound="<<silentCalls<<" fullsave-world-effect-cache-sound-and-original-miner=exact failures=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"private-chrono-warp FAIL "<<e.what()<<'\n';return 1;}}
}
