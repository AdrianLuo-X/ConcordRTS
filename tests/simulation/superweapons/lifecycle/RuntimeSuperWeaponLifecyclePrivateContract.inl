namespace h020_private_qualification {
using namespace concord::runtime::session_detail;
template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_same_v<T,float>)return "f"+std::to_string(std::bit_cast<std::uint32_t>(v))+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
    else if constexpr (std::is_same_v<T,SuperWeaponObjectLifecycleEvent>) {return encode(v.kind)+encode(v.removalDisposition)+encode(v.tick)+encode(v.objectId)+encode(v.healthBefore)+encode(v.healthAfter)+encode(v.superWeaponInstanceId)+encode(v.sourceOwner)+encode(v.superWeaponId)+encode(v.warheadId)+encode(v.reason)+encode(v.oldCell)+encode(v.newCell)+encode(v.oldOwner)+encode(v.newOwner);}
    else if constexpr (requires { v.voxelDebris;v.parentActorId;v.deathWeaponRequestId; }) { return encode(v.voxelDebris)+encode(v.homingInitialized)+encode(v.homingYaw)+encode(v.homingPitch)+encode(v.actorId)+encode(v.parentActorId)+encode(v.generation)+encode(v.attackerObjectId)+encode(v.targetObjectId)+encode(v.sourceAnchorObjectId)+encode(v.owner)+encode(v.fromCell)+encode(v.toCell)+encode(v.currentCell)+encode(v.hasSourcePosition)+encode(v.sourceX)+encode(v.sourceY)+encode(v.sourceZ)+encode(v.sourceScreenOffsetX)+encode(v.sourceScreenOffsetY)+encode(v.sourceNativeFixedX)+encode(v.sourceNativeFixedY)+encode(v.hasTargetPosition)+encode(v.targetX)+encode(v.targetY)+encode(v.targetZ)+encode(v.fromLevel)+encode(v.toLevel)+encode(v.currentLevel)+encode(v.attackerFacing)+encode(v.targetFacing)+encode(v.spawnTick)+encode(v.impactTick)+encode(v.elapsedTicks)+encode(v.damage)+encode(v.veteranDamageMultiplier)+encode(v.spawnedMissileDamage)+encode(v.spawnedMissileBaseDamage)+encode(v.totalTicks)+encode(v.remainingTicks)+encode(v.lifecycle)+encode(v.lifecycleState)+encode(v.renderIntent)+encode(v.weaponId)+encode(v.weaponSlot)+encode(v.projectileId)+encode(v.projectileImageId)+encode(v.warheadId)+encode(v.weaponAnimId)+encode(v.impactAnimId)+encode(v.trailerAnimId)+encode(v.trailerIntentId)+encode(v.warheadImpactIntentId)+encode(v.reportSoundId)+encode(v.arcing)+encode(v.vertical)+encode(v.acceleration)+encode(v.high)+encode(v.veryHigh)+encode(v.shadow)+encode(v.animates)+encode(v.aa)+encode(v.ag)+encode(v.cluster)+encode(v.bright)+encode(v.laser)+encode(v.houseColorLaser)+encode(v.electricBolt)+encode(v.alternateElectricColor)+encode(v.electricColor)+encode(v.shrapnel)+encode(v.shrapnelReleased)+encode(v.laserDuration)+encode(v.laserThickness)+encode(v.cellSpread)+encode(v.percentAtMax)+encode(v.affectsAllies)+encode(v.affectsEnemies)+encode(v.particleId)+encode(v.progress)+encode(v.currentX)+encode(v.currentY)+encode(v.currentZ)+encode(v.speedCellsPerTick)+encode(v.rot)+encode(v.deathWeaponCascadeId)+encode(v.deathWeaponRequestId)+encode(v.deathWeaponCascadeDepth)+encode(v.launchSourceKind)+encode(v.launchSourceType)+encode(v.launchSourceAuthoredAircraft)+encode(v.launchSourceValidated)+encode(v.airstrikeStrikeId)+encode(v.airstrikePlaneToken)+encode(v.particleDamagePulse)+encode(v.suppressWarheadParticleSpawn);}
    else if constexpr (requires { v.framesLeft;v.orientation;v.spin; }) {return encode(v.enabled)+encode(v.framesLeft)+encode(v.frameRemainder)+encode(v.frameCount)+encode(v.bounces)+encode(v.elasticity)+encode(v.position)+encode(v.velocity)+encode(v.orientation)+encode(v.spin);}
    else if constexpr (requires { v.cell;v.x;v.y;v.level; } && !requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.cell)+encode(v.x)+encode(v.y)+encode(v.level);}
    else if constexpr (requires {v.has_value();v.value();})return v ? "some"+encode(*v):"none";
    else if constexpr (requires { v.kind;v.moveTarget; }) {return encode(v.kind)+encode(v.state)+encode(v.moveTarget)+encode(v.targetObjectId)+encode(v.autoAcquired)+encode(v.forceFire)+encode(v.attackGround)+encode(v.attackBridge)+encode(v.attackBridgeCell)+encode(v.path)+encode(v.pathRequestPending)+encode(v.pendingPathTowardRange)+encode(v.pendingPathStart)+encode(v.pendingPathGoal)+encode(v.pendingHasSharedGoal)+encode(v.pendingSharedGoal)+encode(v.pendingAllowedOccupiedObjectId)+encode(v.pendingTargetObjectId)+encode(v.pendingRangeCells)+encode(v.pendingAllowNearbyTarget)+encode(v.pendingIgnoredObjectIds)+encode(v.repackMoveGroup)+encode(v.moveCooldownTicks)+encode(v.moveSegmentFrom)+encode(v.moveIntervalRemainderQ16)+encode(v.moveStartupDistanceQ16)+encode(v.renderStartupAccelerationDistance)+encode(v.factoryExitClearanceHostId)+encode(v.transportTransferHostId)+encode(v.transportTransferKind)+encode(v.weaponCooldownTicks)+encode(v.blockedTicks)+encode(v.pathReplans)+encode(v.nextRepathTick)+encode(v.returnToDeployed)+encode(v.approachSlotIndex)+encode(v.renderFrom)+encode(v.renderTo)+encode(v.renderFromOffsetX)+encode(v.renderFromOffsetY)+encode(v.renderToOffsetX)+encode(v.renderToOffsetY)+encode(v.renderFromLevel)+encode(v.renderToLevel)+encode(v.renderFromOnBridge)+encode(v.renderToOnBridge)+encode(v.renderMoveTicksTotal)+encode(v.renderMoveTicksRemaining)+encode(v.renderArrivalDistanceAfter);}
    else if constexpr (requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.instanceId)+encode(v.kind)+encode(v.mapId)+encode(v.owner)+encode(v.type)+encode(v.health)+encode(v.x)+encode(v.y)+encode(v.cell)+encode(v.level)+encode(v.facing)+encode(v.turretFacing)+encode(v.bodyFacingClock)+encode(v.turretFacingClock)+encode(v.subCell)+encode(v.mission)+encode(v.tag)+encode(v.veterancy)+encode(v.veterancyExperience)+encode(v.crateArmorMultiplier)+encode(v.crateFirepowerMultiplier)+encode(v.crateSpeedMultiplier)+encode(v.crateCloakable)+encode(v.harvestAutoSearchSuspended)+encode(v.hasBeenCaptured)+encode(v.onBridge)+encode(v.authoredAircraft)+encode(v.alive)+encode(v.rubble)+encode(v.submerged)+encode(v.cloaked)+encode(v.cloakCooldownTicks)+encode(v.submergeCooldownTicks)+encode(v.deployed)+encode(v.deployStateTicksRemaining)+encode(v.prone)+encode(v.proneTicksRemaining)+encode(v.weaponCooldownTicksRemaining)+encode(v.spriteFiringStartTick)+encode(v.radiationEruptionCooldownTicksRemaining)+encode(v.hasRenderPosition)+encode(v.renderX)+encode(v.renderY)+encode(v.renderMoveActive)+encode(v.renderLevel)+encode(v.renderOnBridge)+encode(v.movementAnimationDistance)+encode(v.buildAnimTicksTotal)+encode(v.buildAnimTicksRemaining)+encode(v.structureRecoilStartTick)+encode(v.chargeTurretStartTick)+encode(v.workAnimationActive)+encode(v.poweredOperational)+encode(v.powerAnimationPausedAtTick)+encode(v.powerAnimationPausedTicks)+encode(v.factoryExitAnimationStartTick)+encode(v.factoryExitAnimationEndTick)+encode(v.factoryExitAirborne)+encode(v.factoryExitHostId)+encode(v.specialCashNextTick)+encode(v.specialCloneDeliveryObjectId)+encode(v.transportPassengerIds)+encode(v.bioReactorAnimationStartTick)+encode(v.bioReactorAnimationPoweredStartTick)+encode(v.activeGunnerMode)+encode(v.activeGunnerWeaponIndex)+encode(v.activeGunnerTurretIndex)+encode(v.tankBunkerHostId)+encode(v.tankBunkerOccupantId)+encode(v.tankBunkerAnimationStartTick)+encode(v.tankBunkerLowering)+encode(v.dockedToObjectId)+encode(v.dockIndex)+encode(v.airborne)+encode(v.flightLevel)+encode(v.aircraftDockFlightProgress)+encode(v.aircraftBuildingClearanceLevel)+encode(v.wallConnectionMask)+encode(v.garrisonHostObjectId)+encode(v.garrisonOccupantIds)+encode(v.garrisonFireCursor)+encode(v.currentAmmo)+encode(v.pendingAircraftAuxSounds)+encode(v.turretSoundActivityTick)+encode(v.activeTurretSoundEvent)+encode(v.pendingTurretSounds)+encode(v.modExtensions);}
    else if constexpr (requires { v.elapsedNumerator;v.lastFacing; }) {return encode(v.initial)+encode(v.target)+encode(v.rate)+encode(v.elapsedNumerator)+encode(v.lastTick)+encode(v.lastFacing);}
    else if constexpr (requires { v.technoType;v.shieldType;v.effects; }) {return encode(v.initialized)+encode(v.technoType)+encode(v.shieldType)+encode(v.shieldCapacity)+encode(v.shieldHealth)+encode(v.shieldTimer)+encode(v.shieldOnline)+encode(v.innateDelayTicks)+encode(v.effects);}
    else if constexpr (requires { v.sourceType;v.fromWarhead; }) {return encode(v.sourceType)+encode(v.fromWarhead)+encode(v.remainingTicks)+encode(v.elapsedTicks);}
    else if constexpr (requires { v.event;v.takingOff; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.takingOff);}
    else if constexpr (requires { v.event;v.rotating; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.rotating);}
    else if constexpr (requires { v.center;v.members; }) {return encode(v.center)+encode(v.members);}
    else if constexpr (requires { v.x;v.y;v.level; }) {return encode(v.x)+encode(v.y)+encode(v.level);}
    else if constexpr (requires { v.x;v.y; }) {return encode(v.x)+encode(v.y);}
    else if constexpr (requires {v.begin();v.end();}) {
        std::vector<std::string> parts;for(const auto& item:v)parts.push_back(encode(item));
        if constexpr (requires {typename T::key_type;})std::sort(parts.begin(),parts.end());
        std::string out="n"+std::to_string(parts.size())+":";for(const auto& part:parts)out+=std::to_string(part.size())+":"+part;return out;
    } else static_assert(sizeof(T)==0,"unencoded complete field");
}
template<class T> void mutateEncoderField(T& v) {
    if constexpr(std::is_same_v<T,bool>) v=!v;
    else if constexpr(std::is_enum_v<T>) v=static_cast<T>(static_cast<int>(v)==0?1:0);
    else if constexpr(std::is_integral_v<T>) v=v==0?1:0;
    else if constexpr(std::is_floating_point_v<T>) {
        using U=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;
        v=std::bit_cast<T>(std::bit_cast<U>(v)^U{1});
    } else if constexpr(std::is_same_v<T,std::string>) v += "encoder-probe";
    else if constexpr(requires {v.elapsedNumerator;v.lastFacing;}) v.lastFacing=v.lastFacing==0?1:0;
    else if constexpr(requires {v.initialized;v.technoType;v.shieldType;}) v.initialized=!v.initialized;
    else if constexpr(requires {v.empty();v.clear();v.emplace_back();}) {if(v.empty())v.emplace_back();else v.clear();}
    else if constexpr(requires {v.x;v.y;}) v.x=v.x==0?1:0;
    else static_assert(sizeof(T)==0,"unprobed canonical WorldObject field");
}

std::string rec_RuntimeComputerAiState(const RuntimeComputerAiState& v){return encode(v.initialized)+encode(v.owner)+encode(v.country)+encode(v.difficulty)+encode(v.baseCenter)+encode(v.credits)+encode(v.nextThinkTick)+encode(v.nextIncomeTick)+encode(v.nextBuildTick)+encode(v.nextAttackTick)+encode(v.nextRepairTick)+encode(v.productionSerial)+encode(v.incomeEarned)+encode(v.creditsSpent)+encode(v.structuresBuilt)+encode(v.infantryBuilt)+encode(v.unitsBuilt)+encode(v.freeUnitsReceived)+encode(v.attackWaves)+encode(v.attacksOrdered)+encode(v.defenseOrders)+encode(v.tacticalOrders)+encode(v.repairsPerformed)+encode(v.stalledBuilds)+encode(v.lastTargetObjectId)+encode(v.lastBuildType)+encode(v.status);}
std::string rec_SuperWeaponEvent(const SuperWeaponEvent& v){return encode(v.tick)+encode(v.instanceId)+encode(v.activeToken)+encode(v.sourceBuildingId)+encode(v.soundSourceObjectId)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.value)+encode(v.owner)+encode(v.superWeaponId)+encode(v.kind)+encode(v.state)+encode(v.reason)+encode(v.commandVerb)+encode(v.soundIntent)+encode(v.evaIntent)+encode(v.animationIntent)+encode(v.targetCell)+encode(v.targetCell2);}
std::string rec_SuperWeaponSidebarSnapshot(const SuperWeaponSidebarSnapshot& v){return encode(v.instanceId)+encode(v.owner)+encode(v.superWeaponId)+encode(v.uiName)+encode(v.name)+encode(v.sidebarImage)+encode(v.state)+encode(v.progress)+encode(v.remainingTicks)+encode(v.rechargeTicks)+encode(v.stateStartTick)+encode(v.launchTick)+encode(v.flashSidebarTabFrames)+encode(v.targetRange)+encode(v.targetLineMultiplier)+encode(v.showTimer)+encode(v.selected)+encode(v.targetSelectionActive)+encode(v.firstTargetAccepted)+encode(v.firstTargetCell)+encode(v.chargePaused);}
std::string rec_SuperWeaponRenderSnapshot(const SuperWeaponRenderSnapshot& v){return encode(v.instanceId)+encode(v.tick)+encode(v.owner)+encode(v.superWeaponId)+encode(v.kind)+encode(v.phase)+encode(v.resourceId)+encode(v.sourceObjectId)+encode(v.targetObjectId)+encode(v.cell)+encode(v.cell2)+encode(v.radius)+encode(v.sourceLevel)+encode(v.intensity)+encode(v.progress)+encode(v.requiredProgress)+encode(v.expireTick)+encode(v.visibleToAll);}
std::string rec_SuperWeaponActiveLifecycleSnapshot(const SuperWeaponActiveLifecycleSnapshot& v){return encode(v.instanceId)+encode(v.activeToken)+encode(v.superWeaponId)+encode(v.superWeaponType)+encode(v.sourceOwner)+encode(v.sourceObjectId)+encode(v.cell)+encode(v.startedTick)+encode(v.activeUntilTick);}
namespace original {
bool advance(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeSuperWeapons", state.tick);
    if (!state.rulesLoaded || state.rules.getSuperWeaponTypes().empty()) {
        return false;
    }
    ensureRuntimeSuperWeaponsInitialized(state);
    if (!state.superWeaponsInitialized) {
        return false;
    }
    const std::vector<std::array<int, 4>> parasiteHostPoses =
        runtimeParasiteLinkedHostPoses(state);
    const std::uint64_t beforeHash = state.superWeapons.stateHash();
    SuperWeaponWorldContext context = runtimeSuperWeaponContext(state);
    context.emit = runtimeSuperWeaponEventSink(state);
    state.superWeapons.tick(context);
    bool changed = beforeHash != state.superWeapons.stateHash();

    // Defensive super-weapons react to targeted hostile launches. Keep that
    // decision on the same deterministic command path as ordinary AI fire,
    // but update each computer player's base center first so the authored
    // distance gate is evaluated against current world state.
    const bool computerAiEnabled =
        runtimeComputerAiEnabledForSession(state);
    if (computerAiEnabled) {
        ensureRuntimeComputerAiStates(state);
    }
    std::vector<std::string> computerOwners;
    computerOwners.reserve(state.computerAiByOwner.size());
    for (const auto& [ownerKey, ai] : state.computerAiByOwner) {
        (void)ai;
        computerOwners.push_back(ownerKey);
    }
    std::sort(computerOwners.begin(), computerOwners.end());
    const SpecialMechanismRules& mechanismRules =
        state.rules.getSpecialMechanismRules();
    for (const std::string& ownerKey : computerOwners) {
        if (!computerAiEnabled) {
            break;
        }
        auto found = state.computerAiByOwner.find(ownerKey);
        if (found == state.computerAiByOwner.end()
            || !found->second.initialized
            || trimAsciiLocal(found->second.owner).empty()) {
            continue;
        }
        RuntimeComputerAiState& ai = found->second;
        updateRuntimeComputerAiBaseCenter(state, ai);
        const int probabilityIndex =
            runtimeAiSuperDefenseProbabilityIndex(ai.difficulty);
        std::string reason;
        if (state.superWeapons.fireAiDefense(
                ai.owner,
                {ai.baseCenter.x, ai.baseCenter.y},
                mechanismRules.aiSuperDefense.probabilityPercent[
                    static_cast<std::size_t>(probabilityIndex)
                ],
                mechanismRules.aiSuperDefense.alertFrames,
                mechanismRules.aiSuperDefense.distanceCells,
                context,
                &reason
            )) {
            changed = true;
        }
    }

    // The runtime does not have a separate computer-player command queue
    // yet.  Fire ready enemy super weapons at a fixed cadence through the
    // same validated command path used by the player, so AI effects remain
    // deterministic and replay-visible instead of bypassing the system.
    if (computerAiEnabled
        && state.tick > 0
        && (state.tick % std::max(1, context.fixedTickHz)) == 0) {
        std::vector<std::pair<std::string, std::string>> aiWeapons;
        for (const SuperWeaponSidebarSnapshot& snapshot :
                state.superWeapons.sidebarSnapshots()) {
            const SuperWeaponRule* rule = state.rules.findSuperWeaponType(
                snapshot.superWeaponId
            );
            if (snapshot.state == SuperWeaponRuntimeState::Ready
                && !runtimeIsForceShieldSuperWeaponRule(rule)
                && !runtimeComputerTacticReservesSuperWeapon(state, snapshot.owner, rule)
                && (state.campaignMode || state.computerAiByOwner.contains(toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))))
                && toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))
                    != toLowerAsciiLocal(trimAsciiLocal(state.economy.owner))) {
                aiWeapons.emplace_back(snapshot.owner, snapshot.superWeaponId);
            }
        }
        for (const auto& [owner, superWeaponId] : aiWeapons) {
            std::string reason;
            if (state.superWeapons.fireAi(owner, superWeaponId, context, &reason)) {
                changed = true;
            }
        }
    }
    changed = notifyRuntimeParasiteHostsRelocated(
        state,
        parasiteHostPoses
    ) || changed;
    changed = drainRuntimeObjectExitQueue(state) || changed;
    changed = flushRuntimeSuperWeaponHostEvents(state) || changed;
    return changed || !state.superWeapons.renderSnapshots().empty();
}
bool oldHasSupportFlightPose(const WorldObjectInstance& object) {
    return object.alive && object.airborne && (object.mission=="ParadropDrop"
        || object.mission=="SupportFlightEast" || object.mission=="SupportFlightWest");
}
void oldWriteSupportFlightPoses(const RuntimeGameSessionState& state, std::ostream& output) {
    const auto precision=output.precision(); output<<std::setprecision(17);
    const auto& objects=state.world.getObjects();
    output<<"\n[SupportFlightPoses]\nVersion=1\nCount="
        <<std::count_if(objects.begin(),objects.end(),oldHasSupportFlightPose)<<"\n";
    int index=0;
    for (const auto& object:objects) if (oldHasSupportFlightPose(object)) {
        output<<index++<<"="<<object.instanceId<<"|"<<object.renderX<<"|"<<object.renderY
            <<"|"<<object.renderLevel<<"|"<<object.movementAnimationDistance<<"\n";
    }
    output.precision(precision);
}
bool oldRestoreSupportFlightPoses(const IniFile& ini,
    std::vector<WorldObjectInstance>& objects, std::string& error) {
    const auto reject=[&]() {error="invalid saved support aircraft/paratrooper pose";return false;};
    if (!ini.getSection("SupportFlightPoses")) {
        // Earlier saves retain integer world coordinates but no sub-cell
        // pose. Resume those flights from that last known position.
        for (auto& object:objects) if (oldHasSupportFlightPose(object)) {
            object.hasRenderPosition=object.renderMoveActive=true;
            object.renderX=object.x; object.renderY=object.y; object.renderLevel=object.level;
        }
        return true;
    }
    int version=0,count=-1;
    const auto expected=std::count_if(objects.begin(),objects.end(),oldHasSupportFlightPose);
    if (!parseRuntimeInt(ini.getValue("SupportFlightPoses","Version",""),version) || version!=1
        || !parseRuntimeInt(ini.getValue("SupportFlightPoses","Count",""),count)
        || count!=expected) return reject();
    std::unordered_set<int> restored;
    for (int i=0;i<count;++i) {
        const auto fields=splitRuntimeSaveFields(ini.getValue("SupportFlightPoses",std::to_string(i),""));
        int id=-1; double x=0,y=0,z=0,distance=0;
        if (fields.size()!=5 || !parseRuntimeInt(fields[0],id) || !restored.insert(id).second
            || !parseRuntimeFiniteDouble(fields[1],x) || !parseRuntimeFiniteDouble(fields[2],y)
            || !parseRuntimeFiniteDouble(fields[3],z) || !parseRuntimeFiniteDouble(fields[4],distance)
            || distance<0.0 || distance>1e9) return reject();
        const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto& candidate){return candidate.instanceId==id;});
        if (object==objects.end() || !oldHasSupportFlightPose(*object)
            || std::abs(x-object->x)>0.500001 || std::abs(y-object->y)>0.500001
            || std::abs(z-object->level)>0.500001 || object->flightLevel!=object->level) return reject();
        object->hasRenderPosition=object->renderMoveActive=true;
        object->renderX=x; object->renderY=y; object->renderLevel=z;
        object->movementAnimationDistance=distance;
    }
    return true;
}
}
int checks=0,states=0,positiveDefense=0,positiveOrdinary=0,poseRejected=0;
std::string location;
void require(bool value,const std::string& reason) {++checks;if(!value)throw std::runtime_error(location+reason);}
std::string image(RuntimeProductionHostState& s) {
    std::ostringstream save;std::string error;
    const bool wrote=writeRuntimeStateToStream(s,save,error);
    require(wrote,"full production save: "+error);
    std::ostringstream weapons;s.superWeapons.save(weapons);
    std::string extra=encode(s.world.getObjects());
    for(const auto& [key,ai]:s.computerAiByOwner)extra+=encode(key)+rec_RuntimeComputerAiState(ai);
    for(const auto& p:s.superWeapons.sidebarSnapshots())extra+=rec_SuperWeaponSidebarSnapshot(p);
    for(const auto& p:s.superWeapons.renderSnapshots())extra+=rec_SuperWeaponRenderSnapshot(p);
    for(const auto& p:s.superWeapons.activeLifecycleSnapshots())extra+=rec_SuperWeaponActiveLifecycleSnapshot(p);
    return encode(save.str())+encode(weapons.str())+encode(s.superWeapons.stateHash())+extra;
}
std::string exercise(bool reference,const std::string& mapPath) {
    std::ostringstream observed;
    for(int hz:{15,30,60})for(int scenario=0;scenario<12;++scenario) {
        location="reference="+std::to_string(reference)+" hz="+std::to_string(hz)+" scenario="+std::to_string(scenario)+" ";
        auto box=std::make_unique<IndependentRecoveryHost>();auto& s=*box;std::string error;
        require(configureRuntimeDetailSnapshotSmokeWorld(s,false,error),"real setup: "+error);
        RuntimeSnapshotActiveStateScope scope(s);
        s.config.defaultMapPath=mapPath;s.config.fixedTickHz=hz;s.config.gameMode="Battle";
        s.config.multiplayerActive=false;s.config.computerAiEnabled=scenario!=0;
        s.config.playerSlots.clear();s.tick=scenario==3?hz-1:hz;s.campaignMode=scenario==4;
        s.config.playerCountry="Americans";s.config.campaignId="H020";s.config.campaignMissionId="Lifecycle";
        s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;s.artLoaded=false;
        s.economy.owner="human";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.skirmishModeRulesLoaded=true;s.skirmishModeAlliesAllowed=true;
        s.skirmishTeamByOwner={{"defender","1"},{"ally","1"},{"attacker","2"},{"beta","3"},{"human","4"}};
        IniFile ini;
        require(ini.loadFromString(R"H20([General]
AISuperDefenseProbability=100,50,0
AISuperDefenseFrames=50
AISuperDefenseDistance=12
[BuildingTypes]
0=HQ
[HQ]
Strength=1000
Foundation=1x1
ConstructionYard=yes
[AircraftTypes]
0=POSE_PLANE
[POSE_PLANE]
Strength=100
[SuperWeaponTypes]
0=DefenseThreat
1=DefenseShield
2=OrdinaryReveal
[DefenseThreat]
Type=MultiMissile
Action=MultiMissile
AIDefendAgainst=yes
[DefenseShield]
Type=ForceShield
Action=ForceShield
[OrdinaryReveal]
Type=PsychicReveal
Action=PsychicReveal
)H20")&&s.rules.loadFromIni(ini),"actual typed rules");s.rulesLoaded=true;
        IniFile mapIni;MapDatabase map;
        require(mapIni.loadFromPath(std::filesystem::u8path(mapPath))&&map.loadFromIni(mapIni)&&s.world.loadFromMap(map,&s.rules),"actual map/world producers");
        s.world.accessObjects().clear();s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();
        s.impactEffects.clear();s.pendingObjectExits.clear();s.deathWeapons.reset();
        s.wrecks.clear();s.buildingCollapses.clear();s.computerAiByOwner.clear();
        s.specialMechanismsInitialized=false;ensureRuntimeSpecialMechanismsInitialized(s);
        s.particles.reset(hz);require(s.particles.alignEmptyStateTick(s.tick),"particle clock");s.particlesInitialized=true;
        rebuildRuntimeTerrainInfoCache(s);initializeRuntimeNavigation(s);
        for(int i=0;i<5;++i){WorldObjectInstance o;o.instanceId=801+i;o.kind=WorldObjectKind::Structure;
            o.mapId="H020HQ"+std::to_string(i);o.owner=std::array<std::string,5>{"defender","ally","attacker","beta","human"}[i];
            o.type="HQ";o.health=1000;o.alive=true;o.poweredOperational=true;
            o.x=i==0?10:i==1?12:11+i;o.y=10;o.cell=runtimeCellKey(o.x,o.y);clearRuntimeRenderPosition(o);
            s.world.accessObjects().push_back(o);}
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        auto threat=*s.rules.findSuperWeaponType("DefenseThreat");threat.targetMode="Area";threat.aiDefendAgainst=true;threat.targetEnemy=true;threat.targetFriendly=false;
        auto shield=*s.rules.findSuperWeaponType("DefenseShield");shield.targetMode="Area";shield.targetEnemy=false;shield.targetFriendly=true;shield.targetInfantry=false;shield.targetUnits=false;shield.targetBuildings=true;
        auto reveal=*s.rules.findSuperWeaponType("OrdinaryReveal");reveal.targetMode="Area";reveal.targetEnemy=true;reveal.targetFriendly=false;
        s.superWeapons.reset(std::vector<SuperWeaponRule>{threat,shield,reveal});s.superWeaponsInitialized=true;
        auto context=runtimeSuperWeaponContext(s);context.tick=std::max(0,s.tick-5);
        for(const std::string& owner:{"defender","beta","human"})require(s.superWeapons.grant(owner,"OrdinaryReveal")&&s.superWeapons.forceReady(owner,"OrdinaryReveal"),"ordinary ready");
        require(s.superWeapons.grant("defender","DefenseShield")&&s.superWeapons.forceReady("defender","DefenseShield"),"defense ready");
        if(scenario!=1)require(s.superWeapons.grant("attacker","DefenseThreat")&&s.superWeapons.forceReady("attacker","DefenseThreat")
            &&s.superWeapons.beginTargetSelection("attacker","DefenseThreat",context,&error)
            &&s.superWeapons.submitTarget("attacker","DefenseThreat",{{13,10},-1},context,&error),"real hostile launch: "+error);
        if(scenario==2)require(s.superWeapons.disable("defender","DefenseShield"),"disable case");
        auto addAi=[&](const std::string& key,const std::string& owner,bool initialized,const std::string& difficulty){RuntimeComputerAiState ai;
            ai.initialized=initialized;ai.owner=owner;ai.difficulty=difficulty;ai.baseCenter={0,0};ai.credits=431;ai.status="original";s.computerAiByOwner.emplace(key,ai);};
        addAi("defender","defender",scenario!=5,scenario==6?"Easy":"Hard");
        addAi("beta","beta",true,"Medium");addAi("empty","  ",true,"Hard");addAi("uninit","attacker",false,"Brutal");
        if(scenario==3){s.tick=hz-1;context.tick=s.tick;}
        context.tick=s.tick;s.superWeapons.tick(context);
        require(!image(s).empty(),"initial full production image");
        const bool changed=reference?original::advance(s):advanceRuntimeSuperWeapons(s);
        if(scenario!=2&&s.superWeapons.stateOf("defender","DefenseShield")!=SuperWeaponRuntimeState::Ready)++positiveDefense;
        if(s.superWeapons.stateOf("beta","OrdinaryReveal")!=SuperWeaponRuntimeState::Ready)++positiveOrdinary;
        observed<<encode(changed)<<image(s);
        if(scenario==0)require(s.superWeapons.stateOf("beta","OrdinaryReveal")==SuperWeaponRuntimeState::Ready,"disabled AI keeps ready weapon");
        if(scenario!=0&&scenario!=5)require(s.computerAiByOwner.at("beta").baseCenter.x==14,"actual host base-center port");
        WorldObjectInstance flight;flight.instanceId=900;flight.mapId="H020Flight";flight.kind=WorldObjectKind::Unit;
        flight.type="POSE_PLANE";flight.owner="defender";flight.alive=true;flight.airborne=true;flight.authoredAircraft=true;flight.health=100;
        flight.mission="SupportFlightEast";flight.x=4;flight.y=4;flight.level=flight.flightLevel=3;flight.cell=runtimeCellKey(4,4);
        flight.hasRenderPosition=flight.renderMoveActive=true;flight.renderX=4.125;flight.renderY=3.875;flight.renderLevel=3.25;flight.movementAnimationDistance=9.5;
        s.world.accessObjects().push_back(flight);
        std::ostringstream poses;poses.precision(7);
        if(reference)original::oldWriteSupportFlightPoses(s,poses);else writeRuntimeSupportFlightPoses(s,poses);
        require(poses.precision()==7,"actual writer preserves stream precision");
        observed<<encode(poses.str());auto doc=poses.str();
        if(scenario==7)doc="[Other]\nVersion=1\n";
        if(scenario==8)doc="[SupportFlightPoses]\nVersion=2\nCount=1\n";
        if(scenario==9)doc="[SupportFlightPoses]\nVersion=1\nCount=1\n0=900|4.125tail|4|3|9\n";
        if(scenario==10)doc="[SupportFlightPoses]\nVersion=1\nCount=1\n0=900|4.125|4|3|1000000001\n";
        if(scenario==11)doc="[SupportFlightPoses]\nVersion=1\nCount=1\n0=900|4.500002|4|3|9\n";
        IniFile saved;require(saved.loadFromString(doc),"actual saved pose parser");
        auto& actualObjects=s.world.accessObjects();actualObjects.back().renderX=-999;error="preexisting-error";
        const bool restored=reference?original::oldRestoreSupportFlightPoses(saved,actualObjects,error):restoreRuntimeSupportFlightPoses(saved,actualObjects,error);
        require(restored==(scenario<8),"original pose acceptance branch "+std::to_string(scenario));if(!restored)++poseRejected;
        observed<<encode(restored)<<encode(error)<<image(s);
        auto continued=runtimeSuperWeaponContext(s);continued.emit=runtimeSuperWeaponEventSink(s);
        for(int step=1;step<=3;++step){++s.tick;continued.tick=s.tick;s.superWeapons.tick(continued);observed<<image(s);}
        ++states;
    }
    return observed.str();
}
int run(const std::string& mapPath){try{
    const auto old=exercise(true,mapPath),now=exercise(false,mapPath);require(old==now,"full real-root save/hash/world87/AI28/snapshots/pose/continued bytes differ");
    require(positiveDefense>0&&positiveOrdinary>0&&poseRejected>0,"positive actual defense/ordinary and negative pose coverage mandatory");
    std::cout<<"private-superweapon-lifecycle checks="<<checks<<" actual-root-states="<<states<<" positive-defense="<<positiveDefense<<" positive-ordinary="<<positiveOrdinary<<" rejected-poses="<<poseRejected<<" real-host-ports-and-fullsave-world87-AI28-continuation=exact failures=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
}
