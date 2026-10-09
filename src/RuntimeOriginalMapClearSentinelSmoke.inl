// Original-map sentinel diagnosis. The loaded map and all authored objects remain intact.
// Before mode is a bug reproduction receipt, never an issue acceptance result.
bool runRuntimeOriginalMapClearSentinelSmoke(RuntimeWindowState& s, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& detail) {
        error = "Original map clear sentinel: " + detail; return false;
    };
    const char* output = std::getenv("CONCORD_ORIGINAL_MAP_MOVE_OUTPUT");
    const char* expectation = std::getenv("CONCORD_ORIGINAL_MAP_MOVE_EXPECT");
    if (!output || !*output || !expectation ||
        (std::string(expectation) != "before" && std::string(expectation) != "after"))
        return fail("private output and explicit before/after expectation are required");
    const bool beforeMode = std::string(expectation) == "before";
    RuntimeCellPoint source{91,31}, target{90,31};
    const auto readPoint = [&](const char* name, RuntimeCellPoint& point) {
        const char* value = std::getenv(name);
        if (!value || !*value) return true;
        std::istringstream input(value); char comma=0;
        if (!(input >> point.x >> comma >> point.y) || comma!=',' || point.x<0 || point.y<0
            || point.x>=512 || point.y>=512) return false;
        input >> std::ws; return input.eof();
    };
    if (!readPoint("CONCORD_ORIGINAL_MAP_MOVE_SOURCE",source)
        || !readPoint("CONCORD_ORIGINAL_MAP_MOVE_TARGET",target)) return fail("invalid cell parameter");
    if (std::abs(source.x-target.x)+std::abs(source.y-target.y)!=1)
        return fail("this minimal proof requires cardinal adjacent original cells");
    const char* typeValue = std::getenv("CONCORD_ORIGINAL_MAP_MOVE_TYPE");
    const std::string typeId = typeValue && *typeValue ? typeValue : "MTNK";
    const UnitType* type = findRuntimeUnitTypeById(s.rules,typeId);
    if (!s.report.success || !s.theaterLoaded || !s.rulesLoaded || !s.artLoaded
        || !type || type->speed<=0 || type->teleporter || runtimeUnitUsesAirMovement(*type)
        || runtimeUnitUsesJumpjetMovement(*type)) return fail("original ground unit/catalogue unavailable");
    const auto sourceIt=s.terrainByCell.find(runtimeCellKey(source.x,source.y));
    const auto targetIt=s.terrainByCell.find(runtimeCellKey(target.x,target.y));
    if (sourceIt==s.terrainByCell.end() || !sourceIt->second || targetIt==s.terrainByCell.end()
        || !targetIt->second) return fail("requested cells are absent from the actual map");
    const TerrainCell originalSource=*sourceIt->second, originalTarget=*targetIt->second;
    if (originalSource.tileIndex!=0 || originalSource.subTileIndex!=0
        || originalTarget.tileIndex!=65535 || originalTarget.subTileIndex!=0
        || originalSource.level!=originalTarget.level || originalSource.hasOverlay || originalTarget.hasOverlay)
        return fail("requested map does not have the original flat Clear0/sentinel tuple");
    const auto projection=buildRuntimeRadarProjection(s);
    if (!runtimeRadarProjectionContains(projection,runtimeRadarDxyPoint(source.x,source.y,projection.fullMapWidth))
        || !runtimeRadarProjectionContains(projection,runtimeRadarDxyPoint(target.x,target.y,projection.fullMapWidth)))
        return fail("requested cells are outside the original LocalSize projection");
    const auto authoredOccupancy=buildRuntimeOccupancyLookup(s);
    if (authoredOccupancy.contains(runtimeCellKey(source.x,source.y))
        || authoredOccupancy.contains(runtimeCellKey(target.x,target.y))) return fail("authored object blocks the proof cells");
    const std::size_t originalObjectCount=s.world.getObjects().size();
    const std::size_t originalTreeCount=s.world.getTerrainObjects().size();
    const std::size_t originalFactsCount=s.mapFacts.cells.size();
    const auto quote=[](const std::string& value) {
        std::string result="\""; for (char c:value) { if(c=='\"') result+='\"'; result+=c; }
        return result+'\"';
    };
    const auto terrainRow=[&](const TerrainCell& cell) {
        std::ostringstream out;
        out << cell.x << ',' << cell.y << ',' << cell.tileIndex << ',' << cell.subTileIndex << ','
            << int(cell.level) << ',' << cell.cellIndex512 << ',' << cell.hasOverlay << ',' << cell.overlayIndex
            << ',' << cell.overlayFrame << ',' << quote(cell.overlayType) << ',' << quote(cell.overlayImage)
            << ',' << cell.iceGrowth << ',' << cell.overlayIsOre << ',' << cell.overlayIsGem;
        return out.str();
    };
    const auto terrainReceipt=[&]() {
        std::ostringstream out;
        for (const auto& cell:s.world.getTerrainCells()) out << terrainRow(cell) << '\n';
        return out.str();
    };
    const auto originalTerrainCells=s.world.getTerrainCells();
    const std::string originalTerrain=terrainReceipt();
    const char* terrainHeader="x,y,tile_index,subtile,level,cell_index512,has_overlay,overlay_index,overlay_frame,overlay_type,overlay_image,ice_growth,is_ore,is_gem\n";
    std::unordered_set<int> originalBareResourceCells;
    const auto originalTerrainObjectCells=buildRuntimeTerrainObjectCellLookup(s);
    for (const auto& cell:originalTerrainCells) if (!cell.hasOverlay && runtimeCellCanSpawnNewOre(
            s,{cell.x,cell.y},authoredOccupancy,originalTerrainObjectCells))
        originalBareResourceCells.insert(runtimeCellKey(cell.x,cell.y));
    struct OriginalOreSource {
        WorldTerrainObject tree; RuntimeOreOverlayIdentity identity; int radius=0;
    };
    std::vector<OriginalOreSource> originalOreSources;
    const int firstSimulationTick=s.tick+1;
    try {
        const std::filesystem::path directory(output);
        std::filesystem::create_directories(directory);
        std::ofstream terrainFile(directory/"terrain.csv"), terrainFinal(directory/"terrain-final.csv"),
            terrainDiff(directory/"terrain-diff.csv"), cells(directory/"cells.csv"),
            motion(directory/"motion.csv"), result(directory/"cases.csv");
        if (!terrainFile || !terrainFinal || !terrainDiff || !cells || !motion || !result)
            return fail("cannot open private evidence files");
        terrainFile << terrainHeader << originalTerrain;
        // Fixture isolation disables scripts and AI; the complete native simulation,
        // including path requests and Guard scans, is retained. No terrain, map facts,
        // obstacle, unit rules, path, requested destination or actor body is rewritten.
        s.config.computerAiEnabled=false; s.config.multiplayerActive=false;
        s.config.crates=false; s.config.shortGame=false; s.config.superweapons=false;
        s.visionEnabled=false; s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true; s.runtimePreferencePersistenceEnabled=false;
        s.triggers.loaded=false; s.triggers.bootstrapComplete=true;
        s.economy.skirmishStartApplied=true; s.campaignMode=true;
        int suspendedOriginalHarvesters=0;
        for (const auto& object:s.world.getObjects()) if (object.alive && runtimeObjectIsHarvester(s,object)) {
            auto* harvester=findMutableObjectById(s,object.instanceId);
            if (harvester) { harvester->harvestAutoSearchSuspended=true; ++suspendedOriginalHarvesters; }
        }
        if (s.rules.getTiberiumSpreads()) for (const auto& tree:s.world.getTerrainObjects())
            if (runtimeTerrainObjectSpawnsOre(s,tree) && runtimeTerrainObjectOreSpawnAllowedByMode(s,tree))
                originalOreSources.push_back({tree,runtimeOreIdentityForTerrainObject(s,tree),
                    runtimeTerrainObjectOreGrowthRadius(s,tree)});
        if (s.economy.owner.empty()) s.economy.owner="Americans";
        RuntimeHumanHouseAuthority authority; authority.owner=s.economy.owner;
        authority.canonicalOwner=runtimeCanonicalHumanHouseOwner(authority.owner); authority.localFacade=true;
        if (std::none_of(s.humanHouseAuthorities.begin(),s.humanHouseAuthorities.end(),[&](const auto& existing) {
                return existing.canonicalOwner==authority.canonicalOwner && existing.localFacade;
            })) s.humanHouseAuthorities.push_back(std::move(authority));
        auto* created=addRuntimeObjectInstance(s,runtimeBuildKindForUnitTypeId(s.rules,typeId),typeId,source,false);
        if (!created) return fail("cannot create the original-rule mover in the real source cell");
        const int actorId=created->instanceId;
        created->facing=runtimeFacingFromDelta(target.x-source.x,target.y-source.y);
        created->harvestAutoSearchSuspended=true;
        if (runtimeCellPointForObject(*created)!=source || !runtimeObjectCanReceiveMoveCommand(s,*created))
            return fail("mover was relocated or cannot receive the actual Move");
        const auto occupied=buildRuntimeOccupancyLookup(s,actorId);
        const auto context=runtimeTerrainMetadataContext(s);
        TerrainCell canonical=originalTarget; canonical.tileIndex=0; canonical.subTileIndex=0;
        const auto canonicalLand=resolveRuntimeTerrainSubTileLandType(canonical,context,s.renderCache);
        const int canonicalRamp=resolveRuntimeTerrainSubTileRampType(canonical,context,s.renderCache);
        if (canonicalLand!=RuntimeTerrainLandType::Clear || canonicalRamp!=0)
            return fail("the original theater Clear0 TMP is not genuinely flat Clear");
        cells << "role,x,y,tile_index,subtile,level,has_overlay,nav_kind,nav_passable,water,shore,cliff,ramp,bridge,"
            "movement_land,movement_ramp,authored_land,direct_land,direct_ramp,clear0_land,clear0_ramp,map_facts_has_terrain,"
            "map_facts_cliff,map_facts_ramp,map_facts_hint,whole_occupant,foundation_blocked,cell_passable,map_gate\n";
        const auto writeCell=[&](const char* role,const TerrainCell& cell) {
            const RuntimeCellPoint point{cell.x,cell.y}; const auto info=runtimeCachedTerrainInfoForCell(s,cell);
            const auto* facts=runtimeMapQueryCell(s,point);
            const auto occupant=occupied.find(runtimeCellKey(cell.x,cell.y));
            cells << role << ',' << cell.x << ',' << cell.y << ',' << cell.tileIndex << ',' << cell.subTileIndex
                << ',' << int(cell.level) << ',' << cell.hasOverlay << ',' << int(info.kind) << ',' << info.passable
                << ',' << info.water << ',' << info.shore << ',' << info.cliff << ',' << info.ramp << ',' << info.bridge
                << ',' << int(info.movementLandType) << ',' << int(info.movementRampType)
                << ',' << int(static_cast<RuntimeTerrainLandType>(info.authoredLandType))
                << ',' << int(resolveRuntimeTerrainSubTileLandType(cell,context,s.renderCache))
                << ',' << int(resolveRuntimeTerrainSubTileRampType(cell,context,s.renderCache))
                << ',' << int(canonicalLand) << ',' << canonicalRamp << ',' << (facts&&facts->hasTerrain)
                << ',' << (facts&&facts->isCliff) << ',' << (facts&&facts->isRamp) << ','
                << quote(facts?facts->passabilityHint:"") << ',' << (occupant==occupied.end()?-1:occupant->second)
                << ',' << runtimeFoundationBlocksMovement(s,*created,point,-1,false,RuntimeGatePassageMode::Commit)
                << ',' << runtimeCellIsPassable(s,s.terrainByCell,occupied,point,*created)
                << ',' << runtimeMapQueryPassabilityAllows(s,point,*created) << '\n';
        };
        writeCell("source",originalSource); writeCell("requested",originalTarget);
        const auto sourceInfo=runtimeCachedTerrainInfoForCell(s,originalSource);
        const auto targetInfo=runtimeCachedTerrainInfoForCell(s,originalTarget);
        const bool sourcePassable=runtimeCellIsPassable(s,s.terrainByCell,occupied,source,*created);
        const bool targetPassable=runtimeCellIsPassable(s,s.terrainByCell,occupied,target,*created);
        const bool transition=runtimeCellTransitionIsPassable(s,s.terrainByCell,occupied,source,target,*created);
        if (!sourcePassable || !sourceInfo.passable || sourceInfo.water || sourceInfo.cliff
            || runtimeFoundationBlocksMovement(s,*created,target,-1,false,RuntimeGatePassageMode::Commit))
            return fail("proof cell has a real source, water, cliff or foundation obstruction");
        motion << "event,tick,actor,type,logical_x,logical_y,slot,body_x,body_y,body_z,active,behavior,state,"
            "goal_x,goal_y,path_size,remaining,total,cooldown,health\n" << std::setprecision(17);
        const auto writeMotion=[&](const char* event) {
            const auto* object=findObjectById(s,actorId); if(!object||!object->alive) return false;
            const auto offset=worldObjectSubCellOffset(*object); const auto found=s.behaviors.find(actorId);
            motion << event << ',' << s.tick << ',' << actorId << ',' << quote(object->type) << ',' << object->x
                << ',' << object->y << ',' << object->subCell << ',' << runtimeObjectRenderX(*object)+offset.x
                << ',' << runtimeObjectRenderY(*object)+offset.y << ','
                << (object->hasRenderPosition?object->renderLevel:double(object->level)) << ',' << object->renderMoveActive
                << ',' << (found!=s.behaviors.end()) << ',' << quote(found==s.behaviors.end()?"none":toString(found->second.state))
                << ',' << (found==s.behaviors.end()?-1:found->second.moveTarget.x)
                << ',' << (found==s.behaviors.end()?-1:found->second.moveTarget.y)
                << ',' << (found==s.behaviors.end()?0:found->second.path.size())
                << ',' << (found==s.behaviors.end()?0:found->second.renderMoveTicksRemaining)
                << ',' << (found==s.behaviors.end()?0:found->second.renderMoveTicksTotal)
                << ',' << (found==s.behaviors.end()?0:found->second.moveCooldownTicks) << ',' << object->health << '\n';
            return std::isfinite(runtimeObjectRenderX(*object))&&std::isfinite(runtimeObjectRenderY(*object))
                &&std::isfinite(object->hasRenderPosition?object->renderLevel:double(object->level));
        };
        if (!writeMotion("spawn")) return fail("invalid initial body");
        applySelection(s,{actorId},false,false);
        std::map<int,RuntimeCellPoint> acceptedTargets;
        issueRuntimeMoveCommand(s,target,&acceptedTargets);
        const int accepted=s.lastCommand.acceptedCount, rejected=s.lastCommand.rejectedCount;
        const auto chosen=acceptedTargets.find(actorId);
        const RuntimeCellPoint assigned=chosen==acceptedTargets.end()?RuntimeCellPoint{-1,-1}:chosen->second;
        if (!writeMotion("command")) return fail("mover lost on command");
        bool arrived=false; int elapsed=0;
        for (;elapsed<std::max(1,s.config.fixedTickHz)*10;++elapsed) {
            ++s.tick; advanceRuntimeSimulation(s);
            if (!writeMotion("tick")) return fail("mover was lost or has nonfinite coordinates");
            const auto* object=findObjectById(s,actorId); const auto found=s.behaviors.find(actorId);
            const bool settled=(found==s.behaviors.end() || found->second.state==RuntimeBehaviorState::Complete)
                && !object->renderMoveActive && (found==s.behaviors.end() || found->second.moveCooldownTicks<=0);
            if (settled && runtimeCellPointForObject(*object)==target && object->level==originalTarget.level
                && std::abs((object->hasRenderPosition?object->renderLevel:double(object->level))-originalTarget.level)<=1e-9
                && std::hypot(runtimeObjectRenderX(*object)-target.x,runtimeObjectRenderY(*object)-target.y)<=1e-9) {
                arrived=true; ++elapsed; break;
            }
            if (settled) { ++elapsed; break; }
        }
        const std::string finalTerrain=terrainReceipt();
        const bool terrainUnchanged=originalTerrain==finalTerrain;
        terrainFinal << terrainHeader << finalTerrain;
        terrainDiff << "index,classification,allowed,witness_tree_type,witness_tree_x,witness_tree_y,witness_tick,before_receipt,after_receipt\n";
        const auto& finalTerrainCells=s.world.getTerrainCells();
        bool terrainGeometryUnchanged=originalTerrainCells.size()==finalTerrainCells.size();
        bool terrainProtected=terrainGeometryUnchanged;
        int changedTerrainCells=0, resourceFrameChanges=0, resourceImageNormalizations=0,
            naturalResourceChanges=0, rejectedTerrainChanges=0;
        for (std::size_t i=0;i<std::min(originalTerrainCells.size(),finalTerrainCells.size());++i) {
            const auto& before=originalTerrainCells[i]; const auto& after=finalTerrainCells[i];
            if (terrainRow(before)==terrainRow(after)) continue;
            ++changedTerrainCells;
            const bool geometrySame=before.x==after.x && before.y==after.y
                && before.tileIndex==after.tileIndex && before.subTileIndex==after.subTileIndex
                && before.level==after.level && before.cellIndex512==after.cellIndex512 && before.iceGrowth==after.iceGrowth;
            terrainGeometryUnchanged=terrainGeometryUnchanged && geometrySame;
            bool allowed=false; std::string classification="rejected";
            std::string witnessType; int witnessX=-1,witnessY=-1,witnessTick=-1;
            const bool beforeResource=before.hasOverlay && (before.overlayIsOre!=before.overlayIsGem);
            const bool afterResource=after.hasOverlay && (after.overlayIsOre!=after.overlayIsGem);
            if (geometrySame && beforeResource && afterResource) {
                const std::string family=runtimeResourceFamilyForTerrain(before);
                TerrainCell normalized=before;
                setRuntimeResourceTerrainVariant(normalized,family,runtimeResourceVariantForTerrain(before));
                const bool identitySame=before.overlayIndex==after.overlayIndex && before.overlayType==after.overlayType
                    && before.overlayIsOre==after.overlayIsOre && before.overlayIsGem==after.overlayIsGem
                    && after.overlayIndex==normalized.overlayIndex && after.overlayType==normalized.overlayType;
                const bool imageSame=before.overlayImage==after.overlayImage;
                const bool imageNormalized=before.overlayImage.empty() && after.overlayImage==normalized.overlayImage;
                allowed=identitySame && (imageSame || imageNormalized) && after.overlayFrame>=0
                    && after.overlayFrame<runtimeResourceMaxBailsForFamily(family);
                if (allowed) {
                    classification="same-resource-frame-or-image-normalization";
                    resourceFrameChanges+=before.overlayFrame!=after.overlayFrame;
                    resourceImageNormalizations+=!imageSame && imageNormalized;
                }
            } else if (geometrySame && !before.hasOverlay && !before.overlayIsOre && !before.overlayIsGem
                    && afterResource && originalBareResourceCells.contains(runtimeCellKey(before.x,before.y))) {
                // New resource must be a canonical native identity, on an originally legal bare cell,
                // within the original spawning tree's actual radius and a real due tick in this run.
                for (const auto& source:originalOreSources) {
                    const int dx=after.x-source.tree.x,dy=after.y-source.tree.y;
                    const long long distanceSquared=1LL*dx*dx+1LL*dy*dy;
                    if (source.radius<0 || distanceSquared>1LL*source.radius*source.radius
                        || (source.radius>0 && distanceSquared==0)) continue;
                    const int variant=source.identity.fixedVariant ? source.identity.variant
                        : runtimeGeneratedResourceVariant(after,source.identity.family);
                    TerrainCell expected=after; setRuntimeResourceTerrainVariant(expected,source.identity.family,variant);
                    if (after.overlayIndex!=expected.overlayIndex || after.overlayType!=expected.overlayType
                        || after.overlayImage!=expected.overlayImage || after.overlayIsOre!=expected.overlayIsOre
                        || after.overlayIsGem!=expected.overlayIsGem || after.overlayFrame<0
                        || after.overlayFrame>=runtimeResourceMaxBailsForFamily(source.identity.family)) continue;
                    for (int tick=firstSimulationTick;tick<=s.tick;++tick)
                        if (runtimeTerrainOreSpawnEventDue(s,source.tree,tick)) { witnessTick=tick; break; }
                    if (witnessTick<0) continue;
                    witnessType=source.tree.type; witnessX=source.tree.x; witnessY=source.tree.y;
                    allowed=true; classification="natural-resource-spawn"; ++naturalResourceChanges; break;
                }
            }
            if (!allowed) { terrainProtected=false; ++rejectedTerrainChanges; }
            terrainDiff << i << ',' << quote(classification) << ',' << allowed << ',' << quote(witnessType)
                << ',' << witnessX << ',' << witnessY << ',' << witnessTick << ','
                << quote(terrainRow(before)) << ',' << quote(terrainRow(after)) << '\n';
        }
        terrainProtected=terrainProtected && terrainGeometryUnchanged;
        const bool factsCountUnchanged=originalFactsCount==s.mapFacts.cells.size();
        const bool reproduced=sourcePassable && !targetInfo.passable && !targetPassable && !transition && !arrived;
        const bool corrected=sourcePassable && targetInfo.passable && targetPassable && transition
            && accepted==1 && assigned==target && arrived;
        const auto* final=findObjectById(s,actorId);
        result << "map,theater,mode,type,actor,hz,source_x,source_y,requested_x,requested_y,accepted,rejected,"
            "assigned_x,assigned_y,ticks,arrived,logical_x,logical_y,terrain_cells,original_objects,original_terrain_objects,"
            "terrain_unchanged,map_facts_count_unchanged,source_passable,target_passable,transition,bug_reproduced,corrected,"
            "terrain_geometry_unchanged,terrain_protected,changed_terrain_cells,resource_frame_changes,"
            "resource_image_normalizations,natural_resource_changes,rejected_terrain_changes,suspended_original_harvesters\n"
            << quote(s.report.map.path) << ',' << quote(s.report.map.theater) << ',' << quote(expectation) << ',' << quote(typeId)
            << ',' << actorId << ',' << s.config.fixedTickHz << ',' << source.x << ',' << source.y << ',' << target.x << ',' << target.y
            << ',' << accepted << ',' << rejected << ',' << assigned.x << ',' << assigned.y << ',' << elapsed << ',' << arrived
            << ',' << final->x << ',' << final->y << ',' << s.world.getTerrainCells().size() << ',' << originalObjectCount
            << ',' << originalTreeCount << ',' << terrainUnchanged << ',' << factsCountUnchanged << ',' << sourcePassable
            << ',' << targetPassable << ',' << transition << ',' << reproduced << ',' << corrected
            << ',' << terrainGeometryUnchanged << ',' << terrainProtected << ',' << changedTerrainCells
            << ',' << resourceFrameChanges << ',' << resourceImageNormalizations << ',' << naturalResourceChanges
            << ',' << rejectedTerrainChanges << ',' << suspendedOriginalHarvesters << '\n';
        terrainFile.flush(); terrainFinal.flush(); terrainDiff.flush(); cells.flush(); motion.flush(); result.flush();
        if (!terrainFile || !terrainFinal || !terrainDiff || !cells || !motion || !result) return fail("evidence write failed");
        if (!terrainProtected || !factsCountUnchanged) return fail("original map geometry/static overlays/facts changed during the probe");
        if (beforeMode ? !reproduced : !corrected) return fail(beforeMode?
            "the loaded original map did not reproduce the sentinel barrier":
            "the original requested cell was not reached with a settled body and unchanged target");
        std::cout << "Original map clear sentinel: " << (beforeMode?"BUG_REPRODUCED (not acceptance)":"AFTER_EXACT_ARRIVAL")
            << " type=" << typeId << " accepted=" << accepted << " assigned=" << assigned.x << ',' << assigned.y
            << " requested=" << target.x << ',' << target.y << " arrived=" << arrived << " ticks=" << elapsed
            << " originalTerrain=" << s.world.getTerrainCells().size() << " exact=" << terrainUnchanged
            << " protected=" << terrainProtected << " resourceFrames=" << resourceFrameChanges
            << " imageNormalizations=" << resourceImageNormalizations << " naturalResources=" << naturalResourceChanges << '\n';
        return true;
    } catch (const std::exception& exception) { return fail(exception.what()); }
}
