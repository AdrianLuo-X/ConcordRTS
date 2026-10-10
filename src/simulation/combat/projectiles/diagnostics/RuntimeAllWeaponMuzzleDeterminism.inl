// Integration proof: the same authoritative discharge is independent of the
// local camera, and captured visual fields survive records without entering hash.
for (const std::string type : {"HTNK", "SHK", "ORCA", "BFRT"}) {
    std::uint64_t referenceHash = 0;
    for (const int width : {48, 60, 72}) {
        auto canonical = std::make_unique<RuntimeWindowState>();
        auto& s = *canonical;
        s.config = probe.config; s.report = probe.report;
        s.rules = probe.rules; s.rulesLoaded = true; s.art = probe.art; s.artLoaded = true;
        s.theater = probe.theater; s.theaterLoaded = probe.theaterLoaded;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimeSoundsEnabled = false;
        s.runtimePreferencePersistenceEnabled = false;
        s.camera.tileWidth = width; s.camera.tileHeight = width/2;
        s.camera.worldOffsetX = width*11; s.camera.worldOffsetY = -width*3;
        auto source = fixture(type, type=="SHK" ? WorldObjectKind::Infantry : WorldObjectKind::Unit, 2, 0);
        source.instanceId = 1; source.authoredAircraft = type=="ORCA"; source.airborne = type=="ORCA";
        auto target = targetFor(source); target.instanceId = 2;
        s.world.accessObjects() = {source, target};
        auto shooter = source;
        int hostId = -1;
        RuntimeWeaponProfile profile;
        if (type=="BFRT") {
            auto passenger = fixture("SHK", WorldObjectKind::Infantry, 2, 0);
            passenger.instanceId=3; passenger.alive=false; passenger.mission="InsideTransport";
            s.world.accessObjects()[0].transportPassengerIds={3};
            s.world.accessObjects().push_back(passenger);
            profile=runtimeCombatPassengerWeaponProfile(s, passenger, &target);
            shooter=runtimeCombatFiringPose(passenger, &source); hostId=1;
        } else profile=runtimeWeaponProfileForObject(s, source, &target);
        if (!profile.weapon) { failures.push_back(type+": camera witness weapon missing"); continue; }
        fireRuntimeCombatRound(s, shooter, target, *profile.weapon, profile, 1.0, hostId, false);
        if (s.projectiles.empty()) { failures.push_back(type+": camera witness discharge missing"); continue; }
        // Rebuild the actual native geometry at the displayed camera rather
        // than comparing two paths that share the reference-offset scaling.
        RuntimeMapRenderContext view;
        view.art=&s.art; view.rules=&s.rules; view.theater=s.theaterLoaded?&s.theater:nullptr;
        view.assetRootPath=runtimeAssetRootPath(s.config);
        view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        view.animationTick=s.tick; view.fixedTickHz=s.config.fixedTickHz;
        if (type=="BFRT") {
            const auto fired=s.combatDetails.fireCursors.find(shooter.instanceId);
            if (fired==s.combatDetails.fireCursors.end() || fired->second.sourceAnchorObjectId!=hostId) {
                failures.push_back(type+": actual discharge lost its passenger host");continue;
            }
            // fireRuntimeCombatRound derives this bit on its private firing
            // profile. Reconstruct that observed mode for the visual oracle.
            profile.openTransport=true;
        }
        const auto pose=runtimeWeaponFiringPose(s,shooter,profile);
        const auto port=runtimeOpenTransportMuzzle(s,shooter,profile);
        const auto& visible=port?*port->host:pose;
        const auto flh=port?port->flh:runtimeWeaponFireFlh(s,shooter,profile,runtimeArtTypeForObject(s,shooter)).value_or(RuntimeWeaponFireFlh{});
        const RECT viewport{0,0,1024,768};
        const auto placement=runtimeObjectWorldRenderPlacement(visible,s.world,viewport,view,s.camera,s.renderCache);
        POINT physical{};
        const bool resolved=(!port && resolveRuntimeInfantryWeaponMuzzlePoint(visible,placement.center,view,s.camera,s.renderCache,physical))
            || resolveRuntimeVoxelWeaponMuzzlePoint(visible,placement.center,view,s.camera,s.renderCache,
                flh.forwardLeptons,flh.lateralLeptons,flh.heightLeptons,
                port?std::max(0,visible.facing):s.projectiles.front().attackerFacing,physical,!port);
        const auto displayed=runtimeProjectileActorScreenPoint(makeRuntimeProjectileRenderActor(s.projectiles.front()),viewport,s.camera);
        if (!resolved || std::abs(displayed.x-physical.x)>1 || std::abs(displayed.y-physical.y)>1)
            failures.push_back(type+": real native source at zoom="+std::to_string(width)
                +" delta="+std::to_string(displayed.x-physical.x)+","+std::to_string(displayed.y-physical.y));
        std::string hashError;
        const auto hash=runtimeWorldSimulationHash(s,hashError,false);
        if (!hash || !hashError.empty()) failures.push_back(type+": camera witness hash: "+hashError);
        if (width==48) referenceHash=hash;
        else if (hash!=referenceHash) failures.push_back(type+": local camera changed authoritative discharge");
        const auto savedShot=serializeRuntimeProjectileEffect(s.projectiles.front());
        RuntimeProjectileEffect restored;
        if (!parseRuntimeProjectileEffect(savedShot, restored)
            || restored.sourceScreenOffsetX!=s.projectiles.front().sourceScreenOffsetX
            || restored.sourceScreenOffsetY!=s.projectiles.front().sourceScreenOffsetY
            || restored.sourceNativeFixedX!=s.projectiles.front().sourceNativeFixedX
            || restored.sourceNativeFixedY!=s.projectiles.front().sourceNativeFixedY)
            failures.push_back(type+": captured source lost on restore");
        auto oldFields=splitRuntimeSaveFields(savedShot);
        oldFields.resize(runtimeProjectileSaveFieldCountV5);
        std::ostringstream oldRecord;
        for (std::size_t i=0;i<oldFields.size();++i) { if(i) oldRecord << '|'; oldRecord << oldFields[i]; }
        RuntimeProjectileEffect oldRestored;
        if (!parseRuntimeProjectileEffect(oldRecord.str(),oldRestored,runtimeProjectileSaveSchemaV5)
            || oldRestored.sourceNativeFixedX!=0 || oldRestored.sourceNativeFixedY!=0)
            failures.push_back(type+": legacy v5 projectile rejected or changed visual default");
        for (auto& shot:s.projectiles) { shot.sourceScreenOffsetX+=17; shot.sourceScreenOffsetY-=23; shot.sourceNativeFixedX-=19; shot.sourceNativeFixedY+=21; }
        for (auto& impact:s.impactEffects) { impact.screenOffsetX+=17; impact.screenOffsetY-=23; impact.nativeFixedX-=19; impact.nativeFixedY+=21; }
        if (runtimeWorldSimulationHash(s,hashError,false)!=hash)
            failures.push_back(type+": presentation offsets entered authority hash");
        for (const auto& impact:s.impactEffects) {
            RuntimeImpactEffect restoredImpact;
            if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(impact), restoredImpact)
                || restoredImpact.screenOffsetX!=impact.screenOffsetX || restoredImpact.screenOffsetY!=impact.screenOffsetY
                || restoredImpact.nativeFixedX!=impact.nativeFixedX || restoredImpact.nativeFixedY!=impact.nativeFixedY)
                failures.push_back(type+": muzzle snapshot lost captured visual attachment");
        }
    }
    // Run the audit in a second process with alternate VXL presentation settings
    // and compare these witnesses: the real shot/flash remain authority-identical.
    std::cout<<"Muzzle authority witness "<<type<<'='<<referenceHash<<'\n';
}
// Both launch decks and both slots use the actual parent model's projection.
int generatedDeckChecks=0;
for (const std::string type:{"V3","DRED","BSUB"}) for(int rank:{0,2})
    for(int direction=0;direction<8;++direction) for(int slot=0;slot<(type=="V3"?1:2);++slot) {
        auto source=fixture(type,WorldObjectKind::Unit,direction,rank);
        probe.world.accessObjects()={source};
        const auto logical=runtimeMissileDeckPosition(probe,source,slot);
        const auto visual=runtimeMissileDeckPresentationOffset(probe,source,slot);
        RuntimeProjectileEffect shot;
        shot.attackerObjectId=source.instanceId;shot.hasSourcePosition=true;
        shot.sourceX=logical.x;shot.sourceY=logical.y;shot.sourceZ=logical.z;
        shot.sourceScreenOffsetX=visual.x;shot.sourceScreenOffsetY=visual.y;
        shot.sourceNativeFixedX=visual.fixedX;shot.sourceNativeFixedY=visual.fixedY;
        shot.currentX=logical.x;shot.currentY=logical.y;shot.currentZ=logical.z;
        shot.totalTicks=shot.remainingTicks=1;shot.weaponId="SpawnerDeck";
        appendShot(source,shot,type+"/SpawnerDeck/slot"+std::to_string(slot)+"/rank"+std::to_string(rank)+"/"+std::to_string(source.facing));
        for (int zoom : {48,60,72}) {
            RuntimeMapCamera camera;camera.tileWidth=zoom;camera.tileHeight=zoom/2;
            const auto native=runtimeMissileDeckOffsetAtCamera(probe,source,slot,camera);
            if (std::abs(runtimeMuzzlePixelX(visual.x,camera,visual.fixedX)-native.x)>1
                || std::abs(runtimeMuzzlePixelY(visual.y,camera,visual.fixedY)-native.y)>1)
                failures.push_back(type+": generated launch deck moved with zoom");
        }
        ++generatedDeckChecks;
    }
if (generatedDeckChecks!=80) failures.push_back("generated deck coverage incomplete");
