#include "runtime/campaign/RuntimeCampaignMovieResolver.h"

#include "assets/archive/MixArchive.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t kMaximumCampaignMovieBytes =
    1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kCopyBufferBytes = 1024U * 1024U;

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

std::string pathString(const fs::path& path) {
    const std::u8string encoded = path.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size()
    );
}

fs::path absoluteNormalized(const fs::path& path) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error) && !error;
}

std::uint64_t regularFileSize(const fs::path& path) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size > std::numeric_limits<std::uint64_t>::max()) {
        return 0;
    }
    return static_cast<std::uint64_t>(size);
}

std::vector<std::string> editionOrder(const std::string& sourceId) {
    const std::string normalized =
        normalizeRuntimeCampaignMovieSourceId(sourceId);
    if (normalized == "ra2" || normalized == "yr") {
        return {normalized};
    }
    if (!normalized.empty()) {
        return {};
    }
    // Preserve the non-campaign historical preference while keeping every
    // explicit campaign selection strictly edition-bound.
    return {"yr", "ra2"};
}

std::string normalizedMovieEntryName(const std::string& movieId) {
    std::string value = trimAscii(movieId);
    if (value.empty()) {
        return {};
    }
    fs::path path(value);
    if (path.filename() != path || path.has_parent_path()) {
        return {};
    }
    if (path.extension().empty()) {
        value += ".bik";
        path = fs::path(value);
    }
    if (lowerAscii(path.extension().string()) != ".bik") {
        return {};
    }
    const bool safe = std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char character) {
            return std::isalnum(character) != 0
                || character == '_' || character == '-'
                || character == '.';
        }
    );
    return safe ? value : std::string{};
}

void appendUniquePath(std::vector<fs::path>& paths, const fs::path& path) {
    if (path.empty()) {
        return;
    }
    const std::string key = lowerAscii(pathString(path.lexically_normal()));
    const bool duplicate = std::any_of(
        paths.begin(),
        paths.end(),
        [&key](const fs::path& existing) {
            return lowerAscii(pathString(existing.lexically_normal())) == key;
        }
    );
    if (!duplicate) {
        paths.push_back(path);
    }
}

std::vector<fs::path> looseCandidates(
    const fs::path& root,
    const std::string& sourceId,
    const std::string& entryName
) {
    std::vector<fs::path> candidates;
    if (root.empty()) {
        return candidates;
    }
    if (sourceId == "ra2") {
        for (const fs::path& directory : {
                 root / "resources" / "mix_exports" / "movies01",
                 root / "resources" / "mix_exports" / "movies02",
                 root / "movies01",
                 root / "movies02",
                 root / "Game" / "Movies",
                 root / "Game"
             }) {
            appendUniquePath(candidates, directory / entryName);
        }
    } else {
        for (const fs::path& directory : {
                 root / "resources" / "mix_exports" / "movmd03",
                 root / "resources" / "mix_exports" / "ra2md" / "movmd03",
                 root / "movmd03",
                 root / "GameMD" / "Movies",
                 root / "GameMD"
             }) {
            appendUniquePath(candidates, directory / entryName);
        }
    }
    return candidates;
}

std::vector<fs::path> mixCandidates(
    const fs::path& root,
    const std::string& sourceId
) {
    std::vector<fs::path> candidates;
    if (root.empty()) {
        return candidates;
    }
    if (sourceId == "ra2") {
        for (const fs::path& candidate : {
                 root / "movies01.mix",
                 root / "movies02.mix",
                 root / "Game" / "movies01.mix",
                 root / "Game" / "movies02.mix",
                 root / "resources" / "mix_exports" / "movies01.mix",
                 root / "resources" / "mix_exports" / "movies02.mix"
             }) {
            appendUniquePath(candidates, candidate);
        }
    } else {
        for (const fs::path& candidate : {
                 root / "movmd03.mix",
                 root / "GameMD" / "movmd03.mix",
                 root / "resources" / "mix_exports" / "movmd03.mix",
                 root / "resources" / "mix_exports" / "ra2md" / "movmd03.mix"
             }) {
            appendUniquePath(candidates, candidate);
        }
    }
    return candidates;
}

RuntimeCampaignMovieResolution resolveForEdition(
    const RuntimeCampaignMovieResolverOptions& options,
    const std::string& sourceId,
    const std::string& movieId,
    const std::string& entryName
) {
    for (const std::string& rootText : {
             options.modRootPath,
             options.localDataRootPath,
             options.resourceRootPath
         }) {
        if (rootText.empty()) {
            continue;
        }
        const fs::path root(rootText);
        for (const fs::path& candidate : looseCandidates(
                 root,
                 sourceId,
                 entryName
             )) {
            if (!regularFile(candidate)) {
                continue;
            }
            RuntimeCampaignMovieResolution resolution;
            resolution.found = true;
            resolution.sourceId = sourceId;
            resolution.movieId = movieId;
            resolution.referencePath = pathString(absoluteNormalized(candidate));
            resolution.byteSize = regularFileSize(candidate);
            return resolution;
        }
        for (const fs::path& candidate : mixCandidates(root, sourceId)) {
            if (!regularFile(candidate)) {
                continue;
            }
            MixArchive archive;
            if (!archive.loadFromPath(candidate) || !archive.isIndexReadable()) {
                continue;
            }
            const MixEntry* entry = archive.findEntryByName(entryName);
            if (entry == nullptr || entry->size == 0
                || entry->size > kMaximumCampaignMovieBytes) {
                continue;
            }
            RuntimeCampaignMovieResolution resolution;
            resolution.found = true;
            resolution.mixBacked = true;
            resolution.sourceId = sourceId;
            resolution.movieId = movieId;
            resolution.archivePath = pathString(
                absoluteNormalized(candidate)
            );
            resolution.entryName = entryName;
            resolution.referencePath = resolution.archivePath
                + "!" + entryName;
            resolution.byteSize = entry->size;
            return resolution;
        }
    }
    return {};
}

void hashByte(std::uint64_t& hash, std::uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
}

void hashString(std::uint64_t& hash, const std::string& value) {
    for (const unsigned char byte : value) {
        hashByte(hash, byte);
    }
}

void hashInteger(std::uint64_t& hash, std::uint64_t value) {
    for (unsigned int index = 0; index < 8U; ++index) {
        hashByte(hash, static_cast<std::uint8_t>(value & 0xffU));
        value >>= 8U;
    }
}

std::string cacheIdentity(
    const fs::path& archivePath,
    const MixEntry& entry
) {
    std::uint64_t hash = 1469598103934665603ULL;
    hashString(hash, lowerAscii(pathString(absoluteNormalized(archivePath))));
    hashInteger(hash, regularFileSize(archivePath));
    std::error_code timeError;
    const fs::file_time_type writeTime = fs::last_write_time(
        archivePath,
        timeError
    );
    if (!timeError) {
        hashInteger(
            hash,
            static_cast<std::uint64_t>(writeTime.time_since_epoch().count())
        );
    }
    hashInteger(hash, entry.absoluteOffset);
    hashInteger(hash, entry.size);
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

bool fileHasSize(const fs::path& path, std::uint64_t expectedSize) {
    return regularFile(path) && regularFileSize(path) == expectedSize;
}

bool removeSingleFile(const fs::path& path, std::string& errorMessage) {
    std::error_code statusError;
    if (!fs::exists(path, statusError)) {
        return !statusError;
    }
    if (statusError || !fs::is_regular_file(path, statusError)
        || statusError) {
        errorMessage = "movie cache target is not a regular file: "
            + pathString(path);
        return false;
    }
    std::error_code removeError;
    if (!fs::remove(path, removeError) || removeError) {
        errorMessage = "could not replace movie cache file: "
            + removeError.message();
        return false;
    }
    return true;
}

bool streamMixEntry(
    const fs::path& archivePath,
    const MixEntry& entry,
    const fs::path& outputPath,
    std::string& errorMessage
) {
    std::ifstream input(archivePath, std::ios::binary);
    if (!input) {
        errorMessage = "could not open campaign movie MIX: "
            + pathString(archivePath);
        return false;
    }
    input.seekg(
        static_cast<std::streamoff>(entry.absoluteOffset),
        std::ios::beg
    );
    if (!input) {
        errorMessage = "could not seek to campaign movie MIX entry";
        return false;
    }
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        errorMessage = "could not create campaign movie cache file: "
            + pathString(outputPath);
        return false;
    }

    // Keep the streaming buffer off the relatively small Windows thread
    // stack; retail Bink entries can be hundreds of megabytes.
    std::vector<char> buffer(kCopyBufferBytes);
    std::uint64_t remaining = entry.size;
    while (remaining > 0) {
        const std::size_t requested = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size())
        );
        input.read(buffer.data(), static_cast<std::streamsize>(requested));
        if (input.gcount() != static_cast<std::streamsize>(requested)) {
            errorMessage = "campaign movie MIX entry ended unexpectedly";
            output.close();
            return false;
        }
        output.write(buffer.data(), static_cast<std::streamsize>(requested));
        if (!output) {
            errorMessage = "could not write complete campaign movie cache file";
            output.close();
            return false;
        }
        remaining -= requested;
    }
    output.close();
    if (!output) {
        errorMessage = "could not finalize campaign movie cache file";
        return false;
    }
    return true;
}

} // namespace

std::string normalizeRuntimeCampaignMovieSourceId(
    const std::string& sourceId
) {
    const std::string normalized = lowerAscii(trimAscii(sourceId));
    if (normalized == "ra2" || normalized == "redalert2"
        || normalized == "red alert 2") {
        return "ra2";
    }
    if (normalized == "yr" || normalized == "yuri"
        || normalized == "yurisrevenge"
        || normalized == "yuri's revenge") {
        return "yr";
    }
    return normalized;
}

bool isRuntimeCampaignMovieMixReference(
    const std::string& referencePath
) {
    const std::size_t separator = referencePath.rfind('!');
    if (separator == std::string::npos || separator == 0
        || separator + 1U >= referencePath.size()) {
        return false;
    }
    return lowerAscii(
        fs::path(referencePath.substr(0, separator)).extension().string()
    ) == ".mix"
        && !normalizedMovieEntryName(
            referencePath.substr(separator + 1U)
        ).empty();
}

RuntimeCampaignMovieResolution resolveRuntimeCampaignMovie(
    const RuntimeCampaignMovieResolverOptions& options,
    const std::string& sourceId,
    const std::string& movieId
) {
    const std::string entryName = normalizedMovieEntryName(movieId);
    if (entryName.empty()) {
        return {};
    }
    const std::string normalizedMovieId = fs::path(entryName).stem().string();
    for (const std::string& edition : editionOrder(sourceId)) {
        RuntimeCampaignMovieResolution resolution = resolveForEdition(
            options,
            edition,
            normalizedMovieId,
            entryName
        );
        if (resolution.found) {
            return resolution;
        }
    }
    return {};
}

bool materializeRuntimeCampaignMovie(
    const RuntimeCampaignMovieResolverOptions& options,
    const std::string& referencePath,
    std::string& moviePath,
    std::string& errorMessage
) {
    moviePath.clear();
    errorMessage.clear();
    if (referencePath.empty()) {
        errorMessage = "campaign has no authored Bink movie";
        return false;
    }
    if (!isRuntimeCampaignMovieMixReference(referencePath)) {
        const fs::path loosePath(referencePath);
        if (!regularFile(loosePath)) {
            errorMessage = "campaign movie file was not found: "
                + referencePath;
            return false;
        }
        moviePath = pathString(absoluteNormalized(loosePath));
        return true;
    }

    const std::size_t separator = referencePath.rfind('!');
    const fs::path archivePath(referencePath.substr(0, separator));
    const std::string entryName = normalizedMovieEntryName(
        referencePath.substr(separator + 1U)
    );
    MixArchive archive;
    if (!archive.loadFromPath(archivePath) || !archive.isIndexReadable()) {
        errorMessage = "campaign movie MIX could not be read: "
            + archive.getErrorMessage();
        return false;
    }
    const MixEntry* entry = archive.findEntryByName(entryName);
    if (entry == nullptr || entry->size == 0
        || entry->size > kMaximumCampaignMovieBytes) {
        errorMessage = "campaign movie MIX entry was not found or is invalid: "
            + entryName;
        return false;
    }

    fs::path cacheRoot;
    if (!options.localDataRootPath.empty()) {
        cacheRoot = fs::path(options.localDataRootPath);
    } else {
        std::error_code tempError;
        cacheRoot = fs::temp_directory_path(tempError);
        if (tempError) {
            errorMessage = "campaign movie cache root is unavailable: "
                + tempError.message();
            return false;
        }
        cacheRoot /= "ConcordRTS";
    }
    cacheRoot /= "runtime_campaign_cache";
    cacheRoot /= "movies";
    cacheRoot /= lowerAscii(archivePath.stem().string())
        + "_" + cacheIdentity(archivePath, *entry);
    std::error_code directoryError;
    fs::create_directories(cacheRoot, directoryError);
    if (directoryError) {
        errorMessage = "campaign movie cache directory could not be created: "
            + directoryError.message();
        return false;
    }

    const fs::path candidate = cacheRoot / lowerAscii(entryName);
    if (fileHasSize(candidate, entry->size)) {
        moviePath = pathString(absoluteNormalized(candidate));
        return true;
    }
    if (!removeSingleFile(candidate, errorMessage)) {
        return false;
    }
    fs::path temporary = candidate;
    temporary += ".tmp";
    if (!removeSingleFile(temporary, errorMessage)) {
        return false;
    }
    if (!streamMixEntry(archivePath, *entry, temporary, errorMessage)) {
        std::string cleanupError;
        removeSingleFile(temporary, cleanupError);
        return false;
    }
    if (!fileHasSize(temporary, entry->size)) {
        errorMessage = "campaign movie cache size verification failed";
        std::string cleanupError;
        removeSingleFile(temporary, cleanupError);
        return false;
    }
    std::error_code renameError;
    fs::rename(temporary, candidate, renameError);
    if (renameError) {
        if (!fileHasSize(candidate, entry->size)) {
            errorMessage = "campaign movie cache could not be committed: "
                + renameError.message();
            std::string cleanupError;
            removeSingleFile(temporary, cleanupError);
            return false;
        }
        std::string cleanupError;
        removeSingleFile(temporary, cleanupError);
    }
    if (!fileHasSize(candidate, entry->size)) {
        errorMessage = "campaign movie cache verification failed";
        return false;
    }
    moviePath = pathString(absoluteNormalized(candidate));
    return true;
}
