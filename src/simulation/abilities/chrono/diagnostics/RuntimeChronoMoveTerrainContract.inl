// Include after RuntimePlannerCellPassabilityContract.inl and
// all actual command/parasite/simulation helpers. Uses the public shell-local
// RuntimeChronoMoveTerrainView and dedicated reference override supplied by root.
namespace RuntimeChronoMoveTerrainContractDetail {
using CellRecord = std::tuple<int,int,int,int,int,int,int,bool,int,int,
    std::string,std::string,bool,bool>;
using InfoRecord = std::tuple<int,RuntimeTerrainLandType,unsigned char,
    RuntimeTerrainKind,RuntimeTerrainLandType,bool,bool,bool,bool,bool,bool,int>;
struct TerrainState {
    std::vector<CellRecord> cells;
    std::vector<InfoRecord> info;
    std::vector<std::pair<int,std::ptrdiff_t>> lookup;
    bool operator==(const TerrainState&) const = default;
};
TerrainState terrainState(const RuntimeWindowState& state) {
    TerrainState out;
    const auto& cells=state.world.getTerrainCells();
    std::unordered_map<const TerrainCell*,std::ptrdiff_t> indices;
    indices.reserve(cells.size());
    for (std::size_t index=0;index<cells.size();++index) {
        const auto& c=cells[index];
        indices.emplace(&c,static_cast<std::ptrdiff_t>(index));
        out.cells.emplace_back(c.x,c.y,c.cellIndex512,c.tileIndex,c.subTileIndex,
            c.level,c.iceGrowth,c.hasOverlay,c.overlayIndex,c.overlayFrame,
            c.overlayType,c.overlayImage,c.overlayIsOre,c.overlayIsGem);
    }
    for (const auto& [key,c]:state.terrainInfoByCell)
        out.info.emplace_back(key,c.movementLandType,c.movementRampType,c.kind,
            c.authoredLandType,c.passable,c.water,c.shore,c.cliff,c.ramp,c.bridge,c.cost);
    for (const auto& [key,c]:state.terrainByCell) {
        const auto found=indices.find(c);
        out.lookup.emplace_back(key,c==nullptr ? -1 : found==indices.end() ? -2 : found->second);
    }
    std::sort(out.info.begin(),out.info.end());
    std::sort(out.lookup.begin(),out.lookup.end());
    return out;
}
struct FullState {
    RuntimePlannerPassabilityContractDetail::Authority authority;
    TerrainState terrain;
    std::uint64_t special=0, persistent=0, standalone=0;
    std::string specialSave, jumpjet;
    bool operator==(const FullState&) const = default;
};
bool capture(RuntimeWindowState& state, FullState& out, std::string& error) {
    RuntimeSnapshotActiveStateScope active(state);
    // Compare completed production transaction boundaries, including any
    // ordinary input-command occupancy refresh. The command's unchanged
    // terrain/lookup check is performed before this call by runCommand.
    (void)completeRuntimeWorldRefresh(state);
    if (state.tick<0 || state.config.fixedTickHz<=0 || state.navigationRefreshPending
        || state.powerRefreshPending || !state.pendingObjectExits.empty()
        || !state.pendingObjectExitIds.empty() || state.drainingObjectExits
        || state.humanHouseScopeActive || state.remoteHumanHouseScopeActive
        || state.reconcilingMagBeams || state.applyingGrindingActions) {
        error="completed chrono contract boundary remains unstable: tick="+std::to_string(state.tick)
            +" navigation="+std::to_string(state.navigationRefreshPending)
            +" power="+std::to_string(state.powerRefreshPending)
            +" exits="+std::to_string(state.pendingObjectExits.size());
        return false;
    }
    if (!RuntimePlannerPassabilityContractDetail::capture(state,out.authority,error)) return false;
    out.terrain=terrainState(state);
    out.special=state.specialMechanisms.stateHash();
    out.persistent=runtimePersistentTransientStateHash(state);
    out.standalone=runtimeStandaloneSimulationStateHash(state,error);
    if (!out.standalone) return false;
    std::ostringstream special;
    state.specialMechanisms.save(special);
    out.specialSave=special.str();
    out.jumpjet=serializeRuntimeActiveJumpjetFlights(state);
    return true;
}
auto commandState(const RuntimeWindowState& state) {
    const auto& c=state.lastCommand;
    return std::make_tuple(c.valid,c.verb,c.tick,c.selectedCount,c.acceptedCount,
        c.rejectedCount,c.targetX,c.targetY,c.targetObjectId,c.targetObjectType,
        c.inputAckImmediate,c.feedbackLatencyTicks,state.selectedObjectIds,
        state.interactionStatus,state.simulationStatus);
}
} // namespace RuntimeChronoMoveTerrainContractDetail

bool runRuntimeChronoMoveTerrainContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeChronoMoveTerrainContractDetail;
    using Observations=RuntimePlannerPassabilityContractDetail::Observations;
    const auto fail=[&](const std::string& why) {
        error="chrono move terrain view: "+why;
        return false;
    };
    std::size_t primitiveChecks=0,commandChecks=0,trajectoryChecks=0,queryChecks=0,airLegacyChecks=0;
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        auto& cells=state.world.accessTerrainCells();
        for (const RuntimeCellPoint point : {RuntimeCellPoint{0,1},RuntimeCellPoint{1000,0},
                RuntimeCellPoint{-1,2},RuntimeCellPoint{0,1}}) {
            TerrainCell cell; cell.x=point.x; cell.y=point.y;
            cell.cellIndex512=runtimeCellKey(point.x,point.y);
            cell.tileIndex=static_cast<int>(cells.size()); cell.level=cell.tileIndex;
            cell.hasOverlay=true; cell.overlayIndex=cell.tileIndex; cell.overlayFrame=3;
            cell.overlayType="ORE"; cell.overlayImage="GOLD01";
            cell.overlayIsOre=true; cell.iceGrowth=5;
            cells.push_back(std::move(cell));
        }
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        TerrainCell foreign; foreign.x=77; foreign.y=88; foreign.tileIndex=91;
        state.terrainByCell.emplace(123456,&foreign);
        state.terrainByCell.emplace(std::numeric_limits<int>::max(),nullptr);
        const auto original=state.terrainByCell;
        const auto originalTerrain=terrainState(state);
        const auto originalWorld=state.world.deterministicSimulationStateHash();
        {
            RuntimeChronoMoveTerrainView oldView(state,true),newView(state,false);
            if (oldView.get()!=original || newView.get()!=original
                || &oldView.get()==&state.terrainByCell || &newView.get()!=&state.terrainByCell
                || oldView.borrowed() || !newView.borrowed()
                || oldView.generated() || newView.generated()
                || !oldView.reference() || newView.reference())
                return fail("primitive populated copy/borrow identity or exact key/pointer contents");
            if (runtimeCellKey(1000,0)!=runtimeCellKey(0,1)
                || newView.get().at(runtimeCellKey(0,1))!=&cells.front()
                || newView.get().at(-1)!=&cells[2]
                || newView.get().at(123456)!=&foreign
                || newView.get().at(std::numeric_limits<int>::max())!=nullptr)
                return fail("primitive original emplace alias/negative/duplicate/null/foreign semantics");
            primitiveChecks+=2;
        }
        if (!(terrainState(state)==originalTerrain)
            || state.world.deterministicSimulationStateHash()!=originalWorld)
            return fail("primitive populated terrain/world mutation");
        const int previous=runtimeChronoMoveTerrainReferenceOverride;
        {
            RuntimeChronoMoveTerrainReferenceScope outer(true);
            RuntimeChronoMoveTerrainView oldView(state);
            if (!runtimeChronoMoveTerrainReference() || !oldView.reference() || oldView.borrowed())
                return fail("primitive reference override selects actual copy");
            {
                RuntimeChronoMoveTerrainReferenceScope inner(false);
                RuntimeChronoMoveTerrainView newView(state);
                if (runtimeChronoMoveTerrainReference() || newView.reference() || !newView.borrowed())
                    return fail("primitive nested false override selects actual borrow");
            }
            if (!runtimeChronoMoveTerrainReference()) return fail("primitive nested restoration");
            primitiveChecks+=3;
        }
        if (runtimeChronoMoveTerrainReferenceOverride!=previous)
            return fail("primitive outer override restoration");
        state.terrainByCell.clear();
        const auto generated=buildRuntimeTerrainLookup(state.world);
        {
            RuntimeChronoMoveTerrainView oldView(state,true),newView(state,false);
            if (oldView.get()!=generated || newView.get()!=generated
                || !oldView.generated() || !newView.generated()
                || oldView.borrowed() || newView.borrowed()
                || !state.terrainByCell.empty())
                return fail("primitive empty lookup uses original generated oracle, without committing it");
            primitiveChecks+=2;
        }
        // A view never survives a world/lookup edit. A fresh local view observes
        // append/rebuild; this is lifetime verification, not a persistent cache.
        TerrainCell appended; appended.x=19; appended.y=29;
        cells.push_back(appended);
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        {
            RuntimeChronoMoveTerrainView newView(state,false);
            if (!newView.borrowed() || newView.get().at(runtimeCellKey(19,29))!=&cells.back())
                return fail("primitive subsequent world rebuild uses current owned world");
            ++primitiveChecks;
        }
        auto empty=std::make_unique<RuntimeWindowState>();
        RuntimeChronoMoveTerrainView emptyOld(*empty,true),emptyNew(*empty,false);
        if (!emptyOld.generated() || !emptyNew.generated()
            || emptyOld.borrowed() || emptyNew.borrowed()
            || !emptyOld.get().empty() || !emptyNew.get().empty())
            return fail("primitive completely empty world fallback");
        ++primitiveChecks;
    }

    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->config.playerSlots.clear(); assets->config.gameModeRulesOverride.clear();
    assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    const auto* droneWeapon=assets->rules.findWeaponType("DroneJump");
    const auto* hostRule=assets->rules.findUnitType("Vehicle","HTNK");
    const auto* chronoRule=assets->rules.findUnitType("Infantry","CLEG");
    if (!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded
        || !droneWeapon || droneWeapon->damage!=50 || !hostRule || hostRule->strength!=400
        || !chronoRule || !chronoRule->teleporter || chronoRule->harvester || chronoRule->moveToShroud)
        return fail("stock resources or native DRON/HTNK/CLEG rule oracle unavailable");

    // The target optimization is the real per-actor move-command branch. Seven
    // selected actors exercise four CLEG plus native ground/jumpjet/air planners,
    // without multiplying this contract's observer log by a full 115k-cell BFS.
    // Dense/component full BFS equivalence remains in its independent contract.
    for (int mapSide : {96,240}) for (bool emptyLookup : {false,true}) {
        const std::string label="side="+std::to_string(mapSide)
            +" emptyLookup="+std::to_string(emptyLookup);
        std::vector<int> selected[2],chronos[2];
        int parasiteHosts[2]{-1,-1};
        std::unique_ptr<RuntimeWindowState> worlds[2];
        for (int side=0;side<2;++side) {
            worlds[side]=std::make_unique<RuntimeWindowState>(); auto& state=*worlds[side];
            state.config=assets->config; state.report=assets->report;
            state.rules=assets->rules; state.art=assets->art; state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
            state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            std::vector<int> army;
            if (!prepareRuntimeMassMovementWorld(state,48,army,error,false,true,false,
                    mapSide,mapSide,true,true)) return fail(label+" native fixture: "+error);
            state.config.defaultMapPath="ChronoMoveTerrainContract.map";
            state.tick=1;
            std::unordered_set<std::string> representativeTypes;
            for (int id:army) {
                const auto* object=findObjectById(state,id);
                if (!object) return fail(label+" missing native roster actor");
                if (object->type=="CLEG") {
                    chronos[side].push_back(id); selected[side].push_back(id);
                } else if ((object->type=="MTNK" || object->type=="JUMPJET" || object->type=="SCHP")
                    && representativeTypes.insert(object->type).second) selected[side].push_back(id);
            }
            std::sort(selected[side].begin(),selected[side].end());
            std::sort(chronos[side].begin(),chronos[side].end());
            if (selected[side].size()!=7 || chronos[side].size()!=4)
                return fail(label+" native roster expected four CLEG plus three native movement modes");

            // Preserve a genuinely nonempty parasite transaction/state. Stock
            // DRON infantry hits do not create a persistent host link; therefore
            // the legal stock host here is an unselected HTNK, not a forged CLEG.
            const int sourceId=nextRuntimeObjectInstanceId(state),hostId=sourceId+1;
            parasiteHosts[side]=hostId;
            const int offset=mapSide/2;
            const auto add=[&](int id,const char* type,const char* owner,int health,int x,int y) {
                WorldObjectInstance object;
                object.instanceId=id; object.mapId="ChronoTerrainParasite"+std::to_string(id);
                object.kind=WorldObjectKind::Unit; object.type=type; object.owner=owner;
                object.alive=object.poweredOperational=true; object.health=health;
                object.x=x; object.y=y; object.cell=runtimeCellKey(x,y);
                object.mission="Guard"; object.currentAmmo=-1;
                object.harvestAutoSearchSuspended=true; clearRuntimeRenderPosition(object);
                state.world.accessObjects().push_back(std::move(object));
            };
            add(sourceId,"DRON","Russians",100,offset+25,offset+63);
            add(hostId,"HTNK","Americans",400,offset+26,offset+63);
            auto terrainInfo=std::move(state.terrainInfoByCell);
            state.objectIndex=RuntimeObjectIndex{}; initializeRuntimeNavigation(state);
            state.terrainInfoByCell=std::move(terrainInfo);
            state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
            invalidateRuntimeTickOccupancyCache(state); (void)completeRuntimeWorldRefresh(state);
            ensureRuntimeSpecialMechanismsInitialized(state);
            const auto* source=findObjectById(state,sourceId);
            const auto* host=findObjectById(state,hostId);
            if (!source || !host) return fail(label+" native parasite identities");
            const auto weapon=runtimeWeaponProfileForObject(state,*source,host);
            if (!weapon.weapon || !launchRuntimeParasiteProjectile(state,*source,*host,*weapon.weapon,weapon)
                || state.projectiles.size()!=1) return fail(label+" native DRON launch");
            const auto impact=state.projectiles.front(); state.projectiles.clear();
            if (!applyRuntimeProjectileImpact(state,impact)
                || !state.specialMechanisms.parasiteLinks().contains(hostId))
                return fail(label+" native DRON impact did not create persistent link");
            const auto actions=state.specialMechanisms.tickParasites(state.world.getObjects(),state.rules,state.tick,{});
            if (!applyRuntimeParasiteActionsAtomically(state,actions,{},sourceId).accepted
                || findObjectById(state,hostId)->health!=350)
                return fail(label+" native first 50-point parasite bite");
            // Enter-limbo/attach/bite are real object transactions and request
            // navigation refresh. Complete that production boundary before the
            // full standalone hash; never clear its guard flags by hand.
            (void)completeRuntimeWorldRefresh(state);
            const auto* hiddenSource=findRuntimeObjectByIdIncludingInactive(state,sourceId);
            const auto* bittenHost=findObjectById(state,hostId);
            if (state.navigationRefreshPending || state.powerRefreshPending
                || !state.pendingObjectExits.empty() || !state.pendingObjectExitIds.empty()
                || state.drainingObjectExits || state.reconcilingMagBeams
                || state.applyingGrindingActions || state.humanHouseScopeActive
                || state.remoteHumanHouseScopeActive)
                return fail(label+" native parasite refresh did not finish stable boundary: navigation="
                    +std::to_string(state.navigationRefreshPending)+" power="
                    +std::to_string(state.powerRefreshPending)+" exits="
                    +std::to_string(state.pendingObjectExits.size()));
            if (!hiddenSource || hiddenSource->alive || hiddenSource->mission!="ParasiteLimbo"
                || !bittenHost || bittenHost->health!=350
                || !state.specialMechanisms.parasiteLinks().contains(hostId)
                || state.specialMechanisms.parasiteLinks().at(hostId).sourceObjectId!=sourceId)
                return fail(label+" refresh lost native limbo source/host link/first bite witness");
            if (emptyLookup) state.terrainByCell.clear();
        }
        if (selected[0]!=selected[1] || chronos[0]!=chronos[1] || parasiteHosts[0]!=parasiteHosts[1])
            return fail(label+" native independently-owned identity order differs");
        const auto compareFull=[&](const std::string& phase) {
            FullState before,after;
            if (!capture(*worlds[0],before,error) || !capture(*worlds[1],after,error))
                return fail(label+" "+phase+" full writer/hash capture: "+error);
            if (!(before==after)) return fail(label+" "+phase+" full state/save/terrain/special/parasite mismatch");
            return true;
        };
        if (!compareFull("setup")) return false;
        const auto runCommand=[&](const std::vector<int>& ids,RuntimeCellPoint goal,
            const std::vector<int>& expectedAcceptedIds,const std::string& phase) {
            const int expectedAccepted=static_cast<int>(expectedAcceptedIds.size());
            std::map<int,RuntimeCellPoint> destinations[2];
            Observations observed[2]; TerrainState terrainBefore[2];
            for (int side=0;side<2;++side) {
                auto& state=*worlds[side]; RuntimeSnapshotActiveStateScope active(state);
                RuntimeChronoMoveTerrainReferenceScope reference(side==0);
                RuntimePlannerPassabilityObserverScope observer(&Observations::append,&observed[side]);
                terrainBefore[side]=terrainState(state);
                applySelection(state,ids,false,false);
                issueRuntimeMoveCommand(state,goal,&destinations[side]);
                if (state.lastCommand.selectedCount!=static_cast<int>(ids.size())
                    || state.lastCommand.acceptedCount!=expectedAccepted
                    || state.lastCommand.rejectedCount!=static_cast<int>(ids.size())-expectedAccepted
                    || destinations[side].size()!=static_cast<std::size_t>(expectedAccepted))
                    return fail(label+" "+phase+" unexpected actual acceptance: accepted="
                        +std::to_string(state.lastCommand.acceptedCount)+" rejected="
                        +std::to_string(state.lastCommand.rejectedCount));
                std::vector<int> actualAcceptedIds;
                for (const auto& [id,destination]:destinations[side]) {
                    (void)destination;
                    actualAcceptedIds.push_back(id);
                }
                if (actualAcceptedIds!=expectedAcceptedIds) {
                    std::string actual,expected;
                    for (int id:actualAcceptedIds) actual+=std::to_string(id)+",";
                    for (int id:expectedAcceptedIds) expected+=std::to_string(id)+",";
                    return fail(label+" "+phase+" exact accepted identity mismatch: expected="
                        +expected+" actual="+actual);
                }
                if (!(terrainState(state)==terrainBefore[side])
                    || (emptyLookup && !state.terrainByCell.empty()))
                    return fail(label+" "+phase+" command changed terrain or committed generated fallback");
                if (!state.specialMechanisms.parasiteLinks().contains(parasiteHosts[side])
                    || findObjectById(state,parasiteHosts[side])->health!=350)
                    return fail(label+" "+phase+" unrelated native parasite relationship/bite changed");
            }
            ++commandChecks; queryChecks+=observed[0].values.size();
            if (destinations[0]!=destinations[1]
                || commandState(*worlds[0])!=commandState(*worlds[1])
                || observed[0].values!=observed[1].values)
                return fail(label+" "+phase+" actual destinations/command or exact query order/results differ");
            return compareFull(phase);
        };
        const int offset=mapSide/2;
        const RuntimeCellPoint hidden{offset+90,offset+3};
        for (int side=0;side<2;++side) {
            auto& state=*worlds[side];
            if (!state.visionEnabled || !state.vision.hasObserver(state.economy.owner)
                || state.vision.isCellExploredBy(state.economy.owner,hidden.x,hidden.y))
                return fail(label+" shroud rejection fixture is not truly uncharted");
        }
        if (!runCommand(chronos[0],hidden,{},"native shroud rejection")) return false;
        const auto start=runtimeCellPointForObject(*findObjectById(*worlds[0],chronos[0].front()));
        if (!runCommand({chronos[0].front()},start,{chronos[0].front()},"native current-cell no-op")) return false;
        std::map<int,RuntimeCellPoint> starts;
        for (int id:chronos[0]) starts.emplace(id,runtimeCellPointForObject(*findObjectById(*worlds[0],id)));
        const RuntimeCellPoint goal{offset+76,offset+32};
        std::vector<int> expectedMixedIds[2];
        for (int side=0;side<2;++side) {
            auto& state=*worlds[side]; RuntimeSnapshotActiveStateScope active(state);
            if (!state.vision.isCellExploredBy(state.economy.owner,goal.x,goal.y))
                return fail(label+" native valid target is not actually explored");
            if (!std::any_of(state.world.getTerrainCells().begin(),state.world.getTerrainCells().end(),
                    [&](const TerrainCell& cell){return cell.x==goal.x && cell.y==goal.y;})
                || state.terrainByCell.empty()!=emptyLookup
                || !runtimeAirCellIsClear(state,-1,goal))
                return fail(label+" native Air oracle requires real-world terrain, exact lookup state and free goal");
            const std::unordered_set<int> replacing(selected[side].begin(),selected[side].end());
            std::vector<int> legacyAirIds;
            for (int id:selected[side]) {
                const auto* object=findObjectById(state,id);
                if (!object) return fail(label+" native mixed oracle lost selected actor");
                if (object->type=="JUMPJET" || object->type=="SCHP") {
                    // The original Air target finder consults state.terrainByCell,
                    // rather than the command's generated ground/chrono lookup.
                    // An empty index rejects both stock Air movers in both
                    // branches. Map-authored Units/Infantry start on the ground;
                    // the command dispatch uses rules-derived mode, not airborne.
                    if (runtimeObjectMovementMode(state,*object)!=PathMovementMode::Air
                        || !runtimeObjectCanReceiveMoveCommand(state,*object)
                        || !runtimeObjectAllowsMoveToCell(state,*object,goal)
                        || object->kind!=(object->type=="JUMPJET" ? WorldObjectKind::Infantry : WorldObjectKind::Unit))
                        return fail(label+" native stock Air movement eligibility/type witness id="+std::to_string(id));
                    const auto target=findRuntimeAirMoveTarget(state,*object,goal,nullptr,&replacing);
                    ++airLegacyChecks;
                    if (emptyLookup ? target.has_value() : (!target || *target!=goal))
                        return fail(label+" original Air target finder did not preserve empty/nonempty lookup semantics id="
                            +std::to_string(id));
                    legacyAirIds.push_back(id);
                    if (!emptyLookup) expectedMixedIds[side].push_back(id);
                } else {
                    if (object->type!="CLEG" && object->type!="MTNK")
                        return fail(label+" unexpected native non-Air representative type="+object->type);
                    expectedMixedIds[side].push_back(id);
                }
            }
            if (legacyAirIds.size()!=2 || expectedMixedIds[side].size()!=(emptyLookup ? 5U : 7U)
                || !std::all_of(chronos[side].begin(),chronos[side].end(),[&](int id){
                    return std::binary_search(expectedMixedIds[side].begin(),expectedMixedIds[side].end(),id);}))
                return fail(label+" mixed exact identity oracle must retain four CLEG plus MTNK and only two legacy Air rejections");
        }
        if (expectedMixedIds[0]!=expectedMixedIds[1])
            return fail(label+" independently-owned mixed accepted identity oracles differ");
        if (!runCommand(selected[0],goal,expectedMixedIds[0],"native mixed successful chrono command")) return false;
        if (!queryChecks) return fail(label+" actual passability callbacks never observed");
        for (int side=0;side<2;++side)
            for (int id:chronos[side])
                if (!worlds[side]->specialMechanisms.isChronoInTransit(id))
                    return fail(label+" successful command did not queue every real CLEG warp");
        for (int n=0;n<2;++n) {
            std::uint64_t hashes[2]{};
            for (int side=0;side<2;++side) {
                auto& state=*worlds[side]; RuntimeSnapshotActiveStateScope active(state);
                RuntimeChronoMoveTerrainReferenceScope reference(side==0);
                ++state.tick; (void)advanceRuntimeSimulation(state);
                hashes[side]=runtimeStandaloneSimulationStateHash(state,error);
                if (!hashes[side]) return fail(label+" tick hash capture: "+error);
            }
            ++trajectoryChecks;
            if (hashes[0]!=hashes[1]) return fail(label+" actual arrival trajectory hash at tick="+std::to_string(n+2));
            if (!compareFull("actual arrival tick="+std::to_string(n+2))) return false;
        }
        for (int side=0;side<2;++side) for (int id:chronos[side]) {
            const auto* object=findObjectById(*worlds[side],id);
            if (!object || !object->alive || runtimeCellPointForObject(*object)==starts.at(id)
                || !worlds[side]->specialMechanisms.isChronoMovementLocked(id,worlds[side]->tick))
                return fail(label+" actual CLEG displacement and stock recovery witness missing");
        }
        if (!runCommand(chronos[0],{offset+22,offset+48},{},"native stock recovery rejection")) return false;
    }
    if (primitiveChecks!=9 || commandChecks!=16 || trajectoryChecks!=8 || queryChecks==0 || airLegacyChecks!=16)
        return fail("complete primitive/actual command/trajectory matrix did not execute");
    std::cout << "Chrono move terrain view contract: primitive=" << primitiveChecks
        << " actualCommands=" << commandChecks << " fullArrivalStates=" << trajectoryChecks
        << " orderedQueries=" << queryChecks
        << " actualLegacyAirTargets=" << airLegacyChecks
        << " exactAcceptedIDs=yes stockParasiteNonempty=4 maps=96,240 emptyFallback=verified pass\n";
    error.clear(); return true;
}
