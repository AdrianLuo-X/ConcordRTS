#include "runtime/menu/RuntimeCampaignMenuController.h"
#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include "runtime/window/win32/RuntimeMenuEventWindowWin32.h"
#include "ui/campaign/win32/RuntimeCampaignMenuPixelAdaptersWin32.h"
#include "ui/campaign/win32/RuntimeCampaignDeviceBitmapWin32.h"
#include "ui/text/win32/RuntimeCampaignSystemFontWin32.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui/campaign/RuntimeCampaignMenu.h"
#include "audio/menu/win32/RuntimeCampaignMapAudioWin32.h"
#include "runtime/host/RuntimeUiClockWin32.h"
#include "assets/codecs/PalDecoder.h"
#include "assets/text/CsfCodec.h"

#include "assets/audio/AudioDecoder.h"
#include "assets/audio/AudioSampleResolver.h"
#include "data/campaign/CampaignPresentationDecoder.h"
#include "data/campaign/CampaignProgress.h"
#include "assets/archive/MixArchive.h"
#include "render/core/RuntimeMapRenderer.h"
#include "ui/skirmish/RuntimeSkirmishMenu.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace concord::campaign_menu;

// MapSel media uses its authored 640x480 coordinate space.
// The RA2/YR menu shell is rendered separately at its native 800x600 size.
constexpr int kBaseWidth = 640;
constexpr int kBaseHeight = 480;
constexpr COLORREF kBrightGold = RGB(255, 229, 150);
constexpr COLORREF kText = RGB(221, 224, 205);





std::wstring widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count <= 0) {
        codePage = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(
            codePage,
            flags,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0
        );
    }
    if (count <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        codePage,
        flags,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        count
    );
    return output;
}

std::uint16_t readLe16(
    const std::vector<unsigned char>& bytes,
    std::size_t at
) {
    if (at + 2 > bytes.size()) return 0;
    return static_cast<std::uint16_t>(bytes[at])
        | (static_cast<std::uint16_t>(bytes[at + 1]) << 8);
}

std::uint32_t readLe32(
    const std::vector<unsigned char>& bytes,
    std::size_t at
) {
    if (at + 4 > bytes.size()) return 0;
    return static_cast<std::uint32_t>(bytes[at])
        | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[at + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

struct CampaignCsfCatalog {
    std::unordered_map<std::string, std::wstring> values;
    std::string source;

    bool append(const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        const std::vector<unsigned char> bytes(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        std::unordered_map<std::string, std::wstring> loadedValues;
        for (auto& entry : concord::csf::decodeEntries(bytes)) {
            const auto value = concord::csf::projectCarriageReturns(
                std::move(entry.firstValue), concord::csf::CarriageReturnPolicy::LineFeed);
            loadedValues[lowerAscii(entry.label)] = std::wstring(value.begin(), value.end());
        }
        if (loadedValues.empty()) return false;
        for (auto& [key, value] : loadedValues) {
            values[key] = std::move(value);
        }
        if (!source.empty()) source += ";";
        source += path.string();
        return true;
    }

    bool load(const fs::path& path) {
        values.clear();
        source.clear();
        return append(path);
    }

    std::wstring get(
        const std::string& key,
        const std::wstring& fallback = L""
    ) const {
        const auto found = values.find(lowerAscii(trimAscii(key)));
        return found == values.end() || found->second.empty()
            ? fallback
            : found->second;
    }
};



bool makeCampaignImage(
    const RuntimeShpFrameImage& frame,
    const std::vector<RuntimePaletteColor>& palette,
    bool transparentZero,
    const std::string& source,
    CampaignImage& output
) {
    output = {};
    if (palette.size() < 256 || frame.width <= 0 || frame.height <= 0
        || frame.pixels.size()
            < static_cast<std::size_t>(frame.width * frame.height)) {
        return false;
    }
    output.width = std::max(frame.globalWidth, frame.offsetX + frame.width);
    output.height = std::max(frame.globalHeight, frame.offsetY + frame.height);
    if (output.width <= 0 || output.height <= 0) return false;
    output.bgra.assign(
        static_cast<std::size_t>(output.width * output.height * 4),
        0
    );
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const unsigned char index = frame.pixels[
                static_cast<std::size_t>(y * frame.width + x)
            ];
            const int destinationX = frame.offsetX + x;
            const int destinationY = frame.offsetY + y;
            if (destinationX < 0 || destinationY < 0
                || destinationX >= output.width
                || destinationY >= output.height) {
                continue;
            }
            const RuntimePaletteColor color = palette[index];
            const std::size_t at = static_cast<std::size_t>(
                (destinationY * output.width + destinationX) * 4
            );
            output.bgra[at] = color.b;
            output.bgra[at + 1] = color.g;
            output.bgra[at + 2] = color.r;
            output.bgra[at + 3] = transparentZero && index == 0 ? 0 : 255;
        }
    }
    output.source = source;
    return true;
}

bool readFileBytes(
    const fs::path& path,
    std::vector<unsigned char>& bytes
) {
    bytes.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    return !bytes.empty();
}

bool readMixFileBytes(
    const fs::path& mixPath,
    const std::string& fileName,
    std::vector<unsigned char>& bytes,
    std::string& source
) {
    MixArchive archive;
    if (!archive.loadFromFile(mixPath.string()) || !archive.isIndexReadable()) {
        return false;
    }
    const MixEntry* entry = archive.findEntryByName(fileName);
    std::string error;
    if (entry == nullptr || !archive.readEntryData(*entry, bytes, error)) {
        return false;
    }
    source = mixPath.generic_string() + "!" + fileName;
    return true;
}

struct CampaignResourceRecord {
    bool found = false;
    bool fromMix = false;
    fs::path containerPath;
    std::string fileName;
    std::string edition;
    std::string source;
    std::vector<unsigned char> bytes;
};

enum class CampaignResourceEdition {
    Shared,
    Ra2,
    Yr
};

const char* campaignResourceEditionName(CampaignResourceEdition edition) {
    switch (edition) {
    case CampaignResourceEdition::Ra2:
        return "ra2";
    case CampaignResourceEdition::Yr:
        return "yr";
    case CampaignResourceEdition::Shared:
    default:
        return "shared";
    }
}

CampaignResourceEdition campaignResourceEditionForPath(const fs::path& path) {
    const std::string fileName = lowerAscii(path.filename().string());
    static constexpr const char* YrRootMixNames[] = {
        "expandmd01.mix",
        "expandmd02.mix",
        "langmd.mix",
        "mapsmd03.mix",
        "movmd03.mix",
        "multimd.mix",
        "ra2md.mix",
        "thememd.mix"
    };
    for (const char* candidate : YrRootMixNames) {
        if (fileName == candidate) return CampaignResourceEdition::Yr;
    }
    static constexpr const char* Ra2RootMixNames[] = {
        "expand01.mix",
        "language.mix",
        "maps01.mix",
        "maps02.mix",
        "movies01.mix",
        "movies02.mix",
        "ra2.mix",
        "theme.mix"
    };
    for (const char* candidate : Ra2RootMixNames) {
        if (fileName == candidate) return CampaignResourceEdition::Ra2;
    }

    bool ra2Path = false;
    for (const fs::path& component : path) {
        const std::string token = lowerAscii(component.string());
        if (token == "gamemd"
            || token == "ra2md"
            || token == "langmd"
            || token == "mapsmd03"
            || token == "yr"
            || token == "yuri") {
            return CampaignResourceEdition::Yr;
        }
        if (token == "game"
            || token == "ra2"
            || token == "language"
            || token == "maps01"
            || token == "maps02") {
            ra2Path = true;
        }
    }

    return ra2Path
        ? CampaignResourceEdition::Ra2
        : CampaignResourceEdition::Shared;
}

int campaignResourceEditionRank(
    CampaignResourceEdition edition,
    const std::string& sourceId,
    const std::string& resourceName
) {
    const std::string source = lowerAscii(trimAscii(sourceId));
    if (edition == CampaignResourceEdition::Shared) return 0;
    if (source == "ra2") {
        return edition == CampaignResourceEdition::Ra2 ? 1 : -1;
    }
    if (source == "yr") {
        if (edition == CampaignResourceEdition::Yr) return 1;
        if (edition != CampaignResourceEdition::Ra2) return -1;
        const std::string extension = lowerAscii(
            fs::path(resourceName).extension().string()
        );
        const bool editionSpecificMapMedia = extension == ".vqa"
            || extension == ".pcx"
            || extension == ".pal"
            || extension == ".aud";
        // YR is an expansion and its authored MapSel overlays reuse base SHP
        // data.  Its edition-specific palette, click map, VQA and voice media
        // must never fall through to a same-named RA2 payload.
        return editionSpecificMapMedia ? -1 : 2;
    }
    return -1;
}

int campaignMixSearchPriority(const fs::path& path) {
    const std::string lower = lowerAscii(path.generic_string());
    const std::string fileName = lowerAscii(path.filename().string());
    if (fileName == "expandmd02.mix") return -30;
    if (fileName == "expandmd01.mix") return -20;
    if (fileName == "ra2md.mix") return -10;
    if (lower.find("/ra2md/localmd.mix") != std::string::npos) return 0;
    if (lower.find("/ra2md/conqmd.mix") != std::string::npos) return 10;
    if (lower.find("/ra2md/") != std::string::npos) return 20;
    if (lower.find("/ra2/local.mix") != std::string::npos) return 30;
    if (lower.find("/ra2/conquer.mix") != std::string::npos) return 40;
    if (fileName == "expand01.mix") return 45;
    if (fileName == "ra2.mix") return 46;
    if (lower.find("/ra2/") != std::string::npos) return 50;
    return 100;
}

struct CampaignResourceLayer {
    CampaignResourceLayer() = default;
    CampaignResourceLayer(CampaignResourceLayer&&) noexcept = default;
    CampaignResourceLayer& operator=(CampaignResourceLayer&&) noexcept = default;
    CampaignResourceLayer(const CampaignResourceLayer&) = delete;
    CampaignResourceLayer& operator=(const CampaignResourceLayer&) = delete;

    fs::path root;
    struct PathRecord {
        fs::path path;
        CampaignResourceEdition edition = CampaignResourceEdition::Shared;
    };
    std::unordered_map<std::string, std::vector<PathRecord>> looseFiles;
    std::vector<PathRecord> mixPaths;
    std::vector<std::unique_ptr<MixArchive>> mixArchives;
    std::vector<bool> mixLoadAttempted;
};

// Map-selection assets are authored as loose files by many mods, but retail
// installations normally place them in MIX archives.  Each mounted layer is
// searched as a unit (loose files, then that layer's MIX order) so a mod MIX
// correctly overrides lower-priority loose retail data.
struct CampaignResourceStore {
    std::vector<CampaignResourceLayer> layers;
    std::unordered_map<std::string, CampaignResourceRecord> cache;
    bool indexed = false;

    static std::string normalizedName(const std::string& resourceId) {
        const std::string trimmed = trimAscii(resourceId);
        if (trimmed.empty()) return {};
        const fs::path path(trimmed);
        if (path.is_absolute() || path.has_root_name()
            || trimmed.find("..") != std::string::npos) {
            return {};
        }
        return lowerAscii(path.filename().string());
    }

    void configure(const RuntimeConfig& config) {
        layers.clear();
        cache.clear();
        indexed = false;

        std::unordered_set<std::string> seen;
        const auto addRoot = [&seen, this](const std::string& rawPath) {
            if (trimAscii(rawPath).empty()) return;
            std::error_code error;
            fs::path path = fs::weakly_canonical(fs::path(rawPath), error);
            if (error) {
                error.clear();
                path = fs::absolute(fs::path(rawPath), error);
            }
            if (error) return;
            const std::string key = lowerAscii(path.generic_string());
            if (seen.insert(key).second) {
                CampaignResourceLayer layer;
                layer.root = std::move(path);
                layers.push_back(std::move(layer));
            }
        };
        addRoot(config.modRootPath);
        addRoot(config.localDataRootPath);
        addRoot(config.resourceRootPath);
    }

    void ensureIndexed() {
        if (indexed) return;
        indexed = true;
        for (CampaignResourceLayer& layer : layers) {
            std::error_code error;
            if (!fs::is_directory(layer.root, error) || error) continue;
            std::vector<fs::path> files;
            fs::recursive_directory_iterator iterator(
                layer.root,
                fs::directory_options::skip_permission_denied,
                error
            );
            const fs::recursive_directory_iterator end;
            while (!error && iterator != end) {
                const fs::directory_entry entry = *iterator;
                iterator.increment(error);
                std::error_code typeError;
                if (entry.is_regular_file(typeError) && !typeError) {
                    files.push_back(entry.path());
                }
            }
            std::sort(
                files.begin(),
                files.end(),
                [](const fs::path& left, const fs::path& right) {
                    return lowerAscii(left.generic_string())
                        < lowerAscii(right.generic_string());
                }
            );
            for (const fs::path& path : files) {
                const std::string name = lowerAscii(path.filename().string());
                if (lowerAscii(path.extension().string()) == ".mix") {
                    layer.mixPaths.push_back({
                        path,
                        campaignResourceEditionForPath(path)
                    });
                } else if (!name.empty()) {
                    layer.looseFiles[name].push_back({
                        path,
                        campaignResourceEditionForPath(path)
                    });
                }
            }
            std::sort(
                layer.mixPaths.begin(),
                layer.mixPaths.end(),
                [](const CampaignResourceLayer::PathRecord& left,
                   const CampaignResourceLayer::PathRecord& right) {
                    const int leftPriority =
                        campaignMixSearchPriority(left.path);
                    const int rightPriority =
                        campaignMixSearchPriority(right.path);
                    if (leftPriority != rightPriority) {
                        return leftPriority < rightPriority;
                    }
                    return lowerAscii(left.path.generic_string())
                        < lowerAscii(right.path.generic_string());
                }
            );
            layer.mixArchives.resize(layer.mixPaths.size());
            layer.mixLoadAttempted.assign(layer.mixPaths.size(), false);
        }
    }

    MixArchive* ensureMixLoaded(
        CampaignResourceLayer& layer,
        std::size_t index
    ) {
        if (!layer.mixLoadAttempted[index]) {
            layer.mixLoadAttempted[index] = true;
            auto archive = std::make_unique<MixArchive>();
            if (archive->loadFromPath(layer.mixPaths[index].path)
                && archive->isIndexReadable()) {
                layer.mixArchives[index] = std::move(archive);
            }
        }
        return layer.mixArchives[index].get();
    }

    void prepare() {
        ensureIndexed();
        for (CampaignResourceLayer& layer : layers) {
            for (std::size_t index = 0; index < layer.mixPaths.size(); ++index) {
                ensureMixLoaded(layer, index);
            }
        }
    }

    const CampaignResourceRecord& load(
        const std::string& resourceId,
        const std::string& sourceId
    ) {
        ensureIndexed();
        const std::string name = normalizedName(resourceId);
        const std::string source = lowerAscii(trimAscii(sourceId));
        const std::string cacheKey = source + "|" + name;
        const auto cached = cache.find(cacheKey);
        if (cached != cache.end()) return cached->second;

        CampaignResourceRecord record;
        record.fileName = name;
        if (name.empty()) {
            return cache.emplace(cacheKey, std::move(record)).first->second;
        }
        for (CampaignResourceLayer& layer : layers) {
            for (int editionRank = 0; editionRank <= 2; ++editionRank) {
                const auto loose = layer.looseFiles.find(name);
                if (loose != layer.looseFiles.end()) {
                    for (const CampaignResourceLayer::PathRecord& candidate :
                            loose->second) {
                        if (campaignResourceEditionRank(
                                candidate.edition,
                                source,
                                name
                            ) != editionRank
                            || !readFileBytes(candidate.path, record.bytes)) {
                            continue;
                        }
                        record.found = true;
                        record.containerPath = candidate.path;
                        record.edition = campaignResourceEditionName(
                            candidate.edition
                        );
                        record.source = candidate.path.generic_string();
                        return cache.emplace(
                            cacheKey,
                            std::move(record)
                        ).first->second;
                    }
                }
                for (std::size_t index = 0;
                    index < layer.mixPaths.size();
                    ++index) {
                    const CampaignResourceLayer::PathRecord& candidate =
                        layer.mixPaths[index];
                    if (campaignResourceEditionRank(
                            candidate.edition,
                            source,
                            name
                        ) != editionRank) {
                        continue;
                    }
                    MixArchive* archive = ensureMixLoaded(layer, index);
                    if (archive == nullptr) continue;
                    const MixEntry* entry = archive->findEntryByName(name);
                    std::string error;
                    if (entry == nullptr
                        || !archive->readEntryData(
                            *entry,
                            record.bytes,
                            error
                        )) {
                        continue;
                    }
                    record.found = true;
                    record.fromMix = true;
                    record.containerPath = candidate.path;
                    record.edition = campaignResourceEditionName(
                        candidate.edition
                    );
                    record.source = candidate.path.generic_string()
                        + "!" + name;
                    return cache.emplace(
                        cacheKey,
                        std::move(record)
                    ).first->second;
                }
            }
        }
        return cache.emplace(cacheKey, std::move(record)).first->second;
    }
};

bool decodeCampaignPalette(
    const std::vector<unsigned char>& bytes,
    std::vector<RuntimePaletteColor>& palette
) {
    palette.clear();
    concord::assets::PalRawPalette raw;
    if (!concord::assets::decodePalBytes(
            bytes, raw, concord::assets::PalLengthPolicy::Prefix768)) return false;
    const auto colors = concord::assets::mapPalColors(
        raw, concord::assets::PalColorPolicy::ReplicatedSixBit);
    palette.resize(256);
    for (std::size_t index = 0; index < 256; ++index) {
        palette[index] = {colors[index].r, colors[index].g, colors[index].b};
    }
    return true;
}

struct CampaignPresentationAudioRecord {
    fs::path wavePath;
    std::string source;
};

std::string campaignAudioCacheKey(
    const std::string& sourceId,
    const std::string& sampleName
) {
    const std::string normalizedSample = lowerAscii(
        CampaignResourceStore::normalizedName(sampleName)
    );
    if (normalizedSample.empty()) return {};
    return lowerAscii(trimAscii(sourceId)) + "|" + normalizedSample;
}

bool writeCampaignAudioCache(
    const RuntimeConfig& config,
    const std::string& sampleName,
    const std::string& source,
    const std::vector<unsigned char>& wavBytes,
    fs::path& outputPath
) {
    if (wavBytes.empty()) return false;
    const fs::path cacheRoot = fs::path(config.localDataRootPath)
        / "runtime_audio_cache" / "campaign_map_selection";
    std::error_code error;
    fs::create_directories(cacheRoot, error);
    if (error) return false;

    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16)
         << static_cast<unsigned long long>(
                std::hash<std::string>{}(
                    lowerAscii(sampleName + "|" + source)
                )
            )
         << ".wav";
    outputPath = cacheRoot / name.str();
    error.clear();
    if (fs::is_regular_file(outputPath, error) && !error
        && fs::file_size(outputPath, error) == wavBytes.size() && !error) {
        return true;
    }
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(
        reinterpret_cast<const char*>(wavBytes.data()),
        static_cast<std::streamsize>(wavBytes.size())
    );
    return output.good();
}

std::optional<CampaignPresentationAudioRecord>
resolveCampaignPresentationAudio(
    const RuntimeConfig& config,
    CampaignResourceStore& resources,
    std::unordered_map<std::string, CampaignPresentationAudioRecord>&
        resolvedCache,
    std::unordered_set<std::string>& missingCache,
    const std::string& sourceId,
    const std::string& sampleName
) {
    const std::string key = campaignAudioCacheKey(sourceId, sampleName);
    if (key.empty() || missingCache.find(key) != missingCache.end()) {
        return std::nullopt;
    }
    const auto cached = resolvedCache.find(key);
    if (cached != resolvedCache.end()) return cached->second;

    const CampaignResourceRecord& resource = resources.load(
        sampleName,
        sourceId
    );
    if (resource.found) {
        std::vector<unsigned char> wavBytes;
        if (isWaveFileBytes(resource.bytes)) {
            wavBytes = resource.bytes;
        } else {
            AudioDecodeResult decoded = decodeAudToWave(resource.bytes);
            if (decoded.success) wavBytes = std::move(decoded.wavBytes);
        }
        fs::path cachePath;
        if (writeCampaignAudioCache(
                config,
                sampleName,
                resource.source,
                wavBytes,
                cachePath
            )) {
            CampaignPresentationAudioRecord resolved;
            resolved.wavePath = cachePath;
            resolved.source = resource.source;
            resolvedCache[key] = resolved;
            return resolved;
        }
    }

    const fs::path generalCache = fs::path(config.localDataRootPath)
        / "runtime_audio_cache";
    const std::array<std::string, 3> roots{
        config.modRootPath,
        config.localDataRootPath,
        config.resourceRootPath
    };
    for (const std::string& root : roots) {
        if (trimAscii(root).empty()) continue;
        AudioSampleResolveResult resolved = resolveAudioSampleToWaveFromPaths(
            {fs::path(root), generalCache},
            sampleName,
            sourceId
        );
        if (resolved.success && !resolved.wavFilePath.empty()) {
            CampaignPresentationAudioRecord audio;
            audio.wavePath = resolved.wavFilePath;
            audio.source = resolved.sourcePath;
            resolvedCache[key] = audio;
            return audio;
        }
    }
    missingCache.insert(key);
    return std::nullopt;
}

concord::audio::CampaignMapAudioDevice& campaignVoiceDevice() {
    static auto device = concord::audio::createCampaignMapAudioDeviceWin32(concord::audio::CampaignMapAudioChannel::Voice);
    return *device;
}

concord::audio::CampaignMapAudioDevice& campaignEffectDevice() {
    static auto device = concord::audio::createCampaignMapAudioDeviceWin32(concord::audio::CampaignMapAudioChannel::Effect);
    return *device;
}

bool playCampaignVoiceWave(const fs::path& path, int volume) {
    return campaignVoiceDevice().play(path, volume);
}

bool playCampaignEffectWave(const fs::path& path, int volume) {
    return campaignEffectDevice().play(path, volume);
}

void stopCampaignVoice() {
    campaignVoiceDevice().stopAndClose();
}

void stopCampaignMapEffect() {
    campaignEffectDevice().stopAndClose();
}



std::wstring missionLabel(
    const CampaignMissionDefinition& mission,
    const CampaignCsfCatalog& csf
) {
    std::wstring result = csf.get(mission.displayNameKey);
    if (result.empty()) result = csf.get(mission.briefingKey);
    if (result.empty()) result = widen(mission.id);
    if (result.size() > 96) {
        result.resize(93);
        result += L"...";
    }
    return result;
}

struct CampaignMenuState : RuntimeCampaignMenuServices, RuntimeCampaignMenuController {
    CampaignMenuState() : RuntimeCampaignMenuController(static_cast<RuntimeCampaignMenuServices&>(*this), runtimeWin32UiClock()) {}
    std::wstring text(const std::string&, const std::wstring&) const override;
    std::wstring widen(const std::string&) const override;
    std::wstring missionLabel(const CampaignMissionDefinition&) const override;
    std::wstring editionSuffix(const CampaignDefinition*) const override;
    std::string loadingScreenAsset(const std::string&) const override;
    std::string savePath(const std::string&,const std::string&,const std::string&) const override;
    const CampaignMapSelectionStage* findMapStage(const CampaignDefinition&,const std::string&) const override;
    const CampaignMissionDefinition* findMission(const std::string&,const std::string&) const override;
    bool saveProgress(const std::string&,const CampaignProgressData&,std::string&) override;
    bool validateSavePaths(const RuntimeConfig&,const SkirmishSaveInfo&,std::string&) const override;
    void refreshBackground() override;
    void refreshPresentation() override;
    void releaseMapCapture() override;
    void stopVoice() override;
    void stopMapEffect() override;
    void playMapSound(const CampaignMapSelectionSound&) override;
    void repaintValidWindow() override;
    void destroyWindow() override;
    int mapColorAt(int,int) const override;
    std::wstring saveTitle(const LoadSaveEntry&) const override;
    void updatePresentation() override;
    RuntimeInputPixelRect eventClientRect() const override;
    bool shiftPressed() const override;
    void repaintEvent(bool requireValid) override;
    void trackMouseLeave() override;
    void setHandCursor(bool) override;
    void focusEvent() override;
    void captureEvent() override;
    void releaseEventCapture() override;
    CampaignDatabase loadDatabase(const CampaignDatabaseOptions&) override;
    bool loadProgress(const std::string&, CampaignProgressData&, std::string&) override;
    std::vector<CampaignSaveInfo> campaignSaves(const std::string&) override;
    std::vector<SkirmishSaveInfo> skirmishSaves(const std::string&) override;
    std::wstring incompatibleSaveWarning(const std::string&) const override;
    RuntimeMenuEventWindowWin32 eventWindow;


    // Presentation-only borrowed time; never saved or part of campaign progress.
    fs::path exportRoot;
    RuntimeMapRenderContext assetContext;
    RuntimeMapRenderCache assetCache;
    CampaignResourceStore presentationResources;
    std::unordered_map<std::string, CampaignPresentationAudioRecord>
        presentationAudio;
    std::unordered_set<std::string> missingPresentationAudio;
    CampaignCsfCatalog csf;
    CampaignImage campaignShell;
    std::unordered_map<std::string, CampaignImage> shellImages;
    std::vector<RuntimePaletteColor> campaignPalette;
    std::vector<CampaignImage> campaignLogoFrames;
    CampaignImage missionBackground;
    std::string missionBackgroundKey;
    std::string missionBackgroundError;
    bool officialCampaignShellLoaded = false;
    bool suppressPresentationAudio = false;
    HWND window = nullptr;
};





std::wstring loadSaveTitle(const LoadSaveEntry& entry) {
    if (entry.kind == LoadSaveKind::Skirmish) {
        return L"[遭遇战] " + widen(
            fs::path(entry.skirmish.mapPath).filename().string()
        );
    }
    return L"[战役] "
        + widen(entry.campaign.campaignId + "/" + entry.campaign.missionId);
}

std::wstring loadSaveSummary(const LoadSaveEntry& entry) {
    if (entry.kind == LoadSaveKind::Skirmish) {
        return widen(entry.skirmish.gameMode)
            + L"  |  tick " + std::to_wstring(entry.skirmish.tick);
    }
    return widen(entry.campaign.difficulty)
        + L"  |  tick " + std::to_wstring(entry.campaign.tick);
}

bool pathIsWithinRoot(
    const fs::path& candidate,
    const fs::path& root
) {
    if (trimAscii(root.string()).empty()) {
        return false;
    }
    std::error_code error;
    const fs::path absoluteCandidate = fs::weakly_canonical(candidate, error);
    if (error) {
        return false;
    }
    const fs::path absoluteRoot = fs::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    const std::string candidateText = lowerAscii(
        absoluteCandidate.generic_string()
    );
    std::string rootText = lowerAscii(absoluteRoot.generic_string());
    while (!rootText.empty() && rootText.back() == '/') {
        rootText.pop_back();
    }
    return candidateText == rootText
        || (candidateText.size() > rootText.size()
            && candidateText.compare(0, rootText.size(), rootText) == 0
            && candidateText[rootText.size()] == '/');
}

bool pathIsWithinMountedRoots(
    const RuntimeConfig& config,
    const fs::path& candidate
) {
    return pathIsWithinRoot(candidate, config.localDataRootPath)
        || pathIsWithinRoot(candidate, config.resourceRootPath)
        || (!trimAscii(config.modRootPath).empty()
            && pathIsWithinRoot(candidate, config.modRootPath));
}

bool validateSkirmishSaveStartupPaths(
    const RuntimeConfig& config,
    const SkirmishSaveInfo& save,
    std::string& errorMessage
) {
    std::error_code error;
    const fs::path mapPath(save.mapPath);
    const std::string mapExtension = lowerAscii(mapPath.extension().string());
    if ((mapExtension != ".map" && mapExtension != ".mpr")
        || !fs::is_regular_file(mapPath, error)
        || error
        || !pathIsWithinMountedRoots(config, mapPath)) {
        errorMessage = "saved map is missing or outside mounted resource roots";
        return false;
    }

    const fs::path configuredRules(trimAscii(save.gameModeRulesOverride));
    if (configuredRules.empty()) {
        return true;
    }
    if (lowerAscii(configuredRules.extension().string()) != ".ini") {
        errorMessage = "saved game-mode rules are not an INI resource";
        return false;
    }
    if (configuredRules.is_absolute()) {
        error.clear();
        if (!fs::is_regular_file(configuredRules, error)
            || error
            || !pathIsWithinMountedRoots(config, configuredRules)) {
            errorMessage =
                "saved game-mode rules are missing or outside mounted roots";
            return false;
        }
        return true;
    }
    if (configuredRules.has_parent_path()
        || configuredRules.filename() != configuredRules) {
        errorMessage = "saved game-mode rules use an unsafe relative path";
        return false;
    }

    const fs::path localRoot(config.localDataRootPath);
    const std::array<fs::path, 4> candidates{{
        localRoot / "resources" / "mix_exports" / "expandmd01"
            / configuredRules,
        localRoot / "resources" / "mix_exports" / "ra2md" / "localmd"
            / configuredRules,
        localRoot / "resources" / "mix_exports" / "ra2md"
            / configuredRules,
        localRoot / configuredRules
    }};
    for (const fs::path& candidate : candidates) {
        error.clear();
        if (fs::is_regular_file(candidate, error) && !error) {
            return true;
        }
    }
    errorMessage = "saved game-mode rules are not available in local resources";
    return false;
}

















RECT campaignMapMediaRect(const CampaignMapPresentationState& presentation) {
    int sourceWidth = presentation.vqaFrame.width;
    int sourceHeight = presentation.vqaFrame.height;
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        sourceWidth = presentation.clickMap.width;
        sourceHeight = presentation.clickMap.height;
    }
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return RECT{0, 0, kBaseWidth, kBaseHeight};
    }
    int width = kBaseWidth;
    int height = std::max(
        1,
        static_cast<int>(
            std::lround(
                static_cast<double>(sourceHeight) * kBaseWidth / sourceWidth
            )
        )
    );
    if (height > kBaseHeight) {
        height = kBaseHeight;
        width = std::max(
            1,
            static_cast<int>(
                std::lround(
                    static_cast<double>(sourceWidth) * kBaseHeight
                        / sourceHeight
                )
            )
        );
    }
    const int left = (kBaseWidth - width) / 2;
    return RECT{left, 0, left + width, height};
}

int campaignMapColorAtBasePoint(
    const CampaignMenuState& state,
    int baseX,
    int baseY
) {
    const CampaignMapPresentationState& presentation = state.mapPresentation;
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    if (presentation.phase != CampaignMapPresentationPhase::Interactive
        || !presentation.hasClickMap() || stage == nullptr
        || baseX < 0 || baseY < 0
        || baseX >= kBaseWidth || baseY >= kBaseHeight) {
        return 0;
    }
    const RECT media = campaignMapMediaRect(presentation);
    if (baseX < media.left || baseX >= media.right
        || baseY < media.top || baseY >= media.bottom) {
        return 0;
    }
    const int sourceX = std::clamp<int>(
        static_cast<int>(baseX - media.left) * presentation.clickMap.width
            / std::max(1, static_cast<int>(media.right - media.left)),
        0,
        presentation.clickMap.width - 1
    );
    const int sourceY = std::clamp<int>(
        static_cast<int>(baseY - media.top) * presentation.clickMap.height
            / std::max(1, static_cast<int>(media.bottom - media.top)),
        0,
        presentation.clickMap.height - 1
    );
    const unsigned char color = presentation.clickMap.pixels[
        static_cast<std::size_t>(
            sourceY * presentation.clickMap.width + sourceX
        )
    ];
    return mapChoiceByColor(stage, color) == nullptr ? 0 : color;
}







std::wstring campaignEditionSuffix(const CampaignDefinition* campaign) {
    if (campaignSourceIs(campaign, "ra2")) {
        return L"（红色警戒 2）";
    }
    if (campaignSourceIs(campaign, "yr")) {
        return L"（尤里的复仇）";
    }
    return L"";
}

const CampaignMissionDefinition* visualMission(
    const CampaignMenuState& state
) {
    const LoadSaveEntry* entry = selectedSave(state);
    if (state.page == CampaignPage::Saves
        && entry != nullptr
        && entry->kind == LoadSaveKind::Campaign) {
        const CampaignSaveInfo& save = entry->campaign;
        if (const CampaignMissionDefinition* mission =
                state.database.findMission(save.campaignId, save.missionId)) {
            return mission;
        }
    }
    return selectedMission(state);
}

void refreshMissionBackground(CampaignMenuState& state) {
    const CampaignMissionDefinition* mission = visualMission(state);
    if (mission == nullptr) {
        state.missionBackground = {};
        state.missionBackgroundKey.clear();
        return;
    }
    const std::string key = lowerAscii(
        mission->loadingBackground800 + "|" + mission->loadingPalette800
    );
    if (key == state.missionBackgroundKey
        && state.missionBackground.valid()) {
        return;
    }
    state.missionBackground = {};
    state.missionBackgroundKey = key;
    state.missionBackgroundError.clear();
    if (mission->loadingBackground800.empty()
        || mission->loadingPalette800.empty()) {
        state.missionBackgroundError =
            "missionmd.ini has no LS800 background metadata";
        return;
    }

    std::vector<RuntimePaletteColor> palette;
    std::string paletteSource;
    std::string error;
    if (!loadRuntimePaletteResource(
            state.assetContext,
            state.assetCache,
            mission->loadingPalette800,
            palette,
            paletteSource,
            error
        )) {
        state.missionBackgroundError =
            "palette " + mission->loadingPalette800 + ": " + error;
        return;
    }
    RuntimeShpFrameImage frame;
    std::string resolved;
    std::string source;
    if (!loadRuntimeAuxiliaryShpFrameImage(
            state.assetContext,
            state.assetCache,
            mission->loadingBackground800,
            0,
            frame,
            resolved,
            source,
            error,
            false
        )
        || !makeCampaignImage(
            frame,
            palette,
            false,
            source,
            state.missionBackground
        )) {
        state.missionBackgroundError =
            "background " + mission->loadingBackground800 + ": " + error;
    }
}

bool loadCampaignShellImage(
    CampaignMenuState& state, const std::string& id,
    const std::string& paletteId, int frameIndex, bool transparent,
    CampaignImage& image
) {
    std::vector<RuntimePaletteColor> palette;
    RuntimeShpFrameImage frame;
    std::string source, error;
    // Campaign palettes live in NTRLMD.MIX, outside the map renderer's
    // ordinary palette candidates. Resolve the matching SHP/PAL pair from
    // the mounted menu resource store, including mod and loose-file overrides.
    const auto& expansionPalette = state.presentationResources.load(paletteId, "yr");
    // Shared shell palettes are inherited from RA2. Keep this explicit menu
    // fallback separate from edition-bound MapSel media resolution.
    const auto& paletteResource = expansionPalette.found ? expansionPalette
        : state.presentationResources.load(paletteId, "ra2");
    const auto& resource = state.presentationResources.load(id, "yr");
    bool loaded = paletteResource.found && decodeCampaignPalette(paletteResource.bytes, palette);
    if (!loaded) error = "palette resource not found or invalid: " + paletteId;
    if (loaded && resource.found) {
        source = resource.source;
        loaded = resource.fromMix
            ? loadRuntimeShpFrameFromMixArchive(resource.containerPath.string(), state.assetCache,
                  id, frameIndex, frame, source, error)
            : decodeRuntimeShpFrameFile(resource.containerPath.string(), frameIndex, frame, error);
    } else if (!resource.found) {
        loaded = false;
        error = "SHP resource not found: " + id;
    }
    loaded = loaded && makeCampaignImage(frame, palette, transparent, source, image);
    if (!loaded) std::cerr << "Campaign shell asset " << id << " / " << paletteId
                          << ": " << error << "\n";
    return loaded;
}

bool initializeCampaignAssets(CampaignMenuState& state) {
    state.exportRoot = fs::path(state.config.localDataRootPath)
        / "resources" / "mix_exports";
    state.presentationResources.configure(state.config);
    if (!state.loadGame) {
        // Do the one-time filesystem/MIX index work before creating the modal
        // campaign window.  Entering the Missions page then remains a bounded
        // cache lookup even when a referenced VQA or ClickMap is absent.
        state.presentationResources.prepare();
    }
    state.assetContext.assetRootPath = state.exportRoot.string();
    state.assetContext.structureRootPath = state.exportRoot.string();
    state.assetContext.tileRootPath = state.exportRoot.string();

    state.csf = CampaignCsfCatalog{};
    std::vector<fs::path> ra2CsfCandidates;
    std::vector<fs::path> yrCsfCandidates;
    std::vector<fs::path> otherCsfCandidates;
    const auto addCsfCandidate = [](
        std::vector<fs::path>& candidates,
        const fs::path& path
    ) {
        if (path.empty()) return;
        const std::string key = lowerAscii(path.lexically_normal().generic_string());
        const bool duplicate = std::any_of(
            candidates.begin(),
            candidates.end(),
            [&key](const fs::path& existing) {
                return lowerAscii(existing.lexically_normal().generic_string())
                    == key;
            }
        );
        if (!duplicate) candidates.push_back(path);
    };
    const auto classifyCsfCandidate = [&](const fs::path& path) {
        const std::string name = lowerAscii(path.filename().string());
        if (name == "ra2.csf") {
            addCsfCandidate(ra2CsfCandidates, path);
        } else if (name == "ra2md.csf") {
            addCsfCandidate(yrCsfCandidates, path);
        } else {
            addCsfCandidate(otherCsfCandidates, path);
        }
    };
    for (const CampaignSourceDefinition& source : state.database.sources) {
        if (source.textCatalogPath.empty()) continue;
        if (lowerAscii(source.sourceId) == "ra2") {
            addCsfCandidate(ra2CsfCandidates, source.textCatalogPath);
        } else if (lowerAscii(source.sourceId) == "yr") {
            addCsfCandidate(yrCsfCandidates, source.textCatalogPath);
        } else {
            classifyCsfCandidate(source.textCatalogPath);
        }
    }
    for (const std::string& path : state.database.textCatalogPaths) {
        classifyCsfCandidate(path);
    }
    if (!state.database.textCatalogPath.empty()) {
        classifyCsfCandidate(state.database.textCatalogPath);
    }

    // The expansion catalog intentionally overlays the base catalog.  Both
    // contain some shared labels, while the base-only campaign keys must stay
    // available when the YR catalog is present.
    addCsfCandidate(
        ra2CsfCandidates,
        state.exportRoot / "language" / "ra2.csf"
    );
    addCsfCandidate(
        ra2CsfCandidates,
        fs::path(state.config.resourceRootPath) / "Game" / "ra2.csf"
    );
    addCsfCandidate(
        ra2CsfCandidates,
        fs::path(state.config.resourceRootPath) / "ra2.csf"
    );
    addCsfCandidate(
        yrCsfCandidates,
        state.exportRoot / "langmd" / "ra2md.csf"
    );
    addCsfCandidate(
        yrCsfCandidates,
        fs::path(state.config.resourceRootPath) / "GameMD" / "ra2md.csf"
    );
    addCsfCandidate(
        yrCsfCandidates,
        fs::path(state.config.resourceRootPath) / "ra2md.csf"
    );
    const auto appendFirstReadable = [&state](
        const std::vector<fs::path>& candidates
    ) {
        for (const fs::path& path : candidates) {
            if (state.csf.append(path)) return true;
        }
        return false;
    };
    appendFirstReadable(ra2CsfCandidates);
    appendFirstReadable(otherCsfCandidates);
    appendFirstReadable(yrCsfCandidates);

    // FSALG/FSSLG are the retail campaign emblems. LOGO01 and CAMPAIGN.PCX
    // are legacy assets still shipped in the archives, not the RA2/YR menu.
    state.officialCampaignShellLoaded = loadCampaignShellImage(
        state, "fsbkgdlg.shp", "fsbkgdlg.pal", 0, false, state.campaignShell);
    state.campaignLogoFrames.resize(2);
    state.officialCampaignShellLoaded = loadCampaignShellImage(
        state, "fsalg.shp", "fsalg.pal", 0, true, state.campaignLogoFrames[0])
        && state.officialCampaignShellLoaded;
    state.officialCampaignShellLoaded = loadCampaignShellImage(
        state, "fsslg.shp", "fsslg.pal", 0, true, state.campaignLogoFrames[1])
        && state.officialCampaignShellLoaded;
    for (const auto& [id, palette, frame, transparent] :
            std::vector<std::tuple<std::string, std::string, int, bool>>{
                {"lwscrnl.shp", "shell.pal", 0, false},
                // The warning animation belongs to the small monitor in frame 0.
                {"sdtp.shp", "shell.pal", 0, false},
                {"sdwrnanm.shp", "shell.pal", 0, true},
                {"sdbtnbkgd.shp", "shell.pal", 0, false},
                {"sdbtm.shp", "shell.pal", 0, false},
                {"sdbtnanm.shp", "sdbtnanm.pal", 0, true}}) {
        state.officialCampaignShellLoaded = loadCampaignShellImage(
            state, id, palette, frame, transparent, state.shellImages[id])
            && state.officialCampaignShellLoaded;
    }
    for (int frame = 0; frame < 3; ++frame) {
        state.officialCampaignShellLoaded = loadCampaignShellImage(
            state, "mnbttn.shp", "mainbttn.pal", frame, true,
            state.shellImages["button" + std::to_string(frame)])
            && state.officialCampaignShellLoaded;
    }
    // An absent MapSel palette must not recolor map art with a UI palette.
    state.campaignPalette.resize(256);
    for (int index = 0; index < 256; ++index) {
        const auto value = static_cast<unsigned char>(index);
        state.campaignPalette[static_cast<std::size_t>(index)] = {value, value, value};
    }
    if (!state.officialCampaignShellLoaded && state.status.empty()) {
        state.status = L"部分菜单美术资源未找到。";
    }

    refreshMissionBackground(state);
    return state.officialCampaignShellLoaded;
}





bool loadCampaignPresentationShpFrame(
    CampaignMenuState& state,
    const std::string& assetId,
    int frameIndex,
    RuntimeShpFrameImage& frame,
    std::string& source,
    std::string& errorMessage
) {
    frame = {};
    source.clear();
    errorMessage.clear();
    const CampaignDefinition* campaign = selectedCampaign(state);
    const std::string sourceId = campaign == nullptr
        ? std::string{}
        : campaign->sourceId;
    const CampaignResourceRecord& resource =
        state.presentationResources.load(assetId, sourceId);
    if (!resource.found) {
        errorMessage = "resource not found";
        return false;
    }
    if (resource.fromMix) {
        return loadRuntimeShpFrameFromMixArchive(
            resource.containerPath.string(),
            state.assetCache,
            assetId,
            frameIndex,
            frame,
            source,
            errorMessage
        );
    }
    source = resource.source;
    return decodeRuntimeShpFrameFile(
        resource.containerPath.string(),
        frameIndex,
        frame,
        errorMessage
    );
}

void loadCampaignMapPalette(
    CampaignMenuState& state,
    const CampaignDefinition& campaign
) {
    CampaignMapPresentationState& presentation = state.mapPresentation;
    presentation.palette.clear();
    presentation.paletteSource.clear();
    presentation.paletteFallback = false;
    if (!campaign.mapSelectionAnims.palette.empty()) {
        const CampaignResourceRecord& paletteResource =
            state.presentationResources.load(
                campaign.mapSelectionAnims.palette,
                campaign.sourceId
            );
        if (paletteResource.found
            && decodeCampaignPalette(
                paletteResource.bytes,
                presentation.palette
            )) {
            presentation.paletteSource = paletteResource.source;
            return;
        }
        appendMapPresentationDiagnostic(
            presentation,
            campaign.mapSelectionAnims.palette + " missing; palette fallback"
        );
    }
    presentation.palette = state.campaignPalette;
    presentation.paletteSource = "neutral grayscale fallback";
    presentation.paletteFallback = true;
}

void loadCampaignMapOverlays(
    CampaignMenuState& state,
    const CampaignMapSelectionStage& stage
) {
    CampaignMapPresentationState& presentation = state.mapPresentation;
    for (const std::string& overlayId : stage.overlays) {
        RuntimeShpFrameImage frame;
        std::string source;
        std::string error;
        if (!loadCampaignPresentationShpFrame(
                state,
                overlayId,
                0,
                frame,
                source,
                error
            )) {
            appendMapPresentationDiagnostic(
                presentation,
                overlayId + " overlay: " + error
            );
            continue;
        }
        CampaignImage image;
        if (makeCampaignImage(
                frame,
                presentation.palette,
                true,
                source,
                image
            )) {
            presentation.overlays.push_back(std::move(image));
        }
    }
}

void loadCampaignMapAnimations(
    CampaignMenuState& state,
    const CampaignDefinition& campaign
) {
    CampaignMapPresentationState& presentation = state.mapPresentation;
    for (const CampaignMapSelectionAnimation& authored :
            campaign.mapSelectionAnims.animations) {
        CampaignMapAnimationVisual visual;
        visual.assetId = authored.shpFile;
        visual.x = authored.x;
        visual.y = authored.y;
        visual.frameRate = std::max(1, authored.rate);
        for (int frameIndex = 0; frameIndex < 64; ++frameIndex) {
            RuntimeShpFrameImage frame;
            std::string source;
            std::string error;
            if (!loadCampaignPresentationShpFrame(
                    state,
                    authored.shpFile,
                    frameIndex,
                    frame,
                    source,
                    error
                )) {
                break;
            }
            CampaignImage image;
            if (!makeCampaignImage(
                    frame,
                    presentation.palette,
                    true,
                    source,
                    image
                )) {
                break;
            }
            visual.frames.push_back(std::move(image));
        }
        if (!visual.frames.empty()) {
            presentation.animations.push_back(std::move(visual));
        } else {
            appendMapPresentationDiagnostic(
                presentation,
                authored.shpFile + " animation missing"
            );
        }
    }
}

void playCampaignMapSound(
    CampaignMenuState& state,
    const CampaignMapSelectionSound& sound
) {
    if (state.suppressPresentationAudio || sound.sample.empty()) return;
    const CampaignDefinition* campaign = selectedCampaign(state);
    const std::string sourceId = campaign == nullptr
        ? std::string{}
        : campaign->sourceId;
    const std::optional<CampaignPresentationAudioRecord> wave =
        resolveCampaignPresentationAudio(
        state.config,
        state.presentationResources,
        state.presentationAudio,
        state.missingPresentationAudio,
        sourceId,
        sound.sample
    );
    if (wave.has_value()) {
        playCampaignEffectWave(
            wave->wavePath,
            sound.volumeSpecified ? sound.volume : 100
        );
    }
}

void startCampaignMapVoice(
    CampaignMenuState& state,
    const CampaignMapSelectionStage& stage
) {
    if (stage.voiceOver.empty()) return;
    const CampaignDefinition* campaign = selectedCampaign(state);
    const std::string sourceId = campaign == nullptr
        ? std::string{}
        : campaign->sourceId;
    const std::optional<CampaignPresentationAudioRecord> wave =
        resolveCampaignPresentationAudio(
        state.config,
        state.presentationResources,
        state.presentationAudio,
        state.missingPresentationAudio,
        sourceId,
        stage.voiceOver
    );
    if (wave.has_value()) {
        state.mapPresentation.voiceSource = wave->source;
        if (!state.suppressPresentationAudio) {
            state.mapPresentation.voiceStarted = playCampaignVoiceWave(
                wave->wavePath,
                100
            );
        }
    } else {
        appendMapPresentationDiagnostic(
            state.mapPresentation,
            stage.voiceOver + " voice missing"
        );
    }
}

void updateCampaignVqaImage(
    CampaignMapPresentationState& presentation,
    const std::string& source
) {
    const std::vector<std::uint8_t>& pixels =
        presentation.vqa.currentFrameBgra();
    const std::uint32_t width = presentation.vqa.width();
    const std::uint32_t height = presentation.vqa.height();
    if (width == 0 || height == 0
        || pixels.size() != static_cast<std::size_t>(width) * height * 4U) {
        return;
    }
    presentation.vqaFrame.width = static_cast<int>(width);
    presentation.vqaFrame.height = static_cast<int>(height);
    presentation.vqaFrame.bgra.assign(pixels.begin(), pixels.end());
    presentation.vqaFrame.source = source;
}



bool reconstructCampaignClickMapFromAuthoredTargets(
    CampaignMenuState& state,
    const CampaignMapSelectionStage& stage
) {
    CampaignMapPresentationState& presentation = state.mapPresentation;
    const CampaignDefinition* campaign = selectedCampaign(state);
    if (campaign == nullptr || presentation.hasClickMap()
        || stage.targets.empty() || stage.choices.empty()) {
        return false;
    }

    std::size_t targetCount = std::min(
        stage.targets.size(),
        stage.choices.size()
    );
    if (stage.declaredTargetCount >= 0) {
        targetCount = std::min(
            targetCount,
            static_cast<std::size_t>(stage.declaredTargetCount)
        );
    }
    if (targetCount == 0) return false;

    presentation.clickMap = {};
    presentation.clickMap.width = kBaseWidth;
    presentation.clickMap.height = kBaseHeight;
    presentation.clickMap.pixels.assign(
        static_cast<std::size_t>(kBaseWidth * kBaseHeight),
        0
    );

    bool wroteRegion = false;
    constexpr int kTargetHitRadius = 30;
    for (std::size_t index = 0; index < targetCount; ++index) {
        const CampaignMapSelectionChoice& choice = stage.choices[index];
        const int missionIndex = missionIndexById(
            *campaign,
            choice.targetMissionId
        );
        if (choice.colorIndex <= 0 || choice.colorIndex > 255
            || missionIndex < 0
            || !campaign->missions[static_cast<std::size_t>(missionIndex)]
                .available) {
            continue;
        }
        const CampaignMapSelectionPoint& target = stage.targets[index];
        const int minX = std::max(0, target.x - kTargetHitRadius);
        const int maxX = std::min(kBaseWidth - 1, target.x + kTargetHitRadius);
        const int minY = std::max(0, target.y - kTargetHitRadius);
        const int maxY = std::min(kBaseHeight - 1, target.y + kTargetHitRadius);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const int dx = x - target.x;
                const int dy = y - target.y;
                if (dx * dx + dy * dy
                    > kTargetHitRadius * kTargetHitRadius) {
                    continue;
                }
                presentation.clickMap.pixels[static_cast<std::size_t>(
                    y * kBaseWidth + x
                )] = static_cast<unsigned char>(choice.colorIndex);
                wroteRegion = true;
            }
        }
    }

    if (!wroteRegion) {
        presentation.clickMap = {};
        return false;
    }
    presentation.authoredReconstruction = true;
    presentation.clickMapSource = "MapSel Targets:" + stage.sourceSection;
    appendMapPresentationDiagnostic(
        presentation,
        "retail map media unavailable; interactive surface rebuilt from "
            "authored Targets"
    );
    return true;
}

void enterCampaignMapOverlayFade(CampaignMenuState& state) {
    CampaignMapPresentationState& presentation = state.mapPresentation;
    presentation.phase = CampaignMapPresentationPhase::OverlayFade;
    if (!presentation.overlaySoundPlayed) {
        const CampaignDefinition* campaign = selectedCampaign(state);
        if (campaign != nullptr) {
            playCampaignMapSound(state, campaign->mapSelectionSounds.overlay);
        }
        presentation.overlaySoundPlayed = true;
    }
    presentation.phaseStartedAt = state.presentationClock->nowMs();
}

void releaseCampaignMapMouseCapture(CampaignMenuState& state) {
    state.mapPresentation.pressedColor = 0;
    if (state.window != nullptr && GetCapture() == state.window) {
        ReleaseCapture();
    }
}

void refreshCampaignMapPresentation(CampaignMenuState& state) {
    if (state.page != CampaignPage::Missions) {
        if (!state.mapPresentation.key.empty()) {
            releaseCampaignMapMouseCapture(state);
            stopCampaignVoice();
            stopCampaignMapEffect();
            state.mapPresentation = CampaignMapPresentationState{};
        }
        return;
    }
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    const CampaignMapSelectionStage* stage = selectedMapSelectionStage(state);
    std::string key;
    if (campaign != nullptr && mission != nullptr) {
        key = lowerAscii(campaign->id + "|" + mission->id + "|");
        key += stage == nullptr ? "(fallback)" : lowerAscii(stage->id);
    }
    if (key == state.mapPresentation.key) return;

    releaseCampaignMapMouseCapture(state);
    stopCampaignVoice();
    stopCampaignMapEffect();
    state.mapPresentation = CampaignMapPresentationState{};
    CampaignMapPresentationState& presentation = state.mapPresentation;
    presentation.key = key;
    if (campaign == nullptr || mission == nullptr || stage == nullptr) {
        presentation.phase = CampaignMapPresentationPhase::FallbackList;
        presentation.phaseStartedAt = state.presentationClock->nowMs();
        presentation.diagnostic =
            "no incoming authored map-selection stage for this mission";
        return;
    }
    presentation.stageSection = stage->id;
    loadCampaignMapPalette(state, *campaign);

    if (!stage->clickMap.empty()) {
        const CampaignResourceRecord& clickResource =
            state.presentationResources.load(
                stage->clickMap,
                campaign->sourceId
            );
        std::vector<RuntimePaletteColor> ignoredPalette;
        std::string error;
        if (clickResource.found
            && decodeRuntimePcxImageBytes(
                clickResource.bytes,
                presentation.clickMap,
                ignoredPalette,
                error
            )) {
            presentation.clickMapSource = clickResource.source;
        } else {
            appendMapPresentationDiagnostic(
                presentation,
                stage->clickMap + " click map: "
                    + (error.empty() ? "resource not found" : error)
            );
        }
    } else {
        appendMapPresentationDiagnostic(
            presentation,
            "stage has no ClickMap"
        );
    }

    loadCampaignMapOverlays(state, *stage);
    loadCampaignMapAnimations(state, *campaign);

    bool vqaReady = false;
    if (!stage->mapVq.empty()) {
        const CampaignResourceRecord& vqaResource =
            state.presentationResources.load(
                stage->mapVq,
                campaign->sourceId
            );
        if (vqaResource.found
            && presentation.vqa.loadFromMemory(vqaResource.bytes)) {
            updateCampaignVqaImage(presentation, vqaResource.source);
            vqaReady = true;
        } else {
            appendMapPresentationDiagnostic(
                presentation,
                stage->mapVq + " VQA: "
                    + (vqaResource.found
                        ? presentation.vqa.errorMessage()
                        : "resource not found")
            );
        }
    }

    if (!presentation.hasClickMap()) {
        reconstructCampaignClickMapFromAuthoredTargets(state, *stage);
    }

    // Resource resolution can touch archive and audio indexes.  Start the
    // shared presentation clock only after frame zero and the voice channel
    // are ready, keeping the opening image and narration aligned.
    startCampaignMapVoice(state, *stage);
    presentation.phaseStartedAt = state.presentationClock->nowMs();
    presentation.lastFrameAt = presentation.phaseStartedAt;
    if (vqaReady) {
        presentation.phase = CampaignMapPresentationPhase::VqaIntro;
        return;
    }

    if (!presentation.overlays.empty() || !presentation.animations.empty()) {
        enterCampaignMapOverlayFade(state);
    } else if (presentation.hasClickMap()) {
        presentation.keyboardColor = firstUsableMapChoiceColor(state, *stage);
        if (presentation.keyboardColor != 0) {
            presentation.phase = CampaignMapPresentationPhase::Interactive;
            if (!presentation.targetSoundPlayed) {
                playCampaignMapSound(
                    state,
                    campaign->mapSelectionSounds.targetFlyIn
                );
                presentation.targetSoundPlayed = true;
            }
            presentation.phaseStartedAt = state.presentationClock->nowMs();
        } else {
            presentation.phase = CampaignMapPresentationPhase::FallbackList;
            appendMapPresentationDiagnostic(
                presentation,
                "stage has no available map choice"
            );
        }
    } else {
        presentation.phase = CampaignMapPresentationPhase::FallbackList;
    }
}

void updateCampaignMapPresentation(CampaignMenuState& state) {
    refreshCampaignMapPresentation(state);
    CampaignMapPresentationState& presentation = state.mapPresentation;
    const std::uint64_t now = state.presentationClock->nowMs();
    if (presentation.phase == CampaignMapPresentationPhase::VqaIntro) {
        const std::uint64_t interval = 1000ULL
            / std::max<std::uint32_t>(1U, presentation.vqa.frameRate());
        int advancedFrames = 0;
        while (presentation.vqa.hasNextFrame()
            && now >= presentation.lastFrameAt + std::max<std::uint64_t>(1, interval)
            && advancedFrames < 4) {
            presentation.lastFrameAt += std::max<std::uint64_t>(1, interval);
            ++advancedFrames;
            if (!presentation.vqa.advance()) {
                if (!presentation.vqa.errorMessage().empty()) {
                    appendMapPresentationDiagnostic(
                        presentation,
                        "VQA frame: " + presentation.vqa.errorMessage()
                    );
                }
                presentation.vqa.clear();
                break;
            }
            updateCampaignVqaImage(
                presentation,
                presentation.vqaFrame.source
            );
        }
        if (!presentation.vqa.hasNextFrame()) {
            enterCampaignMapOverlayFade(state);
        }
    }
    if (presentation.phase == CampaignMapPresentationPhase::OverlayFade
        && now >= presentation.phaseStartedAt + 750ULL) {
        const CampaignDefinition* campaign = selectedCampaign(state);
        const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
        if (presentation.hasClickMap() && stage != nullptr) {
            presentation.keyboardColor = firstUsableMapChoiceColor(
                state,
                *stage
            );
            if (presentation.keyboardColor == 0) {
                presentation.phase =
                    CampaignMapPresentationPhase::FallbackList;
                appendMapPresentationDiagnostic(
                    presentation,
                    "stage has no available map choice"
                );
                return;
            }
            presentation.phase = CampaignMapPresentationPhase::Interactive;
            if (!presentation.targetSoundPlayed && campaign != nullptr) {
                playCampaignMapSound(
                    state,
                    campaign->mapSelectionSounds.targetFlyIn
                );
                presentation.targetSoundPlayed = true;
            }
            presentation.phaseStartedAt = state.presentationClock->nowMs();
        } else {
            presentation.phase = CampaignMapPresentationPhase::FallbackList;
            presentation.phaseStartedAt = now;
        }
    }
}

#include "ui/campaign/diagnostics/RuntimeCampaignSystemFontTrace.inl"

// System text drawing is owned by the private Campaign font provider.

void blitCampaignImageScaled(
    std::vector<unsigned char>& canvas,
    const CampaignImage& image,
    const RECT& destination,
    bool aspectFill,
    unsigned char opacity = 255
) {
    if (!image.valid()) return;
    const int destinationWidth = destination.right - destination.left;
    const int destinationHeight = destination.bottom - destination.top;
    if (destinationWidth <= 0 || destinationHeight <= 0) return;

    double sourceLeft = 0.0;
    double sourceTop = 0.0;
    double sourceWidth = static_cast<double>(image.width);
    double sourceHeight = static_cast<double>(image.height);
    if (aspectFill) {
        const double destinationAspect =
            static_cast<double>(destinationWidth) / destinationHeight;
        const double sourceAspect =
            static_cast<double>(image.width) / image.height;
        if (sourceAspect > destinationAspect) {
            sourceWidth = image.height * destinationAspect;
            sourceLeft = (image.width - sourceWidth) / 2.0;
        } else {
            sourceHeight = image.width / destinationAspect;
            sourceTop = (image.height - sourceHeight) / 2.0;
        }
    }

    for (int y = 0; y < destinationHeight; ++y) {
        const int canvasY = destination.top + y;
        if (canvasY < 0 || canvasY >= kBaseHeight) continue;
        const int sourceY = std::clamp(
            static_cast<int>(sourceTop
                + (static_cast<double>(y) + 0.5)
                    * sourceHeight / destinationHeight),
            0,
            image.height - 1
        );
        for (int x = 0; x < destinationWidth; ++x) {
            const int canvasX = destination.left + x;
            if (canvasX < 0 || canvasX >= kBaseWidth) continue;
            const int sourceX = std::clamp(
                static_cast<int>(sourceLeft
                    + (static_cast<double>(x) + 0.5)
                        * sourceWidth / destinationWidth),
                0,
                image.width - 1
            );
            const std::size_t sourceAt = static_cast<std::size_t>(
                (sourceY * image.width + sourceX) * 4
            );
            const unsigned int alpha = (
                static_cast<unsigned int>(image.bgra[sourceAt + 3])
                    * opacity + 127U
            ) / 255U;
            if (alpha == 0) continue;
            const std::size_t destinationAt = static_cast<std::size_t>(
                (canvasY * kBaseWidth + canvasX) * 4
            );
            if (alpha == 255) {
                canvas[destinationAt] = image.bgra[sourceAt];
                canvas[destinationAt + 1] = image.bgra[sourceAt + 1];
                canvas[destinationAt + 2] = image.bgra[sourceAt + 2];
                canvas[destinationAt + 3] = 255;
                continue;
            }
            const unsigned int inverse = 255U - alpha;
            for (int channel = 0; channel < 3; ++channel) {
                canvas[destinationAt + static_cast<std::size_t>(channel)] =
                    static_cast<unsigned char>((
                        image.bgra[sourceAt + static_cast<std::size_t>(channel)]
                            * alpha
                        + canvas[destinationAt + static_cast<std::size_t>(channel)]
                            * inverse
                        + 127U
                    ) / 255U);
            }
            canvas[destinationAt + 3] = 255;
        }
    }
}

std::size_t campaignMapAnimationFrameIndex(
    const CampaignMapAnimationVisual& animation,
    std::uint64_t timestamp
) {
    if (animation.frames.empty()) return 0U;
    const std::uint64_t frameDuration = std::max<std::uint64_t>(
        1ULL,
        static_cast<std::uint64_t>(animation.frameRate) * 1000ULL / 60ULL
    );
    return static_cast<std::size_t>(
        (timestamp / frameDuration) % animation.frames.size()
    );
}

std::vector<unsigned char> composeCampaignSurface(const CampaignMenuState& state) {
    std::vector<unsigned char> canvas(kBaseWidth * kBaseHeight * 4, 0);
    const auto& presentation = state.mapPresentation;
    const RECT media = campaignMapMediaRect(presentation);
    blitCampaignImageScaled(canvas, presentation.vqaFrame, media, false);
    const std::uint64_t elapsed = state.presentationClock->nowMs() - presentation.phaseStartedAt;
    const auto opacity = static_cast<unsigned char>(
        presentation.phase == CampaignMapPresentationPhase::VqaIntro ? 0
        : presentation.phase == CampaignMapPresentationPhase::OverlayFade
            ? std::min<std::uint64_t>(255, elapsed * 255 / 750) : 255);
    if (!presentation.paletteFallback) {
        for (const auto& overlay : presentation.overlays) {
            blitCampaignImageScaled(canvas, overlay, media, false, opacity);
        }
        for (const auto& animation : presentation.animations) {
            if (animation.frames.empty()) continue;
            const auto& image = animation.frames[
                campaignMapAnimationFrameIndex(animation, state.presentationClock->nowMs())];
            blitCampaignImageScaled(canvas, image,
                RECT{animation.x, animation.y, animation.x + image.width,
                     animation.y + image.height}, false);
        }
    }
    return canvas;
}







void drawCampaignMapTargets(
    const CampaignMenuState& state,
    HDC dc,
    const RECT& client
) {
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    if (stage == nullptr
        || state.mapPresentation.phase
            != CampaignMapPresentationPhase::Interactive) {
        return;
    }
    std::size_t count = stage->targets.size();
    if (stage->declaredTargetCount >= 0) {
        count = std::min(
            count,
            static_cast<std::size_t>(stage->declaredTargetCount)
        );
    }
    const int clientWidth = std::max(1L, client.right - client.left);
    const int clientHeight = std::max(1L, client.bottom - client.top);
    const RECT media = campaignMapMediaRect(state.mapPresentation);
    const int authoredWidth = state.mapPresentation.clickMap.width > 0
        ? state.mapPresentation.clickMap.width
        : std::max(1, state.mapPresentation.vqaFrame.width);
    const int authoredHeight = state.mapPresentation.clickMap.height > 0
        ? state.mapPresentation.clickMap.height
        : std::max(1, state.mapPresentation.vqaFrame.height);
    const std::uint64_t now = state.presentationClock->nowMs();
    const int pulse = static_cast<int>((now / 80ULL) % 8ULL);
    const double linearFly = std::clamp(
        static_cast<double>(now - state.mapPresentation.phaseStartedAt) / 600.0,
        0.0,
        1.0
    );
    const double fly = 1.0 - (1.0 - linearFly) * (1.0 - linearFly);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    for (std::size_t index = 0; index < count; ++index) {
        const CampaignMapSelectionPoint& target = stage->targets[index];
        const int associatedColor = index < stage->choices.size()
            ? stage->choices[index].colorIndex
            : 0;
        const bool focused = associatedColor > 0
            && (associatedColor == state.mapPresentation.hoverColor
                || associatedColor == state.mapPresentation.keyboardColor);
        const COLORREF color = focused ? kBrightGold : RGB(210, 45, 35);
        HPEN pen = CreatePen(PS_SOLID, focused ? 3 : 2, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const int baseCenterX = static_cast<int>(media.left)
            + target.x * static_cast<int>(media.right - media.left)
                / authoredWidth;
        const int baseCenterY = static_cast<int>(media.top)
            + target.y * static_cast<int>(media.bottom - media.top)
                / authoredHeight;
        const int startBaseX = (index & 1U) == 0U ? -40 : kBaseWidth + 40;
        const int animatedBaseX = static_cast<int>(std::lround(
            startBaseX + (baseCenterX - startBaseX) * fly
        ));
        const int centerX = client.left
            + animatedBaseX * clientWidth / kBaseWidth;
        const int centerY = client.top
            + baseCenterY * clientHeight / kBaseHeight;
        const int radius = std::max(
            7,
            (focused ? 13 : 10) + pulse / 2
        );
        Ellipse(
            dc,
            centerX - radius,
            centerY - radius,
            centerX + radius,
            centerY + radius
        );
        MoveToEx(dc, centerX - radius - 5, centerY, nullptr);
        LineTo(dc, centerX + radius + 5, centerY);
        MoveToEx(dc, centerX, centerY - radius - 5, nullptr);
        LineTo(dc, centerX, centerY + radius + 5);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    SelectObject(dc, oldBrush);
}

#include "ui/campaign/RuntimeCampaignMenuUi.inl"


























std::wstring CampaignMenuState::text(const std::string& key,const std::wstring& fallback) const { return csf.get(key,fallback); }
std::wstring CampaignMenuState::widen(const std::string& value) const { return ::widen(value); }
std::wstring CampaignMenuState::missionLabel(const CampaignMissionDefinition& mission) const { return ::missionLabel(mission,csf); }
std::wstring CampaignMenuState::editionSuffix(const CampaignDefinition* campaign) const { return campaignEditionSuffix(campaign); }
std::string CampaignMenuState::loadingScreenAsset(const std::string& country) const { return runtimeLoadingScreenAssetForCountry(country); }
std::string CampaignMenuState::savePath(const std::string& root,const std::string& campaign,const std::string& mission) const { return campaignSavePath(root,campaign,mission); }
const CampaignMapSelectionStage* CampaignMenuState::findMapStage(const CampaignDefinition& campaign,const std::string& id) const { return campaign.findMapSelectionStage(id); }
const CampaignMissionDefinition* CampaignMenuState::findMission(const std::string& campaign,const std::string& mission) const { return database.findMission(campaign,mission); }
bool CampaignMenuState::saveProgress(const std::string& root,const CampaignProgressData& value,std::string& error) { return saveCampaignProgress(root,value,error); }
bool CampaignMenuState::validateSavePaths(const RuntimeConfig& cfg,const SkirmishSaveInfo& value,std::string& error) const { return validateSkirmishSaveStartupPaths(cfg,value,error); }
void CampaignMenuState::refreshBackground() { refreshMissionBackground(*this); }
void CampaignMenuState::refreshPresentation() { refreshCampaignMapPresentation(*this); }
void CampaignMenuState::releaseMapCapture() { releaseCampaignMapMouseCapture(*this); }
void CampaignMenuState::stopVoice() { stopCampaignVoice(); }
void CampaignMenuState::stopMapEffect() { stopCampaignMapEffect(); }
void CampaignMenuState::playMapSound(const CampaignMapSelectionSound& sample) { playCampaignMapSound(*this,sample); }
void CampaignMenuState::repaintValidWindow() { if(window!=nullptr&&IsWindow(window))InvalidateRect(window,nullptr,FALSE); }
void CampaignMenuState::destroyWindow() { if(window!=nullptr&&IsWindow(window))DestroyWindow(window); }
int CampaignMenuState::mapColorAt(int x,int y) const { return campaignMapColorAtBasePoint(*this,x,y); }


std::wstring CampaignMenuState::saveTitle(const LoadSaveEntry& entry) const { return loadSaveTitle(entry); }
void CampaignMenuState::updatePresentation() { updateCampaignMapPresentation(*this); }
RuntimeInputPixelRect CampaignMenuState::eventClientRect() const { return campaignEventClientRect(eventWindow.value); }
bool CampaignMenuState::shiftPressed() const { return (GetKeyState(VK_SHIFT)&0x8000)!=0; }
void CampaignMenuState::repaintEvent(bool requireValid) { if(!requireValid||IsWindow(eventWindow.value))InvalidateRect(eventWindow.value,nullptr,FALSE); }
void CampaignMenuState::trackMouseLeave() { TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT),TME_LEAVE,eventWindow.value,0};TrackMouseEvent(&tracking); }
void CampaignMenuState::setHandCursor(bool hand) { SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(hand?32649:32512))); }
void CampaignMenuState::focusEvent() { SetFocus(eventWindow.value); }
void CampaignMenuState::captureEvent() { SetCapture(eventWindow.value); }
void CampaignMenuState::releaseEventCapture() { if(GetCapture()==eventWindow.value)ReleaseCapture(); }
CampaignDatabase CampaignMenuState::loadDatabase(const CampaignDatabaseOptions& options) { return loadCampaignDatabase(options); }
bool CampaignMenuState::loadProgress(const std::string& root,CampaignProgressData& value,std::string& error) { return loadCampaignProgress(root,value,error); }
std::vector<CampaignSaveInfo> CampaignMenuState::campaignSaves(const std::string& root) { return listCampaignSaves(root); }
std::vector<SkirmishSaveInfo> CampaignMenuState::skirmishSaves(const std::string& root) { return listSkirmishSaves(root); }
std::wstring CampaignMenuState::incompatibleSaveWarning(const std::string& root) const {
    const fs::path runtimeSave(skirmishSavePath(root));
    std::error_code saveError;
    if (fs::is_regular_file(runtimeSave, saveError)) {
        SkirmishSaveInfo incompatible;
        if (!inspectSkirmishSave(runtimeSave.string(), incompatible)) {
            return L"遭遇战存档不可从主菜单载入：" + ::widen(incompatible.error);
        }
    }
    return {};
}

#include "ui/campaign/RuntimeCampaignMenuInput.inl"

bool registerCampaignMenuClass() {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc = campaignMenuProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.lpszClassName = L"ConcordRTS.RuntimeCampaignMenu";
    if (RegisterClassExW(&cls) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND createCampaignMenuWindow(CampaignMenuState& state, HWND hostWindow) {
    if (!registerCampaignMenuClass()) {
        return nullptr;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (hostWindow != nullptr && IsWindow(hostWindow)) {
        RECT client{};
        GetClientRect(hostWindow, &client);
        return CreateWindowExW(
            0,
            L"ConcordRTS.RuntimeCampaignMenu",
            L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            std::max(1L, client.right),
            std::max(1L, client.bottom),
            hostWindow,
            nullptr,
            instance,
            &state
        );
    }

    RECT windowRect{0, 0, kCampaignShellWidth, kCampaignShellHeight};
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    return CreateWindowExW(
        0,
        L"ConcordRTS.RuntimeCampaignMenu",
        L"Command & Conquer: Yuri's Revenge - Campaign",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        &state
    );
}

using CampaignFixtureBytes = std::vector<std::uint8_t>;

void appendCampaignFixtureTag(
    CampaignFixtureBytes& output,
    const char* tag
) {
    output.insert(output.end(), tag, tag + 4);
}

void appendCampaignFixtureLittle16(
    CampaignFixtureBytes& output,
    std::uint16_t value
) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendCampaignFixtureLittle32(
    CampaignFixtureBytes& output,
    std::uint32_t value
) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void appendCampaignFixtureBig32(
    CampaignFixtureBytes& output,
    std::uint32_t value
) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void writeCampaignFixtureLittle16(
    CampaignFixtureBytes& output,
    std::size_t offset,
    std::uint16_t value
) {
    output[offset] = static_cast<std::uint8_t>(value);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void writeCampaignFixtureLittle32(
    CampaignFixtureBytes& output,
    std::size_t offset,
    std::uint32_t value
) {
    output[offset] = static_cast<std::uint8_t>(value);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    output[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    output[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

void writeCampaignFixtureBig32(
    CampaignFixtureBytes& output,
    std::size_t offset,
    std::uint32_t value
) {
    output[offset] = static_cast<std::uint8_t>(value >> 24U);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
    output[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
    output[offset + 3U] = static_cast<std::uint8_t>(value);
}

void appendCampaignFixtureChunk(
    CampaignFixtureBytes& output,
    const char* tag,
    const CampaignFixtureBytes& payload
) {
    appendCampaignFixtureTag(output, tag);
    appendCampaignFixtureBig32(
        output,
        static_cast<std::uint32_t>(payload.size())
    );
    output.insert(output.end(), payload.begin(), payload.end());
    if ((payload.size() & 1U) != 0U) output.push_back(0U);
}

CampaignFixtureBytes campaignFixtureLcwLiteral(
    const CampaignFixtureBytes& decoded
) {
    CampaignFixtureBytes result;
    std::size_t offset = 0;
    while (offset < decoded.size()) {
        const std::size_t count = std::min<std::size_t>(
            63U,
            decoded.size() - offset
        );
        result.push_back(static_cast<std::uint8_t>(0x80U | count));
        result.insert(
            result.end(),
            decoded.begin() + static_cast<std::ptrdiff_t>(offset),
            decoded.begin() + static_cast<std::ptrdiff_t>(offset + count)
        );
        offset += count;
    }
    result.push_back(0x80U);
    return result;
}

CampaignFixtureBytes makeCampaignFixtureVqa() {
    CampaignFixtureBytes output;
    appendCampaignFixtureTag(output, "FORM");
    appendCampaignFixtureBig32(output, 0U);
    appendCampaignFixtureTag(output, "WVQA");

    CampaignFixtureBytes header;
    appendCampaignFixtureLittle16(header, 2U);
    appendCampaignFixtureLittle16(header, 0U);
    appendCampaignFixtureLittle16(header, 1U);
    appendCampaignFixtureLittle16(header, 8U);
    appendCampaignFixtureLittle16(header, 2U);
    header.push_back(4U);
    header.push_back(2U);
    header.push_back(15U);
    header.push_back(0U);
    appendCampaignFixtureLittle16(header, 4U);
    appendCampaignFixtureLittle16(header, 256U);
    appendCampaignFixtureLittle16(header, 0U);
    appendCampaignFixtureLittle32(header, 0U);
    appendCampaignFixtureLittle16(header, 0U);
    header.push_back(0U);
    header.push_back(0U);
    appendCampaignFixtureLittle32(header, 0U);
    appendCampaignFixtureLittle16(header, 0U);
    appendCampaignFixtureLittle32(header, 256U * 1024U);
    appendCampaignFixtureLittle32(header, 0U);
    appendCampaignFixtureChunk(output, "VQHD", header);

    const std::size_t finfTagOffset = output.size();
    appendCampaignFixtureChunk(output, "FINF", CampaignFixtureBytes(4U, 0U));
    const std::size_t finfDataOffset = finfTagOffset + 8U;
    if ((output.size() & 1U) != 0U) output.push_back(0U);
    writeCampaignFixtureLittle32(
        output,
        finfDataOffset,
        static_cast<std::uint32_t>(output.size() / 2U)
    );

    CampaignFixtureBytes frame;
    appendCampaignFixtureChunk(
        frame,
        "CPL0",
        CampaignFixtureBytes{
            0U, 0U, 0U,
            63U, 0U, 0U,
            0U, 63U, 0U,
            0U, 0U, 63U
        }
    );
    appendCampaignFixtureChunk(
        frame,
        "CBF0",
        CampaignFixtureBytes{
            1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U,
            2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U
        }
    );
    appendCampaignFixtureChunk(
        frame,
        "VPTZ",
        campaignFixtureLcwLiteral(CampaignFixtureBytes{0U, 1U, 0U, 0U})
    );
    CampaignFixtureBytes vqfr;
    appendCampaignFixtureChunk(vqfr, "VQFR", frame);
    output.insert(output.end(), vqfr.begin(), vqfr.end());
    writeCampaignFixtureBig32(
        output,
        4U,
        static_cast<std::uint32_t>(output.size() - 8U)
    );
    return output;
}

CampaignFixtureBytes makeCampaignFixturePalette() {
    CampaignFixtureBytes palette(256U * 3U, 0U);
    for (std::size_t index = 1; index < 256U; ++index) {
        palette[index * 3U] = static_cast<std::uint8_t>(index & 63U);
        palette[index * 3U + 1U] = static_cast<std::uint8_t>(
            (index * 3U) & 63U
        );
        palette[index * 3U + 2U] = static_cast<std::uint8_t>(
            (index * 5U) & 63U
        );
    }
    return palette;
}

CampaignFixtureBytes makeCampaignFixtureClickMap(
    int colorIndex,
    int markedX,
    int markedY
) {
    constexpr int width = 8;
    constexpr int height = 2;
    CampaignFixtureBytes pcx(128U, 0U);
    pcx[0] = 0x0AU;
    pcx[1] = 5U;
    pcx[2] = 1U;
    pcx[3] = 8U;
    writeCampaignFixtureLittle16(pcx, 8U, width - 1);
    writeCampaignFixtureLittle16(pcx, 10U, height - 1);
    writeCampaignFixtureLittle16(pcx, 12U, width);
    writeCampaignFixtureLittle16(pcx, 14U, height);
    pcx[65] = 1U;
    writeCampaignFixtureLittle16(pcx, 66U, width);
    writeCampaignFixtureLittle16(pcx, 68U, 1U);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t value = x == markedX && y == markedY
                ? static_cast<std::uint8_t>(std::clamp(colorIndex, 0, 255))
                : 0U;
            if (value >= 0xC0U) {
                pcx.push_back(0xC1U);
                pcx.push_back(value);
            } else {
                pcx.push_back(value);
            }
        }
    }
    pcx.push_back(0x0CU);
    pcx.resize(pcx.size() + 256U * 3U, 0U);
    const std::size_t paletteOffset = pcx.size() - 256U * 3U;
    const std::size_t colorOffset = paletteOffset
        + static_cast<std::size_t>(std::clamp(colorIndex, 0, 255)) * 3U;
    pcx[colorOffset] = 32U;
    pcx[colorOffset + 1U] = 255U;
    pcx[colorOffset + 2U] = 64U;
    return pcx;
}

bool writeCampaignFixtureBytes(
    const fs::path& path,
    const CampaignFixtureBytes& bytes
) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    return output.good();
}

// Diagnostic-only owning environment read; read errors cannot enable cleanup.
std::optional<std::string> campaignSmokeEnvironment(const char* name) {
    char* value=nullptr;std::size_t size=0;
    if(_dupenv_s(&value,&size,name)!=0)throw std::runtime_error("campaign smoke environment read failed");
    std::unique_ptr<char,decltype(&std::free)> owned(value,&std::free);
    return value?std::optional<std::string>(value):std::nullopt;
}
bool campaignSmokeRetainArtifacts() {
    const auto retain=campaignSmokeEnvironment("CONCORD_CAMPAIGN_SMOKE_RETAIN");
    if(!retain)return false;
    if(*retain!="1")throw std::runtime_error("Campaign smoke retain must be1");
    return true;
}

bool runCampaignMapMediaPathSmoke(
    CampaignMenuState& state,
    int campaignIndex,
    int missionIndex,
    std::string& realPathSummary,
    std::string& missingPathSummary,
    std::string& errorMessage
) {
    const bool retainSmokeArtifacts = campaignSmokeRetainArtifacts();
    // Optional fixture input only: stock reads remain separate from the owned
    // temporary localData/audio-cache outputs. Default stock binding is unchanged.
    const auto stockSmokeRoot = campaignSmokeEnvironment("CONCORD_CAMPAIGN_SMOKE_STOCK_ROOT");
    if (stockSmokeRoot) {
        std::error_code stockError;
        const fs::path stockPath(*stockSmokeRoot);
        if (stockSmokeRoot->empty() || !stockPath.is_absolute()
            || !fs::is_directory(stockPath, stockError) || stockError) {
            errorMessage = "Campaign smoke stock root must be an existing absolute input directory";
            return false;
        }
    }
    realPathSummary.clear();
    missingPathSummary.clear();
    errorMessage.clear();
    if (campaignIndex < 0
        || campaignIndex >= static_cast<int>(state.database.campaigns.size())) {
        errorMessage = "real-media smoke campaign index is invalid";
        return false;
    }

    const RuntimeConfig originalConfig = state.config;
    const int originalCampaignIndex = state.campaignIndex;
    const int originalMissionIndex = state.missionIndex;
    const CampaignPage originalPage = state.page;
    const bool originalSuppressAudio = state.suppressPresentationAudio;
    const CampaignProgressData originalProgress = state.progress;
    std::error_code fileError;
    const fs::path root = fs::temp_directory_path(fileError) / (
        "concord-campaign-map-media-"
        + std::to_string(GetCurrentProcessId()) + "-"
        + std::to_string(GetTickCount64())
    );
    const fs::path modRoot = root / "mod";
    const fs::path localRoot = root / "local";
    const fs::path emptyRoot = root / "empty";
    fs::path palettePath;
    fs::path vqaPath;
    fs::path clickPath;
    fs::path voicePath;
    fs::path cachedVoicePath;

    const auto removeKnown = [](const fs::path& path) {
        if (path.empty()) return;
        std::error_code removeError;
        fs::remove(path, removeError);
    };
    const auto restore = [&]() {
        state.config = originalConfig;
        state.campaignIndex = originalCampaignIndex;
        state.missionIndex = originalMissionIndex;
        state.page = originalPage;
        state.suppressPresentationAudio = originalSuppressAudio;
        state.progress = originalProgress;
        state.presentationResources.configure(state.config);
        state.presentationAudio.clear();
        state.missingPresentationAudio.clear();
        state.mapPresentation = CampaignMapPresentationState{};
    };
    const auto cleanup = [&]() {
        if (retainSmokeArtifacts) return;
        removeKnown(cachedVoicePath);
        removeKnown(palettePath);
        removeKnown(vqaPath);
        removeKnown(clickPath);
        removeKnown(voicePath);
        removeKnown(localRoot / "runtime_audio_cache"
            / "campaign_map_selection");
        removeKnown(localRoot / "runtime_audio_cache");
        removeKnown(modRoot);
        removeKnown(localRoot);
        removeKnown(emptyRoot);
        removeKnown(root);
    };
    const auto fail = [&](const std::string& message) {
        errorMessage = message;
        restore();
        cleanup();
        return false;
    };

    if (fileError
        || !fs::create_directory(root, fileError)
        || !fs::create_directory(modRoot, fileError)
        || !fs::create_directory(localRoot, fileError)
        || !fs::create_directory(emptyRoot, fileError)) {
        return fail("could not create campaign media fixture directories");
    }

    state.campaignIndex = campaignIndex;
    state.missionIndex = missionIndex;
    state.page = CampaignPage::Missions;
    const CampaignDefinition* campaign = selectedCampaign(state);
    if (campaign != nullptr
        && missionIndex >= 0
        && missionIndex < static_cast<int>(campaign->missions.size())) {
        state.progress.campaignId = campaign->id;
        state.progress.currentMissionId =
            campaign->missions[static_cast<std::size_t>(missionIndex)].id;
    }
    const CampaignMapSelectionStage* stage = selectedMapSelectionStage(state);
    if (campaign == nullptr || stage == nullptr || stage->choices.empty()
        || stage->targets.empty() || stage->mapVq.empty()
        || stage->clickMap.empty() || stage->voiceOver.empty()
        || campaign->mapSelectionAnims.palette.empty()) {
        return fail("no voiced authored stage is available for media smoke");
    }

    const int expectedColor = stage->choices.front().colorIndex;
    const CampaignMapSelectionPoint& target = stage->targets.front();
    const int markedX = std::clamp(target.x * 8 / kBaseWidth, 0, 7);
    const int markedY = std::clamp(target.y * 2 / 160, 0, 1);
    palettePath = modRoot / campaign->mapSelectionAnims.palette;
    vqaPath = modRoot / stage->mapVq;
    clickPath = modRoot / stage->clickMap;
    voicePath = modRoot / stage->voiceOver;
    const CampaignFixtureBytes voice = makePcmWaveFile(
        8000,
        8,
        1,
        CampaignFixtureBytes{128U, 132U, 124U, 128U}
    );
    if (!writeCampaignFixtureBytes(palettePath, makeCampaignFixturePalette())
        || !writeCampaignFixtureBytes(vqaPath, makeCampaignFixtureVqa())
        || !writeCampaignFixtureBytes(
            clickPath,
            makeCampaignFixtureClickMap(expectedColor, markedX, markedY)
        )
        || !writeCampaignFixtureBytes(voicePath, voice)) {
        return fail("could not write campaign media fixtures");
    }

    state.config.modRootPath = modRoot.string();
    state.config.localDataRootPath = localRoot.string();
    // Keep the test audio cache disposable while mounting the real stock SHP
    // layer beneath the synthetic core media overrides.
    state.config.resourceRootPath = stockSmokeRoot ? *stockSmokeRoot : originalConfig.localDataRootPath;
    state.presentationResources.configure(state.config);
    state.presentationAudio.clear();
    state.missingPresentationAudio.clear();
    state.mapPresentation = CampaignMapPresentationState{};
    state.suppressPresentationAudio = true;
    refreshCampaignMapPresentation(state);
    const std::string modPrefix = lowerAscii(modRoot.generic_string());
    const auto fromFixture = [&](const std::string& source) {
        return lowerAscii(source).rfind(modPrefix, 0U) == 0U;
    };
    if (state.mapPresentation.authoredReconstruction
        || state.mapPresentation.phase != CampaignMapPresentationPhase::VqaIntro
        || !state.mapPresentation.vqaFrame.valid()
        || !state.mapPresentation.hasClickMap()
        || state.mapPresentation.paletteFallback
        || state.mapPresentation.palette.size() != 256U
        || !fromFixture(state.mapPresentation.vqaFrame.source)
        || !fromFixture(state.mapPresentation.clickMapSource)
        || !fromFixture(state.mapPresentation.paletteSource)
        || !fromFixture(state.mapPresentation.voiceSource)
        || state.mapPresentation.overlays.empty()
        || state.mapPresentation.animations.empty()) {
        return fail("valid VQA/ClickMap/palette/voice did not reach production menu state");
    }
    const auto movingAnimation = std::find_if(
        state.mapPresentation.animations.begin(),
        state.mapPresentation.animations.end(),
        [](const CampaignMapAnimationVisual& animation) {
            return animation.frames.size() > 1U;
        }
    );
    if (movingAnimation == state.mapPresentation.animations.end()) {
        return fail("authored MapSel animation has no multi-frame payload");
    }
    const ULONGLONG animationFrameDuration = std::max<ULONGLONG>(
        1ULL,
        static_cast<ULONGLONG>(movingAnimation->frameRate)
            * 1000ULL / 60ULL
    );
    if (campaignMapAnimationFrameIndex(*movingAnimation, 0ULL) != 0U
        || campaignMapAnimationFrameIndex(
                *movingAnimation,
                animationFrameDuration
            ) != 1U) {
        return fail("authored MapSel animation did not advance deterministically");
    }
    const auto audio = state.presentationAudio.find(campaignAudioCacheKey(
        campaign->sourceId,
        stage->voiceOver
    ));
    if (audio == state.presentationAudio.end()
        || !fs::is_regular_file(audio->second.wavePath, fileError)) {
        return fail("authored VoiceOver payload was not decoded to the runtime cache");
    }
    cachedVoicePath = audio->second.wavePath;
    realPathSummary = "source=" + campaign->sourceId
        + " palette=" + state.mapPresentation.paletteSource
        + " vqa=" + state.mapPresentation.vqaFrame.source
        + " click=" + state.mapPresentation.clickMapSource
        + " voice=" + state.mapPresentation.voiceSource
        + " overlay=" + state.mapPresentation.overlays.front().source
        + " animation="
        + movingAnimation->frames.front().source;

    skipCampaignMapIntro(state);
    if (state.mapPresentation.phase
            != CampaignMapPresentationPhase::Interactive
        || campaignMapColorAtBasePoint(state, target.x, target.y)
            != expectedColor) {
        return fail("real ClickMap pixel did not survive VQA skip/hit testing");
    }
    std::vector<CampaignImage> savedOverlays = std::move(
        state.mapPresentation.overlays
    );
    std::vector<CampaignMapAnimationVisual> savedAnimations = std::move(
        state.mapPresentation.animations
    );
    const std::vector<unsigned char> undecorated =
        composeCampaignSurface(state);
    state.mapPresentation.overlays = savedOverlays;
    const std::vector<unsigned char> overlayOnly =
        composeCampaignSurface(state);
    state.mapPresentation.overlays.clear();
    state.mapPresentation.animations = savedAnimations;
    const std::vector<unsigned char> animationOnly =
        composeCampaignSurface(state);
    state.mapPresentation.overlays = std::move(savedOverlays);
    state.mapPresentation.animations = std::move(savedAnimations);
    if (overlayOnly == undecorated || animationOnly == undecorated) {
        return fail("authored MapSel overlay/animation did not alter production pixels");
    }
    const std::string expectedMission = stage->choices.front().targetMissionId;
    if (!confirmCampaignMapChoice(state, expectedColor)
        || selectedMission(state) == nullptr
        || lowerAscii(selectedMission(state)->id)
            != lowerAscii(expectedMission)) {
        return fail("real ClickMap selection did not switch missions");
    }
    realPathSummary += " missionSwitch=" + expectedMission;

    state.campaignIndex = campaignIndex;
    state.missionIndex = missionIndex;
    state.page = CampaignPage::Missions;
    state.config.modRootPath = emptyRoot.string();
    state.config.localDataRootPath = localRoot.string();
    state.config.resourceRootPath = emptyRoot.string();
    state.presentationResources.configure(state.config);
    state.presentationAudio.clear();
    state.missingPresentationAudio.clear();
    state.mapPresentation = CampaignMapPresentationState{};
    refreshCampaignMapPresentation(state);
    if (!state.mapPresentation.authoredReconstruction
        || !state.mapPresentation.hasClickMap()
        || state.mapPresentation.vqaFrame.valid()
        || !state.mapPresentation.paletteFallback
        || !state.mapPresentation.voiceSource.empty()
        || state.mapPresentation.phase
            != CampaignMapPresentationPhase::Interactive
        || campaignMapColorAtBasePoint(state, target.x, target.y)
            != expectedColor
        || state.mapPresentation.clickMapSource.rfind("MapSel Targets:", 0U)
            != 0U) {
        return fail("missing media did not use the safe authored-target fallback");
    }
    missingPathSummary = state.mapPresentation.diagnostic;
    restore();
    cleanup();
    return true;
}

bool auditCampaignMapMediaInventory(
    CampaignMenuState& state,
    const std::string& sourceId,
    std::size_t expectedReferences,
    std::string& summary,
    std::string& errorMessage
) {
    std::set<std::string> references;
    for (const CampaignDefinition& campaign : state.database.campaigns) {
        if (!campaignSourceIs(&campaign, sourceId)) continue;
        if (!campaign.mapSelectionAnims.palette.empty()) {
            references.insert(lowerAscii(
                campaign.mapSelectionAnims.palette
            ));
        }
        for (const CampaignMapSelectionStage& stage :
                campaign.mapSelectionStages) {
            if (!stage.mapVq.empty()) {
                references.insert(lowerAscii(stage.mapVq));
            }
            if (!stage.clickMap.empty()) {
                references.insert(lowerAscii(stage.clickMap));
            }
            if (!stage.voiceOver.empty()) {
                references.insert(lowerAscii(stage.voiceOver));
            }
        }
    }
    if (references.size() != expectedReferences) {
        errorMessage = sourceId + " MapSel media reference count changed: "
            + std::to_string(references.size()) + "/"
            + std::to_string(expectedReferences);
        return false;
    }

    std::vector<std::string> found;
    std::vector<std::string> missing;
    for (const std::string& resourceId : references) {
        const CampaignResourceRecord& resource =
            state.presentationResources.load(resourceId, sourceId);
        if (!resource.found) {
            missing.push_back(resourceId);
            continue;
        }
        if (sourceId == "ra2" && resource.edition == "yr") {
            errorMessage = "RA2 MapSel resolved an YR resource: "
                + resource.source;
            return false;
        }
        if (sourceId == "yr" && resource.edition == "ra2") {
            errorMessage = "YR MapSel resolved a base RA2 resource: "
                + resource.source;
            return false;
        }

        const std::string extension = lowerAscii(
            fs::path(resourceId).extension().string()
        );
        bool valid = false;
        if (extension == ".pal") {
            std::vector<RuntimePaletteColor> palette;
            valid = decodeCampaignPalette(resource.bytes, palette)
                && palette.size() == 256U;
        } else if (extension == ".pcx") {
            RuntimeShpFrameImage image;
            std::vector<RuntimePaletteColor> palette;
            std::string decodeError;
            valid = decodeRuntimePcxImageBytes(
                resource.bytes,
                image,
                palette,
                decodeError
            ) && image.width > 0 && image.height > 0;
        } else if (extension == ".vqa") {
            CampaignPresentationDecoder decoder;
            valid = decoder.loadFromMemory(resource.bytes)
                && decoder.width() > 0 && decoder.height() > 0;
        } else if (extension == ".aud") {
            valid = isWaveFileBytes(resource.bytes)
                || decodeAudToWave(resource.bytes).success;
        }
        if (!valid) {
            errorMessage = "invalid MapSel media payload: "
                + resource.source;
            return false;
        }
        found.push_back(resourceId + "=" + resource.source);
    }

    const auto join = [](const std::vector<std::string>& values) {
        std::ostringstream output;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0U) output << ',';
            output << values[index];
        }
        return output.str();
    };
    summary = "source=" + sourceId
        + " referenced=" + std::to_string(references.size())
        + " found=" + std::to_string(found.size())
        + " foundSources=[" + join(found) + "]"
        + " missing=[" + join(missing) + "]";
    return true;
}

bool probeCampaignMapRoute(
    CampaignMenuState& state,
    const std::string& sourceId,
    bool soviet,
    const std::string& label,
    std::string& summary,
    std::string& errorMessage
) {
    int campaignIndex = -1;
    int missionIndex = -1;
    for (std::size_t candidateIndex = 0;
        candidateIndex < state.database.campaigns.size()
            && campaignIndex < 0;
        ++candidateIndex) {
        const CampaignDefinition& campaign =
            state.database.campaigns[candidateIndex];
        if (!campaignSourceIs(&campaign, sourceId)
            || isTutorialCampaign(&campaign)
            || isSovietCampaign(&campaign) != soviet) {
            continue;
        }
        for (std::size_t candidateMissionIndex = 0;
            candidateMissionIndex < campaign.missions.size();
            ++candidateMissionIndex) {
            const CampaignMissionDefinition& mission =
                campaign.missions[candidateMissionIndex];
            if (!mission.available
                || mission.incomingMapSelectionStages.empty()) {
                continue;
            }
            const CampaignMapSelectionStage* stage =
                campaign.findMapSelectionStage(
                    mission.incomingMapSelectionStages.front()
                );
            if (stage != nullptr
                && !stage->targets.empty()
                && !stage->choices.empty()) {
                campaignIndex = static_cast<int>(candidateIndex);
                missionIndex = static_cast<int>(candidateMissionIndex);
                break;
            }
        }
    }
    if (campaignIndex < 0 || missionIndex < 0) {
        errorMessage = "no stock MapSel route for " + label;
        return false;
    }

    const int savedCampaignIndex = state.campaignIndex;
    const int savedMissionIndex = state.missionIndex;
    const CampaignPage savedPage = state.page;
    const CampaignProgressData savedProgress = state.progress;
    const bool savedSuppress = state.suppressPresentationAudio;
    CampaignMapPresentationState savedPresentation =
        std::move(state.mapPresentation);
    const auto restore = [&]() {
        state.campaignIndex = savedCampaignIndex;
        state.missionIndex = savedMissionIndex;
        state.page = savedPage;
        state.progress = savedProgress;
        state.suppressPresentationAudio = savedSuppress;
        state.mapPresentation = std::move(savedPresentation);
    };

    state.campaignIndex = campaignIndex;
    state.missionIndex = missionIndex;
    state.page = CampaignPage::Missions;
    state.suppressPresentationAudio = true;
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    if (campaign == nullptr || mission == nullptr) {
        restore();
        errorMessage = "MapSel route lost its campaign/mission: " + label;
        return false;
    }
    state.progress.campaignId = campaign->id;
    state.progress.currentMissionId = mission->id;
    state.mapPresentation = CampaignMapPresentationState{};
    refreshCampaignMapPresentation(state);
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    if (stage == nullptr || stage->targets.empty() || stage->choices.empty()) {
        restore();
        errorMessage = "MapSel route lost its authored stage: " + label;
        return false;
    }
    skipCampaignMapIntro(state);
    const int expectedColor = stage->choices.front().colorIndex;
    const CampaignMapSelectionPoint target = stage->targets.front();
    const bool interactive = state.mapPresentation.phase
            == CampaignMapPresentationPhase::Interactive
        && state.mapPresentation.hasClickMap()
        && campaignMapColorAtBasePoint(state, target.x, target.y)
            == expectedColor;
    const bool ra2Isolated = sourceId != "ra2"
        || (lowerAscii(state.mapPresentation.clickMapSource)
                .find("/ra2md/") == std::string::npos
            && lowerAscii(state.mapPresentation.vqaFrame.source)
                .find("/ra2md/") == std::string::npos
            && lowerAscii(state.mapPresentation.paletteSource)
                .find("/ra2md/") == std::string::npos);
    if (!interactive || !ra2Isolated) {
        restore();
        errorMessage = "MapSel route was not interactive or crossed editions: "
            + label;
        return false;
    }
    summary = label + ":source=" + sourceId
        + " campaign=" + campaign->id
        + " mission=" + mission->id
        + " stage=" + stage->id
        + " presentation="
        + (state.mapPresentation.authoredReconstruction
            ? "reconstructed"
            : "real")
        + " click=" + state.mapPresentation.clickMapSource;
    restore();
    return true;
}

bool probeCustomYuriCampaignMapRoute(
    CampaignMenuState& state,
    std::string& summary,
    std::string& errorMessage
) {
    CampaignDefinition fixture;
    int missionIndex = -1;
    const bool sourceHasYuriCampaign = std::any_of(
        state.database.campaigns.begin(),
        state.database.campaigns.end(),
        [](const CampaignDefinition& campaign) {
            return campaignSourceIs(&campaign, "yr")
                && lowerAscii(trimAscii(campaign.side)) == "yuri";
        }
    );
    for (const CampaignDefinition& candidate : state.database.campaigns) {
        if (!campaignSourceIs(&candidate, "yr")
            || isTutorialCampaign(&candidate)) {
            continue;
        }
        for (std::size_t candidateMissionIndex = 0;
            candidateMissionIndex < candidate.missions.size();
            ++candidateMissionIndex) {
            const CampaignMissionDefinition& mission =
                candidate.missions[candidateMissionIndex];
            if (!mission.available
                || mission.incomingMapSelectionStages.empty()) {
                continue;
            }
            const CampaignMapSelectionStage* stage =
                candidate.findMapSelectionStage(
                    mission.incomingMapSelectionStages.front()
                );
            if (stage != nullptr
                && !stage->targets.empty()
                && !stage->choices.empty()) {
                fixture = candidate;
                missionIndex = static_cast<int>(candidateMissionIndex);
                break;
            }
        }
        if (missionIndex >= 0) break;
    }
    if (missionIndex < 0) {
        errorMessage = "no YR-authored MapSel stage for the custom Yuri probe";
        return false;
    }

    fixture.id = "smoke-custom-yuri";
    fixture.side = "Yuri";
    fixture.battleSection = "YURI-SMOKE";
    fixture.sourceSection = "YURI-SMOKE";
    fixture.displayName = "Custom Yuri MapSel smoke fixture";
    for (CampaignMissionDefinition& mission : fixture.missions) {
        mission.campaignId = fixture.id;
    }

    const int savedCampaignIndex = state.campaignIndex;
    const int savedMissionIndex = state.missionIndex;
    const CampaignPage savedPage = state.page;
    const CampaignProgressData savedProgress = state.progress;
    const bool savedSuppress = state.suppressPresentationAudio;
    CampaignMapPresentationState savedPresentation =
        std::move(state.mapPresentation);
    state.database.campaigns.push_back(std::move(fixture));
    const int fixtureCampaignIndex = static_cast<int>(
        state.database.campaigns.size() - 1U
    );
    const auto restore = [&]() {
        state.campaignIndex = savedCampaignIndex;
        state.missionIndex = savedMissionIndex;
        state.page = savedPage;
        state.progress = savedProgress;
        state.suppressPresentationAudio = savedSuppress;
        state.mapPresentation = std::move(savedPresentation);
        state.database.campaigns.pop_back();
    };

    state.campaignIndex = fixtureCampaignIndex;
    state.missionIndex = missionIndex;
    state.page = CampaignPage::Missions;
    state.suppressPresentationAudio = true;
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    if (campaign == nullptr || mission == nullptr
        || lowerAscii(campaign->side) != "yuri"
        || !campaignSourceIs(campaign, "yr")) {
        restore();
        errorMessage = "custom Yuri MapSel fixture lost its side/source identity";
        return false;
    }
    state.progress.campaignId = campaign->id;
    state.progress.currentMissionId = mission->id;
    state.mapPresentation = CampaignMapPresentationState{};
    refreshCampaignMapPresentation(state);
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    if (stage == nullptr || stage->targets.empty() || stage->choices.empty()) {
        restore();
        errorMessage = "custom Yuri MapSel fixture lost its authored target";
        return false;
    }
    skipCampaignMapIntro(state);
    const CampaignMapSelectionPoint target = stage->targets.front();
    const bool interactive = state.mapPresentation.phase
            == CampaignMapPresentationPhase::Interactive
        && state.mapPresentation.hasClickMap()
        && campaignMapColorAtBasePoint(state, target.x, target.y)
            == stage->choices.front().colorIndex;
    if (!interactive) {
        restore();
        errorMessage = "custom Yuri MapSel route did not become interactive";
        return false;
    }
    summary = "Custom-Yuri:source=yr authorship=test-only-mod-fixture"
        " retailYuriCampaign="
        + std::string(sourceHasYuriCampaign ? "present" : "absent")
        + " presentation="
        + std::string(state.mapPresentation.authoredReconstruction
            ? "reconstructed"
            : "real")
        + " click=" + state.mapPresentation.clickMapSource;
    restore();
    return true;
}

bool runCampaignResourcePrioritySmoke(std::string& errorMessage) {
    const bool retainSmokeArtifacts = campaignSmokeRetainArtifacts();
    errorMessage.clear();
    std::error_code error;
    const fs::path root = fs::temp_directory_path(error) / (
        "concord-campaign-resource-priority-"
        + std::to_string(GetCurrentProcessId()) + "-"
        + std::to_string(GetTickCount64())
    );
    const fs::path modRoot = root / "mod";
    const fs::path localRoot = root / "local";
    const fs::path resourceRoot = root / "resource";
    const fs::path localFile = localRoot / "campaign-priority-probe.bin";
    const fs::path modMix = modRoot / "priority.mix";
    const auto cleanup = [&]() {
        if (retainSmokeArtifacts) return;
        const auto removeKnown = [](const fs::path& path) {
            std::error_code removeError;
            fs::remove(path, removeError);
        };
        removeKnown(localFile);
        removeKnown(modMix);
        removeKnown(modRoot);
        removeKnown(localRoot);
        removeKnown(resourceRoot);
        removeKnown(root);
    };
    if (error
        || !fs::create_directory(root, error)
        || !fs::create_directory(modRoot, error)
        || !fs::create_directory(localRoot, error)
        || !fs::create_directory(resourceRoot, error)) {
        errorMessage = "could not create resource-priority fixture";
        cleanup();
        return false;
    }

    const std::string fileName = "campaign-priority-probe.bin";
    {
        const std::array<unsigned char, 4> localBytes{{'L', 'O', 'W', 'R'}};
        std::ofstream output(localFile, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(localBytes.data()),
            static_cast<std::streamsize>(localBytes.size())
        );
        if (!output.good()) errorMessage = "could not write lower loose file";
    }
    if (errorMessage.empty()) {
        const std::array<unsigned char, 4> modBytes{{'M', 'O', 'D', '!'}};
        std::ofstream output(modMix, std::ios::binary | std::ios::trunc);
        const auto write16 = [&output](std::uint16_t value) {
            const std::array<unsigned char, 2> bytes{{
                static_cast<unsigned char>(value),
                static_cast<unsigned char>(value >> 8U)
            }};
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())
            );
        };
        const auto write32 = [&output](std::uint32_t value) {
            const std::array<unsigned char, 4> bytes{{
                static_cast<unsigned char>(value),
                static_cast<unsigned char>(value >> 8U),
                static_cast<unsigned char>(value >> 16U),
                static_cast<unsigned char>(value >> 24U)
            }};
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())
            );
        };
        write16(1U);
        write32(static_cast<std::uint32_t>(modBytes.size()));
        write32(mixFileIdFromName(fileName));
        write32(0U);
        write32(static_cast<std::uint32_t>(modBytes.size()));
        output.write(
            reinterpret_cast<const char*>(modBytes.data()),
            static_cast<std::streamsize>(modBytes.size())
        );
        if (!output.good()) errorMessage = "could not write mod MIX fixture";
    }

    bool priorityCorrect = false;
    if (errorMessage.empty()) {
        RuntimeConfig config;
        config.modRootPath = modRoot.string();
        config.localDataRootPath = localRoot.string();
        config.resourceRootPath = resourceRoot.string();
        CampaignResourceStore store;
        store.configure(config);
        store.prepare();
        const CampaignResourceRecord& record = store.load(fileName, "yr");
        priorityCorrect = record.found && record.fromMix
            && record.bytes.size() == 4U
            && record.bytes[0] == static_cast<unsigned char>('M')
            && lowerAscii(record.containerPath.filename().string())
                == "priority.mix";
        if (!priorityCorrect) {
            errorMessage =
                "higher-priority mod MIX lost to lower-priority loose data";
        }
    }

    cleanup();
    return priorityCorrect;
}

bool runCampaignResourceEditionIsolationSmoke(std::string& errorMessage) {
    const bool retainSmokeArtifacts = campaignSmokeRetainArtifacts();
    errorMessage.clear();
    std::error_code error;
    const fs::path root = fs::temp_directory_path(error) / (
        "concord-campaign-resource-edition-"
        + std::to_string(GetCurrentProcessId()) + "-"
        + std::to_string(GetTickCount64())
    );
    const fs::path ra2Root = root / "ra2";
    const fs::path yrRoot = root / "ra2md";
    const fs::path sharedName = "edition-probe.bin";
    const fs::path ra2Shared = ra2Root / sharedName;
    const fs::path yrShared = yrRoot / sharedName;
    const fs::path ra2Only = ra2Root / "ra2-only.aud";
    const fs::path yrOnly = yrRoot / "yr-only.aud";
    const auto removeKnown = [](const fs::path& path) {
        std::error_code removeError;
        fs::remove(path, removeError);
    };
    const auto cleanup = [&]() {
        if (retainSmokeArtifacts) return;
        removeKnown(ra2Shared);
        removeKnown(yrShared);
        removeKnown(ra2Only);
        removeKnown(yrOnly);
        removeKnown(ra2Root);
        removeKnown(yrRoot);
        removeKnown(root);
    };
    if (error
        || !fs::create_directory(root, error)
        || !fs::create_directory(ra2Root, error)
        || !fs::create_directory(yrRoot, error)) {
        errorMessage = "could not create resource-edition fixture";
        cleanup();
        return false;
    }

    const auto writeMarker = [&](const fs::path& path, unsigned char marker) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.put(static_cast<char>(marker));
        return output.good();
    };
    if (!writeMarker(ra2Shared, 'R')
        || !writeMarker(yrShared, 'Y')
        || !writeMarker(ra2Only, 'B')
        || !writeMarker(yrOnly, 'X')) {
        errorMessage = "could not write resource-edition fixture";
        cleanup();
        return false;
    }

    RuntimeConfig config;
    config.localDataRootPath = root.string();
    CampaignResourceStore store;
    store.configure(config);
    const CampaignResourceRecord& ra2First = store.load(
        sharedName.string(),
        "ra2"
    );
    const CampaignResourceRecord& yr = store.load(sharedName.string(), "yr");
    const CampaignResourceRecord& ra2AfterYr = store.load(
        sharedName.string(),
        "ra2"
    );
    const CampaignResourceRecord& blockedYr = store.load("yr-only.aud", "ra2");
    const CampaignResourceRecord& blockedRa2 = store.load(
        "ra2-only.aud",
        "yr"
    );
    const bool isolated = ra2First.found
        && ra2First.bytes.size() == 1U
        && ra2First.bytes[0] == static_cast<unsigned char>('R')
        && ra2First.edition == "ra2"
        && yr.found
        && yr.bytes.size() == 1U
        && yr.bytes[0] == static_cast<unsigned char>('Y')
        && yr.edition == "yr"
        && ra2AfterYr.found
        && ra2AfterYr.bytes.size() == 1U
        && ra2AfterYr.bytes[0] == static_cast<unsigned char>('R')
        && !blockedYr.found
        && !blockedRa2.found;
    if (!isolated) {
        errorMessage = "RA2/YR presentation lookup or cache crossed editions";
    }
    cleanup();
    return isolated;
}


#include "ui/campaign/diagnostics/campaign_clock_original/Methods.original.inl"
#include "ui/campaign/diagnostics/RuntimeCampaignPresentationClockSmoke.inl"

} // namespace

RuntimeCampaignSelection runRuntimeCampaignMenu(
    const RuntimeConfig& config,
    RuntimeWindowId hostWindow,
    bool loadGame
) {
    CampaignMenuState state;
    initializeCampaignMenu(state, config, loadGame);
    initializeCampaignAssets(state);

    const auto resolvedParent = resolveRuntimeWindowParentWin32(hostWindow);
    if (!resolvedParent.supported) return RuntimeCampaignSelection{};
    HWND parent = resolvedParent.window;
    if (parent != nullptr && !IsWindow(parent)) {
        parent = nullptr;
    }
    HWND window = createCampaignMenuWindow(state, parent);
    if (window == nullptr) {
        state.status = L"Campaign menu window could not be created.";
        return state.selection;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetForegroundWindow(parent == nullptr ? window : parent);
    SetFocus(window);

    while (IsWindow(window)) {
        MSG message{};
        bool dispatched = false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            dispatched = true;
            if (message.message == WM_QUIT) {
                closeMenu(state, false);
                PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (!IsWindow(window)) {
                break;
            }
        }
        if (!IsWindow(window)) {
            break;
        }
        if (!dispatched) {
            WaitMessage();
        }
    }
    if (state.accepted) {
        return state.selection;
    }
    return RuntimeCampaignSelection{};
}

#include "ui/campaign/diagnostics/RuntimeCampaignMenuHostLifecycleSmoke.inl"

int runRuntimeCampaignMenuSmokeTest(
    const RuntimeConfig& config,
    const std::string& outputPath
) {
    if (const auto only=campaignSmokeEnvironment("CONCORD_CAMPAIGN_HOST_LIFECYCLE_ONLY")) {
        if(*only!="1"){std::cerr<<"Campaign host lifecycle ONLY must be1\n";return 1;}
        return runRuntimeCampaignHostLifecycleSmoke(config);
    }
    if (const auto only=campaignSmokeEnvironment("CONCORD_CAMPAIGN_PRESENTATION_CLOCK_ONLY")) {
        const auto output=campaignSmokeEnvironment("CONCORD_CAMPAIGN_PRESENTATION_CLOCK_OUTPUT");
        if(*only!="1" || !output || output->empty()) {
            std::cerr<<"Campaign clock requires ONLY=1 and explicit OUTPUT\n";return 1;
        }
        return runCampaignPresentationClockSmoke(config,fs::path(*output));
    }
    std::string priorityError;
    if (!runCampaignResourcePrioritySmoke(priorityError)) {
        std::cerr << "Campaign UI smoke failed: " << priorityError << "\n";
        return 1;
    }
    if (!runCampaignResourceEditionIsolationSmoke(priorityError)) {
        std::cerr << "Campaign UI smoke failed: " << priorityError << "\n";
        return 1;
    }
    CampaignMenuState state;
    state.config = config;
    CampaignDatabaseOptions options;
    options.resourceRootPath = config.resourceRootPath;
    options.localDataRootPath = config.localDataRootPath;
    options.modRootPath = config.modRootPath;
    state.database = loadCampaignDatabase(options);
    if (!state.database.loaded || state.database.campaigns.empty()) {
        std::cerr << "Campaign UI smoke failed: campaign database is empty\n";
        return 1;
    }

    int yrCampaignIndex = -1;
    int ra2CampaignIndex = -1;
    std::size_t ra2MainCampaigns = 0;
    std::size_t ra2MainMissions = 0;
    for (std::size_t index = 0; index < state.database.campaigns.size(); ++index) {
        const CampaignDefinition& campaign = state.database.campaigns[index];
        const bool hasSelectableMission = std::any_of(
            campaign.missions.begin(),
            campaign.missions.end(),
            [](const CampaignMissionDefinition& mission) {
                return mission.available;
            }
        );
        if (campaignSourceIs(&campaign, "yr")
            && hasSelectableMission
            && yrCampaignIndex < 0) {
            yrCampaignIndex = static_cast<int>(index);
        }
        if (campaignSourceIs(&campaign, "ra2")
            && !isTutorialCampaign(&campaign)) {
            ++ra2MainCampaigns;
            ra2MainMissions += campaign.missions.size();
            if (hasSelectableMission && ra2CampaignIndex < 0) {
                ra2CampaignIndex = static_cast<int>(index);
            }
        }
    }
    if (yrCampaignIndex < 0) {
        std::cerr << "Campaign UI smoke failed: no selectable Yuri's Revenge "
                     "campaign was discovered\n";
        return 1;
    }
    if (ra2CampaignIndex < 0
        || ra2MainCampaigns < 2
        || ra2MainMissions < 24) {
        std::cerr << "Campaign UI smoke failed: base RA2 campaigns are not "
                     "selectable (campaigns=" << ra2MainCampaigns
                  << " missions=" << ra2MainMissions << ")\n";
        return 1;
    }

    // Keep the legacy authored-asset assertions pinned to YR even though the
    // combined database now sorts the base RA2 entries first.
    state.campaignIndex = yrCampaignIndex;
    chooseFirstUnlockedMission(state);
    if (!initializeCampaignAssets(state)
        || !state.campaignShell.valid()
        || state.campaignLogoFrames.empty()
        || !state.missionBackground.valid()) {
        std::cerr << "Campaign UI smoke failed: official asset stack did not load: "
                  << state.missionBackgroundError << "\n";
        return 1;
    }
    const std::string catalogSource = lowerAscii(state.csf.source);
    if (catalogSource.find("ra2.csf") == std::string::npos
        || catalogSource.find("ra2md.csf") == std::string::npos) {
        std::cerr << "Campaign UI smoke failed: base and expansion CSF "
                     "catalogs were not merged: " << state.csf.source << "\n";
        return 1;
    }
    const std::string shellSource = lowerAscii(state.campaignShell.source);
    const std::string logoSource = lowerAscii(
        state.campaignLogoFrames.front().source
    );
    const std::string missionSource = lowerAscii(state.missionBackground.source);
    if (shellSource.find("fsbkgdlg.shp") == std::string::npos
        || logoSource.find("fsalg.shp") == std::string::npos
        || lowerAscii(state.campaignLogoFrames.back().source).find("fsslg.shp") == std::string::npos
        || missionSource.find("loadmd.mix!ls800") == std::string::npos) {
        std::cerr << "Campaign UI smoke failed: assets were not sourced from "
                     "the authored campaign/local/load MIX stack\n"
                  << " shell=" << state.campaignShell.source << "\n"
                  << " logo=" << state.campaignLogoFrames.front().source << "\n"
                  << " mission=" << state.missionBackground.source << "\n";
        return 1;
    }

    std::string ra2MediaAudit;
    std::string yrMediaAudit;
    std::string mediaAuditError;
    if (!auditCampaignMapMediaInventory(
            state,
            "ra2",
            17U,
            ra2MediaAudit,
            mediaAuditError
        )
        || !auditCampaignMapMediaInventory(
            state,
            "yr",
            13U,
            yrMediaAudit,
            mediaAuditError
        )) {
        std::cerr << "Campaign UI smoke failed: MapSel inventory audit: "
                  << mediaAuditError << "\n";
        return 1;
    }

    std::vector<std::string> routeSummaries;
    for (const auto& route : std::array<std::tuple<
            std::string,
            bool,
            std::string
        >, 4>{{
            {"ra2", false, "RA2-Allied"},
            {"ra2", true, "RA2-Soviet"},
            {"yr", false, "YR-Allied"},
            {"yr", true, "YR-Soviet"}
        }}) {
        std::string routeSummary;
        if (!probeCampaignMapRoute(
                state,
                std::get<0>(route),
                std::get<1>(route),
                std::get<2>(route),
                routeSummary,
                mediaAuditError
            )) {
            std::cerr << "Campaign UI smoke failed: MapSel route probe: "
                      << mediaAuditError << "\n";
            return 1;
        }
        routeSummaries.push_back(std::move(routeSummary));
    }
    std::string customYuriRoute;
    if (!probeCustomYuriCampaignMapRoute(
            state,
            customYuriRoute,
            mediaAuditError
        )) {
        std::cerr << "Campaign UI smoke failed: custom Yuri MapSel route: "
                  << mediaAuditError << "\n";
        return 1;
    }
    routeSummaries.push_back(std::move(customYuriRoute));
    const CampaignDefinition* yrCampaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    if (yrCampaign == nullptr
        || mission == nullptr
        || state.csf.values.empty()
        || state.csf.get(mission->displayNameKey).empty()
        || state.csf.get(mission->briefingKey).empty()
        || resolvedCampaignName(state, yrCampaign).find(L"尤里的复仇")
            == std::wstring::npos) {
        std::cerr << "Campaign UI smoke failed: mission UIName/Briefing did not "
                     "resolve through the merged RA2/YR catalogs\n";
        return 1;
    }

    const int yrMissionIndex = state.missionIndex;
    state.campaignIndex = ra2CampaignIndex;
    chooseFirstUnlockedMission(state);
    const CampaignDefinition* ra2Campaign = selectedCampaign(state);
    const CampaignMissionDefinition* ra2Mission = selectedMission(state);
    if (ra2Campaign == nullptr
        || ra2Mission == nullptr
        || !missionUnlocked(state, *ra2Campaign, *ra2Mission)
        || state.csf.get(ra2Mission->displayNameKey).empty()
        || state.csf.get(ra2Mission->briefingKey).empty()
        || resolvedCampaignName(state, ra2Campaign).find(L"红色警戒 2")
            == std::wstring::npos) {
        std::cerr << "Campaign UI smoke failed: a base RA2 campaign could not "
                     "be selected and localized\n";
        return 1;
    }
    fillSelectionFromMission(
        state,
        *ra2Mission,
        false,
        "",
        "Normal"
    );
    if (state.selection.sourceId != "ra2") {
        std::cerr << "Campaign UI smoke failed: the mission source was not "
                     "propagated into the runtime selection\n";
        return 1;
    }
    state.selection = RuntimeCampaignSelection{};
    state.campaignIndex = yrCampaignIndex;
    state.missionIndex = yrMissionIndex;

    int stagedCampaignIndex = -1;
    int stagedMissionIndex = -1;
    for (std::size_t campaignIndex = 0;
        campaignIndex < state.database.campaigns.size()
            && stagedCampaignIndex < 0;
        ++campaignIndex) {
        const CampaignDefinition& candidate =
            state.database.campaigns[campaignIndex];
        if (!campaignSourceIs(&candidate, "yr")) continue;
        for (std::size_t missionIndex = 0;
            missionIndex < candidate.missions.size();
            ++missionIndex) {
            if (!candidate.missions[missionIndex]
                    .incomingMapSelectionStages.empty()) {
                stagedCampaignIndex = static_cast<int>(campaignIndex);
                stagedMissionIndex = static_cast<int>(missionIndex);
                break;
            }
        }
    }
    if (stagedCampaignIndex < 0 || stagedMissionIndex < 0) {
        std::cerr << "Campaign UI smoke failed: no mission has an incoming "
                     "map-selection stage\n";
        return 1;
    }
    state.campaignIndex = stagedCampaignIndex;
    state.missionIndex = stagedMissionIndex;
    state.page = CampaignPage::Missions;
    state.suppressPresentationAudio = true;
    refreshMissionBackground(state);
    refreshCampaignMapPresentation(state);
    const CampaignMapSelectionStage* mapStage =
        currentMapSelectionStage(state);
    if (mapStage == nullptr || mapStage->choices.empty()) {
        std::cerr << "Campaign UI smoke failed: incoming map stage or choices "
                     "were not consumed by the menu\n";
        return 1;
    }
    if (state.mapPresentation.overlays.empty()
        || state.mapPresentation.animations.empty()) {
        std::cerr << "Campaign UI smoke failed: authored overlay/ambient SHP "
                     "resources were not consumed by the presentation\n";
        return 1;
    }
    const CampaignDefinition* stagedCampaign = selectedCampaign(state);
    CampaignMapSelectionStage branchProbe;
    if (stagedCampaign != nullptr) {
        for (const CampaignMissionDefinition& candidate :
                stagedCampaign->missions) {
            if (candidate.available
                && !missionUnlocked(state, *stagedCampaign, candidate)) {
                CampaignMapSelectionChoice branch;
                branch.colorIndex = 17;
                branch.targetMissionId = candidate.id;
                branchProbe.choices.push_back(std::move(branch));
                break;
            }
        }
    }
    if (branchProbe.choices.empty()
        || firstUsableMapChoiceColor(state, branchProbe) != 17) {
        std::cerr << "Campaign UI smoke failed: an accessible stage did not "
                     "offer its available alternate branch\n";
        return 1;
    }

    CampaignMapPresentationState loadedPresentation =
        std::move(state.mapPresentation);
    state.mapPresentation = CampaignMapPresentationState{};
    state.mapPresentation.key = "synthetic-click-map-smoke";
    state.mapPresentation.stageSection = mapStage->id;
    state.mapPresentation.phase = CampaignMapPresentationPhase::Interactive;
    state.mapPresentation.clickMap.width = kBaseWidth;
    state.mapPresentation.clickMap.height = kBaseHeight;
    state.mapPresentation.clickMap.pixels.assign(
        static_cast<std::size_t>(kBaseWidth * kBaseHeight),
        0
    );
    const int expectedColor = mapStage->choices.front().colorIndex;
    state.mapPresentation.clickMap.pixels[
        static_cast<std::size_t>(100 * kBaseWidth + 100)
    ] = static_cast<unsigned char>(expectedColor);
    if (campaignMapColorAtBasePoint(state, 100, 100) != expectedColor
        || campaignMapColorAtBasePoint(state, 20, 20) != 0) {
        std::cerr << "Campaign UI smoke failed: ClickMap palette hit-testing "
                     "did not distinguish target/background pixels\n";
        return 1;
    }
    state.mapPresentation = std::move(loadedPresentation);

    int voicedCampaignIndex = -1;
    int voicedMissionIndex = -1;
    for (std::size_t campaignIndex = 0;
        campaignIndex < state.database.campaigns.size()
            && voicedCampaignIndex < 0;
        ++campaignIndex) {
        const CampaignDefinition& candidate =
            state.database.campaigns[campaignIndex];
        if (!campaignSourceIs(&candidate, "yr")) continue;
        for (std::size_t missionIndex = 0;
            missionIndex < candidate.missions.size();
            ++missionIndex) {
            const CampaignMissionDefinition& candidateMission =
                candidate.missions[missionIndex];
            for (const std::string& stageId :
                    candidateMission.incomingMapSelectionStages) {
                const CampaignMapSelectionStage* candidateStage =
                    candidate.findMapSelectionStage(stageId);
                if (candidateStage != nullptr
                    && !candidateStage->voiceOver.empty()
                    && !candidateStage->mapVq.empty()
                    && !candidateStage->clickMap.empty()
                    && !candidateStage->targets.empty()
                    && !candidateStage->choices.empty()) {
                    voicedCampaignIndex = static_cast<int>(campaignIndex);
                    voicedMissionIndex = static_cast<int>(missionIndex);
                    break;
                }
            }
            if (voicedCampaignIndex >= 0) break;
        }
    }
    std::string realMediaSummary;
    std::string missingMediaSummary;
    std::string mediaError;
    if (voicedCampaignIndex < 0
        || !runCampaignMapMediaPathSmoke(
            state,
            voicedCampaignIndex,
            voicedMissionIndex,
            realMediaSummary,
            missingMediaSummary,
            mediaError
        )) {
        std::cerr << "Campaign UI smoke failed: real/missing map-media paths: "
                  << mediaError << "\n";
        return 1;
    }
    refreshMissionBackground(state);
    refreshCampaignMapPresentation(state);
    skipCampaignMapIntro(state);
    if (state.mapPresentation.phase
            != CampaignMapPresentationPhase::Interactive
        || !state.mapPresentation.usesClassicSurface()
        || !state.mapPresentation.hasClickMap()) {
        std::cerr << "Campaign UI smoke failed: authored stock stage did not "
                     "reach an interactive classic map surface\n";
        return 1;
    }
    if (state.mapPresentation.authoredReconstruction
        && !mapStage->targets.empty()) {
        const CampaignMapSelectionPoint& target = mapStage->targets.front();
        if (campaignMapColorAtBasePoint(state, target.x, target.y)
            != mapStage->choices.front().colorIndex) {
            std::cerr << "Campaign UI smoke failed: reconstructed authored "
                         "target is not clickable\n";
            return 1;
        }
    }
    if (state.mapPresentation.phaseStartedAt >= 1000ULL) {
        state.mapPresentation.phaseStartedAt -= 1000ULL;
    } else {
        state.mapPresentation.phaseStartedAt = 0;
    }

    std::string navigationError;
    if (!runCampaignMenuNavigationSmoke(state, navigationError)) {
        std::cerr << "Campaign UI smoke failed: " << navigationError << "\n";
        return 1;
    }
    state.page = CampaignPage::Campaigns;
    state.campaignIndex = yrCampaignIndex;
    chooseFirstUnlockedMission(state);
    refreshCampaignMapPresentation(state);
    if (!runCampaignMenuPaintSmoke(state, navigationError)) {
        std::cerr << "Campaign UI smoke failed: " << navigationError << "\n";
        return 1;
    }
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = kCampaignShellWidth;
    bitmapInfo.bmiHeader.biHeight = -kCampaignShellHeight;
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
        0
    );
    ReleaseDC(nullptr, screen);
    if (memory == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (memory != nullptr) DeleteDC(memory);
        std::cerr << "Campaign UI smoke failed: could not create DIB surface\n";
        return 1;
    }
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    const auto systemFontTraceOutput=campaignSmokeEnvironment("CONCORD_CAMPAIGN_SYSTEM_FONT_TRACE_OUTPUT");
    std::vector<CampaignSystemFontTraceCall> systemFontCalls;
    CampaignSystemFontTraceScope systemFontTraceScope(systemFontTraceOutput ? &systemFontCalls : nullptr);
    drawCampaignMenu(state, memory, RECT{0, 0, kCampaignShellWidth, kCampaignShellHeight});
    GdiFlush();

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        std::cerr << "Campaign UI smoke failed: could not open "
                  << outputPath << "\n";
        return 1;
    }
    const DWORD pixelBytes = static_cast<DWORD>(
        kCampaignShellWidth * kCampaignShellHeight * 4
    );
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
    bool written = output.good();
    for (const auto& [page, suffix] : std::array<std::pair<CampaignPage, const char*>, 4>{{
            {CampaignPage::Missions, "missions"}, {CampaignPage::Difficulty, "difficulty"},
            {CampaignPage::Briefing, "briefing"}, {CampaignPage::Saves, "saves"}}}) {
        state.page = page;
        resetCampaignMenuInput(state);
        refreshCampaignMapPresentation(state);
        drawCampaignMenu(state, memory, RECT{0, 0, kCampaignShellWidth, kCampaignShellHeight});
        GdiFlush();
        const fs::path pagePath = fs::path(outputPath).parent_path()
            / (fs::path(outputPath).stem().string() + "-" + suffix + ".bmp");
        std::ofstream pageOutput(pagePath, std::ios::binary | std::ios::trunc);
        pageOutput.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        pageOutput.write(reinterpret_cast<const char*>(&bitmapInfo.bmiHeader), sizeof(bitmapInfo.bmiHeader));
        pageOutput.write(reinterpret_cast<const char*>(bits), pixelBytes);
        written = written && pageOutput.good();
    }
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    if (!written) {
        std::cerr << "Campaign UI smoke failed while writing BMP\n";
        return 1;
    }
    if(systemFontTraceOutput && (systemFontTraceOutput->empty()
        || !writeCampaignSystemFontTrace(fs::path(*systemFontTraceOutput),systemFontCalls))) {
        std::cerr<<"Campaign system font request trace write failed\n";return 1;
    }
    std::ostringstream routeReport;
    for (std::size_t index = 0; index < routeSummaries.size(); ++index) {
        if (index != 0U) routeReport << "; ";
        routeReport << routeSummaries[index];
    }
    std::cout << "Campaign UI smoke passed: shell="
              << state.campaignShell.source
              << " logo=" << state.campaignLogoFrames.front().source
              << " mission=" << state.missionBackground.source
              << " csf=" << state.csf.source
              << " mapStage=" << mapStage->sourceSection
              << " navigation=hosted-mouse-keyboard-800x600,1280x720,1024x768"
              << " repaint=idle-stable,single-complete-frame"
              << " ra2Media={" << ra2MediaAudit << "}"
              << " yrMedia={" << yrMediaAudit << "}"
              << " routes={" << routeReport.str() << "}"
              << " realMedia={" << realMediaSummary << "}"
              << " missingMedia={" << missingMediaSummary << "}"
              << " output=" << outputPath << "\n";
    return 0;
}
