#include "RuntimeCampaignDataSource.h"

#include "IniFile.h"
#include "MixArchive.h"
#include "RuntimeIniTextReadBuffer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaximumRuntimeIniBytes = 16U * 1024U * 1024U;
thread_local int runtimeIniTextReadReferenceOverride = -1;

bool runtimeIniTextReadReference() {
    if (runtimeIniTextReadReferenceOverride >= 0) return runtimeIniTextReadReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_INI_TEXT_READ_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

std::string trimAscii(std::string value) {
    const auto notSpace = [](unsigned char character) {
        return std::isspace(character) == 0;
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace)
    );
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end()
    );
    return value;
}

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

std::string upperAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return value;
}

std::string pathString(const fs::path& path) {
    const std::u8string encoded = path.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size()
    );
}

bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error) && !error;
}

std::vector<std::string> editionOrder(const std::string& sourceId) {
    const std::string normalized = normalizeRuntimeCampaignSourceId(sourceId);
    if (normalized == "ra2" || normalized == "yr") {
        return {normalized};
    }
    if (!normalized.empty()) {
        return {};
    }
    return {"yr", "ra2"};
}

RuntimeIniSource looseSource(
    const std::string& sourceId,
    const fs::path& path
) {
    RuntimeIniSource source;
    source.sourceId = sourceId;
    source.displayPath = pathString(path);
    source.backingPath = source.displayPath;
    return source;
}

RuntimeIniSource mixSource(
    const std::string& sourceId,
    const fs::path& path,
    const std::string& entryName
) {
    RuntimeIniSource source;
    source.sourceId = sourceId;
    source.backingPath = pathString(path);
    source.entryName = entryName;
    source.displayPath = source.backingPath + "!" + entryName;
    source.mixEntry = true;
    return source;
}

void appendLooseCandidates(
    std::vector<fs::path>& candidates,
    const fs::path& root,
    const std::string& sourceId,
    const std::string& fileName,
    bool localDataLayout
) {
    if (root.empty()) {
        return;
    }

    candidates.push_back(root / fileName);
    if (sourceId == "ra2") {
        candidates.push_back(root / "Game" / fileName);
        candidates.push_back(root / "ra2" / "local" / fileName);
        candidates.push_back(root / "ra2" / fileName);
        if (localDataLayout) {
            candidates.push_back(
                root / "resources" / "mix_exports" / "ra2" / "local"
                    / fileName
            );
            candidates.push_back(
                root / "resources" / "mix_exports" / "ra2" / fileName
            );
        }
        return;
    }

    candidates.push_back(root / "GameMD" / fileName);
    candidates.push_back(root / "ra2md" / "localmd" / fileName);
    candidates.push_back(root / "localmd" / fileName);
    if (localDataLayout) {
        const fs::path exports = root / "resources" / "mix_exports";
        candidates.push_back(exports / "ra2md" / "localmd" / fileName);
        // Retail YR patch theater/sound layers are kept as loose prepared
        // resources by the repository. They remain within the YR edition.
        candidates.push_back(exports / "expandmd02" / fileName);
        candidates.push_back(exports / "expandmd01" / fileName);
        candidates.push_back(exports / "ra2md" / fileName);
        candidates.push_back(exports / "localmd" / fileName);
    }
}

void appendMixCandidates(
    std::vector<fs::path>& candidates,
    const fs::path& root,
    const std::string& sourceId,
    bool localDataLayout
) {
    if (root.empty()) {
        return;
    }
    if (sourceId == "ra2") {
        candidates.push_back(root / "local.mix");
        candidates.push_back(root / "Game" / "local.mix");
        candidates.push_back(root / "GameMD" / "local.mix");
        candidates.push_back(root / "ra2" / "local.mix");
        if (localDataLayout) {
            candidates.push_back(
                root / "resources" / "mix_exports" / "ra2" / "local.mix"
            );
        }
        return;
    }

    candidates.push_back(root / "localmd.mix");
    candidates.push_back(root / "GameMD" / "localmd.mix");
    candidates.push_back(root / "ra2md" / "localmd.mix");
    if (localDataLayout) {
        candidates.push_back(
            root / "resources" / "mix_exports" / "ra2md" / "localmd.mix"
        );
    }
}

RuntimeIniSource resolveEditionFile(
    const RuntimeCampaignDataSourceOptions& options,
    const std::string& sourceId,
    const std::string& fileName
) {
    std::vector<fs::path> looseCandidates;
    // An explicit mod is the highest-priority runtime layer, but it is still
    // constrained to the selected edition's canonical filename.
    appendLooseCandidates(
        looseCandidates,
        fs::path(options.modRootPath),
        sourceId,
        fileName,
        false
    );
    appendLooseCandidates(
        looseCandidates,
        fs::path(options.localDataRootPath),
        sourceId,
        fileName,
        true
    );
    appendLooseCandidates(
        looseCandidates,
        fs::path(options.resourceRootPath),
        sourceId,
        fileName,
        false
    );
    for (const fs::path& candidate : looseCandidates) {
        if (regularFile(candidate)) {
            return looseSource(sourceId, candidate);
        }
    }

    std::vector<fs::path> mixCandidates;
    appendMixCandidates(
        mixCandidates,
        fs::path(options.modRootPath),
        sourceId,
        false
    );
    appendMixCandidates(
        mixCandidates,
        fs::path(options.localDataRootPath),
        sourceId,
        true
    );
    appendMixCandidates(
        mixCandidates,
        fs::path(options.resourceRootPath),
        sourceId,
        false
    );
    for (const fs::path& candidate : mixCandidates) {
        if (!regularFile(candidate)) {
            continue;
        }
        MixArchive archive;
        if (!archive.loadFromPath(candidate) || !archive.isIndexReadable()) {
            continue;
        }
        const MixEntry* entry = archive.findEntryByName(fileName);
        if (entry != nullptr && entry->size > 0
            && entry->size <= kMaximumRuntimeIniBytes) {
            return mixSource(sourceId, candidate, fileName);
        }
    }
    return {};
}

std::string theaterFileName(
    const std::string& sourceId,
    const std::string& mapTheater
) {
    const std::string theater = upperAscii(trimAscii(mapTheater));
    if (sourceId == "ra2") {
        if (theater == "TEMPERATE") return "temperat.ini";
        if (theater == "SNOW") return "snow.ini";
        if (theater == "URBAN") return "urban.ini";
        return {};
    }
    if (sourceId == "yr") {
        if (theater == "TEMPERATE") return "temperatmd.ini";
        if (theater == "SNOW") return "snowmd.ini";
        if (theater == "URBAN") return "urbanmd.ini";
        if (theater == "NEWURBAN") return "urbannmd.ini";
        if (theater == "DESERT") return "desertmd.ini";
        if (theater == "LUNAR") return "lunarmd.ini";
    }
    return {};
}

bool readLooseText(
    const std::string& path,
    std::string& text,
    std::string& errorMessage
) {
    std::error_code error;
    const std::uintmax_t bytes = fs::file_size(fs::path(path), error);
    if (error || bytes == 0 || bytes > kMaximumRuntimeIniBytes
        || bytes > static_cast<std::uintmax_t>(
            std::numeric_limits<std::streamsize>::max()
        )) {
        errorMessage = error
            ? "runtime INI size could not be read: " + path
            : "runtime INI has an invalid size: " + path;
        return false;
    }
    std::ifstream input(fs::path(path), std::ios::binary);
    if (!input) {
        errorMessage = "runtime INI could not be opened: " + path;
        return false;
    }
    // Stat is only the historical precondition and an allocation hint. Read
    // through actual EOF, including growth, shrinkage and binary tail bytes.
    const bool reference = runtimeIniTextReadReference();
    if (!reference) text.reserve(static_cast<std::size_t>(bytes));
    RuntimeIniTextReadDetail::readToEnd(input, text, reference);
    if (text.empty() || text.size() > kMaximumRuntimeIniBytes) {
        errorMessage = "runtime INI could not be read: " + path;
        return false;
    }
    return true;
}

bool readMixText(
    const RuntimeIniSource& source,
    std::string& text,
    std::string& errorMessage
) {
    MixArchive archive;
    if (!archive.loadFromFile(source.backingPath)
        || !archive.isIndexReadable()) {
        errorMessage = "runtime MIX could not be opened: "
            + source.backingPath;
        return false;
    }
    const MixEntry* entry = archive.findEntryByName(source.entryName);
    if (entry == nullptr || entry->size == 0
        || entry->size > kMaximumRuntimeIniBytes) {
        errorMessage = "runtime INI entry is missing or invalid: "
            + source.displayPath;
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!archive.readEntryData(*entry, bytes, errorMessage)
        || bytes.empty() || bytes.size() > kMaximumRuntimeIniBytes) {
        if (errorMessage.empty()) {
            errorMessage = "runtime INI entry could not be read: "
                + source.displayPath;
        }
        return false;
    }
    while (!bytes.empty() && bytes.back() == 0) {
        bytes.pop_back();
    }
    if (bytes.empty()) {
        errorMessage = "runtime INI entry is empty: " + source.displayPath;
        return false;
    }
    text.assign(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()
    );
    return true;
}

} // namespace

RuntimeIniTextReadReferenceScope::RuntimeIniTextReadReferenceScope(bool reference) noexcept
    : previous_(runtimeIniTextReadReferenceOverride) {
    runtimeIniTextReadReferenceOverride = reference ? 1 : 0;
}

RuntimeIniTextReadReferenceScope::~RuntimeIniTextReadReferenceScope() noexcept {
    runtimeIniTextReadReferenceOverride = previous_;
}

bool RuntimeIniSource::available() const {
    return !displayPath.empty() && !backingPath.empty();
}

bool RuntimeIniSource::readText(
    std::string& text,
    std::string* errorMessage
) const {
    text.clear();
    std::string localError;
    if (!available()) {
        localError = "runtime INI source is unavailable";
    } else if (mixEntry) {
        if (readMixText(*this, text, localError)) {
            if (errorMessage != nullptr) errorMessage->clear();
            return true;
        }
    } else if (readLooseText(backingPath, text, localError)) {
        if (errorMessage != nullptr) errorMessage->clear();
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = std::move(localError);
    }
    return false;
}

bool RuntimeIniSource::load(
    IniFile& ini,
    std::string* errorMessage
) const {
    std::string text;
    std::string localError;
    if (!readText(text, &localError)) {
        if (errorMessage != nullptr) {
            *errorMessage = std::move(localError);
        }
        return false;
    }
    IniFile parsed;
    if (!parsed.loadFromString(text)) {
        if (errorMessage != nullptr) {
            *errorMessage = "runtime INI could not be parsed: " + displayPath;
        }
        return false;
    }
    ini = std::move(parsed);
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

std::string normalizeRuntimeCampaignSourceId(const std::string& sourceId) {
    const std::string normalized = lowerAscii(trimAscii(sourceId));
    if (normalized == "ra2" || normalized == "yr") {
        return normalized;
    }
    return normalized;
}

std::string runtimeCampaignIniFileName(
    RuntimeCampaignIniKind kind,
    const std::string& sourceId
) {
    const std::string normalized = normalizeRuntimeCampaignSourceId(sourceId);
    const bool yr = normalized == "yr";
    switch (kind) {
    case RuntimeCampaignIniKind::Rules:
        return yr ? "rulesmd.ini" : "rules.ini";
    case RuntimeCampaignIniKind::Art:
        return yr ? "artmd.ini" : "art.ini";
    case RuntimeCampaignIniKind::Ai:
        return yr ? "aimd.ini" : "ai.ini";
    case RuntimeCampaignIniKind::Sound:
        return yr ? "soundmd.ini" : "sound.ini";
    case RuntimeCampaignIniKind::Eva:
        return yr ? "evamd.ini" : "eva.ini";
    }
    return {};
}

RuntimeIniSource resolveRuntimeCampaignIniSource(
    const RuntimeCampaignDataSourceOptions& options,
    RuntimeCampaignIniKind kind
) {
    for (const std::string& sourceId : editionOrder(options.sourceId)) {
        RuntimeIniSource source = resolveEditionFile(
            options,
            sourceId,
            runtimeCampaignIniFileName(kind, sourceId)
        );
        if (source.available()) {
            return source;
        }
    }
    return {};
}

RuntimeIniSource resolveRuntimeCampaignTheaterSource(
    const RuntimeCampaignDataSourceOptions& options,
    const std::string& mapTheater
) {
    for (const std::string& sourceId : editionOrder(options.sourceId)) {
        const std::string fileName = theaterFileName(sourceId, mapTheater);
        if (fileName.empty()) {
            continue;
        }
        RuntimeIniSource source = resolveEditionFile(
            options,
            sourceId,
            fileName
        );
        if (source.available()) {
            return source;
        }
    }
    return {};
}
