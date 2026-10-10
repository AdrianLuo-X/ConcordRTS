// Real stock Virusgun deaths and gas exposure. The small particle contracts
// below are labelled separately; they do not stand in for the six UI scenes.
bool runRuntimeVirusFriendlyGasSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string label, stage, observationError;
    const auto fail = [&](const std::string& why) {
        error = "Virus friendly gas " + label + " [" + stage + "]: " + why;
        return false;
    };
    const char* output = std::getenv("CONCORD_VIRUS_FRIENDLY_GAS_OUTPUT");
    const std::filesystem::path root = output && *output ? output : "build/issue22_11_virus_friendly_gas";
    const char* selected = std::getenv("CONCORD_VIRUS_FRIENDLY_GAS_CASE");
    const std::string filter = selected ? selected : "";
    std::filesystem::create_directories(root);
    std::ofstream cases(root / "cases.csv"), damage(root / "damage.csv"), motion(root / "movement.csv"),
        particles(root / "particles.csv"), checkpoints(root / "checkpoints.csv"), contracts(root / "contracts.csv");
    if (!cases || !damage || !motion || !particles || !checkpoints || !contracts) return fail("output unavailable");
    cases << "case,hz,relation,gun_impacts,entered,moved_out,entrant_damage,gas_pulses,cloud_births,dissipation_births,expired,ordinary14,canonical_extension,result\n";
    damage << std::setprecision(17) << "case,stage,tick,actor,weapon,warhead,particle_pulse,source,object,role,type,owner,armor,prone,alive_before,x,y,z,pulse_x,pulse_y,pulse_z,before_hp,after_hp,expected,actual\n";
    motion << std::setprecision(17) << "case,stage,tick,object,role,x,y,z,body_x,body_y,body_z,hp,mission,moving\n";
    particles << std::setprecision(17) << "case,stage,tick,id,type,source,owner,x,y,z,vx,vy,vz,age,remaining,damage_countdown,state,state_countdown\n";
    checkpoints << "case,stage,format,tick,hz,source,transport,entrant,particles,particle_hash,result\n";
    contracts << "case,hz,kind,object,type,original_hp,final_hp,actual_pulses,result\n";

    IniFile raw, rawArt;
    if (!raw.loadFromFile(live.report.rulesPath) || !rawArt.loadFromFile(live.report.artPath))
        return fail("original rules/art unavailable");
    const auto verses = splitRuntimeTokenList(raw.getValue("VirusGas", "Verses", ""));
    const auto gunVerses = splitRuntimeTokenList(raw.getValue("Virus", "Verses", ""));
    const auto* gun = live.rules.findWeaponType("Virusgun");
    const auto* gas = live.rules.findWarheadType("VirusGas");
    const auto* sourceType = live.rules.findUnitType("infantry", "VIRUS");
    const auto* cloudType = live.rules.findParticleType("VirusCloud1");
    const auto* dyingCloudType = live.rules.findParticleType("VirusCloudD1");
    if (!gun || !gas || !sourceType || !cloudType || !dyingCloudType
        || raw.getValue("VIRUS", "Primary", "") != "Virusgun"
        || raw.getValue("VIRUS", "Strength", "") != "100"
        || raw.getValue("VIRUS", "ImmuneToPoison", "") != "yes"
        || raw.getValue("E1", "Strength", "") != "125" || raw.getValue("E1", "Armor", "") != "none"
        || raw.getValue("Virusgun", "Damage", "") != "125" || raw.getValue("Virusgun", "Warhead", "") != "Virus"
        || raw.getValue("Virusgun", "Projectile", "") != "InvisibleLow" || gun->damage != 125
        || gunVerses.size() != 11 || gunVerses[0] != "100%"
        || raw.getValue("General", "InfantryVirus", "") != "VIRUSD"
        || rawArt.getValue("VIRUSD", "SpawnsParticle", "") != "VirusCloud1"
        || rawArt.getValue("VIRUSD", "NumParticles", "") != "3"
        || raw.getValue("VirusCloud1", "Damage", "") != "5"
        || raw.getValue("VirusCloud1", "MaxDC", "") != "30"
        || raw.getValue("VirusCloud1", "MaxEC", "") != "1000"
        || raw.getValue("VirusCloud1", "Radius", "0") != "0"
        || raw.getValue("VirusCloud1", "NextParticle", "") != "VirusCloudD1"
        || raw.getValue("VirusCloudD1", "MaxDC", "") != "60"
        || raw.getValue("VirusCloudD1", "MaxEC", "") != "50"
        || raw.getValue("VirusGas", "Poison", "") != "yes"
        || raw.getValue("VirusGas", "AffectsAllies", "absent") != "absent"
        || raw.getValue("VirusGas", "ProneDamage", "") != "300%"
        || verses.size() != 11 || verses[0] != "100%" || verses[6] != "0%"
        || !gas->poison || !gas->affectsAllies || !gas->affectsEnemies || !sourceType->immuneToPoison
        || cloudType->damage != 5 || cloudType->maxDamageDelay != 30 || cloudType->maxLifetime != 1000 || cloudType->radius != 0
        || dyingCloudType->maxLifetime != 50 || dyingCloudType->maxDamageDelay != 60)
        return fail("original gun, gas, implicit allies, poison immunity or clock declarations differ");
    const auto* noAllies = live.rules.findWarheadType("SuperPsiPulse");
    if (!noAllies || raw.getValue("SuperPsiPulse", "AffectsAllies", "") != "no" || noAllies->affectsAllies)
        return fail("original explicit AffectsAllies=no policy was lost");
    const std::uint64_t rulesHash = live.rules.getDetailRulesHash();
    std::ostringstream resourceMode;
    for (const auto& t : live.rules.getTiberiumTypes()) resourceMode << '[' << t.id << "]\nGrowthPercentage=0\nSpreadPercentage=0\n";
    const std::string resourceModeText = resourceMode.str();
    const auto resourceValid = [&](const RuntimeWindowState& s) {
        if (!s.rulesLoaded || s.rules.getDetailRulesHash() != rulesHash || !s.skirmishModeRulesLoaded) return false;
        for (const auto& t : live.rules.getTiberiumTypes())
            if (runtimeOreRuleProbability(s,t,false) != 0.0 || runtimeOreRuleProbability(s,t,true) != 0.0) return false;
        return s.config.noCooldown == false && s.particles.fixedTickHz() == s.config.fixedTickHz;
    };
    const auto facade = [](RuntimeWindowState& s, const std::string& owner) {
        s.economy.owner = owner; s.config.playerCountry = owner; s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority a; a.owner = a.country = owner; a.canonicalOwner = runtimeCanonicalHumanHouseOwner(owner); a.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(a)); initializeRuntimeOfflineGameplayRouting(s);
    };
    const auto make = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box; seedRuntimeLoadScratch(s,live,false);
        s.tick = 200; s.config.fixedTickHz = hz; s.config.multiplayerActive = s.config.computerAiEnabled = s.config.noCooldown = false;
        s.config.shortGame = s.config.superweapons = s.config.crates = false; s.config.gameMode = "Battle"; s.config.playerSlots.clear();
        s.config.skirmishLoadSavePath.clear(); s.config.gameModeRulesOverride.clear(); s.campaignMode = false;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.runtimeSoundsEnabled = s.visionEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.triggers.loaded = s.triggers.bootstrapComplete = true; s.economy.skirmishStartApplied = true;
        s.skirmishTeamByOwner = {{"yuricountry","1"},{"russians","1"},{"americans","2"}};
        s.skirmishModeAlliesAllowed = true; s.skirmishModeRulesLoaded = s.skirmishModeRules.loadFromString(resourceModeText);
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFactsLoaded = false; s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = s.report.map.height = -1;
        s.behaviors.clear(); s.projectiles.clear(); s.recentExpiredProjectiles.clear(); s.impactEffects.clear();
        s.pendingObjectExits.clear(); s.pendingObjectExitIds.clear(); s.pendingTransportTargets.clear(); s.selectedObjectIds.clear(); s.playerCommands = {};
        s.deathWeapons.reset(); s.humanHouseAuthorities.clear();
        s.specialMechanismsInitialized = s.superWeaponsInitialized = s.subcellsInitialized = false;
        for (int y=44;y<=84;++y) for (int x=44;x<=84;++x) {
            TerrainCell c; c.x=x; c.y=y; c.cellIndex512=y*512+x; c.tileIndex=c.subTileIndex=0; s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeSuperWeaponsInitialized(s); ensureRuntimeLightingInitialized(s);
        s.particles.reset(hz); s.particlesInitialized = s.particles.alignEmptyStateTick(s.tick); facade(s,"YuriCountry"); return box;
    };
    const auto add = [](RuntimeWindowState& s, const std::string& type, RuntimeBuildKind kind, RuntimeCellPoint point, const std::string& owner) {
        auto* o=addRuntimeObjectInstanceForOwner(s,owner,kind,type,point); if (!o) return -1;
        o->mission="Sleep"; o->buildAnimTicksRemaining=o->buildAnimTicksTotal=0; clearRuntimeRenderPosition(*o);
        if (kind==RuntimeBuildKind::Infantry) assignRuntimeInfantrySubcellAtCurrentLocation(s,*o); return o->instanceId;
    };
    const auto hp = [](const RuntimeWindowState& s,int id) { const auto* o=findRuntimeObjectByIdIncludingInactive(s,id); return o?o->health:-1; };
    const auto describe = [](const RuntimeWindowState& s,int id) {
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,id); if (!o) return std::string("missing#")+std::to_string(id);
        std::ostringstream d; d<<std::setprecision(17)<<o->type<<'#'<<id<<" owner="<<o->owner<<" HP="<<o->health<<" alive="<<o->alive
            <<" XYZ="<<o->x<<','<<o->y<<','<<o->level<<" body="<<runtimeObjectRenderX(*o)<<','<<runtimeObjectRenderY(*o)<<','<<runtimeDrawLevelForObject(*o)
            <<" mission="<<o->mission<<" prone="<<o->prone<<" subcell="<<o->subCell;
        if (const auto b=s.behaviors.find(id);b!=s.behaviors.end()) d<<" behavior="<<static_cast<int>(b->second.kind)<<':'<<static_cast<int>(b->second.state)
            <<" target="<<b->second.targetObjectId<<" move="<<b->second.moveTarget.x<<','<<b->second.moveTarget.y<<" cooldown="<<b->second.moveCooldownTicks;
        return d.str();
    };
    const auto nativeStep = [](RuntimeWindowState& s) { RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s,false); completeRuntimeWorldRefresh(s); };
    struct ObserverScope {
        RuntimeWindowState& s; std::function<void(const RuntimeProjectileEffect&,bool)> previous;
        explicit ObserverScope(RuntimeWindowState& state):s(state),previous(std::move(state.runtimeProjectileImpactObserver)){}
        ~ObserverScope(){s.runtimeProjectileImpactObserver=std::move(previous);}
    };
    int passed=0,totalGunImpacts=0,totalPulses=0,ordinarySaves=0,canonicalSaves=0,contractCount=0;
    for (int hz : {30,60}) for (const std::string relation : {"own","ally","enemy"}) {
        label=relation+'-'+std::to_string(hz); if (!filter.empty() && filter!=label) continue;
        stage="native stock fixture"; observationError.clear(); auto box=make(hz); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        const bool representative=relation=="own";
        const std::string owner=relation=="own"?"YuriCountry":relation=="ally"?"Russians":"Americans";
        if (!resourceValid(s) || !runtimeOwnersAreAllied(s,"YuriCountry","Russians") || runtimeOwnersAreAllied(s,"YuriCountry","Americans"))
            return fail("stock clock/team/context initialization");
        const int source=add(s,"VIRUS",RuntimeBuildKind::Infantry,{55,64},"YuriCountry");
        const int hull=add(s,"LCRF",RuntimeBuildKind::Unit,{54,63},"YuriCountry");
        const int victim=add(s,"E1",RuntimeBuildKind::Infantry,{64,64},"Americans");
        if (source<0 || hull<0 || victim<0 || hp(s,source)!=100 || hp(s,hull)!=300 || hp(s,victim)!=125)
            return fail("registered full-health source, amphibious transport and enemy victim");
        const auto* transport=findObjectById(s,hull);
        if (!transport || !runtimeAmphibiousTransport(s,*transport) || runtimeTransportProfileForObject(s,*transport).passengerCapacity!=12
            || runtimeTransportPassengerSizeLimit(s,*transport)!=6) return fail("stock LCRF boarding capabilities");
        struct Tracked { int id; std::string role; };
        std::vector<Tracked> tracked{{source,"source"},{hull,"transport"},{victim,"gun-victim"}};
        struct Before { int hp,x,y,z; bool prone,alive; std::string type,owner,role; };
        std::map<int,std::map<int,Before>> beforeImpacts;
        std::set<int> observedActors; int gunImpacts=0,pulses=0,entrant=-1,neighbor=-1,outside=-1,immune=-1,entrantDamage=0;
        bool exited=false; int exitHealth=-1,neighborExitHealth=-1;
        ObserverScope observer(s);
        s.runtimeProjectileImpactObserver=[&](const RuntimeProjectileEffect& p,bool after) {
            if (!observationError.empty()) return;
            if (!after) {
                if (!p.particleDamagePulse && (p.weaponId!="Virusgun" || p.attackerObjectId!=source || p.targetObjectId!=victim)) {
                    observationError="unrelated real weapon fired: actor="+std::to_string(p.actorId)+" weapon="+p.weaponId+" attacker="+std::to_string(p.attackerObjectId); return;
                }
                if ((p.particleDamagePulse && (p.warheadId!="VirusGas" || p.damage!=5 || p.cellSpread!=0.0 || !p.affectsAllies || !p.affectsEnemies || !p.ag || !p.aa))
                    || (!p.particleDamagePulse && (p.damage!=125 || p.warheadId!="Virus" || p.projectileId!="InvisibleLow"))) {
                    observationError="native gun/pulse declarations differ actor="+std::to_string(p.actorId)+" damage="+std::to_string(p.damage)+" warhead="+p.warheadId; return;
                }
                if (beforeImpacts.contains(p.actorId) || observedActors.contains(p.actorId)) { observationError="duplicate native impact actor"; return; }
                auto& snapshot=beforeImpacts[p.actorId];
                for (const auto& t:tracked) if (const auto* o=findRuntimeObjectByIdIncludingInactive(s,t.id))
                    snapshot[t.id]={o->health,o->x,o->y,o->level,o->prone,o->alive,o->type,o->owner,t.role};
                return;
            }
            const auto found=beforeImpacts.find(p.actorId); if (found==beforeImpacts.end()) { observationError="impact entry was not observed"; return; }
            for (const auto& [id,b]:found->second) {
                int expected=0;
                if (!p.particleDamagePulse) expected=id==victim?125:0;
                else if (b.alive && b.type=="E1" && b.x==p.toCell.x && b.y==p.toCell.y) expected=5*(b.prone?3:1);
                const int afterHp=hp(s,id),actual=b.hp-afterHp;
                damage<<label<<','<<stage<<','<<s.tick<<','<<p.actorId<<','<<p.weaponId<<','<<p.warheadId<<','<<p.particleDamagePulse<<','
                    <<p.attackerObjectId<<','<<id<<','<<b.role<<','<<b.type<<','<<b.owner<<','<<(b.type=="E1"||b.type=="VIRUS"?"none":"light")<<','
                    <<b.prone<<','<<b.alive<<','<<b.x<<','<<b.y<<','<<b.z<<','<<p.targetX<<','<<p.targetY<<','<<p.targetZ<<','<<b.hp<<','<<afterHp<<','<<expected<<','<<actual<<'\n';
                if (actual!=expected) {
                    observationError="raw gas armor/prone/poison ledger actor="+std::to_string(p.actorId)+" expected="+std::to_string(expected)+" actual="+std::to_string(actual)
                        +" pulse="+std::to_string(p.toCell.x)+","+std::to_string(p.toCell.y)+" "+describe(s,id); return;
                }
                if (p.particleDamagePulse && id==entrant) entrantDamage+=actual;
            }
            observedActors.insert(p.actorId); beforeImpacts.erase(found); p.particleDamagePulse?++pulses:++gunImpacts;
        };
        std::unique_ptr<RuntimeWindowState> ordinary,canonical;
        std::set<std::uint64_t> cloudIds,dyingIds;
        int emissionTick=-1,firstDeathTick=-1,caseOrdinary=0,caseCanonical=0;
        const auto virusDeath=[](const RuntimeWindowState& q)->const RuntimeImpactEffect* {
            for (const auto& effect:q.impactEffects) if (effect.lifecycleState=="VirusDeath") return &effect; return nullptr;
        };
        const auto equal=[&]() {
            for (const auto& pair:{std::pair<const char*,RuntimeWindowState*>{"ordinary14",ordinary.get()},{"canonical-extension",canonical.get()}}) {
                if (!pair.second) continue; const auto& q=*pair.second;
                if (!resourceValid(q) || q.tick!=s.tick || q.particles.stateHash()!=s.particles.stateHash()) return fail(std::string(pair.first)+" stock clock/particle state differs");
                for (const auto& t:tracked) {
                    const auto* a=findRuntimeObjectByIdIncludingInactive(s,t.id); const auto* b=findRuntimeObjectByIdIncludingInactive(q,t.id);
                    if (!a || !b || a->health!=b->health || a->alive!=b->alive || a->x!=b->x || a->y!=b->y || a->level!=b->level || a->subCell!=b->subCell
                        || a->prone!=b->prone || a->mission!=b->mission || a->transportPassengerIds!=b->transportPassengerIds
                        || std::abs(runtimeObjectRenderX(*a)-runtimeObjectRenderX(*b))>1e-8 || std::abs(runtimeObjectRenderY(*a)-runtimeObjectRenderY(*b))>1e-8
                        || std::abs(runtimeDrawLevelForObject(*a)-runtimeDrawLevelForObject(*b))>1e-8)
                        return fail(std::string(pair.first)+" native actor differs original="+describe(s,t.id)+" restored="+describe(q,t.id));
                }
                const auto* a=virusDeath(s); const auto* b=virusDeath(q);
                if ((a==nullptr)!=(b==nullptr) || (a && runtimeMultiplayerSnapshotImpactRecord(*a)!=runtimeMultiplayerSnapshotImpactRecord(*b)))
                    return fail(std::string(pair.first)+" saved swelling/burst consumption differs");
            }
            return true;
        };
        const auto step=[&]() {
            nativeStep(s); if (ordinary) nativeStep(*ordinary); if (canonical) nativeStep(*canonical);
            if (!observationError.empty()) return fail(observationError);
            for (const auto& p:s.particles.particles()) {
                if (p.owner!="YuriCountry" || p.sourceObjectId!=source || p.sourceTag!="animation:VIRUSD") return fail("original gas attribution changed");
                if (p.typeId=="VirusCloud1") cloudIds.insert(p.id); else if (p.typeId=="VirusCloudD1") dyingIds.insert(p.id); else return fail("unexpected particle type "+p.typeId);
                particles<<label<<','<<stage<<','<<s.tick<<','<<p.id<<','<<p.typeId<<','<<p.sourceObjectId<<','<<p.owner<<','<<p.position.x/256.0<<','<<p.position.y/256.0<<','<<p.position.z/256.0
                    <<','<<p.velocity.x<<','<<p.velocity.y<<','<<p.velocity.z<<','<<p.ageTicks<<','<<p.remainingLifetime<<','<<p.damageCountdown<<','<<p.stateAI<<','<<p.stateAdvanceCountdown<<'\n';
            }
            if (cloudIds.size()>3 || dyingIds.size()>3) return fail("gas burst or dissipation was replayed");
            for (const auto& t:tracked) if (const auto* o=findRuntimeObjectByIdIncludingInactive(s,t.id))
                motion<<label<<','<<stage<<','<<s.tick<<','<<t.id<<','<<t.role<<','<<o->x<<','<<o->y<<','<<o->level<<','<<runtimeObjectRenderX(*o)<<','<<runtimeObjectRenderY(*o)<<','
                    <<runtimeDrawLevelForObject(*o)<<','<<o->health<<','<<o->mission<<','<<o->renderMoveActive<<'\n';
            if (hp(s,source)!=100 || hp(s,hull)!=300 || (immune>=0 && hp(s,immune)!=100) || (outside>=0 && hp(s,outside)!=125)) return fail("original source/immune/outside health changed");
            if (exited && (hp(s,entrant)!=exitHealth || hp(s,neighbor)!=neighborExitHealth)) return fail("poison continued after actual exit");
            return equal();
        };
        const auto wait=[&](int budget,const auto& ready,const std::string& why) { for (int n=0;n<budget;++n) { if (ready()) return true; if (!step()) return false; } return ready()?true:fail(why); };
        const auto replicas=[&]() { return std::array<RuntimeWindowState*,3>{{&s,ordinary.get(),canonical.get()}}; };
        const auto move=[&](int id,const std::string& issuing,RuntimeCellPoint point) {
            for (auto* q:replicas()) if (q) { RuntimeSnapshotActiveStateScope scope(*q); facade(*q,issuing); q->selectedObjectIds={id};
                if (!dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::Move,point) || q->lastCommand.acceptedCount!=1)
                    return fail("real UI Move rejected: "+describe(*q,id)); }
            return equal();
        };
        const auto checkpoint=[&]() {
            for (bool exact:{false,true}) {
                std::ostringstream payload,normalized; IniFile base,extension;
                if (!writeRuntimeStateToStream(s,payload,error) || !base.loadFromString(payload.str()) || base.getValue("RuntimeSave","Version","")!="14") return fail("ordinary14 save: "+error);
                const std::string suffix=exact?"canonical-extension":"ordinary14";
                if (exact) {
                    if (!writeRuntimeMultiplayerSnapshotExtension(s,payload,error) || !extension.loadFromString(payload.str())
                        || !writeRuntimeMultiplayerSnapshotCanonicalIni(extension,normalized,error) || !extension.loadFromString(normalized.str())) return fail("canonical extension: "+error);
                }
                std::ofstream saved(root/(label+'-'+stage+'-'+suffix+".ini"),std::ios::binary); saved<<(exact?normalized.str():payload.str());
                auto copy=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy,s,false); RuntimeSnapshotActiveStateScope scope(*copy);
                if (!materializeRuntimeLoadStateFromIni(*copy,base,error) || (exact&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,extension,error))) return fail("strict restore: "+error);
                if (copy->runtimeProjectileImpactObserver || !resourceValid(*copy)) return fail("restore lost fixture context or retained observer");
                facade(*copy,s.economy.owner); copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;
                checkpoints<<label<<','<<stage<<','<<(exact?"ordinary14+canonical-extension":"ordinary14")<<','<<copy->tick<<','<<hz<<','<<source<<','<<hull<<','<<entrant<<','<<copy->particles.particles().size()<<','<<copy->particles.stateHash()<<",PASS\n";
                if (exact) {canonical=std::move(copy);++canonicalSaves;++caseCanonical;} else {ordinary=std::move(copy);++ordinarySaves;++caseOrdinary;}
            }
            return equal();
        };
        stage="original UI Virusgun"; facade(s,"YuriCountry"); s.selectedObjectIds={source};
        if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{64,64},findObjectById(s,victim)) || s.lastCommand.acceptedCount!=1)
            return fail("real enemy Primary rejected: "+s.interactionStatus);
        if (!wait(hz*10,[&](){return hp(s,victim)==0 && virusDeath(s)!=nullptr;},"original full-health victim never died")) return false;
        firstDeathTick=s.tick;
        if (gunImpacts!=1 || !s.particles.particles().empty()) return fail("native single lethal gun or delayed gas contract");
        const auto* death=virusDeath(s);
        // Effects applies the real projectile before advancing the newly
        // created death timeline, consuming its first tick in the birth tick.
        if (!death || death->animationFrameCount!=35 || death->deferredParticleTicks!=hz*7/5-1 || death->frameIndex!=0
            || death->owner!="Americans" || death->deferredParticleOwner!="YuriCountry") return fail("original swelling/frame21 source attribution");
        stage="real UI source boarding"; s.selectedObjectIds={source};
        if (!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop)) return fail("source Stop rejected");
        if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*findObjectById(s,hull)),findObjectById(s,hull))
            || !s.pendingTransportTargets.contains(source) || s.pendingTransportTargets.at(source)!=hull)
            return fail("source Primary did not queue original transport: "+describe(s,source)+" status="+s.interactionStatus);
        if (!wait(hz,[&](){const auto* o=findRuntimeObjectByIdIncludingInactive(s,source);const auto* h=findObjectById(s,hull);
                return o && h && !o->alive && o->health==100 && o->mission=="InsideTransport" && h->transportPassengerIds==std::vector<int>{source};},"source did not naturally enter adjacent LCRF")) return false;
        entrant=add(s,"E1",RuntimeBuildKind::Infantry,{65,64},owner);
        neighbor=add(s,"E1",RuntimeBuildKind::Infantry,{64,65},owner);
        outside=add(s,"E1",RuntimeBuildKind::Infantry,{68,68},owner);
        immune=add(s,"VIRUS",RuntimeBuildKind::Infantry,{64,64},owner);
        if (entrant<0 || neighbor<0 || outside<0 || immune<0 || hp(s,entrant)!=125 || hp(s,neighbor)!=125 || hp(s,immune)!=100) return fail("full-health native exposure actors");
        tracked.insert(tracked.end(),{{entrant,"entrant"},{neighbor,"neighbor"},{outside,"outside"},{immune,"poison-immune"}});
        stage="pre-rupture";
        if (!wait(hz*2,[&](){const auto* d=virusDeath(s);return d && d->deferredParticleTicks==1;},"missed pending natural rupture checkpoint")) return false;
        if (representative && !checkpoint()) return false;
        if (!step()) return false; emissionTick=s.tick;
        if (emissionTick-firstDeathTick+1!=hz*7/5 || cloudIds.size()!=3 || s.particles.particles().size()!=3 || !virusDeath(s)
            || virusDeath(s)->deferredParticleTicks!=-1 || virusDeath(s)->frameIndex!=21) return fail("natural rupture did not emit exactly three frame21 clouds");
        stage="real UI entrant exposure";
        if (!move(entrant,owner,{64,64})) return false;
        if (!wait(hz*5,[&](){const auto* o=findObjectById(s,entrant);return entrantDamage>0 && o && o->x==64 && o->y==64
                && std::lround(runtimeObjectRenderX(*o))==64 && std::lround(runtimeObjectRenderY(*o))==64;},"entrant never naturally entered and received original gas")) return false;
        if (hp(s,entrant)<=0 || hp(s,neighbor)!=125) return fail("first poison exposure killed entrant or broadened into adjacent cell");
        stage="real UI exit";
        if (!move(entrant,owner,{68,67}) || !move(neighbor,owner,{69,65})) return false;
        if (!wait(hz*15,[&](){const auto* e=findObjectById(s,entrant);const auto* n=findObjectById(s,neighbor);return e && n && e->x==68 && e->y==67
                && n->x==69 && n->y==65 && !e->renderMoveActive && !n->renderMoveActive && !s.behaviors.contains(entrant) && !s.behaviors.contains(neighbor);},"native exposure actors never settled outside")) return false;
        exited=true; exitHealth=hp(s,entrant); neighborExitHealth=hp(s,neighbor); if (exitHealth<=0 || neighborExitHealth<=0) return fail("natural exit actors died");
        stage="stable-cloud";
        if (representative) {
            if (!wait(hz*3,[&](){return !s.particles.particles().empty() && std::all_of(s.particles.particles().begin(),s.particles.particles().end(),[](const auto& p){return p.damageCountdown>1;});},"no stable inter-pulse checkpoint")) return false;
            if (!checkpoint()) return false;
            stage="natural-expiry";
            if (!wait(hz*72,[&](){return s.particles.particles().empty();},"original gas and dissipation never expired")) return false;
            // The emission tick advances Cloud1 once; the chained D1 starts
            // at age zero. The inclusive lifetime is exactly 1050 rule frames.
            if (cloudIds.size()!=3 || dyingIds.size()!=3 || s.tick-emissionTick+1!=hz*70) return fail("original 1000+50 rule-frame cloud lifetime changed");
        }
        stage="exit-hold"; const int pulsesBefore=pulses;
        for (int n=0;n<hz*5;++n) if (!step()) return false;
        if (representative && pulses!=pulsesBefore) return fail("expired cloud emitted further pulses");
        if (gunImpacts!=1 || entrantDamage<=0 || !beforeImpacts.empty()) return fail("incomplete real gun/poison observer ledger");
        cases<<label<<','<<hz<<','<<relation<<','<<gunImpacts<<",1,1,"<<entrantDamage<<','<<pulses<<','<<cloudIds.size()<<','<<dyingIds.size()<<','<<representative<<','<<caseOrdinary<<','<<caseCanonical<<",PASS\n";
        ++passed; totalGunImpacts+=gunImpacts; totalPulses+=pulses;
        std::cout<<"Virus friendly gas "<<label<<" PASS realGun="<<gunImpacts<<" poison="<<entrantDamage<<" pulses="<<pulses<<" source=nativeLCRF ordinary14="<<caseOrdinary<<" canonical="<<caseCanonical<<'\n';
    }
    // Original-particle contracts: a cloud actually pulses over an existing
    // wood building, and over a poison-immune original VIRUS. They do not
    // create illegal infantry inside a building or broaden the real scenes.
    if (filter.empty() || filter=="particle-contract") for (int hz:{30,60}) {
        label="particle-contract-"+std::to_string(hz); stage="original particle controls"; auto box=make(hz); auto& s=*box; RuntimeSnapshotActiveStateScope active(s);
        const int source=add(s,"VIRUS",RuntimeBuildKind::Infantry,{54,54},"YuriCountry");
        const int power=add(s,"GAPOWR",RuntimeBuildKind::Building,{74,74},"Americans");
        const int immune=add(s,"VIRUS",RuntimeBuildKind::Infantry,{64,64},"Americans");
        if (source<0 || power<0 || immune<0 || hp(s,power)!=750 || hp(s,immune)!=100 || raw.getValue("GAPOWR","Armor","")!="wood") return fail("original control actors");
        for (RuntimeCellPoint p:std::array<RuntimeCellPoint,2>{{{74,74},{64,64}}}) {
            RuntimeParticleSpawnContext context; context.sourceObjectId=source; context.owner="YuriCountry"; context.sourceTag="original-particle-contract";
            context.position={p.x*256.0,p.y*256.0,0};
            if (!s.particles.spawnParticle(s.rules,"VirusCloud1",context)) return fail("original controlled cloud spawn");
        }
        int powerPulses=0,immunePulses=0; ObserverScope observer(s);
        s.runtimeProjectileImpactObserver=[&](const RuntimeProjectileEffect& p,bool after) {
            if (after || !p.particleDamagePulse || p.warheadId!="VirusGas") return;
            if (p.toCell==RuntimeCellPoint{74,74}) ++powerPulses; if (p.toCell==RuntimeCellPoint{64,64}) ++immunePulses;
        };
        for (int n=0;n<hz*4;++n) nativeStep(s);
        if (powerPulses!=2 || immunePulses!=2 || hp(s,power)!=750 || hp(s,immune)!=100) return fail("original zero-Verses/poison-immune cloud contract");
        contracts<<label<<','<<hz<<",zero-verses,"<<power<<",GAPOWR,750,"<<hp(s,power)<<','<<powerPulses<<",PASS\n"
            <<label<<','<<hz<<",poison-immune,"<<immune<<",VIRUS,100,"<<hp(s,immune)<<','<<immunePulses<<",PASS\n";
        contractCount+=2;
    }
    if ((filter.empty() && (passed!=6 || totalGunImpacts!=6 || ordinarySaves!=4 || canonicalSaves!=4 || contractCount!=4)) || (passed==0 && contractCount==0)) return fail("exact native/save/control coverage count");
    std::cout<<"Virus friendly gas passed: cases="<<passed<<" realGunDeaths="<<totalGunImpacts<<" gasPulses="<<totalPulses<<" ordinary14="<<ordinarySaves<<" canonicalExtensions="<<canonicalSaves<<" particleContracts="<<contractCount<<" originalRulesHash="<<rulesHash<<'\n';
    return true;
}
