bool runRuntimePrismShrapnelSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Prism shrapnel: " + why; return false; };
    int checked = 0;
    for (int hz : {15, 30, 60}) for (bool elite : {false, true}) {
        for (const std::string scene : {"building", "ground", "isolated", "one", "allied", "many", "foundation"}) {
            auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
            seedRuntimeLoadScratch(s, live, false);
            s.runtimeSuppressPlayerAudioFeedback = true; s.config.fixedTickHz = hz;
            s.config.simulationRandomSeed = 815; s.economy.owner = "Americans";
            s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
            s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
            for (int y=56; y<=80; ++y) for (int x=56; x<=80; ++x) {
                TerrainCell c; c.x=x; c.y=y; c.cellIndex512=runtimeCellKey(x,y); c.tileIndex=0;
                s.world.accessTerrainCells().push_back(c);
            }
            initializeRuntimeNavigation(s);
            auto add = [&](RuntimeBuildKind kind, const char* type, RuntimeCellPoint cell, const char* owner) {
                auto* object = addRuntimeObjectInstance(s, kind, type, cell);
                if (!object) return -1;
                object->owner=owner; object->mission="Guard";
                object->buildAnimTicksRemaining=object->buildAnimTicksTotal=0;
                return object->instanceId;
            };
            const int sourceId=add(RuntimeBuildKind::Unit,"SREF",{64,64},"Americans");
            const int targetId=scene=="ground" ? -1 : add(scene=="building" ? RuntimeBuildKind::Building
                : RuntimeBuildKind::Unit,scene=="building" ? "GAPOWR" : "MTNK",{70,64},"Russians");
            int neighborId=-1;
            if (scene=="one" || scene=="allied" || scene=="building" || scene=="ground")
                neighborId=add(RuntimeBuildKind::Unit,"MTNK",{70,63},scene=="allied" ? "Americans" : "Russians");
            if (scene=="many") for (RuntimeCellPoint c : {RuntimeCellPoint{71,63},{70,63},{69,63},{69,64},{71,64}})
                add(RuntimeBuildKind::Unit,"MTNK",c,"Russians");
            if (scene=="foundation") neighborId=add(RuntimeBuildKind::Building,"GAPOWR",{71,64},"Russians");
            auto* source=findMutableObjectById(s,sourceId);
            WorldObjectInstance ground; ground.instanceId=-1; ground.type="GROUND"; ground.owner="Neutral";
            ground.kind=WorldObjectKind::Unit; ground.x=70; ground.y=64; ground.cell=runtimeCellKey(70,64); ground.alive=true;
            const auto* target=targetId<0 ? &ground : findObjectById(s,targetId);
            if (!source || !target) return fail("fixture missing");
            source->veterancy=elite ? 2 : 0;
            const auto* weapon=s.rules.findWeaponType(elite ? "SuperComet" : "Comet");
            if (!weapon) return fail("stock weapon missing");
            RuntimeWeaponProfile profile;
            finalizeRuntimeWeaponProfile(profile,s,RuntimeWeaponSlot::Indexed,*weapon,"shrapnel-probe");
            spawnRuntimeProjectile(s,*source,*target,*weapon,profile,1);
            const int parentId=s.projectiles.front().actorId;
            std::vector<RuntimeProjectileEffect> first;
            int second=0;
            for (const auto& p:s.projectiles) {
                if (!p.houseColorLaser || p.owner!="Americans") return fail("child lost owner color");
                if (p.parentActorId==parentId) first.push_back(p);
                if (p.generation==2) ++second;
            }
            const bool blocked=scene=="building" || scene=="ground";
            if (first.size()!=(blocked ? 0U : 5U)) return fail(scene+" first-level count="+std::to_string(first.size()));
            int aimed=0;
            for (const auto& p:first) {
                if (p.targetObjectId>=0) ++aimed;
                else if (std::abs(p.toCell.x-70)>2 || std::abs(p.toCell.y-64)>2)
                    return fail("ground fragment outside native [-2,2] offsets");
                if (p.fromCell.x!=70 || p.fromCell.y!=64 || p.sourceAnchorObjectId!=targetId)
                    return fail("fragment did not stay anchored to the primary endpoint");
                if (makeRuntimeProjectileRenderActor(p, &s.rules, &s.art).sourceAnchorObjectId!=targetId)
                    return fail("body anchor was lost when preparing the rendered fragment");
            }
            if ((scene=="isolated" || scene=="allied") && aimed!=0) return fail(scene+" acquired invalid neighbor");
            if (scene=="one" && (aimed!=1 || first.front().targetObjectId!=neighborId)) return fail("one neighbor did not leave four ground rays");
            if (scene=="many" && (aimed!=5 || first[0].toCell.x!=71 || first[0].toCell.y!=63
                || first[1].toCell.x!=70 || first[2].toCell.x!=69)) return fail("native first-ring target order changed");
            if (scene=="foundation" && aimed<2) return fail("building foundation was incorrectly deduplicated");
            if (elite && scene=="one" && second<3) return fail("elite second-level fragments missing");
            if (!elite && second!=0) return fail("normal Comet unexpectedly cascaded");
            if (scene=="one") {
                auto precise=s.projectiles.front(); precise.targetX+=0.25; precise.targetY+=0.375;
                const auto begin=s.projectiles.size();
                spawnRuntimeProjectileShrapnel(s,precise,precise.toCell,precise.toLevel);
                for (auto i=begin;i<s.projectiles.size();++i) {
                    const auto& p=s.projectiles[i];
                    if (p.parentActorId==precise.actorId && (std::abs(p.sourceX-precise.targetX)>0.001
                        || std::abs(p.sourceY-precise.targetY)>0.001)) return fail("fractional hit position was rounded away");
                }
                auto* moved=findMutableObjectById(s,targetId); ++moved->x; moved->cell=runtimeCellKey(moved->x,moved->y);
                auto main=s.projectiles.front(); auto child=first.front();
                syncRuntimeProjectileTargetEndpoint(s,main); syncRuntimeProjectileTargetEndpoint(s,child);
                if (child.fromCell.x!=main.toCell.x || child.fromCell.y!=main.toCell.y
                    || std::abs(child.sourceX-main.targetX)>0.001 || std::abs(child.sourceY-main.targetY)>0.001)
                    return fail("moving hit unit separated the two beam endpoints");
            }
            ++checked;
        }
    }
    // Native CellSpread counts read from gamemd 0x7ED3D0. This detects circular
    // Euclidean distance and square-neighborhood substitutions at boundaries.
    const int nativeCounts[]={1,9,21,37,61,89};
    for (int r=0;r<=5;++r) {
        int count=0;
        for (int y=-r;y<=r;++y) for (int x=-r;x<=r;++x) if(runtimeShrapnelCellRing(x,y)<=r)++count;
        if(count!=nativeCounts[r])return fail("retail CellSpread boundary mismatch");
    }
    std::cout << "Prism shrapnel: " << checked << " normal/elite/Hz scenes; building/ground gates, random remainder, allies, cell order, foundation, elite cascade=ok\n";
    return true;
}
