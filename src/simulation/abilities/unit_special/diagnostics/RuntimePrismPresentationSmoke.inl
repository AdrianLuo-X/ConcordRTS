bool runRuntimePrismBeamRenderContractCheck(std::string& error);
bool runRuntimePrismChargeRenderContractCheck(const RuntimeMapRenderContext& context,
    WorldObjectInstance tower, const std::string& root, std::string& error);

bool runRuntimePrismChargeCycleSmoke(RuntimeWindowState& s, int sourceId, int targetId,
    const std::string& root, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Prism charge cycle: " + why; return false; };
    s.config.noCooldown = false;
    auto* source = findMutableObjectById(s, sourceId);
    if (!source) return fail("source missing");
    const auto* target = findObjectById(s, targetId);
    if (!target) return fail("target missing");
    const auto profile = runtimeWeaponProfileForObject(s, *source, target);
    if (!profile.weapon) return fail("weapon missing");
    RuntimeMapRenderContext context;
    context.rules = &s.rules; context.art = &s.art; context.theater = &s.theater;
    context.assetRootPath = runtimeAssetRootPath(s.config);
    context.structureRootPath = runtimeStructureRootPath(s.config, s.report.map);
    context.palettePath = runtimePalettePath(s.config, s.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
    if (!runRuntimePrismChargeRenderContractCheck(context, *source, root, error)) return false;
    const int originalHz = s.config.fixedTickHz;
    for (int hz : {15, 30, 60}) {
        s.config.fixedTickHz = hz; s.tick = hz * 11 + 1;
        source->weaponCooldownTicksRemaining = 0; source->chargeTurretStartTick = -1; source->mission = "Guard";
        s.projectiles.clear(); s.behaviors.clear();
        applySelection(s, {sourceId}, false, false); issueRuntimeAttackCommand(s, *target);
        if (!s.behaviors.contains(sourceId)) return fail("attack command rejected");
        const int prefire = runtimeWeaponPrefireDelayTicks(s, *source, *profile.weapon);
        int cycles = 0, shots = 0, chargeStart = -1;
        bool charging = false;
        for (int n = 0; n < hz * 30 && shots < 3; ++n) {
            ++s.tick; advanceRuntimeWeaponCooldowns(s);
            const auto count = s.projectiles.size();
            advanceRuntimeAttackBehavior(s, *source, s.behaviors.at(sourceId));
            const bool nowCharging = source->mission == "DelayedFire";
            if (nowCharging && !charging) {
                ++cycles; chargeStart = s.tick;
                if (source->chargeTurretStartTick != s.tick) return fail("charge did not reset to this attack");
            }
            if (nowCharging && source->chargeTurretStartTick != chargeStart) return fail("charge restarted before firing");
            if (s.projectiles.size() > count) {
                if (!charging || nowCharging || s.tick - chargeStart < prefire || cycles != shots + 1
                    || s.projectiles.size() != count + 1) return fail("shot without exactly one complete charge");
                ++shots;
            }
            charging = nowCharging;
        }
        if (cycles != 3 || shots != 3) return fail("three charged attacks did not finish at Hz=" + std::to_string(hz));
        std::cout << "Prism charge cycle: Hz=" << hz << " charges=" << cycles << " shots=" << shots << " ok\n";
    }
    s.config.fixedTickHz = originalHz; s.tick = 0; s.projectiles.clear(); s.behaviors.clear();
    source->mission = "Guard"; source->weaponCooldownTicksRemaining = 0; source->chargeTurretStartTick = -1;
    return true;
}

#include "simulation/abilities/unit_special/diagnostics/RuntimePrismShrapnelSmoke.inl"
#include "simulation/abilities/unit_special/diagnostics/RuntimePrismSupportWidthSmoke.inl"

bool runRuntimePrismPresentationSmokeProbe(RuntimeWindowState& live, std::string& error) {
    if (!runRuntimePrismBeamRenderContractCheck(error)) return false;
    if (!runRuntimePrismShrapnelSmoke(live, error)) return false;
    const char* output = std::getenv("CONCORD_PRISM_PRESENTATION_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    if (!runRuntimePrismSupportWidthSmoke(live, root, error)) return false;
    for (const char* targetType : {"E2", "MTNK"}) for (const char* type : {"ATESLA", "SREF"}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false);
        s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessTerrainCells().clear(); s.world.accessSmudges().clear();
        for (int y = 40; y <= 88; ++y) for (int x = 40; x <= 88; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s);
        s.economy.owner = "Americans";
        s.ownerRemapColors["americans"] = RGB(255, 220, 40);
        const bool tower = std::string(type) == "ATESLA";
        auto* source = addRuntimeObjectInstance(s, tower ? RuntimeBuildKind::Building : RuntimeBuildKind::Unit, type, {64,64});
        if (!source) { error = "prism source missing"; return false; }
        const int sourceId = source->instanceId;
        auto* target = addRuntimeObjectInstance(s, std::string(targetType) == "E2"
            ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit, targetType, {70,61});
        if (!target) { error = "prism target missing"; return false; }
        const int targetId = target->instanceId;
        target->owner = "Russians";
        for (auto& object : s.world.accessObjects()) {
            object.buildAnimTicksRemaining = object.buildAnimTicksTotal = 0;
            object.mission = "Guard";
        }
        source = findMutableObjectById(s, sourceId);
        target = findMutableObjectById(s, targetId);
        source->facing = source->turretFacing = runtimeFacingFromDelta(target->x - source->x, target->y - source->y);
        const auto profile = runtimeWeaponProfileForObject(s, *source, target);
        if (!profile.weapon) { error = "stock prism weapon missing"; return false; }
        if (tower && std::string(targetType) == "MTNK"
            && !runRuntimePrismChargeCycleSmoke(s, sourceId, targetId, root, error)) return false;
        spawnRuntimeProjectile(s, *source, *target, *profile.weapon, profile, 1);
        if (tower) {
            for (const auto cell : {RuntimeCellPoint{60,64}, RuntimeCellPoint{64,69}}) {
                auto* supporter = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "ATESLA", cell);
                if (!supporter) { error = "prism support tower missing"; return false; }
                supporter->buildAnimTicksRemaining = supporter->buildAnimTicksTotal = 0;
                const auto* supportWeapon = s.rules.findWeaponType("PrismSupport");
                source = findMutableObjectById(s, sourceId);
                target = findMutableObjectById(s, targetId);
                if (!supportWeapon) { error = "prism support rules missing"; return false; }
                spawnRuntimePrismSupportBeam(s, *supporter, *source, *target, *supportWeapon,
                    s.rules.getSpecialMechanismRules().prismSupport, 5);
                if (s.projectiles.empty() || s.projectiles.back().renderIntent != "prismSupportBeam") {
                    error="prism native support discharge missing";return false;
                }
                const auto& link=s.projectiles.back();
                const auto authored=runtimeProjectileSourcePosition(s,*supporter,RuntimeWeaponSlot::Primary,link.attackerFacing);
                if (link.sourceX!=authored.x || link.sourceY!=authored.y || link.sourceZ!=authored.z) {
                    error="prism native attachment changed authoritative source";return false;
                }
                RuntimeMapRenderContext view;
                view.rules=&s.rules;view.art=&s.art;view.theater=&s.theater;
                view.assetRootPath=runtimeAssetRootPath(s.config);
                view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
                view.animationTick=s.tick;view.fixedTickHz=s.config.fixedTickHz;
                const RECT viewport{0,0,1024,768};
                // Raw stock [GAPRIS]: PrimaryFirePixelOffset=0,-4 plus
                // PrimaryFireFLH=0,0,378, PrimaryFireDualOffset=yes.
                // This independent arithmetic checks the actual emitted link.
                for (int zoom:{48,60,72}) {
                    RuntimeMapCamera camera;camera.tileWidth=zoom;camera.tileHeight=zoom/2;
                    const auto center=runtimeObjectWorldRenderPlacement(*supporter,s.world,viewport,view,camera,s.renderCache).center;
                    const POINT expected{center.x,center.y+static_cast<LONG>(std::lround(-4.0-378.0*std::sqrt(6.0)*15.0/256.0))};
                    const auto actual=runtimeProjectileActorScreenPoint(makeRuntimeProjectileRenderActor(link),viewport,camera);
                    if (std::abs(actual.x-expected.x)>1 || std::abs(actual.y-expected.y)>1) {
                        error="prism support missed native core at zoom="+std::to_string(zoom);return false;
                    }
                }
            }
        }
        const auto stats = drawRuntimeStateOffscreenAtCell(s, {65,64}, 800, 600, error,
            root.empty() ? "" : root + "/" + type + "-" + targetType + ".bmp", false, false);
        if (!stats || !stats->rendered || stats->projectileActorTravelDrawn != (tower ? 3 : 6)) {
            error = std::string(type) + " prism beams did not render: " + error; return false;
        }
        std::cout << "Prism presentation: " << type << " -> " << targetType
            << " native scene=ok straight=ok translucent=ok width=ok\n";
    }
    return true;
}
