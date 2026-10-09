// Real UI force-fire orders and stock Cannon actors are the only damage
// producers here. Original MIX SHP bytes supply the pixel oracle; the bridge
// mutation/frame helpers are deliberately not used to compute expectations.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeBridgeProjectileDamageSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string label = "startup", stage;
    const auto fail = [&](const std::string& why) {
        error = "Bridge projectile damage " + label + " [" + stage + "]: " + why;
        return false;
    };
    const char* filter = std::getenv("CONCORD_BRIDGE_PROJECTILE_DAMAGE_CASE");
    const char* output = std::getenv("CONCORD_BRIDGE_PROJECTILE_DAMAGE_OUTPUT");
    const std::filesystem::path root = output && *output ? output : "build/issue22_04_bridge_projectile_damage";
    std::filesystem::create_directories(root / "native-original");
    std::ofstream records(root / "impacts.csv");
    records << "case,replica,tick,shot,actor,hp,grade,overlay,frame,x,y,z,bridgeSpans\n";
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config; stock->config.playerSlots.clear(); stock->config.gameModeRulesOverride.clear();
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original startup: " + stock->report.errorMessage);
    stock->rulesLoaded = stock->artLoaded = stock->theaterLoaded = true;
    IniFile raw;
    if (!raw.loadFromFile(stock->report.rulesPath) || !stock->rules.loadFromIni(raw)) return fail("original rules");
    const int strength = std::stoi(raw.getValue("CombatDamage", "BridgeStrength", "0"));
    const int damage = std::stoi(raw.getValue("120mm", "Damage", "0"));
    const auto ratio = [&](const char* key) {
        const std::string value = raw.getValue("AudioVisual", key, "");
        return value.empty() ? -1.0 : std::stod(value) / (value.find('%') == std::string::npos ? 1.0 : 100.0);
    };
    const double yellow = ratio("ConditionYellow"), red = ratio("ConditionRed");
    const auto* cannon = stock->rules.findWeaponType("120mm");
    const auto* tank = stock->rules.findUnitType("Vehicle", "HTNK");
    if (!tank || !cannon || strength != 1500 || damage != 90 || stock->rules.getBridgeStrength() != strength
        || tank->primaryWeapon != "120mm" || tank->strength != 400 || cannon->damage != damage || std::max(1,cannon->burst) != 1
        || cannon->projectile != "Cannon" || cannon->warhead != "AP"
        || raw.getValue("120mm", "Projectile", "") != "Cannon" || raw.getValue("120mm", "Warhead", "") != "AP"
        || yellow != .5 || red != .25
        || stock->rules.getSpecialMechanismRules().conditionYellow != yellow
        || stock->rules.getSpecialMechanismRules().conditionRed != red)
        return fail("raw HTNK/120mm/Cannon/AP/BridgeStrength/Condition rules mismatch");
    const auto rulesHash = stock->rules.getDetailRulesHash();
    // Original art has three light variants and three heavy variants. The
    // requested deterministic implementation uses the first in each bank.
    // ConditionYellow/Red are existing rules reused for these health bands;
    // this test does not assert retail's unverified random variant probability.
    const auto grade = [&](int hp) { return hp <= 0 ? 3 : hp <= strength * red ? 2 : hp <= strength * yellow ? 1 : 0; };
    const auto damageOffset = [](int band) { return band == 2 ? 4 : band == 1 ? 1 : 0; };
    const auto nativeType = [&](const std::string& id) -> const OverlayType* {
        for (const auto& type : stock->rules.getOverlayTypes()) if (type.id == id) return &type;
        return nullptr;
    };
    const auto nativeIndex = [&](const std::string& id) {
        const auto& types = stock->rules.getOverlayTypes();
        for (std::size_t ordinal = 0; ordinal < types.size(); ++ordinal)
            if (types[ordinal].id == id) return static_cast<int>(ordinal);
        return -999;
    };
    // OverlayPack is the zero-based insertion order. The original numeric
    // INI labels are one-based and have gaps; listIndex retains those labels.
    const auto* rawOverlays = raw.getSection("OverlayTypes");
    if (!rawOverlays) return fail("original OverlayTypes section unavailable");
    std::vector<IniFile::KeyValue> originalOverlayOrder;
    std::set<std::string> originalOverlayIds;
    for (const auto& entry : *rawOverlays)
        if (!entry.second.empty() && originalOverlayIds.insert(toLowerAsciiLocal(entry.second)).second)
            originalOverlayOrder.push_back(entry);
    const auto& overlayTypes = stock->rules.getOverlayTypes();
    if (originalOverlayOrder.size() != overlayTypes.size()) return fail("raw/native OverlayTypes ordinal count differs");
    for (std::size_t ordinal = 0; ordinal < originalOverlayOrder.size(); ++ordinal)
        if (originalOverlayOrder[ordinal].second != overlayTypes[ordinal].id)
            return fail("raw/native OverlayTypes insertion order differs at " + std::to_string(ordinal));
    struct OriginalOverlay { const char* id; int ordinal; const char* label; };
    for (const auto& expected : {OriginalOverlay{"BRIDGE1",24,"25"},OriginalOverlay{"BRIDGE2",25,"26"},
            OriginalOverlay{"BRIDGEB1",237,"241"},OriginalOverlay{"BRIDGEB2",238,"242"},
            OriginalOverlay{"LOBRDG01",74,"77"},OriginalOverlay{"LOBRDB01",205,"209"}}) {
        const auto* type = nativeType(expected.id);
        if (!type || nativeIndex(expected.id) != expected.ordinal
            || originalOverlayOrder[expected.ordinal].first != expected.label
            || type->listIndex != std::stoi(expected.label))
            return fail("original bridge ordinal/INI-label contract differs: " + std::string(expected.id));
    }
    const auto lowId = [](bool wood, int offset) {
        std::ostringstream text; text << (wood ? "LOBRDG" : "LOBRDB")
            << std::setw(2) << std::setfill('0') << offset + 1; return text.str();
    };
    for (bool wood : {false,true}) for (int offset = 0; offset < 28; ++offset)
        if (nativeIndex(lowId(wood,offset)) != (wood ? 74 : 205) + offset)
            return fail("original low bridge ordinal family is not contiguous: " + lowId(wood,offset));
    const auto bridgeArt = [](const TerrainCell& cell) {
        return cell.hasOverlay && (cell.overlayImage == "BRIDGE" || cell.overlayImage == "BRIDGB"
            || cell.overlayType.rfind("LOBRDG", 0) == 0 || cell.overlayType.rfind("LOBRDB", 0) == 0);
    };
    const auto fieldEqual = [](const TerrainCell& a, const TerrainCell& b) {
        return a.hasOverlay == b.hasOverlay && a.overlayIndex == b.overlayIndex && a.overlayFrame == b.overlayFrame
            && a.overlayType == b.overlayType && a.overlayImage == b.overlayImage && a.tileIndex == b.tileIndex
            && a.subTileIndex == b.subTileIndex && a.level == b.level;
    };
    const auto facade = [](RuntimeWindowState& s) {
        s.economy.owner = "Americans"; s.config.playerCountry = "Americans"; s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority authority; authority.owner = authority.country = "Americans";
        authority.canonicalOwner = "americans"; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
    };
    const auto prepare = [&](RuntimeWindowState& s, int hz) {
        s.config.fixedTickHz = hz; s.config.gameMode = "Battle"; s.config.skirmishLoadSavePath.clear();
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = false;
        s.config.shortGame = s.config.crates = s.config.superweapons = false;
        s.campaignMode = false; s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.destroyableBridges = true;
        s.rules = stock->rules; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.tick = 100; facade(s);
    };
    const auto clearTile = [&](const RuntimeWindowState& s, bool water) {
        for (int index = 0; index < static_cast<int>(s.theater.getTotalTileSlots()); ++index) {
            TerrainCell cell; cell.tileIndex = index; cell.subTileIndex = 0;
            const auto info = runtimeTerrainInfoForCell(s, cell);
            if ((water ? info.water : info.passable && !info.water)
                && !info.shore && !info.cliff && !info.bridge && !info.ramp) return index;
        }
        return -1;
    };
    const auto initialize = [&](RuntimeWindowState& s) {
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        facade(s);
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick;
        advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool canonical) {
        std::ostringstream text, normalized; IniFile base, exact;
        std::string ordinaryPayload, canonicalPayload;
        bool haveExact = false;
        // Keep both complete payloads and audit the original expired actors.
        // The source history is never removed, delayed, or normalized here.
        const auto diagnose = [&](const std::string& result) {
            std::ostringstream diagnostic;
            diagnostic << std::setprecision(std::numeric_limits<double>::max_digits10)
                << "Bridge checkpoint " << label << " stage=" << stage << " tick=" << s.tick
                << " canonical=" << canonical << " live=" << s.projectiles.size()
                << " expired=" << s.recentExpiredProjectiles.size() << " result=" << result << '\n';
            std::set<int> actorIds;
            for (const auto& projectile : s.projectiles) actorIds.insert(projectile.actorId);
            std::string firstInvalid;
            for (std::size_t index = 0; index < s.recentExpiredProjectiles.size(); ++index) {
                const auto& projectile = s.recentExpiredProjectiles[index];
                const auto serialized = serializeRuntimeProjectileEffect(projectile);
                RuntimeProjectileEffect parsed; std::string payload;
                const bool sourceParses = parseRuntimeProjectileEffect(serialized,parsed,runtimeProjectileSaveSchemaV6,true);
                const bool sourceExact = sourceParses && serializeRuntimeProjectileEffect(parsed) == serialized;
                const bool duplicate = !actorIds.insert(projectile.actorId).second;
                const bool payloadDecoded = haveExact && iniHexDecode(exact.getValue("MultiplayerTransientEffects",
                    "RecentExpired"+std::to_string(index),""),payload);
                const bool payloadExact = payloadDecoded && payload == serialized;
                std::ostringstream row;
                row << std::setprecision(std::numeric_limits<double>::max_digits10)
                    << "expired[" << index << "] actor=" << projectile.actorId << " duplicate=" << duplicate
                    << " schema=6 allowExpired=true fields=" << splitRuntimeSaveFields(serialized).size()
                    << " sourceParses=" << sourceParses << " sourceExact=" << sourceExact
                    << " payloadDecoded=" << payloadDecoded << " payloadExact=" << payloadExact
                    << " lifecycle=" << static_cast<int>(projectile.lifecycle) << ':' << projectile.lifecycleState
                    << " spawn=" << projectile.spawnTick << " impact=" << projectile.impactTick
                    << " total=" << projectile.totalTicks << " elapsed=" << projectile.elapsedTicks
                    << " remaining=" << projectile.remainingTicks << " progress=" << projectile.progress
                    << " XYZ=" << projectile.currentX << ',' << projectile.currentY << ',' << projectile.currentZ
                    << " weapon=" << projectile.weaponId << " projectile=" << projectile.projectileId;
                diagnostic << row.str() << '\n';
                if (firstInvalid.empty() && (!sourceExact || duplicate
                    || projectile.lifecycle != RuntimeProjectileLifecycleState::Expired || (haveExact && !payloadExact)))
                    firstInvalid = row.str();
            }
            std::string safeStage = stage;
            for (char& c : safeStage)
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) c = '_';
            const auto prefix = root / (label+"-"+safeStage+"-tick"+std::to_string(s.tick));
            std::ofstream ordinaryDump(prefix.string()+"-ordinary-v14.ini",std::ios::binary);
            ordinaryDump << ordinaryPayload;
            if (canonical) {
                std::ofstream canonicalDump(prefix.string()+"-canonical-mp.ini",std::ios::binary);
                canonicalDump << (canonicalPayload.empty() ? text.str() : canonicalPayload);
            }
            std::ofstream details(prefix.string()+(canonical?"-canonical-checkpoint.txt":"-ordinary-checkpoint.txt"),std::ios::binary);
            details << diagnostic.str();
            if (!firstInvalid.empty() || result != "restored") std::cout << diagnostic.str();
            return firstInvalid;
        };
        const auto reject = [&](const char* point) {
            const std::string cause = std::string(point)+(error.empty()?"":": "+error);
            const std::string expired = diagnose(cause);
            error = cause+(expired.empty()?"":"; "+expired);
            return std::unique_ptr<RuntimeWindowState>{};
        };
        if (!writeRuntimeStateToStream(s,text,error)) return reject("ordinary write");
        ordinaryPayload = text.str();
        if (!base.loadFromString(ordinaryPayload)) return reject("ordinary INI parse");
        if (canonical) {
            if (!writeRuntimeMultiplayerSnapshotExtension(s,text,error) || !exact.loadFromString(text.str())
                || !writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error)) return reject("canonical write");
            canonicalPayload = normalized.str();
            if (!exact.loadFromString(canonicalPayload)) return reject("canonical INI parse");
            haveExact = true;
        }
        auto copy = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy, s, false);
        RuntimeSnapshotActiveStateScope active(*copy);
        if (!materializeRuntimeLoadStateFromIni(*copy, base, error)
            || (canonical && !materializeRuntimeMultiplayerSnapshotExtension(*copy, exact, error))) return reject("checkpoint materialize");
        const std::string invalidExpired = diagnose("restored");
        if (canonical && !invalidExpired.empty()) { error = "canonical expired record round-trip: "+invalidExpired; return std::unique_ptr<RuntimeWindowState>{}; }
        copy->runtimeSoundsEnabled = copy->runtimePreferencePersistenceEnabled = false;
        copy->visionEnabled = false; facade(*copy); return copy;
    };
    struct Scene {
        std::unique_ptr<RuntimeWindowState> state;
        bool high = true, wood = false, alongX = true, actualMap = false;
        RuntimeCellPoint target{32,32}; int land = -1, shooter = 990001, hutCount = 0, rawHutCount = 0;
        std::vector<TerrainCell> original, originalFarTerrain; std::unordered_map<int,int> initialHealth;
        std::string mapName;
    };
    const auto addShooter = [&](Scene& scene, RuntimeCellPoint point) {
        auto& s = *scene.state;
        auto* object=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"HTNK",point);
        if (!object || object->health!=tank->strength) return fail("original HTNK creation/full health unavailable");
        scene.shooter=object->instanceId;object->mission="Sleep";
        object->facing=object->turretFacing=runtimeFacingFromDelta(scene.target.x-point.x,scene.target.y-point.y);
        refreshRuntimeObjectOccupancyAfterLocalChange(s);return true;
    };
    const auto synthetic = [&](bool high, bool wood, bool alongX, int hz) {
        Scene scene; scene.high = high; scene.wood = wood; scene.alongX = alongX;
        scene.state = std::make_unique<RuntimeWindowState>(); auto& s = *scene.state;
        seedRuntimeLoadScratch(s, *stock, false); prepare(s, hz);
        scene.land = clearTile(s, false); const int water = clearTile(s, true);
        if (scene.land < 0 || water < 0) { fail("original clear/water tiles unavailable"); return scene; }
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.mapFacts = {}; s.mapFactsLoaded = false;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        const std::string id = high ? (wood ? (alongX ? "BRIDGEB1" : "BRIDGEB2") : (alongX ? "BRIDGE1" : "BRIDGE2"))
            : lowId(wood, alongX ? 0 : 9);
        const auto* type = nativeType(id);
        if (!type || type->image.empty()) { fail("original overlay definition unavailable: " + id); return scene; }
        for (int y = 22; y <= 42; ++y) for (int x = 22; x <= 42; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = scene.land; cell.subTileIndex = 0;
            const int axial = alongX ? x : y, lane = (alongX ? y : x) - 32;
            if (axial >= 28 && axial <= 36 && lane >= 0 && lane < 3) {
                cell.tileIndex = water;
                if (!high || lane == 0) {
                    cell.hasOverlay = true; cell.overlayType = type->id; cell.overlayImage = type->image;
                    cell.overlayIndex = nativeIndex(type->id); cell.overlayFrame = high ? (alongX ? 0 : 9) : lane;
                }
            }
            s.world.accessTerrainCells().push_back(std::move(cell));
        }
        initialize(s); (void)addShooter(scene, alongX ? RuntimeCellPoint{32,29} : RuntimeCellPoint{29,32});
        return scene;
    };
    const auto health = [&](const RuntimeWindowState& s, RuntimeCellPoint anchor) {
        const auto found = s.bridgeHealthByCell.find(runtimeCellKey(anchor.x, anchor.y));
        return found == s.bridgeHealthByCell.end() ? strength : found->second;
    };
    const auto terrain = [](const RuntimeWindowState& s, RuntimeCellPoint point) -> const TerrainCell* {
        const auto found = s.terrainByCell.find(runtimeCellKey(point.x, point.y));
        return found == s.terrainByCell.end() ? nullptr : found->second;
    };
    // Lookup-only controls: a high anchor owns exactly its two transverse
    // lanes. Native disconnected cap art owns no deck; radius-one lookup may
    // still find the existing adjacent intact segment, without damaging it.
    int sideOwnershipChecks = 0, capOwnershipChecks = 0, radialOwnershipChecks = 0;
    const auto owns = [&](const std::optional<RuntimeBridgeDamageTarget>& actual,
        RuntimeCellPoint anchor, bool alongX, bool wood) {
        if (!actual || actual->anchor != anchor || !actual->high || actual->alongX != alongX
            || actual->material != (wood ? RuntimeBridgeMaterial::Wood : RuntimeBridgeMaterial::Concrete)
            || actual->physicalCells.size() != 3) return false;
        for (int lane = 0; lane < 3; ++lane)
            if (actual->physicalCells[lane] != RuntimeCellPoint{anchor.x+(alongX?0:lane),anchor.y+(alongX?lane:0)})
                return false;
        return true;
    };
    label = "native-high-target-ownership"; stage = "original transverse lanes / lookup only";
    for (bool wood : {false,true}) for (bool alongX : {true,false}) {
        Scene scene = synthetic(true,wood,alongX,30);
        if (!error.empty() || !scene.state || scene.land < 0) return fail("ownership fixture: " + error);
        auto& s = *scene.state;
        for (int lane : {1,2}) {
            const RuntimeCellPoint point{scene.target.x+(alongX?0:lane),scene.target.y+(alongX?lane:0)};
            if (!owns(runtimeBridgeDamageTargetAtCell(s,point,0),scene.target,alongX,wood))
                return fail("R0 side lane did not retain its native anchor/material/three physical cells");
            ++sideOwnershipChecks;
            if (!owns(runtimeBridgeDamageTargetAtCell(s,point,1),scene.target,alongX,wood))
                return fail("R1 displaced a present native R0 side-lane owner");
            ++radialOwnershipChecks;
        }
    }
    for (int frame : {7,8,16,17}) {
        const bool alongX = frame < 9;
        Scene scene = synthetic(true,false,alongX,30);
        if (!error.empty() || !scene.state || scene.land < 0) return fail("native cap fixture: " + error);
        auto& s = *scene.state;
        for (auto& cell : s.world.accessTerrainCells())
            if (cell.x == scene.target.x && cell.y == scene.target.y) cell.overlayFrame = frame;
        const RuntimeCellPoint side{scene.target.x+(alongX?0:1),scene.target.y+(alongX?1:0)};
        if (runtimeBridgeDamageTargetAtCell(s,side,0)) return fail("native disconnected cap retained R0 side-lane ownership frame="+std::to_string(frame));
        ++capOwnershipChecks;
        const RuntimeCellPoint previous{scene.target.x-(alongX?1:0),scene.target.y-(alongX?0:1)};
        if (!owns(runtimeBridgeDamageTargetAtCell(s,side,1),previous,alongX,false))
            return fail("R1 existing negative-axis radial fallback differs at native cap frame="+std::to_string(frame));
        ++radialOwnershipChecks;
    }
    int passed = 0, attempted = 0, shots = 0, naturalFlightTicks = 0, pixelChecks = 0, saves = 0, snapshots = 0, realMapCases = 0;
    std::set<std::string> nativeSources;
    std::set<std::string> copiedNative;
    std::map<std::string, RuntimeShpFrameImage> decoded;
    const auto extensionFor=[](const RuntimeWindowState& s) {
        std::string value=s.theater.getTileExtension();if (!value.empty()&&value.front()=='.') value.erase(value.begin());return value;
    };
    const auto nativeFrame = [&](RuntimeWindowState& s, const std::string& image, int frame, RuntimeShpFrameImage& result) {
        const std::string extension = extensionFor(s);
        const std::string filename = toLowerAsciiLocal(image) + "." + extension;
        const std::string key = filename + ":" + std::to_string(frame);
        const auto known = decoded.find(key); if (known != decoded.end()) { result = known->second; return true; }
        const std::filesystem::path copy = root / "native-original" / filename;
        std::string nativeSource;
        if (copiedNative.insert(filename).second) {
            const std::string assetsRoot = runtimeAssetRootPath(s.config);
            const std::string theaterName = extension == "des" ? "desert" : extension == "sno" ? "snow"
                : extension == "urb" ? "urban" : extension == "ubn" ? "urbann" : extension == "lun" ? "lunar" : "temperat";
            const std::string originalIso = extension == "tem" ? "isotemp.mix"
                : extension == "sno" ? "isosnow.mix" : "iso" + extension + ".mix";
            std::vector<std::uint8_t> bytes;
            for (const std::string directory : {"ra2md", "ra2"}) {
                for (const std::string archiveName : {"iso" + extension + "md.mix", originalIso,
                        theaterName + "md.mix", theaterName + ".mix", extension + ".mix"}) {
                    const std::filesystem::path archivePath = std::filesystem::path(assetsRoot) / directory / archiveName;
                    if (!std::filesystem::is_regular_file(archivePath)) continue;
                    MixArchive archive; if (!archive.loadFromPath(archivePath)) continue;
                    const MixEntry* entry = archive.findEntryByName(filename); if (!entry) continue;
                    if (!archive.readEntryData(*entry, bytes, error)) return fail("original MIX read: " + error);
                    nativeSource = pathString(archivePath) + "!" + filename; break;
                }
                if (!nativeSource.empty()) break;
            }
            if (nativeSource.empty()) return fail("original MIX SHP not found: " + filename);
            std::ofstream stream(copy, std::ios::binary); stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (!stream) return fail("original byte snapshot write: " + pathString(copy));
            nativeSources.insert(nativeSource);
        }
        if (!decodeRuntimeShpFrameFile(pathString(copy), frame, result, error)) return fail("original SHP decode " + key + ": " + error);
        decoded.emplace(key, result); return true;
    };
    constexpr int width = 512, height = 384;
    const RECT viewport{0,0,width,height};
    HDC dc = CreateCompatibleDC(nullptr); BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height; bitmapInfo.bmiHeader.biPlanes = 1; bitmapInfo.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap || !bits) { if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); return fail("DIB creation"); }
    const HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    struct Cleanup { HDC dc; HBITMAP bitmap; HGDIOBJ previous; ~Cleanup() { SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc); } } cleanup{dc,bitmap,previousBitmap};
    auto* pixels = static_cast<std::uint32_t*>(bits);
    const auto capture = [&]() { GdiFlush(); std::vector<std::uint32_t> value(pixels,pixels+width*height);
        for (auto& color : value) color &= 0xffffffU; return value; };
    const auto nativePixels = [&](Scene& scene, RuntimeWindowState& s, const std::string& checkpointName) {
        RuntimeMapRenderContext context; context.rules = &s.rules; context.art = &s.art; context.theater = &s.theater;
        context.assetRootPath = runtimeAssetRootPath(s.config); context.palettePath = runtimePalettePath(s.config,s.report.map);
        context.structureRootPath = runtimeStructureRootPath(s.config,s.report.map);
        context.surfacePixels = pixels; context.surfaceWidth = width; context.surfaceHeight = height;
        context.shadowsEnabled = false;
        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = 60; camera.tileHeight = 30;
        camera.worldOffsetX = (scene.target.x-scene.target.y)*30-width/2;
        camera.worldOffsetY = (scene.target.x+scene.target.y)*15-height/2;
        // Isolate the target and four original neighboring anchors on a native flat
        // terrain probe. Their type/frame/lane come from the real impact,
        // already checked against the independent grade oracle below.
        GameWorld probe = s.world; probe.accessObjects().clear(); probe.accessTerrainObjects().clear(); probe.accessSmudges().clear();
        probe.accessTerrainCells().clear();
        for (int y=scene.target.y-7;y<=scene.target.y+7;++y) for (int x=scene.target.x-7;x<=scene.target.x+7;++x) {
            TerrainCell cell; cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=scene.land;cell.subTileIndex=0;
            const int distance=scene.alongX?x-scene.target.x:y-scene.target.y;
            const int lane=scene.alongX?y-scene.target.y:x-scene.target.x;
            if (distance>=-2 && distance<=2 && lane>=0 && lane<(scene.high?1:3)) {
                const auto* actual=terrain(s,{x,y}); if (!actual) return fail("native art probe actual cell missing");
                cell=*actual;cell.tileIndex=scene.land;cell.subTileIndex=0;cell.level=0;
            }
            probe.accessTerrainCells().push_back(std::move(cell));
        }
        GameWorld bare = probe;
        for (auto& cell : bare.accessTerrainCells()) { cell.hasOverlay=false;cell.overlayIndex=-1;cell.overlayType.clear();cell.overlayImage.clear(); }
        RuntimeMapRenderCache bareCache, referenceCache, actualCache;
        std::fill(pixels,pixels+width*height,0x203040U);auto fixed=camera;
        const auto bareStats=drawRuntimeMap(dc,viewport,bare,context,fixed,bareCache);
        if (!bareStats.rendered || !bareStats.texturedTerrainCells) return fail("native bare terrain draw");
        auto expected=capture(); std::vector<RuntimePaletteColor> palette;std::string palettePath;
        if (!loadRuntimePaletteResource(context,referenceCache,"iso"+extensionFor(s)+".pal",palette,palettePath,error)
            || palette.size()!=256) return fail("original ISO palette: "+error);
        for (const auto& cell:probe.getTerrainCells()) {
            if (!bridgeArt(cell)) continue;
            if (!scene.high && (cell.overlayType==lowId(scene.wood,26)||cell.overlayType==lowId(scene.wood,27))) continue;
            RuntimeShpFrameImage original; if (!nativeFrame(s,cell.overlayImage,cell.overlayFrame,original)) return false;
            const int cx=(cell.x-cell.y)*30-camera.worldOffsetX,cy=(cell.x+cell.y)*15-camera.worldOffsetY;
            const int left=cx-original.globalWidth/2+original.offsetX-(scene.high?30:0);
            const int top=cy-original.globalHeight/2+original.offsetY-(scene.high?16:0);
            for (int y=0;y<original.height;++y) for (int x=0;x<original.width;++x) {
                const auto index=original.pixels[static_cast<std::size_t>(y)*original.width+x];if (!index) continue;
                const int px=left+x,py=top+y;if (px<0||py<0||px>=width||py>=height) continue;
                const auto& color=palette[index];expected[static_cast<std::size_t>(py)*width+px]
                    =(static_cast<std::uint32_t>(color.r)<<16)|(static_cast<std::uint32_t>(color.g)<<8)|color.b;
            }
        }
        for (int warm=0;warm<2;++warm) {
            std::fill(pixels,pixels+width*height,0x203040U);fixed=camera;
            const auto stats=drawRuntimeMap(dc,viewport,probe,context,fixed,actualCache);const auto actual=capture();++pixelChecks;
            if (!stats.rendered||actual!=expected) {
                writeRuntimeSmokeBmp(pathString(root/(label+"-"+checkpointName+"-actual.bmp")),actual.data(),width,height,error);
                writeRuntimeSmokeBmp(pathString(root/(label+"-"+checkpointName+"-expected.bmp")),expected.data(),width,height,error);
                const auto difference=std::mismatch(actual.begin(),actual.end(),expected.begin());
                return fail("original SHP complete-map pixels differ at "+checkpointName+" warm="+std::to_string(warm)
                    +" pixel="+std::to_string(difference.first-actual.begin()));
            }
            if (!warm && (scene.actualMap || (s.config.fixedTickHz==30&&scene.high==scene.alongX)))
                writeRuntimeSmokeBmp(pathString(root/(label+"-"+checkpointName+"-native.bmp")),actual.data(),width,height,error);
        }
        return true;
    };
    const auto check = [&](Scene& scene, RuntimeWindowState& s, int expectedHealth) {
        if (s.rules.getDetailRulesHash()!=rulesHash || health(s,scene.target)!=expectedHealth)
            return fail("native rules or exact health differ; expected="+std::to_string(expectedHealth)
                +" actual="+std::to_string(health(s,scene.target)));
        const int band=grade(expectedHealth), bank=scene.alongX?0:9;
        for (const auto& before:scene.original) {
            const auto* actual=terrain(s,{before.x,before.y});if (!actual) return fail("bridge cell disappeared");
            const int distance=scene.alongX?before.x-scene.target.x:before.y-scene.target.y;
            const int lane=scene.alongX?before.y-scene.target.y:before.x-scene.target.x;
            const bool target=distance==0&&lane>=0&&lane<(scene.high?1:3);
            const bool adjacent=std::abs(distance)==1&&lane>=0&&lane<(scene.high?1:3);
            if (!target && !(band==3&&adjacent)) {
                if (!fieldEqual(*actual,before)) return fail("unhit bridge art/terrain changed at "
                    +std::to_string(before.x)+","+std::to_string(before.y));
                continue;
            }
            if (scene.high) {
                if (target&&band==3) { if (actual->hasOverlay) return fail("lethal target retained high deck"); }
                else {
                    const int frame=target?bank+damageOffset(band):distance<0?(scene.alongX?8:16):(scene.alongX?7:17);
                    if (!actual->hasOverlay||actual->overlayIndex!=before.overlayIndex||actual->overlayType!=before.overlayType
                        || actual->overlayImage!=before.overlayImage||actual->overlayFrame!=frame)
                        return fail("high grade/native cap differs at "+std::to_string(before.x)+","+std::to_string(before.y)
                            +" expectedFrame="+std::to_string(frame)+" actual="+std::to_string(actual->overlayFrame));
                }
            } else {
                // Original low bridges legitimately mix shape variants across
                // their three lanes. Only a real impact canonicalizes the hit
                // segment; initial and mid-flight saves retain the native row.
                if (target && expectedHealth == strength) {
                    if (!fieldEqual(*actual,before)) return fail("unhit original low-bridge lane changed at "
                        +std::to_string(before.x)+","+std::to_string(before.y));
                    continue;
                }
                const int offset=target?(band==3?(scene.alongX?26:27):bank+damageOffset(band))
                    :distance<0?(scene.alongX?8:16):(scene.alongX?7:17);
                const std::string id=lowId(scene.wood,offset);
                if (!actual->hasOverlay||actual->overlayIndex!=nativeIndex(id)||actual->overlayType!=id||actual->overlayImage!=id
                    || actual->overlayFrame!=before.overlayFrame) return fail("low grade/native 3-lane cap differs at "
                        +std::to_string(before.x)+","+std::to_string(before.y)+" expected="+id+"/"+std::to_string(before.overlayFrame)
                        +" actual="+actual->overlayType+"/"+std::to_string(actual->overlayFrame));
            }
        }
        for (const auto& before : scene.originalFarTerrain) {
            const auto* actual = terrain(s,{before.x,before.y});
            if (!actual || !fieldEqual(*actual,before)) return fail("unhit axial +/-2 original terrain changed at "
                +std::to_string(before.x)+","+std::to_string(before.y));
        }
        const int targetKey=runtimeCellKey(scene.target.x,scene.target.y);
        for (const auto& [key,value]:s.bridgeHealthByCell) {
            if (key==targetKey) continue;
            const auto before=scene.initialHealth.find(key);
            if (before==scene.initialHealth.end()||before->second!=value) return fail("unhit neighboring bridge health changed key="+std::to_string(key));
        }
        for (const auto& [key,value]:scene.initialHealth) {
            if (key==targetKey) continue;
            const auto after=s.bridgeHealthByCell.find(key);
            if (after==s.bridgeHealthByCell.end()||after->second!=value) return fail("unhit saved bridge health disappeared");
        }
        return true;
    };
    const auto run = [&](Scene scene,const std::string& name) {
        label=name; if (filter&&*filter&&label.find(filter)==std::string::npos) return true;
        ++attempted; stage="initial native map/bridge";
        if (!error.empty()||!scene.state||scene.land<0) return fail("fixture: "+error);
        auto& s=*scene.state; RuntimeSnapshotActiveStateScope active(s);
        scene.initialHealth=s.bridgeHealthByCell;
        for (const auto& cell:s.world.getTerrainCells()) if (bridgeArt(cell)) scene.original.push_back(cell);
        for (int side : {-2,2}) for (int lane = 0; lane < 3; ++lane) {
            const auto* cell = terrain(s,{scene.target.x+(scene.alongX?side:lane),scene.target.y+(scene.alongX?lane:side)});
            if (cell) scene.originalFarTerrain.push_back(*cell);
        }
        if (scene.original.empty()||!terrain(s,scene.target)) return fail("native connected bridge inventory unavailable");
        std::unique_ptr<RuntimeWindowState> ordinary,exact;
        bool flightSaved=false,partialSaved=false;
        int previousGrade=-1, count=0, caseFlight=0;
        const auto replicas=[&]() { return std::vector<std::pair<std::string,RuntimeWindowState*>>{
            {"original",&s},{"ordinary",ordinary.get()},{"canonical",exact.get()}}; };
        if (!check(scene,s,strength)||!nativePixels(scene,s,"intact")) return false;
        previousGrade=0;
        while (count*damage<strength) {
            stage="UI force-fire / native Cannon launch";
            for (auto [replica,q]:replicas()) if (q) {
                RuntimeSnapshotActiveStateScope current(*q);facade(*q);q->selectedObjectIds={scene.shooter};
                const bool dispatched = dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell,scene.target);
                if (!dispatched || q->lastCommand.acceptedCount!=1 || q->lastCommand.verb!="force-fire-bridge") {
                    const auto* shooter = findRuntimeObjectByIdIncludingInactive(*q,scene.shooter);
                    std::ostringstream detail;
                    detail << replica << " real UI force-fire rejected: dispatched=" << dispatched
                        << " accepted=" << q->lastCommand.acceptedCount << " rejected=" << q->lastCommand.rejectedCount
                        << " verb=" << q->lastCommand.verb << " destroyable=" << q->destroyableBridges
                        << " selected=" << q->selectedObjectIds.size() << " localOwner=" << q->economy.owner
                        << " sourceId=" << scene.shooter << " sourceOwner=" << (shooter?shooter->owner:"missing")
                        << " sourceCell=" << (shooter?shooter->x:-1) << ',' << (shooter?shooter->y:-1)
                        << " sourceAlive=" << (shooter&&shooter->alive) << " sourceOnBridge=" << (shooter&&shooter->onBridge)
                        << " targetCell=" << scene.target.x << ',' << scene.target.y << " status=" << q->interactionStatus;
                    return fail(detail.str());
                }
            }
            int actor=-1; RuntimeProjectileEffect launch;
            const int beforeHealth=std::max(0,strength-count*damage);
            for (int tick=0;tick<s.config.fixedTickHz*12&&actor<0;++tick) {
                for (auto [replica,q]:replicas()) if (q) step(*q);
                for (const auto& projectile:s.projectiles) if (projectile.attackerObjectId==scene.shooter&&projectile.weaponId=="120mm") {
                    actor=projectile.actorId;launch=projectile;break;
                }
                if (health(s,scene.target)!=beforeHealth) return fail("bridge health changed before observing real travelling Cannon");
            }
            if (actor<0||launch.damage!=damage||launch.projectileId!="Cannon"||launch.warheadId!="AP"
                ||launch.toCell!=scene.target||!launch.ag) return fail("original Cannon actor not launched / identity mismatch");
            for (auto [replica,q]:replicas()) if (q) {
                RuntimeSnapshotActiveStateScope current(*q);q->selectedObjectIds={scene.shooter};
                if (!dispatchRuntimeSelectionCommandFromUi(*q,RuntimeMultiplayerGameplayCommandType::Stop)) return fail(replica+" UI Stop rejected");
            }
            stage="natural projectile impact / local health and native grade";
            const int expectedHealth=std::max(0,strength-(count+1)*damage);
            bool impacted=false;
            for (int tick=0;tick<s.config.fixedTickHz*12&&!impacted;++tick) {
                for (auto [replica,q]:replicas()) if (q) step(*q);
                const auto travelling=std::find_if(s.projectiles.begin(),s.projectiles.end(),[&](const RuntimeProjectileEffect& p){return p.actorId==actor;});
                for (auto [replica,q]:replicas()) if (q&&q!=&s) {
                    const auto other=std::find_if(q->projectiles.begin(),q->projectiles.end(),[&](const RuntimeProjectileEffect& p){return p.actorId==actor;});
                    if ((travelling==s.projectiles.end())!=(other==q->projectiles.end())) return fail(replica+" Cannon lifecycle differs during continuation");
                    if (travelling!=s.projectiles.end() && (other->elapsedTicks!=travelling->elapsedTicks
                        ||std::abs(other->currentX-travelling->currentX)>1e-9||std::abs(other->currentY-travelling->currentY)>1e-9
                        ||std::abs(other->currentZ-travelling->currentZ)>1e-9)) return fail(replica+" real Cannon XYZ differs during continuation");
                }
                if (travelling!=s.projectiles.end()) {
                    ++caseFlight;++naturalFlightTicks;
                    if (travelling->elapsedTicks>=1&&!flightSaved) {
                        stage="ordinary/canonical actual mid-flight save";
                        ordinary=checkpoint(s,false);exact=checkpoint(s,true);
                        if (!ordinary||!exact) return fail("mid-flight restore: "+error);
                        ++saves;++snapshots;flightSaved=true;
                        for (auto [replica,q]:replicas()) if (q) {
                            if (!check(scene,*q,beforeHealth)) return false;
                            const auto copy=std::find_if(q->projectiles.begin(),q->projectiles.end(),[&](const RuntimeProjectileEffect& p){return p.actorId==actor;});
                            if (copy==q->projectiles.end()||copy->currentX!=travelling->currentX||copy->currentY!=travelling->currentY
                                ||copy->currentZ!=travelling->currentZ||copy->damage!=damage) return fail(replica+" actual Cannon XYZ/damage lost at restore");
                        }
                        stage="natural restored projectile impact";
                    }
                }
                impacted=health(s,scene.target)!=beforeHealth;
                for (auto [replica,q]:replicas()) if (q&&health(*q,scene.target)!=health(s,scene.target))
                    return fail(replica+" actual restored projectile impact timing/health differ");
            }
            if (!impacted) return fail("natural Cannon never damaged intended bridge; targetXYZ="
                +std::to_string(launch.targetX)+","+std::to_string(launch.targetY)+","+std::to_string(launch.targetZ));
            ++count;++shots;
            for (auto [replica,q]:replicas()) if (q) {
                if (!check(scene,*q,expectedHealth)) return false;
                const auto* cell=terrain(*q,scene.target);
                records<<label<<','<<replica<<','<<q->tick<<','<<count<<','<<actor<<','<<expectedHealth<<','<<grade(expectedHealth)
                    <<','<<cell->overlayType<<','<<cell->overlayFrame<<','<<launch.targetX<<','<<launch.targetY<<','<<launch.targetZ
                    <<','<<q->bridgeRepairSpansByHut.size()<<'\n';
            }
            const int band=grade(expectedHealth);
            if (band!=previousGrade) {
                for (auto [replica,q]:replicas()) if (q&&!nativePixels(scene,*q,replica+"-grade"+std::to_string(band))) return false;
                previousGrade=band;
            }
            if (band==1&&!partialSaved) {
                stage="ordinary/canonical partial bridge save and original frame restore";
                ordinary=checkpoint(s,false);exact=checkpoint(s,true);
                if (!ordinary||!exact) return fail("partial restore: "+error);
                ++saves;++snapshots;partialSaved=true;
                for (auto [replica,q]:replicas()) if (q&&(!check(scene,*q,expectedHealth)
                    ||!nativePixels(scene,*q,replica+"-partial-restored"))) return false;
            }
        }
        stage="post-impact drain / preserved distant bridge segments";
        for (int tick=0;tick<s.config.fixedTickHz*2;++tick) for (auto [replica,q]:replicas()) if (q) step(*q);
        for (auto [replica,q]:replicas()) if (q&&!check(scene,*q,0)) return false;
        if (!flightSaved||!partialSaved||caseFlight<2||count!=(strength+damage-1)/damage)
            return fail("incomplete real flight/health thresholds/save coverage");
        if (scene.actualMap) ++realMapCases;
        ++passed;
        std::cout<<"Bridge projectile damage "<<label<<" PASS shots="<<count<<" hp="<<strength<<"->0 realFlightTicks="<<caseFlight
            <<" yellowCross=8/9 redCross=12/13 localCaps=2 distantPreserved=yes originalHuts="<<scene.rawHutCount
            <<" registeredSpans="<<scene.hutCount
            <<" ordinary/canonical/partial/nativeSHP=exact\n";
        return true;
    };
    for (int hz:{30,60}) for (bool high:{true,false}) for (bool wood:{false,true}) for (bool alongX:{true,false}) {
        const std::string name=std::string(high?"high":"low")+"-"+(wood?"wood":"concrete")+"-"+(alongX?"X":"Y")+"-"+std::to_string(hz);
        label=name;error.clear();if (!run(synthetic(high,wood,alongX,hz),name)) return false;
    }
    struct MapCase { const char* path; const char* name; bool high; };
    for (const auto& map:{MapCase{"multimd/bridgegap.map","bridgegap",true},
            MapCase{"mapsmd03/all02umd.map","all02",true},MapCase{"mapsmd03/all04dmd.map","all04",false},
            MapCase{"multimd/c1a01md.map","c1a01",false},MapCase{"mapsmd03/sov04dmd.map","sov04",false}}) {
        label=std::string("actual-")+map.name+"-30";
        if (filter&&*filter&&label.find(filter)==std::string::npos) continue;
        stage="original real map / native bridge topology and optional CABHUT spans";
        Scene scene;scene.high=map.high;scene.actualMap=true;scene.mapName=map.name;
        scene.state=std::make_unique<RuntimeWindowState>();auto& s=*scene.state;
        s.config=stock->config;s.config.defaultMapPath=pathString(std::filesystem::path(runtimeAssetRootPath(stock->config))/map.path);
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        if (!s.report.success) return fail("original map startup: "+s.report.errorMessage);
        s.mapFactsLoaded=!s.mapFacts.cells.empty();
        prepare(s,30);scene.land=clearTile(s,false);
        IniFile originalMap;
        if (!originalMap.loadFromFile(s.config.defaultMapPath)) return fail("original map topology INI unavailable");
        if (const auto* structures = originalMap.getSection("Structures"))
            for (const auto& [id,value] : *structures) if (value.find(",CABHUT,") != std::string::npos) ++scene.rawHutCount;
        std::erase_if(s.world.accessObjects(),[&](const WorldObjectInstance& object){
            const auto* building=findRuntimeBuildingTypeById(s.rules,object.type);return !building||!building->bridgeRepairHut;
        });
        initialize(s);scene.hutCount=static_cast<int>(s.bridgeRepairSpansByHut.size());
        const bool originalDestroyableBridges = s.destroyableBridges;
        // Navigation honors sov04's original [SpecialFlags] No. This combat
        // fixture explicitly opts into destructible bridges after loading the
        // original topology; the map and its authored option remain unchanged.
        s.destroyableBridges = true;
        // Independent raw overlay facts choose the target. Repair-hut
        // association is reported, never required for a cannon to damage it.
        std::unordered_map<int,const MapCellFacts*> rawByCell;
        for (const auto& fact : s.mapFacts.cells) rawByCell.emplace(runtimeCellKey(fact.x,fact.y),&fact);
        const auto rawAt = [&](RuntimeCellPoint point) -> const MapCellFacts* {
            const auto found = rawByCell.find(runtimeCellKey(point.x,point.y));
            return found == rawByCell.end() ? nullptr : found->second;
        };
        const auto matchesRaw = [&](RuntimeCellPoint point) {
            const auto* fact = rawAt(point); const auto* actual = terrain(s,point);
            return fact && fact->hasOverlay && actual && actual->hasOverlay
                && actual->overlayIndex == fact->overlay.overlayIndex && actual->overlayType == fact->overlay.type
                && actual->overlayImage == fact->overlay.image && actual->overlayFrame == fact->overlay.frameIndex;
        };
        const auto connectedRow = [&](RuntimeCellPoint anchor, bool alongX, bool wood) {
            const int bank = alongX ? 0 : 9;
            for (int lane = 0; lane < (map.high ? 1 : 3); ++lane) {
                const RuntimeCellPoint point{anchor.x+(alongX?0:lane),anchor.y+(alongX?lane:0)};
                const auto* fact = rawAt(point);
                if (!fact || !fact->hasOverlay || !matchesRaw(point)) return false;
                if (map.high) {
                    const std::string id = wood ? (alongX?"BRIDGEB1":"BRIDGEB2") : (alongX?"BRIDGE1":"BRIDGE2");
                    if (fact->overlay.type != id || fact->overlay.overlayIndex != nativeIndex(id)
                        || fact->overlay.frameIndex < bank || fact->overlay.frameIndex > bank+6) return false;
                } else {
                    const int offset = fact->overlay.overlayIndex-(wood?74:205);
                    if (offset < bank || offset > bank+6 || fact->overlay.type != lowId(wood,offset)
                        || fact->overlay.frameIndex != lane) return false;
                }
            }
            return true;
        };
        std::ostringstream topology;
        topology << "Original bridge topology " << label << " rawCABHUT=" << scene.rawHutCount
            << " loadedHuts=" << s.world.getObjects().size() << " registeredSpans=" << scene.hutCount
            << " authoredDestroyableBridges=" << originalMap.getValue("SpecialFlags","DestroyableBridges","")
            << " loadedOriginalDestroyableBridges=" << originalDestroyableBridges
            << " fixtureDestroyableBridges=" << s.destroyableBridges << '\n';
        if (s.world.getObjects().size() != static_cast<std::size_t>(scene.rawHutCount))
            return fail("original CABHUT placements were not preserved");
        for (const auto& hut : s.world.getObjects())
            topology << "hut id=" << hut.instanceId << " type=" << hut.type << " at=" << hut.x << ',' << hut.y << '\n';
        std::vector<int> huts;
        for (const auto& [id,span] : s.bridgeRepairSpansByHut) huts.push_back(id);
        std::sort(huts.begin(),huts.end());
        for (int id : huts) {
            const auto& span = s.bridgeRepairSpansByHut.at(id);
            topology << "span hut=" << id << " high=" << span.high << " X=" << span.alongX
                << " heads=" << span.startHead.x << ',' << span.startHead.y << "->" << span.endHead.x << ',' << span.endHead.y
                << " cells=" << span.cells.size() << '\n';
        }
        struct Candidate { RuntimeCellPoint point; bool wood = false, alongX = true; int center = 0; std::vector<int> huts; };
        std::vector<Candidate> candidates;
        int rawBridgeRecords = 0, normalAnchors = 0;
        for (const auto& fact : s.mapFacts.cells) {
            if (!fact.hasOverlay) continue;
            const int ordinal = fact.overlay.overlayIndex;
            const bool rawHigh = ordinal == 24 || ordinal == 25 || ordinal == 237 || ordinal == 238;
            const bool rawLow = (ordinal >= 74 && ordinal <= 101) || (ordinal >= 205 && ordinal <= 232);
            if (map.high ? !rawHigh : !rawLow) continue;
            ++rawBridgeRecords;
            topology << "raw " << fact.x << ',' << fact.y << " ordinal=" << ordinal << " type=" << fact.overlay.type
                << " image=" << fact.overlay.image << " frame=" << fact.overlay.frameIndex << '\n';
            const bool wood = rawHigh ? ordinal >= 237 : ordinal < 102;
            const bool alongX = rawHigh ? ordinal == 24 || ordinal == 237 : ordinal == (wood?74:205);
            const int bank = alongX ? 0 : 9;
            const bool normal = rawHigh ? fact.overlay.frameIndex == bank
                : (ordinal == (wood?74:205) || ordinal == (wood?74:205)+9) && fact.overlay.frameIndex == 0;
            if (!normal) continue;
            ++normalAnchors;
            Candidate candidate; candidate.point = {fact.x,fact.y}; candidate.wood = wood; candidate.alongX = alongX;
            bool connected = true;
            for (int side : {-1,0,1}) {
                const RuntimeCellPoint point{fact.x+(alongX?side:0),fact.y+(alongX?0:side)};
                connected = connected && connectedRow(point,alongX,wood);
            }
            topology << "candidate " << fact.x << ',' << fact.y << " normalAnchor=1 connectedThreeRows=" << connected;
            for (int id : huts) {
                const auto& span = s.bridgeRepairSpansByHut.at(id);
                if (span.high != map.high) continue;
                for (const auto& cell : span.cells) if (cell.requiresOverlay && cell.point == candidate.point) {
                    candidate.huts.push_back(id); break;
                }
            }
            topology << " coveringHuts="; for (int id : candidate.huts) topology << id << ':';
            if (!connected) { topology << " rejected=original-neighbor-head/cap/missing/lane/axis\n"; continue; }
            int reaches[2]{};
            for (int direction = 0; direction < 2; ++direction) for (int distance = 1; distance <= 64; ++distance) {
                const int side = direction == 0 ? -distance : distance;
                if (!connectedRow({fact.x+(alongX?side:0),fact.y+(alongX?0:side)},alongX,wood)) break;
                ++reaches[direction];
            }
            candidate.center = std::min(reaches[0],reaches[1]);
            topology << " eligible=1 originalConnectedReach=" << reaches[0] << ',' << reaches[1] << '\n';
            candidates.push_back(std::move(candidate));
        }
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
            return std::tuple{a.huts.empty(),-a.center,a.point.x,a.point.y}
                < std::tuple{b.huts.empty(),-b.center,b.point.x,b.point.y};
        });
        topology << "inventory rawBridgeRecords=" << rawBridgeRecords << " normalAnchors=" << normalAnchors
            << " eligible=" << candidates.size() << '\n';
        if (!candidates.empty()) {
            const auto& selected = candidates.front();
            scene.target = selected.point; scene.wood = selected.wood; scene.alongX = selected.alongX;
            const auto actual = runtimeBridgeDamageTargetAtCell(s,scene.target,0);
            bool lookupExact = actual && actual->anchor == scene.target && actual->high == map.high
                && actual->alongX == scene.alongX && actual->physicalCells.size() == 3
                && actual->material == (scene.wood?RuntimeBridgeMaterial::Wood:RuntimeBridgeMaterial::Concrete);
            if (actual) for (int lane = 0; lane < 3 && lookupExact; ++lane)
                lookupExact = actual->physicalCells[lane] == RuntimeCellPoint{
                    scene.target.x+(scene.alongX?0:lane),scene.target.y+(scene.alongX?lane:0)};
            topology << "selected " << scene.target.x << ',' << scene.target.y << " high=" << scene.high
                << " X=" << scene.alongX << " wood=" << scene.wood << " realR0LookupExact=" << lookupExact << '\n';
            std::ofstream dump(root/(label+"-original-topology.txt")); dump << topology.str();
            std::cout << topology.str();
            if (!lookupExact) return fail("real R0 lookup disagrees with independently selected original bridge anchor");
        } else {
            std::ofstream dump(root/(label+"-original-topology.txt")); dump << topology.str();
            std::cout << topology.str();
        }
        if (candidates.empty() || scene.land < 0) return fail("original map lacks normal-health anchor with native connected transverse lanes and axial neighbors");
        std::optional<RuntimeCellPoint> source;
        for (int radius=2;radius<=4&&!source;++radius) for (int dy=-radius;dy<=radius&&!source;++dy) for (int dx=-radius;dx<=radius&&!source;++dx) {
            if (std::max(std::abs(dx),std::abs(dy))!=radius||std::hypot(static_cast<double>(dx),static_cast<double>(dy))>4.5) continue;
            RuntimeCellPoint point{scene.target.x+dx,scene.target.y+dy};const auto* cell=terrain(s,point);if (!cell||bridgeArt(*cell)) continue;
            const auto info=runtimeTerrainInfoForCell(s,*cell);if (!info.passable||info.water||info.shore||info.cliff||info.ramp||info.bridge) continue;
            bool hut=false;for (const auto& object:s.world.getObjects()) if (std::abs(object.x-point.x)<=2&&std::abs(object.y-point.y)<=2) hut=true;
            if (!hut) source=point;
        }
        if (!source) for (int side:{-1,1}) {
            RuntimeCellPoint point{scene.target.x+(scene.alongX?side*3:0),scene.target.y+(scene.alongX?0:side*3)};
            const auto* cell=terrain(s,point);if (!cell||!bridgeArt(*cell)) continue;
            if (connectedRow(point,scene.alongX,scene.wood)) {source=point;break;}
        }
        if (!source) return fail("native land/connected deck launch cell within original 120mm range unavailable");
        if (!addShooter(scene,*source)||!run(std::move(scene),label)) return false;
    }
    if (!attempted||passed!=attempted) return fail("empty/incomplete selected coverage");
    std::cout<<"Bridge projectile damage passed: cases="<<passed<<" actualMaps="<<realMapCases<<" originalShots="<<shots
        <<" actualFlightTicks="<<naturalFlightTicks<<" nativePixelChecks="<<pixelChecks<<" ordinarySaves="<<saves
        <<" canonicalSnapshots="<<snapshots<<" MIXSources="<<nativeSources.size()
        <<" R0HighSideOwners="<<sideOwnershipChecks<<" R0NativeCaps="<<capOwnershipChecks<<" R1ExistingRange="<<radialOwnershipChecks
        <<" UI/stock1500/90/local/threeLane/graded/originalPixels/saveContinuation=ok\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
