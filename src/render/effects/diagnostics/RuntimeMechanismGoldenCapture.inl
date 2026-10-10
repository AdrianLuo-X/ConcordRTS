// Included by RuntimeShell.cpp after the offscreen renderer and runtime
// transition helpers are defined.  Keeping this as a focused capture adapter
// avoids exposing mutable RuntimeWindowState internals as a public API.

namespace {

constexpr int RuntimeMechanismGoldenPixelChannelTolerance = 4;
constexpr double RuntimeMechanismGoldenPixelRatioTolerance = 0.005;
constexpr long long RuntimeMechanismGoldenTimestampToleranceUs = 66667;
constexpr int RuntimeMechanismGoldenLevelTolerance = 1;

struct RuntimeMechanismGoldenFrame {
    int frameIndex = 0;
    int tick = 0;
    long long timestampUs = 0;
    std::string phase;
    std::string logicalType;
    std::string visualType;
    int x = -1;
    int y = -1;
    int level = 0;
    int facing = -1;
    int turretFacing = -1;
    bool airborne = false;
    bool deployed = false;
    int remainingOrStage = -1;
    int intensity = 0;
    int radius = 0;
    std::string color;
    std::uint32_t centerPixel = 0U;
    std::uint32_t edgePixel = 0U;
    std::uint64_t paletteHash = 0U;
    std::uint64_t pixelHash = 0U;
    std::string imagePath;
    int visibleWorldObjects = 0;
    int visibleVoxelBodies = 0;
    int visibleShpSprites = 0;
    int radiationTintPixels = 0;
};

struct RuntimeMechanismGoldenImage {
    std::uint64_t hash = 0U;
    std::string relativePath;
    std::vector<std::uint32_t> pixels;
};

struct RuntimeMechanismGoldenScenario {
    std::string id;
    std::string label;
    std::string authoredReference;
    int width = 128;
    int height = 96;
    std::vector<RuntimeMechanismGoldenFrame> frames;
    std::vector<RuntimeMechanismGoldenImage> uniqueImages;
    std::string currentAtlasPath;
    std::string referenceAtlasPath;
    std::string comparisonPath;
    std::string referenceFramesPath;
    std::string referenceStatus = "missing";
    std::string comparisonDetail = "retail execution capture unavailable";
    int stateMismatches = 0;
    std::size_t pixelsOutsideTolerance = 0U;
    double pixelsOutsideToleranceRatio = 0.0;
    int maximumChannelDelta = 0;
};

struct RuntimeMechanismGoldenBitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

struct RuntimeMechanismGoldenPixelDiff {
    bool comparable = false;
    bool passed = false;
    std::size_t pixelsOutsideTolerance = 0U;
    double pixelsOutsideToleranceRatio = 0.0;
    int maximumChannelDelta = 0;
    std::vector<std::uint32_t> amplifiedPixels;
};

std::string runtimeMechanismGoldenField(std::string value) {
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
}

std::string runtimeMechanismGoldenRelativePath(
    const std::filesystem::path& path,
    const std::filesystem::path& root
) {
    std::error_code errorCode;
    const std::filesystem::path relative =
        std::filesystem::relative(path, root, errorCode);
    return (errorCode ? path : relative).generic_string();
}

std::uint64_t runtimeMechanismGoldenPixelHash(
    const std::vector<std::uint32_t>& pixels
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::uint32_t pixel : pixels) {
        for (int shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<std::uint64_t>((pixel >> shift) & 0xffU);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

void runtimeMechanismGoldenMixPalette(
    std::uint64_t& hash,
    const std::vector<RuntimePaletteColor>& palette
) {
    for (const RuntimePaletteColor& color : palette) {
        hash ^= static_cast<std::uint64_t>(color.r);
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(color.g);
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(color.b);
        hash *= 1099511628211ULL;
    }
}

std::uint64_t runtimeMechanismGoldenPaletteHash(
    const RuntimeMapRenderCache& cache
) {
    std::uint64_t hash = 1469598103934665603ULL;
    runtimeMechanismGoldenMixPalette(hash, cache.palette);
    runtimeMechanismGoldenMixPalette(hash, cache.structurePalette);
    runtimeMechanismGoldenMixPalette(hash, cache.animPalette.palette);
    runtimeMechanismGoldenMixPalette(hash, cache.unitPalette.palette);
    return hash;
}

bool loadRuntimeMechanismGoldenBitmap(
    const std::filesystem::path& path,
    RuntimeMechanismGoldenBitmap& bitmap,
    std::string& errorMessage
) {
    bitmap = {};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        errorMessage = "could not open reference bitmap: " + path.string();
        return false;
    }

    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    input.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    input.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
    if (!input
        || fileHeader.bfType != 0x4d42
        || infoHeader.biSize < sizeof(BITMAPINFOHEADER)
        || infoHeader.biWidth <= 0
        || infoHeader.biHeight == 0
        || infoHeader.biBitCount != 32
        || infoHeader.biCompression != BI_RGB) {
        errorMessage = "reference bitmap must be uncompressed 32-bit BMP: "
            + path.string();
        return false;
    }

    bitmap.width = infoHeader.biWidth;
    bitmap.height = std::abs(infoHeader.biHeight);
    const std::size_t pixelCount =
        static_cast<std::size_t>(bitmap.width)
        * static_cast<std::size_t>(bitmap.height);
    if (pixelCount == 0U || pixelCount > 64U * 1024U * 1024U) {
        errorMessage = "reference bitmap dimensions are outside the Golden cap";
        bitmap = {};
        return false;
    }
    bitmap.pixels.assign(pixelCount, 0U);
    input.seekg(static_cast<std::streamoff>(fileHeader.bfOffBits));
    std::vector<std::uint32_t> row(
        static_cast<std::size_t>(bitmap.width),
        0U
    );
    const bool topDown = infoHeader.biHeight < 0;
    for (int sourceRow = 0; sourceRow < bitmap.height; ++sourceRow) {
        input.read(
            reinterpret_cast<char*>(row.data()),
            static_cast<std::streamsize>(
                row.size() * sizeof(std::uint32_t)
            )
        );
        if (!input) {
            errorMessage = "reference bitmap pixel payload is truncated";
            bitmap = {};
            return false;
        }
        const int targetRow = topDown
            ? sourceRow
            : bitmap.height - sourceRow - 1;
        std::copy(
            row.begin(),
            row.end(),
            bitmap.pixels.begin()
                + static_cast<std::ptrdiff_t>(targetRow * bitmap.width)
        );
    }
    errorMessage.clear();
    return true;
}

RuntimeMechanismGoldenPixelDiff compareRuntimeMechanismGoldenPixels(
    const std::vector<std::uint32_t>& current,
    const std::vector<std::uint32_t>& reference,
    int channelTolerance = RuntimeMechanismGoldenPixelChannelTolerance,
    double ratioTolerance = RuntimeMechanismGoldenPixelRatioTolerance
) {
    RuntimeMechanismGoldenPixelDiff result;
    if (current.empty() || current.size() != reference.size()) {
        return result;
    }
    result.comparable = true;
    result.amplifiedPixels.reserve(current.size());
    for (std::size_t index = 0; index < current.size(); ++index) {
        const std::uint32_t currentPixel = current[index];
        const std::uint32_t referencePixel = reference[index];
        int maximumPixelDelta = 0;
        std::uint32_t amplified = 0U;
        for (int channel = 0; channel < 3; ++channel) {
            const int shift = channel * 8;
            const int currentValue = static_cast<int>(
                (currentPixel >> shift) & 0xffU
            );
            const int referenceValue = static_cast<int>(
                (referencePixel >> shift) & 0xffU
            );
            const int delta = std::abs(currentValue - referenceValue);
            maximumPixelDelta = std::max(maximumPixelDelta, delta);
            result.maximumChannelDelta = std::max(
                result.maximumChannelDelta,
                delta
            );
            amplified |= static_cast<std::uint32_t>(
                std::min(255, delta * 8)
            ) << shift;
        }
        if (maximumPixelDelta > channelTolerance) {
            ++result.pixelsOutsideTolerance;
        }
        result.amplifiedPixels.push_back(amplified);
    }
    result.pixelsOutsideToleranceRatio =
        static_cast<double>(result.pixelsOutsideTolerance)
        / static_cast<double>(current.size());
    result.passed =
        result.pixelsOutsideToleranceRatio <= ratioTolerance;
    return result;
}

std::unique_ptr<RuntimeWindowState> makeRuntimeMechanismGoldenProbe(
    const RuntimeWindowState& source,
    const std::string& owner
) {
    auto probe = std::make_unique<RuntimeWindowState>();
    probe->config = source.config;
    probe->config.fixedTickHz = std::max(1, source.config.fixedTickHz);
    probe->report = source.report;
    probe->rules = source.rules;
    probe->art = source.art;
    probe->theater = source.theater;
    probe->mapFacts = source.mapFacts;
    probe->rulesLoaded = source.rulesLoaded;
    probe->artLoaded = source.artLoaded;
    probe->theaterLoaded = source.theaterLoaded;
    probe->mapFactsLoaded = source.mapFactsLoaded;
    probe->economy.owner = owner;
    probe->economy.techLevel = 10;
    probe->economy.skirmishStartApplied = true;
    probe->triggers.bootstrapComplete = true;
    probe->runtimeSuppressPlayerAudioFeedback = true;
    probe->ownerRemapColors[toLowerAsciiLocal(owner)] = RGB(176, 40, 216);

    // Keep the loaded retail map summary and terrain so the production renderer
    // follows its normal path.  Only mutable scene actors are removed; the
    // capture remains tied to the explicitly supplied map bundle.
    probe->world = source.world;
    probe->world.accessObjects().clear();
    probe->world.accessTerrainObjects().clear();
    probe->world.accessSmudges().clear();
    for (const TerrainCell& terrain : probe->world.getTerrainCells()) {
        probe->terrainInfoByCell.emplace(
            runtimeCellKey(terrain.x, terrain.y),
            RuntimeTerrainInfo{}
        );
    }
    probe->terrainByCell = buildRuntimeTerrainLookup(probe->world);
    ensureRuntimeSpecialMechanismsInitialized(*probe);
    return probe;
}

RuntimeCellPoint runtimeMechanismGoldenCenterCell(
    const RuntimeWindowState& state,
    int padding = 4
) {
    const GameWorldSummary& summary = state.world.getSummary();
    const int targetX = (summary.minCellX + summary.maxCellX) / 2;
    const int targetY = (summary.minCellY + summary.maxCellY) / 2;
    RuntimeCellPoint best{-1, -1};
    long long bestDistance = std::numeric_limits<long long>::max();
    for (const TerrainCell& candidate : state.world.getTerrainCells()) {
        bool clear = true;
        for (int dy = -padding; clear && dy <= padding; ++dy) {
            for (int dx = -padding; dx <= padding; ++dx) {
                const auto found = state.terrainByCell.find(runtimeCellKey(
                    candidate.x + dx,
                    candidate.y + dy
                ));
                if (found == state.terrainByCell.end()
                    || found->second == nullptr
                    || found->second->hasOverlay) {
                    clear = false;
                    break;
                }
            }
        }
        if (!clear) {
            continue;
        }
        const long long dx = static_cast<long long>(candidate.x - targetX);
        const long long dy = static_cast<long long>(candidate.y - targetY);
        const long long distance = dx * dx + dy * dy;
        if (distance < bestDistance
            || (distance == bestDistance
                && (best.y < 0 || candidate.y < best.y
                    || (candidate.y == best.y && candidate.x < best.x)))) {
            best = {candidate.x, candidate.y};
            bestDistance = distance;
        }
    }
    return best;
}

WorldObjectInstance* addRuntimeMechanismGoldenObject(
    RuntimeWindowState& state,
    RuntimeBuildKind kind,
    const std::string& typeId,
    RuntimeCellPoint cell
) {
    WorldObjectInstance* object = addRuntimeObjectInstanceForOwner(
        state,
        state.economy.owner,
        kind,
        typeId,
        cell
    );
    if (object == nullptr) {
        return nullptr;
    }
    object->mission = "Guard";
    object->buildAnimTicksTotal = 0;
    object->buildAnimTicksRemaining = 0;
    clearRuntimeRenderPosition(*object);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return object;
}

int runtimeMechanismGoldenDeployValue(const WorldObjectInstance& object) {
    return object.deployStateTicksRemaining >= 0
        ? object.deployStateTicksRemaining
        : object.buildAnimTicksRemaining;
}

bool captureRuntimeMechanismGoldenFrame(
    RuntimeWindowState& state,
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    RuntimeCellPoint centerCell,
    const WorldObjectInstance* object,
    const std::string& phase,
    int remainingOrStage,
    int intensity,
    int radius,
    const std::string& color,
    std::string& errorMessage
) {
    std::vector<std::uint32_t> pixels;
    RuntimeMapCamera camera;
    camera.initialized = true;
    const RuntimeProjectedPoint projectedCenter = runtimeProjectCellXY(
        centerCell.x,
        centerCell.y,
        terrainLevelAtRuntimeCell(state, centerCell.x, centerCell.y),
        camera
    );
    camera.worldOffsetX = projectedCenter.x - scenario.width / 2;
    camera.worldOffsetY = projectedCenter.y - (scenario.height * 3) / 4;
    const std::optional<RuntimeMapRenderStats> stats =
        drawRuntimeStateOffscreenAtCell(
            state,
            centerCell,
            scenario.width,
            scenario.height,
            errorMessage,
            "",
            false,
            false,
            -1,
            &pixels,
            false,
            &camera
        );
    if (!stats.has_value()
        || pixels.size()
            != static_cast<std::size_t>(scenario.width * scenario.height)) {
        if (errorMessage.empty()) {
            errorMessage = "offscreen Golden capture returned no pixels";
        }
        return false;
    }

    RuntimeMechanismGoldenFrame frame;
    frame.frameIndex = static_cast<int>(scenario.frames.size());
    frame.tick = state.tick;
    frame.timestampUs = static_cast<long long>(std::llround(
        static_cast<double>(state.tick) * 1000000.0
        / static_cast<double>(std::max(1, state.config.fixedTickHz))
    ));
    frame.phase = runtimeMechanismGoldenField(phase);
    frame.remainingOrStage = remainingOrStage;
    frame.intensity = intensity;
    frame.radius = radius;
    frame.color = runtimeMechanismGoldenField(color);
    if (object != nullptr) {
        frame.logicalType = runtimeMechanismGoldenField(object->type);
        RuntimeMapRenderContext renderContext;
        renderContext.rules = &state.rules;
        renderContext.art = &state.art;
        renderContext.animationTick = state.tick;
        frame.visualType = runtimeMechanismGoldenField(
            resolveRuntimeObjectVisualTypeId(*object, renderContext)
        );
        frame.x = object->x;
        frame.y = object->y;
        frame.level = object->level;
        frame.facing = object->facing;
        frame.turretFacing = object->turretFacing;
        frame.airborne = object->airborne;
        frame.deployed = object->deployed;
    } else {
        frame.logicalType = "RadSite";
        frame.visualType = "RadSite";
        frame.x = centerCell.x;
        frame.y = centerCell.y;
    }
    const int centerX = scenario.width / 2;
    const int centerY = scenario.height / 2;
    const int edgeX = std::clamp(centerX + 30, 0, scenario.width - 1);
    const int edgeY = std::clamp(centerY + 15, 0, scenario.height - 1);
    frame.centerPixel = pixels[
        static_cast<std::size_t>(centerY * scenario.width + centerX)
    ];
    frame.edgePixel = pixels[
        static_cast<std::size_t>(edgeY * scenario.width + edgeX)
    ];
    frame.paletteHash = runtimeMechanismGoldenPaletteHash(state.renderCache);
    frame.pixelHash = runtimeMechanismGoldenPixelHash(pixels);
    frame.visibleWorldObjects = stats->visibleWorldObjects;
    frame.visibleVoxelBodies = stats->visibleVoxelBodies;
    frame.visibleShpSprites = stats->visibleObjectShpSprites;
    frame.radiationTintPixels = stats->radiationTintPixels;

    const RuntimeMechanismGoldenImage* reused = nullptr;
    for (const RuntimeMechanismGoldenImage& candidate :
            scenario.uniqueImages) {
        if (candidate.hash == frame.pixelHash
            && candidate.pixels == pixels) {
            reused = &candidate;
            break;
        }
    }
    if (reused != nullptr) {
        frame.imagePath = reused->relativePath;
    } else {
        std::ostringstream fileName;
        fileName << "frame_" << std::setfill('0') << std::setw(4)
                 << frame.frameIndex << "_tick_" << std::setw(6)
                 << frame.tick << ".bmp";
        const std::filesystem::path path = outputRoot
            / "current"
            / scenario.id
            / fileName.str();
        if (!writeRuntimeSmokeBmp(
                pathString(path),
                pixels.data(),
                scenario.width,
                scenario.height,
                errorMessage
            )) {
            return false;
        }
        frame.imagePath = runtimeMechanismGoldenRelativePath(
            path,
            outputRoot
        );
        scenario.uniqueImages.push_back({
            frame.pixelHash,
            frame.imagePath,
            std::move(pixels)
        });
    }
    scenario.frames.push_back(std::move(frame));
    errorMessage.clear();
    return true;
}

bool writeRuntimeMechanismGoldenFrames(
    const RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    std::string& errorMessage
) {
    const std::filesystem::path path = outputRoot
        / "current"
        / scenario.id
        / "frames.tsv";
    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        errorMessage = "could not create Golden state directory: "
            + path.parent_path().string();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        errorMessage = "could not open Golden frame manifest: "
            + path.string();
        return false;
    }
    output
        << "frame\ttick\ttimestamp_us\tphase\tlogical_type\tvisual_type"
        << "\tx\ty\tlevel\tfacing\tturret_facing\tairborne\tdeployed"
        << "\tremaining_or_stage\tintensity\tradius\tcolor"
        << "\tcenter_pixel\tedge_pixel\tpalette_hash\tpixel_hash\timage"
        << "\tvisible_world_objects\tvisible_voxel_bodies"
        << "\tvisible_shp_sprites\tradiation_tint_pixels\n";
    for (const RuntimeMechanismGoldenFrame& frame : scenario.frames) {
        output
            << frame.frameIndex << '\t'
            << frame.tick << '\t'
            << frame.timestampUs << '\t'
            << frame.phase << '\t'
            << frame.logicalType << '\t'
            << frame.visualType << '\t'
            << frame.x << '\t'
            << frame.y << '\t'
            << frame.level << '\t'
            << frame.facing << '\t'
            << frame.turretFacing << '\t'
            << (frame.airborne ? 1 : 0) << '\t'
            << (frame.deployed ? 1 : 0) << '\t'
            << frame.remainingOrStage << '\t'
            << frame.intensity << '\t'
            << frame.radius << '\t'
            << frame.color << '\t'
            << frame.centerPixel << '\t'
            << frame.edgePixel << '\t'
            << frame.paletteHash << '\t'
            << frame.pixelHash << '\t'
            << frame.imagePath << '\t'
            << frame.visibleWorldObjects << '\t'
            << frame.visibleVoxelBodies << '\t'
            << frame.visibleShpSprites << '\t'
            << frame.radiationTintPixels << '\n';
    }
    if (!output) {
        errorMessage = "failed while writing Golden frame manifest";
        return false;
    }
    errorMessage.clear();
    return true;
}

bool buildRuntimeMechanismGoldenAtlas(
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    RuntimeMechanismGoldenBitmap& atlas,
    std::string& errorMessage
) {
    if (scenario.uniqueImages.empty()) {
        errorMessage = "Golden scenario has no unique rendered images";
        return false;
    }
    const int columns = std::min(
        8,
        static_cast<int>(scenario.uniqueImages.size())
    );
    const int rows = static_cast<int>(
        (scenario.uniqueImages.size() + static_cast<std::size_t>(columns) - 1U)
        / static_cast<std::size_t>(columns)
    );
    atlas.width = columns * scenario.width;
    atlas.height = rows * scenario.height;
    atlas.pixels.assign(
        static_cast<std::size_t>(atlas.width * atlas.height),
        0x00202020U
    );
    for (std::size_t imageIndex = 0;
        imageIndex < scenario.uniqueImages.size();
        ++imageIndex) {
        const int column = static_cast<int>(imageIndex) % columns;
        const int row = static_cast<int>(imageIndex) / columns;
        const std::vector<std::uint32_t>& source =
            scenario.uniqueImages[imageIndex].pixels;
        for (int y = 0; y < scenario.height; ++y) {
            const std::size_t sourceOffset =
                static_cast<std::size_t>(y * scenario.width);
            const std::size_t targetOffset = static_cast<std::size_t>(
                (row * scenario.height + y) * atlas.width
                + column * scenario.width
            );
            std::copy_n(
                source.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                scenario.width,
                atlas.pixels.begin()
                    + static_cast<std::ptrdiff_t>(targetOffset)
            );
        }
    }
    const std::filesystem::path path = outputRoot
        / "current"
        / scenario.id
        / "atlas.bmp";
    if (!writeRuntimeSmokeBmp(
            pathString(path),
            atlas.pixels.data(),
            atlas.width,
            atlas.height,
            errorMessage
        )) {
        return false;
    }
    scenario.currentAtlasPath = runtimeMechanismGoldenRelativePath(
        path,
        outputRoot
    );
    return true;
}

std::vector<std::string> splitRuntimeMechanismGoldenTsv(
    const std::string& line
) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(
            start,
            tab == std::string::npos ? std::string::npos : tab - start
        ));
        if (tab == std::string::npos) {
            break;
        }
        start = tab + 1U;
    }
    return fields;
}

bool parseRuntimeMechanismGoldenInteger(
    const std::string& value,
    long long& parsed
) {
    try {
        std::size_t consumed = 0U;
        parsed = std::stoll(value, &consumed, 10);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

bool parseRuntimeMechanismGoldenUnsignedInteger(
    const std::string& value,
    std::uint64_t& parsed
) {
    try {
        std::size_t consumed = 0U;
        parsed = std::stoull(value, &consumed, 10);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

bool compareRuntimeMechanismGoldenState(
    const RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& referenceFrames,
    int& mismatchCount,
    std::string& detail
) {
    mismatchCount = 0;
    std::ifstream input(referenceFrames, std::ios::binary);
    if (!input) {
        detail = "reference state manifest is missing";
        return false;
    }
    std::string header;
    std::getline(input, header);
    std::vector<std::vector<std::string>> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            rows.push_back(splitRuntimeMechanismGoldenTsv(line));
        }
    }
    if (rows.size() != scenario.frames.size()) {
        mismatchCount = static_cast<int>(std::max(
            rows.size(),
            scenario.frames.size()
        ));
        detail = "state frame count differs: current="
            + std::to_string(scenario.frames.size())
            + " reference=" + std::to_string(rows.size());
        return false;
    }

    std::string firstMismatch;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const std::vector<std::string>& fields = rows[index];
        const RuntimeMechanismGoldenFrame& current = scenario.frames[index];
        bool mismatch = fields.size() < 26U;
        long long frame = -1;
        long long tick = -1;
        long long timestampUs = -1;
        long long x = 0;
        long long y = 0;
        long long level = 0;
        long long facing = 0;
        long long turretFacing = 0;
        long long airborne = 0;
        long long deployed = 0;
        long long remaining = 0;
        long long intensity = 0;
        long long radius = 0;
        std::uint64_t paletteHash = 0U;
        if (!mismatch) {
            mismatch =
                !parseRuntimeMechanismGoldenInteger(fields[0], frame)
                || !parseRuntimeMechanismGoldenInteger(fields[1], tick)
                || !parseRuntimeMechanismGoldenInteger(fields[2], timestampUs)
                || !parseRuntimeMechanismGoldenInteger(fields[6], x)
                || !parseRuntimeMechanismGoldenInteger(fields[7], y)
                || !parseRuntimeMechanismGoldenInteger(fields[8], level)
                || !parseRuntimeMechanismGoldenInteger(fields[9], facing)
                || !parseRuntimeMechanismGoldenInteger(fields[10], turretFacing)
                || !parseRuntimeMechanismGoldenInteger(fields[11], airborne)
                || !parseRuntimeMechanismGoldenInteger(fields[12], deployed)
                || !parseRuntimeMechanismGoldenInteger(fields[13], remaining)
                || !parseRuntimeMechanismGoldenInteger(fields[14], intensity)
                || !parseRuntimeMechanismGoldenInteger(fields[15], radius)
                || !parseRuntimeMechanismGoldenUnsignedInteger(
                    fields[19],
                    paletteHash
                );
        }
        if (!mismatch) {
            mismatch = frame != current.frameIndex
                || tick != current.tick
                || std::llabs(timestampUs - current.timestampUs)
                    > RuntimeMechanismGoldenTimestampToleranceUs
                || toLowerAsciiLocal(fields[3])
                    != toLowerAsciiLocal(current.phase)
                || toLowerAsciiLocal(fields[4])
                    != toLowerAsciiLocal(current.logicalType)
                || toLowerAsciiLocal(fields[5])
                    != toLowerAsciiLocal(current.visualType)
                || x != current.x
                || y != current.y
                || std::llabs(level - current.level)
                    > RuntimeMechanismGoldenLevelTolerance
                || facing != current.facing
                || turretFacing != current.turretFacing
                || airborne != (current.airborne ? 1 : 0)
                || deployed != (current.deployed ? 1 : 0)
                || std::llabs(remaining - current.remainingOrStage) > 1
                || std::llabs(intensity - current.intensity) > 1
                || radius != current.radius
                || fields[16] != current.color
                || (paletteHash != 0U
                    && paletteHash != current.paletteHash);
        }
        if (mismatch) {
            ++mismatchCount;
            if (firstMismatch.empty()) {
                firstMismatch = "first mismatch at frame "
                    + std::to_string(index);
            }
        }
    }
    detail = mismatchCount == 0
        ? "state/timing fields are within tolerance"
        : firstMismatch + ", total=" + std::to_string(mismatchCount);
    return mismatchCount == 0;
}

bool writeRuntimeMechanismGoldenComparison(
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    const RuntimeMechanismGoldenBitmap& current,
    const RuntimeMechanismGoldenBitmap* reference,
    const RuntimeMechanismGoldenPixelDiff* diff,
    std::string& errorMessage
) {
    RuntimeMechanismGoldenBitmap composite;
    composite.width = current.width * 3;
    composite.height = current.height;
    composite.pixels.assign(
        static_cast<std::size_t>(composite.width * composite.height),
        0x00404040U
    );
    for (int y = 0; y < current.height; ++y) {
        for (int x = 0; x < current.width; ++x) {
            const std::size_t sourceIndex =
                static_cast<std::size_t>(y * current.width + x);
            const std::size_t targetBase =
                static_cast<std::size_t>(y * composite.width + x);
            composite.pixels[targetBase] = current.pixels[sourceIndex];
            if (reference != nullptr
                && reference->width == current.width
                && reference->height == current.height) {
                composite.pixels[targetBase
                    + static_cast<std::size_t>(current.width)] =
                    reference->pixels[sourceIndex];
            } else {
                const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
                composite.pixels[targetBase
                    + static_cast<std::size_t>(current.width)] =
                    checker ? 0x00585858U : 0x00383838U;
            }
            if (diff != nullptr
                && diff->amplifiedPixels.size() == current.pixels.size()) {
                composite.pixels[targetBase
                    + static_cast<std::size_t>(current.width * 2)] =
                    diff->amplifiedPixels[sourceIndex];
            } else {
                const bool stripe = ((x + y) / 8) % 2 == 0;
                composite.pixels[targetBase
                    + static_cast<std::size_t>(current.width * 2)] =
                    stripe ? 0x00602060U : 0x00301030U;
            }
        }
    }
    const std::filesystem::path path = outputRoot
        / "comparison"
        / (scenario.id + ".bmp");
    if (!writeRuntimeSmokeBmp(
            pathString(path),
            composite.pixels.data(),
            composite.width,
            composite.height,
            errorMessage
        )) {
        return false;
    }
    scenario.comparisonPath = runtimeMechanismGoldenRelativePath(
        path,
        outputRoot
    );
    return true;
}

bool finalizeRuntimeMechanismGoldenScenario(
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    const std::filesystem::path& referenceRoot,
    bool referenceWasExplicit,
    bool& referenceFailure,
    std::string& errorMessage
) {
    if (!writeRuntimeMechanismGoldenFrames(
            scenario,
            outputRoot,
            errorMessage
        )) {
        return false;
    }
    RuntimeMechanismGoldenBitmap currentAtlas;
    if (!buildRuntimeMechanismGoldenAtlas(
            scenario,
            outputRoot,
            currentAtlas,
            errorMessage
        )) {
        return false;
    }

    const std::filesystem::path referenceScenario =
        referenceRoot / scenario.id;
    const std::filesystem::path referenceAtlasPath =
        referenceScenario / "atlas.bmp";
    const std::filesystem::path referenceFramesPath =
        referenceScenario / "frames.tsv";
    scenario.referenceAtlasPath = referenceAtlasPath.generic_string();
    scenario.referenceFramesPath = referenceFramesPath.generic_string();
    const bool atlasExists = pathExists(referenceAtlasPath);
    const bool framesExist = pathExists(referenceFramesPath);
    RuntimeMechanismGoldenBitmap referenceAtlas;
    RuntimeMechanismGoldenPixelDiff pixelDiff;
    const RuntimeMechanismGoldenBitmap* referenceForComposite = nullptr;
    const RuntimeMechanismGoldenPixelDiff* diffForComposite = nullptr;

    if (atlasExists && framesExist) {
        std::string bitmapError;
        const bool bitmapLoaded = loadRuntimeMechanismGoldenBitmap(
            referenceAtlasPath,
            referenceAtlas,
            bitmapError
        );
        bool statePassed = false;
        std::string stateDetail;
        if (bitmapLoaded) {
            pixelDiff = compareRuntimeMechanismGoldenPixels(
                currentAtlas.pixels,
                referenceAtlas.pixels
            );
            if (referenceAtlas.width != currentAtlas.width
                || referenceAtlas.height != currentAtlas.height) {
                pixelDiff.comparable = false;
                pixelDiff.passed = false;
            }
            statePassed = compareRuntimeMechanismGoldenState(
                scenario,
                referenceFramesPath,
                scenario.stateMismatches,
                stateDetail
            );
        }
        if (bitmapLoaded
            && pixelDiff.comparable
            && pixelDiff.passed
            && statePassed) {
            scenario.referenceStatus = "pass";
            scenario.comparisonDetail =
                "retail capture supplied; pixels and state are within tolerance";
        } else {
            scenario.referenceStatus = "fail";
            scenario.comparisonDetail = bitmapLoaded
                ? (pixelDiff.comparable
                    ? "pixel/state mismatch; " + stateDetail
                    : "reference atlas dimensions differ; " + stateDetail)
                : bitmapError;
            referenceFailure = true;
        }
        if (bitmapLoaded
            && referenceAtlas.width == currentAtlas.width
            && referenceAtlas.height == currentAtlas.height) {
            referenceForComposite = &referenceAtlas;
        }
        if (pixelDiff.comparable) {
            diffForComposite = &pixelDiff;
            scenario.pixelsOutsideTolerance =
                pixelDiff.pixelsOutsideTolerance;
            scenario.pixelsOutsideToleranceRatio =
                pixelDiff.pixelsOutsideToleranceRatio;
            scenario.maximumChannelDelta = pixelDiff.maximumChannelDelta;
        }
    } else {
        scenario.referenceStatus = "missing";
        scenario.comparisonDetail =
            "no legal retail execution atlas/state bundle was found";
        if (referenceWasExplicit) {
            referenceFailure = true;
            scenario.comparisonDetail += " in the explicitly supplied root";
        }
    }

    return writeRuntimeMechanismGoldenComparison(
        scenario,
        outputRoot,
        currentAtlas,
        referenceForComposite,
        diffForComposite,
        errorMessage
    );
}

bool writeRuntimeMechanismGoldenReport(
    const RuntimeWindowState& source,
    const std::vector<RuntimeMechanismGoldenScenario>& scenarios,
    const std::filesystem::path& outputRoot,
    const std::filesystem::path& referenceRoot,
    std::string& errorMessage
) {
    const std::filesystem::path path = outputRoot / "difference_report.md";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        errorMessage = "could not open Golden difference report: "
            + path.string();
        return false;
    }
    output << "# Special-mechanism visual and timing Golden report\n\n";
    output << "This run changed no gameplay constants. Retail-authored INI and "
              "art assets are evidence for fields/resources, but they are not "
              "treated as retail execution footage. A missing execution capture "
              "therefore remains **UNASSESSED**, never ‘exact’.\n\n";
    output << "- Runtime tick rate: `" << source.config.fixedTickHz << " Hz`\n";
    output << "- Loaded rules: `" << source.report.rulesPath << "`\n";
    output << "- Loaded art: `" << source.report.artPath << "`\n";
    output << "- Retail reference root: `" << referenceRoot.generic_string()
           << "`\n\n";
    output << "| Mechanism | Retail-authored evidence | Current capture | "
              "Reference | Difference image | Result |\n";
    output << "| --- | --- | --- | --- | --- | --- |\n";
    for (const RuntimeMechanismGoldenScenario& scenario : scenarios) {
        output << "| " << scenario.label
               << " | " << scenario.authoredReference
               << " | [" << scenario.frames.size() << " frames / "
               << scenario.uniqueImages.size() << " unique images]("
               << scenario.currentAtlasPath << ")"
               << " | `" << scenario.referenceStatus << "` (`"
               << scenario.referenceFramesPath << "`)"
               << " | [current / reference / amplified diff]("
               << scenario.comparisonPath << ")"
               << " | " << scenario.comparisonDetail << " |\n";
    }

    output << "\n## Explicit tolerances\n\n";
    output << "| Dimension | Tolerance | Enforcement |\n";
    output << "| --- | ---: | --- |\n";
    output << "| Timestamp | ±66.667 ms (one retail 15 FPS frame) | "
              "Every aligned state row |\n";
    output << "| Cell position | 0 cells | Exact x/y |\n";
    output << "| Render/flight level | ±1 level | Every aligned state row |\n";
    output << "| Body/turret facing | 0 facing units | Exact |\n";
    output << "| Phase, logical type, visual type, airborne/deployed | 0 | "
              "Exact |\n";
    output << "| Countdown/stage and radiation intensity | ±1 | "
              "Every aligned state row |\n";
    output << "| Radiation radius/color | 0 | Exact |\n";
    output << "| Palette fingerprint | 0 | Exact when reference declares "
              "a nonzero fingerprint |\n";
    output << "| Pixel channels | ≤4 per R/G/B channel | Per atlas pixel |\n";
    output << "| Pixels outside channel tolerance | ≤0.5% | Whole atlas |\n";

    output << "\n## Comparison measurements\n\n";
    output << "| Mechanism | State mismatches | Pixels outside tolerance | "
              "Ratio | Maximum channel delta |\n";
    output << "| --- | ---: | ---: | ---: | ---: |\n";
    for (const RuntimeMechanismGoldenScenario& scenario : scenarios) {
        output << "| " << scenario.label
               << " | " << scenario.stateMismatches
               << " | " << scenario.pixelsOutsideTolerance
               << " | " << std::fixed << std::setprecision(6)
               << scenario.pixelsOutsideToleranceRatio
               << " | " << scenario.maximumChannelDelta << " |\n";
    }
    output << "\nThe comparison BMP is ordered left-to-right as current, "
              "reference, amplified absolute difference. When reference is "
              "missing, the latter two panels are explicit placeholders. "
              "Each `frames.tsv` row maps one runtime frame and timestamp to "
              "its screenshot; identical pixels reuse one image path without "
              "losing frame-level state.\n";
    if (!output) {
        errorMessage = "failed while writing Golden difference report";
        return false;
    }
    errorMessage.clear();
    return true;
}

void advanceRuntimeMechanismGoldenDeployTick(RuntimeWindowState& state) {
    ++state.tick;
    (void)advanceRuntimeBuildAnimations(state);
    (void)advanceRuntimeDeployTransitions(state);
}

bool captureRuntimeSiegeChopperGolden(
    const RuntimeWindowState& source,
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    std::string& errorMessage
) {
    const UnitType* rules = findRuntimeUnitTypeById(source.rules, "SCHP");
    if (rules == nullptr || !rules->deployToLand
        || !runtimeCrateTypeIs(rules->unloadingClass, "SCHD")) {
        errorMessage = "stock SCHP DeployToLand/UnloadingClass rules missing";
        return false;
    }
    auto stateStorage = makeRuntimeMechanismGoldenProbe(
        source,
        "GoldenSiege"
    );
    RuntimeWindowState& state = *stateStorage;
    RuntimeSnapshotActiveStateScope snapshotScope(state);
    const RuntimeCellPoint center = runtimeMechanismGoldenCenterCell(state);
    WorldObjectInstance* object = addRuntimeMechanismGoldenObject(
        state,
        RuntimeBuildKind::Unit,
        rules->id,
        center
    );
    if (object == nullptr || !object->airborne) {
        errorMessage = "could not create airborne SCHP Golden probe";
        return false;
    }
    const int objectId = object->instanceId;
    if (!captureRuntimeMechanismGoldenFrame(
            state,
            scenario,
            outputRoot,
            center,
            object,
            "AirborneIdle",
            runtimeMechanismGoldenDeployValue(*object),
            0,
            0,
            "",
            errorMessage
        )) {
        return false;
    }
    ++state.tick;
    if (!executeRuntimeSecondaryAction(state, objectId)) {
        errorMessage = "SCHP Golden deploy command was rejected";
        return false;
    }
    bool deployedIdle = false;
    for (int guard = 0; guard < 256; ++guard) {
        object = findMutableObjectById(state, objectId);
        if (object == nullptr) {
            errorMessage = "SCHP vanished during Golden deploy";
            return false;
        }
        const std::string phase = runtimeMissionStateNameForObject(*object);
        if (!captureRuntimeMechanismGoldenFrame(
                state,
                scenario,
                outputRoot,
                center,
                object,
                phase,
                runtimeMechanismGoldenDeployValue(*object),
                0,
                0,
                "",
                errorMessage
            )) {
            return false;
        }
        if (phase == "DeployedIdle") {
            deployedIdle = true;
            break;
        }
        advanceRuntimeMechanismGoldenDeployTick(state);
    }
    if (!deployedIdle
        || object == nullptr
        || object->airborne
        || !runtimeCrateTypeIs(
            scenario.frames.back().visualType,
            "SCHD"
        )) {
        errorMessage = "SCHP Golden probe did not reach grounded SCHD";
        return false;
    }

    ++state.tick;
    if (!executeRuntimeSecondaryAction(state, objectId)) {
        errorMessage = "SCHD Golden undeploy command was rejected";
        return false;
    }
    bool airborneIdle = false;
    bool takeoffSeen = false;
    for (int guard = 0; guard < 256; ++guard) {
        object = findMutableObjectById(state, objectId);
        if (object == nullptr) {
            errorMessage = "SCHP vanished during Golden undeploy";
            return false;
        }
        const std::string phase = runtimeMissionStateNameForObject(*object);
        takeoffSeen = takeoffSeen || phase == "TakingOffAfterUndeploy";
        if (!captureRuntimeMechanismGoldenFrame(
                state,
                scenario,
                outputRoot,
                center,
                object,
                phase,
                runtimeMechanismGoldenDeployValue(*object),
                0,
                0,
                "",
                errorMessage
            )) {
            return false;
        }
        airborneIdle = phase == "Idle" && object->airborne;
        if (airborneIdle) {
            break;
        }
        advanceRuntimeMechanismGoldenDeployTick(state);
    }
    if (!takeoffSeen
        || !airborneIdle
        || !runtimeCrateTypeIs(scenario.frames.back().visualType, "SCHP")) {
        errorMessage = "SCHP Golden probe did not finish SCHD-to-SCHP takeoff";
        return false;
    }
    return true;
}

bool captureRuntimeYuriUndeployGolden(
    const RuntimeWindowState& source,
    const std::string& typeId,
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    std::string& errorMessage
) {
    const UnitType* rules = findRuntimeUnitTypeById(source.rules, typeId);
    if (rules == nullptr || rules->undeployDelay <= 0) {
        errorMessage = typeId + " stock UndeployDelay rule missing";
        return false;
    }
    auto stateStorage = makeRuntimeMechanismGoldenProbe(
        source,
        "Golden" + typeId
    );
    RuntimeWindowState& state = *stateStorage;
    RuntimeSnapshotActiveStateScope snapshotScope(state);
    const RuntimeCellPoint center = runtimeMechanismGoldenCenterCell(state);
    WorldObjectInstance* object = addRuntimeMechanismGoldenObject(
        state,
        RuntimeBuildKind::Infantry,
        rules->id,
        center
    );
    if (object == nullptr) {
        errorMessage = "could not create " + typeId + " Golden probe";
        return false;
    }
    const int objectId = object->instanceId;
    if (!captureRuntimeMechanismGoldenFrame(
            state,
            scenario,
            outputRoot,
            center,
            object,
            "Idle",
            runtimeMechanismGoldenDeployValue(*object),
            0,
            0,
            "",
            errorMessage
        )) {
        return false;
    }
    ++state.tick;
    if (!executeRuntimeSecondaryAction(state, objectId)) {
        errorMessage = typeId + " Golden deploy command was rejected";
        return false;
    }
    bool deployedIdle = false;
    for (int guard = 0; guard < 256; ++guard) {
        object = findMutableObjectById(state, objectId);
        if (object == nullptr) {
            errorMessage = typeId + " vanished during deploy";
            return false;
        }
        const std::string phase = runtimeMissionStateNameForObject(*object);
        if (!captureRuntimeMechanismGoldenFrame(
                state,
                scenario,
                outputRoot,
                center,
                object,
                phase,
                runtimeMechanismGoldenDeployValue(*object),
                0,
                0,
                "",
                errorMessage
            )) {
            return false;
        }
        if (phase == "DeployedIdle") {
            deployedIdle = true;
            break;
        }
        advanceRuntimeMechanismGoldenDeployTick(state);
    }
    const int expectedDelay = runtimeDeployRuleFramesToTicks(
        state,
        rules->undeployDelay
    );
    if (!deployedIdle
        || object == nullptr
        || object->deployStateTicksRemaining != expectedDelay) {
        errorMessage = typeId + " Golden delay did not start at "
            + std::to_string(expectedDelay) + " runtime ticks";
        return false;
    }

    bool undeploying = false;
    for (int guard = 0; guard <= expectedDelay + 2; ++guard) {
        advanceRuntimeMechanismGoldenDeployTick(state);
        object = findMutableObjectById(state, objectId);
        if (object == nullptr) {
            errorMessage = typeId + " vanished during UndeployDelay";
            return false;
        }
        const std::string phase = runtimeMissionStateNameForObject(*object);
        if (!captureRuntimeMechanismGoldenFrame(
                state,
                scenario,
                outputRoot,
                center,
                object,
                phase,
                runtimeMechanismGoldenDeployValue(*object),
                0,
                0,
                "",
                errorMessage
            )) {
            return false;
        }
        if (phase == "Undeploying") {
            undeploying = true;
            break;
        }
    }
    bool idle = false;
    for (int guard = 0; undeploying && guard < 256; ++guard) {
        advanceRuntimeMechanismGoldenDeployTick(state);
        object = findMutableObjectById(state, objectId);
        if (object == nullptr) {
            errorMessage = typeId + " vanished during auto-undeploy";
            return false;
        }
        const std::string phase = runtimeMissionStateNameForObject(*object);
        if (!captureRuntimeMechanismGoldenFrame(
                state,
                scenario,
                outputRoot,
                center,
                object,
                phase,
                runtimeMechanismGoldenDeployValue(*object),
                0,
                0,
                "",
                errorMessage
            )) {
            return false;
        }
        if (phase == "Idle" && !object->deployed) {
            idle = true;
            break;
        }
    }
    if (!undeploying || !idle) {
        errorMessage = typeId + " Golden probe did not auto-undeploy";
        return false;
    }
    return true;
}

bool captureRuntimePrismChargeGolden(
    const RuntimeWindowState& source,
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    std::string& errorMessage
) {
    const UnitType* rules = findRuntimeUnitTypeById(source.rules, "SREF");
    if (rules == nullptr || !rules->chargeTurret || rules->turretCount != 4) {
        errorMessage = "stock SREF IsChargeTurret/TurretCount rules missing";
        return false;
    }
    auto stateStorage = makeRuntimeMechanismGoldenProbe(
        source,
        "GoldenPrism"
    );
    RuntimeWindowState& state = *stateStorage;
    RuntimeSnapshotActiveStateScope snapshotScope(state);
    const RuntimeCellPoint center = runtimeMechanismGoldenCenterCell(state);
    WorldObjectInstance* object = addRuntimeMechanismGoldenObject(
        state,
        RuntimeBuildKind::Unit,
        rules->id,
        center
    );
    if (object == nullptr) {
        errorMessage = "could not create SREF Golden probe";
        return false;
    }
    object->chargeTurretStartTick = 0;
    std::array<int, 16> observedStages{};
    for (int tick = 0; tick <= 15; ++tick) {
        state.tick = tick;
        RuntimeMapRenderContext renderContext;
        renderContext.rules = &state.rules;
        renderContext.art = &state.art;
        renderContext.animationTick = tick;
        const int stage = runtimeChargeTurretStageForGoldenCapture(
            *object,
            renderContext
        );
        observedStages[static_cast<std::size_t>(tick)] = stage;
        if (!captureRuntimeMechanismGoldenFrame(
                state,
                scenario,
                outputRoot,
                center,
                object,
                "ChargeStage" + std::to_string(stage),
                stage,
                0,
                0,
                "",
                errorMessage
            )) {
            return false;
        }
    }
    for (int tick = 0; tick <= 15; ++tick) {
        const int expected = tick < 5 ? 3 : (tick < 10 ? 2 : (tick < 15 ? 1 : 0));
        if (observedStages[static_cast<std::size_t>(tick)] != expected) {
            errorMessage = "SREF Golden stage selection diverged at tick "
                + std::to_string(tick);
            return false;
        }
    }
    return true;
}

bool captureRuntimeRadiationGolden(
    const RuntimeWindowState& source,
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    std::string& errorMessage
) {
    auto stateStorage = makeRuntimeMechanismGoldenProbe(
        source,
        "GoldenRadiation"
    );
    RuntimeWindowState& state = *stateStorage;
    RuntimeSnapshotActiveStateScope snapshotScope(state);
    const RuntimeCellPoint center = runtimeMechanismGoldenCenterCell(state);
    constexpr int radius = 2;
    constexpr int strength = 200;
    const SpecialMechanismRules& specialRules =
        state.rules.getSpecialMechanismRules();
    if (specialRules.radiationLevelMax != 500
        || specialRules.radiationLevelDelay != 90
        || specialRules.radiationLightDelay != 90
        || specialRules.radiationColor != "0,255,0") {
        errorMessage = "stock [Radiation] rules do not match the audited bundle";
        return false;
    }
    const int areaId = state.specialMechanisms.spawnRadiationArea(
        -1,
        state.economy.owner,
        {center.x, center.y},
        radius,
        strength,
        100,
        specialRules.radiationSiteWarhead,
        state.tick,
        true,
        [](const SpecialMechanismEvent&) {}
    );
    if (areaId < 0) {
        errorMessage = "could not create radiation Golden site";
        return false;
    }
    bool expired = false;
    for (int guard = 0; guard <= strength + 2; ++guard) {
        const auto area = state.specialMechanisms.radiationAreas().find(areaId);
        const bool active = area != state.specialMechanisms.radiationAreas().end();
        const int intensity = active ? area->second.strength : 0;
        const int remaining = active ? area->second.remainingTicks : 0;
        if (!captureRuntimeMechanismGoldenFrame(
                state,
                scenario,
                outputRoot,
                center,
                nullptr,
                active ? "Active" : "Expired",
                remaining,
                intensity,
                radius,
                specialRules.radiationColor,
                errorMessage
            )) {
            return false;
        }
        if (!active) {
            expired = true;
            break;
        }
        ++state.tick;
        (void)state.specialMechanisms.tickRadiation(
            state.world.accessObjects(),
            state.rules,
            state.tick,
            [](const std::string&, const std::string&) { return false; },
            [](int) { return true; },
            [](const SpecialMechanismEvent&) {}
        );
    }
    const bool tintObserved = std::any_of(
        scenario.frames.begin(),
        scenario.frames.end(),
        [](const RuntimeMechanismGoldenFrame& frame) {
            return frame.radiationTintPixels > 0;
        }
    );
    const auto intensityAtTick = [&scenario](int tick) {
        const auto found = std::find_if(
            scenario.frames.begin(),
            scenario.frames.end(),
            [tick](const RuntimeMechanismGoldenFrame& frame) {
                return frame.tick == tick;
            }
        );
        return found == scenario.frames.end() ? -1 : found->intensity;
    };
    if (!expired
        || !tintObserved
        || intensityAtTick(0) != 200
        || intensityAtTick(90) != 110
        || intensityAtTick(180) != 20) {
        errorMessage = "radiation Golden decay/tint checkpoints diverged: "
            "expired=" + std::to_string(expired ? 1 : 0)
            + " tint=" + std::to_string(tintObserved ? 1 : 0)
            + " i0=" + std::to_string(intensityAtTick(0))
            + " i90=" + std::to_string(intensityAtTick(90))
            + " i180=" + std::to_string(intensityAtTick(180));
        return false;
    }
    return true;
}

bool captureRuntimeSlaveMinerFacingGolden(
    const RuntimeWindowState& source,
    RuntimeMechanismGoldenScenario& scenario,
    const std::filesystem::path& outputRoot,
    std::string& errorMessage
) {
    const UnitType* minerRules = findRuntimeUnitTypeById(
        source.rules,
        "SMIN"
    );
    const BuildingType* refineryRules = findRuntimeBuildingTypeById(
        source.rules,
        "YAREFN"
    );
    if (minerRules == nullptr
        || refineryRules == nullptr
        || !runtimeCrateTypeIs(minerRules->deploysInto, "YAREFN")
        || refineryRules->deployFacing != 0) {
        errorMessage = "stock SMIN/YAREFN transform-facing rules missing";
        return false;
    }
    auto stateStorage = makeRuntimeMechanismGoldenProbe(
        source,
        "GoldenSlaveMiner"
    );
    RuntimeWindowState& state = *stateStorage;
    RuntimeSnapshotActiveStateScope snapshotScope(state);
    const RuntimeCellPoint center = runtimeMechanismGoldenCenterCell(state);
    WorldObjectInstance* object = addRuntimeMechanismGoldenObject(
        state,
        RuntimeBuildKind::Unit,
        minerRules->id,
        center
    );
    if (object == nullptr) {
        errorMessage = "could not create SMIN Golden probe";
        return false;
    }
    const int originalId = object->instanceId;
    object->facing = worldObjectFacingFromMapFacing(4 * 32);
    object->turretFacing = object->facing;
    if (!captureRuntimeMechanismGoldenFrame(
            state,
            scenario,
            outputRoot,
            center,
            object,
            "MobileFacing4",
            object->facing,
            0,
            0,
            "",
            errorMessage
        )) {
        return false;
    }
    ++state.tick;
    if (!deployRuntimeMcv(state, originalId, false)) {
        errorMessage = "SMIN Golden transform to YAREFN was rejected";
        return false;
    }
    WorldObjectInstance* refinery = nullptr;
    for (WorldObjectInstance& candidate : state.world.accessObjects()) {
        if (candidate.alive
            && candidate.kind == WorldObjectKind::Structure
            && runtimeCrateTypeIs(candidate.type, "YAREFN")) {
            refinery = &candidate;
            break;
        }
    }
    const int expectedFacing = worldObjectFacingFromMapFacing(0);
    if (refinery == nullptr
        || refinery->facing != expectedFacing
        || !captureRuntimeMechanismGoldenFrame(
            state,
            scenario,
            outputRoot,
            center,
            refinery,
            "DeployedFacing0",
            refinery == nullptr ? -1 : refinery->facing,
            0,
            0,
            "",
            errorMessage
        )) {
        if (errorMessage.empty()) {
            errorMessage = "YAREFN Golden facing was not authored facing 0";
        }
        return false;
    }
    const int refineryId = refinery->instanceId;
    // A freshly deployed refinery must finish its authored buildup before
    // it can repack. Advance the same transition used by the other probes.
    for (int remaining = refinery->buildAnimTicksRemaining; remaining > 0; --remaining) {
        advanceRuntimeMechanismGoldenDeployTick(state);
    }
    ++state.tick;
    if (!undeployRuntimeStructure(state, refineryId)) {
        errorMessage = "YAREFN Golden transform back to SMIN was rejected: " + state.economy.status;
        return false;
    }
    WorldObjectInstance* restored = nullptr;
    for (WorldObjectInstance& candidate : state.world.accessObjects()) {
        if (candidate.alive
            && candidate.kind == WorldObjectKind::Unit
            && candidate.instanceId != originalId
            && runtimeCrateTypeIs(candidate.type, "SMIN")) {
            restored = &candidate;
            break;
        }
    }
    if (restored == nullptr
        || restored->facing != expectedFacing
        || !captureRuntimeMechanismGoldenFrame(
            state,
            scenario,
            outputRoot,
            center,
            restored,
            "RestoredFacing0",
            restored == nullptr ? -1 : restored->facing,
            0,
            0,
            "",
            errorMessage
        )) {
        if (errorMessage.empty()) {
            errorMessage = "restored SMIN did not retain DeployFacing 0";
        }
        return false;
    }
    return true;
}

bool validateRuntimeMechanismGoldenToleranceContract(
    std::string& errorMessage
) {
    std::vector<std::uint32_t> baseline(400U, 0x00202020U);
    std::vector<std::uint32_t> within = baseline;
    for (std::uint32_t& pixel : within) {
        pixel += 4U;
    }
    const RuntimeMechanismGoldenPixelDiff withinDiff =
        compareRuntimeMechanismGoldenPixels(baseline, within);
    std::vector<std::uint32_t> outside = baseline;
    for (std::size_t index = 0; index < 4U; ++index) {
        outside[index] += 5U;
    }
    const RuntimeMechanismGoldenPixelDiff outsideDiff =
        compareRuntimeMechanismGoldenPixels(baseline, outside);
    if (!withinDiff.comparable
        || !withinDiff.passed
        || withinDiff.pixelsOutsideTolerance != 0U
        || !outsideDiff.comparable
        || outsideDiff.passed
        || outsideDiff.pixelsOutsideTolerance != 4U
        || std::abs(outsideDiff.pixelsOutsideToleranceRatio - 0.01)
            > 0.000001) {
        errorMessage = "Golden pixel tolerance boundary self-check failed";
        return false;
    }
    errorMessage.clear();
    return true;
}

} // namespace

int runRuntimeMechanismGoldenSmokeTest(
    const RuntimeConfig& config,
    const std::string& outputRootValue,
    const std::string& referenceRootValue
) {
    const auto fail = [](const std::string& detail) {
        std::cerr << "Mechanism Golden smoke failed: " << detail << ".\n";
        return 1;
    };
    const std::filesystem::path outputRoot =
        std::filesystem::path(outputRootValue).lexically_normal();
    if (trimAsciiLocal(outputRootValue).empty()) {
        return fail("output root is empty");
    }
    std::error_code errorCode;
    std::filesystem::create_directories(outputRoot, errorCode);
    if (errorCode) {
        return fail("could not create output root " + outputRoot.string());
    }

    auto sourceStorage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& source = *sourceStorage;
    source.config = config;
    source.report = buildRuntimeStartupReport(
        config,
        &source.world,
        &source.theater,
        &source.art,
        &source.rules,
        &source.mapFacts
    );
    source.rulesLoaded = source.report.rulesLoaded;
    source.artLoaded = source.report.artLoaded;
    source.theaterLoaded = source.report.theaterLoaded;
    source.mapFactsLoaded = !source.mapFacts.cells.empty();
    printRuntimeStartupReport(source.report);
    if (!source.report.success) {
        return fail("runtime source bundle did not load");
    }

    const bool referenceWasExplicit =
        !trimAsciiLocal(referenceRootValue).empty();
    const std::filesystem::path referenceRoot = referenceWasExplicit
        ? std::filesystem::path(referenceRootValue).lexically_normal()
        : (std::filesystem::path(source.report.localDataRootPath)
            / "reference"
            / "retail_golden"
            / "special_mechanisms");

    std::string errorMessage;
    if (!validateRuntimeMechanismGoldenToleranceContract(errorMessage)) {
        return fail(errorMessage);
    }

    std::vector<RuntimeMechanismGoldenScenario> scenarios;
    scenarios.push_back({
        "siege_chopper",
        "Siege Chopper land / deploy / undeploy / takeoff",
        "rulesmd.ini [SCHP]: DeployToLand=yes, UnloadingClass=SCHD; stock SCHP/SCHD voxel art",
        128,
        96
    });
    scenarios.push_back({
        "yuri_undeploy_delay",
        "YURI UndeployDelay",
        "rulesmd.ini [YURI]: UndeployDelay=150",
        112,
        96
    });
    scenarios.push_back({
        "yuripr_undeploy_delay",
        "YURIPR UndeployDelay",
        "rulesmd.ini [YURIPR]: UndeployDelay=75",
        112,
        96
    });
    scenarios.push_back({
        "prism_charge_turret",
        "Prism Tank IsChargeTurret phases",
        "rulesmd.ini [SREF]: TurretCount=4, IsChargeTurret=true; stock SREF turret voxel family",
        128,
        96
    });
    scenarios.push_back({
        "radiation",
        "Radiation color / intensity / radius / falloff",
        "rulesmd.ini [Radiation]: level/light delay 90, max 500, tint 1.0, color 0,255,0",
        192,
        128
    });
    scenarios.push_back({
        "slave_miner_facing",
        "Slave Miner transform facing",
        "rulesmd.ini [SMIN]/[YAREFN]: DeploysInto/UndeploysInto and DeployFacing=0; stock voxel/building art",
        192,
        144
    });

    if (!captureRuntimeSiegeChopperGolden(
            source,
            scenarios[0],
            outputRoot,
            errorMessage
        )
        || !captureRuntimeYuriUndeployGolden(
            source,
            "YURI",
            scenarios[1],
            outputRoot,
            errorMessage
        )
        || !captureRuntimeYuriUndeployGolden(
            source,
            "YURIPR",
            scenarios[2],
            outputRoot,
            errorMessage
        )
        || !captureRuntimePrismChargeGolden(
            source,
            scenarios[3],
            outputRoot,
            errorMessage
        )
        || !captureRuntimeRadiationGolden(
            source,
            scenarios[4],
            outputRoot,
            errorMessage
        )
        || !captureRuntimeSlaveMinerFacingGolden(
            source,
            scenarios[5],
            outputRoot,
            errorMessage
        )) {
        return fail(errorMessage);
    }

    bool referenceFailure = false;
    for (RuntimeMechanismGoldenScenario& scenario : scenarios) {
        if (!finalizeRuntimeMechanismGoldenScenario(
                scenario,
                outputRoot,
                referenceRoot,
                referenceWasExplicit,
                referenceFailure,
                errorMessage
            )) {
            return fail(errorMessage);
        }
    }
    if (!writeRuntimeMechanismGoldenReport(
            source,
            scenarios,
            outputRoot,
            referenceRoot,
            errorMessage
        )) {
        return fail(errorMessage);
    }

    std::size_t totalFrames = 0U;
    std::size_t totalUniqueImages = 0U;
    for (const RuntimeMechanismGoldenScenario& scenario : scenarios) {
        totalFrames += scenario.frames.size();
        totalUniqueImages += scenario.uniqueImages.size();
        std::cout << "Mechanism Golden: id=" << scenario.id
                  << " frames=" << scenario.frames.size()
                  << " uniqueImages=" << scenario.uniqueImages.size()
                  << " reference=" << scenario.referenceStatus
                  << " atlas=" << scenario.currentAtlasPath
                  << " comparison=" << scenario.comparisonPath
                  << "\n";
    }
    std::cout << "Mechanism Golden summary: mechanisms=" << scenarios.size()
              << " frames=" << totalFrames
              << " uniqueImages=" << totalUniqueImages
              << " pixelTolerance="
              << RuntimeMechanismGoldenPixelChannelTolerance
              << " outsideRatioTolerance="
              << RuntimeMechanismGoldenPixelRatioTolerance
              << " report="
              << (outputRoot / "difference_report.md").string()
              << "\n";
    return referenceFailure ? 1 : 0;
}
