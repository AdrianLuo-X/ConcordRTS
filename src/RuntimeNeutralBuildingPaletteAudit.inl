// Exhaustive stock neutral-building BODY color audit. This file is test-only.
// Palette expectations read original MIX entries, never production selectors.
// Index expectations independently decode the file selected by the live loader.
#include <iterator>
#include <map>
namespace RuntimeNeutralPaletteAudit {
using Bytes = RuntimeNeutralBuildingPaletteOracle::Bytes;
using Frame = RuntimeNeutralBuildingPaletteOracle::Frame;
using Colors = std::array<std::uint32_t, 256>;
using RuntimeNeutralBuildingPaletteOracle::little;

std::string csv(const std::string& value) {
    std::string result = "\"";
    for (char c : value) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot independently read " + path.generic_string());
    return Bytes(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

struct Originals {
    std::map<std::string, std::unique_ptr<MixArchive>> archives;
    std::map<std::string, Bytes> files;
    std::ofstream& evidence;
    explicit Originals(std::ofstream& output) : evidence(output) {}
    const Bytes& get(const std::string& source) {
        const auto old = files.find(source);
        if (old != files.end()) return old->second;
        Bytes bytes;
        const auto separator = source.find('!');
        if (separator == std::string::npos) bytes = readFile(std::filesystem::path(source));
        else {
            const auto path = source.substr(0, separator);
            auto& archive = archives[path];
            if (!archive) {
                archive = std::make_unique<MixArchive>();
                if (!archive->loadFromPath(std::filesystem::path(path)))
                    throw std::runtime_error("cannot independently open original archive " + path);
            }
            const auto* entry = archive->findEntryByName(source.substr(separator + 1));
            std::string error;
            if (!entry || !archive->readEntryData(*entry, bytes, error))
                throw std::runtime_error("cannot independently read original entry " + source + ": " + error);
        }
        evidence << csv(source) << ',' << bytes.size() << '\n';
        return files.emplace(source, std::move(bytes)).first->second;
    }
};

Frame decode(const Bytes& bytes, int number) {
    if (number < 0 || static_cast<unsigned>(number) >= little(bytes, 6, 2))
        throw std::runtime_error("independent original frame number absent");
    const std::size_t header = 8 + static_cast<std::size_t>(number) * 24;
    Frame frame;
    frame.canvasWidth = little(bytes, 2, 2); frame.canvasHeight = little(bytes, 4, 2);
    frame.x = static_cast<std::int16_t>(little(bytes, header, 2));
    frame.y = static_cast<std::int16_t>(little(bytes, header + 2, 2));
    frame.width = little(bytes, header + 4, 2); frame.height = little(bytes, header + 6, 2);
    const auto format = little(bytes, header + 8, 1);
    std::size_t cursor = little(bytes, header + 20, 4);
    if (frame.canvasWidth <= 0 || frame.canvasHeight <= 0 || frame.canvasWidth > 4096
        || frame.canvasHeight > 4096 || frame.width > 4096 || frame.height > 4096)
        throw std::runtime_error("independent original frame dimensions invalid");
    if (!frame.width && !frame.height && !cursor && (format == 1 || format == 3 || format == 4))
        return frame; // The original file explicitly authors a blank frame.
    if (!frame.width || !frame.height || cursor >= bytes.size())
        throw std::runtime_error("independent original frame data absent");
    frame.indices.assign(static_cast<std::size_t>(frame.width) * frame.height, 0);
    frame.sourceOffsets.assign(frame.indices.size(), 0);
    if (format <= 1) {
        if (frame.indices.size() > bytes.size() - cursor)
            throw std::runtime_error("independent raw SHP frame truncated");
        std::copy_n(bytes.begin() + cursor, frame.indices.size(), frame.indices.begin());
        for (std::size_t p = 0; p < frame.indices.size(); ++p) frame.sourceOffsets[p] = cursor + p;
        return frame;
    }
    if (format != 2 && format != 3)
        throw std::runtime_error("independent SHP format unsupported: " + std::to_string(format));
    for (int y = 0; y < frame.height; ++y) {
        const std::size_t length = little(bytes, cursor, 2);
        if (length < 2 || length > bytes.size() - cursor)
            throw std::runtime_error("independent SHP row extent invalid");
        const std::size_t end = cursor + length; cursor += 2;
        int x = 0;
        if (format == 2) {
            // Format 2 rows contain a length-prefixed raw run; omitted tail is transparent.
            for (; cursor < end; ++cursor, ++x) {
                if (x >= frame.width) {
                    if (bytes[cursor]) throw std::runtime_error("independent raw row opaque overflow");
                } else {
                    const auto p = static_cast<std::size_t>(y) * frame.width + x;
                    frame.indices[p] = bytes[cursor]; frame.sourceOffsets[p] = cursor;
                }
            }
        } else {
            while (cursor < end) {
                const auto value = bytes[cursor++];
                if (!value) {
                    if (cursor == end || !bytes[cursor])
                        throw std::runtime_error("independent SHP transparent run invalid");
                    x += bytes[cursor++];
                } else {
                    if (x >= frame.width) throw std::runtime_error("independent SHP opaque row overflow");
                    const auto p = static_cast<std::size_t>(y) * frame.width + x++;
                    frame.indices[p] = value; frame.sourceOffsets[p] = cursor - 1;
                }
            }
            // Retail row padding may contain one transparent pixel past the crop.
            if (x < frame.width || x > frame.width + 1)
                throw std::runtime_error("independent SHP decoded row width mismatch");
        }
    }
    return frame;
}

struct Surface {
    int width, height;
    HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ old = nullptr;
    std::uint32_t* pixels = nullptr;
    Surface(int w, int h) : width(w), height(h) {
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        dc = CreateCompatibleDC(nullptr); void* data = nullptr;
        if (dc) bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        pixels = static_cast<std::uint32_t*>(data);
        if (bitmap && pixels) old = SelectObject(dc, bitmap);
        if (!dc || !bitmap || !pixels || !old) throw std::runtime_error("audit DIB allocation failed");
    }
    ~Surface() { if (old && dc) SelectObject(dc, old); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
};

struct PngWriter {
    ULONG_PTR token = 0;
    CLSID encoder{};
    PngWriter() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("audit PNG startup failed");
        UINT count = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&count, &size);
        std::vector<unsigned char> storage(size);
        auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
        if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok)
            throw std::runtime_error("audit PNG codecs unavailable");
        bool found = false;
        for (UINT i = 0; i < count; ++i)
            if (std::wstring(codecs[i].MimeType) == L"image/png") { encoder = codecs[i].Clsid; found = true; break; }
        if (!found) throw std::runtime_error("audit PNG encoder unavailable");
    }
    ~PngWriter() { if (token) Gdiplus::GdiplusShutdown(token); }
    void save(const std::filesystem::path& path, int width, int height, const std::uint32_t* pixels) {
        GdiFlush();
        Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppRGB,
            reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(pixels)));
        if (bitmap.Save(path.c_str(), &encoder, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("cannot save audit picture " + path.generic_string());
    }
};

constexpr std::uint32_t background = 0x0019232dU;
struct Atlas {
    Surface surface{1280, 960};
    PngWriter& writer;
    std::filesystem::path directory;
    std::ofstream& index;
    int page = 0, slot = 0;
    std::vector<std::string> labels;
    Atlas(PngWriter& png, const std::filesystem::path& path, std::ofstream& output)
        : writer(png), directory(path), index(output) {
        std::filesystem::create_directories(directory);
        std::fill_n(surface.pixels, 1280 * 960, background);
    }
    void flush() {
        if (!slot) return;
        GdiFlush(); SetBkMode(surface.dc, OPAQUE); SetBkColor(surface.dc, RGB(25, 35, 45));
        SetTextColor(surface.dc, RGB(245, 245, 245));
        for (std::size_t n = 0; n < labels.size(); ++n) {
            const int x = static_cast<int>(n % 5) * 256, y = static_cast<int>(n / 5) * 240;
            const RECT header{x, y, x + 256, y + 24};
            ExtTextOutA(surface.dc, x + 7, y + 4, ETO_OPAQUE | ETO_CLIPPED, &header,
                labels[n].c_str(), static_cast<UINT>(labels[n].size()), nullptr);
        }
        writer.save(directory / ("atlas-" + std::to_string(page) + ".png"), 1280, 960, surface.pixels);
        ++page; slot = 0; labels.clear();
        std::fill_n(surface.pixels, 1280 * 960, background);
    }
    void add(const std::string& id, const std::string& status, const std::vector<std::uint32_t>& pixels,
        int width, int height) {
        const int left = slot % 5 * 256, top = slot / 5 * 240;
        const double scale = std::min({1.0, 244.0 / std::max(1, width), 208.0 / std::max(1, height)});
        const int w = std::max(1, static_cast<int>(width * scale));
        const int h = std::max(1, static_cast<int>(height * scale));
        const int dx = left + (256 - w) / 2, dy = top + 25 + (208 - h) / 2;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
            surface.pixels[static_cast<std::size_t>(dy + y) * 1280 + dx + x]
                = pixels[static_cast<std::size_t>(std::min(height - 1, y * height / h)) * width
                    + std::min(width - 1, x * width / w)];
        labels.push_back(id + (status == "pass" ? "" : " [" + status + "]"));
        index << csv(directory.filename().string()) << ',' << page << ',' << slot / 5 << ',' << slot % 5
            << ',' << csv(id) << ',' << csv(status) << ',' << scale << '\n';
        if (++slot == 20) flush();
    }
};
}

bool runRuntimeNeutralBuildingPaletteAudit(
    const RuntimeMapRenderContext& input, const std::string& output, std::string& error) {
    using namespace RuntimeNeutralPaletteAudit;
    try {
        if (!input.rules || !input.art || input.assetRootPath.empty())
            throw std::runtime_error("original rules/art/resource context missing");
        const char* typePath = std::getenv("CONCORD_NEUTRAL_PALETTE_AUDIT_TYPES");
        if (!typePath || !*typePath) throw std::runtime_error("CONCORD_NEUTRAL_PALETTE_AUDIT_TYPES is required");
        std::ifstream list(typePath);
        if (!list) throw std::runtime_error("neutral type inventory cannot be read");
        std::vector<std::string> types; std::set<std::string> unique;
        for (std::string line; std::getline(list, line);) {
            if (types.empty() && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef) line.erase(0, 3);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            if (line.empty()) continue;
            if (!unique.insert(line).second) throw std::runtime_error("duplicate neutral type: " + line);
            types.push_back(line);
        }
        if (types.size() != 320) throw std::runtime_error("expected audited inventory of 320 visible neutral building types");
        const std::filesystem::path root = output.empty() ? "build/neutral-building-palette-audit" : output;
        const std::filesystem::path assets(input.assetRootPath);
        std::filesystem::create_directories(root);
        std::ofstream sources(root / "original-sources.csv"), rows(root / "body-colors.csv"), atlasIndex(root / "atlas-index.csv");
        if (!sources || !rows || !atlasIndex) throw std::runtime_error("audit evidence files unavailable");
        sources << "source,bytes\n";
        rows << "phase,theater,id,art,health,frame,format,resolved_image,source,palette,palette_source,visible_pixels,pixel_mismatch,mask_mismatch,cold_pixel_mismatch,hot_cold_difference,first_x,first_y,first_index,first_actual,first_expected,status,error\n";
        atlasIndex << "gallery,page,row,column,id,status,scale\n";
        Originals originals(sources);
        const auto archiveSource = [&](const char* archive, const std::string& file) {
            return (assets / archive).generic_string() + "!" + file;
        };
        const auto originalColors = [&](const std::string& source) {
            return RuntimeNeutralBuildingPaletteOracle::palette(originals.get(source));
        };
        const auto citySource = archiveSource("ra2/cache.mix", "citytem.pal");
        const Colors cityColors = originalColors(citySource);
        std::map<std::string, Bytes> legacy;
        for (const char* id : {"CITY01", "CITY02", "CITY03", "CITY04", "CITY05", "CITY06"})
            legacy[id] = originals.get(archiveSource("ra2/isotemp.mix", std::string(id) + ".shp"));
        // Independent resource inspection established that each of these five
        // retained city-paletted SHPs is byte-identical in all six theaters.
        // Their RA2 ART identities differ from the older CITY01..06 family.
        const std::map<std::string, std::string> retainedCityFiles{
            {"CACITY01", "ctcity01.shp"}, {"CACITY02", "ctcity02.shp"},
            {"CACITY03", "ctcity03.shp"}, {"CACITY04", "ctcity04.shp"},
            {"CANEWY05", "ctnewy05.shp"}
        };
        for (const auto& [id, file] : retainedCityFiles)
            legacy[id] = originals.get(archiveSource("ra2/isotemp.mix", file));
        IniFile stockArt;
        if (!stockArt.loadFromFile((assets / "ra2md/localmd/artmd.ini").string()))
            throw std::runtime_error("original ART unavailable for explicit override controls");
        struct TheaterSpec { const char* name; const char* ini; const char* suffix; const char* archive; const char* extractedPrefix; };
        const TheaterSpec theaters[]{
            {"TEMPERATE", "temperatmd.ini", "tem", "ra2/cache.mix", "cache_"},
            {"SNOW", "snowmd.ini", "sno", "ra2/cache.mix", "cache_"},
            {"URBAN", "urbanmd.ini", "urb", "ra2/cache.mix", "cache_"},
            {"NEWURBAN", "urbannmd.ini", "ubn", "ra2md/cachemd.mix", "cachemd_"},
            {"DESERT", "desertmd.ini", "des", "ra2md/cachemd.mix", "cachemd_"},
            {"LUNAR", "lunarmd.ini", "lun", "ra2md/cachemd.mix", "cachemd_"}
        };
        PngWriter png;
        auto cache = std::make_unique<RuntimeMapRenderCache>();
        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = 60; camera.tileHeight = 30;
        std::uint64_t checks = 0, failures = 0, visibleTotal = 0, mismatchTotal = 0, maskTotal = 0, authoredEmpty = 0;
        // Returning to temperate with the SAME cache catches stale packed colors.
        for (int phase = 0; phase < 7; ++phase) {
            const auto& theater = theaters[phase % 6];
            TheaterDatabase metadata; IniFile ini;
            const auto iniPath = assets / "ra2md/localmd" / theater.ini;
            if (!ini.loadFromFile(iniPath.string()) || !metadata.loadFromIni(ini, iniPath.string()))
                throw std::runtime_error("original theater metadata absent: " + iniPath.string());
            const std::string suffix = theater.suffix;
            const auto unitSource = archiveSource(theater.archive, "unit" + suffix + ".pal");
            const auto isoSource = archiveSource(theater.archive, "iso" + suffix + ".pal");
            const Colors unitColors = originalColors(unitSource), isoColors = originalColors(isoSource);
            auto view = input; view.theater = &metadata;
            view.vision = nullptr; view.lightingTint = nullptr; view.specialMechanismSnapshots = nullptr;
            view.mapFacts = nullptr; view.ownerRemapColors = nullptr; view.shadowsEnabled = false;
            view.animationTick = 0;
            view.palettePath = (assets / "extracted_palettes" / (std::string(theater.extractedPrefix) + "iso" + suffix + ".pal")).string();
            view.structurePalettePath = (assets / "extracted_palettes" / (std::string(theater.extractedPrefix) + "unit" + suffix + ".pal")).string();
            std::unique_ptr<Atlas> healthyAtlas, damagedAtlas, occupiedAtlas;
            if (phase < 6) {
                healthyAtlas = std::make_unique<Atlas>(png, root / (std::string(theater.name) + "-healthy"), atlasIndex);
                damagedAtlas = std::make_unique<Atlas>(png, root / (std::string(theater.name) + "-damaged"), atlasIndex);
                occupiedAtlas = std::make_unique<Atlas>(png, root / (std::string(theater.name) + "-garrisoned"), atlasIndex);
                std::filesystem::create_directories(root / theater.name);
            }
            for (const auto& id : types) for (int state = 0;
                state < (retainedCityFiles.count(id) ? (phase == 0 ? 6 : 3) : 2); ++state) {
                const bool damaged = state == 1;
                const std::string pose = state == 0 ? "healthy" : state == 1 ? "damaged" : state == 2 ? "garrisoned"
                    : state == 3 ? "explicit-lib" : state == 4 ? "explicit-terrain-no" : "replacement";
                ++checks;
                std::string status = "fail", problem, artId, imageId, source, paletteName, paletteSource;
                int frameIndex = -1, format = -1, firstX = -1, firstY = -1, firstIndex = -1;
                std::uint32_t firstActual = 0, firstExpected = 0;
                std::uint64_t visible = 0, mismatch = 0, mask = 0;
                std::int64_t coldMismatch = -1, hotColdDifference = -1;
                int pictureWidth = 240, pictureHeight = 120;
                std::vector<std::uint32_t> picture(pictureWidth * pictureHeight, background);
                try {
                    auto caseView = view;
                    std::unique_ptr<ArtDatabase> artOverride;
                    Bytes replacement;
                    if (state == 3 || state == 4) {
                        IniFile original = stockArt, overlay;
                        if (!overlay.loadFromString("[" + id + "]\n" + (state == 3 ? "Palette=lib\n" : "TerrainPalette=no\n")))
                            throw std::runtime_error("explicit override INI could not be parsed");
                        original.appendFrom(overlay); artOverride = std::make_unique<ArtDatabase>();
                        if (!artOverride->loadFromIni(original)) throw std::runtime_error("explicit override ART load failed");
                        caseView.art = artOverride.get();
                    }
                    if (state == 5) {
                        replacement = legacy.at(id);
                        const auto originalFrame = decode(replacement, 0);
                        bool changed = false;
                        for (std::size_t p = 0; p < originalFrame.indices.size(); ++p) {
                            const auto old = originalFrame.indices[p];
                            const unsigned char other = old == 100 ? 101 : 100;
                            if (old > 31 && unitColors[old] != unitColors[other]) {
                                replacement[originalFrame.sourceOffsets[p]] = other; changed = true; break;
                            }
                        }
                        if (!changed) throw std::runtime_error("replacement control lacks a legal changed opaque index");
                        const auto replacementDir = root / ("replacement-" + id);
                        std::filesystem::create_directories(replacementDir);
                        const auto replacementPath = replacementDir / retainedCityFiles.at(id);
                        std::ofstream file(replacementPath, std::ios::binary);
                        file.write(reinterpret_cast<const char*>(replacement.data()), static_cast<std::streamsize>(replacement.size()));
                        file.close(); if (!file) throw std::runtime_error("replacement fixture write failed");
                        caseView.structureRootPath = replacementDir.string();
                    }
                    const auto* rules = input.rules->findBuildingType(id);
                    if (!rules || rules->invisibleInGame) throw std::runtime_error("visible stock building rules missing");
                    const auto* art = caseView.art->findArtType(rules->image.empty() ? id : rules->image);
                    if (!art) art = caseView.art->findArtType(id);
                    if (!art) throw std::runtime_error("effective stock ART missing");
                    if (state == 3 && art->palette != "lib")
                        throw std::runtime_error("explicit Palette=lib was not parsed");
                    if (state == 4 && (!art->terrainPaletteSpecified || art->terrainPalette))
                        throw std::runtime_error("explicit TerrainPalette=no was not preserved by parser");
                    artId = art->id;
                    WorldObjectInstance object; object.instanceId = 1; object.kind = WorldObjectKind::Structure;
                    object.type = id; object.owner = "Neutral"; object.alive = object.poweredOperational = true;
                    object.health = damaged ? 1 : std::max(1, rules->strength); object.mission = "Guard";
                    if (state == 2) object.garrisonOccupantIds = {2};
                    const auto* sprite = loadRuntimeObjectShpSpriteFromCache(object, caseView, *cache);
                    if (!sprite) throw std::runtime_error("live body loader did not resolve original SHP");
                    frameIndex = sprite->frameIndex; imageId = sprite->resolvedImageId; source = sprite->sourcePath;
                    const auto& bytes = originals.get(source);
                    if (state == 5 && bytes != replacement)
                        throw std::runtime_error("replacement control failed to load the changed SHP");
                    if (state == 2 && frameIndex != 2)
                        throw std::runtime_error("garrison control did not select original third main frame");
                    const Frame body = decode(bytes, frameIndex);
                    format = little(bytes, 8 + static_cast<std::size_t>(frameIndex) * 24 + 8, 1);
                    if (body.indices != sprite->image.pixels || body.canvasWidth != sprite->image.globalWidth
                        || body.canvasHeight != sprite->image.globalHeight || body.width != sprite->image.width
                        || body.height != sprite->image.height || body.x != sprite->image.offsetX || body.y != sprite->image.offsetY)
                        throw std::runtime_error("live indexed frame differs from independent file decoder");
                    Colors expectedColors = unitColors; paletteName = "unit" + suffix + ".pal"; paletteSource = unitSource;
                    const auto setOriginal = [&](const std::string& name) {
                        paletteName = name; paletteSource = archiveSource(theater.archive, name);
                        expectedColors = originalColors(paletteSource);
                    };
                    const std::string alt = toLowerAscii(art->altPalette);
                    const bool altUnit = alt == "yes" || alt == "true" || alt == "1";
                    const bool isCity = legacy.count(artId) && artId.compare(0, 4, "CITY") == 0 && bytes == legacy.at(artId);
                    const bool isRetainedCity = retainedCityFiles.count(artId) && bytes == legacy.at(artId);
                    const bool declared = art->animPaletteSpecified || !art->altPalette.empty();
                    if (!art->palette.empty()) {
                        const std::string name = std::filesystem::path(art->palette).extension().empty()
                            ? art->palette + suffix + ".pal" : art->palette;
                        setOriginal(name);
                    } else if (!art->customPalette.empty()) setOriginal(art->customPalette);
                    else if (art->animPalette) {
                        paletteName = "anim.pal"; paletteSource = archiveSource("ra2/cache.mix", paletteName);
                        expectedColors = originalColors(paletteSource);
                    } else if (!altUnit && art->terrainPalette) {
                        // Stock TerrainPalette always uses the active
                        // theater's independently read original MIX palette.
                        expectedColors = isoColors; paletteName = "iso" + suffix + ".pal"; paletteSource = isoSource;
                    } else if (!altUnit && !declared && !art->terrainPaletteSpecified
                        && ((isCity && artId != "CITY06") || isRetainedCity)) {
                        expectedColors = cityColors; paletteName = "citytem.pal"; paletteSource = citySource;
                    }
                    // Neutral structures use the fixed Westwood white house
                    // ramp. This independent integer oracle deliberately does
                    // not call the production remap/tint helpers. Alternate
                    // terrain/animation palettes are authored without remap.
                    if (!art->terrainPalette && !art->animPalette) {
                        constexpr std::array<unsigned, 16> whiteRamp{
                            252, 236, 220, 208, 192, 176, 164, 148, 132, 120, 104, 88, 76, 60, 44, 32};
                        for (std::size_t n = 0; n < whiteRamp.size(); ++n)
                            expectedColors[n + 16] = whiteRamp[n] * 0x00010101U;
                    }
                    const int width = std::max(512, body.canvasWidth + std::abs(body.x) * 2 + 64);
                    const int height = std::max(512, body.canvasHeight + std::abs(body.y) * 2 + 64);
                    Surface surface(width, height);
                    const POINT center{width / 2, height / 2}; const RECT viewport{0, 0, width, height};
                    const int left = center.x - body.canvasWidth / 2 + body.x;
                    const int top = center.y - body.canvasHeight / 2 + body.y;
                    std::vector<std::uint32_t> expected(static_cast<std::size_t>(width) * height, background);
                    std::vector<int> indices(expected.size(), -1);
                    for (int y = 0; y < body.height; ++y) for (int x = 0; x < body.width; ++x) {
                        const int index = body.indices[static_cast<std::size_t>(y) * body.width + x];
                        if (!index) continue;
                        if (left + x < 0 || top + y < 0 || left + x >= width || top + y >= height)
                            throw std::runtime_error("independent original picture clipped");
                        const auto p = static_cast<std::size_t>(top + y) * width + left + x;
                        expected[p] = expectedColors[index]; indices[p] = index; ++visible;
                    }
                    GdiFlush(); std::fill_n(surface.pixels, expected.size(), background);
                    caseView.surfacePixels = surface.pixels; caseView.surfaceWidth = width; caseView.surfaceHeight = height;
                    if (!tryDrawObjectShpSprite(surface.dc, viewport, object, center, caseView, camera, *cache))
                        throw std::runtime_error("live body renderer failed");
                    GdiFlush();
                    for (std::size_t p = 0; p < expected.size(); ++p) {
                        const auto actual = surface.pixels[p] & 0x00ffffffU;
                        if (actual != expected[p]) {
                            ++mismatch;
                            if (firstX < 0) { firstX = static_cast<int>(p % width); firstY = static_cast<int>(p / width);
                                firstIndex = indices[p]; firstActual = actual; firstExpected = expected[p]; }
                        }
                        mask += (actual != background) != (indices[p] >= 0);
                    }
                    status = mismatch || mask ? "fail" : visible ? "pass" : "authored-empty";
                    if (!visible) ++authoredEmpty;
                    pictureWidth = std::max(80, body.width + 16); pictureHeight = std::max(48, body.height + 16);
                    picture.assign(static_cast<std::size_t>(pictureWidth) * pictureHeight, background);
                    for (int y = 0; y < pictureHeight; ++y) for (int x = 0; x < pictureWidth; ++x) {
                        const int sx = left - 8 + x, sy = top - 8 + y;
                        if (sx >= 0 && sy >= 0 && sx < width && sy < height)
                            picture[static_cast<std::size_t>(y) * pictureWidth + x] = surface.pixels[static_cast<std::size_t>(sy) * width + sx];
                    }
                    if (phase < 6) png.save(root / theater.name / (id + "-" + pose + ".png"),
                        pictureWidth, pictureHeight, picture.data());
                    if (mismatch || mask) {
                        problem = "actual body RGB/mask disagrees with independent original SHP/PAL";
                        png.save(root / (std::string(theater.name) + "-phase" + std::to_string(phase) + "-" + id
                            + "-" + pose + "-expected.png"), width, height, expected.data());
                    }
                    // Failures get a cold-cache control; AMMOCRAT always gets
                    // this control because its unchanged generic SHP crosses
                    // theater palette boundaries without changing frame keys.
                    if (mismatch || mask || id == "AMMOCRAT") {
                        const std::vector<std::uint32_t> hot(surface.pixels, surface.pixels + expected.size());
                        auto coldCache = std::make_unique<RuntimeMapRenderCache>();
                        GdiFlush(); std::fill_n(surface.pixels, expected.size(), background);
                        if (!tryDrawObjectShpSprite(surface.dc, viewport, object, center, caseView, camera, *coldCache))
                            throw std::runtime_error("cold-cache control body renderer failed");
                        GdiFlush(); coldMismatch = 0; hotColdDifference = 0;
                        for (std::size_t p = 0; p < expected.size(); ++p) {
                            const auto cold = surface.pixels[p] & 0x00ffffffU;
                            coldMismatch += cold != expected[p];
                            hotColdDifference += cold != (hot[p] & 0x00ffffffU);
                        }
                        png.save(root / (std::string(theater.name) + "-phase" + std::to_string(phase) + "-" + id
                            + "-" + pose + "-cold.png"), width, height, surface.pixels);
                        if (coldMismatch || hotColdDifference) status = "fail";
                    }
                } catch (const std::exception& exception) { problem = exception.what(); status = "fail"; }
                failures += status == "fail"; visibleTotal += visible; mismatchTotal += mismatch; maskTotal += mask;
                rows << phase << ',' << theater.name << ',' << csv(id) << ',' << csv(artId) << ',' << pose
                    << ',' << frameIndex << ',' << format << ',' << csv(imageId) << ',' << csv(source) << ',' << csv(paletteName)
                    << ',' << csv(paletteSource) << ',' << visible << ',' << mismatch << ',' << mask << ',' << coldMismatch
                    << ',' << hotColdDifference << ',' << firstX << ',' << firstY
                    << ',' << firstIndex << ',' << firstActual << ',' << firstExpected << ',' << status << ',' << csv(problem) << '\n';
                if (phase < 6 && state < 3)
                    (state == 2 ? occupiedAtlas : damaged ? damagedAtlas : healthyAtlas)->add(id, status, picture, pictureWidth, pictureHeight);
                if (status == "fail" && failures <= 24)
                    std::cout << "Neutral full audit " << theater.name << ' ' << id << ' ' << pose
                        << ": " << problem << " mismatch=" << mismatch << " mask=" << mask << '\n';
            }
            if (phase < 6) { healthyAtlas->flush(); damagedAtlas->flush(); occupiedAtlas->flush(); }
            rows.flush(); atlasIndex.flush();
            std::cout << "Neutral full audit phase=" << phase << " theater=" << theater.name << " checks=" << checks
                << " failures=" << failures << '\n';
        }
        std::ofstream summary(root / "summary.json");
        summary << "{\"types\":" << types.size() << ",\"theaters\":6,\"returnToTemperate\":true,\"bodyChecks\":" << checks
            << ",\"visiblePixels\":" << visibleTotal << ",\"pixelMismatch\":" << mismatchTotal << ",\"maskMismatch\":" << maskTotal
            << ",\"authoredEmptyFrames\":" << authoredEmpty << ",\"failures\":" << failures
            << ",\"neutralWhiteRemapTested\":true,\"attachedAnimationsTested\":false}\n";
        if (failures) { error = "Neutral full body palette audit: " + std::to_string(failures) + " failures; see " + (root / "body-colors.csv").string(); return false; }
        return true;
    } catch (const std::exception& exception) { error = "Neutral full body palette audit: " + std::string(exception.what()); return false; }
}
