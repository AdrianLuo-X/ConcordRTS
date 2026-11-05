#include "runtime/session/detail/RuntimeProductionOperations.h"

namespace concord::runtime::session_detail {

bool pathExists(const std::filesystem::path& path) {
    std::error_code errorCode;
    return std::filesystem::exists(path, errorCode);
}

std::string pathString(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, errorCode);
    return (errorCode ? path.lexically_normal() : canonical).generic_string();
}

std::string runtimeTileRootPath(
    const RuntimeConfig& config,
    const RuntimeMapSummary& map) {
    if (config.localDataRootPath.empty() || map.path.empty()) {
        return "";
    }

    const std::filesystem::path mapStem =
        std::filesystem::path(map.path).stem();
    if (mapStem.empty()) {
        return "";
    }

    return pathString(
        std::filesystem::path(config.localDataRootPath)
            / "resources"
            / "mix_exports"
            / "extracted_tiles"
            / mapStem
    );
}

std::string runtimePalettePath(
    const RuntimeConfig& config,
    const RuntimeMapSummary& map) {
    if (config.localDataRootPath.empty() || map.theater.empty()) {
        return "";
    }

    const std::string theater = toLowerAsciiLocal(map.theater);
    std::string paletteFileName;
    if (theater == "desert") {
        paletteFileName = "isodes.pal";
    } else if (theater == "temperate") {
        paletteFileName = "isotem.pal";
    } else if (theater == "snow") {
        paletteFileName = "isosno.pal";
    } else if (theater == "urban") {
        paletteFileName = "isourb.pal";
    } else if (theater == "newurban") {
        paletteFileName = "isoubn.pal";
    } else if (theater == "lunar") {
        paletteFileName = "isolun.pal";
    }

    if (paletteFileName.empty()) {
        return "";
    }

    return pathString(
        std::filesystem::path(config.localDataRootPath)
            / "resources"
            / "mix_exports"
            / "palettes"
            / paletteFileName
    );
}

std::string runtimeAssetRootPath(const RuntimeConfig& config) {
    if (config.localDataRootPath.empty()) {
        return "";
    }

    return pathString(
        std::filesystem::path(config.localDataRootPath)
            / "resources"
            / "mix_exports"
    );
}

std::string runtimeStructureRootPath(
    const RuntimeConfig& config,
    const RuntimeMapSummary& map) {
    if (config.localDataRootPath.empty() || map.path.empty()) {
        return "";
    }

    const std::filesystem::path mapStem =
        std::filesystem::path(map.path).stem();
    if (mapStem.empty()) {
        return "";
    }

    return pathString(
        std::filesystem::path(config.localDataRootPath)
            / "resources"
            / "mix_exports"
            / "extracted_buildings"
            / mapStem
    );
}

std::string resolveRuntimeStructurePalettePath(
    const RuntimeConfig& config,
    const RuntimeMapSummary& map) {
    if (config.localDataRootPath.empty() || map.theater.empty()) {
        return "";
    }

    const std::filesystem::path mixExports =
        std::filesystem::path(config.localDataRootPath)
            / "resources"
            / "mix_exports";
    const std::filesystem::path paletteRoots[] = {
        mixExports / "extracted_palettes",
        mixExports / "palettes"
    };

    const std::vector<std::string> candidates =
        unitPaletteCandidatesForRuntimeTheater(map.theater);
    for (const std::string& candidateFileName : candidates) {
        for (const std::filesystem::path& root : paletteRoots) {
            const std::filesystem::path candidate = root / candidateFileName;
            std::error_code errorCode;
            if (std::filesystem::exists(candidate, errorCode)
                && std::filesystem::is_regular_file(candidate, errorCode)) {
                return pathString(candidate);
            }
        }
    }

    if (!candidates.empty()) {
        return pathString(paletteRoots[0] / candidates.front());
    }

    return "";
}

std::string runtimeStructurePalettePath(
    const RuntimeConfig& config, const RuntimeMapSummary& map) {
    auto* memo = RuntimePalettePathMemoScope::active;
    if (!memo || runtimePresentationReferenceEnabled())
        return resolveRuntimeStructurePalettePath(config, map);
    if (!memo->valid || memo->root != config.localDataRootPath
        || memo->theater != map.theater) {
        memo->valid = false;
        memo->root = config.localDataRootPath;
        memo->theater = map.theater;
        memo->path = resolveRuntimeStructurePalettePath(config, map);
        memo->valid = true;
    }
    return memo->path;
}

std::string resolveRuntimeModeRulesOverridePath(const RuntimeConfig& config) {
    std::filesystem::path configured = trimAsciiLocal(
        config.gameModeRulesOverride
    );
    if (configured.empty()) {
        configured = runtimeDefaultModeRulesOverride(config.gameMode);
    }
    if (configured.is_absolute()) {
        return pathExists(configured) ? configured.generic_string() : "";
    }

    const std::filesystem::path localDataRoot(config.localDataRootPath);
    const std::array<std::filesystem::path, 5> candidates = {{
        localDataRoot / "resources" / "mix_exports" / "expandmd01" / configured,
        localDataRoot / "resources" / "mix_exports" / "ra2md" / "localmd" / configured,
        localDataRoot / "resources" / "mix_exports" / "ra2md" / configured,
        localDataRoot / configured,
        configured
    }};
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code errorCode;
        if (std::filesystem::is_regular_file(candidate, errorCode)) {
            return candidate.generic_string();
        }
    }
    return "";
}

std::unordered_map<int, const TerrainCell*> buildRuntimeTerrainLookup(
    const GameWorld& world) {
    std::unordered_map<int, const TerrainCell*> terrainByCell;
    terrainByCell.reserve(world.getTerrainCells().size());
    for (const TerrainCell& cell : world.getTerrainCells()) {
        terrainByCell.emplace(runtimeCellKey(cell.x, cell.y), &cell);
    }
    return terrainByCell;
}

std::unordered_map<std::string, RuntimeStructureFootprint>
buildRuntimeStructureFootprints(const RuntimeGameSessionState& state) {
    std::unordered_map<std::string, RuntimeStructureFootprint> footprints;
    if (!state.artLoaded || !state.rulesLoaded) {
        return footprints;
    }

    std::unordered_set<std::string> usedStructureTypes;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.kind == WorldObjectKind::Structure) {
            usedStructureTypes.insert(object.type);
        }
    }

    for (const ArtObjectLink& link : state.art.buildLinks(state.rules)) {
        if (link.objectKind != "building"
            || link.foundation.empty()) {
            continue;
        }

        const RuntimeStructureFootprint footprint =
            parseRuntimeFoundationSize(link.foundation);
        if (footprint.valid) {
            footprints.emplace(link.objectId, footprint);
        }
    }

    for (const std::string& structureType : usedStructureTypes) {
        if (footprints.find(structureType) != footprints.end()) {
            continue;
        }

        const ArtType* directArt = state.art.findArtType(structureType);
        if (directArt == nullptr || directArt->foundation.empty()) {
            continue;
        }

        const RuntimeStructureFootprint footprint =
            parseRuntimeFoundationSize(directArt->foundation);
        if (footprint.valid) {
            footprints.emplace(structureType, footprint);
        }
    }

    return footprints;
}

RuntimeStructureFootprint runtimeStructureFootprintForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const auto footprintIt = state.structureFootprints.find(object.type);
    if (footprintIt != state.structureFootprints.end()
        && footprintIt->second.valid) {
        return footprintIt->second;
    }

    return {1, 1, true};
}

bool runtimeObjectUsesStructureFootprint(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    return object.kind == WorldObjectKind::Structure
        || state.structureFootprints.find(object.type)
            != state.structureFootprints.end();
}

RuntimeCellPoint runtimePlacementAnchorCellForFootprint(
    const RuntimeCellPoint& topLeftCell,
    const RuntimeStructureFootprint& footprint) {
    const RuntimeCellPoint offset =
        runtimePlacementFoundationCenterOffset(footprint);
    return {
        topLeftCell.x + offset.x,
        topLeftCell.y + offset.y
    };
}

std::vector<RuntimeCellPoint> runtimeStructureFootprintCells(
    const RuntimeCellPoint& topLeftCell,
    const RuntimeStructureFootprint& footprint) {
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    std::vector<RuntimeCellPoint> cells;
    cells.reserve(static_cast<std::size_t>(width * height));
    for (int dy = 0; dy < height; ++dy) {
        for (int dx = 0; dx < width; ++dx) {
            cells.push_back({topLeftCell.x + dx, topLeftCell.y + dy});
        }
    }
    return cells;
}

std::vector<RuntimeCellPoint> runtimeStructureFootprintCellsForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (runtimeInvisibleMapBuilding(&state.rules, object)) return {};
    return runtimeStructureFootprintCells(
        runtimeCellPointForObject(object),
        runtimeStructureFootprintForObject(state, object)
    );
}

bool runtimeStructureCellBlocksVehicleNavigation(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& structure,
    const RuntimeCellPoint& cell) {
    // Ordinary traffic goes around the whole building. Authored drive rows
    // are opened only by an explicit allowed host on a service/exit order.
    return runtimeStructureCellInsideFoundation(state, structure, cell);
}

std::vector<RuntimeCellPoint> runtimeStructureNavigationBlockerCellsForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& structure) {
    const std::vector<RuntimeCellPoint> foundation =
        runtimeStructureFootprintCellsForObject(state, structure);
    std::vector<RuntimeCellPoint> blockers;
    blockers.reserve(foundation.size());
    for (const RuntimeCellPoint& cell : foundation) {
        if (runtimeStructureCellBlocksVehicleNavigation(
                state,
                structure,
                cell
            )) {
            blockers.push_back(cell);
        }
    }
    return blockers;
}

std::uint64_t runtimeFootprintCellsHash(
    const std::vector<RuntimeCellPoint>& cells) {
    std::uint64_t hash = runtimeStableHashString("runtimeFootprintCells");
    for (const RuntimeCellPoint& cell : cells) {
        runtimeStableHashMix(hash, cell.x);
        runtimeStableHashMix(hash, cell.y);
    }
    return hash;
}

double runtimeCellDistanceToObjectFootprint(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point,
    const WorldObjectInstance& object) {
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, object);
    // Rectangular foundations: clamping selects the same nearest grid cell.
    const RuntimeCellPoint nearest{
        std::clamp(point.x, object.x, object.x + std::max(1, footprint.width) - 1),
        std::clamp(point.y, object.y, object.y + std::max(1, footprint.height) - 1)
    };
    return runtimeCellDistance(point, nearest);
}

double runtimeWorldDistanceToObjectFootprint(
    const RuntimeGameSessionState& state,
    double worldX,
    double worldY,
    const WorldObjectInstance& object) {
    double bestDistance = std::numeric_limits<double>::max();
    for (const RuntimeCellPoint& cell :
            runtimeStructureFootprintCellsForObject(state, object)) {
        bestDistance = std::min(
            bestDistance,
            std::hypot(
                worldX - static_cast<double>(cell.x),
                worldY - static_cast<double>(cell.y)
            )
        );
    }
    return bestDistance;
}

int runtimeTerrainOccupantId(std::size_t index) {
    constexpr std::size_t MaxTerrainOccupantOffset = 100000000U;
    return runtimeTerrainOccupantIdBase
        - static_cast<int>(std::min(index, MaxTerrainOccupantOffset));
}

bool runtimeOccupantIdIsTerrain(int occupantId) {
    return occupantId <= runtimeTerrainOccupantIdBase;
}

bool runtimeTerrainObjectBlocksGround(
    const WorldTerrainObject& object) {
    return object.alive
        && object.health > 0
        && (object.occupationBits != 0
            || !object.occupiedSubCells.empty());
}

std::vector<RuntimeCellPoint> runtimeTerrainObjectFootprintCells(
    const WorldTerrainObject& object) {
    const int width = std::max(1, object.footprintWidth);
    const int height = std::max(1, object.footprintHeight);
    std::vector<RuntimeCellPoint> cells;
    cells.reserve(static_cast<std::size_t>(width * height));
    for (int dy = 0; dy < height; ++dy) {
        for (int dx = 0; dx < width; ++dx) {
            cells.push_back({object.x + dx, object.y + dy});
        }
    }
    return cells;
}

std::string runtimeTerrainObjectStateKey(
    const WorldTerrainObject& object,
    std::size_t index) {
    const std::string mapId =
        toLowerAsciiLocal(trimAsciiLocal(object.mapId));
    if (!mapId.empty()) {
        return "id:" + mapId;
    }
    return "index:" + std::to_string(index)
        + ":" + toLowerAsciiLocal(trimAsciiLocal(object.type))
        + ":" + std::to_string(object.x)
        + ":" + std::to_string(object.y);
}

std::uint64_t runtimeTerrainObjectLayoutSignature(
    const RuntimeGameSessionState& state) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (std::size_t index = 0;
        index < state.world.getTerrainObjects().size();
        ++index) {
        const WorldTerrainObject& object =
            state.world.getTerrainObjects()[index];
        for (unsigned char character :
                runtimeTerrainObjectStateKey(object, index)) {
            mix(character);
        }
        mix(0xffU);
        for (unsigned char character : object.type) {
            mix(character);
        }
        mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(object.x)));
        mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(object.y)));
        mix(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(object.strength)
        ));
        mix(object.immune ? 1U : 0U);
        mix(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(object.occupationBits)
        ));
        mix(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(object.footprintWidth)
        ));
        mix(static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(object.footprintHeight)
        ));
        for (int subCell : object.occupiedSubCells) {
            mix(static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(subCell)
            ));
        }
        mix(0xfeU);
    }
    mix(state.world.getTerrainObjects().size());
    return hash;
}

void captureRuntimeInitialTerrainObjectState(RuntimeGameSessionState& state) {
    const std::uint64_t layoutSignature =
        runtimeTerrainObjectLayoutSignature(state);
    if (state.terrainObjectInitialStateCaptured
        && state.terrainObjectInitialLayoutSignature == layoutSignature) {
        return;
    }
    state.terrainObjectInitialStateByKey.clear();
    state.terrainObjectInitialStateByKey.reserve(
        state.world.getTerrainObjects().size()
    );
    for (std::size_t index = 0;
        index < state.world.getTerrainObjects().size();
        ++index) {
        const WorldTerrainObject& object =
            state.world.getTerrainObjects()[index];
        const int maximumHealth = std::max(1, object.strength);
        RuntimeTerrainObjectInitialState initial;
        initial.health = std::clamp(object.health, 0, maximumHealth);
        initial.alive = object.alive && initial.health > 0;
        if (!initial.alive) {
            initial.health = 0;
        }
        state.terrainObjectInitialStateByKey[
            runtimeTerrainObjectStateKey(object, index)
        ] = initial;
    }
    state.terrainObjectInitialStateCaptured = true;
    state.terrainObjectInitialLayoutSignature = layoutSignature;
}

std::unordered_map<int, int> buildRuntimeOccupancyLookup(
    const RuntimeGameSessionState& state,
    int ignoredObjectId,
    RuntimeNavigationStats* stats,
    const std::unordered_set<int>* ignoredObjectIds,
    bool includeStructures,
    std::unordered_map<int, int>* occupantCountByCell,
    RuntimeStructureOccupancyMode structureMode,
    std::size_t initialReserveLimit) {
    RuntimePerfTraceScope trace("buildRuntimeOccupancyLookup", state.tick);
    std::unordered_map<int, int> occupiedByCell;
    const std::size_t reserveEntries = std::min(initialReserveLimit,
        (state.world.getObjects().size() * 4)
            + state.world.getTerrainObjects().size());
    occupiedByCell.reserve(reserveEntries);
    if (occupantCountByCell != nullptr) {
        occupantCountByCell->clear();
        occupantCountByCell->reserve(reserveEntries);
    }
    int structureFootprintCells = 0;
    // Terrain objects are inserted first so an authored unit/tree overlap
    // keeps the indestructible navigation representative until the terrain
    // object actually dies.  Runtime movement is currently cell-granular;
    // any occupied infantry subcell therefore blocks the whole cell for a
    // non-air mover.
    for (std::size_t index = 0;
        index < state.world.getTerrainObjects().size();
        ++index) {
        const WorldTerrainObject& object =
            state.world.getTerrainObjects()[index];
        if (!runtimeTerrainObjectBlocksGround(object)) {
            continue;
        }
        for (const RuntimeCellPoint& cell :
                runtimeTerrainObjectFootprintCells(object)) {
            const int key = runtimeCellKey(cell.x, cell.y);
            if (key < 0) {
                continue;
            }
            occupiedByCell.emplace(key, runtimeTerrainOccupantId(index));
            if (occupantCountByCell != nullptr) {
                ++(*occupantCountByCell)[key];
            }
        }
    }
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.instanceId == ignoredObjectId
            || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
            || (!includeStructures
                && object.kind == WorldObjectKind::Structure)
            || (ignoredObjectIds != nullptr
                && ignoredObjectIds->find(object.instanceId)
                    != ignoredObjectIds->end())) {
            continue;
        }

        // Aircraft occupy the air domain rather than a ground navigation
        // cell. Parked planes are already covered by their airfield's
        // foundation, and airborne planes must never block land movement.
        if (object.kind != WorldObjectKind::Structure
            && runtimeObjectMovementMode(state, object)
                == PathMovementMode::Air
            && (object.airborne || object.dockedToObjectId >= 0)) {
            continue;
        }

        const std::vector<RuntimeCellPoint> cells =
            object.kind == WorldObjectKind::Structure
                && structureMode
                    == RuntimeStructureOccupancyMode::VehicleNavigation
            ? runtimeStructureNavigationBlockerCellsForObject(state, object)
            : runtimeStructureFootprintCellsForObject(state, object);
        for (const RuntimeCellPoint& cell : cells) {
            const int key = runtimeCellKey(cell.x, cell.y);
            if (key < 0) {
                continue;
            }
            occupiedByCell.emplace(key, object.instanceId);
            if (occupantCountByCell != nullptr) {
                ++(*occupantCountByCell)[key];
            }
            if (object.kind == WorldObjectKind::Structure) {
                ++structureFootprintCells;
            }
        }
    }

    if (stats != nullptr) {
        stats->occupiedCells = static_cast<int>(occupiedByCell.size());
        stats->structureFootprintCells = structureFootprintCells;
    }
    return occupiedByCell;
}

RuntimeGroundPoseOccupancyContext runtimeGroundPoseContext(const RuntimeGameSessionState& state) {
    return {&state, [](const void* opaque,int id) {
        const auto& state=*static_cast<const RuntimeGameSessionState*>(opaque);
        const auto found=state.behaviors.find(id);
        if (found==state.behaviors.end()) return false;
        const auto& behavior=found->second;
        return behavior.renderFromOffsetX!=0.0 || behavior.renderFromOffsetY!=0.0
            || behavior.renderToOffsetX!=0.0 || behavior.renderToOffsetY!=0.0;
    }};
}

bool runtimeGroundPoseIsSpatialCandidate(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    return worldObjectHasGroundRenderPose(object) && object.alive && object.health>0
        && !object.authoredAircraft && std::isfinite(object.renderX) && std::isfinite(object.renderY)
        && (object.renderMoveActive || worldObjectHasStoppedGroundPose(object))
        && !runtimeObjectOffGroundByMagBeam(state,object.instanceId)
        && runtimeObjectMovementMode(state,object)!=PathMovementMode::Air;
}

bool runtimeGroundPoseSpatialIndexIsCurrent(const RuntimeGameSessionState& state) {
    const auto& proof=state.movementEmptyCellProof;const auto& objects=state.world.getObjects();
    return proof.additionalGroundPoseIndexReady && state.tickOccupancyCacheTick==state.tick
        && proof.objectData==objects.data() && proof.objectCount==objects.size()
        && proof.additionalGroundPoseIndexedObjects.size()==objects.size();
}

bool runtimeGroundPoseSpatialKeys(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,std::vector<int>& keys) {
    keys.clear();
    const auto add=[&](int x,int y) {
        if(x<0 || y<0 || x>=512 || y>=512) return false;
        keys.push_back(runtimeCellKey(x,y));return true;
    };
    if(object.renderMoveActive) {
        const auto found=state.behaviors.find(object.instanceId);
        if(found==state.behaviors.end()) return false;
        const auto& move=found->second;const auto slot=worldObjectSubCellOffset(object);
        const double x[]={object.renderX+slot.x,move.renderFrom.x+move.renderFromOffsetX+slot.x,
            move.renderFrom.x+slot.x,move.renderTo.x+move.renderToOffsetX+slot.x};
        const double y[]={object.renderY+slot.y,move.renderFrom.y+move.renderFromOffsetY+slot.y,
            move.renderFrom.y+slot.y,move.renderTo.y+move.renderToOffsetY+slot.y};
        for(int n=0;n<4;++n) if(!std::isfinite(x[n]) || !std::isfinite(y[n])) return false;
        const double minX=*std::min_element(x,x+4),maxX=*std::max_element(x,x+4);
        const double minY=*std::min_element(y,y+4),maxY=*std::max_element(y,y+4);
        const bool foot=object.kind==WorldObjectKind::Infantry;
        // Round infantry centers exactly as the scalar predicate does.
        // Expand boundary points by one ulp against interpolation roundoff.
        const double left=foot?std::floor(std::nextafter(minX+0.5,-std::numeric_limits<double>::infinity())):std::floor(minX);
        const double right=foot?std::floor(std::nextafter(maxX+0.5,std::numeric_limits<double>::infinity())):std::ceil(maxX);
        const double top=foot?std::floor(std::nextafter(minY+0.5,-std::numeric_limits<double>::infinity())):std::floor(minY);
        const double bottom=foot?std::floor(std::nextafter(maxY+0.5,std::numeric_limits<double>::infinity())):std::ceil(maxY);
        if(left<0 || top<0 || right>=512 || bottom>=512
            || (right-left+1)*(bottom-top+1)>4096) return false;
        for(int row=static_cast<int>(top);row<=static_cast<int>(bottom);++row)
            for(int column=static_cast<int>(left);column<=static_cast<int>(right);++column)
                if(!add(column,row)) return false;
    } else {
        const auto context=runtimeGroundPoseContext(state);bool valid=true;
        forEachRuntimeAdditionalGroundPoseOccupancy(object,[&](const auto& held) {
            valid=add(held.x,held.y)&&valid;
        },&context);
        const auto slot=worldObjectSubCellOffset(object);
        const double x=std::floor(object.renderX+slot.x+0.5),y=std::floor(object.renderY+slot.y+0.5);
        if(!valid || x<0 || y<0 || x>=512 || y>=512
            || !add(static_cast<int>(x),static_cast<int>(y))) return false;
        // Same-navigation-cell stopped bodies still constrain an already
        // accepted follower, although they have no additional held cell.
    }
    std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
    return true;
}

bool runtimeAllGroundBodyIsSpatialCandidate(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    return object.alive && object.health>0 && !object.airborne
        && (object.kind==WorldObjectKind::Unit || object.kind==WorldObjectKind::Infantry)
        && object.dockedToObjectId<0 && object.tankBunkerHostId<0 && object.garrisonHostObjectId<0
        && !runtimeObjectOffGroundByMagBeam(state,object.instanceId)
        && runtimeObjectMovementMode(state,object)!=PathMovementMode::Air;
}

bool runtimeAllGroundBodySpatialIndexIsCurrent(const RuntimeGameSessionState& state) {
    const auto& proof=state.movementEmptyCellProof;const auto& objects=state.world.getObjects();
    return proof.allGroundBodyIndexReady && state.tickOccupancyCacheTick==state.tick
        && proof.objectData==objects.data() && proof.objectCount==objects.size();
}

bool runtimeAppendAllGroundBodySpatialObject(RuntimeGameSessionState& state,std::size_t index) {
    const auto& objects=state.world.getObjects();
    if(index>=objects.size()) return false;
    const auto& object=objects[index];
    if(!runtimeAllGroundBodyIsSpatialCandidate(state,object)) return true;
    std::vector<int> keys;keys.reserve(16);
    if(object.hasRenderPosition && object.renderMoveActive) {
        if(!runtimeGroundPoseSpatialKeys(state,object,keys)) return false;
    } else {
        const auto slot=worldObjectSubCellOffset(object);
        const double x=std::floor(runtimeObjectRenderX(object)+slot.x+0.5);
        const double y=std::floor(runtimeObjectRenderY(object)+slot.y+0.5);
        if(!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=512 || y>=512) return false;
        keys.push_back(runtimeCellKey(static_cast<int>(x),static_cast<int>(y)));
    }
    for(int key:keys) {
        auto& bucket=state.movementEmptyCellProof.allGroundBodyIndicesByCell[key];
        if(std::find(bucket.begin(),bucket.end(),index)==bucket.end()) bucket.push_back(index);
    }
    return true;
}

bool buildRuntimeGroundPoseSpatialIndex(RuntimeGameSessionState& state) {
    auto& proof=state.movementEmptyCellProof;proof.additionalGroundPoseIndexReady=false;
    auto& buckets=proof.additionalGroundPoseIndicesByCell;buckets.clear();
    const auto& objects=state.world.getObjects();
    proof.additionalGroundPoseIndexedObjects.assign(objects.size(),0);
    std::vector<int> keys;keys.reserve(16);
    for(std::size_t index:proof.additionalGroundPoseObjectIndices) {
        if(index>=objects.size()) return false;
        const auto& object=objects[index];
        if(!runtimeGroundPoseIsSpatialCandidate(state,object)) continue;
        if(!runtimeGroundPoseSpatialKeys(state,object,keys)) return false;
        for(int key:keys) buckets[key].push_back(index);
        proof.additionalGroundPoseIndexedObjects[index]=1;
    }
    proof.additionalGroundPoseIndexReady=true;return true;
}

bool runtimeTrackGroundPoseSpatialMove(RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if(runtimeAllGroundBodyIsSpatialCandidate(state,object)) {
        if(!runtimeAllGroundBodySpatialIndexIsCurrent(state)) return false;
        const auto& objects=state.world.getObjects();
        const auto base=reinterpret_cast<std::uintptr_t>(objects.data());
        const auto address=reinterpret_cast<std::uintptr_t>(&object);
        if(address<base || (address-base)%sizeof(WorldObjectInstance)!=0) return false;
        const auto index=(address-base)/sizeof(WorldObjectInstance);
        if(!runtimeAppendAllGroundBodySpatialObject(state,index)) return false;
    }
    if(!runtimeGroundPoseIsSpatialCandidate(state,object)) return true;
    if(!runtimeGroundPoseSpatialIndexIsCurrent(state)) return false;
    auto& proof=state.movementEmptyCellProof;const auto& objects=state.world.getObjects();
    const auto base=reinterpret_cast<std::uintptr_t>(objects.data());
    const auto address=reinterpret_cast<std::uintptr_t>(&object);
    if(address<base || (address-base)%sizeof(WorldObjectInstance)!=0) return false;
    const auto index=(address-base)/sizeof(WorldObjectInstance);
    if(index>=objects.size()) return false;
    std::vector<int> keys;keys.reserve(16);
    if(!runtimeGroundPoseSpatialKeys(state,object,keys)) return false;
    if(!proof.additionalGroundPoseIndexedObjects[index]) {
        proof.additionalGroundPoseIndexedObjects[index]=1;
        proof.additionalGroundPoseObjectIndices.push_back(index);
    }
    for(int key:keys) {
        auto& bucket=proof.additionalGroundPoseIndicesByCell[key];
        if(std::find(bucket.begin(),bucket.end(),index)==bucket.end()) bucket.push_back(index);
    }
    return true;
}

bool runtimeFrameScalingReferenceMovement() {
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_FRAME_SCALING_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

void captureRuntimeMovementEmptyCellProof(RuntimeGameSessionState& state) {
    auto& proof = state.movementEmptyCellProof;
    const auto& objects = state.world.getObjects();
    const auto& terrainObjects = state.world.getTerrainObjects();
    const auto& terrainCells = state.world.getTerrainCells();
    proof.subcellCompatible = true;
    proof.valid = state.tickCrushEmptyCellProofValid
        && state.magBeams.bindings().empty();
    proof.objectData = objects.data();
    proof.terrainObjectData = terrainObjects.data();
    proof.terrainCellData = terrainCells.data();
    proof.mapFactCellData = state.mapFacts.cells.data();
    proof.objectCount = objects.size();
    proof.terrainObjectCount = terrainObjects.size();
    proof.terrainCellCount = terrainCells.size();
    proof.mapFactCellCount = state.mapFacts.cells.size();
    proof.unindexedFoundations.clear();
    proof.subcellAirObjectIndices.clear();
    proof.additionalGroundPoseObjectIndices.clear();
    proof.additionalGroundPoseIndexReady=false;
    proof.allGroundBodyIndicesByCell.clear();
    proof.allGroundBodyIndexReady=true;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const auto& object = objects[index];
        if (!runtimeAppendAllGroundBodySpatialObject(state,index)) proof.allGroundBodyIndexReady=false;
        if (!object.alive) continue;
        if (runtimeGroundPoseIsSpatialCandidate(state,object))
            proof.additionalGroundPoseObjectIndices.push_back(index);
        // Navigation can omit airborne/docked Air objects. Their current
        // ground pose is checked again at query time, including same-tick landings.
        if (object.kind != WorldObjectKind::Structure
            && runtimeObjectMovementMode(state, object) == PathMovementMode::Air)
            proof.subcellAirObjectIndices.push_back(index);
        // Navigation omits id -1. Conservatively exclude all synthetic ids.
        if (object.instanceId < 0) proof.valid = false;
        if (object.kind != WorldObjectKind::Structure) continue;
        const auto footprint = runtimeStructureFootprintForObject(state, object);
        const std::int64_t width = std::max(1, footprint.width);
        const std::int64_t height = std::max(1, footprint.height);
        // The scalar hard-blocker predicate sees the whole foundation even
        // where vehicle navigation omits a drive row or an invisible building.
        if (runtimeInvisibleMapBuilding(&state.rules, object)
            || runtimeStructureBlockedDriveRowCount(state, object) < width) {
            proof.unindexedFoundations.push_back({object.x, object.y,
                static_cast<std::int64_t>(object.x) + width - 1,
                static_cast<std::int64_t>(object.y) + height - 1});
        }
    }
    if(!buildRuntimeGroundPoseSpatialIndex(state)) proof.valid=false;
    for (const auto& terrain : terrainObjects) {
        if (!runtimeTerrainObjectBlocksGround(terrain)) continue;
        const std::int64_t width = std::max(1, terrain.footprintWidth);
        const std::int64_t height = std::max(1, terrain.footprintHeight);
        if (width > 4096 || height > 4096
            || static_cast<std::int64_t>(terrain.x) + width - 1
                > std::numeric_limits<int>::max()
            || static_cast<std::int64_t>(terrain.y) + height - 1
                > std::numeric_limits<int>::max()) proof.valid = false;
    }
}

bool runtimeMovementTickProvesEmptyCell(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& cell) {
    const auto& proof = state.movementEmptyCellProof;
    const auto& objects = state.world.getObjects();
    const auto& terrainObjects = state.world.getTerrainObjects();
    const auto& terrainCells = state.world.getTerrainCells();
    if (!proof.valid || state.tickOccupancyCacheTick != state.tick
        || !state.magBeams.bindings().empty()
        || proof.objectData != objects.data() || proof.objectCount != objects.size()
        || proof.terrainObjectData != terrainObjects.data()
        || proof.terrainObjectCount != terrainObjects.size()
        || proof.terrainCellData != terrainCells.data()
        || proof.terrainCellCount != terrainCells.size()
        || proof.mapFactCellData != state.mapFacts.cells.data()
        || proof.mapFactCellCount != state.mapFacts.cells.size()
        // Keep unusual and legacy coordinate encodings on the scalar path.
        || cell.x < 0 || cell.y < 0 || cell.x >= 512 || cell.y >= 512) return false;
    const int key = runtimeCellKey(cell.x, cell.y);
    if (state.tickOccupiedByCell.contains(key)
        || state.tickGroundOccupantCountByCell.contains(key)) return false;
    if(!proof.additionalGroundPoseIndexReady
        || proof.additionalGroundPoseIndicesByCell.contains(key)) return false;
    for (const auto& bounds : proof.unindexedFoundations) {
        if (cell.x >= bounds[0] && cell.y >= bounds[1]
            && cell.x <= bounds[2] && cell.y <= bounds[3]) return false;
    }
    return true;
}

const std::unordered_map<int, int>& runtimeTickOccupancyLookup(
    RuntimeGameSessionState& state) {
    if (state.tickOccupancyCacheTick == state.tick) {
        return state.tickOccupiedByCell;
    }

    const bool stageReference = runtimeSimulationStageAdmissionReference();
    const std::size_t originalReserve = state.world.getObjects().size() * 4
        + state.world.getTerrainObjects().size();
    const std::size_t selectedReserve = runtimeSimulationStageInitialReserve(originalReserve, stageReference);
    RuntimeSimulationStageAdmissionObservation observation(
        RuntimeSimulationStageAdmissionKind::TickOccupancy, stageReference, state.world.getObjects().size());
    observation.stats.admitted = !stageReference;
    observation.stats.originalReserveEntries = originalReserve * 2;
    observation.stats.selectedReserveEntries = selectedReserve * 2;
    state.tickOccupiedByCell = buildRuntimeOccupancyLookup(
        state,
        -1,
        nullptr,
        nullptr,
        true,
        &state.tickGroundOccupantCountByCell,
        RuntimeStructureOccupancyMode::VehicleNavigation,
        selectedReserve
    );
    state.tickCrushEmptyCellProofValid = state.magBeams.bindings().empty();
    state.tickCrushEmptyCellObjectCount = state.world.getObjects().size();
    state.tickCrushUnindexedFoundationBounds.clear();
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive) continue;
        // The occupancy builder's default ignored id is -1, while a direct
        // crush scan may still see a synthetic object bearing that id.
        if (object.instanceId == -1) state.tickCrushEmptyCellProofValid = false;
        if (object.kind != WorldObjectKind::Structure) continue;
        const auto footprint = runtimeStructureFootprintForObject(state, object);
        const std::int64_t width = std::max(1, footprint.width);
        const std::int64_t height = std::max(1, footprint.height);
        const std::int64_t right = static_cast<std::int64_t>(object.x) + width - 1;
        const std::int64_t bottom = static_cast<std::int64_t>(object.y) + height - 1;
        if (width > 4096 || height > 4096
            || right >= std::numeric_limits<int>::max()
            || bottom >= std::numeric_limits<int>::max()) {
            state.tickCrushEmptyCellProofValid = false;
            continue;
        }
        if (runtimeInvisibleMapBuilding(&state.rules, object)) {
            state.tickCrushUnindexedFoundationBounds.push_back(
                {object.x, object.y, right, bottom});
        }
    }
    state.tickOccupancyCacheTick = state.tick;
    captureRuntimeMovementEmptyCellProof(state);
    return state.tickOccupiedByCell;
}

void updateRuntimeTickOccupancyForMove(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeCellPoint& previous,
    const RuntimeCellPoint& next) {
    RuntimePerfTraceScope movePhaseTrace("moveOccupancyUpdate", state.tick);
    if (runtimeObjectMovementMode(state, object) == PathMovementMode::Air) {
        return;
    }

    state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();

    if (state.tickOccupancyCacheTick != state.tick) {
        return;
    }

    // A moved structure requires its full foundation and exception bounds.
    if (object.kind == WorldObjectKind::Structure) {
        state.tickCrushEmptyCellProofValid = false;
        state.movementEmptyCellProof.valid = false;
    }

    const int previousKey = runtimeCellKey(previous.x, previous.y);
    if (previousKey >= 0) {
        int remainingOccupants = 0;
        const auto countIt =
            state.tickGroundOccupantCountByCell.find(previousKey);
        if (countIt != state.tickGroundOccupantCountByCell.end()) {
            if (countIt->second > 1) {
                --countIt->second;
                remainingOccupants = countIt->second;
            } else {
                state.tickGroundOccupantCountByCell.erase(countIt);
            }
        }
        const auto previousIt = state.tickOccupiedByCell.find(previousKey);
        if (previousIt != state.tickOccupiedByCell.end()
            && previousIt->second == object.instanceId) {
            state.tickOccupiedByCell.erase(previousIt);
            // A navigation cell can legitimately contain more than one
            // object (for example, authored overlaps or a crush stack).  The
            // lookup stores one representative, so promote another live
            // ground occupant when that representative leaves instead of
            // making the remaining objects invisible for the rest of the
            // tick.
            for (const WorldObjectInstance& candidate : state.world.getObjects()) {
                if (remainingOccupants <= 0) {
                    break;
                }
                if (!candidate.alive
                    || candidate.instanceId == object.instanceId
                    || (candidate.kind != WorldObjectKind::Structure
                        && runtimeObjectMovementMode(state, candidate)
                            == PathMovementMode::Air)) {
                    continue;
                }

                const bool occupiesPrevious =
                    candidate.kind == WorldObjectKind::Structure
                        ? (object.kind == WorldObjectKind::Unit
                            ? runtimeStructureCellBlocksVehicleNavigation(
                                state,
                                candidate,
                                previous
                            )
                            : [&]() {
                                const std::vector<RuntimeCellPoint>
                                    candidateCells =
                                        runtimeStructureFootprintCellsForObject(
                                            state,
                                            candidate
                                        );
                                return std::find(
                                    candidateCells.begin(),
                                    candidateCells.end(),
                                    previous
                                ) != candidateCells.end();
                            }())
                        : candidate.x == previous.x && candidate.y == previous.y;
                if (occupiesPrevious) {
                    state.tickOccupiedByCell.emplace(
                        previousKey,
                        candidate.instanceId
                    );
                    break;
                }
            }
        }
    }

    const int nextKey = runtimeCellKey(next.x, next.y);
    if (nextKey >= 0) {
        state.tickOccupiedByCell[nextKey] = object.instanceId;
        ++state.tickGroundOccupantCountByCell[nextKey];
    }
}

void invalidateRuntimeTickOccupancyCache(RuntimeGameSessionState& state) {
    state.foundationCollisionTick = -1;
    state.tickOccupancyCacheTick = -1;
    state.movementEmptyCellProof.valid = false;
    state.movementEmptyCellProof.unindexedFoundations.clear();
    state.movementEmptyCellProof.additionalGroundPoseIndexReady = false;
    state.movementEmptyCellProof.additionalGroundPoseIndicesByCell.clear();
    state.movementEmptyCellProof.additionalGroundPoseIndexedObjects.clear();
    state.movementEmptyCellProof.additionalGroundPoseObjectIndices.clear();
    state.movementEmptyCellProof.allGroundBodyIndexReady = false;
    state.movementEmptyCellProof.allGroundBodyIndicesByCell.clear();
    state.tickOccupiedByCell.clear();
    state.tickGroundOccupantCountByCell.clear();
    state.tickCrushEmptyCellProofValid = false;
    state.tickCrushEmptyCellObjectCount = 0;
    state.tickCrushUnindexedFoundationBounds.clear();
    state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();
}

bool runtimeUnitUsesJumpjetMovement(const UnitType& unit) {
    if (!runtimeMovementClassificationReference()) return runtimeUnitJumpjetMovementProfile(unit);
    return unit.jumpJet || toLowerAsciiLocal(trimAsciiLocal(unit.locomotor))
        == "{92612c46-f71f-11d1-ac9f-006008055bb5}";
}

bool runtimeUnitUsesAirMovement(const UnitType& unit) {
    if (!runtimeMovementClassificationReference()) return runtimeUnitAirMovementProfile(unit);
    const std::string speedType = toLowerAsciiLocal(unit.speedType);
    const std::string movementZone = toLowerAsciiLocal(unit.movementZone);
    const std::string locomotor = toLowerAsciiLocal(unit.locomotor);
    return runtimeUnitUsesJumpjetMovement(unit)
        || unit.consideredAircraft
        || movementZone.find("fly") != std::string::npos
        || speedType.find("winged") != std::string::npos
        || speedType.find("fly") != std::string::npos
        || locomotor.find("fly") != std::string::npos;
}

int runtimeTransportPassengerSpace(
    const RuntimeGameSessionState& state, const WorldObjectInstance& transport,
    const WorldObjectInstance& passenger) {
    if (runtimeInfantryAbsorberType(state, transport) != nullptr) return 1;
    const auto* type = findRuntimeUnitType(state.rules, passenger);
    return type == nullptr ? 1 : std::max(1, type->size);
}

bool runtimeTransportPassengerTypeAllowed(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport) {
    const bool absorber = runtimeInfantryAbsorberType(state, transport) != nullptr;
    if (passenger.kind != WorldObjectKind::Infantry && passenger.kind != WorldObjectKind::Unit) return false;
    const auto* type = findRuntimeUnitType(state.rules, passenger);
    if (type == nullptr) return false;
    const int limit = runtimeTransportPassengerSizeLimit(state, transport);
    if (limit > 0 && std::max(1, type->size) > limit) return false;
    if (passenger.kind == WorldObjectKind::Unit) {
        const auto movement = runtimeObjectMovementMode(state, passenger);
        return !absorber && limit > 0 && movement != PathMovementMode::Air
            && movement != PathMovementMode::Naval;
    }
    return true;
}

int runtimeTransportUsedSpace(const RuntimeGameSessionState& state, const WorldObjectInstance& transport) {
    std::int64_t used = 0;
    const auto& objects = state.world.getObjects();
    for (int id : transport.transportPassengerIds) {
        const auto found = std::find_if(objects.begin(), objects.end(),
            [id](const auto& object) { return object.instanceId == id; });
        used += found == objects.end() ? 1 : runtimeTransportPassengerSpace(state, transport, *found);
    }
    return static_cast<int>(std::min<std::int64_t>(used, std::numeric_limits<int>::max()));
}

bool runtimeAmphibiousTransport(const RuntimeGameSessionState& state, const WorldObjectInstance& transport) {
    const auto* type = findRuntimeUnitType(state.rules, transport);
    return transport.kind == WorldObjectKind::Unit && type != nullptr && type->passengers > 0
        && type->naval && runtimeObjectMovementMode(state, transport) == PathMovementMode::Amphibious;
}

bool runtimeTransportContainsCargo(
    const RuntimeGameSessionState& state, const WorldObjectInstance& root, int candidateId) {
    std::vector<int> pending = root.transportPassengerIds;
    std::unordered_set<int> seen{root.instanceId};
    const auto& objects = state.world.getObjects();
    while (!pending.empty()) {
        const int id = pending.back(); pending.pop_back();
        if (id == candidateId) return true;
        if (!seen.insert(id).second) continue;
        const auto found = std::find_if(objects.begin(), objects.end(),
            [id](const auto& object) { return object.instanceId == id; });
        if (found != objects.end()) pending.insert(pending.end(),
            found->transportPassengerIds.begin(), found->transportPassengerIds.end());
    }
    return false;
}

void syncRuntimeTransportCargoPositions(RuntimeGameSessionState& state, const WorldObjectInstance& root) {
    if (root.transportPassengerIds.empty()) return;
    std::vector<int> pending = root.transportPassengerIds;
    std::unordered_set<int> seen{root.instanceId};
    auto& objects = state.world.accessObjects();
    while (!pending.empty()) {
        const int id = pending.back(); pending.pop_back();
        if (!seen.insert(id).second) continue;
        const auto found = std::find_if(objects.begin(), objects.end(),
            [id](const auto& object) { return object.instanceId == id; });
        if (found == objects.end() || found->alive || found->health <= 0) continue;
        found->x = root.x; found->y = root.y; found->cell = root.cell;
        found->level = root.level; found->onBridge = root.onBridge;
        found->subCell = -1; found->hasRenderPosition = found->renderMoveActive = false;
        pending.insert(pending.end(), found->transportPassengerIds.begin(), found->transportPassengerIds.end());
    }
}

RuntimeWeaponProfile runtimeGarrisonWeaponProfileForOccupant(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& occupant,
    const WorldObjectInstance* target,
    bool forceFire) {
    RuntimeWeaponProfile profile;
    const UnitType* unit = findRuntimeUnitType(state.rules, occupant);
    if (unit == nullptr || !unit->occupier) {
        return profile;
    }

    std::vector<RuntimeWeaponCandidate> candidates;
    if (occupant.veterancy >= 2) {
        candidates = {
            {RuntimeWeaponSlot::EliteOccupy, unit->eliteOccupyWeapon},
            {RuntimeWeaponSlot::Occupy, unit->occupyWeapon},
            {RuntimeWeaponSlot::ElitePrimary, unit->elitePrimaryWeapon},
            {RuntimeWeaponSlot::Primary, unit->primaryWeapon}
        };
    } else {
        candidates = {
            {RuntimeWeaponSlot::Occupy, unit->occupyWeapon},
            {RuntimeWeaponSlot::Primary, unit->primaryWeapon}
        };
    }

    std::unordered_set<std::string> visited;
    for (const RuntimeWeaponCandidate& candidate : candidates) {
        const std::string weaponKey = toLowerAsciiLocal(
            trimAsciiLocal(candidate.second)
        );
        if (weaponKey.empty() || !visited.insert(weaponKey).second) {
            continue;
        }
        const WeaponType* weapon = findRuntimeWeaponType(
            state.rules,
            candidate.second
        );
        if (weapon == nullptr || weapon->neverUse) {
            continue;
        }
        const WarheadType* targetWarhead = findRuntimeWarheadType(state.rules, weapon->warhead);
        if (target != nullptr && targetWarhead != nullptr && targetWarhead->mindControl
            && runtimeOwnersAreAllied(state, occupant.owner, target->owner)) {
            return profile;
        }
        // RuntimeGarrisonSystem deliberately chooses the occupant's first
        // usable authored weapon before it sees a target. Mirror that order
        // here instead of silently falling through to a different weapon.
        if (target != nullptr
            && (!runtimeWeaponSlotCanTargetDomain(
                    state,
                    occupant,
                    candidate.first,
                    *target,
                    forceFire
                )
                || !runtimeWeaponCanTargetObject(state, *weapon, *target))) {
            return profile;
        }
        finalizeRuntimeWeaponProfile(
            profile,
            state,
            candidate.first,
            *weapon,
            "garrisonOccupant"
        );
        profile.occupied = true;
        return profile;
    }
    return profile;
}

double runtimeMovementBaseIntervalTicks(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    const int hz = std::clamp(state.config.fixedTickHz, 1, 1000);
    if (!unit || unit->speed <= 0) return 8.0 * hz / 30.0;
    // Reaction and posture form one locomotion factor; veterancy remains an
    // independent factor so none of these states replaces another speed bonus.
    double locomotionFactor = state.combatDetails.panics.contains(object.instanceId) ? 2.0 : 1.0;
    if (object.kind == WorldObjectKind::Infantry && object.prone) locomotionFactor /= 2.0;
    const double multiplier = locomotionFactor
        * runtimeVeterancyAbilityMultiplier(state, object, VeteranAbility::Faster);
    if (object.airborne && runtimeUnitUsesJumpjetMovement(*unit)) {
        const double authoredSpeed = std::max(0.01, unit->jumpjetSpeed);
        return 256.0 * hz / (runtimeRuleFramesPerSecond * std::max(0.01, authoredSpeed * multiplier));
    }
    // Retail TechnoType::Read_INI (0x714664) stores Speed as a percentage
    // of 256 leptons per rule frame. Use the same clock as Jumpjet, weapons
    // and harvesting; rounding 30/Speed here gave ground units an unrelated
    // sevenfold cruise boost and distorted the ratios between unit types.
    const int speedLeptons = runtimeRuleSpeedLeptons(unit->speed);
    return 256.0 * hz / (runtimeRuleFramesPerSecond
        * std::max(0.01, speedLeptons * multiplier));
}

int runtimeMovementIntervalTicks(const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    return static_cast<int>(std::clamp(std::round(runtimeMovementBaseIntervalTicks(state, object)),
        1.0, double(std::numeric_limits<int>::max() - 1)));
}

std::int64_t runtimeMovementSegmentIntervalQ16(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    const double distance = std::max(1.0, std::hypot(double(to.x)-from.x, double(to.y)-from.y));
    return static_cast<std::int64_t>(std::llround(std::clamp(
        runtimeMovementBaseIntervalTicks(state, object) * distance,
        1.0, double(std::numeric_limits<int>::max() - 1)) * 65536.0));
}

bool runtimeObjectUsesVehicleArrival(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const RuntimeVehicleArrivalHostQueries queries(state);
    return ::runtimeObjectUsesVehicleArrival(queries, object);
}

double prepareRuntimeVehicleArrival(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    const RuntimeVehicleArrivalHostQueries queries(state);
    return ::prepareRuntimeVehicleArrival(queries, object, behavior, from, to);
}

bool runtimeObjectUsesVehicleStartup(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (object.kind != WorldObjectKind::Unit || object.airborne) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!type || !type->accelerates || type->organic || type->speed <= 0) return false;
    const auto mode = runtimeObjectMovementMode(state, object);
    return mode == PathMovementMode::Ground || mode == PathMovementMode::Amphibious
        || mode == PathMovementMode::Naval;
}

double runtimeVehicleStartupAccelerationFactor(const UnitType& type) {
    const bool hover = toLowerAsciiLocal(type.speedType) == "hover"
        || toLowerAsciiLocal(type.locomotor) == "{4a582742-9839-11d1-b709-00a024ddafd1}";
    return hover ? type.hoverAcceleration : type.accelerationFactor;
}

bool runtimeVehicleStartupCannotMove(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    return runtimeObjectUsesVehicleStartup(state, object) && type
        && !(runtimeVehicleStartupAccelerationFactor(*type) > 0.0);
}

double runtimeVehicleStartupAccelerationDistance(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!runtimeObjectUsesVehicleStartup(state, object) || !type) return 0.0;
    const double factor = runtimeVehicleStartupAccelerationFactor(*type);
    const double cruiseCellsPerRuleFrame = std::clamp(state.config.fixedTickHz, 1, 1000)
        / (runtimeMovementBaseIntervalTicks(state, object) * runtimeRuleFramesPerSecond);
    return RuntimeVehicleStartupMotion::accelerationDistance(cruiseCellsPerRuleFrame, factor);
}

double prepareRuntimeVehicleStartup(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to, double arrivalScale) {
    behavior.renderStartupAccelerationDistance = 0.0;
    if (!runtimeObjectUsesVehicleStartup(state, object)
        || (behavior.kind != RuntimeBehaviorKind::Move && behavior.kind != RuntimeBehaviorKind::Attack)
        || from == to || std::abs(to.x-from.x)>1 || std::abs(to.y-from.y)>1) return arrivalScale;
    const double ramp = runtimeVehicleStartupAccelerationDistance(state, object)
        * runtimeTerrainMovementSpeedMultiplier(state, object, from, to);
    if (!std::isfinite(ramp)) return std::numeric_limits<double>::infinity();
    const double length = std::hypot(double(to.x-from.x), double(to.y-from.y));
    const auto rampQ16 = RuntimeVehicleStartupMotion::rampDistanceQ16(ramp);
    if (runtimeFactoryEgressBypassesArrival(state, object, behavior)) {
        behavior.moveStartupDistanceQ16 = std::min(RuntimeVehicleStartupMotion::maximumDistanceQ16,
            rampQ16 + RuntimeVehicleStartupMotion::distanceQ16(length));
        return arrivalScale;
    }
    if (behavior.state == RuntimeBehaviorState::Blocked && behavior.moveCooldownTicks <= 0)
        behavior.moveStartupDistanceQ16 = 0;
    const auto beforeQ16 = std::clamp(behavior.moveStartupDistanceQ16, std::int64_t(0), rampQ16);
    const auto lengthQ16 = RuntimeVehicleStartupMotion::distanceQ16(length);
    behavior.moveStartupDistanceQ16 = std::min(RuntimeVehicleStartupMotion::maximumDistanceQ16,
        beforeQ16 + lengthQ16);
    const double before = double(beforeQ16) / RuntimeVehicleStartupMotion::distanceOneQ16;
    if (before >= ramp || ramp <= 0.0) return arrivalScale;
    behavior.renderStartupAccelerationDistance = ramp;
    return RuntimeVehicleStartupMotion::durationScale(length, before, ramp,
        behavior.renderArrivalDistanceAfter);
}

int consumeRuntimeMovementSegmentIntervalTicks(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to) {
    if (object.airborne && runtimeJumpjetProfile(state, object))
        return beginRuntimeJumpjetSegment(state, object, from, to);
    const double terrainMultiplier = runtimeTerrainMovementSpeedMultiplier(state, object, from, to);
    constexpr std::int64_t q16Half = 1LL << 15U;
    constexpr std::int64_t q16One = 1LL << 16U;
    const double arrivalScale = prepareRuntimeVehicleArrival(state, object, behavior, from, to);
    const double scale = prepareRuntimeVehicleStartup(state, object, behavior, from, to, arrivalScale);
    const std::int64_t baseQ16 = runtimeMovementSegmentIntervalQ16(state, object, from, to);
    const double dockScale = runtimeRepairDepotApproachDistanceScale(state, object, from, to);
    double segmentQ16 = std::clamp(baseQ16 * scale * dockScale / terrainMultiplier,
        double(q16One), double(std::numeric_limits<int>::max() - 1) * q16One);
    if (runtimeFactoryEgressUsesFastClock(state, object, behavior)) {
        // Fast build may finish the next order in one tick. Keep physical
        // occupancy and interpolation, with a 0.2 second per-cell exit cap.
        const double distance = std::max(1.0, std::hypot(double(to.x - from.x), double(to.y - from.y)));
        const double fastQ16 = std::max(double(q16One),
            0.2 * std::clamp(state.config.fixedTickHz, 1, 1000) * distance * q16One);
        segmentQ16 = std::min(segmentQ16, fastQ16);
    }
    const std::int64_t accumulated = static_cast<std::int64_t>(std::llround(segmentQ16))
        + behavior.moveIntervalRemainderQ16;
    const std::int64_t wholeTicks = (accumulated + q16Half) >> 16U;
    const std::int64_t remainder = accumulated - wholeTicks * q16One;
    behavior.moveIntervalRemainderQ16 = static_cast<std::int32_t>(remainder);
    if (wholeTicks >= static_cast<std::int64_t>(
            std::numeric_limits<int>::max())) {
        behavior.moveIntervalRemainderQ16 = 0;
        return std::numeric_limits<int>::max();
    }
    return std::max(1, static_cast<int>(wholeTicks));
}

PathMovementMode runtimeObjectMovementMode(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    if (unit == nullptr) {
        return PathMovementMode::Ground;
    }

    if (!runtimeMovementClassificationReference()) return runtimeUnitMovementModeProfile(*unit);

    const std::string speedType = toLowerAsciiLocal(unit->speedType);
    const std::string movementZone = toLowerAsciiLocal(unit->movementZone);
    if (runtimeUnitUsesAirMovement(*unit)) {
        return PathMovementMode::Air;
    }
    // Hovercraft are deliberately checked before Naval/Water so the landing
    // craft remains the stock exception that can cross both domains.
    if (speedType.find("amphib") != std::string::npos
        || speedType.find("hover") != std::string::npos
        || movementZone.find("amphib") != std::string::npos
        || movementZone.find("hover") != std::string::npos) {
        return PathMovementMode::Amphibious;
    }
    if (unit->naval
        || unit->waterBound
        || speedType.find("float") != std::string::npos
        || speedType.find("water") != std::string::npos
        || movementZone.find("water") != std::string::npos) {
        return PathMovementMode::Naval;
    }
    return PathMovementMode::Ground;
}

bool runtimeOverlayLooksBridge(const TerrainCell& terrain) {
    if (!terrain.hasOverlay) {
        return false;
    }

    if (terrain.overlayIndex == 24
        || terrain.overlayIndex == 25
        || terrain.overlayIndex == 237
        || terrain.overlayIndex == 238
        || (terrain.overlayIndex >= 74 && terrain.overlayIndex <= 101)
        || (terrain.overlayIndex >= 205 && terrain.overlayIndex <= 232)) {
        return true;
    }

    const std::string overlayName = toLowerAsciiLocal(
        terrain.overlayType + " " + terrain.overlayImage
    );
    return overlayName.find("bridge") != std::string::npos
        || overlayName.find("bridg") != std::string::npos
        || overlayName.find("lobrdg") != std::string::npos
        || overlayName.find("lobrdb") != std::string::npos;
}

bool runtimeOverlayIsBrokenLowBridgePlaceholder(
    const TerrainCell& terrain) {
    if (!terrain.hasOverlay) {
        return false;
    }
    if (terrain.overlayIndex == 100
        || terrain.overlayIndex == 101
        || terrain.overlayIndex == 231
        || terrain.overlayIndex == 232) {
        return true;
    }

    const std::string overlayName = toLowerAsciiLocal(
        terrain.overlayType + " " + terrain.overlayImage
    );
    return overlayName.find("lobrdg27") != std::string::npos
        || overlayName.find("lobrdg28") != std::string::npos
        || overlayName.find("lobrdb27") != std::string::npos
        || overlayName.find("lobrdb28") != std::string::npos;
}

bool runtimeOverlayLooksHighBridge(const TerrainCell& terrain) {
    if (!terrain.hasOverlay
        || runtimeOverlayIsBrokenLowBridgePlaceholder(terrain)) {
        return false;
    }
    if (terrain.overlayIndex == 24
        || terrain.overlayIndex == 25
        || terrain.overlayIndex == 237
        || terrain.overlayIndex == 238) {
        return true;
    }

    const std::string overlayName = toLowerAsciiLocal(
        terrain.overlayType + " " + terrain.overlayImage
    );
    return (overlayName.find("bridge") != std::string::npos
            || overlayName.find("bridg") != std::string::npos)
        && overlayName.find("lobrdg") == std::string::npos
        && overlayName.find("lobrdb") == std::string::npos;
}

int runtimeEffectiveTerrainLevel(const TerrainCell& terrain) {
    // High bridge overlays are elevated four leptonic height steps above the
    // underlying water tile. Low bridges and theater bridge tiles already use
    // their terrain level directly.
    return terrain.level + (runtimeOverlayLooksHighBridge(terrain) ? 4 : 0);
}

bool runtimeHighBridgeFrameIsDisconnected(const TerrainCell& terrain) {
    if (!runtimeOverlayLooksHighBridge(terrain)) {
        return false;
    }
    return terrain.overlayFrame == 7
        || terrain.overlayFrame == 8
        || terrain.overlayFrame == 16
        || terrain.overlayFrame == 17;
}

bool runtimeOverlayProvidesBridgeDeck(const TerrainCell& terrain) {
    return runtimeOverlayLooksBridge(terrain)
        && !runtimeHighBridgeFrameIsDisconnected(terrain)
        && !runtimeOverlayIsBrokenLowBridgePlaceholder(terrain);
}

RuntimeTerrainInfo runtimeTerrainInfoForCell(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain) {
    RuntimeTerrainInfo info;

    // Bridge overlays are the playable deck. Some original maps deliberately
    // leave the underlying IsoMapPack tile empty, so the overlay must be
    // resolved before the invalid-tile guard.
    if (runtimeOverlayProvidesBridgeDeck(terrain)) {
        info.kind = RuntimeTerrainKind::Bridge;
        info.bridge = true;
        info.cost = 9;
        return info;
    }

    // An authored clear sentinel uses the same canonical Clear0 as its visual surface.
    // Keep real broken bridge overlays and negative/missing terrain blocked.
    if (terrain.tileIndex == 65535 && state.theaterLoaded
        && !runtimeHighBridgeFrameIsDisconnected(terrain)
        && !runtimeOverlayIsBrokenLowBridgePlaceholder(terrain)) {
        TerrainCell clear = terrain;
        clear.tileIndex = 0;
        clear.subTileIndex = 0;
        return runtimeTerrainInfoForCell(state, clear);
    }

    if (terrain.tileIndex < 0 || terrain.tileIndex == 65535) {
        info.kind = RuntimeTerrainKind::Blocked;
        info.passable = false;
        info.cost = std::numeric_limits<int>::max() / 4;
        return info;
    }

    if (!state.theaterLoaded) {
        return info;
    }

    const TheaterTileReference reference =
        state.theater.resolveTileIndex(terrain.tileIndex);
    const int tileSetIndex = reference.tileSetIndex;
    const std::string setName = toLowerAsciiLocal(reference.tileSetName);
    const std::string fileName = toLowerAsciiLocal(reference.baseFileName);
    const std::string tileName = setName + " " + fileName;

    const bool bridge =
        runtimeTileSetMatches(state.theater, tileSetIndex, "WaterBridge")
        || (
            runtimeTextContains(tileName, "bridge")
            && !runtimeTextContains(tileName, "cliff")
        );
    const bool shore =
        runtimeTileSetMatches(state.theater, tileSetIndex, "ShorePieces")
        || runtimeTextContains(tileName, "shore")
        || runtimeTextContains(tileName, "beach");
    const bool water =
        runtimeTileSetMatches(state.theater, tileSetIndex, "WaterSet")
        || runtimeTextContains(tileName, "water")
        || runtimeTextContains(tileName, "watr")
        || runtimeTextContains(tileName, "wclif");
    const bool ramp =
        runtimeTextContains(tileName, "ramp")
        || runtimeTextContains(tileName, "slope")
        || runtimeTextContains(tileName, "cliff ramp")
        || runtimeTextContains(tileName, "cramp");
    const bool cliff =
        runtimeTileSetMatches(state.theater, tileSetIndex, "CrystalCliff")
        || runtimeTextContains(tileName, "cliff")
        || runtimeTextContains(tileName, "dclif")
        || runtimeTextContains(tileName, "wclif");
    const bool black =
        runtimeTileSetMatches(state.theater, tileSetIndex, "BlackTile")
        || runtimeTextContains(tileName, "black");
    const bool road =
        runtimeTileSetMatches(state.theater, tileSetIndex, "PavedRoads")
        || runtimeTileSetMatches(state.theater, tileSetIndex, "PaveTile")
        || runtimeTileSetMatches(state.theater, tileSetIndex, "Medians")
        || runtimeTileSetMatches(state.theater, tileSetIndex, "MiscPaveTile")
        || runtimeTextContains(tileName, "road")
        || runtimeTextContains(tileName, "pave")
        || runtimeTextContains(tileName, "plat");

    if (black) {
        info.kind = RuntimeTerrainKind::Blocked;
        info.passable = false;
        info.cost = std::numeric_limits<int>::max() / 4;
        return info;
    }

    if (bridge) {
        info.kind = RuntimeTerrainKind::Bridge;
        info.bridge = true;
        info.cost = road ? 7 : 9;
        return info;
    }

    if (shore) {
        info.kind = RuntimeTerrainKind::Shore;
        info.shore = true;
        info.water = water;
        info.cost = 14;
        return info;
    }

    if (water) {
        info.kind = RuntimeTerrainKind::Water;
        info.water = true;
        info.cost = 18;
        return info;
    }

    if (cliff && !ramp) {
        info.kind = RuntimeTerrainKind::Cliff;
        info.cliff = true;
        info.passable = false;
        info.cost = std::numeric_limits<int>::max() / 4;
        return info;
    }

    if (ramp) {
        info.kind = RuntimeTerrainKind::Ramp;
        info.ramp = true;
        info.cost = 18;
        return info;
    }

    info.kind = RuntimeTerrainKind::Land;
    info.cost = road ? 8 : 10;
    return info;
}

RuntimeTerrainInfo runtimeCachedTerrainInfoForCell(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain) {
    const auto found =
        state.terrainInfoByCell.find(runtimeCellKey(terrain.x, terrain.y));
    if (found != state.terrainInfoByCell.end()) {
        return found->second;
    }
    return runtimeTerrainInfoForCell(state, terrain);
}

RuntimeMapRenderContext runtimeTerrainMetadataContext(
    const RuntimeGameSessionState& state) {
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.assetRootPath = runtimeAssetRootPath(state.config);
    return context;
}

bool runtimeCellHasDisconnectedHighBridgeDeck(
    const RuntimeGameSessionState& state, const TerrainCell& terrain) {
    // A native bank TMP can overlap the first body segment. Its set name
    // must not restore a deck after that segment's original SHP is broken.
    for (int lane = 0; lane < 3; ++lane) {
        const auto alongX = state.terrainByCell.find(
            runtimeCellKey(terrain.x, terrain.y - lane));
        if (alongX != state.terrainByCell.end() && alongX->second
            && runtimeHighBridgeFrameIsDisconnected(*alongX->second)
            && (alongX->second->overlayFrame == 7
                || alongX->second->overlayFrame == 8)) return true;
        const auto alongY = state.terrainByCell.find(
            runtimeCellKey(terrain.x - lane, terrain.y));
        if (alongY != state.terrainByCell.end() && alongY->second
            && runtimeHighBridgeFrameIsDisconnected(*alongY->second)
            && (alongY->second->overlayFrame == 16
                || alongY->second->overlayFrame == 17)) return true;
    }
    return false;
}

RuntimeNavigationTerrainInfo runtimeOwnedNavigationTerrainInfo(const RuntimeTerrainInfo& source) {
    RuntimeNavigationTerrainInfo result;
    result.movementLandType = source.movementLandType;
    result.movementRampType = source.movementRampType;
    result.kind = source.kind;
    result.authoredLandType = source.authoredLandType;
    result.passable = source.passable;
    result.water = source.water;
    result.shore = source.shore;
    result.cliff = source.cliff;
    result.ramp = source.ramp;
    result.bridge = source.bridge;
    result.cost = source.cost;
    return result;
}

RuntimeTerrainInfo runtimePrivateNavigationTerrainInfo(const RuntimeNavigationTerrainInfo& source) {
    RuntimeTerrainInfo result;
    result.movementLandType = source.movementLandType;
    result.movementRampType = source.movementRampType;
    result.kind = source.kind;
    result.authoredLandType = source.authoredLandType;
    result.passable = source.passable;
    result.water = source.water;
    result.shore = source.shore;
    result.cliff = source.cliff;
    result.ramp = source.ramp;
    result.bridge = source.bridge;
    result.cost = source.cost;
    return result;
}

RuntimeTerrainInfo runtimeNavigationTerrainInfoForCell(
    RuntimeProductionHostState& state,
    const TerrainCell& terrain,
    const RuntimeMapRenderContext& metadataContext) {
    RuntimeNavigationTerrainRendererAdapter adapter(state, metadataContext);
    return runtimePrivateNavigationTerrainInfo(queryRuntimeNavigationTerrainInfo(adapter, terrain));
}

RuntimeTerrainInfo runtimeNavigationTerrainInfoForCell(
    RuntimeProductionHostState& state,
    const TerrainCell& terrain) {
    const RuntimeMapRenderContext metadataContext =
        runtimeTerrainMetadataContext(state);
    return runtimeNavigationTerrainInfoForCell(
        state,
        terrain,
        metadataContext
    );
}

void rebuildRuntimeTerrainInfoCache(RuntimeProductionHostState& state) {
    const RuntimeMapRenderContext metadataContext =
        runtimeTerrainMetadataContext(state);
    state.terrainInfoByCell.clear();
    state.terrainInfoByCell.reserve(state.world.getTerrainCells().size());
    for (const TerrainCell& terrain : state.world.getTerrainCells()) {
        state.terrainInfoByCell.emplace(
            runtimeCellKey(terrain.x, terrain.y),
            runtimeNavigationTerrainInfoForCell(
                state,
                terrain,
                metadataContext
            )
        );
    }
}

const char* runtimeTerrainLandTypeRulesId(
    RuntimeTerrainLandType landType) {
    switch (landType) {
    case RuntimeTerrainLandType::Clear:
        return "Clear";
    case RuntimeTerrainLandType::Road:
        return "Road";
    case RuntimeTerrainLandType::Water:
        return "Water";
    case RuntimeTerrainLandType::Rock:
        return "Rock";
    case RuntimeTerrainLandType::Wall:
        return "Wall";
    case RuntimeTerrainLandType::Tiberium:
        return "Tiberium";
    case RuntimeTerrainLandType::Beach:
        return "Beach";
    case RuntimeTerrainLandType::Rough:
        return "Rough";
    case RuntimeTerrainLandType::Ice:
        return "Ice";
    case RuntimeTerrainLandType::Railroad:
        return "Railroad";
    case RuntimeTerrainLandType::Tunnel:
        return "Tunnel";
    case RuntimeTerrainLandType::Unknown:
        break;
    }
    return nullptr;
}

bool runtimeObjectUsesTerrainMovementSpeed(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (object.kind != WorldObjectKind::Unit || object.airborne) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!type) return false;
    const auto loco = toLowerAsciiLocal(trimAsciiLocal(type->locomotor));
    if (!loco.empty()) return loco == "{4a582741-9839-11d1-b709-00a024ddafd1}"
        || loco == "{2bea74e1-7cca-11d3-be14-00104b62a16c}";
    // Sparse runtime fixtures and old custom rules omit the default Drive GUID.
    return !type->organic && !type->teleporter && !runtimeUnitUsesAirMovement(*type)
        && toLowerAsciiLocal(type->speedType) != "hover";
}

double runtimeMovementLandPercent(const LandType& land, const UnitType& type) {
    const auto speed = toLowerAsciiLocal(trimAsciiLocal(type.speedType));
    if (speed == "track") return land.track;
    if (speed == "foot") return land.foot;
    if (speed == "wheel") return land.wheel;
    if (speed == "hover") return land.hover;
    if (speed == "float") return land.floatSpeed;
    if (speed == "floatbeach") return land.floatBeach;
    if (speed == "amphibious") return land.amphibious;
    if (speed == "winged") return 100.0;
    // UnitTypeClass::Read_INI (0x7476D3): absent SpeedType defaults to
    // Track for Crusher=yes, otherwise Wheel (e.g. the stock IFV).
    return type.crusher ? land.track : land.wheel;
}

double runtimeMovementCellFloorLevel(const TerrainCell& cell,
    const RuntimeTerrainInfo& info) {
    // CellClass::GetFloorHeight (0x47B3A0), evaluated at the cell centre.
    // The raw ground height is queried even beneath a high bridge.
    const unsigned ramp = info.movementRampType;
    const double centre = (ramp >= 1 && ramp <= 4) || (ramp >= 17 && ramp <= 20)
        ? 0.5 : ramp >= 9 && ramp <= 16 ? 1.0 : 0.0;
    return cell.level + centre;
}

double runtimeTerrainMovementSpeedMultiplier(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeCellPoint& from,
    const RuntimeCellPoint& to) {
    if (!runtimeObjectUsesTerrainMovementSpeed(state, object)) return 1.0;
    const auto* type = findRuntimeUnitType(state.rules, object);
    const auto destination = state.terrainByCell.find(runtimeCellKey(to.x, to.y));
    if (destination == state.terrainByCell.end() || !destination->second) return 1.0;
    const auto& cell = *destination->second;
    const auto info = runtimeCachedTerrainInfoForCell(state, cell);
    auto land = info.movementLandType;
    if (land == RuntimeTerrainLandType::Unknown) land = info.authoredLandType;
    const bool ship = toLowerAsciiLocal(trimAsciiLocal(type->locomotor))
        == "{2bea74e1-7cca-11d3-be14-00104b62a16c}"
        || runtimeObjectMovementMode(state, object) == PathMovementMode::Naval;
    if (info.bridge && !ship) land = RuntimeTerrainLandType::Road;
    else if (cell.hasOverlay && (cell.overlayIsOre || cell.overlayIsGem))
        land = RuntimeTerrainLandType::Tiberium;
    if (land == RuntimeTerrainLandType::Unknown) {
        land = info.water || (info.bridge && ship) ? RuntimeTerrainLandType::Water
            : info.shore ? RuntimeTerrainLandType::Beach : RuntimeTerrainLandType::Clear;
    }
    const char* id = runtimeTerrainLandTypeRulesId(land);
    const auto* authored = id ? state.rules.findLandType(id) : nullptr;
    double multiplier = authored ? runtimeMovementLandPercent(*authored, *type) / 100.0 : 1.0;
    multiplier = std::min(1.0, multiplier);
    const auto source = state.terrainByCell.find(runtimeCellKey(from.x, from.y));
    if (source != state.terrainByCell.end() && source->second && from != to) {
        const double difference = runtimeMovementCellFloorLevel(cell, info)
            - runtimeMovementCellFloorLevel(*source->second,
                runtimeCachedTerrainInfoForCell(state, *source->second));
        const auto& slopes = state.rules.getTerrainMovementRules();
        const auto speed = toLowerAsciiLocal(trimAsciiLocal(type->speedType));
        const bool tracked = speed.empty() ? type->crusher : speed == "track";
        if (difference > 0.0) multiplier *= tracked ? slopes.trackedUphill : slopes.wheeledUphill;
        else if (difference < 0.0) multiplier *= tracked ? slopes.trackedDownhill : slopes.wheeledDownhill;
    }
    // Normally a zero land speed is rejected by navigation. Retail still uses
    // 0.5 for a zero product on a committed/forced step (including a zero slope).
    if (multiplier == 0.0) multiplier = 0.5;
    if (object.health <= runtimeMaxHealthForObject(state, object)
            * state.rules.getSpecialMechanismRules().conditionYellow) multiplier *= 0.75;
    // Drive/Ship store their target percentage before passing it through
    // FootClass::SetSpeedPercentage, which caps the final value at 1.0.
    return std::isfinite(multiplier) ? std::clamp(multiplier, 0.01, 1.0) : 1.0;
}

bool runtimeAuthoredLandTypeAllowsMover(const RuntimeGameSessionState& state,
    const RuntimeTerrainInfo& terrain,const WorldObjectInstance& mover) {
    auto* scope=runtimeActivePlannerLandPermission;
    if (scope) {
        if (const auto answer=scope->query(state,terrain,mover)) return *answer;
    }
    // No scope: no reference lookup, key construction, allocation or hash.
    return runtimeAuthoredLandTypeAllowsMoverUncached(state,terrain,mover);
}

bool runtimeVehicleCannotSurviveOpenWaterAtCell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, RuntimeCellPoint cell) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    if (object.kind != WorldObjectKind::Unit || object.airborne || !unit
        || unit->naval || unit->underwater || unit->organic) return false;
    const auto found = state.terrainByCell.find(runtimeCellKey(cell.x, cell.y));
    const TerrainCell* terrain = found == state.terrainByCell.end() ? nullptr : found->second;
    if (!terrain) for (const auto& candidate : state.world.getTerrainCells()) {
        if (candidate.x == cell.x && candidate.y == cell.y) { terrain = &candidate; break; }
    }
    if (!terrain) return false;
    const auto info = runtimeCachedTerrainInfoForCell(state, *terrain);
    return info.water && !info.bridge && !runtimeAuthoredLandOrdinalAllowsMoverUncached(
        state, RuntimeTerrainLandType::Water, object);
}

std::optional<RuntimeBridgeMaterial> runtimeHighBridgeMaterialForTerrain(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain) {
    if (!state.theaterLoaded || terrain.tileIndex < 0
        || terrain.tileIndex == 65535) {
        return std::nullopt;
    }

    const TheaterTileReference reference =
        state.theater.resolveTileIndex(terrain.tileIndex);
    if (!reference.validTileSet) {
        return std::nullopt;
    }
    if (reference.tileSetIndex
        == state.theater.getGeneralTileSetIndex("BridgeSet")) {
        return RuntimeBridgeMaterial::Concrete;
    }
    if (reference.tileSetIndex
        == state.theater.getGeneralTileSetIndex("WoodBridgeSet")) {
        return RuntimeBridgeMaterial::Wood;
    }
    return std::nullopt;
}

RuntimeHighBridgeHeadType runtimeHighBridgeHeadTypeForTerrain(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain) {
    if (!runtimeHighBridgeMaterialForTerrain(state, terrain).has_value()) {
        return RuntimeHighBridgeHeadType::None;
    }

    const TheaterTileReference reference =
        state.theater.resolveTileIndex(terrain.tileIndex);
    const int ordinal = reference.tileIndexInSet + 1;
    if (runtimeTheaterGeneralValueMatches(
            state,
            ordinal,
            {"BridgeTopLeft1", "BridgeTopLeft2"}
        )) {
        return RuntimeHighBridgeHeadType::TopLeft;
    }
    if (runtimeTheaterGeneralValueMatches(
            state,
            ordinal,
            {"BridgeBottomRight1", "BridgeBottomRight2"}
        )) {
        return RuntimeHighBridgeHeadType::BottomRight;
    }
    if (runtimeTheaterGeneralValueMatches(
            state,
            ordinal,
            {"BridgeTopRight1", "BridgeTopRight2"}
        )) {
        return RuntimeHighBridgeHeadType::TopRight;
    }
    if (runtimeTheaterGeneralValueMatches(
            state,
            ordinal,
            {"BridgeBottomLeft1", "BridgeBottomLeft2"}
        )) {
        return RuntimeHighBridgeHeadType::BottomLeft;
    }
    if (runtimeTheaterGeneralValueMatches(
            state,
            ordinal,
            {"BridgeMiddle1"}
        )) {
        return RuntimeHighBridgeHeadType::MiddleTopLeftBottomRight;
    }
    if (runtimeTheaterGeneralValueMatches(
            state,
            ordinal,
            {"BridgeMiddle2"}
        )) {
        return RuntimeHighBridgeHeadType::MiddleTopRightBottomLeft;
    }
    return RuntimeHighBridgeHeadType::None;
}

bool runtimeHighBridgeHeadIsBoundary(RuntimeHighBridgeHeadType head) {
    return head == RuntimeHighBridgeHeadType::TopLeft
        || head == RuntimeHighBridgeHeadType::BottomRight
        || head == RuntimeHighBridgeHeadType::TopRight
        || head == RuntimeHighBridgeHeadType::BottomLeft;
}

RuntimeHighBridgeHeadType runtimeOppositeHighBridgeHead(
    RuntimeHighBridgeHeadType head) {
    switch (head) {
    case RuntimeHighBridgeHeadType::TopLeft:
        return RuntimeHighBridgeHeadType::BottomRight;
    case RuntimeHighBridgeHeadType::BottomRight:
        return RuntimeHighBridgeHeadType::TopLeft;
    case RuntimeHighBridgeHeadType::TopRight:
        return RuntimeHighBridgeHeadType::BottomLeft;
    case RuntimeHighBridgeHeadType::BottomLeft:
        return RuntimeHighBridgeHeadType::TopRight;
    default:
        return RuntimeHighBridgeHeadType::None;
    }
}

RuntimeCellPoint runtimeHighBridgeDirection(RuntimeHighBridgeHeadType head) {
    switch (head) {
    case RuntimeHighBridgeHeadType::TopLeft:
        return {1, 0};
    case RuntimeHighBridgeHeadType::BottomRight:
        return {-1, 0};
    case RuntimeHighBridgeHeadType::TopRight:
        return {0, 1};
    case RuntimeHighBridgeHeadType::BottomLeft:
        return {0, -1};
    default:
        return {0, 0};
    }
}

RuntimeBridgeMaterial runtimeHighBridgeMaterialForOverlay(
    const TerrainCell& terrain) {
    const std::string token = toLowerAsciiLocal(
        terrain.overlayType + " " + terrain.overlayImage
    );
    return terrain.overlayIndex == 237
            || terrain.overlayIndex == 238
            || token.find("bridgb") != std::string::npos
        ? RuntimeBridgeMaterial::Wood
        : RuntimeBridgeMaterial::Concrete;
}

bool runtimeHighBridgeOverlayMatchesAxis(
    const TerrainCell& terrain,
    bool alongX) {
    if (!runtimeOverlayLooksHighBridge(terrain)) {
        return false;
    }
    if (terrain.overlayIndex == 24 || terrain.overlayIndex == 237) {
        return alongX;
    }
    if (terrain.overlayIndex == 25 || terrain.overlayIndex == 238) {
        return !alongX;
    }

    const std::string token = toLowerAsciiLocal(
        terrain.overlayType + " " + terrain.overlayImage
    );
    if (token.find("bridge1") != std::string::npos
        || token.find("bridgb1") != std::string::npos) {
        return alongX;
    }
    if (token.find("bridge2") != std::string::npos
        || token.find("bridgb2") != std::string::npos) {
        return !alongX;
    }
    return true;
}

RuntimeCellPoint runtimeCanonicalHighBridgeBoundary(
    const RuntimeGameSessionState& state,
    const std::vector<RuntimeCellPoint>& cluster,
    const RuntimeCellPoint& direction,
    const RuntimeCellPoint& fallback,
    int expectedLevel) {
    std::vector<RuntimeCellPoint> levelEdge;
    levelEdge.reserve(cluster.size());
    for (const RuntimeCellPoint& point : cluster) {
        const auto terrainIt = state.terrainByCell.find(
            runtimeCellKey(point.x, point.y)
        );
        if (terrainIt != state.terrainByCell.end()
            && terrainIt->second != nullptr
            && terrainIt->second->level == expectedLevel) {
            levelEdge.push_back(point);
        }
    }
    const std::vector<RuntimeCellPoint>& candidates =
        levelEdge.empty() ? cluster : levelEdge;

    int bestForward = std::numeric_limits<int>::min();
    for (const RuntimeCellPoint& point : candidates) {
        const int forward =
            (point.x * direction.x) + (point.y * direction.y);
        bestForward = std::max(bestForward, forward);
    }

    std::vector<RuntimeCellPoint> frontEdge;
    for (const RuntimeCellPoint& point : candidates) {
        const int forward =
            (point.x * direction.x) + (point.y * direction.y);
        if (forward == bestForward) {
            frontEdge.push_back(point);
        }
    }
    if (frontEdge.empty()) {
        return fallback;
    }
    std::sort(
        frontEdge.begin(),
        frontEdge.end(),
        [&direction](
            const RuntimeCellPoint& left,
            const RuntimeCellPoint& right
        ) {
            return direction.x != 0
                ? left.y < right.y
                : left.x < right.x;
        }
    );
    return frontEdge[frontEdge.size() / 2U];
}

bool runtimeTerrainIsHighBridgeMiddle(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain) {
    const RuntimeHighBridgeHeadType head =
        runtimeHighBridgeHeadTypeForTerrain(state, terrain);
    return head == RuntimeHighBridgeHeadType::MiddleTopLeftBottomRight
        || head == RuntimeHighBridgeHeadType::MiddleTopRightBottomLeft;
}

std::optional<RuntimeBridgeRepairSpan>
runtimeBuildHighBridgeRepairSpanFromBoundary(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& hut,
    const TerrainCell& boundarySeed) {
    const RuntimeHighBridgeHeadType startHead =
        runtimeHighBridgeHeadTypeForTerrain(state, boundarySeed);
    if (!runtimeHighBridgeHeadIsBoundary(startHead)) {
        return std::nullopt;
    }
    const std::optional<RuntimeBridgeMaterial> material =
        runtimeHighBridgeMaterialForTerrain(state, boundarySeed);
    if (!material.has_value()) {
        return std::nullopt;
    }

    const RuntimeCellPoint direction = runtimeHighBridgeDirection(startHead);
    const bool alongX = direction.x != 0;
    const int bridgeLevel = boundarySeed.level;
    const auto startCluster = runtimeSameTileCluster(state, boundarySeed);
    if (startCluster.empty()) return std::nullopt;
    const RuntimeCellPoint startBoundary =
        runtimeCanonicalHighBridgeBoundary(
            state,
            startCluster,
            direction,
            {boundarySeed.x, boundarySeed.y},
            bridgeLevel
        );

    const RuntimeHighBridgeHeadType oppositeHead =
        runtimeOppositeHighBridgeHead(startHead);
    RuntimeCellPoint endHead;
    int endDistance = -1;
    constexpr int maxBridgeLength = 100;
    for (int step = 1; step <= maxBridgeLength; ++step) {
        const RuntimeCellPoint point{
            startBoundary.x + direction.x * step,
            startBoundary.y + direction.y * step
        };
        const auto terrainIt =
            state.terrainByCell.find(runtimeCellKey(point.x, point.y));
        if (terrainIt == state.terrainByCell.end()
            || terrainIt->second == nullptr) {
            return std::nullopt;
        }
        const TerrainCell& terrain = *terrainIt->second;
        if (runtimeHighBridgeHeadTypeForTerrain(state, terrain)
                == oppositeHead
            && runtimeHighBridgeMaterialForTerrain(state, terrain)
                == material) {
            const auto endCluster = runtimeSameTileCluster(state, terrain);
            if (endCluster.empty()) return std::nullopt;
            endHead = runtimeCanonicalHighBridgeBoundary(
                state,
                endCluster,
                runtimeHighBridgeDirection(oppositeHead),
                point,
                bridgeLevel
            );
            endDistance =
                ((endHead.x - startBoundary.x) * direction.x)
                + ((endHead.y - startBoundary.y) * direction.y);
            break;
        }
    }
    if (endDistance <= 1 || endDistance > maxBridgeLength) {
        return std::nullopt;
    }

    std::vector<RuntimeBridgeRepairCell> spanCells;
    spanCells.reserve(static_cast<std::size_t>(endDistance - 1));
    for (int step = 1; step < endDistance; ++step) {
        const RuntimeCellPoint point{
            startBoundary.x + direction.x * step,
            startBoundary.y + direction.y * step
        };
        const auto terrainIt =
            state.terrainByCell.find(runtimeCellKey(point.x, point.y));
        if (terrainIt == state.terrainByCell.end()
            || terrainIt->second == nullptr) {
            return std::nullopt;
        }
        RuntimeBridgeRepairCell repairCell;
        repairCell.point = point;
        repairCell.requiresOverlay =
            !runtimeTerrainIsHighBridgeMiddle(state, *terrainIt->second);
        repairCell.expectedFrame = alongX ? 0 : 9;
        spanCells.push_back(repairCell);
    }

    RuntimeBridgeRepairSpan span;
    span.hutObjectId = hut.instanceId;
    span.high = true;
    span.alongX = alongX;
    span.material = *material;
    span.startHead = startBoundary;
    span.endHead = endHead;
    span.level = bridgeLevel;
    span.overlayIndex =
        *material == RuntimeBridgeMaterial::Concrete
            ? (alongX ? 24 : 25)
            : (alongX ? 237 : 238);
    span.intactFrame = alongX ? 0 : 9;
    if (*material == RuntimeBridgeMaterial::Concrete) {
        span.overlayType = alongX ? "BRIDGE1" : "BRIDGE2";
        span.overlayImage = "BRIDGE";
    } else {
        span.overlayType = alongX ? "BRIDGEB1" : "BRIDGEB2";
        span.overlayImage = "BRIDGB";
    }
    span.cells = std::move(spanCells);
    return span;
}

bool runtimeOverlayLooksVisibleLowBridge(const TerrainCell& terrain) {
    return terrain.hasOverlay
        && ((terrain.overlayIndex >= 74 && terrain.overlayIndex <= 99)
            || (terrain.overlayIndex >= 205
                && terrain.overlayIndex <= 230));
}

std::optional<RuntimeBridgeMaterial> runtimeLowBridgeMaterialForOverlay(
    const TerrainCell& terrain) {
    if (!terrain.hasOverlay) {
        return std::nullopt;
    }
    if (terrain.overlayIndex >= 74 && terrain.overlayIndex <= 101) {
        return RuntimeBridgeMaterial::Wood;
    }
    if (terrain.overlayIndex >= 205 && terrain.overlayIndex <= 232) {
        return RuntimeBridgeMaterial::Concrete;
    }
    return std::nullopt;
}

int runtimeLowBridgeBaseIndex(RuntimeBridgeMaterial material) {
    return material == RuntimeBridgeMaterial::Wood ? 74 : 205;
}

int runtimeLowBridgeOffset(const TerrainCell& terrain) {
    const std::optional<RuntimeBridgeMaterial> material =
        runtimeLowBridgeMaterialForOverlay(terrain);
    return material.has_value()
        ? terrain.overlayIndex - runtimeLowBridgeBaseIndex(*material)
        : -1;
}

bool runtimeLowBridgeOverlayIsAlongX(const TerrainCell& terrain) {
    const int offset = runtimeLowBridgeOffset(terrain);
    return (offset >= 0 && offset <= 8)
        || (offset >= 18 && offset <= 21)
        || offset == 26;
}

bool runtimeLowBridgeOverlayIsHead(const TerrainCell& terrain) {
    if (!runtimeOverlayLooksVisibleLowBridge(terrain)) {
        return false;
    }
    const int offset = runtimeLowBridgeOffset(terrain);
    return offset >= 18 && offset <= 25;
}

bool runtimeLowBridgeOverlayIsStartHead(const TerrainCell& terrain) {
    const int offset = runtimeLowBridgeOffset(terrain);
    return runtimeLowBridgeOverlayIsHead(terrain)
        && offset >= 20 && offset <= 23;
}

bool runtimeLowBridgeOverlayMatches(
    const TerrainCell& terrain,
    RuntimeBridgeMaterial material,
    bool alongX,
    bool allowPlaceholder) {
    const std::optional<RuntimeBridgeMaterial> actualMaterial =
        runtimeLowBridgeMaterialForOverlay(terrain);
    if (!actualMaterial.has_value() || *actualMaterial != material) {
        return false;
    }
    if (!runtimeOverlayLooksVisibleLowBridge(terrain)
        && !(allowPlaceholder
            && runtimeOverlayIsBrokenLowBridgePlaceholder(terrain))) {
        return false;
    }
    return runtimeLowBridgeOverlayIsAlongX(terrain) == alongX;
}

std::string runtimeLowBridgeOverlayName(
    RuntimeBridgeMaterial material,
    int offset) {
    std::ostringstream name;
    name << (material == RuntimeBridgeMaterial::Wood ? "LOBRDG" : "LOBRDB")
         << std::setw(2)
         << std::setfill('0')
         << (offset + 1);
    return name.str();
}

std::optional<RuntimeBridgeRepairSpan>
runtimeBuildLowBridgeRepairSpanFromHead(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& hut,
    const TerrainCell& headTerrain) {
    if (headTerrain.overlayFrame != 0
        || !runtimeLowBridgeOverlayIsHead(headTerrain)) {
        return std::nullopt;
    }
    const std::optional<RuntimeBridgeMaterial> material =
        runtimeLowBridgeMaterialForOverlay(headTerrain);
    if (!material.has_value()) {
        return std::nullopt;
    }

    const bool alongX = runtimeLowBridgeOverlayIsAlongX(headTerrain);
    const bool startHead = runtimeLowBridgeOverlayIsStartHead(headTerrain);
    const RuntimeCellPoint direction{
        alongX ? (startHead ? 1 : -1) : 0,
        alongX ? 0 : (startHead ? 1 : -1)
    };
    const RuntimeCellPoint start{headTerrain.x, headTerrain.y};
    RuntimeCellPoint end;
    int endDistance = -1;
    constexpr int maxBridgeLength = 100;
    for (int step = 1; step <= maxBridgeLength; ++step) {
        const RuntimeCellPoint point{
            start.x + direction.x * step,
            start.y + direction.y * step
        };
        const auto terrainIt =
            state.terrainByCell.find(runtimeCellKey(point.x, point.y));
        if (terrainIt == state.terrainByCell.end()
            || terrainIt->second == nullptr) {
            return std::nullopt;
        }
        const TerrainCell& terrain = *terrainIt->second;
        if (runtimeOverlayLooksVisibleLowBridge(terrain)) {
            if (!runtimeLowBridgeOverlayMatches(
                    terrain,
                    *material,
                    alongX,
                    false
                )) {
                return std::nullopt;
            }
            if (runtimeLowBridgeOverlayIsHead(terrain)) {
                if (runtimeLowBridgeOverlayIsStartHead(terrain)
                    == startHead) {
                    return std::nullopt;
                }
                end = point;
                endDistance = step;
                break;
            }
        } else if (runtimeOverlayIsBrokenLowBridgePlaceholder(terrain)) {
            if (!runtimeLowBridgeOverlayMatches(
                    terrain,
                    *material,
                    alongX,
                    true
                )) {
                return std::nullopt;
            }
        } else if (terrain.hasOverlay) {
            return std::nullopt;
        }
    }
    if (endDistance <= 1) {
        return std::nullopt;
    }

    std::vector<RuntimeBridgeRepairCell> cells;
    cells.reserve(static_cast<std::size_t>((endDistance + 1) * 3));
    for (int step = 0; step <= endDistance; ++step) {
        const RuntimeCellPoint anchor{
            start.x + direction.x * step,
            start.y + direction.y * step
        };
        for (int lane = 0; lane < 3; ++lane) {
            const RuntimeCellPoint point{
                anchor.x + (alongX ? 0 : lane),
                anchor.y + (alongX ? lane : 0)
            };
            const auto terrainIt =
                state.terrainByCell.find(runtimeCellKey(point.x, point.y));
            if (terrainIt == state.terrainByCell.end()
                || terrainIt->second == nullptr) {
                return std::nullopt;
            }
            RuntimeBridgeRepairCell cell;
            cell.point = point;
            cell.requiresOverlay = step > 0 && step < endDistance;
            cell.expectedFrame = lane;
            cells.push_back(cell);
        }
    }

    RuntimeBridgeRepairSpan span;
    span.hutObjectId = hut.instanceId;
    span.high = false;
    span.alongX = alongX;
    span.material = *material;
    span.startHead = start;
    span.endHead = end;
    span.level = headTerrain.level;
    const int normalOffset = alongX ? 0 : 9;
    span.overlayIndex = runtimeLowBridgeBaseIndex(*material) + normalOffset;
    span.intactFrame = 0;
    span.overlayType = runtimeLowBridgeOverlayName(*material, normalOffset);
    span.overlayImage = span.overlayType;
    span.cells = std::move(cells);
    return span;
}

bool runtimeBridgeRepairSpanNeedsRepair(
    const RuntimeGameSessionState& state,
    const RuntimeBridgeRepairSpan& span) {
    for (const RuntimeBridgeRepairCell& repairCell : span.cells) {
        if (!repairCell.requiresOverlay) {
            continue;
        }
        const auto terrainIt = state.terrainByCell.find(
            runtimeCellKey(repairCell.point.x, repairCell.point.y)
        );
        if (terrainIt == state.terrainByCell.end()
            || terrainIt->second == nullptr) {
            return true;
        }
        const TerrainCell& terrain = *terrainIt->second;
        if (!runtimeOverlayProvidesBridgeDeck(terrain)
            || (span.high
                && (!runtimeOverlayLooksHighBridge(terrain)
                    || !runtimeHighBridgeOverlayMatchesAxis(
                        terrain,
                        span.alongX
                    )
                    || runtimeHighBridgeMaterialForOverlay(terrain)
                        != span.material
                    || terrain.overlayFrame
                        != repairCell.expectedFrame))
            || (!span.high
                && (!runtimeOverlayLooksVisibleLowBridge(terrain)
                    || !runtimeLowBridgeOverlayMatches(
                        terrain,
                        span.material,
                        span.alongX,
                        false
                    )
                    || (runtimeLowBridgeOverlayIsHead(terrain)
                        ? (runtimeLowBridgeOffset(terrain) % 2) != 0
                        : runtimeLowBridgeOffset(terrain) != (span.alongX ? 0 : 9))
                    || terrain.overlayFrame
                        != repairCell.expectedFrame))) {
            return true;
        }
    }
    return false;
}

bool runtimeBridgeRepairSpanCanBeRepaired(
    const RuntimeGameSessionState& state,
    const RuntimeBridgeRepairSpan& span) {
    if (!runtimeBridgeRepairSpanNeedsRepair(state, span)) {
        return false;
    }

    std::unordered_set<int> repairFootprint;
    for (const RuntimeBridgeRepairCell& repairCell : span.cells) {
        if (!repairCell.requiresOverlay) {
            continue;
        }
        if (span.high) {
            for (int lane = 0; lane < 3; ++lane) {
                repairFootprint.insert(runtimeCellKey(
                    repairCell.point.x + (span.alongX ? 0 : lane),
                    repairCell.point.y + (span.alongX ? lane : 0)
                ));
            }
        } else {
            repairFootprint.insert(runtimeCellKey(
                repairCell.point.x,
                repairCell.point.y
            ));
        }

        const auto terrainIt = state.terrainByCell.find(runtimeCellKey(
            repairCell.point.x,
            repairCell.point.y
        ));
        if (terrainIt == state.terrainByCell.end()
            || terrainIt->second == nullptr) {
            return false;
        }
        const TerrainCell& terrain = *terrainIt->second;
        if (!terrain.hasOverlay) {
            continue;
        }
        const bool compatible = span.high
            ? (runtimeOverlayLooksHighBridge(terrain)
                && runtimeHighBridgeOverlayMatchesAxis(
                    terrain,
                    span.alongX
                )
                && runtimeHighBridgeMaterialForOverlay(terrain)
                    == span.material)
            : runtimeLowBridgeOverlayMatches(
                terrain,
                span.material,
                span.alongX,
                true
            );
        if (!compatible) {
            return false;
        }
    }

    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure) {
            continue;
        }
        for (const RuntimeCellPoint& cell :
                runtimeStructureFootprintCellsForObject(state, object)) {
            if (repairFootprint.find(runtimeCellKey(cell.x, cell.y))
                != repairFootprint.end()) {
                return false;
            }
        }
    }
    return true;
}

void initializeRuntimeBridgeRepairSpans(RuntimeProductionHostState& state) {
    state.bridgeRepairSpansByHut.clear();

    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr || !building->bridgeRepairHut) {
            continue;
        }

        std::optional<RuntimeBridgeRepairSpan> bestSpan;
        int bestRadius = std::numeric_limits<int>::max();
        int bestDistance = std::numeric_limits<int>::max();
        for (const TerrainCell& terrain : state.world.getTerrainCells()) {
            const int dx = std::abs(terrain.x - object.x);
            const int dy = std::abs(terrain.y - object.y);
            const int radius = std::max(dx, dy);
            if (radius < 1 || radius > 3 || terrain.level != object.level) {
                continue;
            }
            std::optional<RuntimeBridgeRepairSpan> candidate;
            if (runtimeLowBridgeOverlayIsHead(terrain)
                && terrain.overlayFrame == 0) {
                candidate = runtimeBuildLowBridgeRepairSpanFromHead(
                    state,
                    object,
                    terrain
                );
            }
            if (!candidate.has_value() && state.theaterLoaded) {
                const RuntimeHighBridgeHeadType head =
                    runtimeHighBridgeHeadTypeForTerrain(state, terrain);
                if (runtimeHighBridgeHeadIsBoundary(head)) {
                    candidate = runtimeBuildHighBridgeRepairSpanFromBoundary(
                        state,
                        object,
                        terrain
                    );
                }
            }
            if (!candidate.has_value()) {
                continue;
            }
            const int distance = dx + dy;
            if (!bestSpan.has_value()
                || radius < bestRadius
                || (radius == bestRadius && distance < bestDistance)) {
                bestSpan = candidate;
                bestRadius = radius;
                bestDistance = distance;
            }
        }
        if (bestSpan.has_value()) {
            state.bridgeRepairSpansByHut.emplace(
                object.instanceId,
                std::move(*bestSpan)
            );
        }
    }
}

void runtimeClearBridgeOverlay(TerrainCell& terrain) {
    terrain.hasOverlay = false;
    terrain.overlayIndex = -1;
    terrain.overlayFrame = 0;
    terrain.overlayType.clear();
    terrain.overlayImage.clear();
    terrain.overlayIsOre = false;
    terrain.overlayIsGem = false;
}

bool runtimeBridgeRepairHutNeedsRepair(
    const RuntimeGameSessionState& state,
    int hutObjectId) {
    const auto spanIt = state.bridgeRepairSpansByHut.find(hutObjectId);
    return spanIt != state.bridgeRepairSpansByHut.end()
        && runtimeBridgeRepairSpanCanBeRepaired(state, spanIt->second);
}

bool runtimeObjectIsBridgeRepairHut(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const BuildingType* building = object.kind == WorldObjectKind::Structure
        ? findRuntimeBuildingTypeById(state.rules, object.type) : nullptr;
    return building != nullptr && building->bridgeRepairHut;
}

bool runtimeBridgeHutDemolitionAvailable(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    if (!runtimeObjectIsBridgeRepairHut(state, object)
        || !object.alive || object.health <= 0 || !state.destroyableBridges) {
        return false;
    }
    const auto found = state.bridgeRepairSpansByHut.find(object.instanceId);
    if (found == state.bridgeRepairSpansByHut.end()) return false;
    const RuntimeBridgeRepairSpan& span = found->second;
    if (span.hutObjectId != object.instanceId || span.cells.empty()
        || !std::any_of(span.cells.begin(), span.cells.end(),
            [](const RuntimeBridgeRepairCell& cell) { return cell.requiresOverlay; })
        || span.level != object.level || span.overlayIndex < 0
        || span.startHead.x < 0 || span.startHead.y < 0
        || span.endHead.x < 0 || span.endHead.y < 0) {
        return false;
    }
    return span.alongX
        ? span.startHead.y == span.endHead.y && span.startHead.x != span.endHead.x
        : span.startHead.x == span.endHead.x && span.startHead.y != span.endHead.y;
}

bool runtimeWeaponUsesBridgeHutDemolition(
    const WeaponType* weapon, const WarheadType* warhead) {
    return (weapon != nullptr && (weapon->suicide || weapon->sabotageCursor))
        || (warhead != nullptr && warhead->ivanBomb);
}

bool runtimeBridgeHutDemolitionCommandLegal(
    const RuntimeGameSessionState& state, const WorldObjectInstance& target,
    const WeaponType* weapon, const WarheadType* warhead,
    bool sourceHasC4) {
    // Ivan plants on the building itself; only demolition needs a valid,
    // destroyable bridge. Keep C4 and other bridge weapons on that policy.
    return !runtimeObjectIsBridgeRepairHut(state, target)
        || (warhead != nullptr && warhead->ivanBomb)
        || (!sourceHasC4 && !runtimeWeaponUsesBridgeHutDemolition(weapon, warhead))
        || runtimeBridgeHutDemolitionAvailable(state, target);
}

bool runtimeMapQueryPassabilityAllows(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point,
    const WorldObjectInstance& mover,
    std::optional<PathMovementMode> movementModeOverride,
    const RuntimePlannerColdCellLookups::ReadOnlyView* coldLookups) {
    const MapCellFacts* facts = coldLookups ? coldLookups->facts() : runtimeMapQueryCell(state, point);
    if (facts == nullptr) {
        return true;
    }

    bool runtimeBridge = false;
    if (coldLookups) {
        if (const TerrainCell* terrain = coldLookups->stateTerrain())
            runtimeBridge = coldLookups->infoForCell(*terrain).bridge;
    } else {
        const auto terrainIt =
            state.terrainByCell.find(runtimeCellKey(point.x, point.y));
        if (terrainIt != state.terrainByCell.end()
            && terrainIt->second != nullptr) {
            runtimeBridge = runtimeCachedTerrainInfoForCell(
                state,
                *terrainIt->second
            ).bridge;
        }
    }

    if (!facts->hasTerrain && !runtimeBridge) {
        return false;
    }

    const PathMovementMode movementMode = runtimeFrameScalingReferenceMovement()
        ? movementModeOverride.value_or(runtimeObjectMovementMode(state, mover))
        : movementModeOverride ? *movementModeOverride
            : runtimeObjectMovementMode(state, mover);
    if (movementMode == PathMovementMode::Air) {
        return true;
    }

    const std::string& hint = facts->passabilityHint;
    if (!runtimeBridge
        && !hint.empty()
        && hint != "terrain-default-passability") {
        if (containsAsciiCaseInsensitive(hint, "blocked")
            || containsAsciiCaseInsensitive(hint, "impassable")) {
            return false;
        }
    }

    if (movementMode == PathMovementMode::Naval
        && !facts->isWater
        && !facts->isBridge
        && !runtimeBridge) {
        return false;
    }

    if (movementMode == PathMovementMode::Ground
        && facts->isWater
        && !facts->isBridge
        && !runtimeBridge
        && !facts->isShore
    ) {
        return false;
    }

    if (facts->isCliff
        && !facts->isRamp
        && !facts->isBridge
        && !runtimeBridge) {
        return false;
    }

    return true;
}

bool runtimeCellIsAllowedOccupiedStructureFootprint(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point,
    int allowedOccupiedObjectId,
    const WorldObjectInstance* mover) {
    if (allowedOccupiedObjectId < 0) {
        return false;
    }
    const WorldObjectInstance* allowedObject =
        findObjectById(state, allowedOccupiedObjectId);
    if (allowedObject == nullptr) {
        return false;
    }
    if (allowedObject->kind == WorldObjectKind::Unit) {
        if (mover && runtimeTransportAuthorizedBoardingHostId(state, *mover, point)
                == allowedOccupiedObjectId) return true;
        const auto workerTask = mover == nullptr ? state.slaveMinerTasks.end()
            : state.slaveMinerTasks.find(mover->instanceId);
        const auto* miner = findRuntimeUnitType(state.rules, *allowedObject);
        const auto* worker = mover == nullptr ? nullptr : findRuntimeUnitType(state.rules, *mover);
        const auto* deployed = miner == nullptr ? nullptr
            : findRuntimeBuildingTypeById(state.rules, miner->deploysInto);
        return mover != nullptr && mover->kind == WorldObjectKind::Infantry
            && mover->owner == allowedObject->owner && worker != nullptr && worker->slaved
            && workerTask != state.slaveMinerTasks.end()
            && workerTask->second.refineryId == allowedOccupiedObjectId
            && (workerTask->second.phase == RuntimeSlaveMinerPhase::Returning
                || (workerTask->second.phase != RuntimeSlaveMinerPhase::Stored
                    && runtimeCellPointForObject(*mover) == point))
            && miner != nullptr && miner->resourceGatherer && miner->resourceDestination
            && miner->slavesNumber > 0 && deployed != nullptr
            && toUpperAsciiLocal(deployed->enslaves) == toUpperAsciiLocal(mover->type)
            && point == runtimeCellPointForObject(*allowedObject);
    }
    if (allowedObject->kind != WorldObjectKind::Structure) return false;
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, allowedObject->type);
    if (mover != nullptr
        && building != nullptr
        && building->numberImpassableRows > 0
        && runtimeObjectMayUseStructureDriveRows(state, *mover)) {
        return runtimeStructureCellIsVehicleDriveRow(
            state,
            *allowedObject,
            point,
            *mover
        );
    }
    const std::vector<RuntimeCellPoint> footprint =
        runtimeStructureFootprintCellsForObject(state, *allowedObject);
    return std::find(footprint.begin(), footprint.end(), point)
        != footprint.end();
}

RuntimeGateConfig runtimeGateConfigForState(
    const RuntimeGameSessionState& state) {
    RuntimeGateConfig config;
    config.fixedTickHz = std::max(1, state.config.fixedTickHz);
    config.openingTicks = std::max(1, config.fixedTickHz / 2);
    config.closingTicks = std::max(1, config.fixedTickHz / 2);
    return config;
}

bool runtimeGateObjectIsLiveGate(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (object.kind != WorldObjectKind::Structure
        || !object.alive
        || object.health <= 0
        || object.rubble) {
        return false;
    }
    const BuildingType* building =
        state.rules.findBuildingType(object.type);
    return building != nullptr && building->gate;
}

bool runtimeGateObjectIsLimbo(const WorldObjectInstance& object) {
    const std::string mission =
        toLowerAsciiLocal(trimAsciiLocal(object.mission));
    return mission == "insidetransport"
        || mission == "garrisoned"
        || mission == "limbo";
}

bool runtimeGateObjectIsGroundMover(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (!object.alive
        || object.health <= 0
        || (object.kind != WorldObjectKind::Unit
            && object.kind != WorldObjectKind::Infantry)
        || object.garrisonHostObjectId >= 0
        || runtimeGateObjectIsLimbo(object)
        || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
        || object.airborne
        || object.authoredAircraft
        || object.submerged) {
        return false;
    }
    if (findRuntimeUnitType(state.rules, object) == nullptr) {
        return false;
    }
    const PathMovementMode movementMode =
        runtimeObjectMovementMode(state, object);
    return movementMode == PathMovementMode::Ground
        || movementMode == PathMovementMode::Amphibious;
}

void rebuildRuntimeGateFootprintIndex(RuntimeGameSessionState& state) {
    state.gateIdsByCell.clear();
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!runtimeGateObjectIsLiveGate(state, object)) {
            continue;
        }
        for (const RuntimeCellPoint& cell :
                runtimeStructureFootprintCellsForObject(state, object)) {
            const int key = runtimeCellKey(cell.x, cell.y);
            if (key >= 0) {
                state.gateIdsByCell[key].push_back(object.instanceId);
            }
        }
    }
    for (auto& [cell, gateIds] : state.gateIdsByCell) {
        (void)cell;
        std::sort(gateIds.begin(), gateIds.end());
        gateIds.erase(
            std::unique(gateIds.begin(), gateIds.end()),
            gateIds.end()
        );
    }
}

std::uint64_t runtimeGateTopologyFingerprint(
    const RuntimeGameSessionState& state) {
    std::uint64_t hash = runtimeStableHashString("RuntimeGateTopology/1");
    for (const RuntimeGateInstanceState& gate : state.gates.gates()) {
        runtimeStableHashMix(hash, gate.gateId);
        runtimeStableHashMixString(hash, gate.owner);
        runtimeStableHashMixString(hash, gate.type);
        runtimeStableHashMix(hash, static_cast<int>(gate.phase));
        runtimeStableHashMix(hash, gate.phaseStartedTick);
        runtimeStableHashMix(hash, gate.phaseCompletesTick);
        const WorldObjectInstance* object = findObjectById(state, gate.gateId);
        if (object == nullptr) {
            runtimeStableHashMix(hash, -1);
            continue;
        }
        for (const RuntimeCellPoint& cell :
                runtimeStructureFootprintCellsForObject(state, *object)) {
            runtimeStableHashMix(hash, cell.x);
            runtimeStableHashMix(hash, cell.y);
        }
    }
    return hash;
}

std::uint64_t runtimeGateStrictOpenFingerprint(
    const RuntimeGameSessionState& state) {
    std::uint64_t hash = runtimeStableHashString("RuntimeGateStrictOpen/1");
    for (const RuntimeGateInstanceState& gate : state.gates.gates()) {
        if (gate.phase == RuntimeGatePhase::Open
            && findObjectById(state, gate.gateId) != nullptr) {
            runtimeStableHashMix(hash, gate.gateId);
        }
    }
    return hash;
}

bool runtimeGateDoorwayOccupied(
    const RuntimeGameSessionState& state,
    int gateId) {
    const WorldObjectInstance* gate = findObjectById(state, gateId);
    if (gate == nullptr || !runtimeGateObjectIsLiveGate(state, *gate)) {
        return false;
    }
    std::unordered_set<int> footprintCells;
    for (const RuntimeCellPoint& cell :
            runtimeStructureFootprintCellsForObject(state, *gate)) {
        const int key = runtimeCellKey(cell.x, cell.y);
        if (key >= 0) {
            footprintCells.insert(key);
        }
    }
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.instanceId == gateId
            || !object.alive
            || object.health <= 0
            || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
            || object.garrisonHostObjectId >= 0
            || runtimeGateObjectIsLimbo(object)
            || (object.kind != WorldObjectKind::Structure
                && (runtimeObjectMovementMode(state, object)
                        == PathMovementMode::Air
                    || runtimeObjectMovementMode(state, object)
                        == PathMovementMode::Naval))
            || object.submerged
            || object.onBridge != gate->onBridge) {
            continue;
        }
        for (const RuntimeCellPoint& cell :
                runtimeStructureFootprintCellsForObject(state, object)) {
            if (footprintCells.find(runtimeCellKey(cell.x, cell.y))
                != footprintCells.end()) {
                return true;
            }
        }
    }
    for (const auto& [moverId, reservation] :
            state.subcells.reservations()) {
        const WorldObjectInstance* mover = findObjectById(state, moverId);
        if (mover == nullptr
            || !runtimeGateObjectIsGroundMover(state, *mover)
            || reservation.destination.onBridge != gate->onBridge) {
            continue;
        }
        if (footprintCells.find(reservation.destination.cell)
            != footprintCells.end()) {
            return true;
        }
    }
    return false;
}

RuntimeGateContext runtimeGateContext(
    const RuntimeGameSessionState& state,
    const std::vector<RuntimeGatePassageRequest>* passageRequests) {
    RuntimeGateContext context;
    context.rules = &state.rules;
    context.objects = &state.world.getObjects();
    context.passageRequests = passageRequests;
    context.areAllied = [&state](
        const std::string& left,
        const std::string& right
    ) {
        return runtimeOwnersAreAllied(state, left, right);
    };
    context.isGroundMover = [&state](const WorldObjectInstance& object) {
        return runtimeGateObjectIsGroundMover(state, object);
    };
    context.doorwayOccupied = [&state](int gateId) {
        return runtimeGateDoorwayOccupied(state, gateId);
    };
    return context;
}

void ensureRuntimeGatesInitialized(RuntimeGameSessionState& state) {
    if (!state.gatesInitialized) {
        state.gates.reset(runtimeGateConfigForState(state));
        state.gatesInitialized = true;
    }
    rebuildRuntimeGateFootprintIndex(state);
    state.gates.reconcile(runtimeGateContext(state));
    state.gateTopologyFingerprint = runtimeGateTopologyFingerprint(state);
    state.gateStrictOpenFingerprint =
        runtimeGateStrictOpenFingerprint(state);
}

std::unordered_set<int> runtimeTransparentGateObjectIds(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    RuntimeGatePassageMode mode) {
    std::unordered_set<int> transparent;
    if (!state.gatesInitialized
        || !runtimeGateObjectIsGroundMover(state, mover)) {
        return transparent;
    }
    const RuntimeGateContext context = runtimeGateContext(state);
    for (const RuntimeGateInstanceState& gate : state.gates.gates()) {
        const WorldObjectInstance* host = findObjectById(state, gate.gateId);
        if (host == nullptr || !runtimeGateObjectIsLiveGate(state, *host)) {
            continue;
        }
        const bool strictOpen = state.gates.allowsPassage(
            context,
            gate.gateId,
            mover.instanceId
        );
        if (strictOpen
            || (mode == RuntimeGatePassageMode::Planning
                && runtimeOwnersAreAllied(
                    state,
                    host->owner,
                    mover.owner
                ))) {
            transparent.insert(gate.gateId);
        }
    }
    return transparent;
}

RuntimeSubcellContext runtimeGateSubcellContext(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance* mover,
    RuntimeGatePassageMode mode) {
    RuntimeSubcellContext context;
    context.groundPoseContext = runtimeGroundPoseContext(state);
    const auto* invisibleIds = !runtimeHistoryQueryReference()
        && !runtimeFrameScalingReferenceMovement() && runtimeActivePlannerInvisibleBuildings != nullptr
        ? runtimeActivePlannerInvisibleBuildings->find(state) : nullptr;
    if (invisibleIds != nullptr) {
        context.ignoredWholeCellObjectIds = *invisibleIds;
    } else {
        for (const auto& object : state.world.getObjects())
            if (runtimeInvisibleMapBuilding(&state.rules, object))
                context.ignoredWholeCellObjectIds.insert(object.instanceId);
    }
    if (!state.gatesInitialized) {
        return context;
    }
    if (mover != nullptr) {
        const auto openGates = runtimeTransparentGateObjectIds(state, *mover, mode);
        context.ignoredWholeCellObjectIds.insert(openGates.begin(), openGates.end());
        return context;
    }
    for (const RuntimeGateInstanceState& gate : state.gates.gates()) {
        if (gate.phase == RuntimeGatePhase::Open
            && findObjectById(state, gate.gateId) != nullptr) {
            context.ignoredWholeCellObjectIds.insert(gate.gateId);
        }
    }
    return context;
}

std::unordered_set<int> runtimeStrictOpenGateIdsAtCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& cell) {
    std::unordered_set<int> output;
    const auto indexed = state.gateIdsByCell.find(
        runtimeCellKey(cell.x, cell.y)
    );
    if (indexed == state.gateIdsByCell.end()) {
        return output;
    }
    const std::unordered_set<int> strictOpen =
        runtimeTransparentGateObjectIds(
            state,
            mover,
            RuntimeGatePassageMode::Commit
        );
    for (int gateId : indexed->second) {
        if (strictOpen.find(gateId) != strictOpen.end()) {
            output.insert(gateId);
        }
    }
    return output;
}

bool runtimeComponentEmptyCensusReference() {
    if (runtimeComponentEmptyCensusReferenceOverride >= 0)
        return runtimeComponentEmptyCensusReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_EMPTY_CENSUS_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

bool runtimePlannerCensusProvesEmptyCell(const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,RuntimePlannerEmptyCellQuery kind) {
    const auto* scope = runtimeActivePlannerEmptyCellCensus;
    return scope && scope->provesEmpty(state,cell,kind);
}

bool runtimeCrusherUsesCrushingMovement(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (!runtimeObjectIsEffectiveCrusher(state, object)
        || runtimeObjectMovementMode(state, object) == PathMovementMode::Air) {
        return false;
    }

    const RuntimeCachedCapabilityProfile& cached =
        runtimeCachedCapabilityProfileForObject(state, object);
    const std::string speedType =
        toLowerAsciiLocal(trimAsciiLocal(cached.speedType));
    if (speedType.empty()) {
        // Stock vehicles without an explicit SpeedType use Track semantics.
        return object.kind == WorldObjectKind::Unit;
    }
    return speedType.find("track") != std::string::npos
        || speedType.find("hover") != std::string::npos
        || speedType.find("float") != std::string::npos;
}

bool runtimeCellHasUnexpectedGroundOccupant(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    int moverObjectId,
    int allowedOccupiedObjectId) {
    for (const WorldObjectInstance& occupant : state.world.getObjects()) {
        if (!occupant.alive
            || occupant.instanceId == moverObjectId
            || occupant.instanceId == allowedOccupiedObjectId
            || (occupant.kind != WorldObjectKind::Structure
                && runtimeObjectMovementMode(state, occupant)
                    == PathMovementMode::Air)) {
            continue;
        }
        if (runtimeObjectOccupiesCrushCell(state, occupant, cell)) {
            return true;
        }
    }
    for (const WorldTerrainObject& terrainObject :
            state.world.getTerrainObjects()) {
        if (!runtimeTerrainObjectBlocksGround(terrainObject)) {
            continue;
        }
        const std::vector<RuntimeCellPoint> footprint =
            runtimeTerrainObjectFootprintCells(terrainObject);
        if (std::find(footprint.begin(), footprint.end(), cell)
            != footprint.end()) {
            return true;
        }
    }
    return false;
}

RuntimeSubcellLocation runtimeSubcellLocationForCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    return {
        cell.x,
        cell.y,
        runtimeCellKey(cell.x, cell.y),
        runtimeCellIsBridge(state, cell.x, cell.y)
    };
}

bool runtimeInfantryCanUseEmptyCellAvailability(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& destination) {
    if (!state.movementEmptyCellProof.subcellCompatible
        || !runtimeMovementTickProvesEmptyCell(state, destination)) return false;
    const auto& objects = state.world.getObjects();
    // Air movement does not update ground navigation occupancy. Read the
    // current pose, including same-tick landing, docking and dock release.
    for (std::size_t index : state.movementEmptyCellProof.subcellAirObjectIndices) {
        if (index >= objects.size()) return false;
        const auto& object = objects[index];
        if (object.alive && !object.airborne && object.x == destination.x
            && object.y == destination.y) return false;
    }
    return true;
}

std::vector<int> runtimeInfantrySubcellsForProvenEmptyCell(
    const RuntimeGameSessionState& state, const WorldObjectInstance& mover,
    const RuntimeCellPoint& destination) {
    static const std::vector<WorldObjectInstance> noObjects;
    static const std::vector<WorldTerrainObject> noTerrainObjects;
    return state.subcells.availableSubCells(noObjects, noTerrainObjects,
        runtimeSubcellLocationForCell(state, destination), {mover.instanceId});
}

std::vector<int> runtimeAvailableInfantrySubcells(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& destination,
    RuntimeGatePassageMode gateMode,
    bool forceReference) {
    if (mover.kind != WorldObjectKind::Infantry) {
        return {};
    }
    if (!forceReference && !runtimeFrameScalingReferenceMovement()
        && runtimePlannerCensusProvesEmptyCell(state,destination,RuntimePlannerEmptyCellQuery::Availability))
        return runtimeInfantrySubcellsForProvenEmptyCell(state,mover,destination);
    if (!forceReference && !runtimeFrameScalingReferenceMovement()) {
        if (runtimeInfantryCanUseEmptyCellAvailability(state, destination))
            return runtimeInfantrySubcellsForProvenEmptyCell(state, mover, destination);
        if (runtimeActiveInfantryAvoidanceBatch != nullptr) {
            if (const auto* slots = runtimeActiveInfantryAvoidanceBatch->find(state,
                    mover, destination, gateMode)) return *slots;
        }
    }
    return state.subcells.availableSubCells(
        state.world.getObjects(),
        state.world.getTerrainObjects(),
        runtimeSubcellLocationForCell(state, destination),
        {mover.instanceId},
        runtimeGateSubcellContext(state, &mover, gateMode)
    );
}

bool runtimeManualFormationMappedReference() noexcept {
    if(runtimeManualFormationMappedReferenceOverride>=0)
        return runtimeManualFormationMappedReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_MANUAL_FORMATION_MAPPED_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}

int runtimeInfantryAuthorizedSlotlessEntryHostId(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to) {
    if(mover.kind!=WorldObjectKind::Infantry || mover.airborne) return -1;
    const auto found=state.behaviors.find(mover.instanceId);
    if(found==state.behaviors.end() || found->second.kind!=RuntimeBehaviorKind::Move
        || found->second.pendingAllowedOccupiedObjectId<0) return -1;
    const int hostId=found->second.pendingAllowedOccupiedObjectId;
    if(runtimeTransportAuthorizedBoardingHostId(state,mover,to)==hostId) return hostId;
    const auto* host=findObjectById(state,hostId);
    if(!host || !host->alive || host->health<=0
        || !runtimeCellIsAllowedOccupiedStructureFootprint(state,to,hostId,&mover)) return -1;
    return hostId;
}

bool runtimeInfantryBodyUsesAuthorizedSlotlessEntry(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to) {
    return runtimeInfantryAuthorizedSlotlessEntryHostId(state,mover,to)>=0;
}

bool runtimeStoppedGroundPoseBlocksRenderMove(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,double nextBaseX,double nextBaseY) {
    if (mover.airborne || mover.kind==WorldObjectKind::Structure
        || runtimeObjectMovementMode(state,mover)==PathMovementMode::Air) return false;
    const bool foot=mover.kind==WorldObjectKind::Infantry;
    const auto offset=worldObjectSubCellOffset(mover);
    const double currentX=runtimeObjectRenderX(mover)+offset.x;
    const double currentY=runtimeObjectRenderY(mover)+offset.y;
    const double nextX=nextBaseX+offset.x,nextY=nextBaseY+offset.y;
    // Navigation commits the destination layer before the visible body arrives.
    // Preview the same half-segment layer switch as advanceRuntimeRenderMove.
    const bool currentOnBridge=worldObjectHasGroundRenderPose(mover)?mover.renderOnBridge:mover.onBridge;
    bool nextOnBridge=currentOnBridge;
    if(mover.renderMoveActive) {
        const auto found=state.behaviors.find(mover.instanceId);
        if(found!=state.behaviors.end() && found->second.renderMoveTicksTotal>0
            && found->second.renderMoveTicksRemaining>0) {
            const auto& move=found->second;
            const double progress=runtimeVehicleMoveRenderProgressAtRemaining(move,
                std::max(0,move.renderMoveTicksRemaining-1));
            nextOnBridge=progress<0.5?move.renderFromOnBridge:move.renderToOnBridge;
        }
    }
    int entryHostId=-1;
    if(mover.renderMoveActive) {
        const auto found=state.behaviors.find(mover.instanceId);
        if(found!=state.behaviors.end()) {
            entryHostId=runtimeTransportAuthorizedBoardingHostId(state,mover,found->second.renderTo);
            if(entryHostId<0 && foot) entryHostId=runtimeInfantryAuthorizedSlotlessEntryHostId(state,mover,found->second.renderTo);
        }
    }
    const auto activeMove=state.behaviors.find(mover.instanceId);
    const bool transportTransfer=entryHostId>=0
        || (activeMove!=state.behaviors.end() && activeMove->second.transportTransferHostId>=0);
    const bool completeBodyInventory=foot || transportTransfer;
    bool blocked=false;
    const auto inspect=[&](const WorldObjectInstance& other) {
        if (blocked || other.instanceId==mover.instanceId || other.instanceId==entryHostId
            || !(completeBodyInventory?runtimeAllGroundBodyIsSpatialCandidate(state,other):runtimeGroundPoseIsSpatialCandidate(state,other))
            || nextOnBridge!=(other.hasRenderPosition?other.renderOnBridge:other.onBridge)
            || other.dockedToObjectId>=0 || other.tankBunkerHostId>=0
            || other.garrisonHostObjectId>=0 || other.factoryExitHostId>=0
            || runtimeObjectOffGroundByMagBeam(state,other.instanceId)
            || runtimeObjectMovementMode(state,other)==PathMovementMode::Air) return;
        if(runtimeCrusherUsesCrushingMovement(state,mover)
            && runtimeCrusherCanCrushObject(state,mover,other,runtimeCellPointForObject(other),true)) return;
        const auto slot=worldObjectSubCellOffset(other);
        const double otherX=runtimeObjectRenderX(other)+slot.x,otherY=runtimeObjectRenderY(other)+slot.y;
        const bool otherFoot=other.kind==WorldObjectKind::Infantry;
        const double minimum=foot&&otherFoot?0.5:(!foot&&!otherFoot?1.0:0.75);
        const double nextDistance=(nextX-otherX)*(nextX-otherX)+(nextY-otherY)*(nextY-otherY);
        if (nextDistance+1e-9>=minimum*minimum) return;
        const double currentDistance=(currentX-otherX)*(currentX-otherX)+(currentY-otherY)*(currentY-otherY);
        if (currentOnBridge==(other.hasRenderPosition?other.renderOnBridge:other.onBridge)
            && currentDistance+1e-9<minimum*minimum && nextDistance>currentDistance+1e-9) return;
        blocked=true;
    };
    const auto& objects=state.world.getObjects();
    const double column=std::floor(nextX+0.5),row=std::floor(nextY+0.5);
    // Ordinary stationary infantry have no render record but still own a body.
    // Foot queries reuse the complete local buckets; vehicles retain their pose inventory.
    const bool indexCurrent=completeBodyInventory?runtimeAllGroundBodySpatialIndexIsCurrent(state):runtimeGroundPoseSpatialIndexIsCurrent(state);
    if(indexCurrent && std::isfinite(column) && std::isfinite(row)
        && column>=-1 && column<=512 && row>=-1 && row<=512) {
        for(int dy=-1;dy<=1 && !blocked;++dy) for(int dx=-1;dx<=1 && !blocked;++dx) {
            const RuntimeCellPoint cell{static_cast<int>(column)+dx,static_cast<int>(row)+dy};
            if(cell.x<0 || cell.y<0 || cell.x>=512 || cell.y>=512) continue;
            if(completeBodyInventory) forEachRuntimeAllGroundBodyCandidateAtCell(state,cell,inspect);
            else forEachRuntimeGroundPoseCandidateAtCell(state,cell,inspect);
        }
    } else {
        for(const auto& object:objects) if(completeBodyInventory || object.hasRenderPosition) inspect(object);
    }
    return blocked;
}

bool runtimeOmniCrushPendingSegmentContainsCell(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior, const RuntimeCellPoint& cell) {
    if (!runtimeOmniCrushPendingSegment(state, object, behavior)) return false;
    return cell == behavior.renderFrom || cell == behavior.renderTo
        || (behavior.renderFrom.x != behavior.renderTo.x
            && behavior.renderFrom.y != behavior.renderTo.y
            && (cell == RuntimeCellPoint{behavior.renderFrom.x, behavior.renderTo.y}
                || cell == RuntimeCellPoint{behavior.renderTo.x, behavior.renderFrom.y}));
}

std::unordered_set<int> runtimeGroundMoveSegmentPlacementCells(
    const RuntimeGameSessionState& state, int ignoredObjectId) {
    std::unordered_set<int> cells;
    for(const auto& [id,behavior] : state.behaviors) {
        if(id==ignoredObjectId) continue;
        const auto* object=findObjectById(state,id);
        if(!object||!object->alive||object->health<=0||object->airborne
            ||object->kind==WorldObjectKind::Structure||runtimeObjectOffGroundByMagBeam(state,id)
            ||runtimeObjectMovementMode(state,*object)==PathMovementMode::Air) continue;
        const bool pendingOmni=runtimeOmniCrushPendingSegment(state,*object,behavior);
        const auto from=pendingOmni?behavior.renderFrom:behavior.moveSegmentFrom;
        if((behavior.moveCooldownTicks<=0&&!pendingOmni)||from.x<0||from.y<0) continue;
        const auto to=pendingOmni?behavior.renderTo:runtimeCellPointForObject(*object);
        cells.insert(runtimeCellKey(from.x,from.y)); cells.insert(runtimeCellKey(to.x,to.y));
        if (runtimeInfantryMoveUsesCommittedCorner(*object, behavior)) {
            // The resumed first leg precedes moveSegmentFrom. Protect its
            // entire small corridor, including the two sides of a corner,
            // against a new foundation while that visible leg is active.
            const auto slot = worldObjectSubCellOffset(*object);
            const double startX = behavior.renderFrom.x + behavior.renderFromOffsetX + slot.x;
            const double startY = behavior.renderFrom.y + behavior.renderFromOffsetY + slot.y;
            const double pivotX = behavior.renderFrom.x + slot.x;
            const double pivotY = behavior.renderFrom.y + slot.y;
            const int left = std::clamp(static_cast<int>(std::floor(std::min(startX,pivotX)+0.5)),0,511);
            const int right = std::clamp(static_cast<int>(std::floor(std::max(startX,pivotX)+0.5)),0,511);
            const int top = std::clamp(static_cast<int>(std::floor(std::min(startY,pivotY)+0.5)),0,511);
            const int bottom = std::clamp(static_cast<int>(std::floor(std::max(startY,pivotY)+0.5)),0,511);
            for (int y=top;y<=bottom;++y) for (int x=left;x<=right;++x)
                cells.insert(runtimeCellKey(x,y));
        }
        if(from.x!=to.x&&from.y!=to.y) {
            cells.insert(runtimeCellKey(from.x,to.y)); cells.insert(runtimeCellKey(to.x,from.y));
        }
    }
    forEachRuntimeAdditionalGroundPoseOccupant(state,[&](const auto& object,[[maybe_unused]] const auto& held) {
        if (object.instanceId!=ignoredObjectId) cells.insert(runtimeCellKey(held.x,held.y));
    });
    return cells;
}

bool runtimeGroundDepartureBatchReference() {
    if (runtimeGroundDepartureBatchReferenceOverride >= 0)
        return runtimeGroundDepartureBatchReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_DEPARTURE_BATCH_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

bool runtimeGroundDepartureHoldsCell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& cell) {
    if (runtimeActiveGroundDepartureQueryBatch != nullptr) {
        const auto cached = runtimeActiveGroundDepartureQueryBatch->query(state, mover, cell);
        if (cached.has_value()) return *cached;
    }
    if (runtimeObjectMovementMode(state, mover) == PathMovementMode::Air) return false;
    int departingInfantry=0;
    for (const auto& [id, behavior] : state.behaviors) {
        if (id == mover.instanceId) continue;
        const auto* object = findObjectById(state, id);
        if (object == nullptr || !object->alive || object->health <= 0
            || object->kind == WorldObjectKind::Structure
            || runtimeObjectOffGroundByMagBeam(state, id)
            || runtimeObjectMovementMode(state, *object) == PathMovementMode::Air)
            continue;
        if (runtimeOmniCrushPendingSegment(state, *object, behavior)) {
            if (runtimeOmniCrushPendingSegmentContainsCell(state, *object, behavior, cell)) return true;
            continue;
        }
        if (behavior.moveCooldownTicks <= 0 || behavior.moveSegmentFrom != cell) continue;
        if (runtimeCellPointForObject(*object) == cell) continue;
        const bool infantry=object->kind==WorldObjectKind::Infantry;
        if(infantry!=(mover.kind==WorldObjectKind::Infantry)) return true;
        if(infantry) ++departingInfantry;
    }
    bool physicalBlocked=false;
    forEachRuntimeAdditionalGroundPoseOccupantAtCell(state,cell,[&](const auto& object,[[maybe_unused]] const auto& held) {
        if (object.instanceId==mover.instanceId) return;
        // An infantry body keeps one slot, not the entire tile. Subcell
        // allocation/commit handles its capacity and exact held slot.
        if (object.kind!=WorldObjectKind::Infantry || mover.kind!=WorldObjectKind::Infantry)
            physicalBlocked=true;
    });
    if (physicalBlocked) return true;
    if(departingInfantry==0) return false;
    for(const auto& other : state.world.getObjects()) {
        if(other.alive && other.health>0 && !other.airborne
            && other.kind==WorldObjectKind::Infantry && other.instanceId!=mover.instanceId
            && runtimeCellPointForObject(other)==cell) ++departingInfantry;
    }
    return departingInfantry>=3;
}

bool runtimeInfantryActiveSegmentCrossesTransition(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::optional<int> selectedDestinationSubCell) {
    if(mover.kind!=WorldObjectKind::Infantry || from==to || mover.airborne
        || runtimeObjectOffGroundByMagBeam(state,mover.instanceId)
        || runtimeObjectMovementMode(state,mover)==PathMovementMode::Air) return false;
    using Point=std::array<double,2>;using Path=std::array<Point,3>;
    const auto oldSlot=worldObjectSubCellOffset(mover);
    const bool stopped=worldObjectHasStoppedGroundPose(mover);
    const Point start{(stopped?mover.renderX:double(from.x))+oldSlot.x,
        (stopped?mover.renderY:double(from.y))+oldSlot.y};
    // The committed destination can use a different slot. Preview its actual
    // body route while retaining the source body's current physical position.
    const bool slotlessEntry=runtimeInfantryBodyUsesAuthorizedSlotlessEntry(state,mover,to);
    if(selectedDestinationSubCell && !slotlessEntry
        && !RuntimeSubcellSystemState::isActiveSubCell(*selectedDestinationSubCell)) return false;
    int nextSubCell=slotlessEntry?-1:(selectedDestinationSubCell?*selectedDestinationSubCell:mover.subCell);
    const auto reserved=state.subcells.reservations().find(mover.instanceId);
    const auto destination=runtimeSubcellLocationForCell(state,to);
    if(!selectedDestinationSubCell && !slotlessEntry && reserved!=state.subcells.reservations().end()
        && reserved->second.destination.x==destination.x
        && reserved->second.destination.y==destination.y
        && reserved->second.destination.cell==destination.cell
        && reserved->second.destination.onBridge==destination.onBridge
        && RuntimeSubcellSystemState::isActiveSubCell(reserved->second.subCell))
        nextSubCell=reserved->second.subCell;
    const auto nextSlot=worldInfantrySubCellOffset(nextSubCell);
    const Point pivot{from.x+nextSlot.x,from.y+nextSlot.y},end{to.x+nextSlot.x,to.y+nextSlot.y};
    const double fromOffsetX=start[0]-pivot[0],fromOffsetY=start[1]-pivot[1];
    const bool corner=!runtimeInfantryOffsetStartsAtSourceSubcell(nextSubCell,fromOffsetX,fromOffsetY)
        && std::abs(fromOffsetX*(to.y-from.y)-fromOffsetY*(to.x-from.x))>1e-9;
    const Path proposed{start,corner?pivot:start,end};
    const bool layer=worldObjectHasGroundRenderPose(mover)?mover.renderOnBridge:mover.onBridge;
    const auto cross=[](const Point& a,const Point& b,const Point& p) {
        return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]);
    };
    const auto opposite=[](double a,double b) {return (a>1e-9 && b<-1e-9)||(a<-1e-9 && b>1e-9);};
    const auto intersects=[&](const Point& a,const Point& b,const Point& c,const Point& d) {
        return opposite(cross(a,b,c),cross(a,b,d)) && opposite(cross(c,d,a),cross(c,d,b));
    };
    bool blocked=false;
    const auto inspect=[&](const WorldObjectInstance& other) {
        if(blocked || other.instanceId==mover.instanceId || other.kind!=WorldObjectKind::Infantry
            || !other.renderMoveActive || !runtimeGroundPoseIsSpatialCandidate(state,other)
            || layer!=other.renderOnBridge) return;
        if(runtimeCrusherUsesCrushingMovement(state,mover)
            && runtimeCrusherCanCrushObject(state,mover,other,runtimeCellPointForObject(other),true)) return;
        const auto found=state.behaviors.find(other.instanceId);
        if(found==state.behaviors.end()) return;
        const auto& move=found->second;
        if(move.renderMoveTicksTotal<=0 || move.renderMoveTicksRemaining<=0) return;
        const auto otherSlot=worldObjectSubCellOffset(other);
        const Point now{other.renderX+otherSlot.x,other.renderY+otherSlot.y};
        const Point target{move.renderTo.x+move.renderToOffsetX+otherSlot.x,
            move.renderTo.y+move.renderToOffsetY+otherSlot.y};
        Point remainingPivot=now;
        if(runtimeInfantryMoveUsesCommittedCorner(other,move)) {
            const double first=std::hypot(move.renderFromOffsetX,move.renderFromOffsetY);
            const double second=std::hypot(double(move.renderTo.x-move.renderFrom.x),
                double(move.renderTo.y-move.renderFrom.y));
            const int ticks=std::max(2,move.renderMoveTicksTotal);
            const int firstTicks=std::clamp(static_cast<int>(std::ceil(ticks*first/(first+second))),1,ticks-1);
            if(ticks*runtimeVehicleMoveRenderProgress(move)<firstTicks)
                remainingPivot={move.renderFrom.x+otherSlot.x,move.renderFrom.y+otherSlot.y};
        }
        const Path remaining{now,remainingPivot,target};
        for(int a=0;a<2 && !blocked;++a) for(int b=0;b<2 && !blocked;++b)
            blocked=intersects(proposed[a],proposed[a+1],remaining[b],remaining[b+1]);
    };
    double minX=start[0],maxX=start[0],minY=start[1],maxY=start[1];
    for(const auto& point:proposed) {
        minX=std::min(minX,point[0]);maxX=std::max(maxX,point[0]);
        minY=std::min(minY,point[1]);maxY=std::max(maxY,point[1]);
    }
    const double left=std::floor(minX+0.5),right=std::floor(maxX+0.5);
    const double top=std::floor(minY+0.5),bottom=std::floor(maxY+0.5);
    if(runtimeGroundPoseSpatialIndexIsCurrent(state) && std::isfinite(left) && std::isfinite(right)
        && std::isfinite(top) && std::isfinite(bottom) && left>=0 && top>=0 && right<512 && bottom<512
        && (right-left+1)*(bottom-top+1)<=4096) {
        for(int y=static_cast<int>(top);y<=static_cast<int>(bottom) && !blocked;++y)
            for(int x=static_cast<int>(left);x<=static_cast<int>(right) && !blocked;++x)
                forEachRuntimeGroundPoseCandidateAtCell(state,{x,y},inspect);
    } else {
        for(const auto& object:state.world.getObjects()) inspect(object);
    }
    return blocked;
}

bool runtimeFactoryGroundSegmentAdmissionBlocked(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::size_t* candidateVisits) {
    const auto active=state.behaviors.find(mover.instanceId);
    if(state.factoryDeliveries.leases().empty()
        && (active==state.behaviors.end() || active->second.factoryExitClearanceHostId<0)) return false;
    if(from==to || !runtimeFactoryLandVehicleEligible(state,mover)
        || (!runtimeFactoryLandDeliveryOwnsEgress(state,mover)
            && (active==state.behaviors.end() || !runtimeFactoryFastClearanceMove(state,mover,active->second)))) return false;
    using Point=std::array<double,2>;
    struct Leg {Point from,to;bool onBridge;};
    const auto dot=[](const Point& a,const Point& b) {return a[0]*b[0]+a[1]*b[1];};
    const auto delta=[](const Point& a,const Point& b) {return Point{a[0]-b[0],a[1]-b[1]};};
    const auto cross=[](const Point& a,const Point& b) {return a[0]*b[1]-a[1]*b[0];};
    const auto pointDistance=[&](const Point& p,const Point& a,const Point& b) {
        const auto direction=delta(b,a);const double length=dot(direction,direction);
        const double part=length>1e-18?std::clamp(dot(delta(p,a),direction)/length,0.0,1.0):0.0;
        const Point nearest{a[0]+direction[0]*part,a[1]+direction[1]*part};
        const auto distance=delta(p,nearest);return dot(distance,distance);
    };
    const auto segmentDistance=[&](const Point& a,const Point& b,const Point& c,const Point& d) {
        const auto first=delta(b,a),second=delta(d,c),between=delta(c,a);
        const double denominator=cross(first,second);
        if(std::abs(denominator)>1e-12) {
            const double t=cross(between,second)/denominator,u=cross(between,first)/denominator;
            if(t>=0 && t<=1 && u>=0 && u<=1) return 0.0;
        }
        return std::min({pointDistance(a,c,d),pointDistance(b,c,d),
            pointDistance(c,a,b),pointDistance(d,a,b)});
    };
    const auto offset=worldObjectSubCellOffset(mover);
    const bool stopped=worldObjectHasStoppedGroundPose(mover);
    const Point start{(stopped?mover.renderX:double(from.x))+offset.x,
        (stopped?mover.renderY:double(from.y))+offset.y};
    const Point end{to.x+offset.x,to.y+offset.y},direction=delta(end,start);
    const bool fromLayer=worldObjectHasGroundRenderPose(mover)?mover.renderOnBridge:mover.onBridge;
    const bool toLayer=runtimeCellIsBridge(state,to.x,to.y);
    std::array<Leg,2> proposed;int proposedCount=1;
    proposed[0]={start,end,fromLayer};
    if(fromLayer!=toLayer) {
        const Point middle{(start[0]+end[0])*0.5,(start[1]+end[1])*0.5};
        proposed[0]={start,middle,fromLayer};proposed[1]={middle,end,toLayer};proposedCount=2;
    }
    bool blocked=false;std::unordered_set<const WorldObjectInstance*> seen;
    const auto inspect=[&](const WorldObjectInstance& other) {
        if(blocked || other.instanceId==mover.instanceId || !seen.insert(&other).second) return;
        if(candidateVisits) ++*candidateVisits;
        if(!runtimeAllGroundBodyIsSpatialCandidate(state,other)) return;
        if(runtimeCrusherUsesCrushingMovement(state,mover)
            && runtimeCrusherCanCrushObject(state,mover,other,runtimeCellPointForObject(other),true)) return;
        const auto slot=worldObjectSubCellOffset(other);
        const Point now{runtimeObjectRenderX(other)+slot.x,runtimeObjectRenderY(other)+slot.y};
        if(!std::isfinite(now[0]) || !std::isfinite(now[1])) {blocked=true;return;}
        const bool currentLayer=other.hasRenderPosition?other.renderOnBridge:other.onBridge;
        const double minimum=other.kind==WorldObjectKind::Infantry?0.75:1.0;
        const auto separation=delta(start,now);const double currentDistance=dot(separation,separation);
        // Already-overlapping bodies can depart monotonically away. This is the
        // same escape allowed by the per-tick guard, without accelerating clocks.
        const bool escaping=currentLayer==fromLayer && currentDistance+1e-9<minimum*minimum
            && dot(separation,direction)>=-1e-9 && dot(delta(end,now),delta(end,now))>currentDistance+1e-9;
        for(int n=0;n<proposedCount;++n) if(proposed[n].onBridge==currentLayer
            && pointDistance(now,proposed[n].from,proposed[n].to)+1e-9<minimum*minimum && !escaping) {
            blocked=true;return;
        }
        const auto found=state.behaviors.find(other.instanceId);
        if(!other.renderMoveActive || found==state.behaviors.end()) return;
        const auto& move=found->second;
        if(move.renderMoveTicksRemaining<=0 || move.renderMoveTicksTotal<=0) return;
        const double progress=runtimeVehicleMoveRenderProgress(move);
        std::array<double,4> cuts{progress,1.0,1.0,1.0};int count=2;
        if(move.renderFromOnBridge!=move.renderToOnBridge && progress<0.5) cuts[count++]=0.5;
        if(runtimeInfantryMoveUsesCommittedCorner(other,move)) {
            const double first=std::hypot(move.renderFromOffsetX,move.renderFromOffsetY);
            const double second=std::hypot(double(move.renderTo.x-move.renderFrom.x),double(move.renderTo.y-move.renderFrom.y));
            const int ticks=std::max(2,move.renderMoveTicksTotal);
            const int firstTicks=std::clamp(static_cast<int>(std::ceil(ticks*first/(first+second))),1,ticks-1);
            const double pivot=double(firstTicks)/ticks;if(progress<pivot) cuts[count++]=pivot;
        }
        std::sort(cuts.begin(),cuts.begin()+count);
        std::array<Leg,3> remaining;int remainingCount=0;Point previous=now;
        bool behind=proposedCount==1 && currentLayer==fromLayer
            && dot(delta(now,start),direction)<=1e-9 && std::abs(cross(direction,delta(now,start)))<=1e-9;
        for(int n=1;n<count;++n) {
            const auto base=runtimeGroundRenderMovePoint(other,move,cuts[n]);
            const Point next{base[0]+slot.x,base[1]+slot.y};
            const bool layer=(cuts[n-1]+cuts[n])*0.5<0.5?move.renderFromOnBridge:move.renderToOnBridge;
            remaining[remainingCount++]={previous,next,layer};
            behind=behind && layer==fromLayer && dot(delta(next,start),direction)<=1e-9
                && std::abs(cross(direction,delta(next,start)))<=1e-9
                && dot(delta(next,previous),direction)>=-1e-9;
            previous=next;
        }
        // A following accepted route ending behind this start cannot reserve
        // the front car's departure forever through a shared endpoint. Its own
        // body guard still prevents it from catching the departing front car.
        if(behind) return;
        for(int a=0;a<proposedCount && !blocked;++a) for(int b=0;b<remainingCount && !blocked;++b)
            if(proposed[a].onBridge==remaining[b].onBridge
                && segmentDistance(proposed[a].from,proposed[a].to,remaining[b].from,remaining[b].to)+1e-9<minimum*minimum)
                blocked=true;
    };
    const double left=std::floor(std::min(start[0],end[0]))-1,right=std::ceil(std::max(start[0],end[0]))+1;
    const double top=std::floor(std::min(start[1],end[1]))-1,bottom=std::ceil(std::max(start[1],end[1]))+1;
    if(runtimeAllGroundBodySpatialIndexIsCurrent(state) && std::isfinite(left) && std::isfinite(right)
        && std::isfinite(top) && std::isfinite(bottom) && left>=0 && top>=0 && right<512 && bottom<512
        && (right-left+1)*(bottom-top+1)<=4096) {
        for(int y=static_cast<int>(top);y<=static_cast<int>(bottom) && !blocked;++y)
            for(int x=static_cast<int>(left);x<=static_cast<int>(right) && !blocked;++x)
                forEachRuntimeAllGroundBodyCandidateAtCell(state,{x,y},inspect);
    } else {
        for(const auto& object:state.world.getObjects()) inspect(object);
    }
    return blocked;
}

int runtimeInfantryBodyPreviewDestinationSubcell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to) {
    if(runtimeInfantryBodyUsesAuthorizedSlotlessEntry(state,mover,to)) return -1;
    const auto destination=runtimeSubcellLocationForCell(state,to);
    const auto reserved=state.subcells.reservations().find(mover.instanceId);
    if(reserved!=state.subcells.reservations().end()
        && reserved->second.destination.x==destination.x && reserved->second.destination.y==destination.y
        && reserved->second.destination.cell==destination.cell && reserved->second.destination.onBridge==destination.onBridge
        && RuntimeSubcellSystemState::isActiveSubCell(reserved->second.subCell)) return reserved->second.subCell;
    const auto available=runtimeAvailableInfantrySubcells(state,mover,to);
    return RuntimeSubcellSystemState::selectAvailableSubCell(available,mover.subCell);
}

bool runtimeInfantryBodySegmentAdmissionBlocked(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::size_t* candidateVisits,int forcedSubCell,std::vector<int>* blockingObjectIds,
    std::optional<int> selectedDestinationSubCell) {
    if(blockingObjectIds) blockingObjectIds->clear();
    if(mover.kind!=WorldObjectKind::Infantry || mover.airborne || from==to
        || runtimeObjectOffGroundByMagBeam(state,mover.instanceId)
        || runtimeObjectMovementMode(state,mover)==PathMovementMode::Air) return false;
    const int entryHostId=runtimeInfantryAuthorizedSlotlessEntryHostId(state,mover,to);
    const bool slotlessEntry=entryHostId>=0;
    const int nextSubCell=slotlessEntry?-1:(selectedDestinationSubCell?*selectedDestinationSubCell
        :(forcedSubCell>=0?forcedSubCell:runtimeInfantryBodyPreviewDestinationSubcell(state,mover,to)));
    if(!RuntimeSubcellSystemState::isActiveSubCell(nextSubCell) && !slotlessEntry) return true;
    using Point=std::array<double,2>;struct Leg {Point from,to;bool onBridge;};
    const auto dot=[](const Point& a,const Point& b) {return a[0]*b[0]+a[1]*b[1];};
    const auto delta=[](const Point& a,const Point& b) {return Point{a[0]-b[0],a[1]-b[1]};};
    const auto cross=[](const Point& a,const Point& b) {return a[0]*b[1]-a[1]*b[0];};
    const auto pointDistance=[&](const Point& p,const Point& a,const Point& b) {
        const auto direction=delta(b,a);const double length=dot(direction,direction);
        const double part=length>1e-18?std::clamp(dot(delta(p,a),direction)/length,0.0,1.0):0.0;
        const Point nearest{a[0]+direction[0]*part,a[1]+direction[1]*part};
        const auto distance=delta(p,nearest);return dot(distance,distance);
    };
    const auto segmentDistance=[&](const Point& a,const Point& b,const Point& c,const Point& d) {
        const auto first=delta(b,a),second=delta(d,c),between=delta(c,a);
        const double denominator=cross(first,second);
        if(std::abs(denominator)>1e-12) {
            const double t=cross(between,second)/denominator,u=cross(between,first)/denominator;
            if(t>=0 && t<=1 && u>=0 && u<=1) return 0.0;
        }
        return std::min({pointDistance(a,c,d),pointDistance(b,c,d),pointDistance(c,a,b),pointDistance(d,a,b)});
    };
    const auto oldSlot=worldObjectSubCellOffset(mover),slot=worldInfantrySubCellOffset(nextSubCell);
    const bool stopped=worldObjectHasStoppedGroundPose(mover);
    const Point start{(stopped?mover.renderX:double(from.x))+oldSlot.x,
        (stopped?mover.renderY:double(from.y))+oldSlot.y};
    const Point pivot{from.x+slot.x,from.y+slot.y},end{to.x+slot.x,to.y+slot.y};
    const auto offset=delta(start,pivot),direction=delta(end,start);
    const bool corner=!runtimeInfantryOffsetStartsAtSourceSubcell(nextSubCell,offset[0],offset[1])
        && std::abs(offset[0]*(to.y-from.y)-offset[1]*(to.x-from.x))>1e-9;
    const bool fromLayer=worldObjectHasGroundRenderPose(mover)?mover.renderOnBridge:mover.onBridge;
    const bool toLayer=runtimeCellIsBridge(state,to.x,to.y);
    // Foot clocks have no vehicle arrival or docking scale. Read, never consume,
    // their interval remainder to split the same corner/layer curve as begin/advance.
    const auto behavior=state.behaviors.find(mover.instanceId);
    const std::int64_t remainder=behavior==state.behaviors.end()?0:behavior->second.moveIntervalRemainderQ16;
    const double q16=std::clamp(runtimeMovementSegmentIntervalQ16(state,mover,from,to)
        /std::max(1e-9,runtimeTerrainMovementSpeedMultiplier(state,mover,from,to)),65536.0,
        double(std::numeric_limits<int>::max()-1)*65536.0);
    int total=static_cast<int>(std::clamp<std::int64_t>((std::llround(q16)+remainder+32768)/65536,
        std::int64_t(1),std::int64_t(std::numeric_limits<int>::max()-1)));
    const double first=std::hypot(offset[0],offset[1]);
    const double second=std::hypot(double(to.x-from.x),double(to.y-from.y));
    double firstPart=0.0;
    if(corner) {
        total=static_cast<int>(std::clamp(std::ceil(total*(first+second)/second),1.0,
            double(std::numeric_limits<int>::max()-1)));
        const int ticks=std::max(2,total);
        firstPart=double(std::clamp(static_cast<int>(std::ceil(ticks*first/(first+second))),1,ticks-1))/ticks;
    }
    const auto proposedPoint=[&](double progress) {
        if(!corner) return Point{start[0]+direction[0]*progress,start[1]+direction[1]*progress};
        if(progress<firstPart) {const double part=progress/firstPart;
            return Point{start[0]+(pivot[0]-start[0])*part,start[1]+(pivot[1]-start[1])*part};}
        const double part=(progress-firstPart)/(1.0-firstPart);
        return Point{pivot[0]+(end[0]-pivot[0])*part,pivot[1]+(end[1]-pivot[1])*part};
    };
    std::array<double,4> cuts{0.0,1.0,1.0,1.0};int cutCount=2;
    if(corner) cuts[cutCount++]=firstPart;
    if(fromLayer!=toLayer) cuts[cutCount++]=0.5;
    std::sort(cuts.begin(),cuts.begin()+cutCount);
    std::array<Leg,3> proposed;int proposedCount=0;
    for(int n=1;n<cutCount;++n) if(cuts[n]>cuts[n-1])
        proposed[proposedCount++]={proposedPoint(cuts[n-1]),proposedPoint(cuts[n]),
            (cuts[n-1]+cuts[n])*0.5<0.5?fromLayer:toLayer};
    bool blocked=false;std::unordered_set<const WorldObjectInstance*> seen;
    const auto recordBlock=[&](const WorldObjectInstance& other) {
        blocked=true;if(blockingObjectIds) blockingObjectIds->push_back(other.instanceId);
    };
    const auto inspect=[&](const WorldObjectInstance& other) {
        if((blocked && !blockingObjectIds) || other.instanceId==mover.instanceId || other.instanceId==entryHostId
            || !seen.insert(&other).second) return;
        if(candidateVisits) ++*candidateVisits;
        if(!runtimeAllGroundBodyIsSpatialCandidate(state,other) || other.factoryExitHostId>=0) return;
        if(runtimeCrusherUsesCrushingMovement(state,mover)
            && runtimeCrusherCanCrushObject(state,mover,other,runtimeCellPointForObject(other),true)) return;
        const auto otherSlot=worldObjectSubCellOffset(other);
        const Point now{runtimeObjectRenderX(other)+otherSlot.x,runtimeObjectRenderY(other)+otherSlot.y};
        if(!std::isfinite(now[0]) || !std::isfinite(now[1])) {recordBlock(other);return;}
        const bool currentLayer=other.hasRenderPosition?other.renderOnBridge:other.onBridge;
        const double minimum=other.kind==WorldObjectKind::Infantry?0.5:0.75;
        const auto separation=delta(start,now);const double currentDistance=dot(separation,separation);
        // An accepted straight front segment keeps moving away from a normal
        // follower. Its final body must clear this follower's complete route.
        // Validate before checking its current point, which can equal our end.
        const auto front=state.behaviors.find(other.instanceId);
        if(proposedCount==1 && other.renderMoveActive && front!=state.behaviors.end()
            && front->second.renderMoveTicksRemaining>0 && front->second.renderMoveTicksTotal>0
            && currentLayer==fromLayer && currentDistance+1e-9>=minimum*minimum
            && dot(delta(now,start),direction)>0.0 && std::abs(cross(direction,delta(now,start)))<=1e-9) {
            const auto& ahead=front->second;
            const Point final{ahead.renderTo.x+ahead.renderToOffsetX+otherSlot.x,
                ahead.renderTo.y+ahead.renderToOffsetY+otherSlot.y};
            if(!runtimeInfantryMoveUsesCommittedCorner(other,ahead)
                && ahead.renderFromOnBridge==fromLayer && ahead.renderToOnBridge==fromLayer
                && std::abs(cross(direction,delta(final,start)))<=1e-9
                && dot(delta(final,now),direction)>=0.0
                && dot(delta(final,end),direction)+1e-9>=minimum*std::hypot(direction[0],direction[1])) return;
        }
        bool escaping=currentLayer==fromLayer && currentDistance+1e-9<minimum*minimum;
        Point escapePrevious=start;
        for(int n=0;n<proposedCount && escaping;++n) {
            const auto away=delta(escapePrevious,now),travel=delta(proposed[n].to,escapePrevious);
            escaping=proposed[n].onBridge==currentLayer && dot(away,travel)>=-1e-9;
            escapePrevious=proposed[n].to;
        }
        escaping=escaping && dot(delta(end,now),delta(end,now))>currentDistance+1e-9;
        for(int n=0;n<proposedCount;++n) if(proposed[n].onBridge==currentLayer
            && pointDistance(now,proposed[n].from,proposed[n].to)+1e-9<minimum*minimum && !escaping) {
            recordBlock(other);return;
        }
        const auto found=state.behaviors.find(other.instanceId);
        if(!other.renderMoveActive || found==state.behaviors.end()) return;
        const auto& move=found->second;
        if(move.renderMoveTicksRemaining<=0 || move.renderMoveTicksTotal<=0) return;
        const double progress=runtimeVehicleMoveRenderProgress(move);
        std::array<double,4> remainingCuts{progress,1.0,1.0,1.0};int count=2;
        if(move.renderFromOnBridge!=move.renderToOnBridge && progress<0.5) remainingCuts[count++]=0.5;
        if(runtimeInfantryMoveUsesCommittedCorner(other,move)) {
            const double a=std::hypot(move.renderFromOffsetX,move.renderFromOffsetY);
            const double b=std::hypot(double(move.renderTo.x-move.renderFrom.x),double(move.renderTo.y-move.renderFrom.y));
            const int ticks=std::max(2,move.renderMoveTicksTotal);
            const double part=double(std::clamp(static_cast<int>(std::ceil(ticks*a/(a+b))),1,ticks-1))/ticks;
            if(progress<part) remainingCuts[count++]=part;
        }
        std::sort(remainingCuts.begin(),remainingCuts.begin()+count);
        std::array<Leg,3> remaining;int remainingCount=0;Point previous=now;
        // A straight departure moves monotonically away from every point in
        // this rear half-plane. Let it clear a follower's shared endpoint.
        bool behind=proposedCount==1 && currentLayer==fromLayer
            && currentDistance+1e-9>=minimum*minimum && dot(delta(now,start),direction)<=1e-9;
        for(int n=1;n<count;++n) if(remainingCuts[n]>remainingCuts[n-1]) {
            const auto base=runtimeGroundRenderMovePoint(other,move,remainingCuts[n]);
            const Point next{base[0]+otherSlot.x,base[1]+otherSlot.y};
            const bool layer=(remainingCuts[n-1]+remainingCuts[n])*0.5<0.5?move.renderFromOnBridge:move.renderToOnBridge;
            remaining[remainingCount++]={previous,next,layer};
            behind=behind && layer==fromLayer && dot(delta(next,start),direction)<=1e-9;
            previous=next;
        }
        if(behind) return;
        bool objectBlocked=false;
        for(int a=0;a<proposedCount && !objectBlocked;++a) for(int b=0;b<remainingCount && !objectBlocked;++b)
            if(proposed[a].onBridge==remaining[b].onBridge
                && segmentDistance(proposed[a].from,proposed[a].to,remaining[b].from,remaining[b].to)+1e-9<minimum*minimum)
                objectBlocked=true;
        if(objectBlocked) recordBlock(other);
    };
    double minX=start[0],maxX=start[0],minY=start[1],maxY=start[1];
    for(int n=0;n<proposedCount;++n) for(const auto& point:{proposed[n].from,proposed[n].to}) {
        minX=std::min(minX,point[0]);maxX=std::max(maxX,point[0]);minY=std::min(minY,point[1]);maxY=std::max(maxY,point[1]);
    }
    const double left=std::floor(minX)-1,right=std::ceil(maxX)+1,top=std::floor(minY)-1,bottom=std::ceil(maxY)+1;
    if(runtimeAllGroundBodySpatialIndexIsCurrent(state) && std::isfinite(left) && std::isfinite(right)
        && std::isfinite(top) && std::isfinite(bottom) && left>=0 && top>=0 && right<512 && bottom<512
        && (right-left+1)*(bottom-top+1)<=4096) {
        for(int y=static_cast<int>(top);y<=static_cast<int>(bottom) && (!blocked || blockingObjectIds);++y)
            for(int x=static_cast<int>(left);x<=static_cast<int>(right) && (!blocked || blockingObjectIds);++x)
                forEachRuntimeAllGroundBodyCandidateAtCell(state,{x,y},inspect);
    } else {
        for(const auto& object:state.world.getObjects()) inspect(object);
    }
    return blocked;
}

bool runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    int targetSubCell,std::vector<int>* blockingObjectIds) {
    return runtimeInfantryBodySegmentAdmissionBlocked(state,mover,from,to,nullptr,targetSubCell,blockingObjectIds);
}

bool runtimeGroundDepartureHoldsTransition(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    std::optional<int> selectedDestinationSubCell) {
    return runtimeFactoryGroundSegmentAdmissionBlocked(state,mover,from,to)
        || runtimeInfantryActiveSegmentCrossesTransition(state,mover,from,to,selectedDestinationSubCell)
        || runtimeGroundDepartureHoldsCell(state,mover,to)
        || (from.x!=to.x && from.y!=to.y
            && (runtimeGroundDepartureHoldsCell(state,mover,{from.x,to.y})
                || runtimeGroundDepartureHoldsCell(state,mover,{to.x,from.y})));
}

bool runtimePlannerEmptySubcellBooleanReference() noexcept {
    if (runtimePlannerEmptySubcellBooleanReferenceOverride >= 0)
        return runtimePlannerEmptySubcellBooleanReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_EMPTY_SUBCELL_BOOLEAN_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

bool runtimePlannerEmptySubcellBooleanResult(const RuntimeSubcellLocation& destination,
    int ignoredMover, const std::map<int, RuntimeSubcellReservation>& reservations) {
    const bool result = runtimeEmptySubcellHasAvailability(destination, ignoredMover, reservations);
    if (runtimePlannerEmptySubcellBooleanObserver)
        runtimePlannerEmptySubcellBooleanObserver(runtimePlannerEmptySubcellBooleanObserverContext, result);
    static const bool trace = std::getenv("CONCORD_RUNTIME_PERF_TRACE") != nullptr;
    if (trace) {
        struct Totals {
            std::size_t queries = 0, available = 0;
            ~Totals() {
                std::cerr << "Runtime planner empty subcell boolean: queries=" << queries
                          << " available=" << available << '\n';
            }
        };
        static thread_local Totals totals;
        ++totals.queries;
        totals.available += result;
    }
    return result;
}

bool runtimeCellGateScratchReference() {
    if (runtimeCellGateScratchReferenceOverride >= 0)
        return runtimeCellGateScratchReferenceOverride != 0;
    static const bool value = [] {
        const char* raw = std::getenv("CONCORD_CELL_GATE_SCRATCH_REFERENCE");
        return raw && raw[0] == '1' && raw[1] == '\0';
    }();
    return value;
}

bool runtimeCellGateScratchExactMiss(const RuntimeGameSessionState& state, int legacyKey) {
    const bool reference = runtimeCellGateScratchReference();
    // Reference does not add a new map lookup before the old strict helper.
    const bool missing = !reference && state.gateIdsByCell.find(legacyKey) == state.gateIdsByCell.end();
    if (auto* stats = runtimeCellGateScratchStats) {
        ++stats->tailQueries;
        if (reference) ++stats->referenceOriginal;
        else if (missing) ++stats->exactMisses;
        else ++stats->indexedOriginal;
    }
    return missing;
}

bool runtimeCellIsPassableUncached(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& point,
    const WorldObjectInstance& mover,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* softOccupiedObjectIds,
    bool allowCrushableOccupants,
    std::optional<PathMovementMode> movementModeOverride) {
    const int key = runtimeCellKey(point.x, point.y);
    if (key < 0) {
        return false;
    }

    // Reuse one classification across the map-fact and terrain checks.
    // value_or evaluates its fallback even when the optional has a value.
    if (!runtimeFrameScalingReferenceMovement() && !movementModeOverride) {
        movementModeOverride = runtimeObjectMovementMode(state, mover);
    }
    const auto occupantAtPoint = occupiedByCell.find(key);
    const bool allowedStructureCell =
        runtimeCellIsAllowedOccupiedStructureFootprint(
            state,
            point,
            allowedOccupiedObjectId,
            &mover
        );
    const auto coldLookups = runtimeActivePlannerColdCellLookups
        ? runtimeActivePlannerColdCellLookups->viewFor(state, mover, terrainByCell, occupiedByCell, point)
        : std::optional<RuntimePlannerColdCellLookups::ReadOnlyView>{};
    if (!allowedStructureCell
        && !runtimeMapQueryPassabilityAllows(
            state,
            point,
            mover,
            movementModeOverride,
            coldLookups ? &*coldLookups : nullptr
        )) {
        return false;
    }

    const TerrainCell* selectedTerrain = nullptr;
    if (coldLookups) {
        selectedTerrain = coldLookups->suppliedTerrain();
    } else {
        const auto terrainIt = terrainByCell.find(key);
        if (terrainIt != terrainByCell.end()) selectedTerrain = terrainIt->second;
    }
    if (selectedTerrain == nullptr) return false;

    const TerrainCell& terrain = *selectedTerrain;
    const PathMovementMode movementMode = runtimeFrameScalingReferenceMovement()
        ? movementModeOverride.value_or(runtimeObjectMovementMode(state, mover))
        : movementModeOverride ? *movementModeOverride
            : runtimeObjectMovementMode(state, mover);
    if (movementMode == PathMovementMode::Air) {
        return runtimeAirCellIsClear(state, mover.instanceId, point);
    }
    if (runtimeFoundationBlocksMovement(state, mover, point, allowedOccupiedObjectId,
            allowCrushableOccupants, RuntimeGatePassageMode::Commit)) return false;
    const bool allowedObjectOccupiesPoint =
        runtimeAllowedObjectOccupiesCell(
            state,
            point,
            allowedOccupiedObjectId
        );
    if (allowedObjectOccupiesPoint
        && runtimeCellHasUnexpectedGroundOccupant(
            state,
            point,
            mover.instanceId,
            allowedOccupiedObjectId
        )) {
        return false;
    }
    const WorldObjectInstance* allowedObject =
        allowedObjectOccupiesPoint
        ? findObjectById(state, allowedOccupiedObjectId)
        : nullptr;
    const bool allowedNonStructureCell = allowedObject != nullptr
        && allowedObject->kind != WorldObjectKind::Structure;
    const auto& originalOccupiedIt = occupantAtPoint;
    if (allowedStructureCell) {
        return originalOccupiedIt == occupiedByCell.end()
            || originalOccupiedIt->second == mover.instanceId
            || originalOccupiedIt->second == allowedOccupiedObjectId;
    }
    const RuntimeTerrainInfo terrainInfo = coldLookups
        ? coldLookups->infoForCell(terrain)
        : runtimeCachedTerrainInfoForCell(state, terrain);
    if (!terrainInfo.passable
        || !runtimeAuthoredLandTypeAllowsMover(state, terrainInfo, mover)) {
        return false;
    }

    if (movementMode == PathMovementMode::Naval
        && !terrainInfo.water
        && !terrainInfo.bridge) {
        return false;
    }

    if (movementMode == PathMovementMode::Ground
        && terrainInfo.water
        && !terrainInfo.bridge
        && !terrainInfo.shore
    ) {
        return false;
    }

    // Infantry are the only ground movers with subcell occupancy.  A cell
    // containing one or two infantry remains traversable while one of the
    // stable 2/4/3 slots is free; terrain, vehicles, and structures are still
    // reported as whole-cell blockers by RuntimeSubcellSystemState.  The
    // allowed structure path is kept intact for engineer/garrison entry.
    if (mover.kind == WorldObjectKind::Infantry
        && !allowedStructureCell) {
        const std::unordered_set<int> strictOpenGateIds =
            runtimeStrictOpenGateIdsAtCell(state, mover, point);
        if (runtimeCellHasHardWholeCellBlocker(
                state,
                point,
                mover.instanceId,
                allowedOccupiedObjectId,
                strictOpenGateIds
            )) {
            return false;
        }
        const auto reserved = state.subcells.reservations().find(
            mover.instanceId
        );
        if (reserved != state.subcells.reservations().end()
            && reserved->second.destination.x == point.x
            && reserved->second.destination.y == point.y
            && reserved->second.destination.cell
                == runtimeCellKey(point.x, point.y)
            && reserved->second.destination.onBridge
                == runtimeCellIsBridge(state, point.x, point.y)) {
            // reserveBatch already validated simultaneous departures and
            // competing arrivals.  Let commit perform the final atomic
            // blocker check instead of re-blocking on a departing occupant.
            return true;
        }
        // Only this complete planner can replace the boolean consumption of
        // the proven-empty vector. Keep the original layer and self reservation.
        if (coldLookups && !runtimePlannerEmptySubcellBooleanReference()
            && !runtimeFrameScalingReferenceMovement()
            && runtimePlannerEmptySubcellBooleanObserversAllowed()
            && runtimePlannerCensusProvesEmptyCell(state, point,
                RuntimePlannerEmptyCellQuery::Availability)) {
            return runtimePlannerEmptySubcellBooleanResult(
                runtimeSubcellLocationForCell(state, point), mover.instanceId,
                state.subcells.reservations());
        }
        return !runtimeAvailableInfantrySubcells(
            state,
            mover,
            point,
            RuntimeGatePassageMode::Commit
        ).empty();
    }

    // The original strict helper returns empty immediately on this same key miss.
    // Keep its original gated branch and every prior predicate in their old order.
    if (runtimeCellGateScratchExactMiss(state, key)) {
        const auto occupiedIt = occupiedByCell.find(key);

        if (occupiedIt != occupiedByCell.end()
            && occupiedIt->second != mover.instanceId
            && (occupiedIt->second != allowedOccupiedObjectId
                || (!allowedStructureCell && !allowedNonStructureCell))) {
            if (runtimeOccupantIdIsTerrain(occupiedIt->second)) {
                return false;
            }
            if (allowCrushableOccupants
                && runtimeCanEnterAndCrushCell(state, mover, point)) {
                return true;
            }
            if (softOccupiedObjectIds != nullptr
                && softOccupiedObjectIds->find(occupiedIt->second)
                    != softOccupiedObjectIds->end()
                && state.behaviors.find(occupiedIt->second) != state.behaviors.end()) {
                return true;
            }
            return false;
        }

        return true;
    }

    const std::unordered_set<int> strictOpenGateIds =
        runtimeStrictOpenGateIdsAtCell(state, mover, point);
    std::unordered_map<int, int> gateFilteredOccupancy;
    const std::unordered_map<int, int>* effectiveOccupiedByCell =
        &occupiedByCell;
    if (!strictOpenGateIds.empty()) {
        gateFilteredOccupancy = buildRuntimeOccupancyLookup(
            state,
            mover.instanceId,
            nullptr,
            &strictOpenGateIds,
            true,
            nullptr,
            mover.kind == WorldObjectKind::Unit
                ? RuntimeStructureOccupancyMode::VehicleNavigation
                : RuntimeStructureOccupancyMode::FullFoundation
        );
        effectiveOccupiedByCell = &gateFilteredOccupancy;
    }
    const auto occupiedIt = effectiveOccupiedByCell->find(key);

    if (occupiedIt != effectiveOccupiedByCell->end()
        && occupiedIt->second != mover.instanceId
        && (occupiedIt->second != allowedOccupiedObjectId
            || (!allowedStructureCell && !allowedNonStructureCell))) {
        if (runtimeOccupantIdIsTerrain(occupiedIt->second)) {
            return false;
        }
        if (allowCrushableOccupants
            && runtimeCanEnterAndCrushCell(state, mover, point)) {
            return true;
        }
        if (softOccupiedObjectIds != nullptr
            && softOccupiedObjectIds->find(occupiedIt->second)
                != softOccupiedObjectIds->end()
            && state.behaviors.find(occupiedIt->second) != state.behaviors.end()) {
            return true;
        }
        return false;
    }

    return true;
}

bool runtimeComponentPassabilityReference() {
    if (runtimeComponentPassabilityReferenceOverride >= 0)
        return runtimeComponentPassabilityReferenceOverride != 0;
    static const bool value = [] {
        const char* raw = std::getenv("CONCORD_COMPONENT_PASSABILITY_REFERENCE");
        return raw && raw[0] == '1' && raw[1] == '\0';
    }();
    return value;
}

bool runtimeComponentDenseReference() {
    if (runtimeComponentDenseReferenceOverride >= 0)
        return runtimeComponentDenseReferenceOverride != 0;
    static const bool value = [] {
        const char* raw = std::getenv("CONCORD_COMPONENT_DENSE_REFERENCE");
        return raw && raw[0] == '1' && raw[1] == '\0';
    }();
    return value;
}

bool runtimeComponentQueryLeaseReference() {
    if (runtimeComponentQueryLeaseReferenceOverride >= 0)
        return runtimeComponentQueryLeaseReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_QUERY_LEASE_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

bool runtimeCellIsPassable(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& point, const WorldObjectInstance& mover,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* softOccupiedObjectIds,
    bool allowCrushableOccupants,
    std::optional<PathMovementMode> movementModeOverride) {
    auto* scope = runtimeActivePlannerCellPassability;
    if (!scope && !runtimePlannerPassabilityObserver)
        return runtimeCellIsPassableUncached(state, terrainByCell, occupiedByCell, point,
            mover, allowedOccupiedObjectId, softOccupiedObjectIds, allowCrushableOccupants,
            movementModeOverride);
    const auto scalar = [&] {
        return runtimeCellIsPassableUncached(state, terrainByCell, occupiedByCell, point,
            mover, allowedOccupiedObjectId, softOccupiedObjectIds, allowCrushableOccupants,
            movementModeOverride);
    };
    const bool reusable = scope && softOccupiedObjectIds == nullptr
        && scope->matches(state, mover, terrainByCell, occupiedByCell)
        && !runtimeComponentPassabilityReference();
    if (!reusable && !runtimePlannerPassabilityObserver) return scalar();
    const RuntimePlannerPassabilityKey key{point.x, point.y, allowedOccupiedObjectId,
        allowCrushableOccupants, movementModeOverride.has_value(),
        movementModeOverride.value_or(PathMovementMode::Ground)};
    bool hit = false;
    bool value;
    // Non-null soft sets always use the original membership query, including
    // empty sets and same-address/same-size changes; no content hash is assumed.
    if (reusable)
        value = scope->query(key, scalar, hit);
    else value = scalar();
    if (runtimePlannerPassabilityObserver)
        runtimePlannerPassabilityObserver(runtimePlannerPassabilityObserverContext,
            mover, key, softOccupiedObjectIds, value, hit);
    return value;
}

bool runtimeComponentCrushAliasReference() {
    if (runtimeComponentCrushAliasReferenceOverride>=0)
        return runtimeComponentCrushAliasReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_COMPONENT_CRUSH_ALIAS_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}

bool runtimePlannerEmptySubcellBooleanObserversAllowed() noexcept {
    return runtimePlannerPassabilityObserver == nullptr
        && runtimePlannerEmptyCensusObserver == nullptr;
}

bool runtimeCellTransitionIsPassable(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& current,
    const RuntimeCellPoint& next,
    const WorldObjectInstance& mover,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* softOccupiedObjectIds,
    bool allowCrushableDestination,
    std::optional<PathMovementMode> movementModeOverride,
    const RuntimePlannerTerrainQueries* terrainQueries,
    const RuntimePlannerCellPassabilityReadOnlyLease* passabilityLease,
    const RuntimePlannerIndexedTopology* indexedTopology) {
    RuntimePerfTraceScope movePhaseTrace("moveTransition", state.tick);
    const int dx = next.x - current.x;
    const int dy = next.y - current.y;
    if (std::abs(dx) > 1 || std::abs(dy) > 1 || (dx == 0 && dy == 0)) {
        return false;
    }
    const PathMovementMode movementMode = movementModeOverride
        ? *movementModeOverride : runtimeObjectMovementMode(state, mover);
    if (movementMode == PathMovementMode::Air) {
        return runtimeAirTransitionIsClear(state, mover.instanceId, current, next);
    }
    if (allowedOccupiedObjectId < 0)
        allowedOccupiedObjectId = runtimeStructureDepartureHostId(state, mover);

    const auto cellIsPassable = [&](const RuntimeCellPoint& point, bool crush) {
        const auto mode = runtimeFrameScalingReferenceMovement()
            ? std::nullopt : std::optional<PathMovementMode>(movementMode);
        if (passabilityLease != nullptr)
            return passabilityLease->query(state,terrainByCell,occupiedByCell,
                point,mover,allowedOccupiedObjectId,softOccupiedObjectIds,crush,mode);
        return runtimeCellIsPassable(state,terrainByCell,occupiedByCell,
            point,mover,allowedOccupiedObjectId,softOccupiedObjectIds,crush,mode);
    };

    if (runtimeCellReservedByOtherRepairUnit(
            state,
            next,
            mover.instanceId
        )) {
        return false;
    }

    if (!cellIsPassable(next,allowCrushableDestination)) {
        return false;
    }
    if (std::abs(dx) == 1 && std::abs(dy) == 1) {
        const RuntimeCellPoint horizontal{current.x + dx, current.y};
        const RuntimeCellPoint vertical{current.x, current.y + dy};
        if (runtimeCellReservedByOtherRepairUnit(
                state,
                horizontal,
                mover.instanceId
            )
            || runtimeCellReservedByOtherRepairUnit(
                state,
                vertical,
                mover.instanceId
            )
            || !cellIsPassable(horizontal,false)
            || !cellIsPassable(vertical,false)) {
            return false;
        }
    }

    // This optional view is obtained only AFTER all original dynamic/diagonal
    // predicates. Missing slots are resolved at the original terrain null veto.
    const auto indexedGeometry = indexedTopology
        ? indexedTopology->pairFor(state,mover,terrainByCell,occupiedByCell,current,next) : std::nullopt;
    const auto terrainView = !indexedGeometry && terrainQueries
        ? terrainQueries->viewFor(state, terrainByCell) : std::nullopt;
    const auto terrainAtKey = [&](int key) {
        if (terrainView) return terrainView->terrainAtKey(key);
        const auto found = terrainByCell.find(key);
        return found == terrainByCell.end() ? nullptr : found->second;
    };
    const TerrainCell* currentTerrain = indexedGeometry ? indexedGeometry->current->terrain
        : terrainAtKey(runtimeCellKey(current.x, current.y));
    const TerrainCell* nextTerrain = indexedGeometry ? indexedGeometry->next->terrain
        : terrainAtKey(runtimeCellKey(next.x, next.y));
    if (currentTerrain == nullptr || nextTerrain == nullptr) {
        return false;
    }

    if (runtimeCellIsAllowedOccupiedStructureFootprint(
            state,
            next,
            allowedOccupiedObjectId,
            &mover
        )) {
        return true;
    }

    if (indexedGeometry)
        return RuntimePlannerIndexedTopology::allowsGeometry(*indexedGeometry,movementMode);

    const RuntimeTerrainInfo currentInfo =
        terrainView ? terrainView->infoForCell(*currentTerrain)
            : runtimeCachedTerrainInfoForCell(state, *currentTerrain);
    const RuntimeTerrainInfo nextInfo =
        terrainView ? terrainView->infoForCell(*nextTerrain)
            : runtimeCachedTerrainInfoForCell(state, *nextTerrain);
    if (movementMode != PathMovementMode::Amphibious
        && currentInfo.water != nextInfo.water
        && !(currentInfo.shore
            || nextInfo.shore
            || currentInfo.bridge
            || nextInfo.bridge)) {
        return false;
    }

    const int levelDelta = std::abs(
        runtimeEffectiveTerrainLevel(*nextTerrain)
            - runtimeEffectiveTerrainLevel(*currentTerrain)
    );
    if (levelDelta > 1) {
        return false;
    }
    if (levelDelta > 0
        && !(currentInfo.ramp
            || nextInfo.ramp
            || currentInfo.bridge
            || nextInfo.bridge)) {
        return false;
    }
    return true;
}

PathCell toPathCell(const RuntimeCellPoint& point) {
    return {point.x, point.y};
}

std::vector<RuntimeCellPoint> toRuntimePath(
    const std::vector<PathCell>& path,
    const RuntimeCellPoint& start) {
    std::vector<RuntimeCellPoint> converted;
    converted.reserve(path.size());
    for (const PathCell& cell : path) {
        const RuntimeCellPoint point = toRuntimeCellPoint(cell);
        if (point == start) {
            continue;
        }
        converted.push_back(point);
    }
    return converted;
}

RuntimePathQueueStatsSnapshot toRuntimePathQueueStatsSnapshot(
    const PathRequestQueueLiveStats& stats) {
    RuntimePathQueueStatsSnapshot snapshot;
    snapshot.pendingCount = stats.pendingCount;
    snapshot.cacheHits = stats.cacheHits;
    snapshot.cacheMisses = stats.cacheMisses;
    snapshot.budgetExhaustedResults = stats.budgetExhaustedResults;
    snapshot.lastPumpBudgetExhausted = stats.lastPumpBudgetExhausted;
    snapshot.backpressureRejectedRequests = stats.backpressureRejectedRequests;
    snapshot.requestOverflowRejectedRequests =
        stats.requestOverflowRejectedRequests;
    snapshot.collapsedPendingRequests = stats.collapsedPendingRequests;
    snapshot.massAttackCollapsedRequests = stats.massAttackCollapsedRequests;
    snapshot.sharedApproachCacheHits = stats.sharedApproachCacheHits;
    snapshot.sharedApproachCacheMisses = stats.sharedApproachCacheMisses;
    snapshot.repeatedFailureSuppressed = stats.repeatedFailureSuppressed;
    snapshot.noPathCooldownHits = stats.noPathCooldownHits;
    snapshot.cooldownNoPathResults = stats.cooldownNoPathResults;
    snapshot.realNoPathResults = stats.realNoPathResults;
    snapshot.equivalentStartBucketCollapses =
        stats.equivalentStartBucketCollapses;
    snapshot.goalSlotAssignments = stats.goalSlotAssignments;
    snapshot.fallbackCandidatesConsidered = stats.fallbackCandidatesConsidered;
    snapshot.fallbackRingRequests = stats.fallbackRingRequests;
    snapshot.fallbackTargetCacheHits = stats.fallbackTargetCacheHits;
    snapshot.fallbackTargetCacheMisses = stats.fallbackTargetCacheMisses;
    snapshot.mapQueryApproachRequests = stats.mapQueryApproachRequests;
    snapshot.mapQueryApproachUsedResults = stats.mapQueryApproachUsedResults;
    snapshot.lastPumpExpandedNodes = stats.lastPumpExpandedNodes;
    snapshot.cachedPathCount = stats.cachedPathCount;
    snapshot.fallbackTargetCacheEntries = stats.fallbackTargetCacheEntries;
    snapshot.usesShortHashKeys = stats.usesShortHashKeys;
    snapshot.mapQueryApproachAvailable = stats.mapQueryApproachAvailable;
    snapshot.usesBlockedComponentApproachFallback =
        stats.usesBlockedComponentApproachFallback;
    return snapshot;
}

std::uint64_t runtimeMovementConstraintHash(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    std::uint64_t hash = runtimeStableHashString("runtimeMovementConstraint");
    runtimeStableHashMix(hash, static_cast<int>(object.kind));
    runtimeStableHashMixString(hash, object.type);
    runtimeStableHashMix(
        hash,
        static_cast<int>(runtimeObjectMovementMode(state, object))
    );
    runtimeStableHashMix(hash, runtimeObjectCanMove(state, object) ? 1 : 0);
    return hash;
}

std::uint64_t runtimePathTargetFootprintHash(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& target) {
    if (target.kind == WorldObjectKind::Structure) {
        return runtimeFootprintCellsHash(
            runtimeStructureFootprintCellsForObject(state, target)
        );
    }

    std::uint64_t hash = runtimeStableHashString("runtimeTargetCell");
    runtimeStableHashMixString(hash, target.type);
    runtimeStableHashMix(hash, static_cast<int>(target.kind));
    runtimeStableHashMix(hash, target.x);
    runtimeStableHashMix(hash, target.y);
    return hash;
}

std::uint64_t runtimePathCellListHash(
    const std::string& purpose,
    const std::vector<RuntimeCellPoint>& cells) {
    std::uint64_t hash = runtimeStableHashString(purpose);
    for (const RuntimeCellPoint& cell : cells) {
        runtimeStableHashMix(hash, cell.x);
        runtimeStableHashMix(hash, cell.y);
    }
    return hash;
}

void populateRuntimePathContextCommon(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& requester,
    PathApproachContext& context,
    int goalSlotCount) {
    context.requesterStableHash = runtimeRequesterStableHash(requester);
    context.movementConstraintHash =
        runtimeMovementConstraintHash(state, requester);
    context.movementMode = runtimeObjectMovementMode(state, requester);
    context.noPathCooldownTicks = runtimeRepathCooldownTicks * 8;
    context.goalSlotCount = std::max(0, goalSlotCount);
}

PathApproachContext makeRuntimePathApproachContext(
    const RuntimeGameSessionState& state,
    const std::string& purpose,
    const WorldObjectInstance& requester,
    const WorldObjectInstance& target,
    const std::vector<RuntimeCellPoint>& candidates,
    int goalSlotCount) {
    PathApproachContext context;
    populateRuntimePathContextCommon(
        state,
        requester,
        context,
        goalSlotCount > 0
            ? goalSlotCount
            : static_cast<int>(std::min<std::size_t>(16U, candidates.size()))
    );
    context.targetFootprintHash =
        runtimePathTargetFootprintHash(state, target);
    if (candidates.empty()) {
        return context;
    }

    context.mapQueryApproachAvailable = true;
    context.approachRingHash =
        runtimeMapQueryApproachContextHash(purpose, target, candidates);
    context.approachCells.reserve(candidates.size());
    for (const RuntimeCellPoint& candidate : candidates) {
        context.approachCells.push_back(toPathCell(candidate));
    }
    return context;
}

PathApproachContext makeRuntimeMovePathApproachContext(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& requester,
    const RuntimeCellPoint& goal,
    int groupSize,
    bool allowNearbyTarget,
    bool exactTargetRequired,
    std::optional<RuntimeCellPoint> sharedGoal) {
    // Delivery leases are released only at the authored exit target. A
    // temporary blocker must not turn a retry into arrival at a nearby cell.
    exactTargetRequired = exactTargetRequired
        || runtimeFactoryDeliveryOwnsEgress(state, requester.instanceId)
        || runtimeCloneDeliveryOwnsDoorway(state, requester.instanceId);
    if (runtimeOmniCrushOwnsOccupiedEndpoint(state, requester, goal)) {
        exactTargetRequired = true;
        sharedGoal.reset();
    }
    const RuntimeCellPoint contextGoal =
        sharedGoal.has_value() ? *sharedGoal : goal;
    const int slotCount =
        exactTargetRequired
            ? 0
            : allowNearbyTarget
            ? (groupSize > 1 ? std::clamp(groupSize, 4, 32) : 8)
            : 0;
    PathApproachContext context;
    populateRuntimePathContextCommon(state, requester, context, slotCount);
    context.targetFootprintHash = runtimePathCellListHash(
        "moveTarget",
        std::vector<RuntimeCellPoint>{contextGoal}
    );
    if (exactTargetRequired || !allowNearbyTarget) {
        return context;
    }
    context.shareAcrossGoalSlots = groupSize > 1;
    const int radius =
        groupSize > 1
            ? std::clamp(
                static_cast<int>(
                    std::ceil(
                        std::sqrt(static_cast<double>(std::max(1, groupSize)))
                        * 0.55
                    )
                ) + 2,
                3,
                5
            )
            : (allowNearbyTarget ? 3 : 2);
    const std::vector<RuntimeCellPoint> alternateGoals =
        runtimeAlternateGoalCellsNear(contextGoal, radius);
    context.approachRingHash =
        runtimePathCellListHash("moveAlternateGoals", alternateGoals);
    context.alternateGoalCells.reserve(alternateGoals.size());
    for (const RuntimeCellPoint& cell : alternateGoals) {
        context.alternateGoalCells.push_back(toPathCell(cell));
    }
    return context;
}

const std::vector<PathTerrainCell>& runtimeStaticPathTerrainSnapshot(
    const RuntimeGameSessionState& state) {
    RuntimePerfTraceScope trace("runtimeStaticPathTerrainSnapshot", state.tick);
    RuntimePathTerrainSnapshotCache& cache = state.pathTerrainCache;
    const std::vector<TerrainCell>& terrainCells = state.world.getTerrainCells();
    if (cache.valid && cache.terrainCellCount == terrainCells.size()) {
        return cache.staticCells;
    }

    cache.staticCells.clear();
    cache.staticCells.reserve(terrainCells.size());
    for (const TerrainCell& terrain : terrainCells) {
        const RuntimeTerrainInfo terrainInfo =
            runtimeCachedTerrainInfoForCell(state, terrain);
        PathTerrainCell cell;
        cell.cell = {terrain.x, terrain.y};
        cell.level = runtimeEffectiveTerrainLevel(terrain);
        cell.passable = terrainInfo.passable;
        cell.water = terrainInfo.water;
        cell.shore = terrainInfo.shore;
        cell.bridge = terrainInfo.bridge;
        cell.ramp = terrainInfo.ramp;
        cell.cliff = terrainInfo.cliff;
        cell.resource =
            terrain.hasOverlay
            && (terrain.overlayIsOre || terrain.overlayIsGem);
        cell.cost = terrainInfo.cost;
        cache.staticCells.push_back(cell);
    }

    std::unordered_map<int, std::size_t> snapshotIndexByCell;
    snapshotIndexByCell.reserve(cache.staticCells.size());
    cache.staticNextIndexByCell.assign(cache.staticCells.size(),
        std::numeric_limits<std::size_t>::max());
    for (std::size_t index = 0; index < cache.staticCells.size(); ++index) {
        const PathTerrainCell& cell = cache.staticCells[index];
        const auto [first, inserted] = snapshotIndexByCell.emplace(
            runtimeCellKey(cell.cell.x, cell.cell.y),
            index
        );
        if (!inserted) {
            cache.staticNextIndexByCell[index] = cache.staticNextIndexByCell[first->second];
            cache.staticNextIndexByCell[first->second] = index;
        }
    }
    for (PathTerrainCell& cell : cache.staticCells) {
        if (!cell.passable || cell.water || cell.bridge || cell.ramp) {
            continue;
        }

        int edgePenalty = 0;
        for (const RuntimeCellPoint& neighbor :
                runtimeNeighborsOf({cell.cell.x, cell.cell.y})) {
            const auto found = snapshotIndexByCell.find(
                runtimeCellKey(neighbor.x, neighbor.y)
            );
            if (found == snapshotIndexByCell.end()
                || found->second >= cache.staticCells.size()) {
                continue;
            }
            const PathTerrainCell& neighborCell =
                cache.staticCells[found->second];
            if (neighborCell.cliff) {
                edgePenalty = std::max(edgePenalty, 42);
            } else if (!neighborCell.passable) {
                edgePenalty = std::max(edgePenalty, 28);
            } else if (neighborCell.water && !neighborCell.bridge) {
                edgePenalty = std::max(edgePenalty, 18);
            } else if (neighborCell.shore) {
                edgePenalty = std::max(edgePenalty, 10);
            }
        }
        cell.cost = std::min(cell.cost + edgePenalty, 80);
    }

    cache.staticFirstIndexByCell = std::move(snapshotIndexByCell);
    cache.terrainCellCount = terrainCells.size();
    cache.valid = true;
    return cache.staticCells;
}

bool runtimePathSnapshotReferenceOccupancy() {
    static const bool useReference = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return useReference;
}

bool runtimePathSnapshotHasCellIndex(const RuntimePathTerrainSnapshotCache& cache,
    const std::vector<PathTerrainCell>& snapshot) {
    // Legacy fixtures may author staticCells directly. Fall back to the old
    // scan if the static builder has not generated its coordinate index.
    return cache.staticNextIndexByCell.size() == snapshot.size()
        && !cache.staticFirstIndexByCell.empty();
}

void applyRuntimePathFoundationConstraints(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, int allowedObjectId, bool usesCrushingMovement,
    const RuntimePathTerrainQueryScope& query, std::vector<PathTerrainCell>& snapshot) {
    const auto& cache = state.pathTerrainCache;
    if (runtimeSimulationQueryReuseReference() || !runtimePathSnapshotHasCellIndex(cache, snapshot)) {
        for (auto& cell : snapshot) {
            if (cell.passable && runtimeFoundationBlocksMovement(state, mover,
                {cell.cell.x, cell.cell.y}, allowedObjectId, usesCrushingMovement,
                RuntimeGatePassageMode::Planning)) cell.passable = false;
        }
        return;
    }
    // The scalar path never initializes the foundation cache when all cells
    // are blocked. Preserve that cache lifetime as well as the boolean result.
    if (std::none_of(snapshot.begin(), snapshot.end(), [](const PathTerrainCell& cell) {
            return cell.passable;
        })) return;
    // Use exactly the original cache and its object offsets; do not rescan or
    // apply different alive/health/transport eligibility to its contents.
    const auto& foundations = runtimeSolidFoundationsByCell(state);
    struct Candidate {
        std::size_t snapshotIndex;
        const std::vector<std::size_t>* foundationIndices;
        bool gateCell;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(std::min(snapshot.size(), foundations.size()));
    for (const auto& [key, indices] : foundations) {
        const auto first = cache.staticFirstIndexByCell.find(key);
        if (first == cache.staticFirstIndexByCell.end()) continue;
        const bool gateCell = state.gateIdsByCell.contains(key);
        for (auto index = first->second; index != std::numeric_limits<std::size_t>::max();
                index = cache.staticNextIndexByCell[index])
            candidates.push_back({index, &indices, gateCell});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.snapshotIndex < b.snapshotIndex;
    });
    for (const auto& candidate : candidates) {
        auto& cell = snapshot[candidate.snapshotIndex];
        if (cell.passable && query.foundationBlocks({cell.cell.x, cell.cell.y},
            *candidate.foundationIndices, candidate.gateCell)) cell.passable = false;
    }
}

const std::vector<PathTerrainCell>& buildRuntimePathTerrainSnapshot(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds,
    bool forceReferenceOccupancy) {
    RuntimePerfTraceScope trace("buildRuntimePathTerrainSnapshot", state.tick);
    if (allowedOccupiedObjectId < 0)
        allowedOccupiedObjectId = runtimeStructureDepartureHostId(state, mover);
    RuntimePathTerrainSnapshotCache& cache = state.pathTerrainCache;
    const std::vector<PathTerrainCell>& staticSnapshot =
        runtimeStaticPathTerrainSnapshot(state);
    const PathMovementMode movementMode =
        runtimeObjectMovementMode(state, mover);
    const LandType* tiberiumLandType = state.rules.findLandType("Tiberium");
    const double tiberiumLandPercent =
        tiberiumLandType == nullptr
            ? 100.0
            : runtimeLandTypePercentForMover(
                *tiberiumLandType,
                state,
                mover
            );
    const UnitType* moverUnit = findRuntimeUnitType(state.rules, mover);
    const bool radiationSensitive =
        movementMode != PathMovementMode::Air
        && (moverUnit == nullptr || !moverUnit->immuneToRadiation);
    std::uint64_t radiationFingerprint = 0;
    bool hasActivePathRadiation = false;
    if (radiationSensitive) {
        radiationFingerprint = runtimeStableHashString("runtimeRadiationPathProfile/1");
        for (const auto& [areaId, area] :
                state.specialMechanisms.radiationAreas()) {
            if (area.strength <= 0 || area.remainingTicks <= 0) {
                continue;
            }
            hasActivePathRadiation = true;
            runtimeStableHashMix(radiationFingerprint, areaId);
            runtimeStableHashMix(radiationFingerprint, area.center.x);
            runtimeStableHashMix(radiationFingerprint, area.center.y);
            runtimeStableHashMix(radiationFingerprint, area.radius);
            runtimeStableHashMix(radiationFingerprint, area.strength);
            runtimeStableHashMix(radiationFingerprint, area.maximumStrength);
        }
    }
    const auto threats = runtimePathThreatAreas(state, mover, runtimePathThreatAvoidance(state, mover));
    std::uint64_t threatFingerprint = 0;
    for (const auto& threat : threats) {
        runtimeStableHashMix(threatFingerprint, threat.objectId);
        runtimeStableHashMix(threatFingerprint, threat.x);
        runtimeStableHashMix(threatFingerprint, threat.y);
        runtimeStableHashMix(threatFingerprint, threat.width);
        runtimeStableHashMix(threatFingerprint, threat.height);
        runtimeStableHashMix(threatFingerprint, static_cast<long long>(std::llround(threat.range * 256)));
        runtimeStableHashMix(threatFingerprint, threat.cost);
    }
    std::unordered_set<int> effectiveIgnoredObjectIds;
    if (ignoredObjectIds != nullptr) {
        effectiveIgnoredObjectIds = *ignoredObjectIds;
    }
    const std::unordered_set<int> planningGateIds =
        runtimeTransparentGateObjectIds(
            state,
            mover,
            RuntimeGatePassageMode::Planning
        );
    effectiveIgnoredObjectIds.insert(
        planningGateIds.begin(),
        planningGateIds.end()
    );
    const std::unordered_set<int>* effectiveIgnoredObjectIdsPtr =
        effectiveIgnoredObjectIds.empty()
            ? nullptr
            : &effectiveIgnoredObjectIds;
    const std::uint64_t ignoredObjectHash =
        runtimeObjectIdSetHash(effectiveIgnoredObjectIdsPtr);
    if (cache.dynamicValid
        && cache.dynamicTick == state.tick
        && cache.dynamicTerrainCellCount == staticSnapshot.size()
        && cache.dynamicMoverObjectId == mover.instanceId
        && cache.dynamicAllowedOccupiedObjectId == allowedOccupiedObjectId
        && cache.dynamicIgnoredObjectHash == ignoredObjectHash
        && cache.dynamicGateTopologyFingerprint
            == state.gateTopologyFingerprint
        && cache.dynamicRadiationFingerprint == radiationFingerprint
        && cache.dynamicThreatFingerprint == threatFingerprint
        && cache.dynamicMovementMode == movementMode) {
        return cache.dynamicCells;
    }

    if (staticSnapshot.empty()) {
        return staticSnapshot;
    }
    std::vector<PathTerrainCell> snapshot = takeRuntimePathSnapshotScratch(
        cache.dynamicCells, cache.dynamicValid, staticSnapshot);

    const std::unordered_map<int, int> occupiedByCell =
        buildRuntimeOccupancyLookup(
            state,
            mover.instanceId,
            nullptr,
            effectiveIgnoredObjectIdsPtr,
            true,
            nullptr,
            mover.kind == WorldObjectKind::Unit
                ? RuntimeStructureOccupancyMode::VehicleNavigation
                : RuntimeStructureOccupancyMode::FullFoundation
        );
    RuntimePerfTraceScope dynamicTrace("pathDynamicCells", state.tick);

    const bool useReferenceOccupancy = forceReferenceOccupancy
        || runtimePathSnapshotReferenceOccupancy();
    RuntimePathDynamicSparseQueries sparseQueries(&state, &mover, state.tick,
        occupiedByCell, snapshot.size(), movementMode != PathMovementMode::Air
            && threats.empty() && !hasActivePathRadiation && !useReferenceOccupancy
            && !runtimeSimulationQueryReuseReference()
            && !runtimePathSnapshotMetadataQueryReference()
            && !runtimePathSnapshotOccupancyQueryReference(),
        [&](auto&& addRepairPoint) {
            for (const auto& relation : state.repairDepots.relations()) {
                if (relation.unitId == mover.instanceId) continue;
                const auto& reserved = relation.phase == RuntimeRepairDepotRelationPhase::Queued
                    ? relation.queueCell : relation.dockCell;
                if (reserved.x >= 0 && reserved.y >= 0)
                    addRepairPoint(reserved.x, reserved.y);
            }
        });
    const bool perCellBatchReference = runtimePathDynamicCellBatchReference();
    const auto sparseView = sparseQueries.viewFor(&state, &mover, state.tick, occupiedByCell);
    runtimeRecordPathDynamicSparseAdmission(sparseView.has_value(),
        movementMode != PathMovementMode::Air,
        useReferenceOccupancy || runtimeSimulationQueryReuseReference()
            || runtimePathSnapshotMetadataQueryReference() || runtimePathSnapshotOccupancyQueryReference(),
        runtimePathDynamicSparseQueriesReference(), snapshot.size(), occupiedByCell.size(),
        threats.size(), hasActivePathRadiation, sparseQueries.stats().retainedBytes,
        perCellBatchReference);
    const RuntimePathRepresentativeOccupants representativeOccupants(
        occupiedByCell, snapshot.size(), sparseView.has_value()
            || runtimePathSnapshotOccupancyQueryReference());
    // A mover has one permission for each authored land type throughout this
    // snapshot. Avoid resolving its rules and normalizing movement strings
    // for every terrain cell; keep this memo local to the exact mover/query.
    constexpr std::size_t landTypeCount =
        static_cast<std::size_t>(RuntimeTerrainLandType::Tunnel) + 1U;
    std::array<signed char, landTypeCount> landTypeAllowed;
    landTypeAllowed.fill(-1);
    const auto authoredLandTypeAllowsMover =
        [&](const RuntimeTerrainInfo& info) {
        const int index = static_cast<int>(info.authoredLandType);
        if (index < 0 || static_cast<std::size_t>(index) >= landTypeAllowed.size()) {
            return runtimeAuthoredLandTypeAllowsMover(state, info, mover);
        }
        signed char& allowed = landTypeAllowed[static_cast<std::size_t>(index)];
        if (allowed < 0) {
            allowed = runtimeAuthoredLandTypeAllowsMover(state, info, mover)
                ? 1 : 0;
        }
        return allowed != 0;
    };
    const bool preparationEligible = movementMode != PathMovementMode::Air
        && threats.empty() && !hasActivePathRadiation && !useReferenceOccupancy
        && !runtimeSimulationQueryReuseReference() && !runtimePathSnapshotMetadataQueryReference()
        && !runtimePathSnapshotOccupancyQueryReference() && !runtimePathDynamicSparseQueriesReference()
        && !perCellBatchReference && runtimePathDynamicSparseQueryObserver == nullptr
        && runtimePathSnapshotConditionalQueryObserver == nullptr;
    const bool resourceFusionEligible = preparationEligible && tiberiumLandType != nullptr
        && std::isfinite(tiberiumLandPercent) && !runtimePathDynamicResourceFusionReference()
        && runtimePathDynamicWorksetObserver == nullptr && runtimePathDynamicPreparationObserver == nullptr;
    RuntimePathDynamicPreparation dynamicPreparation(state, mover, snapshot, occupiedByCell,
        preparationEligible, movementMode != PathMovementMode::Air,
        movementMode == PathMovementMode::Ground, movementMode == PathMovementMode::Naval,
        tiberiumLandType != nullptr, resourceFusionEligible, tiberiumLandPercent);
    const auto visitPreparationRepair = [&](auto&& addRepairPoint) {
        for (const auto& relation : state.repairDepots.relations()) {
            if (relation.unitId == mover.instanceId) continue;
            const auto& reserved = relation.phase == RuntimeRepairDepotRelationPhase::Queued
                ? relation.queueCell : relation.dockCell;
            if (reserved.x >= 0 && reserved.y >= 0) addRepairPoint(reserved.x, reserved.y);
        }
    };
    const std::vector<std::int64_t>* denseAuthoredLandValues=nullptr;
    const bool denseAuthoredLandEligible=preparationEligible
        && !runtimePathDenseAuthoredLandReference() && !runtimePathDynamicPreparationReference()
        && !runtimePathDynamicResourceFusionReference()
        && !runtimePathDynamicWorksetReference() && runtimeActivePlannerLandPermission==nullptr
        && runtimeActivePathDynamicWorkset==nullptr
        && runtimePathDynamicWorksetObserver==nullptr && runtimePathDynamicPreparationObserver==nullptr
        && runtimePathDynamicResourceFusionObserver==nullptr
        && dynamicPreparation.permitsDenseAuthoredLand();
    if(denseAuthoredLandEligible && snapshot.size()>=4096 && snapshot.size()<=262144
        && state.terrainInfoByCell.size()>=4096 && state.terrainInfoByCell.size()<=262144) {
        try { denseAuthoredLandValues=&state.terrainInfoByCell.denseAuthoredLandValues(); }
        catch(const std::bad_alloc&) { denseAuthoredLandValues=nullptr; }
    }
    const auto denseAuthoredLandAllowsMover=[&](std::int64_t ordinal) {
        if(ordinal<0 || ordinal>static_cast<std::int64_t>(RuntimeTerrainLandType::Tunnel))return true;
        auto& allowed=landTypeAllowed[static_cast<std::size_t>(ordinal)];
        if(allowed<0)allowed=runtimeAuthoredLandOrdinalAllowsMoverUncached(
            state,static_cast<RuntimeTerrainLandType>(ordinal),mover)?1:0;
        return allowed!=0;
    };
    dynamicPreparation.prepare(true, visitPreparationRepair, authoredLandTypeAllowsMover,
        RuntimePathDenseAuthoredLandPermission(denseAuthoredLandValues,denseAuthoredLandAllowsMover));
    if(runtimePathDenseAuthoredLandPreparationObserver) {
        const auto denseObservation=dynamicPreparation.denseAuthoredLandObservation(
            denseAuthoredLandEligible,denseAuthoredLandValues!=nullptr);
        runtimePathDenseAuthoredLandPreparationObserver(runtimePathDenseAuthoredLandPreparationObserverContext,
            &state,&mover,state.tick,denseObservation);
    }
    bool includeUnexpectedOccupants = !useReferenceOccupancy
        && allowedOccupiedObjectId >= 0 && movementMode != PathMovementMode::Air;
    if (includeUnexpectedOccupants && !runtimeSimulationQueryReuseReference()) {
        // Both unexpected-occupant calls are behind the allowed structure
        // footprint gate. A live unit/infantry target cannot reach either.
        // Keep zero-health and hidden/transport structures: the gate's exact
        // eligibility is alive plus Structure, without these extra filters.
        const auto* allowedObject = findObjectById(state, allowedOccupiedObjectId);
        includeUnexpectedOccupants = allowedObject != nullptr
            && allowedObject->kind == WorldObjectKind::Structure;
    }
    std::optional<RuntimePerfTraceScope> blockerPreparationTrace;
    if (runtimePathDynamicPreparationTraceEnabled())
        blockerPreparationTrace.emplace("pathSnapshotBlockerPrepare", state.tick);
    const RuntimePathSnapshotOccupancy blockerLookup(
        state, snapshot, mover.instanceId, allowedOccupiedObjectId,
        planningGateIds,
        !useReferenceOccupancy && mover.kind == WorldObjectKind::Infantry
            && movementMode != PathMovementMode::Air,
        includeUnexpectedOccupants,
        dynamicPreparation.boundsFor(includeUnexpectedOccupants
            || (!useReferenceOccupancy && mover.kind == WorldObjectKind::Infantry
                && movementMode != PathMovementMode::Air))
    );
    blockerPreparationTrace.reset();
    const auto cellHasHardBlocker = [&](const RuntimeCellPoint& cell) {
        return useReferenceOccupancy || blockerLookup.needsScalarTerrainFallback()
            ? runtimeCellHasHardWholeCellBlocker(state, cell, mover.instanceId,
                allowedOccupiedObjectId, planningGateIds)
            : blockerLookup.hardBlocker(cell);
    };
    const auto cellHasUnexpectedOccupant = [&](const RuntimeCellPoint& cell) {
        return useReferenceOccupancy || blockerLookup.needsScalarTerrainFallback()
            ? runtimeCellHasUnexpectedGroundOccupant(state, cell,
                mover.instanceId, allowedOccupiedObjectId)
            : blockerLookup.unexpectedOccupant(cell);
    };
    std::vector<bool> infantrySubcellAvailable;
    if (mover.kind == WorldObjectKind::Infantry
        && movementMode != PathMovementMode::Air) {
        std::optional<RuntimePerfTraceScope> infantryCollectionTrace;
        if (runtimePathDynamicPreparationTraceEnabled())
            infantryCollectionTrace.emplace("pathSnapshotInfantryCollect", state.tick);
        infantrySubcellAvailable.resize(snapshot.size(), false);
        std::vector<RuntimeSubcellLocation> destinations;
        std::vector<std::size_t> destinationIndices;
        const auto collectDestinations = [&](auto first, auto last, auto sparseEnabled, auto collectStats) {
            for (auto entry = first; entry != last; ++entry) {
            const std::size_t index = *entry;
            const PathTerrainCell& cell = snapshot[index];
            const int* occupied = nullptr;
            if constexpr (decltype(sparseEnabled)::value) {
                const auto inputs = sparseView->template possibleInputs<decltype(collectStats)::value>(cell.cell.x, cell.cell.y);
                occupied = sparseView->template representative<decltype(collectStats)::value>(inputs, [&] {
                    return representativeOccupants.find(runtimeCellKey(cell.cell.x, cell.cell.y));
                });
            } else {
                occupied = representativeOccupants.find(runtimeCellKey(cell.cell.x, cell.cell.y));
            }
            if (occupied == nullptr || *occupied == mover.instanceId) continue;
            destinations.push_back(runtimeSubcellLocationForCell(state, {cell.cell.x, cell.cell.y}));
            destinationIndices.push_back(index);
            }
        };
        struct AuthoredIndex {
            std::size_t value;
            std::size_t operator*() const noexcept { return value; }
            AuthoredIndex& operator++() noexcept { ++value; return *this; }
            bool operator!=(const AuthoredIndex& other) const noexcept { return value != other.value; }
        };
        const auto* preparationIndices = dynamicPreparation.infantryIndices();
        const auto invokeCollection = [&](auto sparseEnabled, auto collectStats) {
            if (preparationIndices != nullptr) {
                collectDestinations(preparationIndices->begin(), preparationIndices->end(), sparseEnabled, collectStats);
            } else if (perCellBatchReference) {
                for (std::size_t index = 0; index < snapshot.size(); ++index)
                    collectDestinations(AuthoredIndex{index}, AuthoredIndex{index + 1}, sparseEnabled, collectStats);
            } else {
                collectDestinations(AuthoredIndex{0}, AuthoredIndex{snapshot.size()}, sparseEnabled, collectStats);
            }
        };
        if (sparseView) {
            if (sparseQueries.collectStats()) {
                invokeCollection(std::true_type{}, std::true_type{});
            } else {
                invokeCollection(std::true_type{}, std::false_type{});
            }
        } else {
            invokeCollection(std::false_type{}, std::false_type{});
        }
        infantryCollectionTrace.reset();
        dynamicPreparation.recordInfantryCollection(preparationIndices != nullptr
            ? preparationIndices->size() : snapshot.size(), destinations.size());
        if (!destinations.empty()) {
            const std::vector<std::vector<int>> available =
                state.subcells.availableSubCellsBatch(
                    state.world.getObjects(),
                    state.world.getTerrainObjects(),
                    destinations,
                    {mover.instanceId},
                    runtimeGateSubcellContext(
                        state, &mover, RuntimeGatePassageMode::Planning
                    )
                );
            for (std::size_t index = 0; index < available.size(); ++index) {
                infantrySubcellAvailable[destinationIndices[index]] =
                    !available[index].empty();
            }
        }
    }
    const bool usesCrushingMovement = movementMode != PathMovementMode::Air
        && runtimeCrusherUsesCrushingMovement(state, mover);
    std::optional<RuntimePerfTraceScope> crushPreparationTrace;
    if (runtimePathDynamicPreparationTraceEnabled())
        crushPreparationTrace.emplace("pathSnapshotCrushPrepare", state.tick);
    const RuntimeCrushCandidateLookup crushCandidates = usesCrushingMovement
        ? buildRuntimeCrushCandidateLookup(state, snapshot, dynamicPreparation.boundsFor(true))
        : RuntimeCrushCandidateLookup{};
    crushPreparationTrace.reset();
    const RuntimeCrushCandidateList noCrushCandidates;
    const auto crushCandidatesForCell = [&](const RuntimeCellPoint& cell) {
        const auto found = crushCandidates.find(
            runtimeCrushCandidateCellKey(cell.x, cell.y)
        );
        return found == crushCandidates.end()
            ? &noCrushCandidates : &found->second;
    };
    const RuntimePathTerrainQueryScope terrainQuery(state, mover, allowedOccupiedObjectId,
        planningGateIds, usesCrushingMovement, movementMode != PathMovementMode::Air);
    // Reference/observer/rejected early paths acquire metadata at its ORIGINAL
    // late point. Conditional diagnostics remain before Workset construction.
    dynamicPreparation.prepareMetadata();
    const auto& metadataView = dynamicPreparation.metadataView();
    RuntimePathSnapshotConditionalQueries conditionalQueries(state);
    dynamicPreparation.prepare(false, visitPreparationRepair, authoredLandTypeAllowsMover);
    auto& workset = dynamicPreparation.workset();
    const RuntimePathDynamicPreparationWorksetExit worksetExit(dynamicPreparation);
    const auto worksetView = workset.viewFor(&state, &mover, state.tick, snapshot, occupiedByCell);
    // All allocation/domain/density/metadata admission completed before the
    // first copied-output write. These authored rows have no other dynamic
    // effect; later allowed-foundation restoration and masks remain original.
    if(worksetView && workset.resourceFusionEnabled()) {
        const auto applyResources=[&](auto collect) {
            for(const std::size_t index:workset.resourceIndicesForDiagnostics()) {
                PathTerrainCell& cell=snapshot[index];
                workset.template recordResourceVisit<decltype(collect)::value>();
                if(tiberiumLandPercent<=0.0) cell.passable=false;
                else cell.cost=runtimeLandTypeCost(cell.cost,tiberiumLandPercent);
            }
        };
        if(workset.collectsStats()) applyResources(std::true_type{});
        else applyResources(std::false_type{});
    }
    const auto applyDynamicCells = [&](auto first, auto last, auto sparseEnabled, auto collectStats, auto worksetStats) {
        for (auto entry = first; entry != last; ++entry) {
        PathTerrainCell& cell = *entry;
        workset.template recordBodyVisit<decltype(worksetStats)::value>();
        cell.hazardCost = runtimePathThreatCost(threats, cell.cell);
        bool passable = movementMode == PathMovementMode::Air
            ? true
            : cell.passable;
        if (movementMode == PathMovementMode::Air) {
            cell.cost = 10;
        }
        if (passable
            && movementMode == PathMovementMode::Naval
            && !cell.water
            && !cell.bridge) {
            passable = false;
        }
        if (passable
            && movementMode == PathMovementMode::Ground
            && cell.water
            && !cell.bridge
            && !cell.shore) {
            passable = false;
        }

        unsigned char sparseInputs = RuntimePathDynamicSparseQueries::Original;
        if constexpr (decltype(sparseEnabled)::value)
            sparseInputs = sparseView->template possibleInputs<decltype(collectStats)::value>(cell.cell.x, cell.cell.y);
        const int key = runtimeCellKey(cell.cell.x, cell.cell.y);
        const RuntimeTerrainInfo* terrainInfo = conditionalQueries.metadata(
            passable && movementMode != PathMovementMode::Air, [&] {
                return metadataView ? metadataView->find(key)
                    : RuntimePathSnapshotMetadataQueries::originalFind(state, key);
            });
        if (passable
            && movementMode != PathMovementMode::Air
            && terrainInfo != nullptr
            && !authoredLandTypeAllowsMover(*terrainInfo)) {
            passable = false;
        }
        const auto repairReservedForCell = [&] {
            if constexpr (decltype(sparseEnabled)::value) {
                return sparseView->template repairReserved<decltype(collectStats)::value>(sparseInputs, [&] {
                    return terrainQuery.repairReserved({cell.cell.x, cell.cell.y});
                });
            } else {
                return terrainQuery.repairReserved({cell.cell.x, cell.cell.y});
            }
        };
        if (passable
            && movementMode != PathMovementMode::Air
            && repairReservedForCell()) {
            passable = false;
        }
        const int* occupiedIt = conditionalQueries.occupant(
            passable && movementMode != PathMovementMode::Air, [&] {
                if constexpr (decltype(sparseEnabled)::value)
                    return sparseView->template representative<decltype(collectStats)::value>(sparseInputs, [&] { return representativeOccupants.find(key); });
                else
                    return representativeOccupants.find(key);
            });
        if (passable
            && movementMode != PathMovementMode::Air
            && occupiedIt != nullptr
            && *occupiedIt != mover.instanceId) {
            const RuntimeCellPoint occupiedCell{cell.cell.x, cell.cell.y};
            const bool infantryHardBlocked =
                mover.kind == WorldObjectKind::Infantry
                && cellHasHardBlocker(occupiedCell);
            const bool infantryHasSubcell =
                mover.kind == WorldObjectKind::Infantry
                && !infantryHardBlocked
                && infantrySubcellAvailable[
                    static_cast<std::size_t>(&cell - snapshot.data())
                ];
            if (infantryHasSubcell) {
                // Partial terrain occupation and up to two infantry are
                // intentionally represented as passable in an infantry path
                // snapshot. Non-infantry movers continue through the legacy
                // whole-cell branch below.
            } else if (runtimeOccupantIdIsTerrain(*occupiedIt)) {
                passable = false;
            } else if (*occupiedIt == allowedOccupiedObjectId) {
                if (!terrainQuery.allowedFootprint(occupiedCell)
                    || cellHasUnexpectedOccupant(occupiedCell)) {
                    passable = false;
                }
            } else if (!runtimeCanEnterAndCrushCell(
                    state,
                    mover,
                    occupiedCell,
                    &planningGateIds,
                    crushCandidatesForCell(occupiedCell)
                )) {
                passable = false;
            }
        }
        if (passable
            && movementMode != PathMovementMode::Air
            && cell.resource
            && tiberiumLandType != nullptr) {
            if (tiberiumLandPercent <= 0.0) {
                passable = false;
            } else {
                cell.cost = runtimeLandTypeCost(
                    cell.cost,
                    tiberiumLandPercent
                );
            }
        }
        if (passable && radiationSensitive) {
            for (const auto& [unusedAreaId, area] :
                    state.specialMechanisms.radiationAreas()) {
                (void)unusedAreaId;
                if (area.strength <= 0 || area.remainingTicks <= 0) {
                    continue;
                }
                const double radiationFalloff = runtimeRadiationFalloff(
                    static_cast<double>(cell.cell.x) - area.center.x,
                    static_cast<double>(cell.cell.y) - area.center.y, area.radius);
                if (radiationFalloff <= 0.0) {
                    continue;
                }
                const int maximumStrength = std::max(1, area.maximumStrength);
                const double normalizedStrength = static_cast<double>(
                    std::clamp(area.strength, 0, maximumStrength)
                ) / static_cast<double>(maximumStrength);
                const int hazardCost = std::clamp(
                    40 + static_cast<int>(std::lround(140.0 * normalizedStrength * radiationFalloff)),
                    0,
                    240
                );
                cell.hazardCost = std::max(cell.hazardCost, hazardCost);
            }
        }
        cell.passable = passable;
        }
    };
    // Every variant preserves the same complete ordered predicate body. The
    // normal loop lives inside its outlined function, called once per builder.
    // Reference restores one call per cell without duplicating the algorithm.
    const auto invokeRanges = [&](auto sparseEnabled, auto collectStats, auto worksetStats) {
        if (worksetView) {
            applyDynamicCells(worksetView->begin(), worksetView->end(), sparseEnabled, collectStats, worksetStats);
        } else if (perCellBatchReference) {
            for (PathTerrainCell& cell : snapshot)
                applyDynamicCells(&cell, &cell + 1, sparseEnabled, collectStats, worksetStats);
        } else {
            applyDynamicCells(snapshot.data(), snapshot.data() + snapshot.size(), sparseEnabled, collectStats, worksetStats);
        }
    };
    const auto invokeDynamicCells = [&](auto sparseEnabled, auto collectStats) {
        if (workset.collectsStats()) invokeRanges(sparseEnabled, collectStats, std::true_type{});
        else invokeRanges(sparseEnabled, collectStats, std::false_type{});
    };
    if (sparseView) {
        if (sparseQueries.collectStats()) {
            invokeDynamicCells(std::true_type{}, std::true_type{});
        } else {
            invokeDynamicCells(std::true_type{}, std::false_type{});
        }
    } else {
        invokeDynamicCells(std::false_type{}, std::false_type{});
    }
    const auto* allowedPathObject = allowedOccupiedObjectId < 0 ? nullptr
        : findObjectById(state, allowedOccupiedObjectId);
    const bool enteringMobileSlaveHome = allowedPathObject != nullptr
        && allowedPathObject->kind == WorldObjectKind::Unit
        && runtimeCellIsAllowedOccupiedStructureFootprint(state,
            runtimeCellPointForObject(*allowedPathObject), allowedOccupiedObjectId, &mover);
    if (allowedOccupiedObjectId >= 0
        && (enteringMobileSlaveHome
            || !restoreRuntimeAllowedPathFoundation(state, terrainQuery, movementMode,
                cellHasUnexpectedOccupant, snapshot))) {
        const WorldObjectInstance* allowedObject =
            findObjectById(state, allowedOccupiedObjectId);
        if (allowedObject != nullptr
            && allowedObject->alive
            && (allowedObject->kind == WorldObjectKind::Structure
                || runtimeCellIsAllowedOccupiedStructureFootprint(state,
                    runtimeCellPointForObject(*allowedObject), allowedOccupiedObjectId, &mover))) {
            const std::vector<RuntimeCellPoint> allowedCells =
                allowedObject->kind == WorldObjectKind::Structure
                    ? runtimeStructureFootprintCellsForObject(state, *allowedObject)
                    : std::vector<RuntimeCellPoint>{runtimeCellPointForObject(*allowedObject)};
            for (PathTerrainCell& cell : snapshot) {
                for (const RuntimeCellPoint& allowedCell : allowedCells) {
                    if (cell.cell.x == allowedCell.x
                        && cell.cell.y == allowedCell.y) {
                        if (runtimeCellIsAllowedOccupiedStructureFootprint(
                                state,
                                allowedCell,
                                allowedOccupiedObjectId,
                                &mover
                            )
                            && (movementMode == PathMovementMode::Air
                                || !runtimeCellReservedByOtherRepairUnit(
                                    state,
                                    allowedCell,
                                    mover.instanceId
                                ))) {
                            cell.passable =
                                movementMode == PathMovementMode::Air
                                || !cellHasUnexpectedOccupant(allowedCell);
                        }
                        break;
                    }
                }
            }
        }
    }
    // Formation filters and representative occupants must never erase a solid
    // foundation, including when an explicitly allowed host overlaps another.
    if (movementMode != PathMovementMode::Air)
        applyRuntimePathFoundationConstraints(state, mover, allowedOccupiedObjectId,
            usesCrushingMovement, terrainQuery, snapshot);
    cache.dynamicCells = std::move(snapshot);
    cache.dynamicValid = true;
    cache.dynamicTick = state.tick;
    cache.dynamicTerrainCellCount = cache.dynamicCells.size();
    cache.dynamicMoverObjectId = mover.instanceId;
    cache.dynamicAllowedOccupiedObjectId = allowedOccupiedObjectId;
    cache.dynamicIgnoredObjectHash = ignoredObjectHash;
    cache.dynamicGateTopologyFingerprint =
        state.gateTopologyFingerprint;
    cache.dynamicRadiationFingerprint = radiationFingerprint;
    cache.dynamicThreatFingerprint = threatFingerprint;
    cache.dynamicMovementMode = movementMode;
    return cache.dynamicCells;
}

RuntimeQueuedPathResult runtimeQueuedPathResultFromPathResult(
    const PathResult& result,
    const RuntimeCellPoint& start) {
    RuntimeQueuedPathResult output;
    output.reachable = result.reachable;
    output.budgetExhausted = result.searchBudgetExhausted
        || result.status == PathResultStatus::BudgetExhausted;
    output.cooldownNoPath =
        result.status == PathResultStatus::CooldownNoPath;
    output.realNoPath = result.status == PathResultStatus::RealNoPath;
    output.cacheHit = result.cacheHit;
    output.sharedApproachHit = result.sharedApproachCacheHit;
    output.goalAdjusted = result.goalAdjusted;
    output.resolvedGoal = toRuntimeCellPoint(result.resolvedGoal);
    if (result.reachable || !result.path.empty()) {
        output.path = toRuntimePath(result.path, start);
    }
    return output;
}

PathRequestQueue runtimeTimedPathRequestQueue(const std::vector<PathTerrainCell>& terrain,
    int tick, PathRequestQueueTerrainLayoutCache& layout) {
    RuntimePerfTraceScope trace("prepareRuntimePathQueueTerrain", tick);
    return PathRequestQueue(RuntimeReadOnlyPathTerrainTag{}, terrain, layout);
}

RuntimeQueuedPathResult resolveRuntimePathWithQueue(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds,
    const PathApproachContext& context) {
    RuntimePerfTraceScope trace("resolveRuntimePathWithQueue", state.tick);
    RuntimeQueuedPathResult output;
    output.resolvedGoal = goal;
    if (start == goal) {
        output.reachable = true;
        output.resolvedGoal = goal;
        return output;
    }

    const auto pathStart = std::chrono::steady_clock::now();
    { // Lexical terrain borrow: result conversion and diagnostic copies are pure.
    PathRequestQueue queue = runtimeTimedPathRequestQueue(
        buildRuntimePathTerrainSnapshot(
            state,
            mover,
            allowedOccupiedObjectId,
            ignoredObjectIds
        ), state.tick, state.pathTerrainCache.queueLayout
    );
    const PathRequestHandle handle =
        queue.submit(toPathCell(start), toPathCell(goal), context);
    if (handle.id >= 0) {
        if (auto* runner = getPathGoalCandidateRunner(
            state.pathCandidateRunnerChecked, state.pathCandidateRunner))
            queue.pumpWithCandidates(1, runtimePathExpandedNodeBudget, *runner, state.tick);
        else queue.pumpWithBudget(1, runtimePathExpandedNodeBudget);
    }
    state.lastPathQueueStats =
        toRuntimePathQueueStatsSnapshot(queue.liveStats());
    state.lastPathQueueStats.mapQueryApproachAvailable =
        state.lastPathQueueStats.mapQueryApproachAvailable
        || state.pathQueueMapQueryApproachObserved;
    applyRuntimeMapQueryApproachStats(state);
    const PathRequestQueueLiveStats liveStats = queue.liveStats();
    state.perf.lastSimPathRequests += liveStats.cacheMisses;

    for (const PathResult& result : queue.getResults()) {
        if (result.requestId != handle.id) {
            continue;
        }
        output = runtimeQueuedPathResultFromPathResult(result, start);
        break;
    }
    } // Destroy queue before returning to any caller that can mutate dynamicCells.
    if (output.cooldownNoPath) {
        ++state.pathFailureStats.cooldownHits;
    }
    state.perf.lastSimPathMs +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pathStart
        ).count();
    return output;
}

std::vector<RuntimeCellPoint> findRuntimePathUntimed(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds,
    RuntimePathQueueStatsSnapshot* pathStatsOutput) {
    RuntimePerfTraceScope trace("findRuntimePathUntimed", state.tick);
    if (start == goal) {
        return {};
    }

    PathRequestQueue queue = runtimeTimedPathRequestQueue(
        buildRuntimePathTerrainSnapshot(
            state,
            mover,
            allowedOccupiedObjectId,
            ignoredObjectIds
        ), state.tick, state.pathTerrainCache.queueLayout
    );
    PathApproachContext movementContext;
    movementContext.movementMode = runtimeObjectMovementMode(state, mover);
    const PathRequestHandle handle = queue.submit(
        toPathCell(start),
        toPathCell(goal),
        movementContext
    );
    // This separate const query retains the original synchronous path.
    queue.pump(1);
    if (pathStatsOutput != nullptr) {
        *pathStatsOutput = toRuntimePathQueueStatsSnapshot(queue.liveStats());
    }

    for (const PathResult& result : queue.getResults()) {
        if (result.requestId == handle.id && result.reachable) {
            return toRuntimePath(result.path, start);
        }
    }

    return {};
}

std::vector<RuntimeCellPoint> findRuntimePath(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds) {
    const auto pathStart = std::chrono::steady_clock::now();
    ++state.perf.lastSimPathRequests;
    std::vector<RuntimeCellPoint> path = findRuntimePathUntimed(
        state,
        mover,
        start,
        goal,
        allowedOccupiedObjectId,
        ignoredObjectIds,
        &state.lastPathQueueStats
    );
    state.perf.lastSimPathMs +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pathStart
        ).count();
    state.lastPathQueueStats.mapQueryApproachAvailable =
        state.lastPathQueueStats.mapQueryApproachAvailable
        || state.pathQueueMapQueryApproachObserved;
    applyRuntimeMapQueryApproachStats(state);
    return path;
}

std::vector<RuntimeCellPoint> findRuntimeGreedyPathUntimed(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds,
    int maxSteps,
    const std::unordered_map<int, int>* occupiedByCellOverride) {
    if (start == goal) {
        return {};
    }

    if (runtimeObjectMovementMode(state, mover) == PathMovementMode::Air) {
        return findRuntimeAirPath(state, mover, start, goal);
    }
    std::unordered_map<int, const TerrainCell*> generatedTerrainByCell;
    const std::unordered_map<int, const TerrainCell*>* terrainByCell =
        &state.terrainByCell;
    if (terrainByCell->empty()) {
        generatedTerrainByCell = buildRuntimeTerrainLookup(state.world);
        terrainByCell = &generatedTerrainByCell;
    }
    std::unordered_map<int, int> generatedOccupiedByCell;
    const std::unordered_map<int, int>* occupiedByCell =
        occupiedByCellOverride;
    if (occupiedByCell == nullptr) {
        generatedOccupiedByCell =
            buildRuntimeOccupancyLookup(
                state,
                mover.instanceId,
                nullptr,
                ignoredObjectIds
            );
        occupiedByCell = &generatedOccupiedByCell;
    }

    RuntimeCellPoint current = start;
    std::vector<RuntimeCellPoint> path;
    std::unordered_set<int> visited;
    visited.insert(runtimeCellKey(current.x, current.y));
    const double startDistance = runtimeCellDistance(start, goal);
    const int manhattan =
        std::abs(goal.x - start.x) + std::abs(goal.y - start.y);
    const int stepLimit = std::min(
        std::max(24, (manhattan * 3) + 16),
        std::max(1, maxSteps)
    );

    for (int step = 0; step < stepLimit; ++step) {
        RuntimeCellPoint best = current;
        double bestScore = std::numeric_limits<double>::max();
        bool found = false;
        for (const RuntimeCellPoint& next : runtimeNeighborsOf(current)) {
            if (next != goal) {
                const int key = runtimeCellKey(next.x, next.y);
                if (key < 0 || visited.find(key) != visited.end()) {
                    continue;
                }
            }
            if (!runtimeCellTransitionIsPassable(
                    state,
                    *terrainByCell,
                    *occupiedByCell,
                    current,
                    next,
                    mover,
                    allowedOccupiedObjectId
                )) {
                continue;
            }

            const double score =
                runtimeCellDistance(next, goal)
                + (std::abs(goal.x - next.x) + std::abs(goal.y - next.y))
                    * 0.02;
            if (!found || score < bestScore) {
                best = next;
                bestScore = score;
                found = true;
            }
        }

        if (!found) {
            return !path.empty()
                    && runtimeCellDistance(current, goal) < startDistance
                ? path
                : std::vector<RuntimeCellPoint>{};
        }

        current = best;
        path.push_back(current);
        visited.insert(runtimeCellKey(current.x, current.y));
        if (current == goal) {
            return path;
        }
    }

    return !path.empty() && runtimeCellDistance(current, goal) < startDistance
        ? path
        : std::vector<RuntimeCellPoint>{};
}

std::vector<RuntimeCellPoint> findRuntimeGreedyPath(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds,
    int maxSteps,
    const std::unordered_map<int, int>* occupiedByCellOverride) {
    const auto pathStart = std::chrono::steady_clock::now();
    std::vector<RuntimeCellPoint> path =
        findRuntimeGreedyPathUntimed(
            state,
            mover,
            start,
            goal,
            allowedOccupiedObjectId,
            ignoredObjectIds,
            maxSteps,
            occupiedByCellOverride
        );
    if (!path.empty()) {
        ++state.perf.lastSimPathRequests;
    }
    state.perf.lastSimPathMs +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pathStart
        ).count();
    return path;
}

std::vector<RuntimeCellPoint> findRuntimePath(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds) {
    return findRuntimePathUntimed(
        state,
        mover,
        start,
        goal,
        allowedOccupiedObjectId,
        ignoredObjectIds,
        nullptr
    );
}

RuntimeQueuedPathResult resolveRuntimePathTowardRange(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& mover,
    const WorldObjectInstance& target,
    double rangeCells,
    const std::unordered_set<int>* ignoredObjectIds,
    bool forceFire) {
    const RuntimeCellPoint start = runtimeCellPointForObject(mover);
    const RuntimeWeaponProfile approachWeaponProfile =
        runtimeWeaponProfileForObject(state, mover, &target, forceFire);
    if (rangeCells == 0.0
        && runtimeKirovBuildingBombProfile(mover, target, approachWeaponProfile)) {
        // The stock firing radius does not choose this aircraft's stopping point.
        // Jumpjet segments have integer endpoints. The existing rounded original
        // foundation center is the nearest expressible overhead cell.
        RuntimeQueuedPathResult result;
        result.resolvedGoal = runtimeTargetCellForProjectile(state, target);
        if (start == result.resolvedGoal) {
            result.reachable = true;
            return result;
        }
        const auto started = std::chrono::steady_clock::now();
        result.path = findRuntimeAirPath(state, mover, start, result.resolvedGoal);
        result.reachable = !result.path.empty();
        result.realNoPath = !result.reachable;
        ++state.perf.lastSimPathRequests;
        state.perf.lastSimPathMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return result;
    }
    const bool currentLineOfSight = approachWeaponProfile.weapon == nullptr
        || runtimeWeaponHasLineOfSight(
            state,
            mover,
            target,
            *approachWeaponProfile.weapon
        );
    const bool cellRangefinding = approachWeaponProfile.weapon != nullptr
        && approachWeaponProfile.weapon->cellRangefinding
        && !approachWeaponProfile.weapon->drainWeapon;
    if (runtimeAttackRangeDistanceFromCell(
            state,
            start,
            target,
            cellRangefinding
        ) <= rangeCells
        && (approachWeaponProfile.weapon == nullptr
            || runtimeWeaponDistanceToTarget(state, mover, target, *approachWeaponProfile.weapon)
                + 0.001 >= approachWeaponProfile.weapon->minimumRange)
        && currentLineOfSight) {
        RuntimeQueuedPathResult reached;
        reached.reachable = true;
        reached.resolvedGoal = start;
        return reached;
    }

    const RuntimeAttackApproachCacheEntry& approachEntry =
        runtimeAttackApproachEntry(
            state,
            target,
            rangeCells,
            cellRangefinding
        );
    // Ground approach rings omit water. Ships and amphibious vehicles
    // need the complete range ring, then their own movement-domain filter.
    // Aircraft also include overhead positions for vertical bombs.
    std::vector<RuntimeCellPoint> domainApproachCells;
    const bool customDomain = runtimeObjectMovementMode(state, mover) != PathMovementMode::Ground;
    if (customDomain) {
        const auto footprint = runtimeStructureFootprintForObject(state, target);
        const int radius = std::clamp(static_cast<int>(std::ceil(rangeCells)), 1, 512);
        const int minX = std::max(0, target.x - radius);
        const int minY = std::max(0, target.y - radius);
        const int maxX = std::min(511, target.x + std::max(1, footprint.width) - 1 + radius);
        const int maxY = std::min(511, target.y + std::max(1, footprint.height) - 1 + radius);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const RuntimeCellPoint point{x, y};
                const double distance = runtimeAttackRangeDistanceFromCell(state, point, target, cellRangefinding);
                if (distance <= rangeCells + 0.001 && (distance > 0.001 || rangeCells < 1.0)) {
                    domainApproachCells.push_back(point);
                }
            }
        }
    }
    const auto& approachCells = customDomain ? domainApproachCells : approachEntry.candidates;
    std::vector<RuntimeCellPoint> candidates;
    candidates.reserve(std::min<std::size_t>(approachCells.size(), 8));
    const std::unordered_map<int, int> occupiedByCell =
        buildRuntimeOccupancyLookup(state, mover.instanceId, nullptr, ignoredObjectIds);
    for (const RuntimeCellPoint& point : approachCells) {
        if (point == start
            || !runtimeCellIsPassable(
                state,
                state.terrainByCell,
                occupiedByCell,
                point,
                mover
            )) {
            continue;
        }
        if (approachWeaponProfile.weapon != nullptr) {
            WorldObjectInstance firingPosition = mover;
            clearRuntimeRenderPosition(firingPosition);
            firingPosition.x = point.x;
            firingPosition.y = point.y;
            firingPosition.cell = runtimeCellKey(point.x, point.y);
            const auto terrainIt = state.terrainByCell.find(
                firingPosition.cell
            );
            if (!firingPosition.airborne && terrainIt != state.terrainByCell.end()
                && terrainIt->second != nullptr) {
                firingPosition.level = runtimeEffectiveTerrainLevel(
                    *terrainIt->second
                );
                firingPosition.onBridge = runtimeCachedTerrainInfoForCell(
                    state,
                    *terrainIt->second
                ).bridge;
            }
            if (runtimeWeaponDistanceToTarget(state, firingPosition, target, *approachWeaponProfile.weapon)
                    + 0.001 < approachWeaponProfile.weapon->minimumRange) {
                continue;
            }
            if (!runtimeWeaponHasLineOfSight(
                    state,
                    firingPosition,
                    target,
                    *approachWeaponProfile.weapon
                )) {
                continue;
            }
        }
        candidates.push_back(point);
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [&state, &start, &target, cellRangefinding](
            const RuntimeCellPoint& left,
            const RuntimeCellPoint& right
        ) {
            const double leftScore =
                runtimeCellDistance(start, left)
                + runtimeAttackRangeDistanceFromCell(
                    state,
                    left,
                    target,
                    cellRangefinding
                );
            const double rightScore =
                runtimeCellDistance(start, right)
                + runtimeAttackRangeDistanceFromCell(
                    state,
                    right,
                    target,
                    cellRangefinding
                );
            if (leftScore != rightScore) {
                return leftScore < rightScore;
            }
            if (left.y != right.y) {
                return left.y < right.y;
            }
            return left.x < right.x;
        }
    );

    constexpr std::size_t maxAttackPathCandidatesPerUnit = 4;
    const auto behaviorIt = state.behaviors.find(mover.instanceId);
    const int approachSlotIndex =
        behaviorIt != state.behaviors.end()
            ? std::max(0, behaviorIt->second.approachSlotIndex)
            : 0;
    if (!candidates.empty() && approachSlotIndex > 0) {
        const std::size_t rotateCount =
            static_cast<std::size_t>(approachSlotIndex)
            % std::min<std::size_t>(candidates.size(), 24);
        std::rotate(
            candidates.begin(),
            candidates.begin() + static_cast<std::ptrdiff_t>(rotateCount),
            candidates.end()
        );
    }
    if (candidates.size() > maxAttackPathCandidatesPerUnit * 4U) {
        candidates.resize(maxAttackPathCandidatesPerUnit * 4U);
    }

    if (runtimeObjectMovementMode(state, mover) == PathMovementMode::Air) {
        RuntimeQueuedPathResult result;
        const auto started = std::chrono::steady_clock::now();
        for (std::size_t index = 0; index < std::min(candidates.size(), maxAttackPathCandidatesPerUnit); ++index) {
            auto path = findRuntimeAirPath(state, mover, start, candidates[index]);
            if (path.empty()) continue;
            result.reachable = true;
            result.resolvedGoal = candidates[index];
            result.path = std::move(path);
            break;
        }
        ++state.perf.lastSimPathRequests;
        state.perf.lastSimPathMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return result;
    }

    // A minimum-range weapon must not fall back to the target's adjacent
    // footprint when every valid firing position is blocked or outside the map.
    if (candidates.empty() && approachWeaponProfile.weapon != nullptr
        && approachWeaponProfile.weapon->minimumRange > 0.0) {
        RuntimeQueuedPathResult blocked;
        blocked.realNoPath = true;
        return blocked;
    }
    const std::vector<RuntimeCellPoint>& contextCells =
        candidates.empty() ? approachEntry.mapQueryCandidates : candidates;
    const PathApproachContext pathApproachContext =
        makeRuntimePathApproachContext(
            state,
            "attack",
            mover,
            target,
            contextCells,
            std::max<int>(
                4,
                std::min<int>(
                    32,
                    static_cast<int>(std::max<std::size_t>(
                        1U,
                        contextCells.size()
                    ))
                )
            )
        );
    if (!contextCells.empty()) {
        recordRuntimeMapQueryApproachContext(
            state,
            "attack",
            target,
            approachEntry.mapQueryCandidates.empty()
                ? contextCells
                : approachEntry.mapQueryCandidates,
            1
        );
    }
    state.attackApproachStats.submitted += 1;

    // The requested cell can be the launcher's own cell during force fire.
    // We already established that it is outside the legal firing interval.
    // Give the queue a valid firing goal so its start==goal fast paths cannot
    // report arrival before considering the minimum-range approach ring.
    RuntimeCellPoint queueGoal = runtimeCellPointForObject(target);
    if (queueGoal == start && !contextCells.empty()) queueGoal = contextCells.front();
    const RuntimeQueuedPathResult result =
        resolveRuntimePathWithQueue(
            state,
            mover,
            start,
            queueGoal,
            target.instanceId,
            ignoredObjectIds,
            pathApproachContext
        );
    return result;
}

std::optional<RuntimeCellPoint> findNearestRuntimePassableCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* reservedCells,
    const std::unordered_set<int>* ignoredObjectIds) {
    RuntimePerfTraceScope trace("findNearestRuntimePassableCell", state.tick);
    std::unordered_map<int, const TerrainCell*> generatedTerrainByCell;
    const std::unordered_map<int, const TerrainCell*>* terrainByCell =
        &state.terrainByCell;
    if (terrainByCell->empty()) {
        generatedTerrainByCell = buildRuntimeTerrainLookup(state.world);
        terrainByCell = &generatedTerrainByCell;
    }
    const std::unordered_map<int, int> occupiedByCell =
        buildRuntimeOccupancyLookup(state, mover.instanceId, nullptr, ignoredObjectIds);

    return findNearestRuntimePassableCellWithOccupancy(
        state,
        mover,
        desired,
        allowedOccupiedObjectId,
        reservedCells,
        *terrainByCell,
        occupiedByCell
    );
}

std::optional<RuntimeCellPoint> findNearestRuntimePassableCellWithOccupancy(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* reservedCells,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>* reachableCells,
    const std::unordered_map<int, int>* componentByCell,
    int requiredComponent,
    const RuntimeManualFormationMappedFilter* manualMappedFilter) {
    RuntimePerfTraceScope plannerTrace("nearestFormationDestination", state.tick);
    RuntimeBuildingDepartureReadOnlyQueryScope departureQueries(state, mover);
    const bool infantryQueries = mover.kind == WorldObjectKind::Infantry;
    RuntimeSubcellReadOnlyQueryScope occupancyQueries(state.world.getObjects(), infantryQueries,
        runtimeGroundPoseContext(state));
    RuntimePlannerInvisibleBuildingScope invisibleBuildings(state, infantryQueries);
    std::deque<RuntimeCellPoint> frontier;
    std::unordered_set<int> visited;
    frontier.push_back(desired);
    visited.insert(runtimeCellKey(desired.x, desired.y));

    constexpr int maxRadiusCells = 24;
    while (!frontier.empty() && visited.size() < 400) {
        const RuntimeCellPoint current = frontier.front();
        frontier.pop_front();

        if (std::abs(current.x - desired.x) <= maxRadiusCells
            && std::abs(current.y - desired.y) <= maxRadiusCells
            && (reservedCells == nullptr
                || reservedCells->find(runtimeCellKey(current.x, current.y))
                    == reservedCells->end())
            && (reachableCells == nullptr
                || reachableCells->find(runtimeCellKey(current.x, current.y))
                    != reachableCells->end())
            && (componentByCell == nullptr
                || requiredComponent < 0
                || (componentByCell->find(runtimeCellKey(current.x, current.y))
                    != componentByCell->end()
                    && componentByCell->find(runtimeCellKey(current.x, current.y))->second
                        == requiredComponent))
            && runtimeCellIsPassable(
                state,
                terrainByCell,
                occupiedByCell,
                current,
                mover,
                allowedOccupiedObjectId
            )
            && (manualMappedFilter == nullptr || manualMappedFilter->allows(current))) {
            return current;
        }

        for (const RuntimeCellPoint& next : runtimeNeighborsOf(current)) {
            if (std::abs(next.x - desired.x) > maxRadiusCells
                || std::abs(next.y - desired.y) > maxRadiusCells) {
                continue;
            }
            const int key = runtimeCellKey(next.x, next.y);
            if (key < 0 || visited.find(key) != visited.end()) {
                continue;
            }
            visited.insert(key);
            frontier.push_back(next);
        }
    }

    if (componentByCell != nullptr && requiredComponent >= 0) {
        std::optional<RuntimeCellPoint> bestCell;
        int bestScore = std::numeric_limits<int>::max();
        for (const auto& [key, component] : *componentByCell) {
            if (component != requiredComponent
                || (reservedCells != nullptr
                    && reservedCells->find(key) != reservedCells->end())) {
                continue;
            }
            const auto terrainIt = terrainByCell.find(key);
            if (terrainIt == terrainByCell.end()
                || terrainIt->second == nullptr) {
                continue;
            }
            const RuntimeCellPoint candidate{
                terrainIt->second->x,
                terrainIt->second->y
            };
            if (!runtimeCellIsPassable(
                    state,
                    terrainByCell,
                    occupiedByCell,
                    candidate,
                    mover,
                    allowedOccupiedObjectId
                )
                || (manualMappedFilter != nullptr && !manualMappedFilter->allows(candidate))) {
                continue;
            }
            const int dx = std::abs(candidate.x - desired.x);
            const int dy = std::abs(candidate.y - desired.y);
            const int score = (std::max(dx, dy) * 1000) + dx + dy;
            if (!bestCell.has_value() || score < bestScore) {
                bestCell = candidate;
                bestScore = score;
            }
        }
        if (bestCell.has_value()) {
            return bestCell;
        }
    }

    return std::nullopt;
}

std::optional<RuntimeCellPoint> findBestRuntimeFormationTargetCellWithOccupancyImpl(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* reservedCells,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>* reachableCells,
    const std::unordered_map<int, int>* componentByCell,
    int requiredComponent,
    bool pruneUncompetitiveCandidates,
    const RuntimeManualFormationMappedFilter* manualMappedFilter,
    const std::unordered_set<int>* certifiedUnmappedKeys) {
    RuntimePerfTraceScope plannerTrace("bestFormationDestination", state.tick);
    RuntimeBuildingDepartureReadOnlyQueryScope departureQueries(state, mover);
    const bool infantryQueries = mover.kind == WorldObjectKind::Infantry;
    RuntimeSubcellReadOnlyQueryScope occupancyQueries(state.world.getObjects(), infantryQueries,
        runtimeGroundPoseContext(state));
    RuntimePlannerInvisibleBuildingScope invisibleBuildings(state, infantryQueries);
    std::deque<RuntimeCellPoint> frontier;
    std::unordered_set<int> visited;
    frontier.push_back(desired);
    visited.insert(runtimeCellKey(desired.x, desired.y));

    std::optional<RuntimeCellPoint> bestCell;
    int bestScore = std::numeric_limits<int>::max();
    constexpr int maxRadiusCells = 16;
    while (!frontier.empty() && visited.size() < 320) {
        const RuntimeCellPoint current = frontier.front();
        frontier.pop_front();
        const int currentKey = runtimeCellKey(current.x, current.y);

        const int dx = std::abs(current.x - desired.x);
        const int dy = std::abs(current.y - desired.y);
        const int distanceScore = (std::max(dx, dy) * 500) + ((dx + dy) * 30);
        if (dx <= maxRadiusCells
            && dy <= maxRadiusCells
            // Every remaining score term is nonnegative and ties keep the
            // earlier candidate. Preserve BFS visitation and its exact cap,
            // but avoid full-world infantry probes for cells that cannot win.
            && (!pruneUncompetitiveCandidates || !bestCell.has_value()
                || distanceScore < bestScore)
            && (reservedCells == nullptr
                || reservedCells->find(currentKey) == reservedCells->end())
            && (reachableCells == nullptr
                || reachableCells->find(currentKey) != reachableCells->end())
            && (componentByCell == nullptr
                || requiredComponent < 0
                || (componentByCell->find(currentKey) != componentByCell->end()
                    && componentByCell->find(currentKey)->second
                        == requiredComponent))
            // This optional proof is consulted at the ORIGINAL component-filter
            // point only. Negative requiredComponent keeps its original bypass.
            && (certifiedUnmappedKeys==nullptr || requiredComponent<0
                || !certifiedUnmappedKeys->contains(currentKey))
            && runtimeCellIsPassable(
                state,
                terrainByCell,
                occupiedByCell,
                current,
                mover,
                allowedOccupiedObjectId
            )
            && (manualMappedFilter == nullptr || manualMappedFilter->allows(current))) {
            const int softPenalty =
                runtimeMoveTargetSoftPenalty(
                    state,
                    terrainByCell,
                    occupiedByCell,
                    current
                );
            std::uint64_t tieBreak = runtimeStableHashString("moveSlot");
            runtimeStableHashMix(tieBreak, mover.instanceId);
            runtimeStableHashMix(tieBreak, current.x);
            runtimeStableHashMix(tieBreak, current.y);
            const int score =
                distanceScore
                + softPenalty
                + static_cast<int>(tieBreak & 31U);
            if (!bestCell.has_value() || score < bestScore) {
                bestCell = current;
                bestScore = score;
            }
        }

        for (const RuntimeCellPoint& next : runtimeNeighborsOf(current)) {
            if (std::abs(next.x - desired.x) > maxRadiusCells
                || std::abs(next.y - desired.y) > maxRadiusCells) {
                continue;
            }
            const int key = runtimeCellKey(next.x, next.y);
            if (key < 0 || visited.find(key) != visited.end()) {
                continue;
            }
            visited.insert(key);
            frontier.push_back(next);
        }
    }

    return bestCell;
}

std::optional<RuntimeCellPoint> findBestRuntimeFormationTargetCellWithOccupancy(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* reservedCells,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>* reachableCells,
    const std::unordered_map<int, int>* componentByCell,
    int requiredComponent,
    const RuntimeManualFormationMappedFilter* manualMappedFilter,
    const std::unordered_set<int>* certifiedUnmappedKeys) {
    static const bool useReference = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return findBestRuntimeFormationTargetCellWithOccupancyImpl(state, mover,
        desired, allowedOccupiedObjectId, reservedCells, terrainByCell,
        occupiedByCell, reachableCells, componentByCell, requiredComponent, !useReference,
        manualMappedFilter,certifiedUnmappedKeys);
}

bool runtimeComponentKernelReference() {
    if (runtimeComponentKernelReferenceOverride >= 0)
        return runtimeComponentKernelReferenceOverride != 0;
    static const bool reference = [] {
        const char* value=std::getenv("CONCORD_COMPONENT_KERNEL_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}

std::optional<RuntimeCellPoint> findRuntimePassableCellNear(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    int maxRadiusCells,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* reservedCells,
    const std::unordered_set<int>* ignoredObjectIds,
    const std::unordered_map<int, int>* occupiedByCellOverride,
    std::optional<PathMovementMode> movementModeOverride) {
    std::unordered_map<int, const TerrainCell*> generatedTerrainByCell;
    const std::unordered_map<int, const TerrainCell*>* terrainByCell =
        &state.terrainByCell;
    if (terrainByCell->empty()) {
        generatedTerrainByCell = buildRuntimeTerrainLookup(state.world);
        terrainByCell = &generatedTerrainByCell;
    }
    const bool canUseOccupiedOverride =
        occupiedByCellOverride != nullptr
        && ignoredObjectIds == nullptr
        && allowedOccupiedObjectId < 0;
    std::unordered_map<int, int> generatedOccupiedByCell;
    const std::unordered_map<int, int>* occupiedByCell =
        occupiedByCellOverride;
    if (!canUseOccupiedOverride) {
        generatedOccupiedByCell =
            buildRuntimeOccupancyLookup(state, mover.instanceId, nullptr, ignoredObjectIds);
        occupiedByCell = &generatedOccupiedByCell;
    }
    const int allowedOccupiedIdForProbe =
        canUseOccupiedOverride ? mover.instanceId : allowedOccupiedObjectId;

    const auto canUse = [&](const RuntimeCellPoint& candidate) {
        const int key = runtimeCellKey(candidate.x, candidate.y);
        return key >= 0
            && (reservedCells == nullptr
                || reservedCells->find(key) == reservedCells->end())
            && runtimeCellIsPassable(
                state,
                *terrainByCell,
                *occupiedByCell,
                candidate,
                mover,
                allowedOccupiedIdForProbe,
                nullptr,
                false,
                movementModeOverride
            );
    };

    if (canUse(desired)) {
        return desired;
    }

    for (int radius = 1; radius <= std::max(0, maxRadiusCells); ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) != radius && std::abs(dy) != radius) {
                    continue;
                }
                const RuntimeCellPoint candidate{
                    desired.x + dx,
                    desired.y + dy
                };
                if (canUse(candidate)) {
                    return candidate;
                }
            }
        }
    }

    return std::nullopt;
}

void ensureRuntimeSubcellsInitialized(RuntimeGameSessionState& state) {
    if (state.subcellsInitialized) {
        return;
    }
    ensureRuntimeGatesInitialized(state);
    state.subcells.clearReservations();
    state.subcellsInitialized = true;
    state.subcells.reconcile(
        state.world.accessObjects(),
        state.world.getTerrainObjects(),
        runtimeGateSubcellContext(state)
    );
}

void invalidateRuntimeSubcellPathState(RuntimeGameSessionState& state) {
    state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();
    state.pathFailureCache.clear();
}

void invalidateRuntimeSubcellDynamicPathState(RuntimeGameSessionState& state) {
    state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();
    state.attackApproachCache.clear();
}

bool runtimeSubcellReservationsEqual(
    const std::map<int, RuntimeSubcellReservation>& left,
    const std::map<int, RuntimeSubcellReservation>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    auto leftIt = left.begin();
    auto rightIt = right.begin();
    while (leftIt != left.end()) {
        const RuntimeSubcellReservation& a = leftIt->second;
        const RuntimeSubcellReservation& b = rightIt->second;
        if (leftIt->first != rightIt->first
            || a.moverObjectId != b.moverObjectId
            || a.destination.x != b.destination.x
            || a.destination.y != b.destination.y
            || a.destination.cell != b.destination.cell
            || a.destination.onBridge != b.destination.onBridge
            || a.subCell != b.subCell) {
            return false;
        }
        ++leftIt;
        ++rightIt;
    }
    return true;
}

bool runtimeInfantryUsesGroundSubcells(const WorldObjectInstance& object) {
    // Match the subcell system's ground occupancy domain. Flying infantry
    // cannot reserve ground slots and must use the normal air cell update.
    return object.kind == WorldObjectKind::Infantry && !object.airborne;
}

void assignRuntimeInfantrySubcellAtCurrentLocation(
    RuntimeGameSessionState& state,
    WorldObjectInstance& object) {
    if (!runtimeInfantryUsesGroundSubcells(object)
        || !object.alive
        || object.health <= 0) {
        object.subCell = -1;
        return;
    }
    ensureRuntimeSubcellsInitialized(state);
    const RuntimeSubcellLocation location{
        object.x,
        object.y,
        object.cell,
        object.onBridge
    };
    const std::vector<int> available = state.subcells.availableSubCells(
        state.world.getObjects(),
        state.world.getTerrainObjects(),
        location,
        {object.instanceId},
        runtimeGateSubcellContext(
            state,
            &object,
            RuntimeGatePassageMode::Commit
        )
    );
    int selected = -1;
    const auto reserved = state.subcells.reservations().find(
        object.instanceId
    );
    if (reserved != state.subcells.reservations().end()
        && reserved->second.destination.x == location.x
        && reserved->second.destination.y == location.y
        && reserved->second.destination.onBridge == location.onBridge
        && std::find(
            available.begin(),
            available.end(),
            reserved->second.subCell
        ) != available.end()) {
        selected = reserved->second.subCell;
    } else if (RuntimeSubcellSystemState::isActiveSubCell(object.subCell)
        && std::find(available.begin(), available.end(), object.subCell)
            != available.end()) {
        selected = object.subCell;
    } else if (!available.empty()) {
        selected = available.front();
    }
    if (object.subCell != selected) {
        object.subCell = selected;
        invalidateRuntimeSubcellDynamicPathState(state);
    }
}

void releaseRuntimeInfantrySubcellReservation(
    RuntimeGameSessionState& state,
    int objectId) {
    if (state.subcells.release(objectId)) {
        invalidateRuntimeSubcellDynamicPathState(state);
    }
}

bool runtimeMoveWillCompleteBeforeSubcellStep(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    return runtimeMoveCanFinishAtCrowdedDestination(state, object, behavior);
}

bool runtimeAttackWillUseSubcellStep(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    std::optional<WorldObjectInstance> bridgeTarget;
    const WorldObjectInstance* target = nullptr;
    if (behavior.attackGround) {
        bridgeTarget = runtimeAttackGroundTargetObject(state, behavior);
        if (bridgeTarget.has_value()) {
            target = &*bridgeTarget;
        }
    } else if (behavior.attackBridge) {
        bridgeTarget = runtimeBridgeAttackTargetObject(
            state,
            behavior.attackBridgeCell,
            0
        );
        if (bridgeTarget.has_value()) {
            target = &*bridgeTarget;
        }
    } else {
        target = findObjectById(state, behavior.targetObjectId);
    }
    if (target == nullptr || !target->alive) {
        return false;
    }
    const RuntimeWeaponProfile profile =
        runtimeWeaponProfileForObject(state, object, target, behavior.forceFire);
    if (profile.weapon == nullptr || !runtimeWeaponHasAttackRange(*profile.weapon)
        || !runtimeObjectCanMove(state, object)) return false;
    const double distance = runtimeWeaponDistanceToTarget(state, object, *target, *profile.weapon);
    return distance > runtimeEffectiveWeaponRange(state, object, *target, *profile.weapon)
        || (!behavior.autoAcquired && !behavior.returnToDeployed
            && distance + 0.001 < profile.weapon->minimumRange);
}

RuntimeSubcellReconcileReport reconcileRuntimeSubcells(
    RuntimeGameSessionState& state,
    bool relocateInvalidInfantry) {
    RuntimePerfTraceScope stageTrace("reconcileRuntimeSubcells", state.tick);
    ensureRuntimeSubcellsInitialized(state);
    RuntimeSubcellReconcileReport report = state.subcells.reconcile(
        state.world.accessObjects(),
        state.world.getTerrainObjects(),
        runtimeGateSubcellContext(state)
    );
    if (!relocateInvalidInfantry || report.needsMoveOut == 0) {
        if (report.reassignedInfantry > 0
            || report.droppedReservations > 0) {
            invalidateRuntimeSubcellPathState(state);
            invalidateRuntimeTickOccupancyCache(state);
        }
        return report;
    }

    bool relocatedInfantry = false;
    for (const RuntimeSubcellReconcileAction& action : report.actions) {
        if (action.kind != RuntimeSubcellReconcileActionKind::NeedsMoveOut) {
            continue;
        }
        WorldObjectInstance* object = nullptr;
        for (WorldObjectInstance& candidate : state.world.accessObjects()) {
            if (candidate.instanceId == action.objectId) {
                object = &candidate;
                break;
            }
        }
        if (object == nullptr || !object->alive
            || object->kind != WorldObjectKind::Infantry) {
            continue;
        }
        const auto entryBehavior = state.behaviors.find(action.objectId);
        if (entryBehavior != state.behaviors.end()
            && entryBehavior->second.kind == RuntimeBehaviorKind::Move
            && entryBehavior->second.pendingAllowedOccupiedObjectId >= 0
            && runtimeCellIsAllowedOccupiedStructureFootprint(
                state,
                runtimeCellPointForObject(*object),
                entryBehavior->second.pendingAllowedOccupiedObjectId,
                object
            )) {
            // Engineer entry intentionally spends its render/cooldown phase
            // inside the target footprint without a ground subcell.  The
            // entry completion consumes it once movement settles; relocating
            // here would restart the approach forever.
            continue;
        }
        const bool enteringGrinder = std::any_of(state.grinding.relations().begin(), state.grinding.relations().end(),
            [&](const RuntimeGrindingRelation& relation) {
                return relation.sourceId == action.objectId && relation.phase == RuntimeGrindingRelationPhase::Entering
                    && runtimeCellIsAllowedOccupiedStructureFootprint(state,
                        runtimeCellPointForObject(*object), relation.hostId);
            });
        if (enteringGrinder) continue;
        const auto slave = state.slaveMinerTasks.find(action.objectId);
        if (slave != state.slaveMinerTasks.end()
            && runtimeCellIsAllowedOccupiedStructureFootprint(
                state, runtimeCellPointForObject(*object), slave->second.refineryId, object)) {
            // Delivery runs on the economy step after arrival. Keep workers at
            // their own refinery until they unload and walk back out; otherwise
            // reconciliation teleports them away before they can deliver.
            continue;
        }
        const std::optional<RuntimeCellPoint> destination =
            findRuntimePassableCellNear(
                state,
                *object,
                {object->x, object->y},
                6
            );
        if (!destination.has_value()
            || (destination->x == object->x
                && destination->y == object->y)) {
            continue;
        }
        object->x = destination->x;
        object->y = destination->y;
        object->cell = runtimeCellKey(destination->x, destination->y);
        const auto terrain = state.terrainByCell.find(object->cell);
        if (terrain != state.terrainByCell.end()
            && terrain->second != nullptr) {
            object->level = runtimeEffectiveTerrainLevel(*terrain->second);
        }
        object->onBridge = runtimeCellIsBridge(
            state,
            destination->x,
            destination->y
        );
        clearRuntimeRenderPosition(*object);
        assignRuntimeInfantrySubcellAtCurrentLocation(state, *object);
        relocatedInfantry = true;
    }
    if (!relocatedInfantry) {
        return report;
    }
    RuntimeSubcellReconcileReport afterRelocation =
        state.subcells.reconcile(
            state.world.accessObjects(),
            state.world.getTerrainObjects(),
            runtimeGateSubcellContext(state)
        );
    report.retainedInfantry += afterRelocation.retainedInfantry;
    report.reassignedInfantry += afterRelocation.reassignedInfantry;
    report.needsMoveOut = afterRelocation.needsMoveOut;
    report.droppedReservations += afterRelocation.droppedReservations;
    report.actions.insert(
        report.actions.end(),
        afterRelocation.actions.begin(),
        afterRelocation.actions.end()
    );
    invalidateRuntimeSubcellPathState(state);
    invalidateRuntimeTickOccupancyCache(state);
    return report;
}

WorldObjectInstance* findRuntimePathQueueObject(
    RuntimeGameSessionState& state,
    int objectId) {
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (object.alive && object.instanceId == objectId) {
            return &object;
        }
    }

    return nullptr;
}

std::uint64_t runtimePathFailureKey(
    const RuntimeGameSessionState& state,
    const std::string& purpose,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& goal,
    int targetObjectId,
    double rangeCells) {
    std::uint64_t hash = runtimeStableHashString("pathFailure");
    runtimeStableHashMixString(hash, purpose);
    runtimeStableHashMixString(hash, mover.type);
    runtimeStableHashMix(hash, static_cast<int>(mover.kind));
    runtimeStableHashMix(hash, mover.instanceId);
    const RuntimeCellPoint start = runtimeCellPointForObject(mover);
    runtimeStableHashMix(hash, start.x);
    runtimeStableHashMix(hash, start.y);
    runtimeStableHashMix(
        hash,
        static_cast<int>(runtimeObjectMovementMode(state, mover))
    );
    runtimeStableHashMix(hash, goal.x);
    runtimeStableHashMix(hash, goal.y);
    runtimeStableHashMix(hash, targetObjectId);
    runtimeStableHashMix(hash, runtimeAttackApproachRangeBucket(rangeCells));
    return hash;
}

std::uint64_t runtimePathFailureKeyForPending(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeObjectBehavior& behavior) {
    return runtimePathFailureKey(
        state,
        behavior.pendingPathTowardRange ? "attack" : "move",
        mover,
        behavior.pendingPathGoal,
        behavior.pendingPathTowardRange ? behavior.pendingTargetObjectId : -1,
        behavior.pendingPathTowardRange ? behavior.pendingRangeCells : 0.0
    );
}

int runtimePathFailureCooldownTicks(int failureCount) {
    return runtimeRepathCooldownTicks
        * std::min(12, std::max(1, failureCount * 2));
}

bool runtimePathFailureCooldownActive(
    RuntimeGameSessionState& state,
    std::uint64_t key,
    RuntimeObjectBehavior& behavior) {
    const auto found = state.pathFailureCache.find(key);
    if (found == state.pathFailureCache.end()) {
        return false;
    }
    if (state.tick >= found->second.nextAllowedTick) {
        return false;
    }

    ++state.pathFailureStats.cooldownHits;
    behavior.state = RuntimeBehaviorState::Blocked;
    behavior.nextRepathTick =
        std::max(behavior.nextRepathTick, found->second.nextAllowedTick);
    return true;
}

void rememberRuntimePathFailure(
    RuntimeGameSessionState& state,
    std::uint64_t key,
    RuntimeObjectBehavior& behavior) {
    RuntimePathFailureCacheEntry& entry = state.pathFailureCache[key];
    if (entry.failureCount > 0) {
        ++state.pathFailureStats.repeatedFailures;
    } else {
        entry.firstTick = state.tick;
    }
    ++entry.failureCount;
    entry.lastTick = state.tick;
    entry.nextAllowedTick =
        state.tick + runtimePathFailureCooldownTicks(entry.failureCount);
    ++state.pathFailureStats.recordedFailures;
    state.pathFailureStats.activeEntries =
        static_cast<int>(state.pathFailureCache.size());
    behavior.nextRepathTick =
        std::max(behavior.nextRepathTick, entry.nextAllowedTick);
}

void clearRuntimePathFailure(
    RuntimeGameSessionState& state,
    std::uint64_t key) {
    state.pathFailureCache.erase(key);
    state.pathFailureStats.activeEntries =
        static_cast<int>(state.pathFailureCache.size());
}

bool sameRuntimeMovePathRequest(
    const RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    bool allowNearbyTarget,
    const std::unordered_set<int>* ignoredObjectIds,
    std::optional<RuntimeCellPoint> sharedGoal) {
    if (!behavior.pathRequestPending
        || behavior.pendingPathTowardRange
        || behavior.pendingPathGoal != goal
        || behavior.pendingHasSharedGoal != sharedGoal.has_value()
        || (sharedGoal.has_value()
            && behavior.pendingSharedGoal != *sharedGoal)
        || behavior.pendingAllowedOccupiedObjectId != allowedOccupiedObjectId
        || behavior.pendingAllowNearbyTarget != allowNearbyTarget) {
        return false;
    }

    if (ignoredObjectIds == nullptr) {
        return behavior.pendingIgnoredObjectIds.empty();
    }
    return behavior.pendingIgnoredObjectIds == *ignoredObjectIds;
}

void submitRuntimeMovePathRequest(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal,
    int allowedOccupiedObjectId,
    const std::unordered_set<int>* ignoredObjectIds,
    bool allowNearbyTarget,
    int delayTicks,
    std::optional<RuntimeCellPoint> sharedGoal) {
    if (sameRuntimeMovePathRequest(
            behavior,
            goal,
            allowedOccupiedObjectId,
            allowNearbyTarget,
            ignoredObjectIds,
            sharedGoal
        )) {
        return;
    }

    const std::uint64_t failureKey =
        runtimePathFailureKey(
            state,
            "move",
            object,
            goal,
            allowedOccupiedObjectId,
            0.0
        );
    if (runtimePathFailureCooldownActive(state, failureKey, behavior)) {
        state.simulationStatus =
            "path failure cooldown #"
            + std::to_string(object.instanceId);
        return;
    }

    const bool wasPending = behavior.pathRequestPending;
    behavior.pathRequestPending = true;
    behavior.pendingPathTowardRange = false;
    behavior.pendingPathStart = start;
    behavior.pendingPathGoal = goal;
    behavior.pendingHasSharedGoal = sharedGoal.has_value();
    behavior.pendingSharedGoal =
        sharedGoal.has_value() ? *sharedGoal : RuntimeCellPoint{};
    behavior.pendingAllowedOccupiedObjectId = allowedOccupiedObjectId;
    behavior.pendingTargetObjectId = -1;
    behavior.pendingRangeCells = 0.0;
    behavior.pendingAllowNearbyTarget = allowNearbyTarget;
    behavior.pendingIgnoredObjectIds.clear();
    if (ignoredObjectIds != nullptr) {
        behavior.pendingIgnoredObjectIds = *ignoredObjectIds;
    }
    behavior.nextRepathTick =
        std::max(behavior.nextRepathTick, state.tick + std::max(0, delayTicks));
    behavior.state = RuntimeBehaviorState::Queued;

    if (!wasPending) {
        state.pendingPathObjectIds.push_back(object.instanceId);
    }
}

void submitRuntimeAttackPathRequest(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const WorldObjectInstance& target,
    double rangeCells,
    int delayTicks,
    const std::unordered_set<int>* ignoredObjectIds) {
    const bool samePending =
        behavior.pathRequestPending
        && behavior.pendingPathTowardRange
        && behavior.pendingTargetObjectId == target.instanceId
        && std::abs(behavior.pendingRangeCells - rangeCells) < 0.001;
    if (samePending) {
        return;
    }

    const std::uint64_t failureKey =
        runtimePathFailureKey(
            state,
            "attack",
            object,
            runtimeCellPointForObject(target),
            target.instanceId,
            rangeCells
        );
    if (runtimePathFailureCooldownActive(state, failureKey, behavior)) {
        state.simulationStatus =
            "attack path failure cooldown #"
            + std::to_string(object.instanceId);
        return;
    }

    const bool wasPending = behavior.pathRequestPending;
    behavior.pathRequestPending = true;
    behavior.pendingPathTowardRange = true;
    behavior.pendingPathStart = runtimeCellPointForObject(object);
    behavior.pendingPathGoal = runtimeCellPointForObject(target);
    behavior.pendingHasSharedGoal = false;
    behavior.pendingSharedGoal = RuntimeCellPoint{};
    behavior.pendingAllowedOccupiedObjectId = target.instanceId;
    behavior.pendingTargetObjectId = target.instanceId;
    behavior.pendingRangeCells = rangeCells;
    behavior.pendingAllowNearbyTarget = false;
    behavior.pendingIgnoredObjectIds.clear();
    if (ignoredObjectIds != nullptr) {
        behavior.pendingIgnoredObjectIds = *ignoredObjectIds;
    }
    behavior.nextRepathTick =
        std::max(behavior.nextRepathTick, state.tick + std::max(0, delayTicks));
    behavior.state = RuntimeBehaviorState::Chasing;

    if (!wasPending) {
        state.pendingPathObjectIds.push_back(object.instanceId);
    }
}

bool applyRuntimeMovePathResult(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,
    const RuntimeQueuedPathResult& queuedPath) {
    const std::unordered_set<int> ignoredObjectIds =
        behavior.pendingIgnoredObjectIds;
    const RuntimeCellPoint current = runtimeCellPointForObject(object);
    const std::uint64_t failureKey =
        runtimePathFailureKeyForPending(state, object, behavior);
    if (queuedPath.goalAdjusted
        && queuedPath.resolvedGoal != behavior.moveTarget
        && (runtimeFactoryDeliveryOwnsEgress(state, object.instanceId)
            || runtimeCloneDeliveryOwnsDoorway(state, object.instanceId)
            || runtimeOmniCrushOwnsOccupiedEndpoint(state, object, behavior.moveTarget)
            || state.pendingOccupyTargets.contains(object.instanceId)
            || runtimeUnitTransportApproachAllowsBodyDetour(state, object, behavior)
            || std::any_of(state.grinding.relations().begin(), state.grinding.relations().end(),
                [&](const RuntimeGrindingRelation& entry) { return entry.sourceId == object.instanceId; }))) {
        // The path queue supplies a blocked-goal fallback even when no
        // alternates were requested. Preserve the actual doorway / entry
        // destination while another soldier finishes passing through it.
        behavior.path.clear();
        behavior.state = RuntimeBehaviorState::Blocked;
        ++behavior.blockedTicks;
        behavior.nextRepathTick = state.tick + runtimeRepathCooldownTicks;
        state.simulationStatus = "waiting for structure passage #"
            + std::to_string(object.instanceId);
        return false;
    }
    if (queuedPath.reachable
        || current == behavior.pendingPathGoal
        || (queuedPath.budgetExhausted && !queuedPath.path.empty())) {
        replaceRuntimeBehaviorPath(behavior, queuedPath.path);
        if (queuedPath.reachable
            && queuedPath.goalAdjusted
            && !queuedPath.sharedApproachHit) {
            behavior.moveTarget = queuedPath.resolvedGoal;
        }
        behavior.blockedTicks = 0;
        behavior.state = RuntimeBehaviorState::Queued;
        clearRuntimePathFailure(state, failureKey);
        if (queuedPath.budgetExhausted && !queuedPath.reachable) {
            behavior.nextRepathTick =
                std::min(behavior.nextRepathTick, state.tick + 1);
            state.simulationStatus =
                "partial path #"
                + std::to_string(object.instanceId);
        }
        return true;
    }

    ++behavior.blockedTicks;
    behavior.state = RuntimeBehaviorState::Blocked;
    if (queuedPath.budgetExhausted) {
        behavior.nextRepathTick =
            std::max(
                behavior.nextRepathTick,
                state.tick + (runtimeRepathCooldownTicks * 2)
            );
        state.simulationStatus =
            "path request budget exhausted #"
            + std::to_string(object.instanceId);
        return false;
    }

    const int cooldownScale =
        ignoredObjectIds.size() > 8U
            ? 16
            : std::min(4, std::max(1, behavior.blockedTicks));
    behavior.nextRepathTick =
        std::max(
            behavior.nextRepathTick,
            state.tick
                + runtimeRepathCooldownTicks
                    * cooldownScale
        );
    rememberRuntimePathFailure(state, failureKey, behavior);
    state.simulationStatus =
        "path request blocked #" + std::to_string(object.instanceId);
    return false;
}

bool completeRuntimeMovePathRequest(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior) {
    RuntimeCellPoint goal = behavior.pendingPathGoal;
    const int allowedOccupiedObjectId = behavior.pendingAllowedOccupiedObjectId;
    const bool allowNearbyTarget = behavior.pendingAllowNearbyTarget;
    const std::unordered_set<int> ignoredObjectIds =
        behavior.pendingIgnoredObjectIds;
    const std::unordered_set<int>* ignoredPtr =
        ignoredObjectIds.empty() ? nullptr : &ignoredObjectIds;
    const RuntimeCellPoint current = runtimeCellPointForObject(object);
    const std::uint64_t failureKey =
        runtimePathFailureKeyForPending(state, object, behavior);

    if (runtimePathFailureCooldownActive(state, failureKey, behavior)) {
        state.simulationStatus =
            "path failure cooldown #"
            + std::to_string(object.instanceId);
        return false;
    }

    const bool exactMoveTargetRequired =
        state.pendingEngineerCaptures.find(object.instanceId)
            != state.pendingEngineerCaptures.end()
        || state.pendingOccupyTargets.find(object.instanceId)
            != state.pendingOccupyTargets.end()
        || state.pendingTransportTargets.find(object.instanceId)
            != state.pendingTransportTargets.end()
        || std::any_of(state.grinding.relations().begin(), state.grinding.relations().end(),
            [&](const RuntimeGrindingRelation& entry) { return entry.sourceId == object.instanceId; });
    const PathApproachContext pathApproachContext =
        makeRuntimeMovePathApproachContext(
            state,
            object,
            goal,
            static_cast<int>(ignoredObjectIds.size()),
            allowNearbyTarget,
            exactMoveTargetRequired,
            !exactMoveTargetRequired && behavior.pendingHasSharedGoal
                ? std::optional<RuntimeCellPoint>(behavior.pendingSharedGoal)
                : std::nullopt
        );
    const RuntimeQueuedPathResult queuedPath =
        resolveRuntimePathWithQueue(
            state,
            object,
            current,
            goal,
            allowedOccupiedObjectId,
            ignoredPtr,
            pathApproachContext
        );
    return applyRuntimeMovePathResult(state, object, behavior, queuedPath);
}

bool completeRuntimeAttackPathRequest(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior) {
    std::optional<WorldObjectInstance> bridgeTarget;
    WorldObjectInstance* target = nullptr;
    if (behavior.attackGround) {
        bridgeTarget = runtimeAttackGroundTargetObject(state, behavior);
        if (bridgeTarget.has_value()) {
            target = &*bridgeTarget;
        }
    } else if (behavior.attackBridge) {
        bridgeTarget = runtimeBridgeAttackTargetObject(
            state,
            behavior.attackBridgeCell
        );
        if (bridgeTarget.has_value()) {
            target = &*bridgeTarget;
        }
    } else {
        target = findRuntimePathQueueObject(
            state,
            behavior.pendingTargetObjectId
        );
    }
    if (target == nullptr || !target->alive) {
        behavior.state = RuntimeBehaviorState::Complete;
        return true;
    }

    const std::uint64_t failureKey =
        runtimePathFailureKeyForPending(state, object, behavior);
    if (runtimePathFailureCooldownActive(state, failureKey, behavior)) {
        state.simulationStatus =
            "attack path failure cooldown #"
            + std::to_string(object.instanceId);
        return false;
    }

    const bool omniCrushPursuit = behavior.pendingRangeCells == 0.0
        && !behavior.attackGround && !behavior.attackBridge
        && target->kind != WorldObjectKind::Structure
        && runtimeObjectUsesOmniCrushApproach(state, object)
        && runtimeCrusherCanCrushObject(state, object, *target, runtimeCellPointForObject(*target));
    const auto* ignored = behavior.pendingIgnoredObjectIds.empty()
        ? nullptr : &behavior.pendingIgnoredObjectIds;
    RuntimeQueuedPathResult queuedPath;
    if (omniCrushPursuit) {
        const auto goal = runtimeCellPointForObject(*target);
        const auto context = makeRuntimeMovePathApproachContext(state, object, goal,
            static_cast<int>(behavior.pendingIgnoredObjectIds.size()), false, true);
        // This is a crush approach, not a firing ring or a structure entrance.
        // The real occupied-cell gate must authorize every step and the goal.
        queuedPath = resolveRuntimePathWithQueue(state, object,
            runtimeCellPointForObject(object), goal, -1, ignored, context);
        if (queuedPath.goalAdjusted && queuedPath.resolvedGoal != goal) {
            behavior.path.clear();
            ++behavior.blockedTicks;
            behavior.state = RuntimeBehaviorState::Blocked;
            behavior.nextRepathTick = state.tick + runtimeRepathCooldownTicks;
            state.simulationStatus = "waiting for exact crush target #"
                + std::to_string(target->instanceId);
            return false;
        }
    } else {
        queuedPath = resolveRuntimePathTowardRange(state, object, *target,
            behavior.pendingRangeCells, ignored, behavior.forceFire);
    }
    replaceRuntimeBehaviorPath(behavior, queuedPath.path);
    if (!behavior.path.empty() || queuedPath.reachable) {
        behavior.blockedTicks = 0;
        behavior.state = RuntimeBehaviorState::Queued;
        clearRuntimePathFailure(state, failureKey);
        return true;
    }

    if (queuedPath.budgetExhausted || queuedPath.cooldownNoPath) {
        behavior.state = RuntimeBehaviorState::Blocked;
        behavior.nextRepathTick = std::min(
            behavior.nextRepathTick,
            state.tick + 1
        );
        state.simulationStatus =
            queuedPath.budgetExhausted
                ? "attack path budget exhausted #"
                    + std::to_string(object.instanceId)
                : "attack path cooldown #"
                    + std::to_string(object.instanceId);
        return false;
    }

    ++behavior.blockedTicks;
    behavior.state = RuntimeBehaviorState::Blocked;
    behavior.nextRepathTick =
        std::max(
            behavior.nextRepathTick,
            state.tick
                + runtimeRepathCooldownTicks
                    * std::min(4, std::max(1, behavior.blockedTicks))
        );
    if (queuedPath.realNoPath) {
        rememberRuntimePathFailure(state, failureKey, behavior);
    }
    state.simulationStatus =
        "attack path request blocked #" + std::to_string(object.instanceId);
    return false;
}

bool runtimePendingMovePathCanBatch(
    const RuntimeGameSessionState& state,
    int objectId,
    const RuntimeObjectBehavior& behavior) {
    if (behavior.pendingPathTowardRange
        || behavior.pendingIgnoredObjectIds.size() <= 1U) {
        return false;
    }
    return state.pendingEngineerCaptures.find(objectId)
            == state.pendingEngineerCaptures.end()
        && state.pendingOccupyTargets.find(objectId)
            == state.pendingOccupyTargets.end()
        && state.pendingTransportTargets.find(objectId)
            == state.pendingTransportTargets.end()
        && std::none_of(state.grinding.relations().begin(), state.grinding.relations().end(),
            [objectId](const RuntimeGrindingRelation& entry) { return entry.sourceId == objectId; });
}

bool runtimeMovePathBatchCompatible(
    const RuntimeGameSessionState& state,
    const RuntimeMovePathBatchWork& first,
    const RuntimeMovePathBatchWork& candidate,
    const WorldObjectInstance& firstObject,
    const WorldObjectInstance& candidateObject) {
    constexpr int MaxSharedStartDelta = 12;
    const int startDelta = std::max(
        std::abs(first.start.x - candidate.start.x),
        std::abs(first.start.y - candidate.start.y)
    );
    return startDelta <= MaxSharedStartDelta
        && first.allowedOccupiedObjectId == candidate.allowedOccupiedObjectId
        && first.ignoredObjectIds == candidate.ignoredObjectIds
        // Both work contexts were constructed in this read-only gathering
        // scope. Any movement-affecting mutation requires fresh contexts.
        && runtimeMoveBatchConstraintsMatch(first.context, candidate.context,
            [&] { return runtimeMovementConstraintHash(state, firstObject); },
            [&] { return runtimeMovementConstraintHash(state, candidateObject); });
}

bool processRuntimeMovePathBatch(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeMovePathBatchWork>& batch) {
    if (batch.empty()) {
        return false;
    }

    WorldObjectInstance* firstObject =
        findRuntimePathQueueObject(state, batch.front().objectId);
    if (firstObject == nullptr) {
        return false;
    }

    const std::unordered_set<int>* ignoredPtr =
        batch.front().ignoredObjectIds.empty()
            ? nullptr
            : &batch.front().ignoredObjectIds;
    const auto pathStart = std::chrono::steady_clock::now();

    struct RuntimeMovePathBatchHandle {
        RuntimeMovePathBatchWork work;
        PathRequestHandle handle;
    };
    struct RuntimeMovePathBatchDonor {
        RuntimeMovePathBatchWork work;
        RuntimeQueuedPathResult path;
    };

    std::vector<RuntimeMovePathBatchHandle> handles;
    std::unordered_map<int, RuntimeQueuedPathResult> resultByRequestId;
    std::vector<RuntimeMovePathBatchDonor> donorPaths;
    PathRequestQueueLiveStats liveStats;
    bool changed = false;
    { // No snapshot rebuild or authority mutation until owned results are extracted.
    PathRequestQueue queue = runtimeTimedPathRequestQueue(
        buildRuntimePathTerrainSnapshot(
            state,
            *firstObject,
            batch.front().allowedOccupiedObjectId,
            ignoredPtr
        ), state.tick, state.pathTerrainCache.queueLayout
    );
    handles.reserve(batch.size());
    for (const RuntimeMovePathBatchWork& work : batch) {
        WorldObjectInstance* object =
            findRuntimePathQueueObject(state, work.objectId);
        auto behaviorIt = state.behaviors.find(work.objectId);
        if (object == nullptr
            || behaviorIt == state.behaviors.end()) {
            continue;
        }

        [[maybe_unused]] RuntimeObjectBehavior& behavior = behaviorIt->second;
        const PathRequestHandle handle =
            queue.submit(toPathCell(work.start), toPathCell(work.goal), work.context);
        handles.push_back({work, handle});
    }

    if (auto* runner = getPathGoalCandidateRunner(
            state.pathCandidateRunnerChecked, state.pathCandidateRunner))
        queue.pumpWithCandidates(static_cast<int>(handles.size()),
            runtimePathBatchExpandedNodeBudget, *runner, state.tick);
    else queue.pumpWithBudget(
        static_cast<int>(handles.size()),
        runtimePathBatchExpandedNodeBudget
    );

    const std::vector<PathResult>& results = queue.getResults();
    resultByRequestId.reserve(results.size());
    donorPaths.reserve(results.size());
    for (const RuntimeMovePathBatchHandle& batchHandle : handles) {
        if (batchHandle.handle.id < 0) {
            continue;
        }
        for (auto it = results.rbegin(); it != results.rend(); ++it) {
            if (it->requestId != batchHandle.handle.id) {
                continue;
            }
            RuntimeQueuedPathResult queuedPath =
                runtimeQueuedPathResultFromPathResult(
                    *it,
                    batchHandle.work.start
                );
            resultByRequestId.emplace(batchHandle.handle.id, queuedPath);
            if (queuedPath.reachable && !queuedPath.path.empty()) {
                donorPaths.push_back({batchHandle.work, std::move(queuedPath)});
            }
            break;
        }
    }

    liveStats = queue.liveStats();
    } // Queue dies before greedy joins and authority assignment can rewrite dynamicCells.

    const auto sharedBatchPathFor = [&state](
        const WorldObjectInstance& object,
        const RuntimeMovePathBatchWork& work,
        const std::vector<RuntimeMovePathBatchDonor>& donors
    ) -> std::optional<RuntimeQueuedPathResult> {
        if (!work.context.shareAcrossGoalSlots || donors.empty()) {
            return std::nullopt;
        }
        const std::unordered_set<int>* ignoredPtr =
            work.ignoredObjectIds.empty() ? nullptr : &work.ignoredObjectIds;
        for (const RuntimeMovePathBatchDonor& donor : donors) {
            if (donor.work.objectId == work.objectId
                || !donor.path.reachable
                || donor.path.path.empty()) {
                continue;
            }

            std::vector<RuntimeCellPoint> sharedPath;
            const RuntimeCellPoint firstStep = donor.path.path.front();
            if (work.start == firstStep) {
                sharedPath = donor.path.path;
            } else {
                std::vector<RuntimeCellPoint> joinPath =
                    findRuntimeGreedyPathUntimed(
                        state,
                        object,
                        work.start,
                        firstStep,
                        work.allowedOccupiedObjectId,
                        ignoredPtr,
                        12
                    );
                if (joinPath.empty() || joinPath.back() != firstStep) {
                    continue;
                }
                sharedPath = std::move(joinPath);
                if (donor.path.path.size() > 1U) {
                    sharedPath.insert(
                        sharedPath.end(),
                        donor.path.path.begin() + 1,
                        donor.path.path.end()
                    );
                }
            }

            if (sharedPath.empty()) {
                continue;
            }

            RuntimeQueuedPathResult output = donor.path;
            output.cacheHit = true;
            output.sharedApproachHit = true;
            output.budgetExhausted = false;
            output.cooldownNoPath = false;
            output.realNoPath = false;
            output.goalAdjusted = true;
            output.path = std::move(sharedPath);
            return output;
        }
        return std::nullopt;
    };

    for (const RuntimeMovePathBatchHandle& batchHandle : handles) {
        WorldObjectInstance* object =
            findRuntimePathQueueObject(state, batchHandle.work.objectId);
        auto behaviorIt = state.behaviors.find(batchHandle.work.objectId);
        if (object == nullptr
            || behaviorIt == state.behaviors.end()) {
            continue;
        }

        [[maybe_unused]] RuntimeObjectBehavior& behavior = behaviorIt->second;
        RuntimeQueuedPathResult queuedPath;
        bool hasResult = false;
        if (batchHandle.handle.id >= 0) {
            const auto resultIt = resultByRequestId.find(batchHandle.handle.id);
            if (resultIt != resultByRequestId.end()) {
                queuedPath = resultIt->second;
                hasResult = true;
            }
        }

        if (!hasResult) {
            std::optional<RuntimeQueuedPathResult> sharedPath =
                sharedBatchPathFor(*object, batchHandle.work, donorPaths);
            if (sharedPath.has_value()) {
                queuedPath = std::move(*sharedPath);
                hasResult = true;
            }
        }

        if (!hasResult && batchHandle.handle.id >= 0) {
            behavior.pathRequestPending = true;
            behavior.nextRepathTick = state.tick + 1;
            state.pendingPathObjectIds.push_back(batchHandle.work.objectId);
            continue;
        }

        if (!hasResult) {
            queuedPath.budgetExhausted = true;
        }

        changed =
            applyRuntimeMovePathResult(state, *object, behavior, queuedPath)
            || changed;
    }

    state.lastPathQueueStats =
        toRuntimePathQueueStatsSnapshot(liveStats);
    state.lastPathQueueStats.mapQueryApproachAvailable =
        state.lastPathQueueStats.mapQueryApproachAvailable
        || state.pathQueueMapQueryApproachObserved;
    applyRuntimeMapQueryApproachStats(state);
    const int actualSearches = liveStats.cacheMisses;
    const int scheduledBatches = actualSearches > 0 ? 1 : 0;
    state.actionSummary.repathsThisTick += scheduledBatches;
    state.perf.lastSimRepathCount += scheduledBatches;
    state.perf.lastSimPathRequests += scheduledBatches;
    state.perf.lastSimPathMs +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pathStart
        ).count();
    return changed;
}

bool processRuntimePathRequests(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("processRuntimePathRequests", state.tick);
    bool changed = false;
    std::vector<int> readyObjectIds;
    const int readyPathRequestLimit =
        std::min<int>(
            runtimeMaxReadyPathCandidateScanPerTick,
            static_cast<int>(state.pendingPathObjectIds.size())
        );
    readyObjectIds.reserve(std::max(0, readyPathRequestLimit));
    // A large formation can leave many delayed (or stale) request ids in the
    // deque.  Visiting the entire deque when none of them is ready turns one
    // simulation tick into an unbounded O(pending) cleanup pass.  Rotate only
    // a fixed number per tick; delayed entries are pushed to the back below,
    // so every live request still advances fairly through the queue.
    const std::size_t queueVisits = std::min<std::size_t>(
        state.pendingPathObjectIds.size(),
        static_cast<std::size_t>(
            runtimeMaxReadyPathCandidateScanPerTick
        )
    );
    for (std::size_t visit = 0;
        visit < queueVisits
            && static_cast<int>(readyObjectIds.size()) < readyPathRequestLimit
            && !state.pendingPathObjectIds.empty();
        ++visit) {
        const int objectId = state.pendingPathObjectIds.front();
        state.pendingPathObjectIds.pop_front();

        auto behaviorIt = state.behaviors.find(objectId);
        if (behaviorIt == state.behaviors.end()
            || !behaviorIt->second.pathRequestPending) {
            continue;
        }

        [[maybe_unused]] RuntimeObjectBehavior& behavior = behaviorIt->second;
        WorldObjectInstance* object = findRuntimePathQueueObject(state, objectId);
        if (object == nullptr || !object->alive) {
            behavior.pathRequestPending = false;
            continue;
        }

        if (runtimeStructureIsRepacking(*object) || state.tick < behavior.nextRepathTick) {
            state.pendingPathObjectIds.push_back(objectId);
            continue;
        }

        // Refresh before constructing any batch, within the existing bounded
        // queue scan rather than walking unrelated behaviors every tick.
        refreshRuntimeRepackMoveGroup(state, *object, behavior);
        readyObjectIds.push_back(objectId);
    }

    std::vector<bool> consumed(readyObjectIds.size(), false);
    bool processedMoveBatchThisTick = false;
    int nonBatchRequestsProcessed = 0;
    const auto requeuePendingReadyObject = [&state](int readyObjectId) {
        const auto behaviorIt = state.behaviors.find(readyObjectId);
        if (behaviorIt != state.behaviors.end()
            && behaviorIt->second.pathRequestPending) {
            state.pendingPathObjectIds.push_back(readyObjectId);
        }
    };
    for (std::size_t index = 0; index < readyObjectIds.size(); ++index) {
        if (consumed[index]) {
            continue;
        }

        const int objectId = readyObjectIds[index];
        if (processedMoveBatchThisTick) {
            requeuePendingReadyObject(objectId);
            consumed[index] = true;
            continue;
        }

        auto behaviorIt = state.behaviors.find(objectId);
        if (behaviorIt == state.behaviors.end()
            || !behaviorIt->second.pathRequestPending) {
            consumed[index] = true;
            continue;
        }
        WorldObjectInstance* object =
            findRuntimePathQueueObject(state, objectId);
        if (object == nullptr || !object->alive) {
            behaviorIt->second.pathRequestPending = false;
            consumed[index] = true;
            continue;
        }

        [[maybe_unused]] RuntimeObjectBehavior& behavior = behaviorIt->second;
        if (runtimePendingMovePathCanBatch(state, objectId, behavior)) {
            RuntimeMovePathBatchWork firstWork;
            firstWork.objectId = objectId;
            firstWork.start = runtimeCellPointForObject(*object);
            firstWork.goal = behavior.pendingPathGoal;
            firstWork.allowedOccupiedObjectId =
                behavior.pendingAllowedOccupiedObjectId;
            firstWork.ignoredObjectIds = behavior.pendingIgnoredObjectIds;
            firstWork.context = makeRuntimeMovePathApproachContext(
                state,
                *object,
                firstWork.goal,
                static_cast<int>(firstWork.ignoredObjectIds.size()),
                behavior.pendingAllowNearbyTarget,
                false,
                behavior.pendingHasSharedGoal
                    ? std::optional<RuntimeCellPoint>(behavior.pendingSharedGoal)
                    : std::nullopt
            );

            std::vector<RuntimeMovePathBatchWork> batch;
            batch.push_back(firstWork);
            consumed[index] = true;
            for (std::size_t otherIndex = index + 1;
                otherIndex < readyObjectIds.size();
                ++otherIndex) {
                if (consumed[otherIndex]) {
                    continue;
                }
                const int otherObjectId = readyObjectIds[otherIndex];
                auto otherBehaviorIt = state.behaviors.find(otherObjectId);
                if (otherBehaviorIt == state.behaviors.end()
                    || !otherBehaviorIt->second.pathRequestPending
                    || !runtimePendingMovePathCanBatch(
                        state,
                        otherObjectId,
                        otherBehaviorIt->second
                    )) {
                    continue;
                }
                WorldObjectInstance* otherObject =
                    findRuntimePathQueueObject(state, otherObjectId);
                if (otherObject == nullptr || !otherObject->alive) {
                    otherBehaviorIt->second.pathRequestPending = false;
                    consumed[otherIndex] = true;
                    continue;
                }

                RuntimeMovePathBatchWork candidate;
                candidate.objectId = otherObjectId;
                candidate.start = runtimeCellPointForObject(*otherObject);
                candidate.goal = otherBehaviorIt->second.pendingPathGoal;
                candidate.allowedOccupiedObjectId =
                    otherBehaviorIt->second.pendingAllowedOccupiedObjectId;
                candidate.ignoredObjectIds =
                    otherBehaviorIt->second.pendingIgnoredObjectIds;
                candidate.context = makeRuntimeMovePathApproachContext(
                    state,
                    *otherObject,
                    candidate.goal,
                    static_cast<int>(candidate.ignoredObjectIds.size()),
                    otherBehaviorIt->second.pendingAllowNearbyTarget,
                    false,
                    otherBehaviorIt->second.pendingHasSharedGoal
                        ? std::optional<RuntimeCellPoint>(
                            otherBehaviorIt->second.pendingSharedGoal
                        )
                        : std::nullopt
                );
                if (!runtimeMovePathBatchCompatible(
                        state,
                        batch.front(),
                        candidate,
                        *object,
                        *otherObject
                    )) {
                    continue;
                }
                batch.push_back(std::move(candidate));
                consumed[otherIndex] = true;
            }

            for (const RuntimeMovePathBatchWork& work : batch) {
                auto workBehaviorIt = state.behaviors.find(work.objectId);
                if (workBehaviorIt == state.behaviors.end()) {
                    continue;
                }
                workBehaviorIt->second.pathRequestPending = false;
                workBehaviorIt->second.path.clear();
                ++workBehaviorIt->second.pathReplans;
                workBehaviorIt->second.nextRepathTick =
                    state.tick + runtimeRepathCooldownTicks;
            }
            changed = processRuntimeMovePathBatch(state, batch) || changed;
            processedMoveBatchThisTick = true;
            continue;
        }

        if (nonBatchRequestsProcessed >= runtimeMaxReadyPathRequestsPerTick) {
            requeuePendingReadyObject(objectId);
            consumed[index] = true;
            continue;
        }
        ++nonBatchRequestsProcessed;
        behavior.pathRequestPending = false;
        behavior.path.clear();
        ++behavior.pathReplans;
        ++state.actionSummary.repathsThisTick;
        ++state.perf.lastSimRepathCount;
        behavior.nextRepathTick = state.tick + runtimeRepathCooldownTicks;
        const bool completed =
            behavior.pendingPathTowardRange
                ? completeRuntimeAttackPathRequest(state, *object, behavior)
                : completeRuntimeMovePathRequest(state, *object, behavior);
        changed = completed || changed;
    }

    return changed;
}

void rebuildRuntimeNavigationStats(RuntimeGameSessionState& state) {
    RuntimeNavigationStats stats;
    stats.terrainCells = static_cast<int>(state.world.getTerrainCells().size());
    stats.structureFootprints =
        static_cast<int>(state.structureFootprints.size());

    for (const TerrainCell& terrain : state.world.getTerrainCells()) {
        const RuntimeTerrainInfo info = runtimeCachedTerrainInfoForCell(state, terrain);
        if (!info.passable) {
            ++stats.blockedCells;
        }

        switch (info.kind) {
        case RuntimeTerrainKind::Land:
            ++stats.landCells;
            break;
        case RuntimeTerrainKind::Water:
            ++stats.waterCells;
            break;
        case RuntimeTerrainKind::Shore:
            ++stats.shoreCells;
            break;
        case RuntimeTerrainKind::Cliff:
            ++stats.cliffCells;
            break;
        case RuntimeTerrainKind::Ramp:
            ++stats.rampCells;
            break;
        case RuntimeTerrainKind::Bridge:
            ++stats.bridgeCells;
            break;
        case RuntimeTerrainKind::Blocked:
            break;
        }
    }

    RuntimeNavigationStats occupancyStats;
    buildRuntimeOccupancyLookup(
        state,
        -1,
        &occupancyStats,
        nullptr,
        true,
        nullptr,
        RuntimeStructureOccupancyMode::VehicleNavigation
    );
    stats.occupiedCells = occupancyStats.occupiedCells;
    stats.structureFootprintCells = occupancyStats.structureFootprintCells;
    state.navigationStats = stats;
}

void refreshRuntimeNavigationOccupancyStats(RuntimeGameSessionState& state) {
    RuntimeNavigationStats occupancyStats;
    buildRuntimeOccupancyLookup(
        state,
        -1,
        &occupancyStats,
        nullptr,
        true,
        nullptr,
        RuntimeStructureOccupancyMode::VehicleNavigation
    );
    state.navigationStats.structureFootprints =
        static_cast<int>(state.structureFootprints.size());
    state.navigationStats.occupiedCells = occupancyStats.occupiedCells;
    state.navigationStats.structureFootprintCells =
        occupancyStats.structureFootprintCells;
}

void initializeRuntimeNavigation(RuntimeProductionHostState& state) {
    captureRuntimeInitialTerrainObjectState(state);
    if (!state.bridgeSpecialFlagsLoaded) {
        state.destroyableBridges = state.rules.getDestroyableBridges();
        IniFile mapIni;
        if (!state.config.defaultMapPath.empty()
            && mapIni.loadFromFile(state.config.defaultMapPath)) {
            const std::string mapOverride = trimAsciiLocal(
                mapIni.getValue(
                    "SpecialFlags",
                    "DestroyableBridges",
                    ""
                )
            );
            if (!mapOverride.empty()) {
                state.destroyableBridges = parseRuntimeBool(mapOverride);
            }
            state.mapInitialVeteran =
                runtimeMapInitialVeteranSpecialFlag(mapIni);
        }
        state.bridgeSpecialFlagsLoaded = true;
    }
    if (state.ownerRemapColors.empty() || state.neutralRemapOwners.empty()) {
        state.ownerRemapColors = loadRuntimeMapOwnerRemapColors(
            state.config,
            &state.neutralRemapOwners
        );
    }
    // Navigation rebuilds must preserve authored or saved hut health,
    // including damage from an attached Ivan charge.
    if (!state.cloakStateInitialized) {
        state.cloakObservedHealthByObject.clear();
        for (WorldObjectInstance& object : state.world.accessObjects()) {
            if (!object.alive) {
                continue;
            }
            initializeRuntimeObjectCloakState(state, object);
            state.cloakObservedHealthByObject[object.instanceId] =
                object.health;
        }
        state.cloakStateInitialized = true;
    }
    state.mapFactsLoaded = !state.mapFacts.cells.empty();
    if (state.mapFactsLoaded) {
        std::ostringstream mapQueryStatus;
        mapQueryStatus
            << "MapQuery cells="
            << state.mapFacts.cells.size()
            << " mode=lightweight"
            << " structures="
            << state.mapFacts.structureAnchors.size()
            << " resources="
            << state.mapFacts.resourceCellCount
            << " water="
            << state.mapFacts.waterCellCount
            << " cliffs="
            << state.mapFacts.cliffCellCount;
        state.mapQueryStatus = mapQueryStatus.str();
    } else {
        state.mapQueryStatus = "MapQuery unavailable";
    }
    state.structureFootprints = buildRuntimeStructureFootprints(state);
    initializeRuntimeInitialOreFields(state);
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    initializeRuntimeBridgeRepairSpans(state);
    // Authored damage frames and local gaps belong to individual segments.
    // Rebuilding navigation or loading a save must not erase their whole span.
    rebuildRuntimeTerrainInfoCache(state);
    // A navigation rebuild can follow an in-place map/height replacement;
    // pointer identity and terrain count alone cannot validate screen picks.
    state.renderCache.terrainLayer = RuntimeTerrainLayerCache{};
    state.renderCache.terrainLookupWorld = nullptr;
    state.renderCache.terrainLookupData = nullptr;
    state.renderCache.terrainLookupCount = 0;
    state.pathTerrainCache.valid = false;
    state.pathTerrainCache.terrainCellCount = 0;
    state.pathTerrainCache.staticCells.clear();
    state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();
    state.navigationRefreshPending = false;
    state.powerRefreshPending = false;

    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive) {
            continue;
        }

        if (runtimeHasSupportFlightPose(object)
            ||runtimeObjectManagedByAirstrike(state,object.instanceId)) {
            // Support aircraft and parachuting infantry have their own saved
            // flight authority. A navigation rebuild cannot ground them or
            // replace their continuous pose with a path interpolation pose.
            if (!object.hasRenderPosition) {
                object.renderX=object.x; object.renderY=object.y;
                object.renderLevel=object.flightLevel;
            }
            object.hasRenderPosition=object.renderMoveActive=true;
            object.level=object.flightLevel;
            object.onBridge=object.renderOnBridge=false;
            continue;
        }
        const auto missile = state.specialMechanisms.spawnedMissiles().find(object.instanceId);
        if (missile != state.specialMechanisms.spawnedMissiles().end()) {
            // Missile poses are authoritative, not ordinary path interpolation.
            // Navigation rebuilds and save loading must preserve their altitude.
            applyRuntimeMissilePose(object, missile->second);
            continue;
        }
        const auto terrainIt =
            state.terrainByCell.find(runtimeCellKey(object.x, object.y));
        const bool airborneObject =
            runtimeObjectMovementMode(state, object) == PathMovementMode::Air
            && object.airborne;
        if (airborneObject) {
            object.onBridge = false;
            object.level = runtimeCarrierHasDeckFlight(state, object) && object.hasRenderPosition
                ? static_cast<int>(std::lround(object.renderLevel)) : object.flightLevel;
        } else {
            object.onBridge = terrainIt != state.terrainByCell.end()
                && terrainIt->second != nullptr
                && runtimeCachedTerrainInfoForCell(state, *terrainIt->second).bridge;
            if (terrainIt != state.terrainByCell.end()
                && terrainIt->second != nullptr) {
                object.level = runtimeEffectiveTerrainLevel(*terrainIt->second);
            }
        }
        if (object.renderMoveActive) {
            const auto behaviorIt = state.behaviors.find(object.instanceId);
            if (behaviorIt != state.behaviors.end()
                && behaviorIt->second.renderMoveTicksTotal > 0) {
                auto& segment = behaviorIt->second;
                if (segment.transportTransferKind == RuntimeTransportTransferKind::Boarding) {
                    // The accepted doorway owns its exact stopped hull endpoint, not the access terrain.
                } else if (runtimeOmniCrushPendingSegment(state, object, segment)) {
                    segment.renderToLevel = terrainLevelAtRuntimeCell(
                        state, segment.renderTo.x, segment.renderTo.y);
                    segment.renderToOnBridge = runtimeCellIsBridge(
                        state, segment.renderTo.x, segment.renderTo.y);
                } else {
                    segment.renderToLevel = static_cast<double>(object.level);
                    segment.renderToOnBridge = object.onBridge;
                }
            } else {
                const auto sortie = state.specialMechanisms.carrierChildren().find(object.instanceId);
                const bool carrierReturn = airborneObject && object.hasRenderPosition
                    && sortie != state.specialMechanisms.carrierChildren().end()
                    && (sortie->second.phase == CarrierChildPhase::Returning
                        || (sortie->second.phase == CarrierChildPhase::Launching
                            && runtimeCarrierHasDeckFlight(state, object))
                        || runtimeCarrierHasContinuousAttackFlight(state, object))
                    && !state.rules.findSpawnedMissileProfile(object.type);
                if (!carrierReturn) clearRuntimeRenderPosition(object);
            }
        } else if (!worldObjectHasStoppedGroundPose(object)) {
            object.renderLevel = static_cast<double>(object.level);
            object.renderOnBridge = object.onBridge;
        }
    }

    {
        RuntimePerfTraceScope stageTrace("applyRuntimeFlightPoses", state.tick);
        for (auto& object : state.world.accessObjects()) {
            applyRuntimeJumpjetPose(state, object);
            applyRuntimeFixedWingPose(state, object);
        }
    }
    reconcileRuntimeSubcells(state, true);

    refreshRuntimeWallConnectionMasks(state);
    rebuildRuntimeNavigationStats(state);
}

void refreshRuntimeObjectOccupancyAfterLocalChange(RuntimeProductionHostState& state) {
    state.attackApproachCache.clear();
    refreshRuntimeWallConnectionMasks(state);
    // Static path cells only describe terrain. Object/structure occupancy is
    // overlaid in the per-tick dynamic snapshot, so local object changes must
    // not force an O(map-size) terrain rebuild.
    state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();
    invalidateRuntimeTickOccupancyCache(state);
    invalidateRuntimeObjectProjectionCache(state);
    state.navigationRefreshPending = true;
}

void writeRuntimeTransportTransferPresentation(const RuntimeGameSessionState& state, std::ostream& output,
    bool multiplayer) {
    std::vector<int> ids;
    for (const auto& [id, move] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        if (move.transportTransferHostId >= 0 && move.transportTransferKind != RuntimeTransportTransferKind::None
            && (move.kind == RuntimeBehaviorKind::Move || move.kind == RuntimeBehaviorKind::Attack)
            && object && object->alive && !object->airborne && object->renderMoveActive
            && move.renderMoveTicksRemaining > 0 && move.renderMoveTicksRemaining == move.moveCooldownTicks)
            ids.push_back(id);
    }
    if (ids.empty()) return;
    std::sort(ids.begin(), ids.end());
    output << "\n[" << (multiplayer ? "MultiplayerTransportTransferPresentation" : "TransportTransferPresentation")
        << "]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t n = 0; n < ids.size(); ++n) {
        const auto& move=state.behaviors.at(ids[n]);
        output << n << '=' << ids[n] << '|' << move.transportTransferHostId << '|'
            << (move.transportTransferKind==RuntimeTransportTransferKind::Boarding?"boarding":"egress") << '\n';
    }
}

bool loadRuntimeTransportTransferProofIntents(RuntimeGameSessionState& state,
    const IniFile& ini, std::string& errorMessage) {
    const auto* marker=ini.getSection("TransportTransferPresentation");
    if (!marker) return true;
    int count=0;
    const auto fail=[&](){errorMessage="invalid transport presentation proof intent";return false;};
    if (!parseRuntimeInt(ini.getValue("TransportIntents","Count","0"),count)
        || count<0 || count>100000) return fail();
    std::map<int,int> intents;
    for(int n=0;n<count;++n) {
        const auto fields=splitRuntimeSaveFields(ini.getValue("TransportIntents",std::to_string(n),""));
        int id=-1,host=-1;
        if(fields.size()!=2 || !parseRuntimeInt(fields[0],id) || !parseRuntimeInt(fields[1],host)
            || id<0 || host<0 || id==host || !intents.emplace(id,host).second) return fail();
    }
    for(const auto& [id,host]:intents) state.pendingTransportTargets[id]=host;
    return true;
}

bool loadRuntimeTransportTransferPresentation(RuntimeGameSessionState& state,
    const IniFile& ini, std::string& errorMessage, bool multiplayer) {
    const char* sectionName=multiplayer && ini.getSection("MultiplayerTransportTransferPresentation")
        ? "MultiplayerTransportTransferPresentation" : "TransportTransferPresentation";
    struct Record {int id,host;RuntimeTransportTransferKind kind;};
    std::vector<Record> records;
    const auto fail=[&](){errorMessage="invalid transport transfer presentation";return false;};
    if (ini.getSection(sectionName)) {
        int schema=0,count=0;
        if(!parseRuntimeInt(ini.getValue(sectionName,"Schema",""),schema)||schema!=1
            ||!parseRuntimeInt(ini.getValue(sectionName,"Count",""),count)
            ||count<0||std::size_t(count)>state.world.getObjects().size()) return fail();
        const auto* section=ini.getSection(sectionName);
        if(section->size()!=std::size_t(count)+2) return fail();
        std::unordered_set<std::string> keys;
        for(const auto& item:*section) {
            const auto key=toLowerAsciiLocal(trimAsciiLocal(item.first));int index=-1;
            if(!keys.insert(key).second || (key!="schema"&&key!="count"
                &&(!parseRuntimeInt(key,index)||index<0||index>=count||key!=std::to_string(index)))) return fail();
        }
        std::unordered_set<int> seen;
        for(int n=0;n<count;++n) {
            const auto fields=splitRuntimeSaveFields(ini.getValue(sectionName,std::to_string(n),""));
            int id=-1,host=-1;
            if(fields.size()!=3||!parseRuntimeInt(fields[0],id)||!parseRuntimeInt(fields[1],host)
                ||id<0||host<0||id==host||!seen.insert(id).second) return fail();
            RuntimeTransportTransferKind kind=RuntimeTransportTransferKind::None;
            if(fields[2]=="boarding")kind=RuntimeTransportTransferKind::Boarding;
            else if(fields[2]=="egress")kind=RuntimeTransportTransferKind::Egress;
            else return fail();
            const auto* object=findObjectById(state,id);const auto found=state.behaviors.find(id);
            if(!object||!object->alive||object->health<=0||object->airborne
                ||(object->kind!=WorldObjectKind::Infantry&&object->kind!=WorldObjectKind::Unit)
                ||found==state.behaviors.end()
                ||(found->second.kind!=RuntimeBehaviorKind::Move&&found->second.kind!=RuntimeBehaviorKind::Attack)
                ||found->second.moveCooldownTicks<=0) return fail();
            const auto& move=found->second;
            const auto from=move.moveSegmentFrom,logical=runtimeCellPointForObject(*object);
            if(from.x<0||from.y<0||from.x>=512||from.y>=512) return fail();
            if(kind==RuntimeTransportTransferKind::Boarding) {
                if(move.kind!=RuntimeBehaviorKind::Move||logical!=from
                    ||(object->kind==WorldObjectKind::Infantry&&!RuntimeSubcellSystemState::isActiveSubCell(object->subCell))
                    ||runtimeTransportAuthorizedBoardingHostId(state,*object,move.moveTarget)!=host
                    ||from==move.moveTarget||std::abs(from.x-move.moveTarget.x)>1||std::abs(from.y-move.moveTarget.y)>1) return fail();
            } else if(from==logical||std::abs(from.x-logical.x)>1||std::abs(from.y-logical.y)>1) return fail();
            // Egress is immutable accepted presentation. Its old host may already be dead or have changed owner.
            records.push_back({id,host,kind});
        }
    }
    for(auto& [id,move]:state.behaviors) {
        move.transportTransferHostId=-1;move.transportTransferKind=RuntimeTransportTransferKind::None;
    }
    for(const auto& record:records) {
        auto& move=state.behaviors.at(record.id);
        move.transportTransferHostId=record.host;move.transportTransferKind=record.kind;
    }
    return true;
}

bool validateRuntimeTransportTransferPresentation(const RuntimeGameSessionState& state,std::string& errorMessage) {
    for(const auto& [id,move]:state.behaviors) {
        if(move.transportTransferHostId<0&&move.transportTransferKind==RuntimeTransportTransferKind::None)continue;
        const auto fail=[&](){errorMessage="invalid restored transport transfer curve";return false;};
        const auto* object=findObjectById(state,id);
        if(move.transportTransferHostId<0||move.transportTransferKind==RuntimeTransportTransferKind::None
            ||!object||!object->alive||object->airborne||!object->renderMoveActive
            ||(move.kind!=RuntimeBehaviorKind::Move&&move.kind!=RuntimeBehaviorKind::Attack)
            ||move.renderMoveTicksRemaining<=0||move.renderMoveTicksRemaining!=move.moveCooldownTicks
            ||move.renderMoveTicksTotal<move.renderMoveTicksRemaining||move.renderArrivalDistanceAfter!=-1.0
            ||move.renderFrom!=move.moveSegmentFrom||move.renderFrom==move.renderTo
            ||std::abs(move.renderTo.x-move.renderFrom.x)>1||std::abs(move.renderTo.y-move.renderFrom.y)>1)return fail();
        const double x=move.renderFrom.x+move.renderFromOffsetX,y=move.renderFrom.y+move.renderFromOffsetY;
        const double tx=move.renderTo.x+move.renderToOffsetX,ty=move.renderTo.y+move.renderToOffsetY;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(tx)||!std::isfinite(ty)
            ||x<-.5||y<-.5||tx<-.5||ty<-.5||x>512.5||y>512.5||tx>512.5||ty>512.5
            ||std::hypot(tx-x,ty-y)<=1.0e-9)return fail();
        if(move.transportTransferKind==RuntimeTransportTransferKind::Boarding) {
            if(!runtimeTransportSavedBoardingEndpointAllowed(state,*object,move.renderFrom,move.renderTo,
                    move.renderToLevel,move.renderToOnBridge))return fail();
            const auto* host=findObjectById(state,move.transportTransferHostId);
            const auto offset=worldObjectSubCellOffset(*object),hostOffset=worldObjectSubCellOffset(*host);
            if(std::abs(tx+offset.x-runtimeObjectRenderX(*host)-hostOffset.x)>1.0e-9
                ||std::abs(ty+offset.y-runtimeObjectRenderY(*host)-hostOffset.y)>1.0e-9)return fail();
        } else if(runtimeCellPointForObject(*object)!=move.renderTo
            ||move.renderToOffsetX!=0.0||move.renderToOffsetY!=0.0)return fail();
    }
    return true;
}

bool runtimeGroundAttackWalkPresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& walk) {
    return (object.kind == WorldObjectKind::Infantry
            || (object.kind == WorldObjectKind::Unit && !object.authoredAircraft))
        && !object.airborne && findRuntimeUnitType(state.rules, object) != nullptr
        && runtimeObjectMovementMode(state, object) != PathMovementMode::Air
        && walk.kind == RuntimeBehaviorKind::Attack && walk.moveCooldownTicks > 0
        && walk.moveSegmentFrom.x >= 0 && walk.moveSegmentFrom.x < 512
        && walk.moveSegmentFrom.y >= 0 && walk.moveSegmentFrom.y < 512;
}

void writeRuntimeInfantryWalks(const RuntimeGameSessionState& state, std::ostream& output,
    bool includeMobileUnits) {
    std::vector<int> ids;
    for (const auto& [id, walk] : state.behaviors) {
        const auto* unit = findObjectById(state, id);
        if (unit != nullptr && unit->alive
            && (unit->kind == WorldObjectKind::Infantry
                || (includeMobileUnits && unit->kind == WorldObjectKind::Unit && !unit->authoredAircraft))
            && !unit->airborne && unit->hasRenderPosition && unit->renderMoveActive
            && (walk.kind == RuntimeBehaviorKind::Move
                || (runtimeGroundAttackWalkPresentation(state, *unit, walk)
                    && walk.moveSegmentFrom == walk.renderFrom
                    && runtimeCellPointForObject(*unit) == walk.renderTo))
            && walk.renderMoveTicksRemaining > 0
            && walk.renderMoveTicksRemaining == walk.moveCooldownTicks
            && walk.renderMoveTicksTotal >= walk.renderMoveTicksRemaining
            && walk.renderFrom != walk.renderTo
            && std::abs(walk.renderTo.x - walk.renderFrom.x) <= 1
            && std::abs(walk.renderTo.y - walk.renderFrom.y) <= 1)
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    const bool offsets = std::any_of(ids.begin(), ids.end(), [&](int id) {
        const auto& walk = state.behaviors.at(id);
        return walk.renderFromOffsetX != 0.0 || walk.renderFromOffsetY != 0.0
            || walk.renderToOffsetX != 0.0 || walk.renderToOffsetY != 0.0;
    });
    output << (includeMobileUnits ? "\n[MultiplayerMovementSteps]\n" : "\n[InfantryWalkPresentation]\n")
        << "Schema=" << (offsets ? 2 : 1) << "\nCount=" << ids.size() << '\n';
    const auto precision = output.precision();
    output << std::setprecision(17);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& walk = state.behaviors.at(ids[i]);
        const auto* unit = findObjectById(state, ids[i]);
        output << i << '=' << ids[i] << '|' << walk.renderFrom.x << '|' << walk.renderFrom.y
            << '|' << walk.renderTo.x << '|' << walk.renderTo.y
            << '|' << walk.renderFromLevel << '|' << walk.renderToLevel
            << '|' << runtimeBoolText(walk.renderFromOnBridge) << '|' << runtimeBoolText(walk.renderToOnBridge)
            << '|' << walk.renderMoveTicksTotal << '|' << walk.renderMoveTicksRemaining
            << '|' << unit->movementAnimationDistance;
        if (offsets) output << '|' << walk.renderFromOffsetX << '|' << walk.renderFromOffsetY
            << '|' << walk.renderToOffsetX << '|' << walk.renderToOffsetY;
        output << '\n';
    }
    output.precision(precision);
}

bool loadRuntimeInfantryWalks(RuntimeGameSessionState& state, const IniFile& ini, std::string& error,
    bool includeMobileUnits) {
    const char* section = includeMobileUnits ? "MultiplayerMovementSteps" : "InfantryWalkPresentation";
    if (ini.getSection(section) == nullptr) return true; // Older saves have no interpolation.
    const auto fail = [&]() { error = "invalid infantry walk presentation"; return false; };
    int schema = 0, count = 0;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size()) return fail();
    std::unordered_set<int> seen;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        int id = -1;
        RuntimeCellPoint from, to;
        double fromLevel = 0.0, toLevel = 0.0, distance = 0.0;
        bool fromBridge = false, toBridge = false;
        int total = 0, remaining = 0;
        std::array<double, 4> offsets{};
        if (fields.size() != (schema == 1 ? 12U : 16U) || !parseRuntimeInt(fields[0], id) || !seen.insert(id).second
            || !parseRuntimeInt(fields[1], from.x) || !parseRuntimeInt(fields[2], from.y)
            || !parseRuntimeInt(fields[3], to.x) || !parseRuntimeInt(fields[4], to.y)
            || !parseRuntimeFiniteDouble(fields[5], fromLevel) || !parseRuntimeFiniteDouble(fields[6], toLevel)
            || !parseRuntimeStrictBool(fields[7], fromBridge) || !parseRuntimeStrictBool(fields[8], toBridge)
            || !parseRuntimeInt(fields[9], total) || !parseRuntimeInt(fields[10], remaining)
            || !parseRuntimeFiniteDouble(fields[11], distance)
            || from.x < 0 || from.y < 0 || to.x < 0 || to.y < 0
            || from.x >= 512 || from.y >= 512 || to.x >= 512 || to.y >= 512
            || from == to || std::abs(to.x - from.x) > 1 || std::abs(to.y - from.y) > 1
            || total <= 0 || total > 100000 || remaining <= 0 || remaining > total
            || distance < 0.0 || distance >= 1024.0) return fail();
        if (schema == 2) {
            for (std::size_t n = 0; n < offsets.size(); ++n)
                if (!parseRuntimeFiniteDouble(fields[12+n], offsets[n]) || std::abs(offsets[n]) > 16.0)
                    return fail();
        }
        auto* unit = findMutableObjectById(state, id);
        auto behavior = state.behaviors.find(id);
        if (unit == nullptr || !unit->alive || unit->airborne
            || (unit->kind != WorldObjectKind::Infantry
                && !(includeMobileUnits && unit->kind == WorldObjectKind::Unit && !unit->authoredAircraft))
            || behavior == state.behaviors.end()
            || (behavior->second.kind != RuntimeBehaviorKind::Move
                && !(runtimeGroundAttackWalkPresentation(state, *unit, behavior->second)
                    && behavior->second.moveSegmentFrom == from))
            || behavior->second.moveCooldownTicks != remaining) return fail();
        if (behavior->second.transportTransferKind == RuntimeTransportTransferKind::Boarding) {
            if (!runtimeTransportSavedBoardingEndpointAllowed(state, *unit, from, to, toLevel, toBridge)) return fail();
        } else if (runtimeCellPointForObject(*unit) != to
            || std::abs(toLevel - unit->level) > 0.000001 || toBridge != unit->onBridge) return fail();
        auto& walk = behavior->second;
        walk.renderFrom = from; walk.renderTo = to;
        walk.renderFromLevel = fromLevel; walk.renderToLevel = toLevel;
        walk.renderFromOnBridge = fromBridge; walk.renderToOnBridge = toBridge;
        walk.renderMoveTicksTotal = total; walk.renderMoveTicksRemaining = remaining;
        walk.renderFromOffsetX = offsets[0]; walk.renderFromOffsetY = offsets[1];
        walk.renderToOffsetX = offsets[2]; walk.renderToOffsetY = offsets[3];
        const double progress = 1.0 - static_cast<double>(remaining) / total;
        unit->hasRenderPosition = unit->renderMoveActive = true;
        const auto point = runtimeGroundRenderMovePoint(*unit, walk, progress);
        unit->renderX = point[0]; unit->renderY = point[1];
        unit->renderLevel = fromLevel + (toLevel - fromLevel) * progress;
        unit->renderOnBridge = progress < 0.5 ? fromBridge : toBridge;
        unit->movementAnimationDistance = distance;
    }
    return true;
}

void writeRuntimeStoppedMovePoses(const RuntimeGameSessionState& state, std::ostream& output) {
    std::vector<const WorldObjectInstance*> objects;
    for (const auto& object : state.world.getObjects()) {
        if (object.alive && worldObjectHasStoppedGroundPose(object)
            && !runtimeObjectUsesNavalDockPresentation(state, object)) objects.push_back(&object);
    }
    std::sort(objects.begin(), objects.end(), [](const auto* left, const auto* right) {
        return left->instanceId < right->instanceId;
    });
    output << "\n[StoppedMovePoses]\nSchema=1\nCount=" << objects.size() << '\n';
    const auto precision = output.precision();
    output << std::setprecision(17);
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& object = *objects[i];
        output << i << '=' << object.instanceId << '|' << object.renderX << '|' << object.renderY
            << '|' << object.renderLevel << '|' << runtimeBoolText(object.renderOnBridge)
            << '|' << object.movementAnimationDistance << '\n';
    }
    output.precision(precision);
}

bool parseRuntimeStoppedMovePoses(const RuntimeGameSessionState& state, const IniFile& ini,
    std::vector<RuntimeStoppedMovePoseRecord>& pending, std::unordered_set<int>& seen, std::string& error) {
    constexpr const char* section = "StoppedMovePoses";
    if (!ini.getSection(section)) return true;
    const auto fail = [&]() { error = "invalid stopped move pose"; return false; };
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size()) return fail();
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        RuntimeStoppedMovePoseRecord pose;
        if (fields.size() != 6 || !parseRuntimeInt(fields[0], pose.objectId)
            || !seen.insert(pose.objectId).second
            || !parseRuntimeFiniteDouble(fields[1], pose.position[0])
            || !parseRuntimeFiniteDouble(fields[2], pose.position[1])
            || !parseRuntimeFiniteDouble(fields[3], pose.position[2])
            || !parseRuntimeStrictBool(fields[4], pose.onBridge)
            || !parseRuntimeFiniteDouble(fields[5], pose.animationDistance)
            || pose.position[0] < -0.5 || pose.position[0] > 512.5
            || pose.position[1] < -0.5 || pose.position[1] > 512.5
            || pose.position[2] < -128.0 || pose.position[2] > 512.0
            || pose.animationDistance < 0.0 || pose.animationDistance >= 1024.0) return fail();
        const auto* object = findObjectById(state, pose.objectId);
        const auto behavior = state.behaviors.find(pose.objectId);
        if (!object || !object->alive || object->airborne
            || (object->kind != WorldObjectKind::Unit && object->kind != WorldObjectKind::Infantry)
            || object->dockedToObjectId >= 0 || object->tankBunkerHostId >= 0
            || object->factoryExitHostId >= 0 || object->garrisonHostObjectId >= 0
            || std::abs(pose.position[0] - object->x) >= 2.0
            || std::abs(pose.position[1] - object->y) >= 2.0
            || (behavior != state.behaviors.end() && behavior->second.moveCooldownTicks > 0)) return fail();
        pending.push_back(pose);
    }
    return true;
}

void restoreRuntimeStoppedMovePose(WorldObjectInstance& object, const RuntimeStoppedMovePoseRecord& pose) {
    object.hasRenderPosition = true;
    object.renderMoveActive = false;
    object.renderX = pose.position[0]; object.renderY = pose.position[1];
    object.renderLevel = pose.position[2]; object.renderOnBridge = pose.onBridge;
    object.movementAnimationDistance = pose.animationDistance;
}

std::int64_t runtimeLegacyVehicleStartupDistanceQ16(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    if (move.moveCooldownTicks <= 0) return 0;
    const double ramp = runtimeVehicleStartupAccelerationDistance(state, object);
    if (!(ramp > 0.0) || !std::isfinite(ramp)) return 0;
    double segment = std::sqrt(2.0);
    if (move.moveSegmentFrom.x >= 0 && move.moveSegmentFrom.y >= 0)
        segment = std::hypot(double(object.x - move.moveSegmentFrom.x),
            double(object.y - move.moveSegmentFrom.y));
    const long double distance = std::ceil((static_cast<long double>(ramp) + segment) * 65536.0L);
    return distance >= static_cast<long double>(RuntimeVehicleStartupMotion::maximumDistanceQ16)
        ? RuntimeVehicleStartupMotion::maximumDistanceQ16
        : static_cast<std::int64_t>(distance);
}

bool runtimeOmniCrushAttackMovePresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    return move.kind == RuntimeBehaviorKind::Attack
        && move.targetObjectId >= 0 && !move.attackGround && !move.attackBridge
        && move.renderArrivalDistanceAfter == -1.0
        && runtimeObjectUsesOmniCrushApproach(state, object);
}

bool runtimeGroundVehicleAttackMovePresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    return object.kind == WorldObjectKind::Unit && !object.authoredAircraft && !object.airborne
        && findRuntimeUnitType(state.rules, object) != nullptr
        && runtimeObjectMovementMode(state, object) != PathMovementMode::Air
        && move.kind == RuntimeBehaviorKind::Attack && move.moveCooldownTicks > 0
        && move.moveSegmentFrom.x >= 0 && move.moveSegmentFrom.x < 512
        && move.moveSegmentFrom.y >= 0 && move.moveSegmentFrom.y < 512;
}

bool runtimeNavalOffsetMovePresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    // A cancelled repair order can still be finishing its committed segment,
    // so the offsets, rather than a live repair reservation, own this state.
    return runtimeObjectUsesNavalDockPresentation(state, object)
        && move.renderArrivalDistanceAfter == -1.0
        && (move.renderFromOffsetX != 0.0 || move.renderFromOffsetY != 0.0
            || move.renderToOffsetX != 0.0 || move.renderToOffsetY != 0.0);
}

bool runtimeNavalFactoryMovePresentation(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    if (!runtimeObjectUsesNavalDockPresentation(state, object)
        || move.kind != RuntimeBehaviorKind::Move || move.renderArrivalDistanceAfter != -1.0)
        return false;
    // Ordinary ship egress has zero offsets too. Its delivery lease remains
    // live until the last visible segment ends, so save that segment alongside
    // the lease instead of treating its committed navigation cell as arrival.
    return std::any_of(state.factoryDeliveries.leases().begin(), state.factoryDeliveries.leases().end(),
        [&](const RuntimeFactoryDeliveryLease& lease) {
            return lease.producedObjectId == object.instanceId
                && lease.domain == RuntimeFactoryDeliveryDomain::Naval
                && lease.lane == RuntimeProductionLane::Ships
                && !lease.rallyCell.onBridge
                && move.moveTarget == RuntimeCellPoint{lease.rallyCell.x, lease.rallyCell.y};
        });
}

void writeRuntimeVehicleArrivals(const RuntimeGameSessionState& state, std::ostream& output) {
    std::vector<int> ids;
    for (const auto& [id, move] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        const bool navalOffset = object && runtimeNavalOffsetMovePresentation(state, *object, move);
        const bool navalFactory = object && runtimeNavalFactoryMovePresentation(state, *object, move);
        const bool committedAttack = object && runtimeGroundVehicleAttackMovePresentation(state, *object, move)
            && move.moveSegmentFrom == move.renderFrom;
        if (object != nullptr && object->alive
            && (runtimeObjectUsesVehicleArrival(state, *object) || runtimeObjectUsesVehicleStartup(state, *object)
                || navalOffset || navalFactory || committedAttack)
            && (move.kind == RuntimeBehaviorKind::Move
                || runtimeOmniCrushAttackMovePresentation(state, *object, move) || committedAttack)
            && object->hasRenderPosition
            && object->renderMoveActive && move.renderMoveTicksRemaining > 0
            && move.renderMoveTicksRemaining == move.moveCooldownTicks
            && move.renderMoveTicksTotal >= move.renderMoveTicksRemaining
            && (move.renderFrom != move.renderTo
                || (navalOffset && (move.renderFromOffsetX != move.renderToOffsetX
                    || move.renderFromOffsetY != move.renderToOffsetY))))
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[VehicleMovePresentation]\nSchema=2\nCount=" << ids.size() << '\n';
    const auto precision = output.precision();
    output << std::setprecision(17);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& move = state.behaviors.at(ids[i]);
        const auto* object = findObjectById(state, ids[i]);
        output << i << '=' << ids[i] << '|' << move.renderFrom.x << '|' << move.renderFrom.y
            << '|' << move.renderTo.x << '|' << move.renderTo.y
            << '|' << move.renderFromLevel << '|' << move.renderToLevel
            << '|' << runtimeBoolText(move.renderFromOnBridge) << '|' << runtimeBoolText(move.renderToOnBridge)
            << '|' << move.renderMoveTicksTotal << '|' << move.renderMoveTicksRemaining
            << '|' << object->movementAnimationDistance << '|' << move.renderArrivalDistanceAfter
            << '|' << move.renderFromOffsetX << '|' << move.renderFromOffsetY
            << '|' << move.renderToOffsetX << '|' << move.renderToOffsetY << '\n';
    }
    ids.clear();
    for (const auto& [id, move] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        if (object && object->alive && runtimeObjectUsesVehicleStartup(state, *object)
            && (move.kind == RuntimeBehaviorKind::Move || move.kind == RuntimeBehaviorKind::Attack)
            && (move.moveStartupDistanceQ16 != 0 || move.renderStartupAccelerationDistance != 0.0))
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[VehicleStartupMotion]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& move = state.behaviors.at(ids[i]);
        output << i << '=' << ids[i] << '|' << move.moveStartupDistanceQ16
            << '|' << move.renderStartupAccelerationDistance << '\n';
    }
    ids.clear();
    for (const auto& object : state.world.getObjects()) {
        const auto move = state.behaviors.find(object.instanceId);
        if (object.alive && runtimeObjectUsesNavalDockPresentation(state, object)
            && object.hasRenderPosition && !object.renderMoveActive && object.dockedToObjectId < 0
            && (object.renderX != object.x || object.renderY != object.y)
            && (move == state.behaviors.end() || move->second.moveCooldownTicks <= 0))
            ids.push_back(object.instanceId);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[NavalDockStopPoses]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto* object = findObjectById(state, ids[i]);
        output << i << '=' << ids[i] << '|' << object->renderX << '|' << object->renderY
            << '|' << object->renderLevel << '|' << runtimeBoolText(object->renderOnBridge) << '\n';
    }
    writeRuntimeStoppedMovePoses(state, output);
    output.precision(precision);
}

bool loadRuntimeVehicleArrivals(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    constexpr const char* section = "VehicleMovePresentation";
    const auto fail = [&]() { error = "invalid vehicle move presentation"; return false; };
    int schema = 2, count = 0;
    if (ini.getSection(section) && (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size())) return fail();
    struct PendingStartup {
        std::int64_t distanceQ16 = 0;
        double accelerationDistance = 0.0;
    };
    std::unordered_map<int, PendingStartup> startups;
    constexpr const char* startupSection = "VehicleStartupMotion";
    const bool hasStartup = ini.getSection(startupSection) != nullptr;
    for (const auto& [id, move] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        if (object && object->alive && runtimeObjectUsesVehicleStartup(state, *object)
            && (move.kind == RuntimeBehaviorKind::Move || move.kind == RuntimeBehaviorKind::Attack))
            startups.emplace(id, PendingStartup{hasStartup ? 0
                : runtimeLegacyVehicleStartupDistanceQ16(state, *object, move), 0.0});
    }
    if (hasStartup) {
        int startupSchema = 0, startupCount = -1;
        if (!parseRuntimeInt(ini.getValue(startupSection, "Schema", ""), startupSchema) || startupSchema != 1
            || !parseRuntimeInt(ini.getValue(startupSection, "Count", ""), startupCount) || startupCount < 0
            || static_cast<std::size_t>(startupCount) > state.world.getObjects().size()) return fail();
        std::unordered_set<int> startupIds;
        for (int i = 0; i < startupCount; ++i) {
            const auto fields = splitRuntimeSaveFields(ini.getValue(startupSection, std::to_string(i), ""));
            int id = -1; std::uint64_t distanceQ16 = 0; double ramp = 0.0;
            if (fields.size() != 3 || !parseRuntimeInt(fields[0], id) || !startupIds.insert(id).second
                || !parseRuntimeUint64Strict(fields[1], distanceQ16)
                || distanceQ16 > static_cast<std::uint64_t>(RuntimeVehicleStartupMotion::maximumDistanceQ16)
                || !parseRuntimeFiniteDouble(fields[2], ramp) || ramp < 0.0) return fail();
            const auto found = startups.find(id);
            const auto move = state.behaviors.find(id);
            if (found == startups.end() || move == state.behaviors.end()
                || (ramp > 0.0 && (distanceQ16 == 0 || move->second.moveCooldownTicks <= 0))) return fail();
            found->second = {static_cast<std::int64_t>(distanceQ16), ramp};
        }
    }
    std::unordered_set<int> seen;
    std::vector<std::pair<int, std::array<double, 3>>> stopped;
    constexpr const char* stoppedSection = "NavalDockStopPoses";
    if (ini.getSection(stoppedSection)) {
        int stoppedSchema = 0, stoppedCount = 0;
        if (!parseRuntimeInt(ini.getValue(stoppedSection, "Schema", ""), stoppedSchema) || stoppedSchema != 1
            || !parseRuntimeInt(ini.getValue(stoppedSection, "Count", ""), stoppedCount) || stoppedCount < 0
            || static_cast<std::size_t>(stoppedCount) > state.world.getObjects().size()) return fail();
        for (int i = 0; i < stoppedCount; ++i) {
            const auto fields = splitRuntimeSaveFields(ini.getValue(stoppedSection, std::to_string(i), ""));
            int id = -1; std::array<double, 3> pose{}; bool bridge = false;
            if (fields.size() != 5 || !parseRuntimeInt(fields[0], id) || !seen.insert(id).second
                || !parseRuntimeFiniteDouble(fields[1], pose[0]) || !parseRuntimeFiniteDouble(fields[2], pose[1])
                || !parseRuntimeFiniteDouble(fields[3], pose[2]) || !parseRuntimeStrictBool(fields[4], bridge)
                || bridge || pose[0] < 0 || pose[0] >= 512 || pose[1] < 0 || pose[1] >= 512) return fail();
            const auto* object = findObjectById(state, id);
            const auto move = state.behaviors.find(id);
            if (!object || !object->alive || !runtimeObjectUsesNavalDockPresentation(state, *object)
                || object->dockedToObjectId >= 0 || object->dockIndex >= 0
                || std::abs(pose[2] - object->level) > 0.000001
                || std::abs(pose[0] - object->x) >= 2 || std::abs(pose[1] - object->y) >= 2
                || (pose[0] == object->x && pose[1] == object->y)
                || (move != state.behaviors.end() && move->second.moveCooldownTicks > 0)) return fail();
            stopped.push_back({id, pose});
        }
    }
    std::vector<RuntimeStoppedMovePoseRecord> stoppedMoves;
    if (!parseRuntimeStoppedMovePoses(state, ini, stoppedMoves, seen, error)) return false;
    struct PendingPresentation {
        int id = -1;
        RuntimeObjectBehavior move;
        double animationDistance = 0.0;
    };
    std::vector<PendingPresentation> pending;
    std::unordered_set<int> movingPresentationIds;
    pending.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        int id = -1, total = 0, remaining = 0;
        RuntimeCellPoint from, to;
        double fromLevel = 0.0, toLevel = 0.0, distance = 0.0, after = -1.0;
        bool fromBridge = false, toBridge = false;
        if (fields.size() != (schema == 1 ? 13U : 17U) || !parseRuntimeInt(fields[0], id) || !seen.insert(id).second
            || !parseRuntimeInt(fields[1], from.x) || !parseRuntimeInt(fields[2], from.y)
            || !parseRuntimeInt(fields[3], to.x) || !parseRuntimeInt(fields[4], to.y)
            || !parseRuntimeFiniteDouble(fields[5], fromLevel) || !parseRuntimeFiniteDouble(fields[6], toLevel)
            || !parseRuntimeStrictBool(fields[7], fromBridge) || !parseRuntimeStrictBool(fields[8], toBridge)
            || !parseRuntimeInt(fields[9], total) || !parseRuntimeInt(fields[10], remaining)
            || !parseRuntimeFiniteDouble(fields[11], distance) || !parseRuntimeFiniteDouble(fields[12], after)
            || from.x < 0 || from.y < 0 || to.x < 0 || to.y < 0
            || from.x >= 512 || from.y >= 512 || to.x >= 512 || to.y >= 512
            || std::abs(to.x - from.x) > 1 || std::abs(to.y - from.y) > 1
            || total <= 0 || total > 100000 || remaining <= 0 || remaining > total
            || distance < 0.0 || distance >= 1024.0
            || (after != -1.0 && (after < 0.0 || after >= RuntimeVehicleArrivalMotion::brakingDistance))) return fail();
        auto* object = findMutableObjectById(state, id);
        const auto found = state.behaviors.find(id);
        if (object == nullptr || !object->alive
            || found == state.behaviors.end()
            || (found->second.kind != RuntimeBehaviorKind::Move
                && !runtimeOmniCrushAttackMovePresentation(state, *object, found->second)
                && !(runtimeGroundVehicleAttackMovePresentation(state, *object, found->second)
                    && found->second.moveSegmentFrom == from))
            || found->second.moveCooldownTicks != remaining) return fail();
        const bool pendingOmni = from != to
            && runtimeObjectUsesOmniCrushApproach(state, *object)
            && runtimeCellPointForObject(*object) == from;
        const bool fromLayerMatches = std::abs(fromLevel - object->level) <= 0.000001
            && fromBridge == object->onBridge;
        if (pendingOmni) {
            // This source has approached but not entered the occupied cell.
            // Its navigation authority is still FROM; validate TO against the
            // real terrain instead of weakening the original layer checks.
            const auto terrain = state.terrainByCell.find(runtimeCellKey(to.x, to.y));
            const bool hasDestination = (terrain != state.terrainByCell.end() && terrain->second)
                || std::any_of(state.world.getTerrainCells().begin(), state.world.getTerrainCells().end(),
                    [&](const TerrainCell& cell) { return cell.x == to.x && cell.y == to.y; });
            if (found->second.moveSegmentFrom != from || !hasDestination
                || std::abs(toLevel - terrainLevelAtRuntimeCell(state, to.x, to.y)) > 0.000001
                || toBridge != runtimeCellIsBridge(state, to.x, to.y)) return fail();
        } else if (found->second.transportTransferKind == RuntimeTransportTransferKind::Boarding) {
            const int hostId = found->second.transportTransferHostId;
            const auto hostPose = std::find_if(stoppedMoves.begin(), stoppedMoves.end(),
                [&](const auto& pose) { return pose.objectId == hostId; });
            const std::optional<std::pair<double, bool>> validatedHostPose = hostPose == stoppedMoves.end()
                ? std::nullopt : std::optional<std::pair<double,bool>>{{hostPose->position[2], hostPose->onBridge}};
            if (!runtimeTransportSavedBoardingEndpointAllowed(state, *object, from, to,
                    toLevel, toBridge, validatedHostPose)) return fail();
        } else if (runtimeCellPointForObject(*object) != to
            || std::abs(toLevel - object->level) > 0.000001 || toBridge != object->onBridge) return fail();
        // Validate all rows before changing live behavior or world poses. A
        // malformed late row must not partly restore an earlier unit.
        auto move = found->second;
        if (const auto startup = startups.find(id); startup != startups.end()) {
            move.moveStartupDistanceQ16 = startup->second.distanceQ16;
            move.renderStartupAccelerationDistance = startup->second.accelerationDistance;
            if (move.renderStartupAccelerationDistance > 0.0
                && move.moveStartupDistanceQ16 < std::llround(std::hypot(double(to.x - from.x),
                    double(to.y - from.y)) * 65536.0)) return fail();
        }
        move.renderFromOffsetX = move.renderFromOffsetY = move.renderToOffsetX = move.renderToOffsetY = 0.0;
        if (schema == 2) {
            if (!parseRuntimeFiniteDouble(fields[13], move.renderFromOffsetX)
                || !parseRuntimeFiniteDouble(fields[14], move.renderFromOffsetY)
                || !parseRuntimeFiniteDouble(fields[15], move.renderToOffsetX)
                || !parseRuntimeFiniteDouble(fields[16], move.renderToOffsetY)) return fail();
            for (double offset : {move.renderFromOffsetX, move.renderFromOffsetY,
                    move.renderToOffsetX, move.renderToOffsetY})
                if (std::abs(offset) > 16.0) return fail();
            if (from.x+move.renderFromOffsetX < 0 || from.x+move.renderFromOffsetX >= 512
                || from.y+move.renderFromOffsetY < 0 || from.y+move.renderFromOffsetY >= 512
                || to.x+move.renderToOffsetX < 0 || to.x+move.renderToOffsetX >= 512
                || to.y+move.renderToOffsetY < 0 || to.y+move.renderToOffsetY >= 512) return fail();
        }
        move.renderFrom = from; move.renderTo = to;
        move.renderFromLevel = fromLevel; move.renderToLevel = toLevel;
        move.renderFromOnBridge = fromBridge; move.renderToOnBridge = toBridge;
        move.renderMoveTicksTotal = total; move.renderMoveTicksRemaining = remaining;
        move.renderArrivalDistanceAfter = after;
        if (pendingOmni && !fromLayerMatches) {
            // At the end of an approach, a moving victim or new crew can keep
            // the destination occupied. Its one-tick clock freezes at the real
            // destination layer while navigation still owns FROM. A new
            // approach can also inherit a real stopped fractional pose; use
            // the existing StoppedMovePoses bounds for that departure only.
            const bool holdingDestination = schema == 2 && total == 1 && remaining == 1
                && after == -1.0
                && from.x + move.renderFromOffsetX == to.x + move.renderToOffsetX
                && from.y + move.renderFromOffsetY == to.y + move.renderToOffsetY
                && fromLevel == toLevel && fromBridge == toBridge;
            const bool inheritedStoppedPose = schema == 2
                && (move.renderFromOffsetX != 0.0 || move.renderFromOffsetY != 0.0)
                && std::abs(move.renderFromOffsetX) < 2.0
                && std::abs(move.renderFromOffsetY) < 2.0
                && fromLevel >= -128.0 && fromLevel <= 512.0;
            if (!holdingDestination && !inheritedStoppedPose) return fail();
        }
        if (move.kind == RuntimeBehaviorKind::Attack
            && !runtimeOmniCrushAttackMovePresentation(state, *object, move)
            && !(runtimeGroundVehicleAttackMovePresentation(state, *object, move)
                && move.moveSegmentFrom == from)) return fail();
        if (pendingOmni) {
            auto pose = *object;
            restoreRuntimeVehicleMovePresentation(pose, move);
            if (!runtimeOmniCrushPendingSegment(state, pose, move)) return fail();
        }
        const bool navalOffset = schema == 2
            && runtimeNavalOffsetMovePresentation(state, *object, move);
        const bool navalFactory = schema == 2
            && runtimeNavalFactoryMovePresentation(state, *object, move);
        if (!runtimeObjectUsesVehicleArrival(state, *object) && !runtimeObjectUsesVehicleStartup(state, *object)
            && !navalOffset && !navalFactory
            && !runtimeGroundVehicleAttackMovePresentation(state, *object, move)) return fail();
        if ((navalOffset || navalFactory) && (fromBridge || toBridge || std::abs(fromLevel - object->level) > 0.000001
            || std::abs(move.renderFromOffsetX) >= 2 || std::abs(move.renderFromOffsetY) >= 2
            || std::abs(move.renderToOffsetX) >= 2 || std::abs(move.renderToOffsetY) >= 2)) return fail();
        if (from == to && (!navalOffset
            || (move.renderFromOffsetX == move.renderToOffsetX
                && move.renderFromOffsetY == move.renderToOffsetY))) return fail();
        movingPresentationIds.insert(id);
        pending.push_back({id, std::move(move), distance});
    }
    for (const auto& [id, startup] : startups) {
        if (startup.accelerationDistance > 0.0 && !movingPresentationIds.contains(id)) return fail();
    }
    // All sidecar and presentation rows have passed, including any late row.
    // Commit startup authority before the restored curve evaluates its pose.
    for (const auto& [id, startup] : startups) {
        auto& move = state.behaviors.at(id);
        move.moveStartupDistanceQ16 = startup.distanceQ16;
        move.renderStartupAccelerationDistance = startup.accelerationDistance;
    }
    for (auto& presentation : pending) {
        auto& move = state.behaviors.at(presentation.id);
        move = std::move(presentation.move);
        auto* object = findMutableObjectById(state, presentation.id);
        restoreRuntimeVehicleMovePresentation(*object, move);
        object->movementAnimationDistance = presentation.animationDistance;
    }
    for (const auto& [id, pose] : stopped)
        restoreRuntimeNavalDockCommandStopPose(*findMutableObjectById(state, id), pose);
    for (const auto& pose : stoppedMoves)
        restoreRuntimeStoppedMovePose(*findMutableObjectById(state, pose.objectId), pose);
    return true;
}

bool runtimeRepackMoveGroupActor(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    if (!object.alive || move.kind != RuntimeBehaviorKind::Move || move.autoAcquired
        || move.targetObjectId >= 0 || move.pendingAllowedOccupiedObjectId >= 0
        || move.forceFire || move.attackGround || move.attackBridge) return false;
    if (runtimeStructureIsRepacking(object)) {
        const auto* yard = findRuntimeBuildingTypeById(state.rules, object.type);
        if (!yard || !yard->constructionYard) return false;
        WorldObjectInstance mobile = object;
        mobile.kind = WorldObjectKind::Unit;
        mobile.type = yard->undeploysInto;
        const auto* unit = findRuntimeUnitType(state.rules, mobile);
        return unit && runtimeUnitTypeIsMcv(state, *unit);
    }
    const auto* unit = object.kind == WorldObjectKind::Unit
        ? findRuntimeUnitType(state.rules, object) : nullptr;
    return unit && runtimeUnitTypeIsMcv(state, *unit);
}

bool runtimeRepackMoveGroupMember(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& member) {
    if (!member.alive || member.owner != actor.owner) return false;
    if (member.kind == WorldObjectKind::Unit || member.kind == WorldObjectKind::Infantry)
        return true;
    if (!runtimeStructureIsRepacking(member)) return false;
    const auto* yard = findRuntimeBuildingTypeById(state.rules, member.type);
    return yard && yard->constructionYard;
}

std::optional<RuntimeRepackMoveGroupIntent> canonicalRuntimeRepackMoveGroup(
    const RuntimeGameSessionState& state, int actorId, const RuntimeObjectBehavior& move) {
    if (!move.repackMoveGroup) return std::nullopt;
    const auto* actor = findObjectById(state, actorId);
    if (!actor || !runtimeRepackMoveGroupActor(state, *actor, move))
        return std::nullopt;
    // Keep raw identities, including dead, missing or temporarily controlled
    // peers. Save/hash must retain the same future membership as the live
    // order if a controlled peer later returns to its original owner.
    RuntimeRepackMoveGroupIntent intent = *move.repackMoveGroup;
    std::sort(intent.members.begin(), intent.members.end());
    intent.members.erase(std::unique(intent.members.begin(), intent.members.end()), intent.members.end());
    return intent;
}

void refreshRuntimeRepackMoveGroup(RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, RuntimeObjectBehavior& move) {
    if (!move.repackMoveGroup) return;
    const auto intent = canonicalRuntimeRepackMoveGroup(state, actor.instanceId, move);
    if (!intent) { move.repackMoveGroup.reset(); return; }
    move.pendingHasSharedGoal = true;
    move.pendingSharedGoal = intent->center;
    move.pendingIgnoredObjectIds.clear();
    for (int id : intent->members) {
        const auto* member = findObjectById(state, id);
        // Never ignore a still-folding foundation. Keep its ID in the intent
        // so the eventual conversion can substitute the real vehicle ID.
        if (member && runtimeRepackMoveGroupMember(state, actor, *member)
            && member->kind != WorldObjectKind::Structure
            && runtimeObjectCanMove(state, *member)) move.pendingIgnoredObjectIds.insert(id);
    }
}

void remapRuntimeRepackMoveGroupMember(RuntimeGameSessionState& state, int oldId, int newId) {
    for (auto& [actorId, move] : state.behaviors) {
        if (!move.repackMoveGroup) continue;
        for (int& id : move.repackMoveGroup->members) if (id == oldId) id = newId;
        auto& members = move.repackMoveGroup->members;
        std::sort(members.begin(), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        if (const auto* actor = findObjectById(state, actorId))
            refreshRuntimeRepackMoveGroup(state, *actor, move);
    }
}

void writeRuntimeRepackMoveGroups(const RuntimeGameSessionState& state, std::ostream& output) {
    std::map<int, RuntimeRepackMoveGroupIntent> records;
    for (const auto& [id, move] : state.behaviors)
        if (auto intent = canonicalRuntimeRepackMoveGroup(state, id, move)) records.emplace(id, std::move(*intent));
    output << "\n[RepackMoveGroups]\nSchema=1\nCount=" << records.size() << '\n';
    int index = 0;
    for (const auto& [id, intent] : records) {
        output << index++ << '=' << id << '|' << intent.center.x << '|' << intent.center.y
            << '|' << intent.members.size();
        for (int member : intent.members) output << '|' << member;
        output << '\n';
    }
}

bool loadRuntimeRepackMoveGroups(RuntimeGameSessionState& state, const IniFile& ini,
    std::string& error, bool preserveExactBehavior) {
    constexpr const char* section = "RepackMoveGroups";
    const auto* entries = ini.getSection(section);
    if (!entries) return true; // Legacy saves retain their target-only order.
    const auto fail = [&]() { error = "invalid repack move group intent"; return false; };
    int schema = 0, count = 0;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size()
        || entries->size() != static_cast<std::size_t>(count) + 2) return fail();
    std::unordered_set<std::string> keys;
    for (const auto& [key, value] : *entries)
        if (!keys.insert(toUpperAsciiLocal(trimAsciiLocal(key))).second) return fail();
    std::map<int, RuntimeRepackMoveGroupIntent> records;
    int previousActor = -1;
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(index), ""));
        int actorId = -1, memberCount = 0;
        RuntimeRepackMoveGroupIntent intent;
        if (fields.size() < 5 || !parseRuntimeInt(fields[0], actorId) || actorId <= previousActor
            || !parseRuntimeInt(fields[1], intent.center.x) || !parseRuntimeInt(fields[2], intent.center.y)
            || intent.center.x < 0 || intent.center.y < 0 || intent.center.x >= 512 || intent.center.y >= 512
            || !parseRuntimeInt(fields[3], memberCount) || memberCount < 1
            || memberCount > 100000
            || fields.size() != static_cast<std::size_t>(memberCount) + 4) return fail();
        previousActor = actorId;
        const auto* actor = findObjectById(state, actorId);
        const auto behavior = state.behaviors.find(actorId);
        if (!actor || behavior == state.behaviors.end()
            || !runtimeRepackMoveGroupActor(state, *actor, behavior->second)) return fail();
        int previousMember = -1;
        for (int index = 0; index < memberCount; ++index) {
            int id = -1;
            if (!parseRuntimeInt(fields[4 + index], id) || id <= 0 || id <= previousMember || id >= INT_MAX) return fail();
            previousMember = id;
            intent.members.push_back(id);
        }
        if (!std::binary_search(intent.members.begin(), intent.members.end(), actorId)) return fail();
        // Exact snapshots already own their pending request, path and timing.
        // Attach only the independent intent after that behavior is restored.
        if (preserveExactBehavior && (!behavior->second.pendingHasSharedGoal
            || behavior->second.pendingSharedGoal != intent.center)) return fail();
        records.emplace(actorId, std::move(intent));
    }
    // Entire section is checked before touching even one destination behavior.
    for (auto& [id, intent] : records) {
        auto& move = state.behaviors.at(id);
        move.repackMoveGroup = std::move(intent);
        if (!preserveExactBehavior) refreshRuntimeRepackMoveGroup(state, *findObjectById(state, id), move);
    }
    return true;
}

std::string serializeRuntimeActiveJumpjetFlights(const RuntimeGameSessionState& state,
    bool includePresentation) {
    RuntimeJumpjetFlights active;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || !object.airborne || !runtimeJumpjetProfile(state, object)) continue;
        const auto found = state.jumpjetFlights.find(object.instanceId);
        if (found != state.jumpjetFlights.end()) active.emplace(found->first, found->second);
    }
    return serializeRuntimeJumpjetFlights(active, includePresentation);
}

void writeRuntimeJumpjetFlights(const RuntimeGameSessionState& state, std::ostream& output) {
    const auto data = serializeRuntimeActiveJumpjetFlights(state);
    output << "[JumpjetFlights]\nSchema=5"
        << "\nDataHex=" << iniHexEncode(data) << "\n\n";
}

bool readRuntimeJumpjetFlights(RuntimeProductionHostState& state, const IniFile& ini, std::string& error) {
    RuntimeJumpjetFlights staged;
    if (ini.getSection("JumpjetFlights")) {
        std::string payload;
        const auto schema=ini.getValue("JumpjetFlights", "Schema", "");
        if ((schema != "1" && schema != "2" && schema != "3" && schema != "4" && schema != "5")
            || !iniHexDecode(ini.getValue("JumpjetFlights", "DataHex", ""), payload, 16U*1024U*1024U)
            || !deserializeRuntimeJumpjetFlights(payload, staged)) {
            error = "invalid Jumpjet flight save section"; return false;
        }
        if (!payload.starts_with("JumpjetFlights " + schema + " ")) {
            error = "Jumpjet flight schema does not match payload"; return false;
        }
        for (const auto& [id, flight] : staged) {
            const auto* object = findObjectById(state, id);
            const auto* unit = object ? findRuntimeUnitType(state.rules, *object) : nullptr;
            if (!object || !object->alive || !unit || !runtimeUnitUsesJumpjetMovement(*unit)
                || flight.lastTick > state.tick || flight.verticalTick > state.tick
                || flight.clearanceTick > state.tick
                || ((flight.active || flight.stopped)
                    && (!object->airborne || object->x != flight.toX || object->y != flight.toY))) {
                error = "Jumpjet flight does not match saved world"; return false;
            }
        }
    }
    state.jumpjetFlights = std::move(staged);
    for (auto& object : state.world.accessObjects()) {
        const auto flight = state.jumpjetFlights.find(object.instanceId);
        // Restore the physical fractional pose before resolving a legacy
        // roof lift; the core object can still be at its committed cell.
        applyRuntimeJumpjetPose(state, object);
        if (flight != state.jumpjetFlights.end() && flight->second.clearanceTick < 0) {
            initializeRuntimeKirovClearanceMotion(state, object, flight->second);
            applyRuntimeJumpjetPose(state, object);
        }
    }
    return true;
}

void writeRuntimeCarrierFlightPoses(const RuntimeGameSessionState& state, std::ostream& output) {
    std::vector<const WorldObjectInstance*> children;
    for (const auto& [id, sortie] : state.specialMechanisms.carrierChildren()) {
        const auto* child = findObjectById(state, id);
        if (child && child->alive && child->airborne
            && sortie.phase != CarrierChildPhase::Docked
            && sortie.phase != CarrierChildPhase::Destroyed
            && !state.rules.findSpawnedMissileProfile(child->type)) children.push_back(child);
    }
    if (children.empty()) return;
    output << "\n[CarrierFlightPoses]\nSchema=2\nCount=" << children.size() << '\n';
    const auto precision = output.precision();
    output << std::setprecision(17);
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto& child = *children[i];
        output << i << '=' << child.instanceId << '|' << runtimeObjectRenderX(child) << '|' << runtimeObjectRenderY(child)
            << '|' << runtimeObjectRenderLevel(child) << '|' << runtimeBoolText(child.renderMoveActive)
            << '|' << child.movementAnimationDistance << '|' << runtimeBoolText(child.hasRenderPosition) << '\n';
        const auto order = state.behaviors.find(child.instanceId);
        if (order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack) {
            const auto& move = order->second;
            output << "Order" << i << '=' << iniHexEncode(
                runtimeMultiplayerSnapshotBehaviorRecord(child.instanceId, move)) << '\n';
            if (move.renderMoveTicksTotal > 0) {
                output << "Segment" << i << '=' << move.renderFrom.x << '|' << move.renderFrom.y
                    << '|' << move.renderTo.x << '|' << move.renderTo.y
                    << '|' << move.renderFromLevel << '|' << move.renderToLevel
                    << '|' << move.renderMoveTicksTotal << '|' << move.renderMoveTicksRemaining
                    << '|' << move.renderFromOffsetX << '|' << move.renderFromOffsetY
                    << '|' << move.renderToOffsetX << '|' << move.renderToOffsetY << '\n';
            }
        }
    }
    output.precision(precision);
}

bool validateRuntimeCarrierAircraftCellOrders(const RuntimeGameSessionState& state, std::string& error) {
    const auto fail = [&]() { error = "invalid carrier aircraft cell target"; return false; };
    // A synthetic target must remain bound to the existing terrain attack
    // order, rather than admitting arbitrary negative object references.
    for (const auto& [id, sortie] : state.specialMechanisms.carrierChildren()) {
        if (sortie.targetObjectId >= -1) continue;
        SpecialCell cell;
        bool bridge = false;
        const auto* child = findObjectById(state, id);
        const auto* unit = child ? findRuntimeUnitType(state.rules, *child) : nullptr;
        if (!decodeRuntimeCarrierCellTarget(sortie.targetObjectId, cell, bridge)
            || !unit || !unit->spawned || !runtimeUnitUsesAirMovement(*unit)
            || unit->missileSpawn || state.rules.findSpawnedMissileProfile(child->type)
            || !state.terrainByCell.contains(runtimeCellKey(cell.x, cell.y))) return fail();
        if (sortie.phase == CarrierChildPhase::Launching
            || sortie.phase == CarrierChildPhase::Attacking) {
            const auto order = state.behaviors.find(id);
            if (order == state.behaviors.end() || order->second.kind != RuntimeBehaviorKind::Attack
                || order->second.targetObjectId != sortie.targetObjectId
                || order->second.attackBridge != bridge
                || order->second.attackGround == bridge
                || (!bridge && !order->second.forceFire)) return fail();
            if (bridge) {
                // A clicked bridge lane resolves to the span's damage anchor;
                // the order retains the requested lane, not that anchor.
                const auto target = runtimeBridgeAttackTargetObject(state, order->second.attackBridgeCell);
                if (!target || target->instanceId != sortie.targetObjectId) return fail();
            } else if (order->second.attackBridgeCell.x != cell.x
                || order->second.attackBridgeCell.y != cell.y) return fail();
        }
    }
    return true;
}

bool parseRuntimeCarrierFlightPoses(const RuntimeGameSessionState& state, const IniFile& ini,
    std::vector<RuntimeCarrierFlightPoseRecord>& poses, std::string& error) {
    constexpr const char* section = "CarrierFlightPoses";
    const auto fail = [&]() { error = "invalid carrier aircraft flight pose"; return false; };
    if (!validateRuntimeCarrierAircraftCellOrders(state, error)) return false;
    if (!ini.getSection(section)) return true;
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.specialMechanisms.carrierChildren().size()) return fail();
    std::unordered_set<int> seen;
    std::vector<RuntimeCarrierFlightPoseRecord> staged;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        RuntimeCarrierFlightPoseRecord pose;
        if (fields.size() != (schema == 1 ? 6U : 7U) || !parseRuntimeInt(fields[0], pose.objectId)
            || !seen.insert(pose.objectId).second
            || !parseRuntimeFiniteDouble(fields[1], pose.x)
            || !parseRuntimeFiniteDouble(fields[2], pose.y)
            || !parseRuntimeFiniteDouble(fields[3], pose.level)
            || !parseRuntimeStrictBool(fields[4], pose.moving)
            || !parseRuntimeFiniteDouble(fields[5], pose.animationDistance)
            || (schema == 2 && !parseRuntimeStrictBool(fields[6], pose.hasRenderPosition))
            || (pose.moving && !pose.hasRenderPosition)
            || pose.x < -0.5 || pose.x > 512.5 || pose.y < -0.5 || pose.y > 512.5
            || pose.level < -128.0 || pose.level > 512.0 || pose.animationDistance < 0.0) return fail();
        const auto* child = findObjectById(state, pose.objectId);
        const auto sortie = state.specialMechanisms.carrierChildren().find(pose.objectId);
        const auto* unit = child ? findRuntimeUnitType(state.rules, *child) : nullptr;
        if (!child || !child->alive || !child->airborne || !unit || !unit->spawned
            || !runtimeUnitUsesAirMovement(*unit) || unit->missileSpawn
            || state.rules.findSpawnedMissileProfile(child->type)
            || child->dockedToObjectId >= 0 || child->dockIndex >= 0
            || sortie == state.specialMechanisms.carrierChildren().end()
            || sortie->second.phase == CarrierChildPhase::Docked
            || sortie->second.phase == CarrierChildPhase::Destroyed
            || std::abs(pose.x - child->x) >= 2.0
            || std::abs(pose.y - child->y) >= 2.0) return fail();
        const auto savedOrder = ini.getValue(section, "Order" + std::to_string(i), "");
        if (!savedOrder.empty()) {
            std::string text;
            int orderId = -1;
            RuntimeObjectBehavior order;
            const auto ordinary = state.behaviors.find(pose.objectId);
            if (!iniHexDecode(savedOrder, text, 1024U * 1024U)
                || !parseRuntimeMultiplayerSnapshotBehaviorRecord(text, orderId, order)
                || orderId != pose.objectId || order.kind != RuntimeBehaviorKind::Attack
                || ordinary == state.behaviors.end() || ordinary->second.kind != order.kind
                || ordinary->second.targetObjectId != order.targetObjectId
                || ordinary->second.forceFire != order.forceFire
                || ordinary->second.attackGround != order.attackGround
                || ordinary->second.attackBridge != order.attackBridge
                || ordinary->second.attackBridgeCell != order.attackBridgeCell
                || ordinary->second.moveCooldownTicks != order.moveCooldownTicks
                || order.moveCooldownTicks < 0 || order.moveCooldownTicks > 100000
                || order.weaponCooldownTicks < 0 || order.weaponCooldownTicks > 100000
                || order.moveIntervalRemainderQ16 < -0x8000 || order.moveIntervalRemainderQ16 > 0x7fff
                || order.blockedTicks < 0 || order.pathReplans < 0 || order.nextRepathTick < 0
                || !std::isfinite(order.pendingRangeCells) || order.pendingRangeCells < 0.0
                || order.pendingRangeCells > 10000.0) return fail();
            for (const auto& cell : order.path)
                if (cell.x < 0 || cell.x > 999 || cell.y < 0 || cell.y > 999) return fail();
            const auto segment = ini.getValue(section, "Segment" + std::to_string(i), "");
            if (!segment.empty()) {
                const auto fields = splitRuntimeSaveFields(segment);
                if (fields.size() != (schema == 1 ? 8U : 12U) || !parseRuntimeInt(fields[0], order.renderFrom.x)
                    || !parseRuntimeInt(fields[1], order.renderFrom.y)
                    || !parseRuntimeInt(fields[2], order.renderTo.x)
                    || !parseRuntimeInt(fields[3], order.renderTo.y)
                    || !parseRuntimeFiniteDouble(fields[4], order.renderFromLevel)
                    || !parseRuntimeFiniteDouble(fields[5], order.renderToLevel)
                    || !parseRuntimeInt(fields[6], order.renderMoveTicksTotal)
                    || !parseRuntimeInt(fields[7], order.renderMoveTicksRemaining)
                    || order.renderFrom.x < 0 || order.renderFrom.x > 999
                    || order.renderFrom.y < 0 || order.renderFrom.y > 999
                    || order.renderTo.x != child->x || order.renderTo.y != child->y
                    || std::abs(order.renderTo.x - order.renderFrom.x) > 1
                    || std::abs(order.renderTo.y - order.renderFrom.y) > 1
                    || order.renderFrom == order.renderTo || !pose.moving
                    || order.renderFromLevel < -128.0 || order.renderFromLevel > 512.0
                    || order.renderToLevel != child->level
                    || order.renderMoveTicksTotal <= 0 || order.renderMoveTicksTotal > 100000
                    || order.renderMoveTicksRemaining <= 0
                    || order.renderMoveTicksRemaining > order.renderMoveTicksTotal
                    || order.renderMoveTicksRemaining != order.moveCooldownTicks) return fail();
                if (schema == 2 && (!parseRuntimeFiniteDouble(fields[8], order.renderFromOffsetX)
                    || !parseRuntimeFiniteDouble(fields[9], order.renderFromOffsetY)
                    || !parseRuntimeFiniteDouble(fields[10], order.renderToOffsetX)
                    || !parseRuntimeFiniteDouble(fields[11], order.renderToOffsetY)
                    || std::abs(order.renderFromOffsetX) >= 2.0 || std::abs(order.renderFromOffsetY) >= 2.0
                    || order.renderToOffsetX != 0.0 || order.renderToOffsetY != 0.0)) return fail();
            } else if (order.moveCooldownTicks > 0) return fail();
            pose.order = std::move(order);
        }
        staged.push_back(pose);
    }
    poses = std::move(staged);
    return true;
}

void restoreRuntimeCarrierFlightPoses(RuntimeGameSessionState& state,
    const std::vector<RuntimeCarrierFlightPoseRecord>& poses) {
    for (const auto& pose : poses) {
        if (auto* child = findMutableObjectById(state, pose.objectId)) {
            if (pose.order) {
                state.behaviors[pose.objectId] = *pose.order;
                if (pose.order->pathRequestPending
                    && std::find(state.pendingPathObjectIds.begin(), state.pendingPathObjectIds.end(),
                        pose.objectId) == state.pendingPathObjectIds.end())
                    state.pendingPathObjectIds.push_back(pose.objectId);
            }
            child->hasRenderPosition = pose.hasRenderPosition; child->renderMoveActive = pose.moving;
            child->renderX = pose.x; child->renderY = pose.y; child->renderLevel = pose.level;
            child->renderOnBridge = false;
            child->movementAnimationDistance = pose.animationDistance;
            applyRuntimeCarrierDeckFlightPresentation(state, *child);
        }
    }
}

void restoreRuntimeCarrierFlightSegments(RuntimeGameSessionState& state,
    const std::vector<RuntimeCarrierFlightPoseRecord>& poses) {
    for (const auto& pose : poses) {
        if (auto* child = findMutableObjectById(state, pose.objectId))
            applyRuntimeCarrierDeckFlightPresentation(state, *child);
        const auto order = state.behaviors.find(pose.objectId);
        if (!pose.order || order == state.behaviors.end()) continue;
        auto& move = order->second;
        move.renderFrom = pose.order->renderFrom; move.renderTo = pose.order->renderTo;
        move.renderFromLevel = pose.order->renderFromLevel; move.renderToLevel = pose.order->renderToLevel;
        move.renderMoveTicksTotal = pose.order->renderMoveTicksTotal;
        move.renderMoveTicksRemaining = pose.order->renderMoveTicksRemaining;
        move.renderFromOffsetX = pose.order->renderFromOffsetX;
        move.renderFromOffsetY = pose.order->renderFromOffsetY;
        move.renderToOffsetX = pose.order->renderToOffsetX;
        move.renderToOffsetY = pose.order->renderToOffsetY;
    }
}

bool runtimeJumpjetSavedMoveObjectIsEligible(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeJumpjetFlight& flight) {
    return state.rulesLoaded && object.alive && object.health > 0 && object.airborne
        && object.factoryExitHostId < 0
        && toUpperAsciiLocal(trimAsciiLocal(object.type)) == "ZEP"
        && runtimeJumpjetProfile(state, object) && (flight.active || flight.stopped);
}

bool runtimeJumpjetSavedMoveCellIsValid(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& cell) {
    return cell.x >= 0 && cell.x < 512 && cell.y >= 0 && cell.y < 512
        && findRuntimeTerrainCellAt(state, cell) != nullptr;
}

bool runtimeJumpjetSavedPathKindIsEligible(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    if (behavior.kind == RuntimeBehaviorKind::Move) return true;
    if (behavior.kind != RuntimeBehaviorKind::Attack || behavior.attackGround
        || behavior.attackBridge || behavior.targetObjectId < 0) return false;
    const auto* target = findObjectById(state, behavior.targetObjectId);
    return target && runtimeKirovBuildingBombProfile(object, *target,
            runtimeWeaponProfileForObject(state, object, target, behavior.forceFire))
        && behavior.moveTarget == runtimeTargetCellForProjectile(state, *target);
}

bool runtimeJumpjetSavedMoveRouteIsValid(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeJumpjetFlight& flight, const RuntimeObjectBehavior& behavior) {
    if (!runtimeJumpjetSavedPathKindIsEligible(state, object, behavior)
        || behavior.pathRequestPending
        || behavior.state == RuntimeBehaviorState::Idle
        || behavior.state == RuntimeBehaviorState::Complete
        || behavior.path.size() > runtimeJumpjetSavedMoveMaximumNodes
        || behavior.moveCooldownTicks <= 0 || behavior.weaponCooldownTicks < 0
        || behavior.moveIntervalRemainderQ16 < -0x8000
        || behavior.moveIntervalRemainderQ16 > 0x7fff
        || behavior.blockedTicks < 0 || behavior.pathReplans < 0
        || behavior.nextRepathTick < 0 || behavior.approachSlotIndex < 0
        || !std::isfinite(behavior.pendingRangeCells) || behavior.pendingRangeCells < 0.0
        || behavior.factoryExitClearanceHostId >= 0
        || behavior.attackBridge || behavior.attackGround
        || flight.lastTick > state.tick || flight.verticalTick > state.tick
        || flight.clearanceTick > state.tick
        || flight.length <= 0 || flight.progress >= flight.length
        || object.x != flight.toX || object.y != flight.toY
        || behavior.moveSegmentFrom != RuntimeCellPoint{flight.fromX, flight.fromY}
        || !runtimeJumpjetSavedMoveCellIsValid(state, behavior.moveTarget)) return false;
    RuntimeCellPoint previous = runtimeCellPointForObject(object);
    if (!runtimeJumpjetSavedMoveCellIsValid(state, previous)
        || !runtimeJumpjetSavedMoveCellIsValid(state, behavior.moveSegmentFrom)) return false;
    for (const auto& cell : behavior.path) {
        if (!runtimeJumpjetSavedMoveCellIsValid(state, cell) || cell == previous
            || std::abs(cell.x - previous.x) > 1
            || std::abs(cell.y - previous.y) > 1) return false;
        previous = cell;
    }
    // Empty is meaningful: the saved segment really may be terminal or waiting
    // for a new route. Future occupancy is checked by normal air navigation.
    return behavior.path.empty() || behavior.path.back() == behavior.moveTarget;
}

bool runtimeJumpjetSavedMoveMatchesOrdinary(
    const RuntimeObjectBehavior& restored, const RuntimeObjectBehavior& ordinary) {
    return restored.kind == ordinary.kind && restored.moveTarget == ordinary.moveTarget
        && restored.targetObjectId == ordinary.targetObjectId
        && restored.moveCooldownTicks == ordinary.moveCooldownTicks
        && restored.weaponCooldownTicks == ordinary.weaponCooldownTicks
        && restored.moveIntervalRemainderQ16 == ordinary.moveIntervalRemainderQ16
        && restored.pendingAllowedOccupiedObjectId == ordinary.pendingAllowedOccupiedObjectId
        && restored.attackBridge == ordinary.attackBridge
        && restored.attackBridgeCell == ordinary.attackBridgeCell
        && restored.forceFire == ordinary.forceFire
        && restored.attackGround == ordinary.attackGround
        && restored.moveSegmentFrom == ordinary.moveSegmentFrom
        && restored.factoryExitClearanceHostId == ordinary.factoryExitClearanceHostId;
}

void writeRuntimeJumpjetMovePaths(const RuntimeGameSessionState& state, std::ostream& output) {
    std::vector<int> ids;
    std::size_t nodes = 0;
    for (const auto& [id, behavior] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        const auto flight = state.jumpjetFlights.find(id);
        if (!object || flight == state.jumpjetFlights.end()
            || !runtimeJumpjetSavedMoveObjectIsEligible(state, *object, flight->second)
            || !runtimeJumpjetSavedPathKindIsEligible(state, *object, behavior)
            || behavior.pathRequestPending
            || behavior.state == RuntimeBehaviorState::Idle
            || behavior.state == RuntimeBehaviorState::Complete) continue;
        if (!runtimeJumpjetSavedMoveRouteIsValid(state, *object, flight->second, behavior)
            || behavior.path.size() > runtimeJumpjetSavedMoveMaximumNodes - nodes
            || ids.size() >= 100000U) {
            output.setstate(std::ios::failbit);
            return;
        }
        nodes += behavior.path.size();
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[JumpjetMovePaths]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t index = 0; index < ids.size(); ++index)
        output << index << '=' << runtimeMultiplayerSnapshotBehaviorRecord(
            ids[index], state.behaviors.at(ids[index])) << '\n';
}

bool restoreRuntimeJumpjetMovePaths(
    RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    const auto* section = ini.getSection("JumpjetMovePaths");
    if (!section) return true;
    const auto fail = [&](int id, const char* reason) {
        error = "invalid Jumpjet move path"
            + (id > 0 ? " #" + std::to_string(id) : std::string{}) + ": " + reason;
        return false;
    };
    int schema = 0, count = 0;
    if (!parseRuntimeInt(ini.getValue("JumpjetMovePaths", "Schema", ""), schema)
        || schema != 1
        || !parseRuntimeInt(ini.getValue("JumpjetMovePaths", "Count", ""), count)
        || count < 0 || count > 100000
        || static_cast<std::size_t>(count) > state.behaviors.size()) return fail(-1, "header");
    std::unordered_set<std::string> keys;
    for (const auto& [key, value] : *section) {
        (void)value;
        const auto normalized = toLowerAsciiLocal(trimAsciiLocal(key));
        if (!keys.insert(normalized).second) return fail(-1, "duplicate key");
        if (normalized == "schema" || normalized == "count") continue;
        int index = -1;
        if (!parseRuntimeInt(normalized, index) || index < 0 || index >= count
            || normalized != std::to_string(index)) return fail(-1, "record index");
    }
    if (keys.size() != static_cast<std::size_t>(count) + 2U) return fail(-1, "record count");
    RuntimeJumpjetFlights flights;
    if (count > 0) {
        std::string payload;
        const auto flightSchema = ini.getValue("JumpjetFlights", "Schema", "");
        if ((flightSchema != "1" && flightSchema != "2" && flightSchema != "3"
                && flightSchema != "4" && flightSchema != "5")
            || !iniHexDecode(ini.getValue("JumpjetFlights", "DataHex", ""), payload,
                16U * 1024U * 1024U)
            || !payload.starts_with("JumpjetFlights " + flightSchema + " ")
            || !deserializeRuntimeJumpjetFlights(payload, flights)) return fail(-1, "flight authority");
    }
    std::unordered_set<int> seen;
    std::vector<std::pair<int, RuntimeObjectBehavior>> staged;
    staged.reserve(static_cast<std::size_t>(count));
    std::size_t nodes = 0;
    for (int index = 0; index < count; ++index) {
        const auto record = ini.getValue("JumpjetMovePaths", std::to_string(index), "");
        const auto fields = splitRuntimeSaveFields(record);
        int expectedId = -1, objectId = -1;
        if (record.size() > 16U * 1024U * 1024U || fields.empty()
            || !parseRuntimeInt(fields[0], expectedId) || expectedId <= 0
            || !seen.insert(expectedId).second) return fail(expectedId, "object ID/record size");
        const auto ordinary = state.behaviors.find(expectedId);
        const auto* object = findObjectById(state, expectedId);
        const auto flight = flights.find(expectedId);
        if (!object || ordinary == state.behaviors.end() || flight == flights.end()
            || !runtimeJumpjetSavedPathKindIsEligible(state, *object, ordinary->second)
            || !runtimeJumpjetSavedMoveObjectIsEligible(state, *object, flight->second))
            return fail(expectedId, "Kirov/flight/ordinary move authority");
        RuntimeObjectBehavior restored = ordinary->second;
        if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(record, objectId, restored)
            || objectId != expectedId) return fail(expectedId, "native behavior record");
        if (!runtimeJumpjetSavedMoveMatchesOrdinary(restored, ordinary->second))
            return fail(expectedId, "ordinary authority differs");
        if (!runtimeJumpjetSavedMoveRouteIsValid(state, *object, flight->second, restored))
            return fail(expectedId, "route/segment/goal/clock geometry");
        if (restored.path.size() > runtimeJumpjetSavedMoveMaximumNodes - nodes)
            return fail(expectedId, "total node budget");
        nodes += restored.path.size();
        staged.emplace_back(expectedId, std::move(restored));
    }
    for (auto& [id, behavior] : staged) state.behaviors.at(id) = std::move(behavior);
    return true;
}

RuntimeGateContext runtimeSavedGateContext(
    const RuntimeGameSessionState& state,
    const std::vector<WorldObjectInstance>& objects) {
    RuntimeGateContext context;
    context.rules = &state.rules;
    context.objects = &objects;
    context.areAllied = [&state](
        const std::string& left,
        const std::string& right
    ) {
        return runtimeOwnersAreAllied(state, left, right);
    };
    context.isGroundMover = [&state](const WorldObjectInstance& object) {
        return runtimeGateObjectIsGroundMover(state, object);
    };
    context.doorwayOccupied = [](int gateId) {
        (void)gateId;
        return false;
    };
    return context;
}

RuntimeSubcellContext runtimeSavedGateSubcellContext(
    const RuntimeGateSystemState& gates) {
    RuntimeSubcellContext context;
    for (const RuntimeGateInstanceState& gate : gates.gates()) {
        if (gate.phase == RuntimeGatePhase::Open) {
            context.ignoredWholeCellObjectIds.insert(gate.gateId);
        }
    }
    return context;
}

const TerrainCell* findRuntimeTerrainCellAt(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    const int key = runtimeCellKey(cell.x, cell.y);
    const auto cachedIt = state.terrainByCell.find(key);
    if (cachedIt != state.terrainByCell.end()) {
        return cachedIt->second;
    }
    // Entries come from this world's cells. Equal counts make a cache miss
    // authoritative; empty or partial indices retain the linear fallback.
    if (state.terrainByCell.size() == state.world.getTerrainCells().size()) {
        return nullptr;
    }
    for (const TerrainCell& terrain : state.world.getTerrainCells()) {
        if (runtimeCellKey(terrain.x, terrain.y) == key) {
            return &terrain;
        }
    }
    return nullptr;
}

TerrainCell* findMutableRuntimeTerrainCellAt(
    RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    const int key = runtimeCellKey(cell.x, cell.y);
    const auto cachedIt = state.terrainByCell.find(key);
    if (cachedIt != state.terrainByCell.end()) {
        return const_cast<TerrainCell*>(cachedIt->second);
    }
    // Entries come from this world's cells. Equal counts make a cache miss
    // authoritative; empty or partial indices retain the linear fallback.
    if (state.terrainByCell.size() == state.world.getTerrainCells().size()) {
        return nullptr;
    }
    for (TerrainCell& terrain : state.world.accessTerrainCells()) {
        if (runtimeCellKey(terrain.x, terrain.y) == key) {
            return &terrain;
        }
    }
    return nullptr;
}

std::optional<RuntimeOreOverlayIdentity> runtimeOreIdentityFromTerrain(
    const TerrainCell& terrain) {
    if (!terrain.hasOverlay || (!terrain.overlayIsOre && !terrain.overlayIsGem)) {
        return std::nullopt;
    }

    return runtimeOreIdentityForVariant(
        runtimeResourceFamilyForTerrain(terrain),
        runtimeResourceVariantForTerrain(terrain),
        runtimeResourceBailCountForTerrain(terrain)
    );
}

const ArtType* runtimeArtTypeForTerrainObject(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    const ArtType* art = state.art.findArtType(object.type);
    if (art != nullptr) {
        return art;
    }
    return nullptr;
}

int runtimeTerrainObjectTiberiumSpreadRadius(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    const ArtType* art = runtimeArtTypeForTerrainObject(state, object);
    return art == nullptr ? -1 : art->tiberiumSpreadRadius;
}

int runtimeTerrainObjectOreGrowthRadius(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    const int artRadius =
        runtimeTerrainObjectTiberiumSpreadRadius(state, object);
    return artRadius >= 0 ? artRadius : 6;
}

std::optional<RuntimeOreOverlayIdentity> runtimeOreIdentityFromTerrainObjectArt(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    const ArtType* art = runtimeArtTypeForTerrainObject(state, object);
    if (art == nullptr) {
        return std::nullopt;
    }
    return runtimeOreIdentityFromSpawnType(art->tiberiumSpawnType);
}

RuntimeOreOverlayIdentity runtimeOreIdentityForTerrainObject(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    const std::optional<RuntimeOreOverlayIdentity> artIdentity =
        runtimeOreIdentityFromTerrainObjectArt(state, object);
    RuntimeOreOverlayIdentity identity = artIdentity.has_value()
        ? *artIdentity
        : runtimeOreIdentityNearCell(state, {object.x, object.y});
    // A terrain spawner chooses the ore family/variant here, but a new cell
    // always begins at the lowest density and grows on later rule ticks.
    identity.bails = runtimeOreLowestBails;
    return identity;
}

bool runtimeTerrainObjectSpawnsOre(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    if (!object.alive || object.health <= 0) {
        return false;
    }
    const TerrainType* terrainType = state.rules.findTerrainType(object.type);
    if (terrainType != nullptr && terrainType->spawnsTiberium) {
        return true;
    }
    if (runtimeOreIdentityFromTerrainObjectArt(state, object).has_value()) {
        return true;
    }

    const std::string type = toLowerAsciiLocal(object.type);
    return type.rfind("tibtre", 0) == 0;
}

int runtimeTerrainOreSpawnCooldownTicks(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    const TerrainType* terrainType = state.rules.findTerrainType(object.type);
    if (terrainType != nullptr && terrainType->animationProbability > 0.0) {
        const double cooldown =
            std::floor(1.0 / terrainType->animationProbability);
        if (std::isfinite(cooldown) && cooldown > 0.0) {
            return std::max(1, static_cast<int>(cooldown));
        }
    }

    return 333;
}

double runtimeTerrainOreSpawnProbability(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    const TerrainType* terrainType = state.rules.findTerrainType(object.type);
    if (terrainType != nullptr && terrainType->animationProbability > 0.0) {
        double probability = terrainType->animationProbability;
        if (probability > 1.0) {
            probability /= 100.0;
        }
        return std::clamp(probability, 0.0, 1.0);
    }
    return 1.0 / 333.0;
}

bool runtimeTerrainOreSpawnEventDue(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object,
    int tick) {
    if (tick <= 0) {
        return false;
    }

    const double probability =
        runtimeTerrainOreSpawnProbability(state, object);
    if (probability <= 0.0) {
        return false;
    }
    const int interval = runtimeTerrainOreSpawnCooldownTicks(state, object);
    if (probability >= 1.0 || interval <= 1) {
        return true;
    }

    std::uint64_t hash =
        runtimeStableHashString("runtimeTerrainOreSpawnPhase");
    runtimeStableHashMixString(hash, object.type);
    runtimeStableHashMix(hash, object.x);
    runtimeStableHashMix(hash, object.y);
    const int phase = static_cast<int>(hash % static_cast<std::uint64_t>(interval));
    const int offset = (tick - phase) % interval;
    return (offset < 0 ? offset + interval : offset) == 0;
}

std::unordered_set<int> buildRuntimeTerrainObjectCellLookup(
    const RuntimeGameSessionState& state) {
    std::unordered_set<int> occupiedCells;
    occupiedCells.reserve(state.world.getTerrainObjects().size());
    for (const WorldTerrainObject& object : state.world.getTerrainObjects()) {
        if (!object.alive || object.health <= 0) {
            continue;
        }
        const int key = runtimeCellKey(object.x, object.y);
        if (key >= 0) {
            occupiedCells.insert(key);
        }
    }
    return occupiedCells;
}

bool runtimeCellCanSpawnNewOreIgnoringTerrainObjects(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell,
    const std::unordered_map<int, int>& occupiedByCell) {
    static const std::unordered_set<int> emptyTerrainObjectCells;
    return runtimeCellCanSpawnNewOre(
        state,
        cell,
        occupiedByCell,
        emptyTerrainObjectCells
    );
}

bool runtimeTerrainObjectOreSpawnAllowedByMode(
    const RuntimeGameSessionState& state,
    const WorldTerrainObject& object) {
    if (!state.skirmishModeRulesLoaded) {
        return true;
    }
    const RuntimeOreOverlayIdentity identity =
        runtimeOreIdentityForTerrainObject(state, object);
    const TiberiumType* tiberium = runtimeTiberiumTypeForOreIdentity(
        state.rules,
        identity
    );
    return tiberium == nullptr
        || runtimeOreRuleProbability(state, *tiberium, true) > 0.0;
}

RuntimeCameraWorldBounds runtimeFullTerrainCameraWorldBounds(
    const RuntimeProductionHostState& state) {
    RuntimeCameraWorldBounds bounds;
    const int halfWidth = std::max(1, state.camera.tileWidth / 2);
    const int halfHeight = std::max(1, state.camera.tileHeight / 2);
    for (const TerrainCell& cell : state.world.getTerrainCells()) {
        const int projectedX =
            ((cell.x - cell.y) * state.camera.tileWidth) / 2;
        const int projectedY =
            ((cell.x + cell.y) * state.camera.tileHeight) / 2
            - ((cell.level * state.camera.tileHeight) / 2);
        const int left = projectedX - halfWidth;
        const int right = projectedX + halfWidth;
        const int top = projectedY - halfHeight;
        const int bottom = projectedY + halfHeight;
        if (!bounds.valid) {
            bounds = RuntimeCameraWorldBounds{
                left,
                top,
                right,
                bottom,
                true
            };
        } else {
            bounds.left = std::min(bounds.left, left);
            bounds.top = std::min(bounds.top, top);
            bounds.right = std::max(bounds.right, right);
            bounds.bottom = std::max(bounds.bottom, bottom);
        }
    }
    return bounds;
}

RuntimeCellPoint formationOffsetForIndex(int index, int spacing) {
    spacing = std::max(1, spacing);
    if (index <= 0) {
        return {0, 0};
    }

    int ring = 1;
    while (((ring * 2) + 1) * ((ring * 2) + 1) <= index) {
        ++ring;
    }

    const int previousRingSide = (ring * 2) - 1;
    const int rawSlot = index - (previousRingSide * previousRingSide);
    const int side = ring * 2;
    const int perimeter = std::max(1, side * 4);
    // Keep the formation compact, but avoid filling each ring from one corner
    // in a visible queue. For perimeter 8r, step 4r-1 is coprime, so each
    // slot is still used exactly once.
    const int permutationStep = std::max(1, (side * 2) - 1);
    const int slot = ((rawSlot * permutationStep) + (ring * 3)) % perimeter;
    RuntimeCellPoint offset{-ring, -ring};
    if (slot < side) {
        offset.x = -ring + slot;
        offset.y = -ring;
    } else if (slot < side * 2) {
        offset.x = ring;
        offset.y = -ring + (slot - side);
    } else if (slot < side * 3) {
        offset.x = ring - (slot - (side * 2));
        offset.y = ring;
    } else {
        offset.x = -ring;
        offset.y = ring - (slot - (side * 3));
    }

    offset.x *= spacing;
    offset.y *= spacing;
    return offset;
}

int terrainLevelAtRuntimeCell(
    const RuntimeGameSessionState& state,
    int x,
    int y) {
    const int key = runtimeCellKey(x, y);
    const auto cachedIt = state.terrainByCell.find(key);
    if (cachedIt != state.terrainByCell.end() && cachedIt->second != nullptr) {
        return runtimeEffectiveTerrainLevel(*cachedIt->second);
    }

    for (const TerrainCell& cell : state.world.getTerrainCells()) {
        if (runtimeCellKey(cell.x, cell.y) == key) {
            return runtimeEffectiveTerrainLevel(cell);
        }
    }
    return 0;
}

bool runtimeCellIsBridge(
    const RuntimeGameSessionState& state,
    int x,
    int y) {
    const int key = runtimeCellKey(x, y);
    const auto cachedIt = state.terrainByCell.find(key);
    if (cachedIt != state.terrainByCell.end() && cachedIt->second != nullptr) {
        return runtimeCachedTerrainInfoForCell(state, *cachedIt->second).bridge;
    }

    for (const TerrainCell& cell : state.world.getTerrainCells()) {
        if (runtimeCellKey(cell.x, cell.y) == key) {
            return runtimeCachedTerrainInfoForCell(state, cell).bridge;
        }
    }

    return false;
}

void refreshRuntimeBridgeMutationState(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeCellPoint>& changedCells) {
    if (state.drainingObjectExits) {
        state.deferredBridgeMutationCells.insert(state.deferredBridgeMutationCells.end(),
            changedCells.begin(), changedCells.end());
        return;
    }
    std::unordered_set<int> changedCellKeys;
    for (const RuntimeCellPoint& point : changedCells) {
        const int key = runtimeCellKey(point.x, point.y);
        const auto terrainIt = state.terrainByCell.find(key);
        if (terrainIt == state.terrainByCell.end()
            || terrainIt->second == nullptr) {
            continue;
        }
        changedCellKeys.insert(key);
        state.terrainInfoByCell[key] =
            runtimeNavigationTerrainInfoForCell(state, *terrainIt->second);
    }

    state.attackApproachCache.clear();
    state.pathFailureCache.clear();
    state.pathTerrainCache.valid = false;
    state.pathTerrainCache.terrainCellCount = 0;
    state.pathTerrainCache.staticCells.clear();
    state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();
    invalidateRuntimeTickOccupancyCache(state);
    invalidateRuntimeObjectProjectionCache(state);
    state.renderCache.terrainLayer = RuntimeTerrainLayerCache{};
    state.radarTerrainKey.clear();
    state.radarProjectionCacheValid = false;
    state.repaintRequested = true;

    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive) {
            continue;
        }
        if (object.airborne && object.hasRenderPosition && runtimeCarrierHasDeckFlight(state, object)) {
            applyRuntimeCarrierDeckFlightPresentation(state, object); continue;
        }
        const bool stoppedPose = worldObjectHasStoppedGroundPose(object);
        const bool stoppedTerrainChanged = stoppedPose
            && (changedCellKeys.contains(runtimeCellKey(object.x, object.y))
                || changedCellKeys.contains(runtimeCellKey(
                    static_cast<int>(std::lround(object.renderX)),
                    static_cast<int>(std::lround(object.renderY)))));
        const auto terrainIt = state.terrainByCell.find(
            runtimeCellKey(object.x, object.y)
        );
        const bool airborneObject =
            runtimeObjectMovementMode(state, object) == PathMovementMode::Air
            && object.airborne;
        if (airborneObject) {
            object.onBridge = false;
            object.level = object.flightLevel;
        } else {
            object.onBridge = terrainIt != state.terrainByCell.end()
                && terrainIt->second != nullptr
                && runtimeCachedTerrainInfoForCell(
                    state,
                    *terrainIt->second
                ).bridge;
            if (terrainIt != state.terrainByCell.end()
                && terrainIt->second != nullptr) {
                object.level = runtimeEffectiveTerrainLevel(*terrainIt->second);
            }
        }
        if (object.renderMoveActive) {
            const auto behaviorIt = state.behaviors.find(object.instanceId);
            if (behaviorIt != state.behaviors.end()
                && behaviorIt->second.renderMoveTicksTotal > 0) {
                if (behaviorIt->second.transportTransferKind != RuntimeTransportTransferKind::Boarding) {
                    behaviorIt->second.renderToLevel =
                        static_cast<double>(object.level);
                    behaviorIt->second.renderToOnBridge = object.onBridge;
                }
            } else {
                clearRuntimeRenderPosition(object);
            }
        } else if (!stoppedPose || stoppedTerrainChanged) {
            // A distant bridge refresh must not pull a stopped mover's
            // fractional slope height to its committed navigation cell.
            // Local terrain changes still use the normal new ground pose.
            object.renderLevel = static_cast<double>(object.level);
            object.renderOnBridge = object.onBridge;
        }
    }
    for (auto& object : state.world.accessObjects()) {
        applyRuntimeJumpjetPose(state, object);
        applyRuntimeFixedWingPose(state, object);
    }
    rebuildRuntimeNavigationStats(state);
}

int runtimeFacingFromRenderedDelta(double dx, double dy) {
    if (dx == 0.0 && dy == 0.0) return -1;
    constexpr double pi = 3.14159265358979323846;
    double degrees = std::atan2(dx - dy, -(dx + dy)) * 180.0 / pi;
    degrees = std::fmod(degrees + 360.0, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return static_cast<int>(std::lround(degrees * 256.0 / 360.0)) % 256;
}

int runtimeMoveSegmentFacing(const RuntimeObjectBehavior& move) {
    if (move.renderFromOffsetX == 0.0 && move.renderFromOffsetY == 0.0
        && move.renderToOffsetX == 0.0 && move.renderToOffsetY == 0.0)
        return runtimeFacingFromDelta(move.renderTo.x - move.renderFrom.x,
            move.renderTo.y - move.renderFrom.y);
    return runtimeFacingFromRenderedDelta(
        move.renderTo.x + move.renderToOffsetX - move.renderFrom.x - move.renderFromOffsetX,
        move.renderTo.y + move.renderToOffsetY - move.renderFrom.y - move.renderFromOffsetY);
}

void alignRuntimeFacingToRenderMove(WorldObjectInstance& object,
    const RuntimeObjectBehavior& move) {
    int facing = object.kind == WorldObjectKind::Unit && !object.airborne
        ? runtimeMoveSegmentFacing(move)
        : runtimeFacingFromDelta(move.renderTo.x - move.renderFrom.x,
            move.renderTo.y - move.renderFrom.y);
    if (runtimeInfantryMoveUsesCommittedCorner(object, move)) {
        const double first = std::hypot(move.renderFromOffsetX, move.renderFromOffsetY);
        const double second = std::hypot(double(move.renderTo.x - move.renderFrom.x),
            double(move.renderTo.y - move.renderFrom.y));
        const int ticks = std::max(2, move.renderMoveTicksTotal);
        const int firstTicks = std::clamp(static_cast<int>(std::ceil(ticks * first / (first + second))),
            1, ticks - 1);
        const bool approachingCorner = runtimeVehicleMoveRenderProgress(move) * ticks < firstTicks;
        facing = approachingCorner
            ? runtimeFacingFromRenderedDelta(-move.renderFromOffsetX, -move.renderFromOffsetY)
            : runtimeFacingFromDelta(move.renderTo.x - move.renderFrom.x, move.renderTo.y - move.renderFrom.y);
    }
    if (facing >= 0) object.facing = facing;
}

int runtimeMoveTargetFacing(const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    if (object.kind != WorldObjectKind::Unit || object.airborne)
        return runtimeFacingFromDelta(to.x - from.x, to.y - from.y);
    const auto dock = runtimeRepairDepotApproachPoint(state, object, to);
    const double fromX = object.hasRenderPosition ? object.renderX : double(from.x);
    const double fromY = object.hasRenderPosition ? object.renderY : double(from.y);
    const double toX = dock ? (*dock)[0] : double(to.x);
    const double toY = dock ? (*dock)[1] : double(to.y);
    if (fromX == from.x && fromY == from.y && toX == to.x && toY == to.y)
        return runtimeFacingFromDelta(to.x - from.x, to.y - from.y);
    return runtimeFacingFromRenderedDelta(toX - fromX, toY - fromY);
}

const UnitType* runtimeJumpjetProfile(const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    return unit && runtimeUnitUsesJumpjetMovement(*unit) && !unit->spawned && !unit->missileSpawn
        && !runtimeHasSupportFlightPose(object)
        ? unit : nullptr;
}

RuntimeJumpjetFlight& runtimeJumpjetFlightFor(RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    auto [it, inserted] = state.jumpjetFlights.try_emplace(object.instanceId);
    if (inserted) {
        it->second.height = std::llround((object.hasRenderPosition ? object.renderLevel : object.flightLevel)
            * runtimeJumpjetCellQ16);
        it->second.fromX = it->second.toX = object.x;
        it->second.fromY = it->second.toY = object.y;
    }
    return it->second;
}

bool runtimeJumpjetUsesKirovClearance(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    return object.alive && object.airborne
        && toUpperAsciiLocal(object.type) == "ZEP" && object.factoryExitHostId < 0
        && runtimeJumpjetProfile(state, object);
}

std::int64_t runtimeKirovClearanceTargetQ16(RuntimeProductionHostState& state,
    const WorldObjectInstance& object, double directionX, double directionY) {
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = state.artLoaded ? &state.art : nullptr;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    const auto* unit = runtimeJumpjetProfile(state, object);
    const double approachSpeed = unit ? unit->jumpjetSpeed * std::clamp(
        runtimeVeterancyAbilityMultiplier(state, object, VeteranAbility::Faster), 0.01, 10.0) : 0.0;
    const double target = state.geometry->runtimeKirovClearanceTargetLevel(state.world, object,
        context, state.renderCache, directionX, directionY, approachSpeed);
    return std::isfinite(target)
        ? std::llround(std::clamp(target, 0.0, 64.0) * runtimeJumpjetCellQ16) : 0;
}

void initializeRuntimeKirovClearanceMotion(RuntimeProductionHostState& state,
    const WorldObjectInstance& object, RuntimeJumpjetFlight& flight) {
    if (!runtimeJumpjetUsesKirovClearance(state, object)) {
        flight.clearanceLift = 0;
        flight.clearanceTick = -1;
        return;
    }
    if (!runtimeSessionGeometryAvailable(state)) return;
    // Legacy and newly created flights begin at their old current-position
    // roof pose. Forecasting belongs only to later simulation ticks.
    flight.clearanceLift = runtimeKirovClearanceTargetQ16(state, object, 0.0, 0.0);
    flight.clearanceTick = state.tick;
}

void advanceRuntimeKirovClearanceMotion(RuntimeProductionHostState& state,
    const WorldObjectInstance& object, RuntimeJumpjetFlight& flight, const UnitType& unit) {
    if (!runtimeJumpjetUsesKirovClearance(state, object)) {
        flight.clearanceLift = 0;
        flight.clearanceTick = -1;
        return;
    }
    if (!runtimeSessionGeometryAvailable(state)) return;
    if (flight.clearanceTick < 0) {
        initializeRuntimeKirovClearanceMotion(state, object, flight);
        return;
    }
    if (flight.clearanceTick == state.tick) return;
    double directionX = 0.0, directionY = 0.0;
    if (flight.active && !flight.stopped) {
        directionX = flight.toX - flight.fromX;
        directionY = flight.toY - flight.fromY;
    } else if (!flight.stopped) {
        const auto behavior = state.behaviors.find(object.instanceId);
        if (behavior != state.behaviors.end()
            && behavior->second.state != RuntimeBehaviorState::Idle
            && behavior->second.state != RuntimeBehaviorState::Complete) {
            RuntimeCellPoint destination{-1, -1};
            const auto& intent = behavior->second;
            // Ordinary saves rebuild future path cells only after the saved
            // segment completes. Forecast from persisted intent in every
            // branch, including the original with its warm path cache.
            if (intent.kind == RuntimeBehaviorKind::Move) {
                destination = flight.targetX >= 0 && flight.targetY >= 0
                    ? RuntimeCellPoint{flight.targetX, flight.targetY} : intent.moveTarget;
            } else if (intent.kind == RuntimeBehaviorKind::Attack) {
                const auto* targetObject = findObjectById(state, intent.targetObjectId);
                if (targetObject && targetObject->alive)
                    destination = runtimeCellPointForObject(*targetObject);
                else if (intent.attackGround || intent.attackBridge)
                    destination = intent.attackBridgeCell;
            }
            if (destination.x >= 0 && destination.y >= 0
                && destination.x < 512 && destination.y < 512) {
                directionX = destination.x - object.renderX;
                directionY = destination.y - object.renderY;
            }
        }
    }
    const double length = std::hypot(directionX, directionY);
    if (length > 0.0) { directionX /= length; directionY /= length; }
    const auto target = runtimeKirovClearanceTargetQ16(state, object, directionX, directionY);
    const double rate = std::clamp(target > flight.clearanceLift
        ? unit.jumpjetClimb : unit.jumpjetCrash, 0.0, 10000.0);
    const auto delta = std::max<std::int64_t>(0, std::llround(rate * runtimeJumpjetQ16 * 15.0
        / std::max(1, state.config.fixedTickHz)));
    flight.clearanceLift += std::clamp(target - flight.clearanceLift, -delta, delta);
    flight.clearanceTick = state.tick;
}

void applyRuntimeJumpjetPose(const RuntimeGameSessionState& state, WorldObjectInstance& object) {
    const auto found = state.jumpjetFlights.find(object.instanceId);
    object.aircraftBuildingClearanceLevel = -1.0;
    if (runtimeJumpjetUsesKirovClearance(state, object)
        && found != state.jumpjetFlights.end() && found->second.clearanceTick >= 0)
        object.aircraftBuildingClearanceLevel = double(found->second.clearanceLift) / runtimeJumpjetCellQ16;
    if (object.alive) {
        const auto* visualUnit = runtimeJumpjetProfile(state, object);
        if (visualUnit && runtimeUnitUsesAutomaticJumpjetLanding(*visualUnit)
            && object.factoryExitHostId < 0) {
            const auto ground = std::int64_t(terrainLevelAtRuntimeCell(state, object.x, object.y))
                * runtimeJumpjetCellQ16;
            const auto height = found != state.jumpjetFlights.end() ? found->second.height
                : std::llround((object.hasRenderPosition ? object.renderLevel : object.flightLevel)
                    * runtimeJumpjetCellQ16);
            object.aircraftDockFlightProgress = !object.airborne ? 0.0
                : std::clamp(double(height - ground)
                    / std::max<std::int64_t>(1, visualUnit->jumpJetHeight * runtimeJumpjetQ16), 0.0, 1.0);
        }
    }
    // The derived lift above is presentation only. Preserve the original
    // authority-pose no-op conditions for absent, dead and grounded flights.
    if (!runtimeHistoryQueryReference()
        && (found == state.jumpjetFlights.end() || !object.alive || !object.airborne)) return;
    const auto* unit = runtimeJumpjetProfile(state, object);
    if (found == state.jumpjetFlights.end() || !unit || !object.alive || !object.airborne) return;
    const auto& f = found->second;
    const bool segmentPose = (f.active || f.stopped) && f.length > 0;
    const double p = segmentPose ? double(f.progress) / f.length : 1.0;
    const double fromX = f.fromX, fromY = f.fromY;
    object.renderX = segmentPose ? fromX + (f.toX-fromX)*p : object.x;
    object.renderY = segmentPose ? fromY + (f.toY-fromY)*p : object.y;
    const auto cruise = terrainLevelAtRuntimeCell(state, object.x, object.y)*runtimeJumpjetCellQ16
        + unit->jumpJetHeight*runtimeJumpjetQ16;
    const double wobble = unit->jumpjetNoWobbles || unit->jumpjetWobbles <= 0.0 || f.height != cruise
        ? 0.0 : std::sin(f.wobblePhase * (6.2831853071795864769/65536.0)) * unit->jumpjetDeviation / 256.0;
    object.renderLevel = std::max(double(terrainLevelAtRuntimeCell(state, object.x, object.y)),
        double(f.height)/runtimeJumpjetCellQ16 + wobble);
    object.flightLevel = static_cast<int>((f.height + runtimeJumpjetCellQ16-1)/runtimeJumpjetCellQ16);
    object.level = object.flightLevel;
    object.onBridge = object.renderOnBridge = false;
    object.subCell = -1;
    object.hasRenderPosition = true;
    object.renderMoveActive = f.active;
}

bool advanceRuntimeJumpjetHeight(RuntimeProductionHostState& state, WorldObjectInstance& object, bool landing) {
    const auto* unit = runtimeJumpjetProfile(state, object);
    if (!unit) return false;
    if (!landing && runtimeFactoryAirExitWaitingForRoof(state, object)) return false;
    if (!landing && object.kind == WorldObjectKind::Unit
        && !state.pendingTransportTargets.empty()
        && runtimeCachedCapabilityProfileForObject(state, object).transport.canTransport)
        cancelRuntimeTransportBoardingForMovingHost(state, object.instanceId);
    auto& f = runtimeJumpjetFlightFor(state, object);
    int ground = terrainLevelAtRuntimeCell(state, object.x, object.y);
    if (!landing) {
        if (f.active || f.stopped) ground = std::max(ground, terrainLevelAtRuntimeCell(state, f.fromX, f.fromY));
        const auto behavior = state.behaviors.find(object.instanceId);
        if (behavior != state.behaviors.end() && !behavior->second.path.empty()) {
            const auto next = behavior->second.path.front();
            ground = std::max(ground, terrainLevelAtRuntimeCell(state, next.x, next.y));
        }
    }
    const std::int64_t target = ground * runtimeJumpjetCellQ16
        + (landing ? 0 : unit->jumpJetHeight * runtimeJumpjetQ16);
    if (f.verticalTick != state.tick) {
        f.verticalTick = state.tick;
        // Retail ordinary landing uses Climb; Crash is the wreck descent rate.
        // Keep the explicit deploy transition's existing clock separate.
        const double rate = std::clamp(landing && runtimeUnitUsesAutomaticJumpjetLanding(*unit)
            ? unit->jumpjetClimb : f.height > target ? unit->jumpjetCrash : unit->jumpjetClimb, 0.0, 10000.0);
        const auto delta = std::max<std::int64_t>(0, std::llround(rate * runtimeJumpjetQ16 * 15.0
            / std::max(1, state.config.fixedTickHz)));
        f.height += std::clamp(target-f.height, -delta, delta);
    }
    object.airborne = !landing || f.height > target;
    object.flightLevel = static_cast<int>((f.height + runtimeJumpjetCellQ16-1)/runtimeJumpjetCellQ16);
    object.level = object.flightLevel;
    object.renderLevel = double(f.height)/runtimeJumpjetCellQ16;
    applyRuntimeJumpjetPose(state, object);
    if (!object.airborne) {
        object.onBridge = object.renderOnBridge = runtimeCellIsBridge(state, object.x, object.y);
        object.renderMoveActive = false;
    }
    syncRuntimeTransportCargoPositions(state, object);
    return f.height == target;
}

bool runtimeJumpjetShouldLand(const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const UnitType& unit, const RuntimeJumpjetFlight& flight) {
    if (!runtimeUnitUsesAutomaticJumpjetLanding(unit) || flight.active || flight.stopped
        || object.renderMoveActive || object.factoryExitHostId >= 0) return false;
    const auto mission = toLowerAsciiLocal(object.mission);
    bool transportUnloading = false;
    if (mission == toLowerAsciiLocal(runtimeMissionName(RuntimeMissionIntent::Unloading))
        && object.kind == WorldObjectKind::Unit
        && !object.transportPassengerIds.empty()) {
        const auto transport = runtimeTransportProfileForObject(state, object);
        transportUnloading = transport.canTransport && transport.passengerCapacity > 0;
    }
    if (mission != "guard" && mission != "areaguard" && mission != "idle"
        && !transportUnloading) return false;
    const auto behavior = state.behaviors.find(object.instanceId);
    if (behavior != state.behaviors.end() && behavior->second.state != RuntimeBehaviorState::Complete
        && behavior->second.state != RuntimeBehaviorState::Idle) return false;
    const auto queue = state.playerCommands.queues.find(object.instanceId);
    if (queue != state.playerCommands.queues.end() && !queue->second.orders.empty()) return false;
    if (state.playerCommands.following.contains(object.instanceId)
        || state.playerCommands.travelling.contains(object.instanceId)) return false;
    const auto route = state.playerCommands.waypoints.routes.find(object.instanceId);
    if (route != state.playerCommands.waypoints.routes.end()
        && (route->second.active || route->second.cursor < route->second.points.size()
            || route->second.loopCommitted)) return false;
    const auto current = runtimeCellPointForObject(object);
    const auto terrain = state.terrainByCell.find(runtimeCellKey(current.x, current.y));
    if (terrain == state.terrainByCell.end() || !terrain->second) return false;
    const auto info = runtimeCachedTerrainInfoForCell(state, *terrain->second);
    // Retail ordinary jumpjet landing rejects Water and Beach; a valid bridge
    // deck is ground even when its underlying cell is water.
    if (!info.bridge && (info.water || info.shore)) return false;
    const auto ground = findRuntimePassableCellNear(state, object, current, 0, -1,
        nullptr, nullptr, nullptr, PathMovementMode::Ground);
    return ground && *ground == current;
}

void advanceRuntimeJumpjetFlight(RuntimeProductionHostState& state, WorldObjectInstance& object) {
    const auto* unit = runtimeJumpjetProfile(state, object);
    if (!unit || !object.alive || !object.airborne) return;
    auto& f = runtimeJumpjetFlightFor(state, object);
    if ((f.active || f.stopped) && (object.x != f.toX || object.y != f.toY)) {
        // A teleport/transport relocation cancels a previous segment, even
        // when arrival recovery freezes the object at its new location.
        f.active = f.stopped = false; f.speed = 0; f.segmentCarry = 0;
        f.length = f.progress = 0;
    }
    if (state.specialMechanismsInitialized && (state.specialMechanisms.isTemporalFrozen(object.instanceId)
        || state.specialMechanisms.isChronoMovementLocked(object.instanceId, state.tick))) {
        applyRuntimeJumpjetPose(state, object);
        return;
    }
    if (f.lastTick == state.tick) { applyRuntimeJumpjetPose(state, object); return; }
    // Do not bank movement while waiting for a path, collision clearance or
    // a new order. A completed segment can only continue during its own tick.
    f.segmentCarry = 0;
    f.lastTick = state.tick;
    const auto mission = toLowerAsciiLocal(object.mission);
    const bool landing = runtimeJumpjetShouldLand(state, object, *unit, f);
    const bool wasAirborne = object.airborne;
    if (mission != "landingtodeploy" && mission != "takingoffafterundeploy")
        (void)advanceRuntimeJumpjetHeight(state, object, landing);
    if (wasAirborne != object.airborne) refreshRuntimeObjectOccupancyAfterLocalChange(state);
    const double multiplier = runtimeVeterancyAbilityMultiplier(state, object, VeteranAbility::Faster);
    const auto maximum = std::llround(unit->jumpjetSpeed * std::clamp(multiplier, 0.01, 10.0) * runtimeJumpjetQ16);
    const auto acceleration = std::llround(unit->jumpjetAccel * runtimeJumpjetQ16);
    const int hz = std::max(1, state.config.fixedTickHz);
    const auto dv = std::max<std::int64_t>(1, acceleration*15/hz);
    std::int64_t desired = 0;
    if (f.active) {
        desired = maximum;
        const auto behavior = state.behaviors.find(object.instanceId);
        const bool terminal = behavior == state.behaviors.end() || behavior->second.path.empty();
        if (terminal && acceleration > 0) {
            const double braking = std::sqrt(2.0 * double(acceleration) * double(f.length-f.progress));
            desired = std::min(desired, static_cast<std::int64_t>(braking));
        }
        if (behavior == state.behaviors.end() && f.speed > 0) desired = std::min(desired, f.speed);
    }
    f.speed = acceleration == 0 ? desired : f.speed + std::clamp(desired-f.speed, -dv, dv);
    if (f.active && f.speed > 0) {
        const auto previous = f.progress;
        const auto available = std::max<std::int64_t>(1, f.speed*15/hz);
        f.progress = std::min(f.length, f.progress + available);
        object.movementAnimationDistance = std::fmod(object.movementAnimationDistance
            + double(f.progress-previous)/runtimeJumpjetCellQ16, 1024.0);
        if (f.progress == f.length) {
            f.active = false;
            f.segmentCarry = available - (f.progress - previous);
        }
    }
    if (!unit->jumpjetNoWobbles && unit->jumpjetWobbles > 0.0
        && !landing
        && mission != "landingtodeploy" && mission != "takingoffafterundeploy") {
        f.wobblePhase = (f.wobblePhase + static_cast<int>(std::llround(unit->jumpjetWobbles*65536.0/hz))) % 65536;
    }
    applyRuntimeJumpjetPose(state, object);
    advanceRuntimeKirovClearanceMotion(state, object, f, *unit);
    applyRuntimeJumpjetPose(state, object);
}

bool advanceRuntimeJumpjetFlights(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeJumpjetFlights", state.tick);
    bool changed = false;
    for (auto& object : state.world.accessObjects()) {
        if (object.alive && object.airborne && runtimeJumpjetProfile(state, object)) {
            advanceRuntimeJumpjetFlight(state, object);
            if (!object.airborne) state.jumpjetFlights.erase(object.instanceId);
            changed = true;
        } else {
            state.jumpjetFlights.erase(object.instanceId);
            object.aircraftBuildingClearanceLevel = -1.0;
        }
    }
    for (auto it = state.jumpjetFlights.begin(); it != state.jumpjetFlights.end();) {
        if (!findObjectById(state, it->first)) it = state.jumpjetFlights.erase(it); else ++it;
    }
    return changed;
}

void bindRuntimeJumpjetMoveGoal(RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& requested, const RuntimeCellPoint& assigned, int minimumRadius) {
    if (!object.airborne || !runtimeJumpjetProfile(state, object)) return;
    auto& f = runtimeJumpjetFlightFor(state, object);
    f.goalX=std::clamp(requested.x,0,511); f.goalY=std::clamp(requested.y,0,511);
    f.targetX=assigned.x; f.targetY=assigned.y;
    f.goalRadius=std::clamp(std::max(minimumRadius,
        static_cast<int>(std::ceil(std::hypot(assigned.x-f.goalX, assigned.y-f.goalY)))),1,512);
}

int beginRuntimeJumpjetSegment(RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    auto& f = runtimeJumpjetFlightFor(state, object);
    const auto carry = f.lastTick == state.tick && !f.active
        && f.toX == from.x && f.toY == from.y ? f.segmentCarry : 0;
    f.fromX=from.x; f.fromY=from.y; f.toX=to.x; f.toY=to.y;
    f.stopped = false;
    f.length = std::llround(std::hypot(to.x-from.x, to.y-from.y)*runtimeJumpjetCellQ16);
    // Navigation still approves one cell at a time. Stock speeds use less
    // than one cell/tick; extreme mod speeds retain the existing boundary cap.
    f.progress = std::min(carry, std::max<std::int64_t>(0, f.length-1));
    f.segmentCarry = 0;
    f.active = f.length > 0;
    return 2; // A live segment holds the same two air cells until physics completes it.
}

void stopRuntimeUnstartedJumpjetSegment(RuntimeGameSessionState& state, WorldObjectInstance& object) {
    const auto it = state.jumpjetFlights.find(object.instanceId);
    if (it == state.jumpjetFlights.end() || !it->second.active
        || it->second.progress != 0 || it->second.speed != 0) return;
    auto& f = it->second;
    const auto previous = runtimeCellPointForObject(object);
    const RuntimeCellPoint resting{f.fromX, f.fromY};
    f.active = false; f.length = 0; f.segmentCarry = 0; f.toX = f.fromX; f.toY = f.fromY;
    setRuntimeObjectCell(state, object, resting, false);
    updateRuntimeTickOccupancyForMove(state, object, previous, resting);
    applyRuntimeJumpjetPose(state, object);
}

void stopRuntimeJumpjetSegment(RuntimeGameSessionState& state, WorldObjectInstance& object) {
    const auto found = state.jumpjetFlights.find(object.instanceId);
    if (found == state.jumpjetFlights.end() || !found->second.active) return;
    // Preserve the old no-travel rollback so an unstarted flight does not
    // occupy the next cell while visibly still sitting at its departure.
    if (found->second.progress == 0 && found->second.speed == 0) {
        stopRuntimeUnstartedJumpjetSegment(state, object);
        return;
    }
    auto& flight = found->second;
    flight.active = false;
    flight.stopped = true;
    flight.speed = 0;
    flight.segmentCarry = 0;
    applyRuntimeJumpjetPose(state, object);
}

void resumeRuntimeJumpjetSegment(RuntimeGameSessionState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, bool resumeStopped) {
    if (!object.airborne || !runtimeJumpjetProfile(state, object)) return;
    const auto it = state.jumpjetFlights.find(object.instanceId);
    if (it != state.jumpjetFlights.end() && it->second.stopped && resumeStopped) {
        // A new command releases the frozen segment. Retain its exact
        // progress so the first resumed frame cannot jump to a cell anchor.
        it->second.stopped = false;
        it->second.active = true;
    }
    if (it != state.jumpjetFlights.end() && it->second.active) {
        behavior.moveCooldownTicks = 2;
        behavior.moveSegmentFrom = {it->second.fromX, it->second.fromY};
    }
}

bool advanceRuntimeJumpjetSegment(RuntimeProductionHostState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior) {
    advanceRuntimeJumpjetFlight(state, object);
    const auto& f = runtimeJumpjetFlightFor(state, object);
    behavior.moveCooldownTicks = f.active ? 2 : 1;
    behavior.renderMoveTicksRemaining = behavior.renderMoveTicksTotal = f.active ? 2 : 0;
    return true;
}

bool runtimeJumpjetCanEnter(const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& to) {
    const auto* unit = runtimeJumpjetProfile(state, object);
    if (!unit || !object.airborne) return true;
    if (!runtimeFactoryAirExitReadyToMove(state, object)) return false;
    if (unit->jumpjetSpeed <= 0.0) return false;
    const auto it = state.jumpjetFlights.find(object.instanceId);
    const auto height = it == state.jumpjetFlights.end() ? object.flightLevel*runtimeJumpjetCellQ16 : it->second.height;
    return height >= terrainLevelAtRuntimeCell(state, to.x, to.y)*runtimeJumpjetCellQ16 + runtimeJumpjetQ16;
}

int runtimeJumpjetTurnStep(const RuntimeGameSessionState& state, const UnitType& unit) {
    const auto hz = std::max(1, state.config.fixedTickHz);
    const std::int64_t rate = unit.jumpjetTurnRate*15LL;
    return static_cast<int>(std::min<std::int64_t>(128,
        (static_cast<std::int64_t>(state.tick)+1)*rate/hz - static_cast<std::int64_t>(state.tick)*rate/hz));
}

int runtimeProjectileTravelTicks(const RulesDatabase& rules, const WeaponType& weapon,
    double distanceCells, int fixedTickHz) {
    const int speed = runtimeProjectileSpeedLeptons(weapon,
        rules.findProjectileType(weapon.projectile), rules.getCombatDetailRules().projectileGravity);
    const auto frames = static_cast<int>(std::clamp(std::ceil(
        std::max(0.0, distanceCells) * 256.0 / std::max(1, speed)), 1.0, 36000.0));
    return std::max(1, runtimeRuleFramesToTicks(frames, fixedTickHz));
}

void updateRuntimeProjectileTravelPosition(
    RuntimeProjectileEffect& projectile) {
    if (projectile.renderIntent == "sonicWave") return;
    projectile.progress = runtimeProjectileProgress(projectile);
    if (projectile.renderIntent == "diskLaserCharge") {
        projectile.currentX = projectile.sourceX;
        projectile.currentY = projectile.sourceY;
        projectile.currentZ = projectile.sourceZ;
        projectile.currentCell = projectile.fromCell;
        projectile.currentLevel = projectile.fromLevel;
        return;
    }
    const double travelProgress =
        projectile.vertical && projectile.acceleration > 0
            ? std::pow(
                projectile.progress,
                1.0 + static_cast<double>(
                    std::clamp(projectile.acceleration, 0, 4)
                )
            )
            : projectile.progress;
    const double sourceX = projectile.hasSourcePosition
        ? projectile.sourceX
        : static_cast<double>(projectile.fromCell.x);
    const double sourceY = projectile.hasSourcePosition
        ? projectile.sourceY
        : static_cast<double>(projectile.fromCell.y);
    const double sourceZ = projectile.hasSourcePosition
        ? projectile.sourceZ
        : static_cast<double>(projectile.fromLevel);
    const double targetX = projectile.hasTargetPosition
        ? projectile.targetX
        : static_cast<double>(projectile.toCell.x);
    const double targetY = projectile.hasTargetPosition
        ? projectile.targetY
        : static_cast<double>(projectile.toCell.y);
    const double targetZ = projectile.hasTargetPosition
        ? projectile.targetZ
        : static_cast<double>(projectile.toLevel);
    const bool verticalBomb = projectile.vertical && !projectile.projectileImageId.empty();
    projectile.currentX = verticalBomb ? sourceX
        : sourceX + ((targetX - sourceX) * travelProgress);
    projectile.currentY = verticalBomb ? sourceY
        : sourceY + ((targetY - sourceY) * travelProgress);
    const double level =
        sourceZ + ((targetZ - sourceZ) * travelProgress);
    double arcHeight = 0.0;
    if (projectile.arcing) {
        arcHeight = 0.35;
    }
    if (projectile.high) {
        arcHeight = std::max(arcHeight, 0.7);
    }
    if (projectile.veryHigh) {
        arcHeight = std::max(arcHeight, 1.1);
    }
    projectile.currentZ =
        level
        + (arcHeight <= 0.0 || verticalBomb
            ? 0.0
            : (std::sin(projectile.progress * 3.14159265358979323846)
                * arcHeight));
    projectile.currentLevel =
        static_cast<int>(std::round(projectile.currentZ));
    projectile.currentCell = {
        static_cast<int>(std::round(projectile.currentX)),
        static_cast<int>(std::round(projectile.currentY))
    };
}

std::optional<RuntimeProjectileTerrainCollision>
findRuntimeProjectileTerrainCollision(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    double previousX,
    double previousY,
    double previousZ) {
    const ProjectileType* projectileType =
        findRuntimeProjectileType(state.rules, projectile.projectileId);
    const WeaponType* weapon =
        findRuntimeWeaponType(state.rules, projectile.weaponId);
    if (projectileType == nullptr
        || projectileType->dropping
        || (weapon != nullptr && weapon->limboLaunch)) {
        return std::nullopt;
    }

    const bool wallCanBlock = projectileType->subjectToWalls;
    const bool cliffCanBlock = projectileType->subjectToCliffs
        && projectileType->rot == 0;
    const double dx = projectile.currentX - previousX;
    const double dy = projectile.currentY - previousY;
    const double dz = projectile.currentZ - previousZ;
    const int sampleCount = std::max(
        1,
        static_cast<int>(std::ceil(
            std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) * 4.0
        ))
    );
    RuntimeCellPoint previousCell{
        static_cast<int>(std::round(previousX)),
        static_cast<int>(std::round(previousY))
    };
    double previousSampleZ = previousZ;
    bool previousCellWasCliff = false;
    for (int sample = 1; sample <= sampleCount; ++sample) {
        const double t = static_cast<double>(sample)
            / static_cast<double>(sampleCount);
        const double sampleZ = previousZ
            + (projectile.currentZ - previousZ) * t;
        const RuntimeCellPoint cell{
            static_cast<int>(std::round(previousX + dx * t)),
            static_cast<int>(std::round(previousY + dy * t))
        };
        const bool enteredNewCell = cell != previousCell;
        if (enteredNewCell) {
            previousCell = cell;
        }

        if (wallCanBlock) {
            for (const WorldObjectInstance& object : state.world.getObjects()) {
                if (!object.alive
                    || object.instanceId == projectile.attackerObjectId
                    || object.kind != WorldObjectKind::Structure) {
                    continue;
                }
                const BuildingType* building =
                    findRuntimeBuildingTypeById(state.rules, object.type);
                if (building != nullptr
                    && building->wall
                    && runtimeObjectOccupiesCrushCell(state, object, cell)
                    && sampleZ <= static_cast<double>(object.level) + 2.001) {
                    return RuntimeProjectileTerrainCollision{
                        cell,
                        object.level,
                        object.instanceId
                    };
                }
            }
        }

        const auto terrainIt = state.terrainByCell.find(
            runtimeCellKey(cell.x, cell.y)
        );
        const TerrainCell* terrain = terrainIt == state.terrainByCell.end()
            ? nullptr
            : terrainIt->second;
        if (terrain == nullptr) {
            const auto fallback = std::find_if(
                state.world.getTerrainCells().begin(),
                state.world.getTerrainCells().end(),
                [&cell](const TerrainCell& candidate) {
                    return candidate.x == cell.x && candidate.y == cell.y;
                }
            );
            if (fallback != state.world.getTerrainCells().end()) {
                terrain = &*fallback;
            }
        }
        if (terrain == nullptr) {
            if (enteredNewCell) {
                previousCellWasCliff = false;
            }
            previousSampleZ = sampleZ;
            continue;
        }
        const RuntimeTerrainInfo info =
            runtimeCachedTerrainInfoForCell(state, *terrain);
        const int level = runtimeEffectiveTerrainLevel(*terrain);
        const bool highBridgeDeck = runtimeOverlayProvidesBridgeDeck(*terrain)
            && runtimeOverlayLooksHighBridge(*terrain);
        if (highBridgeDeck) {
            const double deckZ = static_cast<double>(level);
            const bool crossedDeckFromAbove = previousSampleZ > deckZ
                && sampleZ <= deckZ + 0.001;
            const bool hitBridgeUnderside = previousSampleZ < deckZ
                && sampleZ >= deckZ - 1.001;
            if (crossedDeckFromAbove || hitBridgeUnderside) {
                return RuntimeProjectileTerrainCollision{cell, level, -1};
            }
        }
        if (enteredNewCell && cliffCanBlock) {
            const bool risingCliffShoulder = !info.cliff
                && previousCellWasCliff
                && static_cast<double>(level) > sampleZ + 0.001;
            if ((info.cliff
                    && static_cast<double>(level) > sampleZ + 0.001)
                || risingCliffShoulder) {
                return RuntimeProjectileTerrainCollision{cell, level, -1};
            }
            previousCellWasCliff = info.cliff;
        }
        previousSampleZ = sampleZ;
    }
    return std::nullopt;
}

bool runtimeCancelledStructureEntryNeedsRecovery(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& previous) {
    if (!object.alive || object.health <= 0 || object.airborne
        || (object.kind != WorldObjectKind::Infantry && object.kind != WorldObjectKind::Unit)
        || runtimeObjectMovementMode(state, object) == PathMovementMode::Air
        || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
        || previous.kind != RuntimeBehaviorKind::Move || previous.pendingAllowedOccupiedObjectId < 0
        || object.garrisonHostObjectId >= 0 || object.tankBunkerHostId >= 0
        || object.dockedToObjectId >= 0 || object.factoryExitHostId >= 0) return false;
    const int hostId = previous.pendingAllowedOccupiedObjectId;
    const auto* host = findObjectById(state, hostId);
    if (!host || !host->alive || host->kind != WorldObjectKind::Structure) return false;
    const auto footprint = runtimeStructureFootprintForObject(state, *host);
    const auto offset = worldObjectSubCellOffset(object);
    const double footX = runtimeObjectRenderX(object) + offset.x;
    const double footY = runtimeObjectRenderY(object) + offset.y;
    const bool footInside = std::isfinite(footX) && std::isfinite(footY)
        && footX > host->x - .5 + 1e-9 && footX < host->x + footprint.width - .5 - 1e-9
        && footY > host->y - .5 + 1e-9 && footY < host->y + footprint.height - .5 - 1e-9;
    if (!footInside && !runtimeStructureCellInsideFoundation(state, *host, runtimeCellPointForObject(object))) return false;
    if (std::any_of(state.factoryDeliveries.leases().begin(), state.factoryDeliveries.leases().end(),
            [&](const auto& lease) { return lease.producedObjectId == object.instanceId; })) return false;
    if (std::any_of(state.repairDepots.relations().begin(), state.repairDepots.relations().end(),
            [&](const auto& relation) { return relation.unitId == object.instanceId && relation.hostId == hostId; })) return false;
    // Player commands release a repair relation before erasing its approach.
    // The authored pad/drive-row departure remains the repair system's contract.
    const auto* hostRules = state.rules.findBuildingType(host->type);
    if (object.kind == WorldObjectKind::Unit && hostRules && hostRules->unitRepair) return false;
    const auto harvester = state.harvesterTasks.find(object.instanceId);
    if (harvester != state.harvesterTasks.end() && harvester->second.targetRefineryId == hostId
        && harvester->second.phase != RuntimeHarvesterPhase::Idle) return false;
    const auto slave = state.slaveMinerTasks.find(object.instanceId);
    if (slave != state.slaveMinerTasks.end() && slave->second.refineryId == hostId
        && (slave->second.phase == RuntimeSlaveMinerPhase::Returning
            || slave->second.phase == RuntimeSlaveMinerPhase::Stored)) return false;
    if (std::any_of(state.grinding.relations().begin(), state.grinding.relations().end(),
            [&](const auto& relation) { return relation.sourceId == object.instanceId && relation.hostId == hostId; })) return false;

    // Revoking an already accepted foundation permission must not depend on
    // a pending map or current entry eligibility: rejection may clear that map,
    // and capture may change the host's owner while the last step is visible.
    return true;
}

bool runtimeCancelledStructureEntryNeedsRecovery(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const auto previous = state.behaviors.find(object.instanceId);
    return previous != state.behaviors.end()
        && runtimeCancelledStructureEntryNeedsRecovery(state, object, previous->second);
}

bool recoverRuntimeCancelledStructureEntry(
    RuntimeProductionHostState& state, WorldObjectInstance& object,
    const RuntimeObjectBehavior& previous) {
    if (!runtimeCancelledStructureEntryNeedsRecovery(state, object, previous)) return false;
    const auto oldSlot = worldObjectSubCellOffset(object);
    const double bodyX = runtimeObjectRenderX(object) + oldSlot.x;
    const double bodyY = runtimeObjectRenderY(object) + oldSlot.y;
    const double bodyLevel = worldObjectHasGroundRenderPose(object) ? object.renderLevel : double(object.level);
    const bool bodyOnBridge = worldObjectHasGroundRenderPose(object) ? object.renderOnBridge : object.onBridge;
    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const auto* terrain = &state.terrainByCell;
    if (terrain->empty()) { generatedTerrain = buildRuntimeTerrainLookup(state.world); terrain = &generatedTerrain; }
    const auto occupied = buildRuntimeOccupancyLookup(state, object.instanceId);
    const auto ordinary = [&](const RuntimeCellPoint& cell) {
        if (runtimeCellKey(cell.x, cell.y) < 0
            || !runtimeCellIsPassable(state, *terrain, occupied, cell, object, -1, nullptr, false)) return false;
        return !runtimeInfantryUsesGroundSubcells(object)
            || !state.subcells.availableSubCells(state.world.getObjects(), state.world.getTerrainObjects(),
                runtimeSubcellLocationForCell(state, cell), {object.instanceId},
                runtimeGateSubcellContext(state, &object, RuntimeGatePassageMode::Commit)).empty();
    };
    const auto cellForBody = [](double x, double y) -> std::optional<RuntimeCellPoint> {
        if (!std::isfinite(x) || !std::isfinite(y) || x < -.5 || y < -.5 || x >= 511.5 || y >= 511.5)
            return std::nullopt;
        return RuntimeCellPoint{static_cast<int>(std::floor(x + .5)), static_cast<int>(std::floor(y + .5))};
    };
    const bool infantry = runtimeInfantryUsesGroundSubcells(object);
    // Ordinary occupancy grants a slot, not arbitrary continuous body space.
    // Inspect every live ground body, including stationary units without a
    // render record. Recovery cannot use the movement-only separation exception.
    const auto bodyClear = [&](double x, double y, bool onBridge) {
        for (const auto& other : state.world.getObjects()) {
            if (other.instanceId == object.instanceId
                || !runtimeAllGroundBodyIsSpatialCandidate(state, other)
                || onBridge != (worldObjectHasGroundRenderPose(other) ? other.renderOnBridge : other.onBridge)) continue;
            const auto offset = worldObjectSubCellOffset(other);
            const double dx = x - runtimeObjectRenderX(other) - offset.x;
            const double dy = y - runtimeObjectRenderY(other) - offset.y;
            const bool otherInfantry = other.kind == WorldObjectKind::Infantry;
            const double minimum = infantry && otherInfantry ? .5 : !infantry && !otherInfantry ? 1.0 : .75;
            if (dx * dx + dy * dy + 1e-9 < minimum * minimum) return false;
        }
        return true;
    };
    const double sourceBodyX = previous.renderFrom.x + previous.renderFromOffsetX + oldSlot.x;
    const double sourceBodyY = previous.renderFrom.y + previous.renderFromOffsetY + oldSlot.y;
    std::optional<RuntimeCellPoint> destination;
    int recoveredSlot = -1;
    double recoveredBodyX = 0, recoveredBodyY = 0, recoveredLevel = 0;
    bool recoveredOnBridge = false;
    enum class RecoveryPose { Current, Source, Canonical };
    const auto tryDestination = [&](const RuntimeCellPoint& cell, RecoveryPose pose) {
        if (!ordinary(cell)) return false;
        const bool onBridge = runtimeCellIsBridge(state, cell.x, cell.y);
        const double level = terrainLevelAtRuntimeCell(state, cell.x, cell.y);
        const std::vector<int> slots = infantry
            ? state.subcells.availableSubCells(state.world.getObjects(), state.world.getTerrainObjects(),
                runtimeSubcellLocationForCell(state, cell), {object.instanceId},
                runtimeGateSubcellContext(state, &object, RuntimeGatePassageMode::Commit))
            : std::vector<int>{-1};
        if (slots.empty()) return false;
        const auto accept = [&](int slot, double x, double y) {
            if (!bodyClear(x, y, onBridge)) return false;
            destination = cell; recoveredSlot = slot;
            recoveredBodyX = x; recoveredBodyY = y;
            recoveredLevel = level; recoveredOnBridge = onBridge;
            return true;
        };
        if (pose != RecoveryPose::Canonical) {
            const double x = pose == RecoveryPose::Current ? bodyX : sourceBodyX;
            const double y = pose == RecoveryPose::Current ? bodyY : sourceBodyY;
            const auto heldCell = cellForBody(x, y);
            const double heldLevel = pose == RecoveryPose::Current ? bodyLevel : previous.renderFromLevel;
            const bool heldBridge = pose == RecoveryPose::Current ? bodyOnBridge : previous.renderFromOnBridge;
            if (heldCell && *heldCell == cell && heldBridge == onBridge && std::abs(heldLevel - level) < 1e-6) {
                int preciseSlot = slots.front();
                bool canKeep = !infantry || pose == RecoveryPose::Current;
                if (infantry) {
                    double nearestDistance = std::numeric_limits<double>::infinity();
                    for (int slot : slots) {
                        const auto offset = worldInfantrySubCellOffset(slot);
                        const double dx = cell.x + offset.x - x, dy = cell.y + offset.y - y;
                        const double distance = dx * dx + dy * dy;
                        if (distance < nearestDistance) { nearestDistance = distance; preciseSlot = slot; }
                        if (std::abs(dx) < 1e-6 && std::abs(dy) < 1e-6) canKeep = true;
                    }
                }
                if (canKeep && accept(preciseSlot, x, y)) return true;
            }
            // A body still safely outside stays exact. Otherwise prefer its
            // entry source before moving it to any other ordinary free slot.
            if (pose == RecoveryPose::Current) return false;
        }
        for (int slot : slots) {
            const auto offset = infantry ? worldInfantrySubCellOffset(slot) : WorldObjectCellOffset{};
            if (accept(slot, cell.x + offset.x, cell.y + offset.y)) return true;
        }
        return false;
    };
    const auto bodyCell = cellForBody(bodyX, bodyY);
    if (bodyCell) tryDestination(*bodyCell, RecoveryPose::Current);
    if (!destination) tryDestination(previous.renderFrom, RecoveryPose::Source);
    // Match the bounded deterministic perimeter order of the ordinary outside
    // search, but skip cells whose free slots are too close to a visible body.
    if (!destination) {
        const RuntimeCellPoint desired = runtimeCellKey(previous.renderFrom.x, previous.renderFrom.y) >= 0
            ? previous.renderFrom : runtimeCellPointForObject(object);
        tryDestination(desired, RecoveryPose::Canonical);
        for (int radius = 1; radius <= 6 && !destination; ++radius)
            for (int dy = -radius; dy <= radius && !destination; ++dy)
                for (int dx = -radius; dx <= radius && !destination; ++dx)
                    if (std::abs(dx) == radius || std::abs(dy) == radius)
                        tryDestination({desired.x + dx, desired.y + dy}, RecoveryPose::Canonical);
    }
    if (!destination) return false;

    releaseRuntimeInfantrySubcellReservation(state, object.instanceId);
    setRuntimeObjectCell(state, object, *destination, false);
    if (runtimeInfantryUsesGroundSubcells(object)) object.subCell = recoveredSlot;
    const auto newSlot = worldObjectSubCellOffset(object);
    object.hasRenderPosition = true;
    object.renderMoveActive = false;
    object.renderX = recoveredBodyX - newSlot.x;
    object.renderY = recoveredBodyY - newSlot.y;
    object.renderLevel = recoveredLevel;
    object.renderOnBridge = recoveredOnBridge;
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    invalidateRuntimeObjectProjectionCache(state);
    state.repaintRequested = true;
    return true;
}

bool runtimeTickOccupancyProvesEmptyCrushDestination(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& crusher,
    const RuntimeCellPoint& destination) {
    if (state.tickOccupancyCacheTick != state.tick
        || !state.tickCrushEmptyCellProofValid
        || state.tickCrushEmptyCellObjectCount != state.world.getObjects().size()
        || !state.magBeams.bindings().empty()
        || crusher.kind != WorldObjectKind::Unit || !crusher.alive
        || runtimeObjectMovementMode(state, crusher) != PathMovementMode::Ground
        || !runtimeObjectMayUseStructureDriveRows(state, crusher)
        || destination.x < 0 || destination.y < 0) return false;
    const std::int64_t key64 = static_cast<std::int64_t>(destination.y) * 1000
        + destination.x;
    if (key64 > std::numeric_limits<int>::max()) return false;
    const int key = static_cast<int>(key64);
    // A stacked cell remains occupied even if representative promotion did
    // not find its remaining non-object (terrain) occupant.
    if (state.tickOccupiedByCell.find(key) != state.tickOccupiedByCell.end()
        || state.tickGroundOccupantCountByCell.find(key)
            != state.tickGroundOccupantCountByCell.end()) return false;
    for (const auto& bounds : state.tickCrushUnindexedFoundationBounds) {
        if (destination.x >= bounds[0] && destination.x <= bounds[2]
            && destination.y >= bounds[1] && destination.y <= bounds[3]) return false;
    }
    return true;
}

bool runtimeObjectBodyOnCollapsedBridge(
    const WorldObjectInstance& object, const std::unordered_set<int>& cells) {
    if (!object.alive || object.kind == WorldObjectKind::Structure || object.airborne)
        return false;
    const bool groundPose = worldObjectHasGroundRenderPose(object)
        && std::isfinite(object.renderX) && std::isfinite(object.renderY);
    const bool onBridge = groundPose ? object.renderOnBridge : object.onBridge;
    if (!onBridge) return false;
    const int x = groundPose ? static_cast<int>(std::lround(object.renderX)) : object.x;
    const int y = groundPose ? static_cast<int>(std::lround(object.renderY)) : object.y;
    return cells.contains(runtimeCellKey(x, y));
}

int queueRuntimeBridgeCollapseOccupants(
    RuntimeGameSessionState& state, const std::unordered_set<int>& cells,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause) {
    int queued = 0;
    for (const auto& object : state.world.getObjects()) {
        if (!runtimeObjectBodyOnCollapsedBridge(object, cells)) continue;
        RuntimeObjectExitRequest exit;
        exit.objectId = object.instanceId;
        exit.kind = RuntimeObjectExitKind::Destroyed;
        exit.tick = state.tick;
        exit.mission = "BridgeCollapsed";
        exit.reason = "bridge collapsed";
        exit.sourceObjectId = sourceObjectId;
        exit.sourceOwner = sourceOwner;
        exit.deathWeaponCause = deathWeaponCause;
        exit.suppressCrew = true;
        const bool bodyPose = worldObjectHasGroundRenderPose(object);
        exit.unitDeathSoundPosition = RuntimeSpatialAudioPosition{
            bodyPose ? object.renderX : static_cast<double>(object.x),
            bodyPose ? object.renderY : static_cast<double>(object.y),
            bodyPose ? object.renderLevel : static_cast<double>(object.level)};
        if (queueRuntimeObjectExit(state, std::move(exit))
            == RuntimeObjectExitQueueResult::Queued) ++queued;
    }
    return queued;
}

void destroyRuntimeBridgeCollapseOccupants(
    RuntimeProductionHostState& state, const std::unordered_set<int>& cells,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause) {
    const auto queueOccupants = [&]() {
        return queueRuntimeBridgeCollapseOccupants(state, cells,
            sourceObjectId, sourceOwner, deathWeaponCause);
    };
    // The first drain also finalizes already queued occupants after merging
    // suppressCrew. A pending parasite can return from limbo during that drain;
    // collect any newly active body on this same footprint before nav refresh.
    (void)queueOccupants();
    if (state.drainingObjectExits) {
        state.pendingBridgeCollapses.push_back({cells, sourceObjectId,
            sourceOwner, deathWeaponCause});
        return;
    }
    (void)drainRuntimeObjectExitQueue(state);
    while (queueOccupants() > 0) {
        if (!drainRuntimeObjectExitQueue(state)) break;
    }
}

std::optional<RuntimeBridgeDamageTarget> runtimeHighBridgeDamageTargetForSideLane(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& point) {
    const auto laneCell = state.terrainByCell.find(runtimeCellKey(point.x, point.y));
    if (laneCell == state.terrainByCell.end() || laneCell->second == nullptr
        || laneCell->second->hasOverlay)
        return std::nullopt;
    for (int lane = 1; lane < 3; ++lane) {
        for (bool alongX : {true, false}) {
            const RuntimeCellPoint anchor{
                point.x - (alongX ? 0 : lane), point.y - (alongX ? lane : 0)};
            const auto found = state.terrainByCell.find(runtimeCellKey(anchor.x, anchor.y));
            if (found == state.terrainByCell.end() || found->second == nullptr) continue;
            const auto& terrain = *found->second;
            if (terrain.level != laneCell->second->level
                || !runtimeOverlayLooksHighBridge(terrain)
                || !runtimeOverlayProvidesBridgeDeck(terrain)
                || !runtimeHighBridgeOverlayMatchesAxis(terrain, alongX)) continue;
            RuntimeBridgeDamageTarget target;
            target.anchor = anchor; target.high = true; target.alongX = alongX;
            target.material = runtimeHighBridgeMaterialForOverlay(terrain);
            for (int side = 0; side < 3; ++side)
                target.physicalCells.push_back({
                    anchor.x + (alongX ? 0 : side), anchor.y + (alongX ? side : 0)});
            return target;
        }
    }
    return std::nullopt;
}

std::optional<RuntimeBridgeDamageTarget> runtimeBridgeDamageTargetAtCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& requestedCell,
    int searchRadius) {
    const int radiusLimit = std::clamp(searchRadius, 0, 3);
    for (int radius = 0; radius <= radiusLimit; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius > 0
                    && std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const RuntimeCellPoint point{
                    requestedCell.x + dx,
                    requestedCell.y + dy
                };
                const auto terrainIt = state.terrainByCell.find(
                    runtimeCellKey(point.x, point.y)
                );
                if (terrainIt == state.terrainByCell.end()
                    || terrainIt->second == nullptr
                    || !runtimeOverlayProvidesBridgeDeck(
                        *terrainIt->second
                    )) {
                    if (const auto target = runtimeHighBridgeDamageTargetForSideLane(state, point))
                        return target;
                    continue;
                }
                const TerrainCell& terrain = *terrainIt->second;
                RuntimeBridgeDamageTarget target;
                if (runtimeOverlayLooksHighBridge(terrain)) {
                    target.anchor = point;
                    target.high = true;
                    target.alongX =
                        runtimeHighBridgeOverlayMatchesAxis(terrain, true);
                    target.material =
                        runtimeHighBridgeMaterialForOverlay(terrain);
                    for (int lane = 0; lane < 3; ++lane) {
                        target.physicalCells.push_back({
                            point.x + (target.alongX ? 0 : lane),
                            point.y + (target.alongX ? lane : 0)
                        });
                    }
                    return target;
                }
                if (!runtimeOverlayLooksVisibleLowBridge(terrain)
                    || runtimeLowBridgeOverlayIsHead(terrain)) {
                    continue;
                }
                target.high = false;
                target.alongX =
                    runtimeLowBridgeOverlayIsAlongX(terrain);
                const std::optional<RuntimeBridgeMaterial> material =
                    runtimeLowBridgeMaterialForOverlay(terrain);
                if (!material.has_value()) {
                    continue;
                }
                target.material = *material;
                target.anchor = {
                    point.x - (!target.alongX ? terrain.overlayFrame : 0),
                    point.y - (target.alongX ? terrain.overlayFrame : 0)
                };
                for (int lane = 0; lane < 3; ++lane) {
                    target.physicalCells.push_back({
                        target.anchor.x + (target.alongX ? 0 : lane),
                        target.anchor.y + (target.alongX ? lane : 0)
                    });
                }
                return target;
            }
        }
    }
    return std::nullopt;
}

std::optional<WorldObjectInstance> runtimeBridgeAttackTargetObject(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& requestedCell,
    int searchRadius) {
    if (!state.destroyableBridges) {
        return std::nullopt;
    }
    const std::optional<RuntimeBridgeDamageTarget> bridgeTarget =
        runtimeBridgeDamageTargetAtCell(state, requestedCell, searchRadius);
    if (!bridgeTarget.has_value()) {
        return std::nullopt;
    }

    WorldObjectInstance target;
    target.instanceId = -960000
        - runtimeCellKey(bridgeTarget->anchor.x, bridgeTarget->anchor.y);
    target.kind = WorldObjectKind::Structure;
    target.type = "BRIDGE";
    target.owner = "RuntimeBridge";
    target.health = state.rules.getBridgeStrength();
    target.x = bridgeTarget->anchor.x;
    target.y = bridgeTarget->anchor.y;
    target.cell = runtimeCellKey(target.x, target.y);
    target.level = terrainLevelAtRuntimeCell(state, target.x, target.y);
    target.onBridge = true;
    target.alive = true;
    return target;
}

void runtimeSetLowBridgeOverlayOffset(
    TerrainCell& terrain,
    RuntimeBridgeMaterial material,
    int offset,
    int lane) {
    const std::string name = runtimeLowBridgeOverlayName(material, offset);
    terrain.hasOverlay = true;
    terrain.overlayIndex = runtimeLowBridgeBaseIndex(material) + offset;
    terrain.overlayFrame = lane;
    terrain.overlayType = name;
    terrain.overlayImage = name;
    terrain.overlayIsOre = false;
    terrain.overlayIsGem = false;
}

int runtimeBridgeSegmentDamageBand(const RuntimeGameSessionState& state, int health) {
    const auto& conditions = state.rules.getSpecialMechanismRules();
    const double yellow = std::clamp(conditions.conditionYellow, 0.0, 1.0);
    const double red = std::clamp(conditions.conditionRed, 0.0, yellow);
    const double fraction = static_cast<double>(health)
        / std::max(1, state.rules.getBridgeStrength());
    return fraction <= red ? 4 : (fraction <= yellow ? 1 : 0);
}

int runtimeInitialBridgeSegmentHealth(
    const RuntimeGameSessionState& state, const RuntimeBridgeDamageTarget& target) {
    const int strength = std::max(1, state.rules.getBridgeStrength());
    const auto found = state.terrainByCell.find(
        runtimeCellKey(target.anchor.x, target.anchor.y));
    if (found == state.terrainByCell.end() || found->second == nullptr)
        return strength;
    const auto& terrain = *found->second;
    const int shape = target.high
        ? terrain.overlayFrame - (target.alongX ? 0 : 9)
        : runtimeLowBridgeOffset(terrain) - (target.alongX ? 0 : 9);
    // Maps and older saves can author damage without a saved HP record.
    // A first cannon hit must not heal that appearance back to intact.
    if (shape >= 1 && shape <= 6) {
        const auto& conditions = state.rules.getSpecialMechanismRules();
        const double yellow = std::clamp(conditions.conditionYellow, 0.0, 1.0);
        const double fraction = shape >= 4
            ? std::clamp(conditions.conditionRed, 0.0, yellow) : yellow;
        return std::max(1, static_cast<int>(std::floor(strength * fraction)));
    }
    return strength;
}

void updateRuntimeBridgeSegmentDamageFrame(
    RuntimeProductionHostState& state, const RuntimeBridgeDamageTarget& target, int health) {
    const int band = runtimeBridgeSegmentDamageBand(state, health);
    bool changed = false;
    if (target.high) {
        const auto found = state.terrainByCell.find(
            runtimeCellKey(target.anchor.x, target.anchor.y));
        if (found == state.terrainByCell.end() || found->second == nullptr) return;
        auto& terrain = *const_cast<TerrainCell*>(found->second);
        const int frame = (target.alongX ? 0 : 9) + band;
        if (terrain.overlayFrame != frame) {
            terrain.overlayFrame = frame;
            changed = true;
        }
    } else {
        for (int lane = 0; lane < 3; ++lane) {
            const auto& point = target.physicalCells[static_cast<std::size_t>(lane)];
            const auto found = state.terrainByCell.find(runtimeCellKey(point.x, point.y));
            if (found == state.terrainByCell.end() || found->second == nullptr) continue;
            auto& terrain = *const_cast<TerrainCell*>(found->second);
            // Preserve authored gap ends until this segment itself collapses.
            const int oldShape = runtimeLowBridgeOffset(terrain) - (target.alongX ? 0 : 9);
            if (oldShape >= 7 && oldShape <= 8) continue;
            const int offset = (target.alongX ? 0 : 9) + band;
            if (runtimeLowBridgeOffset(terrain) != offset || terrain.overlayFrame != lane) {
                runtimeSetLowBridgeOverlayOffset(terrain, target.material, offset, lane);
                changed = true;
            }
        }
    }
    if (changed) refreshRuntimeBridgeMutationState(state, target.physicalCells);
}

RuntimeBridgeDamageResult damageRuntimeBridgeAtCell(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& requestedCell,
    int rawDamage,
    int searchRadius,
    int sourceObjectId,
    const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause) {
    RuntimeBridgeDamageResult result;
    if (!state.destroyableBridges || rawDamage <= 0) {
        return result;
    }
    const std::optional<RuntimeBridgeDamageTarget> target =
        runtimeBridgeDamageTargetAtCell(state, requestedCell, searchRadius);
    if (!target.has_value()) {
        return result;
    }

    result.hit = true;
    result.damage = rawDamage;
    result.anchor = target->anchor;
    const int healthKey = runtimeCellKey(target->anchor.x, target->anchor.y);
    int& health = state.bridgeHealthByCell[healthKey];
    if (health <= 0) {
        health = runtimeInitialBridgeSegmentHealth(state, *target);
    }
    health = std::max(0, health - rawDamage);
    result.healthRemaining = health;
    if (health > 0) {
        updateRuntimeBridgeSegmentDamageFrame(state, *target, health);
        return result;
    }

    std::vector<RuntimeCellPoint> changedCells = target->physicalCells;
    std::vector<RuntimeCellPoint> collapsedBridgeCells =
        target->physicalCells;
    if (target->high) {
        const auto targetIt = state.terrainByCell.find(healthKey);
        if (targetIt == state.terrainByCell.end() || targetIt->second == nullptr)
            return result;
        runtimeClearBridgeOverlay(*const_cast<TerrainCell*>(targetIt->second));
        const RuntimeCellPoint axis{target->alongX ? 1 : 0, target->alongX ? 0 : 1};
        // Native collapsed ends trim the two overlapping SHP sprites next to
        // the hole. Leave every farther segment and its health untouched.
        const std::array<std::pair<int, int>, 2> edgeFrames{
            std::pair<int, int>{-1, target->alongX ? 8 : 16},
            std::pair<int, int>{1, target->alongX ? 7 : 17}
        };
        for (const auto& [side, frame] : edgeFrames) {
            const RuntimeCellPoint neighbor{
                target->anchor.x + axis.x * side, target->anchor.y + axis.y * side};
            const auto neighborIt = state.terrainByCell.find(runtimeCellKey(neighbor.x, neighbor.y));
            if (neighborIt == state.terrainByCell.end() || neighborIt->second == nullptr) continue;
            auto& terrain = *const_cast<TerrainCell*>(neighborIt->second);
            if (!runtimeOverlayLooksHighBridge(terrain)
                || !runtimeHighBridgeOverlayMatchesAxis(terrain, target->alongX)
                || runtimeHighBridgeMaterialForOverlay(terrain) != target->material) continue;
            const bool wasConnected = runtimeOverlayProvidesBridgeDeck(terrain);
            if (!wasConnected) continue;
            terrain.overlayFrame = frame;
            for (int lane = 0; lane < 3; ++lane) {
                const RuntimeCellPoint point{
                    neighbor.x + (target->alongX ? 0 : lane),
                    neighbor.y + (target->alongX ? lane : 0)};
                changedCells.push_back(point);
                collapsedBridgeCells.push_back(point);
            }
        }
    } else {
        const int placeholderOffset = target->alongX ? 26 : 27;
        for (int lane = 0; lane < 3; ++lane) {
            const RuntimeCellPoint point = target->physicalCells[
                static_cast<std::size_t>(lane)
            ];
            const auto terrainIt = state.terrainByCell.find(
                runtimeCellKey(point.x, point.y)
            );
            if (terrainIt == state.terrainByCell.end()
                || terrainIt->second == nullptr) {
                continue;
            }
            runtimeSetLowBridgeOverlayOffset(
                *const_cast<TerrainCell*>(terrainIt->second),
                target->material,
                placeholderOffset,
                lane
            );
        }

        const RuntimeCellPoint axis{
            target->alongX ? 1 : 0,
            target->alongX ? 0 : 1
        };
        const std::array<std::pair<int, int>, 2> edgeOffsets{
            std::pair<int, int>{-1, target->alongX ? 8 : 16},
            std::pair<int, int>{1, target->alongX ? 7 : 17}
        };
        for (const auto& [side, edgeOffset] : edgeOffsets) {
            const RuntimeCellPoint neighborAnchor{
                target->anchor.x + axis.x * side,
                target->anchor.y + axis.y * side
            };
            for (int lane = 0; lane < 3; ++lane) {
                const RuntimeCellPoint point{
                    neighborAnchor.x + (target->alongX ? 0 : lane),
                    neighborAnchor.y + (target->alongX ? lane : 0)
                };
                const auto terrainIt = state.terrainByCell.find(
                    runtimeCellKey(point.x, point.y)
                );
                if (terrainIt == state.terrainByCell.end()
                    || terrainIt->second == nullptr) {
                    continue;
                }
                TerrainCell& terrain =
                    *const_cast<TerrainCell*>(terrainIt->second);
                if (!runtimeLowBridgeOverlayMatches(
                        terrain,
                        target->material,
                        target->alongX,
                        false
                    )) {
                    continue;
                }
                int offset = runtimeLowBridgeOffset(terrain);
                if (runtimeLowBridgeOverlayIsHead(terrain)) {
                    if ((offset % 2) == 0) {
                        ++offset;
                    }
                } else {
                    offset = edgeOffset;
                }
                runtimeSetLowBridgeOverlayOffset(
                    terrain,
                    target->material,
                    offset,
                    lane
                );
                changedCells.push_back(point);
            }
        }
    }

    std::unordered_set<int> destroyedPieceCells;
    for (const RuntimeCellPoint& point : collapsedBridgeCells) {
        destroyedPieceCells.insert(runtimeCellKey(point.x, point.y));
    }
    result.destroyedCellKeys.assign(
        destroyedPieceCells.begin(),
        destroyedPieceCells.end()
    );
    destroyRuntimeBridgeCollapseOccupants(state, destroyedPieceCells,
        sourceObjectId, sourceOwner, deathWeaponCause);

    result.destroyed = true;
    refreshRuntimeBridgeMutationState(state, changedCells);
    state.interactionStatus =
        "bridge piece destroyed at "
        + std::to_string(target->anchor.x)
        + ","
        + std::to_string(target->anchor.y);
    state.economy.status = state.interactionStatus;
    return result;
}

RuntimeBridgeDamageResult demolishRuntimeBridgeByHut(
    RuntimeProductionHostState& state, const WorldObjectInstance& hut,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause) {
    RuntimeBridgeDamageResult result;
    if (!runtimeBridgeHutDemolitionAvailable(state, hut)) return result;
    const RuntimeBridgeRepairSpan span = state.bridgeRepairSpansByHut.at(hut.instanceId);
    result.hit = true;
    result.anchor = span.startHead;
    result.damage = state.rules.getBridgeStrength();
    int first = std::numeric_limits<int>::max(), last = std::numeric_limits<int>::min();
    for (const auto& cell : span.cells) if (cell.requiresOverlay) {
        const int axis = span.alongX ? cell.point.x : cell.point.y;
        first = std::min(first, axis); last = std::max(last, axis);
    }
    std::vector<RuntimeCellPoint> changedCells;
    std::unordered_set<int> collapsedKeys;
    for (const auto& cell : span.cells) {
        const auto found = state.terrainByCell.find(runtimeCellKey(cell.point.x, cell.point.y));
        if (found == state.terrainByCell.end() || !found->second) continue;
        auto& terrain = *const_cast<TerrainCell*>(found->second);
        if (span.high) {
            if (!cell.requiresOverlay) continue;
            const int axis = span.alongX ? cell.point.x : cell.point.y;
            if (runtimeOverlayLooksHighBridge(terrain)
                && runtimeHighBridgeOverlayMatchesAxis(terrain, span.alongX)
                && runtimeHighBridgeMaterialForOverlay(terrain) == span.material) {
                if (axis == first || axis == last) {
                    const int cap = axis == first ? (span.alongX ? 8 : 16) : (span.alongX ? 7 : 17);
                    result.destroyed = result.destroyed || terrain.overlayFrame != cap;
                    terrain.overlayFrame = cap;
                } else {
                    runtimeClearBridgeOverlay(terrain);
                    result.destroyed = true;
                }
            }
            state.bridgeHealthByCell[runtimeCellKey(cell.point.x, cell.point.y)] = 0;
            for (int lane = 0; lane < 3; ++lane) {
                const RuntimeCellPoint point{cell.point.x + (span.alongX ? 0 : lane),
                    cell.point.y + (span.alongX ? lane : 0)};
                changedCells.push_back(point);
                collapsedKeys.insert(runtimeCellKey(point.x, point.y));
            }
        } else {
            if (!runtimeLowBridgeOverlayMatches(terrain, span.material, span.alongX, true)) continue;
            const int oldOffset = runtimeLowBridgeOffset(terrain);
            const int offset = cell.requiresOverlay ? (span.alongX ? 26 : 27)
                : oldOffset + (oldOffset % 2 == 0 ? 1 : 0);
            if (!cell.requiresOverlay && !runtimeLowBridgeOverlayIsHead(terrain)) continue;
            result.destroyed = result.destroyed || oldOffset != offset;
            runtimeSetLowBridgeOverlayOffset(terrain, span.material, offset, cell.expectedFrame);
            changedCells.push_back(cell.point);
            collapsedKeys.insert(runtimeCellKey(cell.point.x, cell.point.y));
            if (cell.requiresOverlay && cell.expectedFrame == 0)
                state.bridgeHealthByCell[runtimeCellKey(cell.point.x, cell.point.y)] = 0;
        }
    }
    result.destroyedCellKeys.assign(collapsedKeys.begin(), collapsedKeys.end());
    std::sort(result.destroyedCellKeys.begin(), result.destroyedCellKeys.end());
    destroyRuntimeBridgeCollapseOccupants(state, collapsedKeys,
        sourceObjectId, sourceOwner, deathWeaponCause);
    if (result.destroyed) {
        refreshRuntimeBridgeMutationState(state, changedCells);
        if (state.campaignMode || state.cooperativeSession.active())
            for (int key : result.destroyedCellKeys) state.triggers.campaignBridgeDestroyedTickByCell[key] = state.tick;
    }
    return result;
}

double runtimeCellDistanceToTerrainObjectFootprint(
    const RuntimeCellPoint& point,
    const WorldTerrainObject& object) {
    double bestDistance = std::numeric_limits<double>::max();
    for (const RuntimeCellPoint& cell :
            runtimeTerrainObjectFootprintCells(object)) {
        bestDistance = std::min(
            bestDistance,
            runtimeCellDistance(point, cell)
        );
    }
    return bestDistance;
}

int runtimeProjectileTerrainDamageAtDistance(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const WeaponType* weapon,
    double distance) {
    long long baseDamage = projectile.damage;
    if (runtimeProjectileHasLockedDeathWeaponDamage(projectile)) {
        baseDamage = projectile.damage;
    } else if (projectile.spawnedMissileDamage) {
        baseDamage = projectile.spawnedMissileBaseDamage;
    } else if (weapon != nullptr) {
        baseDamage = runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon);
    }
    if (baseDamage <= 0) {
        return 0;
    }
    double rangeMultiplier = 1.0;
    if (projectile.cellSpread > 0.0) {
        const double radius = std::max(0.1, projectile.cellSpread);
        const double t = std::clamp(distance / radius, 0.0, 1.0);
        rangeMultiplier = 1.0
            - ((1.0 - runtimeProjectilePercentAtMaxFraction(projectile)) * t);
    }
    const long double scaled = static_cast<long double>(baseDamage)
        * static_cast<long double>(std::max(
            0.0,
            projectile.veteranDamageMultiplier
        ))
        * static_cast<long double>(rangeMultiplier);
    if (!std::isfinite(scaled) || scaled >= std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return std::max(0, static_cast<int>(std::llround(scaled)));
}

RuntimeTerrainProjectileDamageResult applyRuntimeProjectileTerrainDamage(
    RuntimeProductionHostState& state,
    const RuntimeProjectileEffect& projectile,
    const RuntimeCellPoint& impactCell,
    const WeaponType* weapon,
    const WarheadType* warhead) {
    RuntimeTerrainProjectileDamageResult result;
    if (!projectile.ag
        || (warhead != nullptr
            && (warhead->mindControl
                || warhead->electricAssault
                || warhead->temporal
                || warhead->ivanBomb
                || warhead->bombDisarm))) {
        return result;
    }

    const double radius = std::max(0.0, projectile.cellSpread);
    for (WorldTerrainObject& object :
            state.world.accessTerrainObjects()) {
        if (!object.alive || object.health <= 0 || object.immune) {
            continue;
        }
        const double distance =
            runtimeCellDistanceToTerrainObjectFootprint(impactCell, object);
        if ((radius <= 0.0 && distance > 0.0)
            || (radius > 0.0 && distance > radius)) {
            continue;
        }
        const int damage = runtimeProjectileTerrainDamageAtDistance(
            state,
            projectile,
            weapon,
            distance
        );
        if (damage <= 0) {
            continue;
        }
        const int maximumHealth = std::max(1, object.strength);
        const int healthBefore = std::clamp(
            object.health,
            1,
            maximumHealth
        );
        object.health = std::clamp(
            healthBefore - damage,
            0,
            maximumHealth
        );
        ++result.objectsDamaged;
        result.totalDamage += std::min(damage, healthBefore);
        if (object.health <= 0) {
            object.health = 0;
            object.alive = false;
            ++result.objectsDestroyed;
        }
    }

    if (result.objectsDamaged > 0) {
        invalidateRuntimeObjectProjectionCache(state);
        state.radarTerrainKey.clear();
        state.repaintRequested = true;
    }
    if (result.objectsDestroyed > 0) {
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        state.pathFailureCache.clear();
        state.placementChecksCacheTick = -1;
    }
    return result;
}

RuntimeMagBeamOccupancySnapshot runtimeMagBeamLandingOccupancy(
    const RuntimeGameSessionState& state,
    int targetObjectId,
    const RuntimeCellPoint& cell) {
    const WorldObjectInstance* landingObject =
        findObjectById(state, targetObjectId);
    const WorldObjectInstance* selected = nullptr;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0
            || object.instanceId == targetObjectId
            || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
            || (object.kind != WorldObjectKind::Structure
                && runtimeObjectMovementMode(state, object)
                    == PathMovementMode::Air)
            || (landingObject != nullptr
                && object.kind == WorldObjectKind::Structure
                && runtimeStructureCellIsVehicleDriveRow(
                    state,
                    object,
                    cell,
                    *landingObject
                ))
            || !runtimeObjectOccupiesCrushCell(state, object, cell)) {
            continue;
        }
        if (selected == nullptr
            || object.instanceId < selected->instanceId) {
            selected = &object;
        }
    }
    RuntimeMagBeamOccupancySnapshot snapshot;
    if (selected == nullptr) {
        return snapshot;
    }
    snapshot.occupied = true;
    snapshot.objectId = selected->instanceId;
    snapshot.owner = selected->owner;
    snapshot.typeId = selected->type;
    const UnitType* ignored = nullptr;
    snapshot.kind = runtimeMagBeamTargetKindForObject(
        state,
        *selected,
        ignored
    );
    return snapshot;
}

bool tryRuntimeStrictSlotVehicleCertificate(
    const RuntimeGameSessionState& state,const std::vector<int>& ids,const RuntimeCellPoint& target,
    const std::unordered_map<int,const TerrainCell*>& terrain,
    const std::unordered_map<int,int>& occupancy,int representativeId,std::size_t movableCount,
    std::vector<RuntimeCellPoint>* provenPoints,
    std::size_t vertexLimit,std::size_t edgeLimit) {
    RuntimeStrictSlotVehicleCertificateStats stats; stats.attempts=1;
    struct Finish {
        RuntimeStrictSlotVehicleCertificateStats& stats;
        bool previous=runtimeStrictSlotVehicleProofActive;
        ~Finish() { runtimeStrictSlotVehicleProofActive=previous;
            if(runtimeStrictSlotVehicleStats) runtimeStrictSlotVehicleStats->add(stats); }
    } finish{stats};
    if(runtimeStrictSlotVehicleReference()) { ++stats.referenceFallbacks; return false; }
    const auto guardFail=[&] { ++stats.guardFallbacks; return false; };
    if(runtimeStrictSlotVehicleProofActive || ids.size()<12 || ids.size()>256
        || movableCount!=ids.size() || representativeId!=ids.front()
        || !std::is_sorted(ids.begin(),ids.end())
        || std::adjacent_find(ids.begin(),ids.end())!=ids.end()
        || !runtimeStrictSlotVehicleCanonical(target)
        || !state.specialMechanismsInitialized || state.config.multiplayerActive
        || !state.repairDepots.relations().empty() || !state.subcells.reservations().empty()
        || !state.grinding.relations().empty() || !state.specialMechanisms.parasiteLinks().empty()
        || !state.combatDetails.panics.empty() || !state.combatDetails.diskDrains.sources.empty()
        || !state.pendingEngineerCaptures.empty() || !state.pendingEngineerActions.empty()
        || !state.pendingOccupyTargets.empty() || !state.pendingTransportTargets.empty()
        || !state.pendingTimedBombIntents.empty()
        || runtimeActivePlannerCellPassability || runtimeActivePlannerColdCellLookups
        || runtimeActivePlannerEmptyCellCensus || runtimeActivePlannerIndexedTopology
        || runtimeActivePlannerLandPermission || runtimeActiveBuildingDepartureReadOnlyQueries
        || runtimePlannerPassabilityObserver || runtimePlannerColdLookupObserver
        || runtimePlannerEmptyCensusObserver || runtimePlannerIndexedTopologyObserver
        || runtimePlannerMetadataBorrowObserver || runtimePlannerKernelLeaseObserver
        || runtimePlannerCrushAliasObserver || runtimePlannerEmptySubcellBooleanObserver
        || runtimeCellGateScratchStats || runtimeFrameScalingReferenceMovement()) return guardFail();
    runtimeStrictSlotVehicleProofActive=true;
    vertexLimit=std::min<std::size_t>(vertexLimit,4096);
    edgeLimit=std::min<std::size_t>(edgeLimit,8192);
    if(!vertexLimit || !edgeLimit) return guardFail();
    RuntimePerfTraceScope trace("moveCommand.strictSlotCertificate",state.tick);
    try {
        std::vector<const WorldObjectInstance*> actors; actors.reserve(ids.size());
        std::unordered_set<int> selected(ids.begin(),ids.end()),seen;
        // Duplicate IDs (including dead history) and hidden cargo are not admitted.
        for(const auto& object:state.world.getObjects()) {
            if(selected.contains(object.instanceId) && !seen.insert(object.instanceId).second) return guardFail();
            for(int passenger:object.transportPassengerIds) if(selected.contains(passenger)) return guardFail();
        }
        for(int id:ids) {
            const auto* actor=findObjectById(state,id);
            if(!actor || id<0 || !actor->alive || actor->health<=0 || actor->kind!=WorldObjectKind::Unit
                || (actor->type!="MTNK" && actor->type!="HTNK") || actor->owner!=state.economy.owner
                || actor->airborne || actor->submerged || actor->dockedToObjectId>=0 || actor->dockIndex>=0
                || actor->tankBunkerHostId>=0 || actor->garrisonHostObjectId>=0
                || !actor->transportPassengerIds.empty() || !actor->garrisonOccupantIds.empty()
                || (actor->mission!="Guard" && actor->mission!="Move")
                || !runtimeStrictSlotVehicleCanonical({actor->x,actor->y})
                || !runtimeObjectCanReceiveMoveCommand(state,*actor)
                || !runtimeObjectAllowsMoveToCell(state,*actor,target)
                || runtimeObjectMovementMode(state,*actor)!=PathMovementMode::Ground
                || runtimeObjectOffGroundByMagBeam(state,id)) return guardFail();
            const auto* unit=findRuntimeUnitType(state.rules,*actor);
            const auto& deploy=runtimeCachedCapabilityProfileForObject(state,*actor).deploy;
            if(!unit || unit->teleporter || unit->harvester || !unit->deploysInto.empty()
                || deploy.togglesDeployedState || deploy.deployToLand
                || runtimeObjectHasDeployedState(state,*actor)
                || runtimeObjectIsMobileSlaveMiner(state,*actor)) return guardFail();
            const auto behavior=state.behaviors.find(id);
            if(behavior!=state.behaviors.end() && behavior->second.kind!=RuntimeBehaviorKind::Move) return guardFail();
            actors.push_back(actor);
        }
        for(const auto& [key,value]:terrain) {
            ++stats.layoutEntries;
            if(!value || !runtimeStrictSlotVehicleCanonical({value->x,value->y})
                || key!=runtimeCellKey(value->x,value->y)) { ++stats.layoutFallbacks; return false; }
        }
        std::vector<RuntimeCellPoint> slots; slots.reserve(actors.size());
        for(std::size_t i=0;i<actors.size();++i) {
            const auto offset=formationOffsetForIndex(static_cast<int>(i),1);
            const RuntimeCellPoint slot{target.x+offset.x,target.y+offset.y};
            std::uint64_t tie=runtimeStableHashString("moveSlot");
            runtimeStableHashMix(tie,actors[i]->instanceId); runtimeStableHashMix(tie,slot.x); runtimeStableHashMix(tie,slot.y);
            if(!runtimeStrictSlotVehicleCanonical(slot)
                || !runtimeCellIsPassable(state,terrain,occupancy,slot,*actors[i],-1)
                || !runtimeObjectAllowsMoveToCell(state,*actors[i],slot)
                || !runtimeStrictSlotVehicleStrictWinner(runtimeMoveTargetSoftPenalty(state,terrain,occupancy,slot),tie)) {
                ++stats.slotFallbacks; return false;
            }
            // Keep the proof valid if the authored offset implementation changes.
            if(std::find(slots.begin(),slots.end(),slot)!=slots.end()) { ++stats.slotFallbacks; return false; }
            slots.push_back(slot); ++stats.strictSlots;
        }
        const auto& representative=*actors.front();
        RuntimeBuildingDepartureReadOnlyQueryScope departure(state,representative);
        if(runtimeStructureDepartureHostId(state,representative)>=0) return guardFail();
        struct Vertex { RuntimeCellPoint point; unsigned char checked=0; };
        std::unordered_map<int,Vertex> vertices; vertices.reserve(std::min<std::size_t>(vertexLimit,512));
        const auto vertex=[&](const RuntimeCellPoint& point)->bool {
            const int key=runtimeCellKey(point.x,point.y);
            if(vertices.contains(key)) return true;
            if(vertices.size()>=vertexLimit || !runtimeStrictSlotVehicleCanonical(point)
                || !runtimeCellIsPassable(state,terrain,occupancy,point,representative,-1,nullptr,false)) return false;
            vertices.emplace(key,Vertex{point,0}); ++stats.vertices; return true;
        };
        const auto walk=[&](const RuntimeCellPoint& goal)->bool {
            RuntimeCellPoint current{representative.x,representative.y};
            if(!vertex(current)) return false;
            while(current!=goal) {
                const RuntimeCellPoint next{current.x+(goal.x>current.x)-(goal.x<current.x),
                    current.y+(goal.y>current.y)-(goal.y<current.y)};
                if(!vertex(next)) return false;
                const int direction=(next.y-current.y+1)*3+(next.x-current.x+1);
                const unsigned char bit=static_cast<unsigned char>(1U<<(direction<4?direction:direction-1));
                auto& from=vertices.at(runtimeCellKey(current.x,current.y));
                if(!(from.checked&bit)) {
                    if(stats.orientedEdges+2>edgeLimit) return false;
                    ++stats.orientedEdges;
                    if(!runtimeCellTransitionIsPassable(state,terrain,occupancy,current,next,representative)) return false;
                    ++stats.orientedEdges;
                    if(!runtimeCellTransitionIsPassable(state,terrain,occupancy,next,current,representative)) return false;
                    from.checked|=bit;
                    const int reverse=8-direction;
                    vertices.at(runtimeCellKey(next.x,next.y)).checked|=
                        static_cast<unsigned char>(1U<<(reverse<4?reverse:reverse-1));
                }
                current=next;
            }
            return true;
        };
        for(const auto* actor:actors) if(!walk({actor->x,actor->y})) { ++stats.walkFallbacks; return false; }
        for(const auto& slot:slots) if(!walk(slot)) { ++stats.walkFallbacks; return false; }
        if(provenPoints) { std::vector<RuntimeCellPoint> owned; owned.reserve(vertices.size());
            for(const auto& [key,value]:vertices) { (void)key; owned.push_back(value.point); }
            *provenPoints=std::move(owned); }
        ++stats.admitted; return true;
    } catch(const std::bad_alloc&) { ++stats.allocationFallbacks; return false; }
}

int repairRuntimeBridgeForHut(
    RuntimeProductionHostState& state,
    int hutObjectId) {
    const auto spanIt = state.bridgeRepairSpansByHut.find(hutObjectId);
    if (spanIt == state.bridgeRepairSpansByHut.end()
        || !runtimeBridgeRepairSpanCanBeRepaired(state, spanIt->second)) {
        return 0;
    }

    const RuntimeBridgeRepairSpan& span = spanIt->second;
    for (const RuntimeBridgeRepairCell& repairCell : span.cells) {
        if (!repairCell.requiresOverlay) {
            continue;
        }
        const auto terrainIt = state.terrainByCell.find(
            runtimeCellKey(repairCell.point.x, repairCell.point.y)
        );
        if (terrainIt == state.terrainByCell.end()
            || terrainIt->second == nullptr) {
            return 0;
        }
        const TerrainCell& terrain = *terrainIt->second;
        if (terrain.hasOverlay) {
            const bool compatible = span.high
                ? (runtimeOverlayLooksHighBridge(terrain)
                    && runtimeHighBridgeOverlayMatchesAxis(
                        terrain,
                        span.alongX
                    )
                    && runtimeHighBridgeMaterialForOverlay(terrain)
                        == span.material)
                : runtimeLowBridgeOverlayMatches(
                    terrain,
                    span.material,
                    span.alongX,
                    true
                );
            if (!compatible) {
                return 0;
            }
        }
    }

    int repaired = 0;
    for (const RuntimeBridgeRepairCell& repairCell : span.cells) {
        const int key =
            runtimeCellKey(repairCell.point.x, repairCell.point.y);
        const auto terrainIt = state.terrainByCell.find(key);
        if (terrainIt == state.terrainByCell.end()
            || terrainIt->second == nullptr) {
            continue;
        }
        TerrainCell& terrain = *const_cast<TerrainCell*>(terrainIt->second);
        bool changed = false;
        const bool hasCompatibleDeck = span.high
            ? (runtimeOverlayLooksHighBridge(terrain)
                && runtimeHighBridgeOverlayMatchesAxis(
                    terrain,
                    span.alongX
                )
                && runtimeHighBridgeMaterialForOverlay(terrain)
                    == span.material)
            : runtimeLowBridgeOverlayMatches(
                terrain,
                span.material,
                span.alongX,
                false
            );
        if (repairCell.requiresOverlay && !hasCompatibleDeck) {
            terrain.hasOverlay = true;
            terrain.overlayIndex = span.overlayIndex;
            terrain.overlayType = span.overlayType;
            terrain.overlayImage = span.overlayImage;
            terrain.overlayIsOre = false;
            terrain.overlayIsGem = false;
            changed = true;
        }
        if (span.high
            && terrain.hasOverlay
            && runtimeOverlayLooksHighBridge(terrain)
            && runtimeHighBridgeOverlayMatchesAxis(terrain, span.alongX)
            && terrain.overlayFrame != span.intactFrame) {
            terrain.overlayFrame = span.intactFrame;
            changed = true;
        } else if (!span.high
            && runtimeOverlayLooksVisibleLowBridge(terrain)
            && runtimeLowBridgeOverlayMatches(
                terrain,
                span.material,
                span.alongX,
                false
            )) {
            const int offset = runtimeLowBridgeOffset(terrain);
            int normalizedOffset = offset;
            if (span.alongX) {
                if (offset >= 1 && offset <= 8) {
                    normalizedOffset = 0;
                } else if (offset == 19) {
                    normalizedOffset = 18;
                } else if (offset == 21) {
                    normalizedOffset = 20;
                }
            } else {
                if (offset >= 10 && offset <= 17) {
                    normalizedOffset = 9;
                } else if (offset == 23) {
                    normalizedOffset = 22;
                } else if (offset == 25) {
                    normalizedOffset = 24;
                }
            }
            const int normalizedIndex =
                runtimeLowBridgeBaseIndex(span.material) + normalizedOffset;
            const std::string normalizedName =
                runtimeLowBridgeOverlayName(span.material, normalizedOffset);
            if (terrain.overlayIndex != normalizedIndex
                || terrain.overlayType != normalizedName
                || terrain.overlayImage != normalizedName) {
                terrain.overlayIndex = normalizedIndex;
                terrain.overlayType = normalizedName;
                terrain.overlayImage = normalizedName;
                changed = true;
            }
            if (terrain.overlayFrame != repairCell.expectedFrame) {
                terrain.overlayFrame = repairCell.expectedFrame;
                changed = true;
            }
        }
        if (changed) {
            ++repaired;
        }
    }

    if (repaired <= 0) {
        return 0;
    }

    std::vector<RuntimeCellPoint> changedCells;
    changedCells.reserve(span.cells.size());
    for (const RuntimeBridgeRepairCell& repairCell : span.cells) {
        changedCells.push_back(repairCell.point);
        const RuntimeCellPoint healthAnchor{
            repairCell.point.x
                - (!span.high && !span.alongX
                    ? repairCell.expectedFrame
                    : 0),
            repairCell.point.y
                - (!span.high && span.alongX
                    ? repairCell.expectedFrame
                    : 0)
        };
        state.bridgeHealthByCell.erase(runtimeCellKey(
            healthAnchor.x,
            healthAnchor.y
        ));
    }
    refreshRuntimeBridgeMutationState(state, changedCells);
    return repaired;
}

std::optional<RuntimeCellPoint> runtimeAmphibiousTransportAccessCell(
    RuntimeProductionHostState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport, bool boarding,
    const std::unordered_set<int>* reservedCells) {
    const auto hull = runtimeCellPointForObject(transport);
    const auto origin = boarding ? runtimeCellPointForObject(passenger) : hull;
    std::vector<RuntimeCellPoint> candidates;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (dx != 0 || dy != 0) candidates.push_back({hull.x + dx, hull.y + dy});
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        const double da = runtimeCellDistance(origin, a), db = runtimeCellDistance(origin, b);
        if (da != db) return da < db;
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    for (const auto& cell : candidates) {
        if (!findRuntimePassableCellNear(state, passenger, cell, 0, -1, reservedCells)) continue;
        if (std::abs(terrainLevelAtRuntimeCell(state, cell.x, cell.y) - transport.level) > 1) continue;
        if (runtimeInfantryUsesGroundSubcells(passenger)) {
            ensureRuntimeSubcellsInitialized(state);
            if (state.subcells.availableSubCells(
                    state.world.getObjects(), state.world.getTerrainObjects(),
                    {cell.x, cell.y, runtimeCellKey(cell.x, cell.y),
                        runtimeCellIsBridge(state, cell.x, cell.y)},
                    {passenger.instanceId},
                    runtimeGateSubcellContext(state, &passenger, RuntimeGatePassageMode::Commit)).empty()) continue;
        }
        if (boarding && origin != cell
            && findRuntimePath(state, passenger, origin, cell, transport.instanceId).empty()) continue;
        if (boarding && transport.kind == WorldObjectKind::Unit
            && !runtimeTransportBoardingAccessBodyClear(state, passenger, transport, cell)) continue;
        return cell;
    }
    return std::nullopt;
}

RuntimeTransportMotionBody runtimeTransportMotionBody(const WorldObjectInstance& object) {
    const auto slot = worldObjectSubCellOffset(object);
    return {runtimeObjectRenderX(object) + slot.x,
        runtimeObjectRenderY(object) + slot.y, runtimeObjectRenderLevel(object),
        object.hasRenderPosition ? object.renderOnBridge : object.onBridge};
}

int runtimeTransportAuthorizedBoardingHostId(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& destination) {
    if (!passenger.alive || passenger.health <= 0 || passenger.airborne
        || (passenger.kind != WorldObjectKind::Infantry && passenger.kind != WorldObjectKind::Unit)) return -1;
    const auto intent = state.pendingTransportTargets.find(passenger.instanceId);
    const auto move = state.behaviors.find(passenger.instanceId);
    if (intent == state.pendingTransportTargets.end() || move == state.behaviors.end()
        || move->second.kind != RuntimeBehaviorKind::Move
        || move->second.pendingAllowedOccupiedObjectId != intent->second) return -1;
    const auto* host = findObjectById(state, intent->second);
    if (!host || host->kind != WorldObjectKind::Unit || !host->alive || host->health <= 0
        || host->airborne || host->renderMoveActive || destination != runtimeCellPointForObject(*host)
        || !runtimeObjectCanEnterTransport(state, passenger, *host, true, true)) return -1;
    return host->instanceId;
}

void restoreRuntimeTransportMotionBody(WorldObjectInstance& object,
    const RuntimeTransportMotionBody& body) {
    const auto slot = worldObjectSubCellOffset(object);
    object.hasRenderPosition = true;
    object.renderMoveActive = false;
    object.renderX = body.x - slot.x;
    object.renderY = body.y - slot.y;
    object.renderLevel = body.level;
    object.renderOnBridge = body.onBridge;
}

std::optional<int> runtimeTransportMotionTransferTicks(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& accessCell, double curveLength) {
    if (!std::isfinite(curveLength) || curveLength <= 0.0) return std::nullopt;
    const double factor = runtimeTerrainMovementSpeedMultiplier(state, passenger, accessCell, accessCell);
    if (!std::isfinite(factor) || factor <= 0.0) return std::nullopt;
    const double ticks = runtimeMovementBaseIntervalTicks(state, passenger) * curveLength / factor;
    if (!std::isfinite(ticks)) return std::nullopt;
    return static_cast<int>(std::clamp(std::ceil(ticks), 1.0,
        double(std::numeric_limits<int>::max() - 1)));
}

bool runtimeTransportUnitBoardingPending(const RuntimeGameSessionState& state, int id) {
    const auto pending = state.pendingTransportTargets.find(id);
    const auto* host = pending == state.pendingTransportTargets.end()
        ? nullptr : findObjectById(state, pending->second);
    return host && host->kind == WorldObjectKind::Unit;
}

bool runtimeUnitTransportApproachAllowsBodyDetour(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeObjectBehavior& move) {
    const auto pending = state.pendingTransportTargets.find(passenger.instanceId);
    if (pending == state.pendingTransportTargets.end()
        || !runtimeInfantryUsesGroundSubcells(passenger)
        || !passenger.alive || passenger.health <= 0
        || move.kind != RuntimeBehaviorKind::Move || move.autoAcquired
        || move.targetObjectId >= 0 || move.pendingHasSharedGoal
        || move.transportTransferKind != RuntimeTransportTransferKind::None
        || move.pendingAllowedOccupiedObjectId < 0
        || pending->second != move.pendingAllowedOccupiedObjectId) return false;
    const auto* host = findObjectById(state, pending->second);
    if (!host || host->kind != WorldObjectKind::Unit || !host->alive || host->health <= 0
        || host->airborne || host->renderMoveActive) return false;
    const auto hull = runtimeCellPointForObject(*host);
    if (move.moveTarget == hull || std::abs(move.moveTarget.x - hull.x) > 1
        || std::abs(move.moveTarget.y - hull.y) > 1) return false;
    return runtimeTransportAuthorizedBoardingHostId(state, passenger, hull) == host->instanceId;
}

bool runtimeTransportUnloadingPending(const RuntimeGameSessionState& state,
    const WorldObjectInstance& host) {
    if (host.kind != WorldObjectKind::Unit || !host.alive || host.health <= 0
        || toLowerAsciiLocal(host.mission) != toLowerAsciiLocal(runtimeMissionName(RuntimeMissionIntent::Unloading))
        || host.transportPassengerIds.empty()) return false;
    const auto profile = runtimeTransportProfileForObject(state, host);
    return profile.canTransport && profile.canUnload;
}

bool runtimeTransportSavedBoardingEndpointAllowed(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    double toLevel, bool toBridge, std::optional<std::pair<double, bool>> validatedHostPose) {
    const auto move = state.behaviors.find(object.instanceId);
    if (move == state.behaviors.end() || move->second.transportTransferKind != RuntimeTransportTransferKind::Boarding
        || move->second.transportTransferHostId < 0 || move->second.moveSegmentFrom != from
        || runtimeCellPointForObject(object) != from || from == to
        || std::abs(from.x-to.x)>1 || std::abs(from.y-to.y)>1) return false;
    const int hostId = runtimeTransportAuthorizedBoardingHostId(state, object, to);
    const auto* host = hostId < 0 ? nullptr : findObjectById(state, hostId);
    if (!host || hostId != move->second.transportTransferHostId) return false;
    const auto expected = validatedHostPose.value_or(std::pair<double,bool>{
        runtimeObjectRenderLevel(*host), host->hasRenderPosition ? host->renderOnBridge : host->onBridge});
    return std::isfinite(toLevel) && std::abs(toLevel-expected.first)<=1.0e-6 && toBridge==expected.second;
}

bool runtimeTransportBoardingReadyForManifest(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeObjectBehavior& move) {
    if (move.kind != RuntimeBehaviorKind::Move || move.state != RuntimeBehaviorState::Complete
        || move.transportTransferKind != RuntimeTransportTransferKind::Boarding
        || move.transportTransferHostId < 0 || move.moveCooldownTicks > 0 || passenger.renderMoveActive
        || move.renderFrom != runtimeCellPointForObject(passenger) || move.renderFrom==move.renderTo
        || std::abs(move.renderFrom.x-move.renderTo.x)>1 || std::abs(move.renderFrom.y-move.renderTo.y)>1) return false;
    const int hostId = runtimeTransportAuthorizedBoardingHostId(state, passenger, move.renderTo);
    const auto* host = hostId < 0 ? nullptr : findObjectById(state, hostId);
    if (!host || hostId != move.transportTransferHostId) return false;
    const auto a=runtimeTransportMotionBody(passenger), b=runtimeTransportMotionBody(*host);
    return std::hypot(a.x-b.x,a.y-b.y)<=1.0e-9 && std::abs(a.level-b.level)<=1.0e-9
        && a.onBridge==b.onBridge;
}

bool cancelRuntimeTransportBoardingTransfer(RuntimeProductionHostState& state,
    WorldObjectInstance& passenger, RuntimeObjectBehavior& move) {
    const auto body=runtimeTransportMotionBody(passenger);
    restoreRuntimeTransportMotionBody(passenger,body);
    move.transportTransferHostId=-1; move.transportTransferKind=RuntimeTransportTransferKind::None;
    move.moveCooldownTicks=move.renderMoveTicksTotal=move.renderMoveTicksRemaining=0;
    move.moveSegmentFrom={-1,-1}; move.pendingAllowedOccupiedObjectId=-1;
    replaceRuntimeBehaviorPath(move,{}); move.pathRequestPending=false;
    move.kind=RuntimeBehaviorKind::None; move.state=RuntimeBehaviorState::Blocked;
    state.pendingTransportTargets.erase(passenger.instanceId);
    releaseRuntimeInfantrySubcellReservation(state,passenger.instanceId);
    setRuntimeMissionIntent(state,passenger,RuntimeMissionIntent::Idle,"transportBoardingCancelled");
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return true;
}

void cancelRuntimeTransportBoardingForMovingHost(RuntimeProductionHostState& state, int hostId) {
    std::vector<int> ids;
    for (const auto& [id,target]:state.pendingTransportTargets) {
        if (target!=hostId) continue;
        const auto found=state.behaviors.find(id);
        const auto* passenger = findObjectById(state, id);
        if (found!=state.behaviors.end()
            && ((found->second.transportTransferKind==RuntimeTransportTransferKind::Boarding
                    && found->second.transportTransferHostId==hostId)
                || (passenger && runtimePersonalChronoTransportPassenger(state, *passenger)
                    && found->second.kind==RuntimeBehaviorKind::Move
                    && found->second.pendingAllowedOccupiedObjectId==hostId))) ids.push_back(id);
    }
    std::sort(ids.begin(),ids.end());
    for (int id:ids) {
        auto* passenger=findMutableObjectById(state,id);
        const auto found=state.behaviors.find(id);
        if (passenger&&found!=state.behaviors.end()) {
            if (runtimePersonalChronoTransportPassenger(state, *passenger)) {
                if (eraseRuntimeBehavior(state, id)) {
                    clearRuntimeRenderPosition(*passenger);
                    setRuntimeMissionIntent(state, *passenger, RuntimeMissionIntent::Idle, "chronoTransportHostMoving");
                    refreshRuntimeObjectOccupancyAfterLocalChange(state);
                }
                continue;
            }
            cancelRuntimeTransportBoardingTransfer(state,*passenger,found->second);
            // The accepted host command can be saved before the next cleanup tick.
            eraseRuntimeBehavior(state,id);
        }
    }
}

bool advanceRuntimeTransportBoardingSegment(RuntimeProductionHostState& state,
    WorldObjectInstance& passenger, RuntimeObjectBehavior& move) {
    const int hostId=runtimeTransportAuthorizedBoardingHostId(state,passenger,move.renderTo);
    const auto* host=hostId<0?nullptr:findObjectById(state,hostId);
    const auto ownSlot=worldObjectSubCellOffset(passenger);
    const auto expected=host?runtimeTransportMotionBody(*host):RuntimeTransportMotionBody{};
    const bool valid=host && hostId==move.transportTransferHostId
        && runtimeCellPointForObject(passenger)==move.renderFrom
        && std::abs(move.renderTo.x+move.renderToOffsetX+ownSlot.x-expected.x)<=1.0e-9
        && std::abs(move.renderTo.y+move.renderToOffsetY+ownSlot.y-expected.y)<=1.0e-9
        && std::abs(move.renderToLevel-expected.level)<=1.0e-9 && move.renderToOnBridge==expected.onBridge;
    if (!valid) return cancelRuntimeTransportBoardingTransfer(state,passenger,move);
    if (move.moveCooldownTicks>0) {
        if (runtimeGroundRenderStepWaitsForStoppedBody(state,passenger,move)) {
            move.state=RuntimeBehaviorState::Blocked; return false;
        }
        move.state=RuntimeBehaviorState::Cooldown;
        const bool changed=advanceRuntimeRenderMove(state,passenger,move);
        --move.moveCooldownTicks;
        if (move.moveCooldownTicks<=0) move.state=RuntimeBehaviorState::Complete;
        return changed;
    }
    move.state=RuntimeBehaviorState::Complete;
    return false;
}

bool runtimeTransportTransferBodiesClear(RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeTransportMotionBody& source,
    const RuntimeTransportMotionBody& destination, int hostId) {
    (void)runtimeTickOccupancyLookup(state);
    const RuntimeTransportMotionPoint a{source.x, source.y}, b{destination.x, destination.y};
    const auto pointDistance = [](const auto& p, const auto& x, const auto& y) {
        const double dx = y[0] - x[0], dy = y[1] - x[1];
        const double length = dx * dx + dy * dy;
        const double t = length > 1.0e-18
            ? std::clamp(((p[0] - x[0]) * dx + (p[1] - x[1]) * dy) / length, 0.0, 1.0) : 0.0;
        return std::pow(p[0] - x[0] - dx * t, 2) + std::pow(p[1] - x[1] - dy * t, 2);
    };
    const auto segmentDistance = [&](const auto& x, const auto& y, const auto& z, const auto& w) {
        const double dx = y[0] - x[0], dy = y[1] - x[1], ex = w[0] - z[0], ey = w[1] - z[1];
        const double denominator = dx * ey - dy * ex;
        if (std::abs(denominator) > 1.0e-12) {
            const double zx = z[0] - x[0], zy = z[1] - x[1];
            const double t = (zx * ey - zy * ex) / denominator;
            const double u = (zx * dy - zy * dx) / denominator;
            if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0.0;
        }
        return std::min({pointDistance(x, z, w), pointDistance(y, z, w),
            pointDistance(z, x, y), pointDistance(w, x, y)});
    };
    std::unordered_set<int> seen;
    bool clear = true;
    const auto inspect = [&](const WorldObjectInstance& other) {
        if (!clear || other.instanceId == passenger.instanceId || other.instanceId == hostId
            || !seen.insert(other.instanceId).second || !runtimeAllGroundBodyIsSpatialCandidate(state, other)
            || runtimeObjectOffGroundByMagBeam(state, other.instanceId)) return;
        if (runtimeCrusherUsesCrushingMovement(state, passenger)
            && runtimeCrusherCanCrushObject(state, passenger, other, runtimeCellPointForObject(other), true)) return;
        const auto slot = worldObjectSubCellOffset(other);
        const RuntimeTransportMotionPoint current{runtimeObjectRenderX(other) + slot.x,
            runtimeObjectRenderY(other) + slot.y};
        const bool layer = other.hasRenderPosition ? other.renderOnBridge : other.onBridge;
        const bool foot = passenger.kind == WorldObjectKind::Infantry;
        const bool otherFoot = other.kind == WorldObjectKind::Infantry;
        const double minimum = foot && otherFoot ? 0.5 : (!foot && !otherFoot ? 1.0 : 0.75);
        const double squared = minimum * minimum;
        const auto check = [&](const auto& x, const auto& y, bool otherLayer) {
            const RuntimeTransportMotionPoint middle{(a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5};
            if (source.onBridge == otherLayer && segmentDistance(a,
                    source.onBridge == destination.onBridge ? b : middle, x, y) + 1.0e-9 < squared) clear = false;
            if (source.onBridge != destination.onBridge && destination.onBridge == otherLayer
                && segmentDistance(middle, b, x, y) + 1.0e-9 < squared) clear = false;
        };
        check(current, current, layer);
        if (!clear || !other.renderMoveActive) return;
        const auto found = state.behaviors.find(other.instanceId);
        if (found == state.behaviors.end() || found->second.renderMoveTicksTotal <= 0) return;
        const auto& move = found->second;
        const double progress = runtimeVehicleMoveRenderProgress(move);
        std::vector<double> cuts{progress, 1.0};
        if (move.renderFromOnBridge != move.renderToOnBridge && progress < 0.5) cuts.push_back(0.5);
        if (runtimeInfantryMoveUsesCommittedCorner(other, move)) {
            const double first = std::hypot(move.renderFromOffsetX, move.renderFromOffsetY);
            const double second = std::hypot(double(move.renderTo.x - move.renderFrom.x),
                double(move.renderTo.y - move.renderFrom.y));
            const int ticks = std::max(2, move.renderMoveTicksTotal);
            const double pivot = double(std::clamp(static_cast<int>(std::ceil(ticks * first / (first + second))),
                1, ticks - 1)) / ticks;
            if (pivot > progress) cuts.push_back(pivot);
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t n = 1; clear && n < cuts.size(); ++n) {
            auto first = runtimeGroundRenderMovePoint(other, move, cuts[n - 1]);
            auto last = runtimeGroundRenderMovePoint(other, move, cuts[n]);
            first[0] += slot.x; first[1] += slot.y; last[0] += slot.x; last[1] += slot.y;
            check(first, last, (cuts[n - 1] + cuts[n]) * 0.5 < 0.5
                ? move.renderFromOnBridge : move.renderToOnBridge);
        }
    };
    const int left = static_cast<int>(std::floor(std::min(a[0], b[0]) + 0.5)) - 1;
    const int right = static_cast<int>(std::floor(std::max(a[0], b[0]) + 0.5)) + 1;
    const int top = static_cast<int>(std::floor(std::min(a[1], b[1]) + 0.5)) - 1;
    const int bottom = static_cast<int>(std::floor(std::max(a[1], b[1]) + 0.5)) + 1;
    for (int y = std::max(0, top); clear && y <= std::min(511, bottom); ++y)
        for (int x = std::max(0, left); clear && x <= std::min(511, right); ++x)
            forEachRuntimeAllGroundBodyCandidateAtCell(state, {x, y}, inspect);
    return clear;
}

bool runtimeTransportTransferTransitionClear(RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeCellPoint& from,
    const RuntimeCellPoint& to, int hostId, bool boarding) {
    if (from == to || std::abs(to.x - from.x) > 1 || std::abs(to.y - from.y) > 1) return false;
    const auto legal = [&](const RuntimeCellPoint& cell) {
        return findRuntimePassableCellNear(state, passenger, cell, 0, -1).has_value();
    };
    // Only the one verified hull cell is internal. All outside and diagonal cells remain ordinary terrain.
    if (!(boarding ? legal(from) : legal(to))) return false;
    if (runtimeFoundationBlocksMovement(state, passenger, from, hostId, false, RuntimeGatePassageMode::Commit)
        || runtimeFoundationBlocksMovement(state, passenger, to, hostId, false, RuntimeGatePassageMode::Commit)) return false;
    if (from.x != to.x && from.y != to.y)
        for (const RuntimeCellPoint side : {RuntimeCellPoint{from.x, to.y}, RuntimeCellPoint{to.x, from.y}})
            if (!legal(side) || runtimeFoundationBlocksMovement(state, passenger, side, hostId,
                    false, RuntimeGatePassageMode::Commit)) return false;
    return true;
}

bool runtimeTransportTransferFoundationClear(const RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const RuntimeTransportMotionBody& source,
    const RuntimeTransportMotionBody& destination, int hostId) {
    if (!std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.level)
        || !std::isfinite(destination.x) || !std::isfinite(destination.y) || !std::isfinite(destination.level)) return false;
    const int left = static_cast<int>(std::floor(std::min(source.x, destination.x) + 0.5));
    const int right = static_cast<int>(std::floor(std::max(source.x, destination.x) + 0.5));
    const int top = static_cast<int>(std::floor(std::min(source.y, destination.y) + 0.5));
    const int bottom = static_cast<int>(std::floor(std::max(source.y, destination.y) + 0.5));
    if (left < 0 || top < 0 || right >= 512 || bottom >= 512 || (right-left+1)*(bottom-top+1) > 25) return false;
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        double first = 0.0, last = 1.0;
        const auto clip = [&](double p, double delta, double low, double high) {
            if (std::abs(delta) <= 1.0e-12) return p >= low && p <= high;
            double a = (low - p) / delta, b = (high - p) / delta;
            if (a > b) std::swap(a, b);
            first = std::max(first, a); last = std::min(last, b);
            return first <= last;
        };
        if (clip(source.x, destination.x-source.x, x-0.5, x+0.5)
            && clip(source.y, destination.y-source.y, y-0.5, y+0.5)
            && runtimeFoundationBlocksMovement(state, passenger, {x,y}, hostId,
                false, RuntimeGatePassageMode::Commit)) return false;
    }
    return true;
}

bool beginRuntimeTransportGroundTransfer(RuntimeProductionHostState& state,
    WorldObjectInstance& passenger, const WorldObjectInstance& host,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    const RuntimeTransportMotionBody& source, const RuntimeTransportMotionBody& destination,
    int destinationSlot, bool boarding, const std::vector<RuntimeCellPoint>& remainingPath) {
    if (!runtimeTransportTransferTransitionClear(state, passenger, from, to, host.instanceId, boarding)
        || !runtimeTransportTransferFoundationClear(state, passenger, source, destination, host.instanceId)
        || !runtimeTransportTransferBodiesClear(state, passenger, source, destination, host.instanceId)) return false;
    const auto ticks = runtimeTransportMotionTransferTicks(state, passenger, boarding ? from : to,
        std::hypot(destination.x - source.x, destination.y - source.y));
    if (!ticks) return false;
    const auto found = state.behaviors.find(passenger.instanceId);
    RuntimeObjectBehavior move = found == state.behaviors.end() ? RuntimeObjectBehavior{} : found->second;
    move.kind = RuntimeBehaviorKind::Move;
    move.state = RuntimeBehaviorState::Moving;
    move.pathRequestPending = false;
    move.moveTarget = remainingPath.empty() ? to : remainingPath.back();
    move.pendingAllowedOccupiedObjectId = host.instanceId;
    move.transportTransferHostId = host.instanceId;
    move.transportTransferKind = boarding ? RuntimeTransportTransferKind::Boarding : RuntimeTransportTransferKind::Egress;
    move.renderArrivalDistanceAfter = -1.0;
    replaceRuntimeBehaviorPath(move, remainingPath);
    releaseRuntimeInfantrySubcellReservation(state, passenger.instanceId);
    passenger.alive = true;
    passenger.airborne = false;
    passenger.dockedToObjectId = passenger.dockIndex = -1;
    passenger.mission = boarding ? "Entering" : "Move";
    // Boarding owns its original legal access cell/slot until the body reaches the hull.
    // Egress owns its real first exit cell immediately, preserving the accepted source pose.
    if (!boarding) {
        setRuntimeObjectCell(state, passenger, to, false);
        passenger.subCell = destinationSlot;
    }
    restoreRuntimeTransportMotionBody(passenger, source);
    move.moveCooldownTicks = *ticks;
    beginRuntimeRenderMove(passenger, move, from, to, source.level, source.onBridge, *ticks, &state);
    const auto slot = worldObjectSubCellOffset(passenger);
    move.renderToOffsetX = destination.x - to.x - slot.x;
    move.renderToOffsetY = destination.y - to.y - slot.y;
    move.renderToLevel = destination.level;
    move.renderToOnBridge = destination.onBridge;
    alignRuntimeFacingToRenderMove(passenger, move);
    state.behaviors[passenger.instanceId] = std::move(move);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return true;
}

bool advanceRuntimeUnitTransportBoarding(RuntimeProductionHostState& state,
    WorldObjectInstance& passenger, const WorldObjectInstance& host) {
    if (passenger.renderMoveActive || host.airborne || host.renderMoveActive) return false;
    if (runtimePersonalChronoTransportPassenger(state, passenger))
        return runtimeChronoTransportArrivalReady(state, passenger, host);
    const auto source = runtimeTransportMotionBody(passenger), destination = runtimeTransportMotionBody(host);
    const auto from = runtimeCellPointForObject(passenger), to = runtimeCellPointForObject(host);
    if (std::hypot(source.x - destination.x, source.y - destination.y) <= 1.0e-9
        && std::abs(source.level - destination.level) <= 1.0e-9
        && source.onBridge == destination.onBridge) return true;
    if (from == to) return false; // Caller takes a real adjacent access route; never fabricate a source.
    (void)beginRuntimeTransportGroundTransfer(state, passenger, host, from, to, source, destination, -1, true);
    return false;
}

void prepareRuntimeTransportUnloadMotion(RuntimeProductionHostState& state, WorldObjectInstance& host) {
    const auto previous = state.behaviors.find(host.instanceId);
    const std::optional<RuntimeObjectBehavior> accepted = previous != state.behaviors.end()
        && previous->second.kind == RuntimeBehaviorKind::Move && previous->second.moveCooldownTicks > 0
        ? std::optional<RuntimeObjectBehavior>(previous->second) : std::nullopt;
    const auto body = runtimeTransportMotionBody(host);
    const bool hadPose = host.hasRenderPosition;
    eraseRuntimeBehavior(state, host.instanceId);
    if (host.airborne && runtimeJumpjetProfile(state, host)) {
        const auto flight = state.jumpjetFlights.find(host.instanceId);
        if (flight != state.jumpjetFlights.end() && (flight->second.active || flight->second.stopped)) {
            RuntimeObjectBehavior last = accepted.value_or(RuntimeObjectBehavior{});
            last.kind = RuntimeBehaviorKind::Move;
            last.state = RuntimeBehaviorState::Moving;
            last.path.clear(); last.pathRequestPending = false;
            last.moveTarget = runtimeCellPointForObject(host);
            resumeRuntimeJumpjetSegment(state, host, last);
            state.behaviors[host.instanceId] = std::move(last);
        }
        applyRuntimeJumpjetPose(state, host);
    } else if (accepted) {
        auto last = *accepted;
        last.path.clear(); last.pathRequestPending = false;
        last.moveTarget = runtimeCellPointForObject(host);
        inheritRuntimeVehicleMoveSegment(host, last, *accepted);
        state.behaviors[host.instanceId] = std::move(last);
    } else if (hadPose) restoreRuntimeTransportMotionBody(host, body);
}

int unloadRuntimeUnitTransportMotion(RuntimeProductionHostState& state, WorldObjectInstance& host) {
    if (!host.alive || host.health <= 0 || host.airborne || host.renderMoveActive) return 0;
    const auto hostCell = runtimeCellPointForObject(host);
    const auto body = runtimeTransportMotionBody(host);
    int unloaded = 0;
    const auto manifest = host.transportPassengerIds;
    // The first physical exit is reserved immediately; its later outer goal is not.
    // Count accepted foot goals once, without reserving or changing any slot.
    std::map<std::pair<int, bool>, int> futureFootGoals;
    if (!runtimeAmphibiousTransport(state, host)) {
        const auto poseContext = runtimeGroundPoseContext(state);
        for (const auto& [otherId, move] : state.behaviors) {
            if (move.kind != RuntimeBehaviorKind::Move
                || move.moveTarget.x < 0 || move.moveTarget.y < 0
                || move.moveTarget.x >= 512 || move.moveTarget.y >= 512
                || std::max(std::abs(move.moveTarget.x-hostCell.x), std::abs(move.moveTarget.y-hostCell.y)) != 2) continue;
            const auto* other = findObjectById(state, otherId);
            if (!other || other->health <= 0 || !runtimeInfantryUsesGroundSubcells(*other)) continue;
            const bool atGoal = runtimeCellPointForObject(*other) == move.moveTarget;
            const bool goalBridge = atGoal ? other->onBridge
                : move.renderMoveTicksTotal > 0 && move.renderTo == move.moveTarget
                    ? move.renderToOnBridge : runtimeCellIsBridge(state, move.moveTarget.x, move.moveTarget.y);
            if (atGoal && RuntimeSubcellSystemState::isActiveSubCell(other->subCell)) continue;
            const auto reserved = state.subcells.reservations().find(otherId);
            if (reserved != state.subcells.reservations().end()
                && RuntimeSubcellSystemState::isActiveSubCell(reserved->second.subCell)
                && reserved->second.destination.x == move.moveTarget.x
                && reserved->second.destination.y == move.moveTarget.y
                && reserved->second.destination.onBridge == goalBridge) continue;
            const auto held = runtimeAdditionalGroundPoseOccupancy(*other, &poseContext);
            if (held && held->x == move.moveTarget.x && held->y == move.moveTarget.y
                && held->onBridge == goalBridge && RuntimeSubcellSystemState::isActiveSubCell(held->subCell)) continue;
            ++futureFootGoals[{runtimeCellKey(move.moveTarget.x, move.moveTarget.y), goalBridge}];
        }
    }
    for (int id : manifest) {
        auto* stored = findMutableRuntimeObjectByIdIncludingInactive(state, id);
        if (!stored || stored->alive || stored->health <= 0) continue;
        auto probe = *stored;
        probe.alive = true; probe.airborne = false; probe.mission = "Move";
        std::vector<RuntimeCellPoint> candidates;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) candidates.push_back({hostCell.x + dx, hostCell.y + dy});
        std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
            const double da = runtimeCellDistance(hostCell, a), db = runtimeCellDistance(hostCell, b);
            if (da != db) return da < db;
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        bool accepted = false;
        for (const auto& first : candidates) {
            if (!findRuntimePassableCellNear(state, probe, first, 0, -1)
                || std::abs(terrainLevelAtRuntimeCell(state, first.x, first.y) - host.level) > 1) continue;
            std::vector<int> slots{-1};
            if (probe.kind == WorldObjectKind::Infantry) slots = runtimeAvailableInfantrySubcells(state, probe, first);
            for (int slot : slots) {
                const auto offset = worldInfantrySubCellOffset(slot);
                const RuntimeTransportMotionBody destination{first.x + offset.x, first.y + offset.y,
                    double(terrainLevelAtRuntimeCell(state, first.x, first.y)), runtimeCellIsBridge(state, first.x, first.y)};
                const double hullClearance = probe.kind == WorldObjectKind::Infantry ? 0.75 : 1.0;
                if (destination.onBridge == body.onBridge
                    && std::hypot(destination.x-body.x, destination.y-body.y) + 1.0e-9 < hullClearance) continue;
                if (!runtimeTransportTransferTransitionClear(state, probe, hostCell, first, host.instanceId, false)
                    || !runtimeTransportTransferBodiesClear(state, probe, body, destination, host.instanceId)) continue;
                std::vector<RuntimeCellPoint> remainingPath;
                if (!runtimeAmphibiousTransport(state, host)) {
                    auto walking = probe;
                    walking.x = first.x; walking.y = first.y; walking.cell = runtimeCellKey(first.x, first.y);
                    walking.level = terrainLevelAtRuntimeCell(state, first.x, first.y);
                    walking.onBridge = runtimeCellIsBridge(state, first.x, first.y); walking.subCell = slot;
                    walking.hasRenderPosition = walking.renderMoveActive = false;
                    for (int dy = -2; remainingPath.empty() && dy <= 2; ++dy)
                        for (int dx = -2; remainingPath.empty() && dx <= 2; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != 2) continue;
                            const RuntimeCellPoint outer{hostCell.x + dx, hostCell.y + dy};
                            if (!findRuntimePassableCellNear(state, walking, outer, 0, -1)) continue;
                            if (runtimeInfantryUsesGroundSubcells(walking)) {
                                const auto key = std::make_pair(runtimeCellKey(outer.x, outer.y),
                                    runtimeCellIsBridge(state, outer.x, outer.y));
                                const auto claimed = futureFootGoals.find(key);
                                if (claimed != futureFootGoals.end()
                                    && runtimeAvailableInfantrySubcells(state, walking, outer).size()
                                        <= static_cast<std::size_t>(claimed->second)) continue;
                            }
                            remainingPath = findRuntimePath(state, walking, first, outer, -1);
                        }
                }
                // Commit the adjacent exit, then walk the real route to a legal radius2 endpoint.
                if (!beginRuntimeTransportGroundTransfer(state, *stored, host, hostCell, first,
                        body, destination, slot, false, remainingPath)) continue;
                auto& cargo = host.transportPassengerIds;
                cargo.erase(std::remove(cargo.begin(), cargo.end(), id), cargo.end());
                syncRuntimeTransportCargoPositions(state, *stored);
                accepted = true; ++unloaded; break;
            }
            if (accepted) break;
        }
        if (accepted) break; // Later cargo waits on real body clearance, not a fixed delay.
    }
    refreshRuntimeTransportGunnerState(state, host);
    if (host.transportPassengerIds.empty()) setRuntimeMissionIntent(state, host,
        RuntimeMissionIntent::Idle, "transportUnloadComplete");
    else setRuntimeMissionIntent(state, host, RuntimeMissionIntent::Unloading, "transportUnloadPending");
    if (unloaded) refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return unloaded;
}

bool advanceRuntimeTransportUnloading(RuntimeProductionHostState& state) {
    std::vector<int> ids;
    for (const auto& object : state.world.getObjects())
        if (runtimeTransportUnloadingPending(state, object)) ids.push_back(object.instanceId);
    bool changed = false;
    for (int id : ids) if (auto* host = findMutableObjectById(state, id))
        changed = unloadRuntimeTransport(state, *host) > 0 || changed;
    return changed;
}

bool runtimeTransportBoardingAccessBodyClear(RuntimeGameSessionState& state,
    const WorldObjectInstance& passenger, const WorldObjectInstance& host, const RuntimeCellPoint& access) {
    auto probe = passenger;
    const bool actual = runtimeCellPointForObject(passenger) == access && !passenger.renderMoveActive;
    int slot = -1;
    if (probe.kind == WorldObjectKind::Infantry) {
        slot = actual ? probe.subCell : RuntimeSubcellSystemState::selectAvailableSubCell(
            runtimeAvailableInfantrySubcells(state, probe, access), probe.subCell);
        if (!RuntimeSubcellSystemState::isActiveSubCell(slot)) return false;
    }
    const auto offset = worldInfantrySubCellOffset(slot);
    const auto source = actual ? runtimeTransportMotionBody(passenger)
        : RuntimeTransportMotionBody{access.x + offset.x, access.y + offset.y,
            double(terrainLevelAtRuntimeCell(state, access.x, access.y)), runtimeCellIsBridge(state, access.x, access.y)};
    return runtimeTransportTransferTransitionClear(state, probe, access, runtimeCellPointForObject(host), host.instanceId, true)
        && runtimeTransportTransferFoundationClear(state, probe, source, runtimeTransportMotionBody(host), host.instanceId)
        && runtimeTransportTransferBodiesClear(state, probe, source, runtimeTransportMotionBody(host), host.instanceId);
}

bool runtimeGroundDepartureCapacityHoldsTransition(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to) {
    return runtimeGroundDepartureHoldsCell(state, object, to)
        || (from.x != to.x && from.y != to.y
            && (runtimeGroundDepartureHoldsCell(state, object, {from.x, to.y})
                || runtimeGroundDepartureHoldsCell(state, object, {to.x, from.y})));
}

bool runtimeMoveAllowsCrowdBodyRoute(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    const bool transportApproach = runtimeUnitTransportApproachAllowsBodyDetour(state, object, behavior);
    if (behavior.kind != RuntimeBehaviorKind::Move || behavior.autoAcquired
        || behavior.targetObjectId >= 0
        || (behavior.pendingAllowedOccupiedObjectId >= 0 && !transportApproach)
        || object.airborne) return false;

    // A legal Fortress contact owns this occupied endpoint. The ordinary
    // crowd search uses non-crushing cells and must not exchange it for air.
    if (runtimeOmniCrushOwnsOccupiedEndpoint(state, object, behavior.moveTarget))
        return false;

    // These activities own a precise entrance, service cell or resource target.
    const int id = object.instanceId;
    if (runtimeFactoryDeliveryOwnsEgress(state, id)
        || runtimeCloneDeliveryOwnsDoorway(state, id)
        || state.pendingEngineerCaptures.contains(id) || state.pendingEngineerActions.contains(id)
        || state.pendingOccupyTargets.contains(id)
        || (state.pendingTransportTargets.contains(id) && !transportApproach)
        || std::any_of(state.grinding.relations().begin(), state.grinding.relations().end(),
            [id](const RuntimeGrindingRelation& entry) { return entry.sourceId == id; })
        || std::any_of(state.repairDepots.relations().begin(), state.repairDepots.relations().end(),
            [id](const RuntimeRepairDepotRelation& entry) { return entry.unitId == id; })
        || (!object.harvestAutoSearchSuspended
            && (runtimeObjectIsHarvester(state, object) || runtimeObjectIsMobileSlaveMiner(state, object))))
        return false;
    return true;
}

bool runtimeMoveAllowsNearbyCrowdArrival(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    return !state.pendingTransportTargets.contains(object.instanceId)
        && !behavior.pendingIgnoredObjectIds.empty()
        && runtimeMoveAllowsCrowdBodyRoute(state, object, behavior);
}

void runtimeSetCrowdInfantryPreviewAtCell(
    const RuntimeGameSessionState& state, WorldObjectInstance& preview,
    const RuntimeCellPoint& cell, int subCell) {
    preview.x=cell.x; preview.y=cell.y; preview.cell=runtimeCellKey(cell.x,cell.y);
    preview.subCell=subCell; preview.onBridge=runtimeCellIsBridge(state,cell.x,cell.y);
    preview.hasRenderPosition=preview.renderMoveActive=false;
}

bool runtimeCrowdBodyTransitionIsPassable(
    const RuntimeGameSessionState& state,
    const std::unordered_map<int,const TerrainCell*>& terrain,
    const std::unordered_map<int,int>& occupied,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    const WorldObjectInstance& preview, bool actualSource,
    PathMovementMode mode, int& destinationSubCell,
    std::vector<int>* blockingObjectIds, bool allowCrushableDestination) {
    if (blockingObjectIds) blockingObjectIds->clear();
    destinationSubCell=preview.subCell;
    if (!runtimeCellTransitionIsPassable(state,terrain,occupied,from,to,
            preview,-1,nullptr,allowCrushableDestination,mode)) return false;
    if (!runtimeInfantryUsesGroundSubcells(preview)) return true;
    if (actualSource) destinationSubCell=runtimeInfantryBodyPreviewDestinationSubcell(state,preview,to);
    else {
        const auto available=runtimeAvailableInfantrySubcells(state,preview,to);
        destinationSubCell=RuntimeSubcellSystemState::selectAvailableSubCell(available,preview.subCell);
    }
    return destinationSubCell>=0 && !runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(
        state,preview,from,to,destinationSubCell,blockingObjectIds);
}

RuntimeSharedMoveCrowdSearch findRuntimeCloserSharedMoveSlot(
    RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    RuntimeSharedMoveCrowdSearch result;
    if ((!behavior.pendingHasSharedGoal && !runtimeInfantryUsesGroundSubcells(object))
        || behavior.moveCooldownTicks > 0 || object.renderMoveActive
        || !(behavior.pendingHasSharedGoal
            ? runtimeMoveAllowsNearbyCrowdArrival(state, object, behavior)
            : runtimeMoveAllowsCrowdBodyRoute(state, object, behavior))) return result;
    const auto current = runtimeCellPointForObject(object);
    const auto center = behavior.pendingHasSharedGoal ? behavior.pendingSharedGoal : behavior.moveTarget;
    const auto radius = [&](const RuntimeCellPoint& cell) {
        return std::max(std::abs(cell.x-center.x), std::abs(cell.y-center.y));
    };
    const int currentRadius = radius(current);
    // Formation slots and this bounded search use square cell rings. Apply
    // the existing near-goal radius in the same metric, including its corners.
    const int searchArrivalRadius=behavior.pendingHasSharedGoal
        ? runtimeSharedMoveArrivalRadius(behavior) : 3;
    bool distantBodyDetour=false;
    if (currentRadius > searchArrivalRadius) {
        if (behavior.pendingHasSharedGoal || behavior.pathRequestPending || behavior.path.empty()) return result;
        // A distant ordinary foot order needs a local physical detour only
        // when its actual next reserved body segment is obstructed. Clear
        // far-away routes keep the ordinary path without a bounded BFS.
        (void)runtimeTickOccupancyLookup(state);
        if (!runtimeInfantryBodySegmentAdmissionBlocked(state,object,current,behavior.path.front())) return result;
        distantBodyDetour=true;
    }
    int infantry = 0, vehicles = 0;
    for (int id : behavior.pendingIgnoredObjectIds) {
        const auto* member = findObjectById(state, id);
        if (!member || !member->alive || member->airborne
            || runtimeObjectMovementMode(state, *member) == PathMovementMode::Air) continue;
        if (runtimeInfantryUsesGroundSubcells(*member)) ++infantry;
        else ++vehicles;
    }
    const int cells = std::max(1, vehicles+(infantry+2)/3);
    while ((2*result.idealRadius+1)*(2*result.idealRadius+1) < cells) ++result.idealRadius;
    const bool foot=runtimeInfantryUsesGroundSubcells(object);
    if (currentRadius <= result.idealRadius && (!foot || current==behavior.moveTarget)) return result;

    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const auto* terrain = &state.terrainByCell;
    if (terrain->empty()) { generatedTerrain = buildRuntimeTerrainLookup(state.world); terrain = &generatedTerrain; }
    const auto here = terrain->find(runtimeCellKey(current.x, current.y));
    if (here == terrain->end() || !here->second) return result;
    const auto mode = runtimeObjectMovementMode(state, object);
    if (mode == PathMovementMode::Air) return result;
    const auto hereInfo = runtimeCachedTerrainInfoForCell(state, *here->second);
    const auto sameSurface = [&](const RuntimeCellPoint& cell) {
        const auto at = terrain->find(runtimeCellKey(cell.x, cell.y));
        if (at == terrain->end() || !at->second
            || runtimeEffectiveTerrainLevel(*at->second) != runtimeEffectiveTerrainLevel(*here->second)) return false;
        const auto info = runtimeCachedTerrainInfoForCell(state, *at->second);
        return info.passable && runtimeAuthoredLandTypeAllowsMover(state, info, object)
            && runtimeMapQueryPassabilityAllows(state, cell, object, mode)
            && (mode == PathMovementMode::Amphibious || hereInfo.water == info.water
                || hereInfo.shore || info.shore || hereInfo.bridge || info.bridge);
    };
    // A nearby plateau or another water surface is still a travel destination.
    if (!sameSurface(center) || !sameSurface(behavior.moveTarget)) return result;
    result.applicable = true;
    const auto& occupied = runtimeTickOccupancyLookup(state);
    // Preserve a genuinely usable committed route before doing any BFS. A
    // passable first step alone is insufficient: its inner end may be sealed.
    if (!behavior.pathRequestPending && !behavior.path.empty() && behavior.path.size() <= 24
        && (behavior.path.back() == behavior.moveTarget
            || (!behavior.pendingHasSharedGoal && radius(behavior.path.back()) < currentRadius
                && runtimeCellDistance(behavior.path.back(),center)+0.001
                    < runtimeCellDistance(current,center)))
        && (radius(behavior.moveTarget) < currentRadius || foot)) {
        RuntimeCellPoint previous = current;
        WorldObjectInstance preview=object;
        bool usable = true, actualSource=true;
        for (const auto& next : behavior.path) {
            int destinationSubCell=preview.subCell;
            if (!sameSurface(next) || !runtimeCrowdBodyTransitionIsPassable(state,*terrain,occupied,
                    previous,next,preview,actualSource,mode,destinationSubCell)) { usable=false; break; }
            previous=next; actualSource=false;
            if (foot) runtimeSetCrowdInfantryPreviewAtCell(state,preview,next,destinationSubCell);
        }
        if (usable) { result.inwardRoute=true; return result; }
    }
    // The navigation occupancy map keeps only one representative per cell.
    // A full infantry cell can contain both settled and departing occupants,
    // or be temporarily full because its last subcell is reserved. Index that
    // evidence once per local search, and classify each blocked cell once.
    bool infantryBarriersIndexed = false;
    std::unordered_map<int,std::vector<const WorldObjectInstance*>> infantryByCell;
    std::set<std::pair<int,bool>> reservedInfantryCells;
    std::unordered_set<int> notedBarrierCells;
    const auto noteOccupant = [&](const WorldObjectInstance* other, bool onBridge) {
        if (!other || !other->alive || other->airborne
            || other->instanceId == object.instanceId || other->onBridge != onBridge
            || (other->kind != WorldObjectKind::Unit && other->kind != WorldObjectKind::Infantry)) return;
        if (!runtimeOwnersAreAllied(state, object.owner, other->owner)) {
            result.foreignBarrier = true; return;
        }
        const auto motion = state.behaviors.find(other->instanceId);
        const bool settled = !other->renderMoveActive && (motion == state.behaviors.end()
            || (motion->second.moveCooldownTicks <= 0 && !motion->second.pathRequestPending
                && motion->second.path.empty()
                && (motion->second.state == RuntimeBehaviorState::Complete
                    || motion->second.state == RuntimeBehaviorState::Idle
                    || (motion->second.kind == RuntimeBehaviorKind::Move
                        ? runtimeCellPointForObject(*other) == motion->second.moveTarget
                        : motion->second.kind == RuntimeBehaviorKind::None
                            || motion->second.kind == RuntimeBehaviorKind::Attack))));
        if (settled) result.settledBarrier = true;
        else result.transientBarrier = true;
    };
    const auto noteBarrier = [&](const RuntimeCellPoint& cell) {
        if ((radius(cell) >= currentRadius && (!foot || cell!=behavior.moveTarget))
            || !sameSurface(cell)) return;
        const int key = runtimeCellKey(cell.x,cell.y);
        if (!notedBarrierCells.insert(key).second) return;
        // A departure cell can be logically empty while its body is still
        // walking out. It is temporary traffic, never a sealed inner ring.
        if (runtimeGroundDepartureHoldsCell(state, object, cell))
            result.transientBarrier = true;
        if (!infantryBarriersIndexed) {
            for (const auto& member : state.world.getObjects()) {
                if (member.alive && !member.airborne && member.kind == WorldObjectKind::Infantry)
                    infantryByCell[runtimeCellKey(member.x,member.y)].push_back(&member);
            }
            for (const auto& [id,reservation] : state.subcells.reservations()) {
                if (id == object.instanceId) continue;
                const auto* member = findObjectById(state,id);
                if (!member || !member->alive || member->airborne
                    || member->kind != WorldObjectKind::Infantry) continue;
                const auto& destination = reservation.destination;
                reservedInfantryCells.emplace(runtimeCellKey(destination.x,destination.y),
                    destination.onBridge);
            }
            infantryBarriersIndexed=true;
        }
        const bool onBridge = runtimeSubcellLocationForCell(state,cell).onBridge;
        const auto at = occupied.find(key);
        if (at != occupied.end()) noteOccupant(findObjectById(state,at->second),onBridge);
        const auto infantryAt = infantryByCell.find(key);
        if (infantryAt != infantryByCell.end())
            for (const auto* member : infantryAt->second) noteOccupant(member,onBridge);
        if (reservedInfantryCells.contains({key,onBridge})) result.transientBarrier=true;
    };
    const auto noteBodyBarrier = [&](const RuntimeCellPoint& cell, const std::vector<int>& ids) {
        if (radius(cell) >= currentRadius && cell!=behavior.moveTarget) return;
        // The body oracle already matched the physical/future bridge layer.
        // Keep the original owner/motion classification without filtering it
        // again by the object's prematurely committed navigation layer.
        for (int id:ids) {
            const auto* other=findObjectById(state,id);
            if (other) noteOccupant(other,other->onBridge);
        }
    };
    struct Node { RuntimeCellPoint cell; int parent; int distance; int subCell; };
    std::vector<Node> nodes{{current, -1, 0, object.subCell}};
    std::unordered_set<int> visited{runtimeCellKey(current.x, current.y)};
    std::set<std::pair<int,int>> visitedBody{{runtimeCellKey(current.x,current.y),object.subCell}};
    int best = -1, exactBest=-1;
    std::tuple<int,int,int,int,int> bestScore{INT_MAX,INT_MAX,INT_MAX,INT_MAX,INT_MAX};
    const int searchRadius = currentRadius+1;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const Node node = nodes[index];
        const int cellRadius = radius(node.cell);
        if (foot && index>0 && node.cell==behavior.moveTarget && exactBest<0)
            exactBest=static_cast<int>(index);
        if (index > 0 && cellRadius < currentRadius
            && (behavior.pendingHasSharedGoal
                || runtimeCellDistance(node.cell,center)+0.001 < runtimeCellDistance(current,center))) {
            const int dx=node.cell.x-center.x, dy=node.cell.y-center.y;
            const auto score = std::make_tuple(std::max(cellRadius, result.idealRadius),
                node.distance, cellRadius, dx*dx+dy*dy, runtimeCellKey(node.cell.x,node.cell.y));
            if (best < 0 || score < bestScore) { best=static_cast<int>(index); bestScore=score; }
        }
        if (node.distance >= 24) { result.exhausted=false; continue; }
        WorldObjectInstance preview=object;
        if (foot && index>0) runtimeSetCrowdInfantryPreviewAtCell(state,preview,node.cell,node.subCell);
        for (const auto& next : runtimeNeighborsOf(node.cell)) {
            const bool outsideSearch=distantBodyDetour
                ? std::max(std::abs(next.x-current.x),std::abs(next.y-current.y))>8
                : radius(next)>searchRadius;
            if (outsideSearch || !sameSurface(next)) continue;
            const int key = runtimeCellKey(next.x,next.y);
            if (!foot && visited.contains(key)) continue;
            // No soft group occupancy: moving friends, diagonal corners and
            // occupied infantry subcells remain real obstacles to this route.
            int destinationSubCell=preview.subCell;
            std::vector<int> bodyBlockers;
            if (!runtimeCrowdBodyTransitionIsPassable(state, *terrain, occupied, node.cell,
                    next, preview, index==0, mode, destinationSubCell, foot?&bodyBlockers:nullptr)) {
                noteBarrier(next); noteBodyBarrier(next,bodyBlockers);
                if (next.x != node.cell.x && next.y != node.cell.y) {
                    noteBarrier({next.x,node.cell.y}); noteBarrier({node.cell.x,next.y});
                }
                continue;
            }
            if (foot && visitedBody.contains({key,destinationSubCell})) continue;
            if (nodes.size() >= 512) { result.exhausted=false; continue; }
            if (foot) visitedBody.insert({key,destinationSubCell});
            else visited.insert(key);
            nodes.push_back({next, static_cast<int>(index), node.distance+1, destinationSubCell});
        }
    }
    if (exactBest>=0) best=exactBest;
    if (best >= 0) {
        for (int index=best; nodes[index].parent>=0; index=nodes[index].parent)
            result.path.push_back(nodes[index].cell);
        std::reverse(result.path.begin(),result.path.end());
    }
    return result;
}

bool prepareRuntimeCloserSharedMoveSlot(
    RuntimeProductionHostState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior) {
    const auto search = findRuntimeCloserSharedMoveSlot(state, object, behavior);
    if (!search.applicable || search.inwardRoute) return false;
    const auto current = runtimeCellPointForObject(object);
    if (search.path.empty()) {
        // A path-queue fallback can already equal this outer cell. Wait for
        // actual passing traffic before declaring that fallback an arrival.
        if (current != behavior.moveTarget || !search.transientBarrier) return false;
        behavior.state = RuntimeBehaviorState::Blocked;
        return true;
    }
    releaseRuntimeInfantrySubcellReservation(state, object.instanceId);
    // Small commands have no shared formation center. Their body detour must
    // retain the exact goal, including a partial safe approach to a sealed ring.
    if (behavior.pendingHasSharedGoal) behavior.moveTarget = search.path.back();
    behavior.pendingPathStart = current;
    behavior.pendingPathGoal = behavior.moveTarget;
    behavior.pendingPathTowardRange = behavior.pendingAllowNearbyTarget = false;
    behavior.pendingTargetObjectId = -1; behavior.pendingRangeCells = 0.0;
    behavior.pathRequestPending = false;
    state.pendingPathObjectIds.erase(std::remove(state.pendingPathObjectIds.begin(),
        state.pendingPathObjectIds.end(),object.instanceId),state.pendingPathObjectIds.end());
    replaceRuntimeBehaviorPath(behavior,search.path);
    behavior.blockedTicks=0; behavior.nextRepathTick=state.tick;
    behavior.state=RuntimeBehaviorState::Queued;
    state.simulationStatus="group closer slot #"+std::to_string(object.instanceId);
    return true;
}

bool runtimeMoveCanFinishAtCrowdedDestination(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    if (behavior.moveCooldownTicks > 0 || object.renderMoveActive) return false;
    const RuntimeCellPoint current = runtimeCellPointForObject(object);
    const double slotDistance = runtimeCellDistance(current, behavior.moveTarget);
    const bool nearSharedGoal = behavior.pendingHasSharedGoal
        && runtimeCellDistance(current, behavior.pendingSharedGoal)
            <= runtimeSharedMoveArrivalRadius(behavior) + 0.001;
    if (current == behavior.moveTarget || (!nearSharedGoal && slotDistance > 2.25)
        || !runtimeMoveAllowsNearbyCrowdArrival(state, object, behavior)) return false;

    const int id = object.instanceId;
    const auto& occupied = runtimeTickOccupancyLookup(state);
    const auto settledAlly = [&](const WorldObjectInstance* other) {
        if (!other || !other->alive || other->instanceId==id || other->airborne || other->renderMoveActive
            || (other->kind != WorldObjectKind::Unit && other->kind != WorldObjectKind::Infantry)
            || !runtimeOwnersAreAllied(state, object.owner, other->owner)) return false;
        const auto active = state.behaviors.find(other->instanceId);
        if (active == state.behaviors.end()) return true;
        const auto& motion = active->second;
        if (motion.moveCooldownTicks > 0 || motion.pathRequestPending || !motion.path.empty()) return false;
        return motion.state == RuntimeBehaviorState::Complete
            || motion.state == RuntimeBehaviorState::Idle
            || (motion.kind == RuntimeBehaviorKind::Move
                ? runtimeCellPointForObject(*other) == motion.moveTarget
                : motion.kind == RuntimeBehaviorKind::None || motion.kind == RuntimeBehaviorKind::Attack);
    };
    const auto settledAllyAt = [&](const RuntimeCellPoint& cell) {
        const auto found = occupied.find(runtimeCellKey(cell.x, cell.y));
        return found!=occupied.end() && settledAlly(findObjectById(state,found->second));
    };
    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const auto* terrain = &state.terrainByCell;
    if (terrain->empty()) { generatedTerrain = buildRuntimeTerrainLookup(state.world); terrain = &generatedTerrain; }
    const auto mode = runtimeObjectMovementMode(state, object);
    if (mode == PathMovementMode::Air) return false;

    // Straight-line proximity across a cliff or separate water surface is not
    // arrival. Keep taking the route to the ramp/shore until on the goal's level.
    const auto here = terrain->find(runtimeCellKey(current.x, current.y));
    if (here == terrain->end() || !here->second) return false;
    const auto hereInfo = runtimeCachedTerrainInfoForCell(state, *here->second);
    const auto sameArrivalSurface = [&](const RuntimeCellPoint& cell) {
        const auto at = terrain->find(runtimeCellKey(cell.x, cell.y));
        if (at == terrain->end() || !at->second
            || runtimeEffectiveTerrainLevel(*at->second) != runtimeEffectiveTerrainLevel(*here->second)) return false;
        const auto info = runtimeCachedTerrainInfoForCell(state, *at->second);
        return info.passable && runtimeAuthoredLandTypeAllowsMover(state, info, object)
            && runtimeMapQueryPassabilityAllows(state, cell, object, mode)
            && (mode == PathMovementMode::Amphibious || hereInfo.water == info.water
                || hereInfo.shore || info.shore || hereInfo.bridge || info.bridge);
    };
    if (!sameArrivalSurface(behavior.moveTarget)) return false;

    const auto sharedSearch = findRuntimeCloserSharedMoveSlot(state, object, behavior);
    if (sharedSearch.applicable) {
        if (sharedSearch.inwardRoute || !sharedSearch.path.empty() || sharedSearch.transientBarrier) return false;
        // A genuinely sealed inner ring permits a stable outer arrival. A
        // sideways first step alone must not restart an endless orbit of it.
        if (sharedSearch.exhausted && sharedSearch.settledBarrier && !sharedSearch.foreignBarrier) return true;
    }

    // Preserve a usable route around the crowd, including a first step that
    // temporarily goes sideways. A direct approach being blocked is not enough
    // to discard an already solved route to an available destination.
    bool crowdBlocksProgress = false, bodyBarrierDeferred = false;
    const auto noteBodyBlockers = [&](const std::vector<int>& ids) {
        if (ids.empty()) return;
        for (int blocker:ids) if (!settledAlly(findObjectById(state,blocker))) {
            bodyBarrierDeferred=true; return;
        }
        crowdBlocksProgress=true;
    };
    int destinationSubCell=object.subCell;
    std::vector<int> bodyBlockers;
    if (!behavior.path.empty() && behavior.path.back() == behavior.moveTarget) {
        if (runtimeCrowdBodyTransitionIsPassable(state, *terrain, occupied, current,
                behavior.path.front(), object, true, mode, destinationSubCell,
                &bodyBlockers, true)) return false;
        noteBodyBlockers(bodyBlockers);
    }

    // A free inner slot can be sealed off by units that have already arrived.
    // Keep advancing while any closer step is available; terrain by itself or
    // a unit merely passing through is not evidence of a crowded destination.
    for (const auto& next : runtimeNeighborsOf(current)) {
        if (runtimeCellDistance(next, behavior.moveTarget) + 0.001 >= slotDistance) continue;
        if (runtimeCrowdBodyTransitionIsPassable(state, *terrain, occupied, current, next,
                object, true, mode, destinationSubCell, &bodyBlockers, true)) return false;
        noteBodyBlockers(bodyBlockers);
        if (!settledAllyAt(next) || !sameArrivalSurface(next)) continue;
        if (next.x != current.x && next.y != current.y) {
            const RuntimeCellPoint horizontal{next.x, current.y}, vertical{current.x, next.y};
            const auto clearOrAllied = [&](const RuntimeCellPoint& cell) {
                return sameArrivalSurface(cell)
                    && (!occupied.contains(runtimeCellKey(cell.x, cell.y)) || settledAllyAt(cell));
            };
            if (!clearOrAllied(horizontal) || !clearOrAllied(vertical)) continue;
        }
        crowdBlocksProgress = true;
    }
    return crowdBlocksProgress && !bodyBarrierDeferred;
}

void acceptRuntimePlayerTravelArrival(
    RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const auto current = runtimeCellPointForObject(object);
    // Attack-move and guard travel must accept this arrival too, otherwise
    // their retained exact destination immediately issues another move order.
    const auto travel = state.playerCommands.travelling.find(object.instanceId);
    if (travel != state.playerCommands.travelling.end()
        && travel->second.owner == runtimeCanonicalHumanHouseOwner(object.owner))
        travel->second.destination = {current.x, current.y};
}

bool completeRuntimeMoveNearCrowdedGoal(
    RuntimeProductionHostState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior) {
    if (!runtimeMoveCanFinishAtCrowdedDestination(state, object, behavior)) return false;
    const auto current = runtimeCellPointForObject(object);
    behavior.moveTarget = behavior.pendingPathGoal = current;
    behavior.state = RuntimeBehaviorState::Complete;
    behavior.path.clear();
    behavior.pathRequestPending = false;
    state.pendingPathObjectIds.erase(std::remove(state.pendingPathObjectIds.begin(),
        state.pendingPathObjectIds.end(),object.instanceId),state.pendingPathObjectIds.end());
    behavior.blockedTicks = 0;
    acceptRuntimePlayerTravelArrival(state, object);
    setRuntimeMissionIntent(state, object, RuntimeMissionIntent::Idle, "moveCrowdedGoalReached");
    clearRuntimeRenderPosition(object);
    ++state.actionSummary.completedCommands;
    return true;
}

std::optional<RuntimeCellPoint> runtimeGateBehaviorGoal(
    const RuntimeGameSessionState& state,
    const RuntimeObjectBehavior& behavior) {
    if (behavior.kind == RuntimeBehaviorKind::Move) {
        return behavior.moveTarget.x >= 0 && behavior.moveTarget.y >= 0
            ? std::optional<RuntimeCellPoint>(behavior.moveTarget)
            : std::nullopt;
    }
    if (behavior.kind != RuntimeBehaviorKind::Attack) {
        return std::nullopt;
    }
    if ((behavior.attackBridge || behavior.attackGround)
        && behavior.attackBridgeCell.x >= 0
        && behavior.attackBridgeCell.y >= 0) {
        return behavior.attackBridgeCell;
    }
    const WorldObjectInstance* target =
        findObjectById(state, behavior.targetObjectId);
    if (target != nullptr && target->alive) {
        return runtimeCellPointForObject(*target);
    }
    if (behavior.pendingPathGoal.x >= 0
        && behavior.pendingPathGoal.y >= 0) {
        return behavior.pendingPathGoal;
    }
    return std::nullopt;
}

double runtimeGateDistanceToFootprint(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& point,
    int gateId) {
    const WorldObjectInstance* gate = findObjectById(state, gateId);
    if (gate == nullptr) {
        return std::numeric_limits<double>::max();
    }
    return runtimeCellDistanceToObjectFootprint(state, point, *gate);
}

std::vector<RuntimeGatePassageRequest> collectRuntimeGatePassageRequests(
    const RuntimeGameSessionState& state) {
    std::set<std::pair<int, int>> uniqueRequests;
    std::vector<int> behaviorIds;
    behaviorIds.reserve(state.behaviors.size());
    for (const auto& [objectId, behavior] : state.behaviors) {
        if (behavior.kind == RuntimeBehaviorKind::Move
            || behavior.kind == RuntimeBehaviorKind::Attack) {
            behaviorIds.push_back(objectId);
        }
    }
    std::sort(behaviorIds.begin(), behaviorIds.end());

    for (int moverId : behaviorIds) {
        const auto behaviorIt = state.behaviors.find(moverId);
        const WorldObjectInstance* mover = findObjectById(state, moverId);
        if (behaviorIt == state.behaviors.end()
            || mover == nullptr
            || !runtimeGateObjectIsGroundMover(state, *mover)) {
            continue;
        }
        [[maybe_unused]] const RuntimeObjectBehavior& behavior = behaviorIt->second;
        const std::optional<RuntimeCellPoint> goal =
            runtimeGateBehaviorGoal(state, behavior);
        if (!goal.has_value()) {
            continue;
        }

        std::set<int> candidateGateIds;
        if (!behavior.path.empty()) {
            const RuntimeCellPoint& next = behavior.path.front();
            const auto indexed = state.gateIdsByCell.find(
                runtimeCellKey(next.x, next.y)
            );
            if (indexed != state.gateIdsByCell.end()) {
                candidateGateIds.insert(
                    indexed->second.begin(),
                    indexed->second.end()
                );
            }
        }

        // A mover beside a doorway must be able to wake the gate even while
        // its path is empty, pending, or cooling down after a previous hard
        // blocker result.  Do not scan remote gates: the doorway must be in
        // the immediate next-cell neighborhood and lead toward the goal.
        if (behavior.path.empty() || behavior.pathRequestPending) {
            const RuntimeCellPoint current = runtimeCellPointForObject(*mover);
            const double currentGoalDistance =
                runtimeCellDistance(current, *goal);
            for (const RuntimeGateInstanceState& gate : state.gates.gates()) {
                const double doorwayDistance = runtimeGateDistanceToFootprint(
                    state,
                    current,
                    gate.gateId
                );
                const double doorwayGoalDistance = runtimeGateDistanceToFootprint(
                    state,
                    *goal,
                    gate.gateId
                );
                if (doorwayDistance <= std::sqrt(2.0) + 0.001
                    && doorwayGoalDistance + 0.001
                        < currentGoalDistance) {
                    candidateGateIds.insert(gate.gateId);
                }
            }
        }

        for (int gateId : candidateGateIds) {
            const RuntimeGateInstanceState* gateState =
                state.gates.findGate(gateId);
            const WorldObjectInstance* gate = findObjectById(state, gateId);
            if (gateState == nullptr
                || gate == nullptr
                || !runtimeGateObjectIsLiveGate(state, *gate)
                || (gateState->phase != RuntimeGatePhase::Open
                    && !runtimeOwnersAreAllied(
                        state,
                        gate->owner,
                        mover->owner
                    ))) {
                continue;
            }
            uniqueRequests.emplace(gateId, moverId);
        }
    }

    std::vector<RuntimeGatePassageRequest> requests;
    requests.reserve(uniqueRequests.size());
    for (const auto& [gateId, moverId] : uniqueRequests) {
        requests.push_back({gateId, moverId});
    }
    return requests;
}

void wakeRuntimeGateBlockedPaths(RuntimeGameSessionState& state) {
    state.pathFailureCache.clear();
    state.pathFailureStats.activeEntries = 0;
    for (auto& [objectId, behavior] : state.behaviors) {
        (void)objectId;
        if (behavior.kind != RuntimeBehaviorKind::Move
            && behavior.kind != RuntimeBehaviorKind::Attack) {
            continue;
        }
        behavior.nextRepathTick = std::min(
            behavior.nextRepathTick,
            state.tick
        );
    }
}

bool advanceRuntimeGates(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeGates", state.tick);
    const std::uint64_t beforeTopology = state.gatesInitialized
        ? state.gateTopologyFingerprint
        : 0;
    const std::uint64_t beforeStrictOpen =
        state.gatesInitialized ? state.gateStrictOpenFingerprint : 0;
    ensureRuntimeGatesInitialized(state);
    const std::vector<RuntimeGatePassageRequest> requests =
        collectRuntimeGatePassageRequests(state);
    const std::vector<RuntimeGateAction> actions = state.gates.tick(
        runtimeGateContext(state, &requests),
        state.tick
    );
    rebuildRuntimeGateFootprintIndex(state);
    state.gateTopologyFingerprint = runtimeGateTopologyFingerprint(state);
    state.gateStrictOpenFingerprint =
        runtimeGateStrictOpenFingerprint(state);

    const bool topologyChanged =
        beforeTopology != state.gateTopologyFingerprint;
    const bool strictOpenChanged =
        beforeStrictOpen != state.gateStrictOpenFingerprint;
    bool becamePassable = false;
    for (const RuntimeGateAction& action : actions) {
        if (action.kind == RuntimeGateActionKind::BeginOpening
            || action.kind == RuntimeGateActionKind::BeginClosing) {
            if (!action.soundId.empty()) {
                if (const auto* gate = findObjectById(state, action.gateId))
                    emitRuntimeSound(state, "sound:" + action.soundId, runtimeSpatialAudioPositionForObject(*gate));
            }
        }
        becamePassable = becamePassable
            || action.kind == RuntimeGateActionKind::FullyOpen;
    }
    if (topologyChanged || strictOpenChanged) {
        state.pathTerrainCache.dynamicValid = false;
        state.pathTerrainCache.dynamicCells.clear();
        state.attackApproachCache.clear();
    }
    if (becamePassable) {
        wakeRuntimeGateBlockedPaths(state);
    }
    const bool transitionActive = std::any_of(
        state.gates.gates().begin(),
        state.gates.gates().end(),
        [](const RuntimeGateInstanceState& gate) {
            return gate.phase == RuntimeGatePhase::Opening
                || gate.phase == RuntimeGatePhase::Closing;
        }
    );
    return topologyChanged
        || strictOpenChanged
        || !actions.empty()
        || transitionActive;
}

}
