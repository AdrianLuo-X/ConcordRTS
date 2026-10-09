// Focused complements to the all-type matrix. These cases retain the cached
// Shell projection between same-tick changes, so invalidation is tested too.
bool runRuntimeUnitSelectionSpecialSmoke(RuntimeWindowState& s,
    const RuntimeMapRenderContext& sourceContext, const std::string& root,
    std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "unit special alignment: " + why; return false;
    };
    const RECT viewport{0,0,640,480};
    const int hz = std::max(1,s.config.fixedTickHz);
    if (s.rules.getSpecialMechanismRules().defaultMirageDisguises.empty())
        return fail("stock Mirage terrain disguise list missing");
    std::ofstream rows(root + "/unit-special-poses.csv");
    rows << "case,shell_checked,layers,pixels,outside,exact,left,top,right,bottom\n";
    int checks = 0, shellChecks = 0;
    const auto makeObject = [&](const std::string& type, WorldObjectKind kind) {
        WorldObjectInstance object;
        object.instanceId=15101; object.type=type; object.kind=kind;
        object.owner=s.economy.owner; object.alive=true;
        object.x=38; object.y=61; object.cell=runtimeCellKey(38,61);
        object.facing=96; object.turretFacing=160; object.mission="Guard";
        const auto* unit=findRuntimeUnitType(s.rules,object);
        object.health=unit ? std::max(1,unit->strength) : 100;
        object.currentAmmo=unit ? unit->ammo : -1;
        return object;
    };
    const auto reset = [&](const WorldObjectInstance& object) {
        s.world.accessObjects()={object}; s.impactEffects.clear();
        s.specialMechanisms.reset(s.rules.getSpecialMechanismRules());
        s.specialMechanismsInitialized=true; s.spies.reset();
        s.playerCommunication.cheerUntilTick.clear(); s.selectedObjectIds.clear();
        s.tick=hz*10;
        s.camera={}; s.camera.initialized=true;
        const auto p=runtimeProjectCellXY(object.x,object.y,object.level,s.camera);
        s.camera.worldOffsetX=p.x-320; s.camera.worldOffsetY=p.y-240;
        invalidateRuntimeObjectProjectionCache(s);
    };
    const auto productionContext = [&]() {
        auto context=sourceContext;
        context.animationTick=s.tick; context.fixedTickHz=hz;
        context.impactActors=runtimeImpactRenderActorsForRender(s);
        refreshRuntimeSpecialMechanismRenderSnapshots(s);
        context.specialMechanismSnapshots=&s.specialMechanismRenderSnapshots;
        context.playerCheerUntilTick=&s.playerCommunication.cheerUntilTick;
        context.selectedObjectIds=&s.selectedObjectIds;
        return context;
    };
    const auto fingerprint = [](const RuntimeMobileSpriteBoundsProbe& probe) {
        unsigned long long hash=1469598103934665603ULL;
        for (auto pixel:probe.pixels) { hash ^= pixel; hash *= 1099511628211ULL; }
        return hash;
    };
    const auto check = [&](const std::string& label, const WorldObjectInstance& object,
        const RuntimeMapRenderContext& context, bool shell,
        RuntimeMobileSpriteBoundsProbe& probe) {
        ensureRuntimeCameraForViewport(s,viewport);
        const POINT center=runtimeObjectBodyAnchorForRender(object,viewport,context,s.camera);
        if (!verifyRuntimeMobileObjectSpriteBounds(object,center,context,s.camera,s.renderCache,probe))
            return fail(label+": "+probe.diagnostic);
        RECT frame=runtimeUnitSelectionBoundsForSprite(probe.renderedBounds);
        if (shell) {
            const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
            const auto found=std::find_if(projected.begin(),projected.end(),[&](const auto& candidate) {
                return candidate.object->instanceId==object.instanceId;
            });
            if (found==projected.end() || !runtimeRectEquals(found->spriteBounds,probe.renderedBounds))
                return fail(label+": cached Shell geometry differs from actual pixels");
            frame=found->selectionBounds;
            if (frame.left>probe.renderedBounds.left || frame.top>probe.renderedBounds.top
                || frame.right<probe.renderedBounds.right || frame.bottom<probe.renderedBounds.bottom)
                return fail(label+": selection does not contain body");
            for (const POINT point:{POINT{frame.left,frame.top},POINT{frame.right-1,frame.top},
                    POINT{frame.left,frame.bottom-1},POINT{frame.right-1,frame.bottom-1}}) {
                const auto* hit=findProjectedObjectAtPoint(projected,point);
                if (!hit || hit->object->instanceId!=object.instanceId)
                    return fail(label+": frame corner did not select object");
            }
            ++shellChecks;
        }
        rows << label << ',' << shell << ',' << probe.layerCount << ',' << probe.renderedPixels
             << ',' << probe.outsidePixels << ',' << probe.exactMatch << ',' << probe.spriteBounds.left
             << ',' << probe.spriteBounds.top << ',' << probe.spriteBounds.right << ',' << probe.spriteBounds.bottom << '\n';
        auto pixels=probe.pixels;
        const auto dot=[&](LONG x,LONG y) {
            x-=probe.imageOrigin.x; y-=probe.imageOrigin.y;
            if (x>=0 && y>=0 && x<probe.imageWidth && y<probe.imageHeight)
                pixels[static_cast<std::size_t>(y)*probe.imageWidth+x]=0x000080ffU;
        };
        for (LONG x=frame.left;x<frame.right;++x) {dot(x,frame.top);dot(x,frame.bottom-1);}
        for (LONG y=frame.top;y<frame.bottom;++y) {dot(frame.left,y);dot(frame.right-1,y);}
        if (!writeRuntimeSmokeBmp(root+"/special-"+label+".bmp",pixels.data(),
                probe.imageWidth,probe.imageHeight,error)) return false;
        ++checks; return true;
    };

    auto tank=makeObject("HTNK",WorldObjectKind::Unit); reset(tank);
    RuntimeMobileSpriteBoundsProbe rest, changed;
    if (!check("tank-rest",tank,productionContext(),true,rest)) return false;
    RuntimeImpactEffect impact;
    impact.renderIntent="warheadImpact"; impact.warheadId="V3WH";
    impact.damage=300; impact.totalTicks=hz; impact.remainingTicks=hz-std::max(1,hz/10);
    impact.hasWorldPosition=true; impact.worldX=tank.x+0.7; impact.worldY=tank.y;
    impact.worldLevel=tank.level; impact.cell={tank.x+1,tank.y};
    s.impactEffects={impact};
    if (!check("tank-blast-same-tick",tank,productionContext(),true,changed)) return false;
    if (fingerprint(rest)==fingerprint(changed)) return fail("blast fixture did not rotate visible body");
    s.impactEffects.clear();
    if (!check("tank-settled-same-tick",tank,productionContext(),true,changed)) return false;
    if (fingerprint(rest)!=fingerprint(changed)) return fail("cleared blast did not return to original pixels");

    // Changing an independent turret bearing cannot reuse a hull-only key.
    for (int facing:{0,7,31,63,97,129,177,223,255}) {
        auto& live=s.world.accessObjects().front(); live.turretFacing=facing;
        runtimeAdvanceFacingClock(live.turretFacingClock,facing,(facing+64)%256,5,s.tick,hz);
        if (!check("turret-"+std::to_string(facing),live,productionContext(),true,changed)) return false;
    }
    const auto signature=runtimeProjectedObjectsSignature(s);
    s.world.accessObjects().front().turretFacingClock.elapsedNumerator+=hz;
    if (runtimeProjectedObjectsSignature(s)==signature) return fail("active facing clock omitted from cache signature");

    reset(tank);
    auto drone=makeObject("DRON",WorldObjectKind::Unit);
    drone.instanceId=15102; drone.owner="Russians"; drone.x=tank.x-1;
    s.world.accessObjects().push_back(drone);
    SpecialMechanismAction limbo;
    std::string reason;
    if (!s.specialMechanisms.tryAttachParasite(s.world.getObjects(),s.rules,
            drone.instanceId,tank.instanceId,"DroneJump",s.tick,hz,limbo,{},reason))
        return fail("stock parasite attachment: "+reason);
    s.world.accessObjects().back().alive=false;
    s.world.accessObjects().back().mission="ParasiteLimbo";
    s.tick += s.specialMechanisms.parasiteLinks().at(tank.instanceId).damageIntervalTicks;
    s.specialMechanisms.tickParasites(s.world.getObjects(),s.rules,s.tick,{});
    ++s.tick;
    if (!check("tank-parasite-bite",tank,productionContext(),true,changed)) return false;
    if (fingerprint(rest)==fingerprint(changed)) return fail("parasite fixture did not rock body");

    auto para=makeObject("E1",WorldObjectKind::Infantry); reset(para);
    s.world.accessObjects().front().airborne=true;
    s.world.accessObjects().front().mission="ParadropDrop";
    s.world.accessObjects().front().level=4;
    for (double distance:{0.0,0.5,1.25}) {
        auto& live=s.world.accessObjects().front(); live.movementAnimationDistance=distance;
        if (!check("parachute-"+std::to_string(static_cast<int>(distance*100)),live,
                productionContext(),true,changed)) return false;
        if (changed.layerCount<2) return fail("parachute case omitted canopy pixels");
    }

    auto mirage=makeObject("MGTK",WorldObjectKind::Unit); reset(mirage);
    if (!check("mirage-chassis",mirage,productionContext(),true,rest)) return false;
    s.tick=std::max(s.tick,s.rules.getSpecialMechanismRules().disguiseDelay+1);
    s.specialMechanisms.tickCamouflage(s.world.accessObjects(),s.rules,s.tick,{},{});
    if (!s.specialMechanisms.isCamouflaged(mirage.instanceId)) return fail("stock Mirage did not camouflage");
    if (!check("mirage-tree",mirage,productionContext(),true,changed)) return false;
    if (fingerprint(rest)==fingerprint(changed)) return fail("camouflage kept chassis pixels");
    const auto treeContext=productionContext();
    std::vector<SpecialMechanismRenderSnapshot> treeSnapshots=*treeContext.specialMechanismSnapshots;
    for (auto& snapshot:treeSnapshots) if(snapshot.kind=="camouflage") snapshot.progress=0;
    auto ownerContext=treeContext; ownerContext.specialMechanismSnapshots=&treeSnapshots;
    ownerContext.visionObserverOwner=mirage.owner;
    ownerContext.animationTick=0;
    if (!check("mirage-owner-chassis-phase",mirage,ownerContext,false,rest)) return false;
    ownerContext.animationTick=hz;
    if (!check("mirage-owner-tree-phase",mirage,ownerContext,false,changed)) return false;
    if (fingerprint(rest)==fingerprint(changed)) return fail("owner disguise blink did not change artwork");

    auto spy=makeObject("SPY",WorldObjectKind::Infantry); spy.owner="Russians"; reset(spy);
    const auto disguised=s.spies.initializeDefaultDisguise(makeRuntimeSpyContext(s),spy.instanceId,
        RuntimeSpySide::Allied,s.tick);
    if (!disguised.accepted) return fail("stock spy disguise initialization");
    const auto spyContext=productionContext();
    if (std::none_of(s.specialMechanismRenderSnapshots.begin(),s.specialMechanismRenderSnapshots.end(),
            [](const auto& snapshot){return snapshot.kind=="spyDisguise";}))
        return fail("opposing observer did not receive spy disguise");
    if (!check("spy-observer-disguise",spy,spyContext,true,changed)) return false;
    // Vehicle/Terrain spy projections are renderer-supported mod identities;
    // stock SPY's authoritative MakeupKit permits infantry targets only.
    for (const auto& [phase,type]:std::vector<std::pair<std::string,std::string>>{
            {"Infantry","E2"},{"Vehicle","HTNK"},{"Terrain",s.rules.getSpecialMechanismRules().defaultMirageDisguises.front()}}) {
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind="spyDisguise"; snapshot.objectId=spy.instanceId;
        snapshot.resourceId=type; snapshot.phase=phase; snapshot.owner=s.economy.owner;
        std::vector<SpecialMechanismRenderSnapshot> snapshots{snapshot};
        auto context=productionContext(); context.specialMechanismSnapshots=&snapshots;
        if (!check("spy-renderer-"+phase,spy,context,false,changed)) return false;
    }

    // The owner's blink must reach both the real compositor and the cached
    // picking projection. Keep the cache across phase changes and selection.
    spy=makeObject("SPY",WorldObjectKind::Infantry); reset(spy);
    const auto ownerDisguise=s.spies.initializeDefaultDisguise(makeRuntimeSpyContext(s),
        spy.instanceId,RuntimeSpySide::Allied,s.tick);
    if (!ownerDisguise.accepted) return fail("owner Spy default disguise initialization");
    const std::string defaultSpyType=s.spies.disguiseState(spy.instanceId)->disguiseType;
    const auto samePixels = [](const RuntimeMobileSpriteBoundsProbe& left,
        const RuntimeMobileSpriteBoundsProbe& right) {
        return left.imageWidth==right.imageWidth && left.imageHeight==right.imageHeight
            && left.imageOrigin.x==right.imageOrigin.x && left.imageOrigin.y==right.imageOrigin.y
            && runtimeRectEquals(left.renderedBounds,right.renderedBounds)
            && left.pixels==right.pixels;
    };
    const auto checkOwnerSpy = [&](const std::string& label, const std::string& expectedType,
        const std::string& expectedOwner, RuntimeMobileSpriteBoundsProbe& actual) {
        const auto context=productionContext();
        if (!check(label,spy,context,true,actual)) return false;
        WorldObjectInstance reference=spy;
        reference.type=expectedType; reference.owner=expectedOwner;
        auto referenceContext=context;
        referenceContext.specialMechanismSnapshots=nullptr;
        RuntimeMobileSpriteBoundsProbe expected;
        if (!check(label+"-reference",reference,referenceContext,false,expected)) return false;
        if (!samePixels(actual,expected))
            return fail(label+": body pixels differ from the same-frame "+expectedType+" reference");
        const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
        const auto found=std::find_if(projected.begin(),projected.end(),[&](const auto& candidate) {
            return candidate.object->instanceId==spy.instanceId;
        });
        if (found==projected.end() || found->object->type!="SPY"
            || found->object->owner!=spy.owner)
            return fail(label+": appearance replaced the selected Spy's identity");
        return true;
    };
    const int defaultStarted=s.tick;
    if (!checkOwnerSpy("spy-owner-default-body", "SPY",spy.owner,rest)) return false;
    s.tick=defaultStarted+hz;
    if (!checkOwnerSpy("spy-owner-default-disguise",defaultSpyType,spy.owner,changed)) return false;
    if (samePixels(rest,changed)) return fail("owner default disguise never changed the visible body");
    s.tick=defaultStarted+4*hz;
    if (!checkOwnerSpy("spy-owner-default-return","SPY",spy.owner,rest)) return false;

    auto disguiseTarget=makeObject("E2",WorldObjectKind::Infantry);
    disguiseTarget.instanceId=15102; disguiseTarget.owner="Russians";
    disguiseTarget.x+=8; disguiseTarget.cell=runtimeCellKey(disguiseTarget.x,disguiseTarget.y);
    s.world.accessObjects().push_back(disguiseTarget);
    const auto* spyRules=findRuntimeUnitType(s.rules,spy);
    const auto* makeup=spyRules ? s.rules.findWeaponType(spyRules->primaryWeapon) : nullptr;
    if (!makeup) return fail("stock Spy MakeupKit is missing");
    const auto copied=s.spies.applyMakesDisguise(makeRuntimeSpyContext(s),spy.instanceId,
        disguiseTarget.instanceId,makeup->warhead,s.tick);
    if (!copied.accepted) return fail("stock Spy could not copy E2: "+std::string(runtimeSpyVetoName(copied.veto)));
    const int copiedStarted=s.tick;
    if (!checkOwnerSpy("spy-owner-copied-body","SPY",spy.owner,rest)) return false;
    s.tick=copiedStarted+hz;
    if (!checkOwnerSpy("spy-owner-copied-E2","E2",disguiseTarget.owner,rest)) return false;
    const auto disguiseHash=s.spies.stateHash();
    s.selectedObjectIds={spy.instanceId};
    if (!checkOwnerSpy("spy-owner-copied-selected","E2",disguiseTarget.owner,changed)) return false;
    if (!samePixels(rest,changed) || s.spies.stateHash()!=disguiseHash)
        return fail("selecting the owner Spy changed its disguise or body pixels");
    s.tick=copiedStarted+4*hz;
    if (!checkOwnerSpy("spy-owner-copied-return","SPY",spy.owner,changed)) return false;

    auto infantry=makeObject("E1",WorldObjectKind::Infantry); reset(infantry);
    if (!check("infantry-idle",infantry,productionContext(),true,rest)) return false;
    s.playerCommunication.cheerUntilTick[infantry.instanceId]=s.tick+hz;
    if (!check("infantry-cheer-same-tick",infantry,productionContext(),true,changed)) return false;
    if (fingerprint(rest)==fingerprint(changed)) return fail("cheer fixture did not change animation pixels");

    auto missing=makeObject("__MISSING_SELECTION_BODY__",WorldObjectKind::Unit);
    auto context=productionContext(); context.specialMechanismSnapshots=nullptr;
    if (verifyRuntimeMobileObjectSpriteBounds(missing,{320,240},context,s.camera,s.renderCache,changed)
        || changed.resourceResolved || changed.diagnostic.empty())
        return fail("missing body was silently treated as a successful pixel probe");
    std::cout << "Special unit alignment passed: cases=" << checks << " shell=" << shellChecks
              << " blast/parasite/parachute/disguise/clock/cheer/missing=ok\n";
    return true;
}
