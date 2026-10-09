#include "RuntimeInternetMenuSkin.h"

#include "RuntimeMapRenderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <system_error>
#include <tuple>

namespace {

namespace fs = std::filesystem;

struct InternetSkinPaletteSpec {
    const char* id;
    const char* relativeRoot;
    const char* fileName;
};

struct InternetSkinAssetSpec {
    const char* id;
    const char* relativeRoot;
    const char* fileName;
    const char* paletteId;
    int width;
    int height;
};

constexpr std::array<InternetSkinPaletteSpec, 6> paletteSpecs{{
    {"dialogn", "ra2md/localmd", "dialogn.pal"},
    {"dialogy", "ra2md/localmd", "dialogy.pal"},
    {"mplsobs", "ra2md/localmd", "mplsobs.pal"},
    {"mpyscrn-md", "ra2md/localmd", "mpyscrn.pal"},
    {"yrii", "ra2md/localmd", "yrii.pal"},
    {"mpyscrn-expand", "expandmd01", "mpyscrn.pal"}
}};

constexpr std::array<InternetSkinAssetSpec, 45> assetSpecs{{
    {"wodact", "ra2md/localmd", "wodact.pcx", "mpyscrn-md", 24, 21},
    {"wodref", "ra2md/localmd", "wodref.pcx", "mpyscrn-md", 24, 24},
    {"woldclan", "ra2md/localmd", "woldclan.pcx", "mpyscrn-md", 16, 20},
    {"woldpriv", "ra2md/localmd", "woldpriv.pcx", "mpyscrn-md", 16, 20},
    {"woldrslkob", "ra2md/localmd", "woldrslkob.pcx", "mpyscrn-md", 16, 20},
    {"wouact", "ra2md/localmd", "wouact.pcx", "mpyscrn-md", 24, 21},
    {"wouref", "ra2md/localmd", "wouref.pcx", "mpyscrn-md", 24, 24},
    {"woluclan", "ra2md/localmd", "woluclan.pcx", "mpyscrn-md", 16, 20},
    {"wolupriv", "ra2md/localmd", "wolupriv.pcx", "mpyscrn-md", 16, 20},
    {"wolurslkob", "ra2md/localmd", "wolurslkob.pcx", "mpyscrn-md", 16, 20},
    {"obs_alli", "ra2md/localmd", "obs_alli.pcx", "mplsobs", 121, 96},
    {"obs_sovi", "ra2md/localmd", "obs_sovi.pcx", "mplsobs", 121, 96},
    {"obs_yuri", "ra2md/localmd", "obs_yuri.pcx", "mplsobs", 121, 96},
    {"number0", "ra2md/localmd", "number0.pcx", "mpyscrn-md", 8, 16},
    {"number1", "ra2md/localmd", "number1.pcx", "mpyscrn-md", 8, 16},
    {"number2", "ra2md/localmd", "number2.pcx", "mpyscrn-md", 8, 16},
    {"number3", "ra2md/localmd", "number3.pcx", "mpyscrn-md", 8, 16},
    {"number4", "ra2md/localmd", "number4.pcx", "mpyscrn-md", 8, 16},
    {"number5", "ra2md/localmd", "number5.pcx", "mpyscrn-md", 8, 16},
    {"number6", "ra2md/localmd", "number6.pcx", "mpyscrn-md", 8, 16},
    {"number7", "ra2md/localmd", "number7.pcx", "mpyscrn-md", 8, 16},
    {"number8", "ra2md/localmd", "number8.pcx", "mpyscrn-md", 8, 16},
    {"number9", "ra2md/localmd", "number9.pcx", "mpyscrn-md", 8, 16},
    {"numbers", "ra2md/localmd", "numbers.pcx", "mpyscrn-md", 74, 16},
    {"private", "ra2md/localmd", "private.pcx", "mpyscrn-md", 15, 16},
    {"corporal", "ra2md/localmd", "corporal.pcx", "mpyscrn-md", 15, 16},
    {"sergeant", "ra2md/localmd", "sergeant.pcx", "mpyscrn-md", 15, 16},
    {"lieutena", "ra2md/localmd", "lieutena.pcx", "mpyscrn-md", 15, 16},
    {"major", "ra2md/localmd", "major.pcx", "mpyscrn-md", 16, 16},
    {"colonel", "ra2md/localmd", "colonel.pcx", "mpyscrn-md", 16, 16},
    {"general", "ra2md/localmd", "general.pcx", "mpyscrn-md", 15, 16},
    {"comchief", "ra2md/localmd", "comchief.pcx", "mpyscrn-md", 16, 16},
    {"stargen", "ra2md/localmd", "stargen.pcx", "mpyscrn-md", 15, 16},
    {"trofnew", "ra2md/localmd", "trofnew.pcx", "mpyscrn-md", 48, 22},
    {"yrii", "ra2md/localmd", "yrii.pcx", "yrii", 47, 23},
    {"mpyscrnlbar01", "expandmd01", "mpyscrnlbar01.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar02", "expandmd01", "mpyscrnlbar02.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar03", "expandmd01", "mpyscrnlbar03.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar04", "expandmd01", "mpyscrnlbar04.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar05", "expandmd01", "mpyscrnlbar05.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar06", "expandmd01", "mpyscrnlbar06.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar07", "expandmd01", "mpyscrnlbar07.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar08", "expandmd01", "mpyscrnlbar08.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar09", "expandmd01", "mpyscrnlbar09.pcx", "mpyscrn-expand", 440, 36},
    {"mpyscrnlbar10", "expandmd01", "mpyscrnlbar10.pcx", "mpyscrn-expand", 440, 36}
}};

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

std::string trimAscii(std::string_view value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1U));
}

std::uint64_t fnv1a64(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
    return bytes.empty() ? 0U : fnv1a64(bytes.data(), bytes.size());
}

void appendUniquePath(
    std::vector<fs::path>& paths,
    std::set<std::string>& seen,
    const fs::path& path
) {
    if (path.empty()) return;
    const std::string key = lowerAscii(path.lexically_normal().generic_string());
    if (seen.insert(key).second) paths.push_back(path);
}

std::vector<fs::path> internetSkinResourceCandidates(
    const RuntimeConfig& config,
    const std::string& relativeRoot,
    const std::string& fileName
) {
    std::vector<fs::path> paths;
    std::set<std::string> seen;
    const fs::path relative(relativeRoot);
    const auto addMountedRoot = [&](const std::string& rawRoot, bool localData) {
        if (trimAscii(rawRoot).empty()) return;
        const fs::path root(rawRoot);
        if (localData) {
            appendUniquePath(
                paths,
                seen,
                root / "resources" / "mix_exports" / relative / fileName
            );
        }
        appendUniquePath(paths, seen, root / relative / fileName);
        appendUniquePath(paths, seen, root / fileName);
    };
    addMountedRoot(config.modRootPath, false);
    addMountedRoot(config.localDataRootPath, true);
    addMountedRoot(config.resourceRootPath, true);
    return paths;
}

fs::path firstRegularFile(const std::vector<fs::path>& candidates) {
    for (const fs::path& candidate : candidates) {
        std::error_code error;
        if (fs::is_regular_file(candidate, error) && !error) return candidate;
    }
    return {};
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
    std::ostringstream output;
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        if (index != 0U) output << "; ";
        output << diagnostics[index];
    }
    return output.str();
}

bool loadInternetSkinPalette(
    const RuntimeConfig& config,
    const InternetSkinPaletteSpec& spec,
    RuntimeInternetMenuSkinPalette& output,
    std::string& diagnostic
) {
    output = RuntimeInternetMenuSkinPalette{};
    output.id = spec.id;
    const fs::path path = firstRegularFile(internetSkinResourceCandidates(
        config,
        spec.relativeRoot,
        spec.fileName
    ));
    if (path.empty()) {
        diagnostic = std::string(spec.fileName) + " palette missing";
        return false;
    }
    std::vector<RuntimePaletteColor> colors;
    std::string error;
    if (!loadRuntimePaletteFile(path.string(), colors, error)
        || colors.size() != 256U) {
        diagnostic = std::string(spec.fileName) + " palette invalid: " + error;
        return false;
    }
    output.source = path.generic_string();
    output.rgb.reserve(colors.size() * 3U);
    for (const RuntimePaletteColor& color : colors) {
        output.rgb.push_back(color.r);
        output.rgb.push_back(color.g);
        output.rgb.push_back(color.b);
    }
    output.pixelFingerprint = fnv1a64(output.rgb);
    return output.valid();
}

bool loadInternetSkinAsset(
    const RuntimeConfig& config,
    const InternetSkinAssetSpec& spec,
    const RuntimeInternetMenuSkin& skin,
    RuntimeInternetMenuSkinAsset& output,
    std::string& diagnostic
) {
    output = RuntimeInternetMenuSkinAsset{};
    output.id = spec.id;
    const fs::path path = firstRegularFile(internetSkinResourceCandidates(
        config,
        spec.relativeRoot,
        spec.fileName
    ));
    if (path.empty()) {
        diagnostic = std::string(spec.fileName) + " PCX missing";
        return false;
    }
    RuntimeShpFrameImage indexed;
    std::vector<RuntimePaletteColor> embeddedPalette;
    std::string error;
    if (!decodeRuntimePcxImageFile(
            path.string(),
            indexed,
            embeddedPalette,
            error
        )
        || indexed.width != spec.width
        || indexed.height != spec.height
        || indexed.pixels.size()
            != static_cast<std::size_t>(spec.width * spec.height)) {
        diagnostic = std::string(spec.fileName) + " PCX invalid/dimensions: "
            + error;
        return false;
    }

    const RuntimeInternetMenuSkinPalette* authored = skin.palette(
        spec.paletteId
    );
    if (authored == nullptr || !authored->valid()) {
        diagnostic = std::string(spec.fileName)
            + " authored palette unavailable";
        return false;
    }
    output.source = path.generic_string();
    output.paletteSource = authored->source;
    output.width = indexed.width;
    output.height = indexed.height;
    output.bgra.resize(indexed.pixels.size() * 4U, 0U);
    for (std::size_t pixel = 0; pixel < indexed.pixels.size(); ++pixel) {
        const std::size_t paletteAt = static_cast<std::size_t>(
            indexed.pixels[pixel]
        ) * 3U;
        const std::size_t destination = pixel * 4U;
        output.bgra[destination] = authored->rgb[paletteAt + 2U];
        output.bgra[destination + 1U] = authored->rgb[paletteAt + 1U];
        output.bgra[destination + 2U] = authored->rgb[paletteAt];
        output.bgra[destination + 3U] = 255U;
    }
    output.pixelFingerprint = fnv1a64(output.bgra);
    return output.valid();
}

std::string sourceFileName(const std::string& source) {
    return lowerAscii(fs::path(source).filename().string());
}

#if defined(_WIN32)
bool drawInternetSkinAsset(
    HDC dc,
    const RuntimeInternetMenuSkinAsset* asset,
    const RECT& destination
) {
    if (dc == nullptr || asset == nullptr || !asset->valid()
        || destination.right <= destination.left
        || destination.bottom <= destination.top) {
        return false;
    }
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(bitmap.bmiHeader);
    bitmap.bmiHeader.biWidth = asset->width;
    bitmap.bmiHeader.biHeight = -asset->height;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    return StretchDIBits(
        dc,
        destination.left,
        destination.top,
        destination.right - destination.left,
        destination.bottom - destination.top,
        0,
        0,
        asset->width,
        asset->height,
        asset->bgra.data(),
        &bitmap,
        DIB_RGB_COLORS,
        SRCCOPY
    ) != GDI_ERROR;
}

COLORREF internetSkinBackground(RuntimeInternetMenuSkinFaction faction) {
    switch (faction) {
    case RuntimeInternetMenuSkinFaction::Soviet:
        return RGB(25, 7, 7);
    case RuntimeInternetMenuSkinFaction::Yuri:
        return RGB(19, 6, 25);
    case RuntimeInternetMenuSkinFaction::Observer:
        return RGB(5, 19, 18);
    case RuntimeInternetMenuSkinFaction::Allied:
    default:
        return RGB(5, 11, 25);
    }
}

COLORREF internetSkinAccent(RuntimeInternetMenuSkinFaction faction) {
    switch (faction) {
    case RuntimeInternetMenuSkinFaction::Soviet:
        return RGB(174, 72, 52);
    case RuntimeInternetMenuSkinFaction::Yuri:
        return RGB(147, 94, 179);
    case RuntimeInternetMenuSkinFaction::Observer:
        return RGB(92, 166, 146);
    case RuntimeInternetMenuSkinFaction::Allied:
    default:
        return RGB(88, 137, 194);
    }
}

const wchar_t* internetSkinPageName(RuntimeInternetMenuSkinPage page) {
    switch (page) {
    case RuntimeInternetMenuSkinPage::Account:
        return L"WESTWOOD ONLINE ACCOUNT";
    case RuntimeInternetMenuSkinPage::Community:
        return L"CHANNELS / CHAT / BUDDY LIST";
    case RuntimeInternetMenuSkinPage::Browser:
        return L"CUSTOM MATCH ROOMS";
    case RuntimeInternetMenuSkinPage::Ladder:
        return L"CLAN / QUICK MATCH LADDER";
    case RuntimeInternetMenuSkinPage::News:
        return L"WESTWOOD ONLINE NEWS";
    case RuntimeInternetMenuSkinPage::MatchHistory:
        return L"\u6392\u4f4d\u5bf9\u5c40\u8bb0\u5f55";
    case RuntimeInternetMenuSkinPage::Lobby:
        return L"MATCH LOBBY / OBSERVER";
    case RuntimeInternetMenuSkinPage::Kicked:
    default:
        return L"CONNECTION STATUS";
    }
}

bool writeInternetSkinBmp(
    const fs::path& path,
    const RuntimeInternetMenuSkin& skin,
    RuntimeInternetMenuSkinPage page,
    RuntimeInternetMenuSkinFaction faction,
    std::uint32_t ladderRank,
    std::string& errorMessage
) {
    constexpr int width = 766;
    constexpr int height = 550;
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(
        screen,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0U
    );
    ReleaseDC(nullptr, screen);
    if (memory == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (memory != nullptr) DeleteDC(memory);
        errorMessage = "could not create Internet skin DIB";
        return false;
    }
    HGDIOBJ previous = SelectObject(memory, bitmap);
    const bool drawn = drawRuntimeInternetMenuSkinSurface(
        memory,
        RECT{0, 0, width, height},
        skin,
        page,
        faction,
        ladderRank
    );
    GdiFlush();

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const DWORD pixelBytes = static_cast<DWORD>(width * height * 4);
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;
    bitmapInfo.bmiHeader.biSizeImage = pixelBytes;
    output.write(
        reinterpret_cast<const char*>(&fileHeader),
        sizeof(fileHeader)
    );
    output.write(
        reinterpret_cast<const char*>(&bitmapInfo.bmiHeader),
        sizeof(bitmapInfo.bmiHeader)
    );
    output.write(reinterpret_cast<const char*>(bits), pixelBytes);
    const bool written = drawn && output.good();
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    if (!written) errorMessage = "could not render/write Internet skin BMP";
    return written;
}
#endif

fs::path suffixedOutputPath(const fs::path& base, const std::string& suffix) {
    return base.parent_path()
        / (base.stem().string() + suffix
            + (base.extension().empty() ? ".bmp" : base.extension().string()));
}

} // namespace

const RuntimeInternetMenuSkinPalette* RuntimeInternetMenuSkin::palette(
    std::string_view id
) const {
    const auto found = palettes.find(lowerAscii(trimAscii(id)));
    return found == palettes.end() ? nullptr : &found->second;
}

const RuntimeInternetMenuSkinAsset* RuntimeInternetMenuSkin::asset(
    std::string_view id
) const {
    const auto found = assets.find(lowerAscii(trimAscii(id)));
    return found == assets.end() ? nullptr : &found->second;
}

bool loadRuntimeInternetMenuSkin(
    const RuntimeConfig& config,
    RuntimeInternetMenuSkin& skin
) {
    RuntimeInternetMenuSkin loaded;
    for (const InternetSkinPaletteSpec& spec : paletteSpecs) {
        RuntimeInternetMenuSkinPalette palette;
        std::string diagnostic;
        if (loadInternetSkinPalette(config, spec, palette, diagnostic)) {
            loaded.palettes.emplace(lowerAscii(spec.id), std::move(palette));
        } else {
            loaded.diagnostics.push_back(std::move(diagnostic));
        }
    }
    for (const InternetSkinAssetSpec& spec : assetSpecs) {
        RuntimeInternetMenuSkinAsset asset;
        std::string diagnostic;
        if (loadInternetSkinAsset(config, spec, loaded, asset, diagnostic)) {
            loaded.assets.emplace(lowerAscii(spec.id), std::move(asset));
        } else {
            loaded.diagnostics.push_back(std::move(diagnostic));
        }
    }
    loaded.productionReady = loaded.palettes.size() == paletteSpecs.size()
        && loaded.assets.size() == assetSpecs.size()
        && loaded.diagnostics.empty();
    skin = std::move(loaded);
    return skin.productionReady;
}

RuntimeInternetMenuSkinFaction runtimeInternetMenuSkinFactionForCountry(
    std::string_view countryId,
    bool observer
) {
    if (observer) return RuntimeInternetMenuSkinFaction::Observer;
    const std::string country = lowerAscii(trimAscii(countryId));
    if (country.find("yuri") != std::string::npos
        || country.find("psychic") != std::string::npos) {
        return RuntimeInternetMenuSkinFaction::Yuri;
    }
    static constexpr const char* SovietTokens[] = {
        "russia", "soviet", "confeder", "africa", "arab", "iraq", "cuba",
        "libya"
    };
    for (const char* token : SovietTokens) {
        if (country.find(token) != std::string::npos) {
            return RuntimeInternetMenuSkinFaction::Soviet;
        }
    }
    return RuntimeInternetMenuSkinFaction::Allied;
}

std::string runtimeInternetMenuSkinFactionName(
    RuntimeInternetMenuSkinFaction faction
) {
    switch (faction) {
    case RuntimeInternetMenuSkinFaction::Soviet:
        return "Soviet";
    case RuntimeInternetMenuSkinFaction::Yuri:
        return "Yuri";
    case RuntimeInternetMenuSkinFaction::Observer:
        return "Observer";
    case RuntimeInternetMenuSkinFaction::Allied:
    default:
        return "Allied";
    }
}

std::string runtimeInternetMenuSkinScrollAsset(
    RuntimeInternetMenuSkinPage page,
    RuntimeInternetMenuSkinFaction faction
) {
    int index = 1;
    switch (page) {
    case RuntimeInternetMenuSkinPage::Account:
        index = 1;
        break;
    case RuntimeInternetMenuSkinPage::Community:
        index = 2;
        break;
    case RuntimeInternetMenuSkinPage::Browser:
        index = 3;
        break;
    case RuntimeInternetMenuSkinPage::Ladder:
    case RuntimeInternetMenuSkinPage::MatchHistory:
        index = 4;
        break;
    case RuntimeInternetMenuSkinPage::News:
        index = 5;
        break;
    case RuntimeInternetMenuSkinPage::Lobby:
        index = faction == RuntimeInternetMenuSkinFaction::Allied ? 6
            : faction == RuntimeInternetMenuSkinFaction::Soviet ? 7
            : faction == RuntimeInternetMenuSkinFaction::Yuri ? 8
            : 9;
        break;
    case RuntimeInternetMenuSkinPage::Kicked:
        index = 10;
        break;
    }
    std::ostringstream id;
    id << "mpyscrnlbar" << std::setw(2) << std::setfill('0') << index;
    return id.str();
}

std::string runtimeInternetMenuSkinRankAsset(std::uint32_t ladderRank) {
    static constexpr std::array<const char*, 9> ordered{{
        "comchief", "general", "stargen", "colonel", "major",
        "lieutena", "sergeant", "corporal", "private"
    }};
    if (ladderRank == 0U) return "private";
    return ordered[std::min<std::size_t>(
        ordered.size() - 1U,
        static_cast<std::size_t>(ladderRank - 1U)
    )];
}

#if defined(_WIN32)
bool drawRuntimeInternetMenuSkinSurface(
    HDC dc,
    const RECT& bounds,
    const RuntimeInternetMenuSkin& skin,
    RuntimeInternetMenuSkinPage page,
    RuntimeInternetMenuSkinFaction faction,
    std::uint32_t ladderRank
) {
    if (dc == nullptr || !skin.productionReady) return false;
    HBRUSH background = CreateSolidBrush(internetSkinBackground(faction));
    FillRect(dc, &bounds, background);
    DeleteObject(background);

    const COLORREF accent = internetSkinAccent(faction);
    HPEN accentPen = CreatePen(PS_SOLID, 1, accent);
    HGDIOBJ oldPen = SelectObject(dc, accentPen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 8, 8, bounds.right - 8, bounds.bottom - 8);
    Rectangle(dc, 16, 90, bounds.right - 16, bounds.bottom - 52);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(accentPen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(218, 205, 148));
    RECT title{24, 64, std::max<LONG>(25, bounds.right - 160), 88};
    if (page == RuntimeInternetMenuSkinPage::MatchHistory) title = RECT{180, 54, bounds.right - 160, 84};
    DrawTextW(
        dc,
        internetSkinPageName(page),
        -1,
        &title,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE
    );
    const std::string factionName = runtimeInternetMenuSkinFactionName(faction);
    const std::wstring factionText = L"ORIGINAL PCX: "
        + std::wstring(factionName.begin(), factionName.end());
    RECT factionRect{24, 42, 270, 62};
    if (page != RuntimeInternetMenuSkinPage::MatchHistory)
        DrawTextW(dc, factionText.c_str(), -1, &factionRect, DT_LEFT | DT_SINGLELINE);

    const RuntimeInternetMenuSkinAsset* sideAsset = nullptr;
    if (faction == RuntimeInternetMenuSkinFaction::Allied) {
        sideAsset = skin.asset("obs_alli");
    } else if (faction == RuntimeInternetMenuSkinFaction::Soviet) {
        sideAsset = skin.asset("obs_sovi");
    } else if (faction == RuntimeInternetMenuSkinFaction::Yuri) {
        sideAsset = skin.asset("obs_yuri");
    }
    if (faction == RuntimeInternetMenuSkinFaction::Observer) {
        const int top = 98;
        const int left = std::max(24L, bounds.right - 264L);
        drawInternetSkinAsset(
            dc, skin.asset("obs_alli"), RECT{left, top, left + 78, top + 62});
        drawInternetSkinAsset(
            dc, skin.asset("obs_sovi"), RECT{left + 86, top, left + 164, top + 62});
        drawInternetSkinAsset(
            dc, skin.asset("obs_yuri"), RECT{left + 172, top, left + 250, top + 62});
        drawInternetSkinAsset(
            dc,
            skin.asset("wolurslkob"),
            RECT{bounds.right - 48, 16, bounds.right - 32, 36}
        );
    } else {
        drawInternetSkinAsset(
            dc,
            sideAsset,
            RECT{bounds.right - 137, 94, bounds.right - 16, 190}
        );
    }
    if (faction == RuntimeInternetMenuSkinFaction::Yuri) {
        drawInternetSkinAsset(
            dc,
            skin.asset("yrii"),
            RECT{bounds.right - 196, 16, bounds.right - 149, 39}
        );
    }

    drawInternetSkinAsset(
        dc,
        skin.asset(page == RuntimeInternetMenuSkinPage::Community
            ? "wodact" : "wouact"),
        RECT{bounds.right - 130, 16, bounds.right - 106, 37}
    );
    drawInternetSkinAsset(
        dc,
        skin.asset("wouref"),
        RECT{bounds.right - 96, 14, bounds.right - 72, 38}
    );
    drawInternetSkinAsset(
        dc,
        skin.asset(page == RuntimeInternetMenuSkinPage::Ladder
            ? "woldclan" : "woluclan"),
        RECT{bounds.right - 62, 17, bounds.right - 46, 37}
    );

    if (page == RuntimeInternetMenuSkinPage::Ladder) {
        drawInternetSkinAsset(
            dc,
            skin.asset("trofnew"),
            RECT{24, 98, 72, 120}
        );
        drawInternetSkinAsset(
            dc,
            skin.asset(runtimeInternetMenuSkinRankAsset(ladderRank)),
            RECT{82, 101, 98, 117}
        );
        const std::string rankText = std::to_string(ladderRank);
        int digitX = 108;
        for (char digit : rankText) {
            const std::string digitId = std::string("number") + digit;
            drawInternetSkinAsset(
                dc,
                skin.asset(digitId),
                RECT{digitX, 101, digitX + 8, 117}
            );
            digitX += 8;
        }
    }

    const RuntimeInternetMenuSkinAsset* scroll = skin.asset(
        runtimeInternetMenuSkinScrollAsset(page, faction)
    );
    const int scrollWidth = std::min<int>(
        440,
        std::max<LONG>(1, bounds.right - bounds.left - 80)
    );
    const int scrollLeft = (bounds.right - scrollWidth) / 2;
    drawInternetSkinAsset(
        dc,
        scroll,
        RECT{
            scrollLeft,
            bounds.bottom - 44,
            scrollLeft + scrollWidth,
            bounds.bottom - 8
        }
    );
    return true;
}

bool drawRuntimeInternetMenuSkinButton(
    HDC dc,
    const RECT& bounds,
    const RuntimeInternetMenuSkin& skin,
    std::string_view releasedAsset,
    std::string_view pressedAsset,
    std::wstring_view text,
    bool pressed,
    bool disabled
) {
    if (dc == nullptr || !skin.productionReady) return false;
    const RuntimeInternetMenuSkinAsset* icon = skin.asset(
        pressed ? pressedAsset : releasedAsset
    );
    if (icon == nullptr || !icon->valid()) return false;
    HBRUSH fill = CreateSolidBrush(
        pressed ? RGB(35, 46, 41) : RGB(17, 26, 24)
    );
    FillRect(dc, &bounds, fill);
    DeleteObject(fill);
    FrameRect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    const int iconHeight = std::min<int>(
        icon->height,
        std::max<LONG>(1, bounds.bottom - bounds.top - 6)
    );
    const int iconWidth = std::max(
        1,
        icon->width * iconHeight / std::max(1, icon->height)
    );
    const int iconTop = bounds.top + (bounds.bottom - bounds.top - iconHeight) / 2;
    drawInternetSkinAsset(
        dc,
        icon,
        RECT{bounds.left + 5, iconTop, bounds.left + 5 + iconWidth,
            iconTop + iconHeight}
    );
    RECT textRect = bounds;
    textRect.left += iconWidth + 10;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(110, 110, 100) : RGB(224, 218, 176));
    DrawTextW(
        dc,
        text.data(),
        static_cast<int>(text.size()),
        &textRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    return true;
}
#endif

int runRuntimeInternetMenuSkinSmokeTest(
    const RuntimeConfig& config,
    const std::string& outputPath
) {
#if !defined(_WIN32)
    (void)config;
    (void)outputPath;
    std::cerr << "Internet menu skin smoke requires Win32\n";
    return 1;
#else
    RuntimeInternetMenuSkin skin;
    if (!loadRuntimeInternetMenuSkin(config, skin)) {
        std::cerr << "Internet menu skin smoke failed: "
                  << joinDiagnostics(skin.diagnostics) << "\n";
        return 1;
    }
    const auto requireAsset = [&](const std::string& id) {
        const RuntimeInternetMenuSkinAsset* asset = skin.asset(id);
        if (asset == nullptr || !asset->valid()
            || asset->pixelFingerprint == 0U
            || sourceFileName(asset->source) != id + ".pcx") {
            std::cerr << "Internet menu skin smoke failed: invalid source/pixels for "
                      << id << "\n";
            return false;
        }
        return true;
    };
    for (const InternetSkinAssetSpec& spec : assetSpecs) {
        const RuntimeInternetMenuSkinAsset* asset = skin.asset(spec.id);
        const RuntimeInternetMenuSkinPalette* palette = skin.palette(
            spec.paletteId
        );
        if (!requireAsset(spec.id) || asset->width != spec.width
            || asset->height != spec.height || palette == nullptr
            || asset->paletteSource != palette->source) {
            std::cerr << "Internet menu skin smoke failed: dimension/palette contract for "
                      << spec.id << "\n";
            return 1;
        }
    }
    for (const InternetSkinPaletteSpec& spec : paletteSpecs) {
        const RuntimeInternetMenuSkinPalette* palette = skin.palette(spec.id);
        if (palette == nullptr || !palette->valid()
            || palette->pixelFingerprint == 0U
            || sourceFileName(palette->source) != spec.fileName) {
            std::cerr << "Internet menu skin smoke failed: palette contract for "
                      << spec.id << "\n";
            return 1;
        }
    }
    const RuntimeInternetMenuSkinAsset* observer = skin.asset("obs_alli");
    const RuntimeInternetMenuSkinAsset* wol = skin.asset("wouact");
    const RuntimeInternetMenuSkinAsset* scroll = skin.asset("mpyscrnlbar01");
    if (observer == nullptr || wol == nullptr || scroll == nullptr
        || sourceFileName(observer->paletteSource) != "mplsobs.pal"
        || sourceFileName(wol->paletteSource) != "mpyscrn.pal"
        || sourceFileName(scroll->paletteSource) != "mpyscrn.pal"
        || lowerAscii(scroll->source).find("/expandmd01/")
            == std::string::npos) {
        std::cerr << "Internet menu skin smoke failed: palette/source routing\n";
        return 1;
    }
    constexpr std::uint64_t expectedObserverPixels = 0x51ccb6cb0e2f71e3ULL;
    constexpr std::uint64_t expectedWolPixels = 0x21c43035a8b72dc3ULL;
    constexpr std::uint64_t expectedScrollPixels = 0x2af6678f5a14f0fbULL;
    if (observer->pixelFingerprint != expectedObserverPixels
        || wol->pixelFingerprint != expectedWolPixels
        || scroll->pixelFingerprint != expectedScrollPixels) {
        std::cerr << "Internet menu skin smoke failed: decoded pixel fingerprint"
                  << " observer=0x" << std::hex
                  << observer->pixelFingerprint
                  << " wol=0x" << wol->pixelFingerprint
                  << " scroll=0x" << scroll->pixelFingerprint << std::dec
                  << "\n";
        return 1;
    }
    constexpr std::array<std::tuple<
        RuntimeInternetMenuSkinPage,
        RuntimeInternetMenuSkinFaction,
        const char*
    >, 10> scrollContracts{{
        {RuntimeInternetMenuSkinPage::Account,
            RuntimeInternetMenuSkinFaction::Allied, "mpyscrnlbar01"},
        {RuntimeInternetMenuSkinPage::Community,
            RuntimeInternetMenuSkinFaction::Allied, "mpyscrnlbar02"},
        {RuntimeInternetMenuSkinPage::Browser,
            RuntimeInternetMenuSkinFaction::Allied, "mpyscrnlbar03"},
        {RuntimeInternetMenuSkinPage::Ladder,
            RuntimeInternetMenuSkinFaction::Allied, "mpyscrnlbar04"},
        {RuntimeInternetMenuSkinPage::News,
            RuntimeInternetMenuSkinFaction::Allied, "mpyscrnlbar05"},
        {RuntimeInternetMenuSkinPage::Lobby,
            RuntimeInternetMenuSkinFaction::Allied, "mpyscrnlbar06"},
        {RuntimeInternetMenuSkinPage::Lobby,
            RuntimeInternetMenuSkinFaction::Soviet, "mpyscrnlbar07"},
        {RuntimeInternetMenuSkinPage::Lobby,
            RuntimeInternetMenuSkinFaction::Yuri, "mpyscrnlbar08"},
        {RuntimeInternetMenuSkinPage::Lobby,
            RuntimeInternetMenuSkinFaction::Observer, "mpyscrnlbar09"},
        {RuntimeInternetMenuSkinPage::Kicked,
            RuntimeInternetMenuSkinFaction::Allied, "mpyscrnlbar10"}
    }};
    const bool scrollRoutingValid = std::all_of(
        scrollContracts.begin(),
        scrollContracts.end(),
        [](const auto& contract) {
            return runtimeInternetMenuSkinScrollAsset(
                std::get<0>(contract),
                std::get<1>(contract)
            ) == std::get<2>(contract);
        }
    );
    if (runtimeInternetMenuSkinFactionForCountry("Americans", false)
            != RuntimeInternetMenuSkinFaction::Allied
        || runtimeInternetMenuSkinFactionForCountry("Russians", false)
            != RuntimeInternetMenuSkinFaction::Soviet
        || runtimeInternetMenuSkinFactionForCountry("YuriCountry", false)
            != RuntimeInternetMenuSkinFaction::Yuri
        || runtimeInternetMenuSkinFactionForCountry("Americans", true)
            != RuntimeInternetMenuSkinFaction::Observer
        || !scrollRoutingValid) {
        std::cerr << "Internet menu skin smoke failed: side/page selection\n";
        return 1;
    }

    RuntimeConfig missingConfig = config;
    const fs::path missingRoot = fs::temp_directory_path()
        / ("concord-internet-skin-missing-"
            + std::to_string(GetCurrentProcessId()) + "-"
            + std::to_string(GetTickCount64()));
    missingConfig.modRootPath.clear();
    missingConfig.localDataRootPath = missingRoot.string();
    missingConfig.resourceRootPath = missingRoot.string();
    RuntimeInternetMenuSkin missingSkin;
    if (loadRuntimeInternetMenuSkin(missingConfig, missingSkin)
        || missingSkin.productionReady
        || missingSkin.diagnostics.empty()) {
        std::cerr << "Internet menu skin smoke failed: missing fallback\n";
        return 1;
    }

    const fs::path corruptRoot = fs::temp_directory_path()
        / ("concord-internet-skin-corrupt-"
            + std::to_string(GetCurrentProcessId()) + "-"
            + std::to_string(GetTickCount64()));
    const fs::path resources = corruptRoot / "resources";
    const fs::path exports = resources / "mix_exports";
    const fs::path ra2md = exports / "ra2md";
    const fs::path localmd = ra2md / "localmd";
    const fs::path corruptPcx = localmd / "wouact.pcx";
    std::error_code fileError;
    const bool directoriesReady = fs::create_directory(corruptRoot, fileError)
        && fs::create_directory(resources, fileError)
        && fs::create_directory(exports, fileError)
        && fs::create_directory(ra2md, fileError)
        && fs::create_directory(localmd, fileError);
    if (!directoriesReady || fileError) {
        std::cerr << "Internet menu skin smoke failed: corrupt fixture setup\n";
        return 1;
    }
    {
        std::ofstream corrupt(corruptPcx, std::ios::binary | std::ios::trunc);
        corrupt << "not-a-pcx";
    }
    RuntimeConfig corruptConfig = missingConfig;
    corruptConfig.localDataRootPath = corruptRoot.string();
    corruptConfig.resourceRootPath = corruptRoot.string();
    RuntimeInternetMenuSkin corruptSkin;
    const bool corruptRejected = !loadRuntimeInternetMenuSkin(
        corruptConfig,
        corruptSkin
    ) && std::any_of(
        corruptSkin.diagnostics.begin(),
        corruptSkin.diagnostics.end(),
        [](const std::string& diagnostic) {
            return lowerAscii(diagnostic).find("wouact.pcx")
                != std::string::npos;
        }
    );
    fs::remove(corruptPcx, fileError);
    fs::remove(localmd, fileError);
    fs::remove(ra2md, fileError);
    fs::remove(exports, fileError);
    fs::remove(resources, fileError);
    fs::remove(corruptRoot, fileError);
    if (!corruptRejected) {
        std::cerr << "Internet menu skin smoke failed: corrupt fallback\n";
        return 1;
    }

    const fs::path base(outputPath);
    const std::array<std::tuple<
        fs::path,
        RuntimeInternetMenuSkinPage,
        RuntimeInternetMenuSkinFaction,
        std::uint32_t
    >, 4> previews{{
        {base, RuntimeInternetMenuSkinPage::Ladder,
            RuntimeInternetMenuSkinFaction::Allied, 1U},
        {suffixedOutputPath(base, "_soviet"),
            RuntimeInternetMenuSkinPage::Community,
            RuntimeInternetMenuSkinFaction::Soviet, 4U},
        {suffixedOutputPath(base, "_yuri"),
            RuntimeInternetMenuSkinPage::Lobby,
            RuntimeInternetMenuSkinFaction::Yuri, 7U},
        {suffixedOutputPath(base, "_observer"),
            RuntimeInternetMenuSkinPage::Browser,
            RuntimeInternetMenuSkinFaction::Observer, 0U}
    }};
    std::string renderError;
    for (const auto& preview : previews) {
        if (!writeInternetSkinBmp(
                std::get<0>(preview),
                skin,
                std::get<1>(preview),
                std::get<2>(preview),
                std::get<3>(preview),
                renderError
            )) {
            std::cerr << "Internet menu skin smoke failed: "
                      << renderError << "\n";
            return 1;
        }
    }

    std::cout << "Internet menu skin smoke passed: assets="
              << skin.assets.size()
              << " palettes=" << skin.palettes.size()
              << " observer=" << observer->source
              << " observerPixels=0x" << std::hex
              << observer->pixelFingerprint
              << " wol=" << wol->source
              << " wolPixels=0x" << wol->pixelFingerprint
              << " scroll=" << scroll->source
              << " scrollPixels=0x" << scroll->pixelFingerprint
              << std::dec
              << " missingFallback=yes corruptFallback=yes outputs=";
    for (std::size_t index = 0; index < previews.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << std::get<0>(previews[index]).generic_string();
    }
    std::cout << "\n";
    return 0;
#endif
}
