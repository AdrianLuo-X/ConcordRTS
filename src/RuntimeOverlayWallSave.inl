// Optional map-wall authority; included before shared terrain projection/save writers.
// UI virtual targets and impact topology use the shared wall combat authority.
void refreshRuntimeOverlayWallTopology(RuntimeWindowState&);

using RuntimeOriginalOverlayWalls = std::map<int, TerrainCell>;

bool buildRuntimeOriginalOverlayWalls(const RuntimeWindowState& state,
    RuntimeOriginalOverlayWalls& originals, std::string& error) {
    originals.clear();
    std::unordered_map<int, const MapOverlayCell*> pack;
    for (const auto& source : state.world.getOverlayCells())
        pack.emplace(runtimeCellKey(source.x, source.y), &source);
    for (const auto& live : state.world.getTerrainCells()) {
        const auto found = pack.find(runtimeCellKey(live.x, live.y));
        const MapOverlayCell* source = found == pack.end() ? nullptr : found->second;
        if (!source) {
            const auto* fact = runtimeMapQueryCell(state, {live.x, live.y});
            if (fact && fact->hasOverlay) source = &fact->overlay;
        }
        if (!source) continue;
        TerrainCell original = live;
        original.hasOverlay = true;
        original.overlayIndex = source->overlayIndex;
        original.overlayFrame = source->frameIndex;
        original.overlayType = source->type;
        original.overlayImage = source->image;
        original.overlayIsOre = source->isOre;
        original.overlayIsGem = source->isGem;
        initializeWorldOverlayWallState(original, state.rules);
        if (original.overlayWall.health < 0) continue;
        if (live.x < 0 || live.y < 0 || live.x >= 512 || live.y >= 512
            || original.overlayWall.originalFrame < 0
            || original.overlayWall.originalFrame > 255
            || !originals.emplace(runtimeCellKey(live.x, live.y), std::move(original)).second) {
            error = "invalid original map-wall identity";
            return false;
        }
    }
    return true;
}

bool runtimeOverlayWallIdentityMatches(const OverlayWallState& value,
    const OverlayWallState& original) {
    return value.originalIndex == original.originalIndex
        && value.originalFrame == original.originalFrame
        && toUpperAsciiLocal(value.originalType) == toUpperAsciiLocal(original.originalType)
        && toUpperAsciiLocal(value.originalImage) == toUpperAsciiLocal(original.originalImage);
}

bool runtimeTerrainShowsOriginalOverlayWall(const TerrainCell& cell,
    const OverlayWallState& wall) {
    return cell.hasOverlay && cell.overlayIndex == wall.originalIndex
        && (cell.overlayType.empty()
            || toUpperAsciiLocal(cell.overlayType) == toUpperAsciiLocal(wall.originalType));
}

void clearRuntimeOverlayWallSprite(TerrainCell& cell) {
    cell.hasOverlay = false;
    cell.overlayIndex = -1;
    cell.overlayFrame = 0;
    cell.overlayType.clear();
    cell.overlayImage.clear();
    cell.overlayIsOre = cell.overlayIsGem = false;
}

bool resetRuntimeOverlayWallsForSavedProjection(RuntimeWindowState& state,
    std::string& error) {
    RuntimeOriginalOverlayWalls originals;
    if (!buildRuntimeOriginalOverlayWalls(state, originals, error)) return false;
    for (auto& cell : state.world.accessTerrainCells()) {
        if (cell.overlayWall.health >= 0
            && runtimeTerrainShowsOriginalOverlayWall(cell, cell.overlayWall))
            clearRuntimeOverlayWallSprite(cell);
        cell.overlayWall = {};
        const auto found = originals.find(runtimeCellKey(cell.x, cell.y));
        if (found == originals.end()) continue;
        const auto& original = found->second;
        cell.overlayWall = original.overlayWall;
        cell.hasOverlay = true;
        cell.overlayIndex = original.overlayIndex;
        cell.overlayFrame = original.overlayFrame;
        cell.overlayType = original.overlayType;
        cell.overlayImage = original.overlayImage;
        cell.overlayIsOre = cell.overlayIsGem = false;
    }
    return true;
}

bool restoreRuntimeOverlayWallStates(RuntimeWindowState& state, const IniFile& ini,
    std::string& error) {
    if (!ini.getSection("WallOverlays")) return true;
    const auto fail = [&] { error = "invalid saved map-wall authority"; return false; };
    RuntimeOriginalOverlayWalls originals;
    if (!buildRuntimeOriginalOverlayWalls(state, originals, error)) return false;
    const auto* section = ini.getSection("WallOverlays");
    std::unordered_set<std::string> keys;
    for (const auto& entry : *section)
        if (!keys.insert(toUpperAsciiLocal(trimAsciiLocal(entry.first))).second) return fail();
    int schema = -1, count = -1;
    if (!parseRuntimeInt(ini.getValue("WallOverlays", "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue("WallOverlays", "Count", ""), count)
        || count < 0 || static_cast<std::size_t>(count) != originals.size()
        || section->size() != static_cast<std::size_t>(count) + 2U) return fail();
    for (const auto& entry : *section) {
        const auto key = toUpperAsciiLocal(trimAsciiLocal(entry.first));
        if (key == "SCHEMA" || key == "COUNT") continue;
        int index = -1;
        if (!parseRuntimeInt(key, index) || index < 0 || index >= count
            || std::to_string(index) != key) return fail();
    }
    std::unordered_map<int, int> healthByCell;
    std::unordered_map<int, int> currentFrameByCell;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("WallOverlays", std::to_string(i), ""));
        int x = -1, y = -1, ordinal = -1, frame = -1, health = -1, currentFrame = -1;
        if (fields.size() != (schema == 1 ? 7U : 8U)
            || !parseRuntimeInt(fields[0], x) || !parseRuntimeInt(fields[1], y)
            || x < 0 || y < 0 || x >= 512 || y >= 512
            || !parseRuntimeInt(fields[2], ordinal) || !parseRuntimeInt(fields[5], frame)
            || !parseRuntimeInt(fields[6], health)
            || (schema == 2 && (!parseRuntimeInt(fields[7], currentFrame)
                || (health == 0 ? currentFrame != -1 : (currentFrame < 0 || currentFrame > 255))))) return fail();
        const auto original = originals.find(runtimeCellKey(x, y));
        if (original == originals.end()) return fail();
        const auto& wall = original->second.overlayWall;
        const auto* building = state.rules.findBuildingType(wall.originalType);
        if (ordinal != wall.originalIndex || frame != wall.originalFrame
            || toUpperAsciiLocal(fields[3]) != toUpperAsciiLocal(wall.originalType)
            || toUpperAsciiLocal(fields[4]) != toUpperAsciiLocal(wall.originalImage)
            || !building || !building->wall || building->strength <= 0
            || health < 0 || health > building->strength
            || !healthByCell.emplace(runtimeCellKey(x, y), health).second) return fail();
        if (schema == 2) currentFrameByCell.emplace(runtimeCellKey(x, y), currentFrame);
    }
    // All rows and schema-specific frames were validated against immutable
    // identity before applying any HP or current artwork.
    for (auto& cell : state.world.accessTerrainCells()) {
        const auto saved = healthByCell.find(runtimeCellKey(cell.x, cell.y));
        if (saved == healthByCell.end()) continue;
        const auto& original = originals.at(saved->first);
        if (saved->second > 0 && cell.hasOverlay
            && !runtimeTerrainShowsOriginalOverlayWall(cell, original.overlayWall)) return fail();
    }
    for (auto& cell : state.world.accessTerrainCells()) {
        const auto saved = healthByCell.find(runtimeCellKey(cell.x, cell.y));
        if (saved == healthByCell.end()) continue;
        const auto& original = originals.at(saved->first);
        cell.overlayWall = original.overlayWall;
        cell.overlayWall.health = saved->second;
        if (saved->second == 0) {
            if (runtimeTerrainShowsOriginalOverlayWall(cell, cell.overlayWall))
                clearRuntimeOverlayWallSprite(cell);
        } else {
            cell.hasOverlay = true;
            cell.overlayIndex = cell.overlayWall.originalIndex;
            cell.overlayType = cell.overlayWall.originalType;
            cell.overlayImage = cell.overlayWall.originalImage;
            cell.overlayIsOre = cell.overlayIsGem = false;
            if (schema == 2) cell.overlayFrame = currentFrameByCell.at(saved->first);
        }
    }
    // Schema1 retains its established reconstruction. Schema2 records current
    // artwork exactly; loading must not rewrite saved visible pixels.
    if (schema == 1) refreshRuntimeOverlayWallTopology(state);
    return true;
}

bool writeRuntimeOverlayWallStates(const RuntimeWindowState& state,
    std::ostream& output, std::string& error) {
    RuntimeOriginalOverlayWalls originals;
    if (!buildRuntimeOriginalOverlayWalls(state, originals, error)) return false;
    std::map<int, const TerrainCell*> current;
    for (const auto& cell : state.world.getTerrainCells()) {
        if (cell.overlayWall.health < 0) continue;
        if (!originals.contains(runtimeCellKey(cell.x, cell.y))
            || !current.emplace(runtimeCellKey(cell.x, cell.y), &cell).second) {
            error = "map-wall save lacks original identity";
            return false;
        }
    }
    if (current.size() != originals.size()) {
        error = "map-wall save is missing initialized wall authority";
        return false;
    }
    for (const auto& [key, original] : originals) {
        const auto& cell = *current.at(key);
        const auto* building = state.rules.findBuildingType(original.overlayWall.originalType);
        if (!runtimeOverlayWallIdentityMatches(cell.overlayWall, original.overlayWall)
            || !building || !building->wall || building->strength <= 0
            || cell.overlayWall.health < 0 || cell.overlayWall.health > building->strength
            || (cell.overlayWall.health > 0
                && (!runtimeTerrainShowsOriginalOverlayWall(cell, cell.overlayWall)
                    || cell.overlayFrame < 0 || cell.overlayFrame > 255))
            || (cell.overlayWall.health == 0
                && runtimeTerrainShowsOriginalOverlayWall(cell, cell.overlayWall))) {
            error = "invalid live map-wall save projection";
            return false;
        }
    }
    output << "\n[WallOverlays]\nSchema=2\nCount=" << current.size() << '\n';
    std::size_t index = 0;
    for (const auto& [key, ptr] : current) {
        (void)key;
        const auto& cell = *ptr;
        const auto& wall = cell.overlayWall;
        output << index++ << '=' << cell.x << '|' << cell.y << '|'
            << wall.originalIndex << '|' << runtimeSaveField(wall.originalType) << '|'
            << runtimeSaveField(wall.originalImage) << '|' << wall.originalFrame << '|'
            << wall.health << '|' << (wall.health > 0 ? cell.overlayFrame : -1) << '\n';
    }
    return true;
}
