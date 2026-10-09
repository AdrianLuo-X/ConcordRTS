// Issue 15. Expected floor geometry comes from retail CellClass::GetFloorHeight
// (gamemd 0x47B3A0 / plane initialization), not the renderer's ramp/pose helpers.
namespace RuntimeVehicleSlopeRenderOracle {
using Bytes = std::vector<unsigned char>;
constexpr double pi = 3.14159265358979323846;
constexpr std::uint32_t background = 0x003c464cU;
constexpr int width = 192, height = 160;

struct Tile {
    int tile = -1, sub = -1, ramp = -1, authoredHeight = 0, land = -1;
    std::string name, source;
    Bytes bytes;
};
struct Vehicle {
    std::string id, image, group;
    int strength = 0;
    bool tilter = true, voxel = false;
};
struct Section {
    RuntimeHvaTransform transform;
    std::array<double, 3> low{}, high{};
};
struct Model {
    std::string image, source;
    RuntimeVoxelModelBounds bounds;
    std::vector<Section> sections;
};

std::uint32_t u32(const Bytes& bytes, std::size_t at) {
    if (at + 4 > bytes.size()) throw std::runtime_error("original resource integer outside file");
    return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1]) << 8)
        | (std::uint32_t(bytes[at + 2]) << 16) | (std::uint32_t(bytes[at + 3]) << 24);
}
double f32(const Bytes& bytes, std::size_t at) {
    const auto bits = u32(bytes, at);
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) throw std::runtime_error("nonfinite original VXL/HVA value");
    return value;
}
bool yes(std::string text) {
    text = toLowerAscii(text);
    return text == "yes" || text == "true" || text == "1";
}
std::uint64_t hash(const std::vector<std::uint32_t>& pixels) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto pixel : pixels) for (int byte = 0; byte < 4; ++byte) {
        value ^= (pixel >> (byte * 8)) & 255U; value *= 1099511628211ULL;
    }
    return value;
}
std::string quoted(const std::string& text) {
    std::string result = "\"";
    for (const auto c : text) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}
bool preserve(const std::filesystem::path& root, const std::string& name, const Bytes& bytes) {
    if (root.empty()) return true;
    const auto path = root / "native-original" / name;
    if (std::filesystem::exists(path)) return false;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bool(file);
}

// Each row is retail's (a,b,c,ceiling,offset), expressed in height levels.
// Retail adds the extra term before clamping a*u+b*v+c+extra. 17..20 are
// constant half-height, despite the different visual diagonal TMP shapes.
double floor(int ramp, double x, double y) {
    static constexpr double planes[21][5] = {
        {0,0,0,0,0}, {1,0,0,1,0}, {0,1,0,1,0}, {-1,0,1,1,0}, {0,-1,1,1,0},
        {1,1,-1,1,0}, {-1,1,0,1,0}, {-1,-1,1,1,0}, {1,-1,0,1,0},
        {1,1,0,1,0}, {-1,1,1,1,0}, {-1,-1,2,1,0}, {1,-1,1,1,0},
        {1,1,0,2,0}, {-1,1,1,2,0}, {-1,-1,2,2,0}, {1,-1,1,2,0},
        {0,0,0,.5,.5}, {0,0,1,.5,-.5}, {0,0,0,.5,.5}, {0,0,1,.5,-.5}
    };
    if (ramp < 0 || ramp > 20) throw std::runtime_error("unexpected original ramp id");
    const auto& p = planes[ramp];
    return std::clamp(p[0] * (x + .5) + p[1] * (y + .5) + p[2] + p[4], 0.0, p[3]);
}

bool readTiles(const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    const std::filesystem::path& output, std::array<Tile, 21>& ramps, Tile& flat,
    Tile& water, std::string& error) {
    const int rampSet = context.theater->getGeneralTileSetIndex("RampBase");
    const int clearSet = context.theater->getGeneralTileSetIndex("ClearTile");
    const int waterSet = context.theater->getGeneralTileSetIndex("WaterSet");
    const auto& sets = context.theater->getTileSets();
    for (const auto& set : sets) {
        if (set.index != rampSet && set.index != clearSet && set.index != waterSet) continue;
        for (int index = 0; index < set.tilesInSet; ++index) {
            const int tileIndex = set.startTileIndex + index;
            const auto reference = context.theater->resolveTileIndex(tileIndex, context.tileRootPath);
            Bytes bytes; std::string source;
            if (!loadRuntimeResourceBytesByName({context.tileRootPath, context.assetRootPath},
                context.assetRootPath, reference.expectedFileName, cache, bytes, source, error)) return false;
            const int gx = int(u32(bytes, 0)), gy = int(u32(bytes, 4));
            if (gx <= 0 || gy <= 0 || gx > 16 || gy > 16
                || bytes.size() < 16 + std::size_t(gx * gy) * 4) {
                error = "original standard TMP grid: " + reference.expectedFileName; return false;
            }
            for (int sub = 0; sub < gx * gy; ++sub) {
                const auto at = u32(bytes, 16 + std::size_t(sub) * 4);
                if (!at) continue;
                if (at + 52 > bytes.size()) { error = "original TMP subtile truncated"; return false; }
                Tile value{tileIndex, sub, int(bytes[at + 42]), int(bytes[at + 40]),
                    int(bytes[at + 41]), reference.expectedFileName, source, bytes};
                if (set.index == rampSet && value.ramp > 0 && value.ramp <= 20
                    && ramps[value.ramp].tile < 0) ramps[value.ramp] = value;
                if (set.index == clearSet && value.ramp == 0 && flat.tile < 0) flat = value;
                // Retail's TMP +0x29 type 9 maps to Water (land-type 2).
                if (set.index == waterSet && value.land == 9 && water.tile < 0) water = value;
            }
            if (set.index != rampSet && ((set.index == clearSet && flat.tile >= 0)
                || (set.index == waterSet && water.tile >= 0))) break;
        }
    }
    if (flat.tile < 0 || water.tile < 0) { error = "original clear/water TMP controls absent"; return false; }
    std::set<std::string> written;
    for (int ramp = 1; ramp <= 20; ++ramp) {
        if (ramps[ramp].tile < 0 || ramps[ramp].land == 9) {
            error = "original grass ramp missing: " + std::to_string(ramp); return false;
        }
        if (written.insert(ramps[ramp].name).second
            && !preserve(output, ramps[ramp].name, ramps[ramp].bytes)) {
            error = "original TMP evidence destination exists or write failed"; return false;
        }
    }
    return true;
}

std::vector<Vehicle> inventory(const IniFile& raw, const IniFile& art) {
    std::vector<Vehicle> result; std::set<std::string> seen;
    const auto* registered = raw.getSection("VehicleTypes");
    if (!registered) return result;
    for (const auto& [index, id] : *registered) {
        if (!seen.insert(toLowerAscii(id)).second) continue;
        Vehicle v; v.id = id; v.image = raw.getValue(id, "Image", id);
        v.strength = std::stoi(raw.getValue(id, "Strength", "0"));
        v.tilter = yes(raw.getValue(id, "IsTilter", "yes"));
        v.voxel = yes(art.getValue(v.image, "Voxel", "no"));
        const auto movement = toLowerAscii(raw.getValue(id, "MovementZone", ""));
        const auto speed = toLowerAscii(raw.getValue(id, "SpeedType", ""));
        const auto locomotor = toLowerAscii(raw.getValue(id, "Locomotor", ""));
        if (!raw.getSection(id)) v.group = "missing-section";
        else if (v.strength <= 0) v.group = "no-vxl";
        else if (yes(raw.getValue(id, "JumpJet", "no"))
            || yes(raw.getValue(id, "ConsideredAircraft", "no"))
            || locomotor == "{92612c46-f71f-11d1-ac9f-006008055bb5}"
            || movement.find("fly") != std::string::npos || speed.find("winged") != std::string::npos)
            v.group = "air";
        else if (yes(raw.getValue(id, "Naval", "no"))
            && movement.find("amphib") == std::string::npos && movement.find("hover") == std::string::npos
            && speed.find("hover") == std::string::npos) v.group = "naval";
        else if (!v.tilter) v.group = "is-tilter-no";
        else v.group = v.voxel ? "land-vxl" : "no-vxl";
        result.push_back(v);
    }
    return result;
}

GameWorld world(const Tile& tile) {
    GameWorld result;
    for (int y = 10; y <= 14; ++y) for (int x = 10; x <= 14; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = tile.tile; cell.subTileIndex = tile.sub; cell.level = 3;
        result.accessTerrainCells().push_back(cell);
    }
    return result;
}
WorldObjectInstance object(const Vehicle& type, int facing, int ramp) {
    WorldObjectInstance value; value.type = type.id; value.instanceId = 1;
    value.kind = WorldObjectKind::Unit; value.owner = "Americans";
    value.alive = true; value.health = type.strength;
    value.x = value.y = 12; value.level = 3;
    value.hasRenderPosition = true; value.renderX = 12.125; value.renderY = 11.875;
    value.renderLevel = 3 + floor(ramp, .125, -.125);
    value.facing = facing; value.turretFacing = (facing + 96) % 256;
    value.mission = "Guard";
    return value;
}
std::array<double, 3> transform(const RuntimeHvaTransform& m, const std::array<double, 3>& p) {
    std::array<double, 3> result{};
    for (int row = 0; row < 3; ++row)
        result[row] = m.m[row * 4] * p[0] + m.m[row * 4 + 1] * p[1]
            + m.m[row * 4 + 2] * p[2] + m.m[row * 4 + 3];
    return result;
}

bool model(const std::string& image, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, const std::filesystem::path& output,
    std::set<std::string>& written, Model& result, std::string& error) {
    const auto& source = loadRuntimeVoxelSourceFromCache(image, context, cache, false);
    if (!source.loaded) { error = "original model " + image + ": " + source.errorMessage; return false; }
    const auto& vxl = source.vxlBytes; const auto& hva = source.hvaBytes;
    if (vxl.size() < 802 || hva.size() < 24) { error = "original model header truncated"; return false; }
    const auto count = u32(vxl, 20), hvaCount = u32(hva, 20), hvaFrames = u32(hva, 16);
    const std::size_t footer = 802 + std::size_t(count) * 28 + u32(vxl, 28);
    if (!count || count > 1024 || !hvaFrames || !hvaCount || hvaCount > 1024
        || footer + std::size_t(count) * 92 > vxl.size()
        || 24 + std::size_t(hvaCount) * 16 + std::size_t(hvaCount) * 48 > hva.size()) {
        error = "original VXL/HVA complete-section budget " + image; return false;
    }
    result = {}; result.image = image; result.source = source.metadata.vxlPath;
    result.bounds.valid = true;
    result.bounds.minimum.fill(std::numeric_limits<double>::infinity());
    result.bounds.maximum.fill(-std::numeric_limits<double>::infinity());
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto at = footer + std::size_t(index) * 92;
        Section s;
        double multiplier = f32(vxl, at + 12); if (multiplier == 0) multiplier = 1;
        s.transform.valid = true;
        for (int m = 0; m < 12; ++m)
            s.transform.m[m] = index < hvaCount
                ? f32(hva, 24 + std::size_t(hvaCount) * 16 + std::size_t(index) * 48 + std::size_t(m) * 4)
                : f32(vxl, at + 16 + std::size_t(m) * 4);
        for (int axis = 0; axis < 3; ++axis) {
            s.low[axis] = f32(vxl, at + 64 + axis * 4);
            s.high[axis] = f32(vxl, at + 76 + axis * 4);
            const int dimension = vxl[at + 88 + axis];
            if (dimension <= 0) { error = "empty original VXL section " + image; return false; }
            if (index < hvaCount)
                s.transform.m[axis * 4 + 3] *= multiplier * (s.high[axis] - s.low[axis]) / dimension;
        }
        for (int corner = 0; corner < 8; ++corner) {
            const auto p = transform(s.transform, {corner & 1 ? s.high[0] : s.low[0],
                corner & 2 ? s.high[1] : s.low[1], corner & 4 ? s.high[2] : s.low[2]});
            for (int axis = 0; axis < 3; ++axis) {
                result.bounds.minimum[axis] = std::min(result.bounds.minimum[axis], p[axis]);
                result.bounds.maximum[axis] = std::max(result.bounds.maximum[axis], p[axis]);
            }
        }
        result.sections.push_back(s);
    }
    if (written.insert(toLowerAscii(image)).second) {
        if (!preserve(output, image + ".vxl", vxl) || !preserve(output, image + ".hva", hva)) {
            error = "original VXL/HVA evidence destination exists or write failed"; return false;
        }
    }
    return true;
}

// Explicit orthonormal world rotation, independent of rotation.rotate().
std::array<double, 3> rotated(const std::array<double, 3>& point, int pitch, int roll) {
    const double p = pitch * pi / 90, r = roll * pi / 90;
    const double x = std::cos(p) * point[0] + std::sin(p) * point[2];
    const double z = -std::sin(p) * point[0] + std::cos(p) * point[2];
    return {x, std::cos(r) * point[1] - std::sin(r) * z,
        std::sin(r) * point[1] + std::cos(r) * z};
}
bool sectionGeometry(const Model& original, const RuntimeVoxelBodyRotation& actual,
    int facing, bool originPivot, std::size_t& checked, std::string& error) {
    const double yaw = (facingBucketForFacingValue(facing, 32) - 8) * 2 * pi / 32;
    const double c = std::cos(yaw), s = std::sin(yaw);
    const double px = originPivot ? 0 : (original.bounds.minimum[0] + original.bounds.maximum[0]) / 2;
    const double py = originPivot ? 0 : (original.bounds.minimum[1] + original.bounds.maximum[1]) / 2;
    for (const auto& section : original.sections) {
        const auto matrix = runtimeTiltVoxelSection(&section.transform, actual, c, s, px, py);
        for (int corner = 0; corner < 8; ++corner) {
            const std::array<double, 3> local{corner & 1 ? section.high[0] : section.low[0],
                corner & 2 ? section.high[1] : section.low[1], corner & 4 ? section.high[2] : section.low[2]};
            const auto old = transform(section.transform, local);
            const auto turn = rotated({c * (old[0] - px) - s * (old[1] - py),
                s * (old[0] - px) + c * (old[1] - py), old[2] - actual.floorZ}, actual.pitch, actual.roll);
            const std::array<double, 3> expected{c * turn[0] + s * turn[1] + px,
                -s * turn[0] + c * turn[1] + py, turn[2] + actual.floorZ};
            const auto painted = transform(matrix, local);
            for (int axis = 0; axis < 3; ++axis) if (std::abs(painted[axis] - expected[axis]) > 1e-8) {
                error = "original complete section world-axis transform " + original.image; return false;
            }
            ++checked;
        }
    }
    return true;
}
double support(const Model& body, int facing, bool originPivot,
    const RuntimeVoxelBodyRotation& rotation, double gx, double gy) {
    const double yaw = (facingBucketForFacingValue(facing, 32) - 8) * 2 * pi / 32;
    const double c = std::cos(yaw), s = std::sin(yaw);
    const double px = originPivot ? 0 : (body.bounds.minimum[0] + body.bounds.maximum[0]) / 2;
    const double py = originPivot ? 0 : (body.bounds.minimum[1] + body.bounds.maximum[1]) / 2;
    double lower = 0;
    for (double x : {body.bounds.minimum[0] - px, body.bounds.maximum[0] - px})
        for (double y : {body.bounds.minimum[1] - py, body.bounds.maximum[1] - py}) {
            const auto p = rotated({c * x - s * y, s * x + c * y, 0}, rotation.pitch, rotation.roll);
            lower = std::min(lower, p[2] - gx * p[0] - gy * p[1]);
        }
    return -lower;
}
void coldRaster(RuntimeMapRenderCache& cache) {
    cache.voxelObjectsByType.clear(); cache.stableVoxelCacheKeyByLookup.clear();
    cache.vehicleBodyPoseUses.clear(); cache.vehicleBodyPoseCacheEntries = 0;
}
bool draw(GameWorld& world, const WorldObjectInstance& object, RuntimeMapRenderContext probe,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    std::vector<std::uint32_t>& pixels, RuntimeVoxelDrawResult& result) {
    std::fill(pixels.begin(), pixels.end(), background);
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    const RECT viewport{0,0,width,height};
    RuntimeGeometryQueryScope geometry(world, viewport, probe, camera, cache);
    const auto center = runtimeObjectBodyAnchorForRender(object, viewport, probe, camera);
    return tryDrawVoxelSprite(nullptr, viewport, object, center, probe, camera, cache, &result);
}
bool reference(const WorldObjectInstance& object, RuntimeMapRenderContext probe,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, const Model& original,
    const Model& turret, const Model& barrel,
    double fit, const RuntimeVoxelBodyRotation& rotation, double lift,
    std::vector<std::uint32_t>& pixels, RuntimeVoxelDrawResult& result) {
    std::fill(pixels.begin(), pixels.end(), background);
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    const RECT viewport{0,0,width,height};
    const auto center = runtimeObjectBodyAnchorForRender(object, viewport, probe, camera);
    POINT paint = runtimeVoxelModelDrawCenter(center, camera);
    const auto sizing = runtimeVxlRasterSizingForOwner(object.type);
    paint.y -= LONG(std::lround(lift * fit * runtimeVxlProjectionParamsForSizing(sizing).heightYScale));
    auto* sprite = loadRuntimeVoxelImageFromCache(original.image, object.type,
        facingBucketForFacingValue(object.facing, 32), probe, cache, fit, 0, 0, 0, &rotation);
    const auto* palette = runtimeVoxelPaletteForRender(probe, cache);
    if (!sprite || !sprite->hasSprite || !palette) return false;
    result.bodyDrawn = drawRuntimeShpFrame(nullptr, viewport, paint, probe, camera, sprite->image, *palette,
        runtimeObjectShouldOwnerRemap(object, probe), runtimeOwnerRemapColor(probe, object.owner),
        cache.voxelPaletteLookupLoaded ? 0 : 8, !cache.voxelPaletteLookupLoaded,
        cache.voxelPaletteLookupLoaded, runtimeObjectSpriteOpacity(object));
    // Start with the preexisting authored mount. Rotate/unproject/project it
    // independently instead of calling the new posed turret/barrel helpers.
    POINT mount = runtimeVoxelTurretOffset(object, probe, fit);
    if (rotation.pitch || rotation.roll) {
        const auto axes = runtimeVxlProjectionAxes();
        const double sum = mount.y / (axes[1] * fit), difference = mount.x / (axes[0] * fit);
        const auto moved = rotated({(sum + difference) / 2, (sum - difference) / 2, 0}, rotation.pitch, rotation.roll);
        mount = {LONG(std::lround((moved[0] - moved[1]) * axes[0] * fit)),
            LONG(std::lround(((moved[0] + moved[1]) * axes[1] - moved[2] * axes[2]) * fit))};
    }
    const auto part = [&](const Model& source, bool isBarrel, RuntimeVoxelSpriteDrawInfo& info) {
        if (source.sections.empty()) return false;
        POINT at{paint.x + mount.x + runtimeVxlPixelOffset(isBarrel
            ? "CONCORD_VXL_BARREL_PIXEL_OFFSET_X" : "CONCORD_VXL_TURRET_PIXEL_OFFSET_X"),
            paint.y + mount.y + runtimeVxlPixelOffset(isBarrel
            ? "CONCORD_VXL_BARREL_PIXEL_OFFSET_Y" : "CONCORD_VXL_TURRET_PIXEL_OFFSET_Y")};
        const double partScale = runtimeVxlSharedPartScaleEnabled()
            ? runtimeVoxelPartFitScale(fit, isBarrel ? runtimeVxlBarrelRelativeScale() : runtimeVxlTurretRelativeScale()) : 0;
        return drawRuntimeVoxelImageSprite(nullptr, viewport, at, probe, camera, cache, source.image,
            object.type + (isBarrel ? "|barrel" : "|turret"), object.turretFacing, 0, 0, 0,
            runtimeOwnerHasRendererRemapColor(probe, object.owner)
                && runtimeImageShouldOwnerRemap(probe, source.image, findArtTypeForObject(object, probe)),
            runtimeOwnerRemapColor(probe, object.owner), &info, partScale, 255, 0, &rotation);
    };
    result.turretDrawn = part(turret, false, result.turretInfo);
    result.barrelDrawn = part(barrel, true, result.barrelInfo);
    return result.bodyDrawn;
}
} // namespace RuntimeVehicleSlopeRenderOracle

bool runRuntimeVehicleTerrainSlopeRenderContract(const RuntimeMapRenderContext& context, std::string& error) {
    using namespace RuntimeVehicleSlopeRenderOracle;
    const auto fail = [&](const std::string& reason) { error = "vehicle terrain slope render: " + reason; return false; };
    if (!context.theater || !context.art || !context.rules) return fail("stock context absent");
    try {
        IniFile raw, art;
        if (!raw.loadFromFile("local_data/rulesmd.ini") || !art.loadFromFile("local_data/artmd.ini"))
            return fail("independent original rules/art files unavailable");
        const auto types = inventory(raw, art);
        int land = 0, noTilt = 0, air = 0, naval = 0, dummy = 0, missing = 0;
        for (const auto& type : types) {
            land += type.group == "land-vxl"; noTilt += type.group == "is-tilter-no";
            air += type.group == "air"; naval += type.group == "naval";
            dummy += type.group == "no-vxl"; missing += type.group == "missing-section";
            if (type.group != "missing-section") {
                const auto* typed = context.rules->findUnitType("Vehicle", type.id);
                if (!typed || typed->strength != type.strength || typed->isTilter != type.tilter)
                    return fail("original typed IsTilter/Strength identity: " + type.id);
            }
        }
        if (types.size() != 80 || land != 57 || noTilt != 2 || air != 6 || naval != 13 || dummy != 1 || missing != 1)
            return fail("independent stock inventory counts");
        auto probe = context; probe.vision = nullptr; probe.mapFacts = nullptr;
        probe.impactActors = nullptr; probe.specialMechanismSnapshots = nullptr; probe.wrecks = nullptr;
        probe.buildingCollapses = nullptr; probe.stableRenderConfiguration = true;
        probe.shadowsEnabled = false; probe.effectOnlyProbe = false; probe.animationTick = 200;
        std::filesystem::path output;
        if (const char* path = std::getenv("CONCORD_VEHICLE_TERRAIN_SLOPE_OUTPUT")) if (*path) output = path;
        std::ofstream rows, assets, controls;
        if (!output.empty()) {
            std::filesystem::create_directories(output / "native-original");
            for (const auto* name : {"render_contract.csv", "render_assets.csv", "render_controls.csv"})
                if (std::filesystem::exists(output / name)) return fail("evidence file already exists: " + std::string(name));
            rows.open(output / "render_contract.csv"); assets.open(output / "render_assets.csv"); controls.open(output / "render_controls.csv");
            if (!rows || !assets || !controls) return fail("evidence CSV output");
            rows << "type,ramp,tile,sub,facing,turret_facing,raw_height,pitch,roll,expected_pitch,expected_roll,"
                "pitch_bin,roll_bin,plane_support,flat_support,body_fit,body_parts,turret_parts,barrel_parts,"
                "opaque_pixels,cold_hash,warm_hash,pan_hash,reference_hash,result\n";
            assets << "kind,id,image,source,sections,result\n";
            controls << "type,control,ramp,terrain_supported,pitch,roll,result\n";
            rows << std::setprecision(17); controls << std::setprecision(17);
        }
        auto actualCache = std::make_unique<RuntimeMapRenderCache>();
        auto referenceCache = std::make_unique<RuntimeMapRenderCache>();
        std::array<Tile,21> ramps; Tile flat, water; std::string why;
        if (!readTiles(probe, *referenceCache, output, ramps, flat, water, why)) return fail(why);
        if (assets) for (int r = 1; r <= 20; ++r)
            assets << "TMP," << r << ',' << quoted(ramps[r].name) << ',' << quoted(ramps[r].source) << ",1,PASS\n";
        std::vector<std::uint32_t> cold(width * height), warm(cold.size()), panned(cold.size()), expected(cold.size());
        std::set<std::string> writtenModels;
        std::size_t combinations = 0, cornerChecks = 0, controlChecks = 0, pixelChecks = 0;
        std::array<std::vector<std::uint32_t>,3> atlases;
        for (auto& atlas : atlases) atlas.assign(width * 5 * height * 4, background);
        const std::array<std::string,3> atlasTypes{"MTNK", "SAPC", "TELE"};
        const std::array<int,5> atlasRamps{1,2,5,13,17};
        for (const auto& type : types) {
            if (type.group == "missing-section" || type.group == "no-vxl") continue;
            auto flatWorld = world(flat);
            if (type.group != "land-vxl" && !(type.group == "is-tilter-no" && type.voxel)) {
                auto controlWorld = world(ramps[1]); auto obj = object(type, 32, 1);
                const auto pose = resolveRuntimeVehicleBodyPose(controlWorld, obj, probe, *actualCache);
                if (pose.terrainSupported || pose.terrainPitch != 0 || pose.terrainRoll != 0)
                    return fail("excluded original type tilts: " + type.id);
                if (controls) controls << type.id << ",original-" << type.group << ",1,0,0,0,PASS\n";
                ++controlChecks; continue;
            }
            for (int face = 0; face < 8; ++face) {
                auto base = object(type, face * 32, 0);
                flatWorld.accessObjects() = {base};
                RuntimeMapCamera camera; camera.initialized = true;
                const auto origin = runtimeObjectDrawPoint(base, {}, camera);
                camera.worldOffsetX = int(std::lround(origin.x)) - width / 2;
                camera.worldOffsetY = int(std::lround(origin.y)) - 115;
                RuntimeVoxelDrawResult flatDraw;
                if (!draw(flatWorld, flatWorld.getObjects().front(), probe, camera, *actualCache, cold, flatDraw)
                    || !flatDraw.bodyDrawn) return fail("original flat VXL body: " + type.id);
                const auto baseline = cold;
                Model body, turret, barrel;
                if (!model(flatDraw.bodyInfo.imageId, probe, *referenceCache, output, writtenModels, body, why)) return fail(why);
                const auto* originalRaster = loadRuntimeVoxelFromCache(base, probe, *actualCache);
                if (!originalRaster || !originalRaster->modelBounds.valid) return fail("original body bounds absent");
                for (int axis = 0; axis < 3; ++axis)
                    if (std::abs(originalRaster->modelBounds.minimum[axis] - body.bounds.minimum[axis]) > 1e-8
                        || std::abs(originalRaster->modelBounds.maximum[axis] - body.bounds.maximum[axis]) > 1e-8)
                        return fail("independent original full-section body bounds: " + type.id);
                if (flatDraw.turretDrawn
                    && !model(flatDraw.turretInfo.imageId, probe, *referenceCache, output, writtenModels, turret, why)) return fail(why);
                if (flatDraw.barrelDrawn
                    && !model(flatDraw.barrelInfo.imageId, probe, *referenceCache, output, writtenModels, barrel, why)) return fail(why);
                if (assets && face == 0) {
                    assets << "VXL," << type.id << ',' << quoted(body.image) << ',' << quoted(body.source) << ',' << body.sections.size() << ",PASS\n";
                    if (flatDraw.turretDrawn) assets << "turret," << type.id << ',' << quoted(turret.image) << ',' << quoted(turret.source) << ',' << turret.sections.size() << ",PASS\n";
                    if (flatDraw.barrelDrawn) assets << "barrel," << type.id << ',' << quoted(barrel.image) << ',' << quoted(barrel.source) << ',' << barrel.sections.size() << ",PASS\n";
                }
                for (int r = 1; r <= 20; ++r) {
                    auto scene = world(ramps[r]); auto obj = object(type, face * 32, r);
                    scene.accessObjects() = {obj};
                    const auto sizing = runtimeVxlRasterSizingForOwner(type.id);
                    const auto projection = runtimeVxlProjectionParamsForSizing(sizing);
                    const double ratio = projection.groundYScale / projection.heightYScale;
                    const double gx = type.tilter ? (floor(r,.475,-.125) - floor(r,-.225,-.125)) / .7 * ratio : 0;
                    const double gy = type.tilter ? (floor(r,.125,.225) - floor(r,.125,-.475)) / .7 * ratio : 0;
                    // Normalize (-gx,-gy,1) and recover the ordered Y/X rotation.
                    const double length = std::sqrt(1 + gx * gx + gy * gy);
                    const std::array<double,3> normal{-gx / length, -gy / length, 1 / length};
                    const double eroll = std::atan2(-normal[1], normal[2]);
                    const double epitch = std::asin(normal[0]);
                    const auto pose = resolveRuntimeVehicleBodyPose(scene, scene.getObjects().front(), probe, *actualCache);
                    if (pose.terrainSupported != type.tilter || std::abs(pose.pitch - epitch) > 1e-10
                        || std::abs(pose.roll - eroll) > 1e-10 || pose.height != 0
                        || std::abs(pose.terrainHeight) > 1e-10)
                        return fail("retail floor normal/height: " + type.id + " ramp=" + std::to_string(r));
                    RuntimeVoxelBodyRotation rotation;
                    rotation.pitch = int(std::lround(epitch * 90 / pi));
                    rotation.roll = int(std::lround(eroll * 90 / pi));
                    rotation.floorZ = body.bounds.minimum[2];
                    const auto actualRotation = runtimeVehicleBodyRotation(pose, body.bounds.minimum[2]);
                    if (actualRotation.pitch != rotation.pitch || actualRotation.roll != rotation.roll)
                        return fail("independent original angle quantization");
                    if (!sectionGeometry(body, rotation, obj.facing, sizing.modelOriginPivot, cornerChecks, why)
                        || (flatDraw.turretDrawn && !sectionGeometry(turret, rotation, obj.turretFacing,
                            runtimeVxlRasterSizingForOwner(type.id + "|turret").modelOriginPivot, cornerChecks, why))
                        || (flatDraw.barrelDrawn && !sectionGeometry(barrel, rotation, obj.turretFacing,
                            runtimeVxlRasterSizingForOwner(type.id + "|barrel").modelOriginPivot, cornerChecks, why))) return fail(why);
                    const double planeLift = support(body, obj.facing, sizing.modelOriginPivot, rotation, gx, gy);
                    const double flatLift = support(body, obj.facing, sizing.modelOriginPivot, rotation, 0, 0);
                    const double actualLift = runtimeVehicleGroundSupportLift(body.bounds, rotation,
                        (facingBucketForFacingValue(obj.facing,32)-8)*2*pi/32, sizing.modelOriginPivot, gx, gy);
                    if (std::abs(actualLift - planeLift) > 1e-9
                        || ((r <= 16 && type.tilter) && planeLift > flatLift + 1e-8))
                        return fail("native plane contact lift: " + type.id);
                    RuntimeVoxelDrawResult first, second, panDraw, referenceDraw;
                    coldRaster(*actualCache); // Cold rasters; immutable original resource bytes remain loaded.
                    if (!draw(scene, scene.getObjects().front(), probe, camera, *actualCache, cold, first)
                        || !draw(scene, scene.getObjects().front(), probe, camera, *actualCache, warm, second)
                        || !reference(obj, probe, camera, *referenceCache, body, turret, barrel, flatDraw.bodyInfo.fitScale,
                            rotation, planeLift, expected, referenceDraw)) return fail("real original VXL draw: " + type.id);
                    if (cold != warm || cold != expected || !first.bodyDrawn
                        || first.turretDrawn != flatDraw.turretDrawn || first.barrelDrawn != flatDraw.barrelDrawn
                        || first.turretDrawn != referenceDraw.turretDrawn || first.barrelDrawn != referenceDraw.barrelDrawn
                        || first.bodyInfo.imageId != body.image
                        || std::abs(first.bodyInfo.fitScale - flatDraw.bodyInfo.fitScale) > 1e-10)
                        return fail("cold/warm/reference body-part pixels: " + type.id + " ramp=" + std::to_string(r)
                            + " facing=" + std::to_string(obj.facing));
                    RuntimeMapCamera pan = camera; pan.worldOffsetX += 7; pan.worldOffsetY -= 5;
                    if (!draw(scene, scene.getObjects().front(), probe, pan, *actualCache, panned, panDraw)) return fail("pan draw");
                    std::size_t opaque = 0;
                    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                        const int ox = x + 7, oy = y - 5;
                        const auto shifted = ox >= 0 && ox < width && oy >= 0 && oy < height
                            ? cold[oy * width + ox] : background;
                        if (panned[y * width + x] != shifted) return fail("panned original pixels: " + type.id);
                        opaque += cold[y * width + x] != background;
                    }
                    if (opaque < 12) return fail("native body pixel coverage missing: " + type.id);
                    ++combinations; pixelChecks += 3;
                    if (rows) rows << type.id << ',' << r << ',' << ramps[r].tile << ',' << ramps[r].sub << ','
                        << obj.facing << ',' << obj.turretFacing << ',' << ramps[r].authoredHeight << ','
                        << pose.pitch << ',' << pose.roll << ',' << epitch << ',' << eroll << ','
                        << rotation.pitch << ',' << rotation.roll << ',' << planeLift << ',' << flatLift << ','
                        << first.bodyInfo.fitScale << ',' << body.sections.size() << ',' << turret.sections.size() << ','
                        << barrel.sections.size() << ',' << opaque << ',' << hash(cold) << ',' << hash(warm) << ','
                        << hash(panned) << ',' << hash(expected) << ",PASS\n";
                    if (face < 4) for (std::size_t a = 0; a < atlasTypes.size(); ++a)
                        if (type.id == atlasTypes[a]) for (std::size_t row = 0; row < atlasRamps.size(); ++row)
                            if (r == atlasRamps[row]) for (int y = 0; y < height; ++y)
                                std::copy_n(cold.begin() + y * width, width,
                                    atlases[a].begin() + (row * height + y) * width * 4 + face * width);
                }
                RuntimeVoxelDrawResult returned;
                if (!draw(flatWorld, flatWorld.getObjects().front(), probe, camera, *actualCache, warm, returned)
                    || warm != baseline || resolveRuntimeVehicleBodyPose(flatWorld, base, probe, *actualCache).terrainSupported)
                    return fail("flat return-to-rest pixels: " + type.id);
            }
        }
        const auto it = std::find_if(types.begin(), types.end(), [](const auto& v){ return v.id == "MTNK"; });
        if (it == types.end()) return fail("stock control MTNK absent");
        for (int variant = 0; variant < 12; ++variant) {
            auto scene = world(ramps[1]); auto obj = object(*it, 32, 1);
            if (variant == 0) obj.alive = false;
            if (variant == 1) obj.health = 0;
            if (variant == 2) obj.dockedToObjectId = 99;
            if (variant == 3) obj.tankBunkerHostId = 99;
            if (variant == 4) obj.factoryExitHostId = 99;
            if (variant == 5) obj.garrisonHostObjectId = 99;
            if (variant == 6) obj.airborne = true;
            if (variant == 7) obj.authoredAircraft = true;
            if (variant == 8) obj.submerged = true;
            if (variant == 9) obj.onBridge = obj.renderOnBridge = true;
            if (variant == 10) { obj.type = "YAREFN"; obj.kind = WorldObjectKind::Structure; obj.deployed = true; }
            if (variant == 11) { obj.type = "E1"; obj.kind = WorldObjectKind::Infantry; }
            const auto pose = resolveRuntimeVehicleBodyPose(scene, obj, probe, *actualCache);
            if (pose.terrainSupported || pose.terrainPitch != 0 || pose.terrainRoll != 0) return fail("hidden/nonvehicle control " + std::to_string(variant));
            if (controls) controls << obj.type << ",hidden-kind-" << variant << ",1,0,0,0,PASS\n";
            ++controlChecks;
        }
        for (const char* id : {"MTNK", "SAPC", "LCRF", "YHVR", "ROBO"}) {
            const auto type = std::find_if(types.begin(), types.end(), [&](const auto& v){return v.id == id;});
            if (type == types.end()) return fail("water control type absent");
            auto wet = world(water); auto obj = object(*type, 64, 0);
            if (resolveRuntimeVehicleBodyPose(wet, obj, probe, *actualCache).terrainSupported) return fail("water support tilted " + type->id);
            auto cliff = world(flat);
            cliff.accessTerrainCells()[13].level = 8; // Actual +X support sample crosses this discontinuity.
            obj.renderX = 12.4;
            if (resolveRuntimeVehicleBodyPose(cliff, obj, probe, *actualCache).terrainSupported) return fail("flat cliff induced slope " + type->id);
            if (controls) controls << type->id << ",actual-water,0,0,0,0,PASS\n" << type->id << ",flat-cliff,0,0,0,0,PASS\n";
            controlChecks += 2;
        }
        if (combinations != 58U * 20U * 8U) return fail("complete 57 land plus CAOS VXL matrix count");
        if (!output.empty()) for (std::size_t a = 0; a < atlasTypes.size(); ++a)
            if (!writeRuntimeBgraBmp(output / ("render-" + atlasTypes[a] + ".bmp"), width * 4, height * 5, atlases[a]))
                return fail("native terrain atlas output");
        if ((rows.is_open() && !rows) || (assets.is_open() && !assets) || (controls.is_open() && !controls)) return fail("CSV write failed");
        std::cout << "Vehicle terrain slope render passed: landVXL=" << land << " originalRamps=20 facings=8 combinations="
            << combinations << " pixelChecks=" << pixelChecks << " originalSectionCorners=" << cornerChecks
            << " excludedControls=" << controlChecks << " cold/warm/panned/reference/flat=exact\n";
        return true;
    } catch (const std::exception& exception) { return fail(exception.what()); }
}
