#include "IniFile.h"
#include "MixArchive.h"
#include "RuntimeCampaignDataSource.h"
#include "RuntimeIniTextReadBuffer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Runtime campaign data source smoke failed: "
                  << message << "\n";
    }
    return condition;
}

bool sourceHasFileName(
    const RuntimeIniSource& source,
    const std::string& fileName
) {
    const std::string lower = lowerAscii(source.displayPath);
    const std::string expected = lowerAscii(fileName);
    return lower.size() >= expected.size()
        && lower.compare(lower.size() - expected.size(), expected.size(), expected)
            == 0;
}

struct ReadObservation {
    bool success = false;
    std::string text = "previous output must follow the original semantics";
    std::string error = "previous error";
};

ReadObservation observe(const RuntimeIniSource& source, bool reference) {
    RuntimeIniTextReadReferenceScope scope(reference);
    ReadObservation result;
    result.success = source.readText(result.text, &result.error);
    return result;
}

bool sameObservation(const ReadObservation& a, const ReadObservation& b) {
    return a.success == b.success && a.text == b.text && a.error == b.error;
}

std::string binaryPattern(std::size_t bytes) {
    std::string text(bytes, '\0');
    for (std::size_t i = 0; i < bytes; ++i) text[i] = static_cast<char>((i * 17U + 3U) & 255U);
    if (bytes > 3) { text[bytes - 3] = '\r'; text[bytes - 2] = '\n'; text.back() = '\0'; }
    return text;
}

bool writeBinary(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return !!output;
}

// Deliberately supports short xsgetn calls and a get area with no bulk bytes.
// An I/O failure terminates the stream after a prefix, as a file-buffer EOF
// does. Both implementations must preserve the prefix and ignore badbit.
class ShortReadBuffer : public std::streambuf {
public:
    ShortReadBuffer(const std::string& bytes, std::size_t chunk, std::size_t stop, bool bad)
        : bytes_(bytes), chunk_(chunk), stop_(std::min(stop, bytes.size())), bad_(bad) {}
    void attach(std::istream& input) { input_ = &input; }
protected:
    std::streamsize xsgetn(char* destination, std::streamsize requested) override {
        const std::size_t count = std::min({static_cast<std::size_t>(requested), chunk_, stop_ - at_});
        if (count) std::memcpy(destination, bytes_.data() + at_, count);
        at_ += count;
        return static_cast<std::streamsize>(count);
    }
    int_type underflow() override {
        if (at_ >= stop_) {
            if (bad_ && input_) input_->setstate(std::ios::badbit);
            return traits_type::eof();
        }
        return traits_type::to_int_type(bytes_[at_]);
    }
    int_type uflow() override {
        const auto value = underflow();
        if (!traits_type::eq_int_type(value, traits_type::eof())) ++at_;
        return value;
    }
private:
    const std::string& bytes_;
    std::size_t chunk_ = 0, stop_ = 0, at_ = 0;
    bool bad_ = false;
    std::istream* input_ = nullptr;
};

bool bufferedTextContract() {
    namespace fs = std::filesystem;
    constexpr std::size_t limit = 16U * 1024U * 1024U;
    constexpr std::size_t chunk = RuntimeIniTextReadDetail::chunkBytes;
    const auto directory = fs::current_path() / "runtime-ini-read-contract"
        / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code error;
    fs::create_directories(directory, error);
    if (!expect(!error, "could not create isolated text contract directory")) return false;
    std::size_t fileChecks = 0, streamChecks = 0;
    const std::array<std::size_t, 8> sizes{0, 1, chunk - 1, chunk, chunk + 1, 742958, limit, limit + 1};
    for (const auto size : sizes) {
        const auto path = directory / (std::to_string(size) + ".ini");
        const auto bytes = binaryPattern(size);
        if (!expect(writeBinary(path, bytes), "could not write isolated binary fixture")) return false;
        RuntimeIniSource source;
        source.backingPath = source.displayPath = path.generic_string();
        const auto reference = observe(source, true), actual = observe(source, false);
        const bool valid = size > 0 && size <= limit;
        const std::string expectedError = valid ? std::string{}
            : "runtime INI has an invalid size: " + source.backingPath;
        if (!expect(sameObservation(actual, reference) && actual.success == valid
            && actual.text == (valid ? bytes : std::string{}) && actual.error == expectedError,
            "loose binary branch/output/error differs at size " + std::to_string(size))) return false;
        ++fileChecks;
    }
    RuntimeIniSource missing;
    missing.backingPath = missing.displayPath = (directory / "missing.ini").generic_string();
    const auto missingReference = observe(missing, true), missingActual = observe(missing, false);
    if (!expect(sameObservation(missingActual, missingReference) && !missingActual.success
        && missingActual.text.empty()
        && missingActual.error == "runtime INI size could not be read: " + missing.backingPath,
        "missing loose file changed error/output semantics")) return false;
    ++fileChecks;
    RuntimeIniSource absent;
    const auto absentReference = observe(absent, true), absentActual = observe(absent, false);
    if (!expect(sameObservation(absentActual, absentReference) && !absentActual.success
        && absentActual.text.empty() && absentActual.error == "runtime INI source is unavailable",
        "unavailable source changed output/error semantics")) return false;
    ++fileChecks;
    // Deterministic post-stat races: the production reader uses stat only as
    // a precondition/reserve hint, while these streams expose the changed data.
    const std::array<std::pair<std::size_t, std::size_t>, 4> changedSizes{{
        {742958, 17}, {1, chunk + 7}, {1, 0}, {1, limit + 1}}};
    for (const auto [reported, actualSize] : changedSizes) {
        const auto path = directory / ("post-stat-" + std::to_string(reported)
            + "-" + std::to_string(actualSize) + ".ini");
        if (!expect(writeBinary(path, binaryPattern(reported)), "could not write pre-stat race fixture")) return false;
        std::error_code statError;
        const auto cachedSize = fs::file_size(path, statError);
        if (!expect(!statError && cachedSize == reported, "post-stat fixture size was not captured")) return false;
        const auto bytes = binaryPattern(actualSize);
        if (!expect(writeBinary(path, bytes), "could not change file after captured stat")) return false;
        std::ifstream original(path, std::ios::binary);
        std::ifstream buffered(path, std::ios::binary);
        if (!expect(!!original && !!buffered, "post-stat changed file did not open")) return false;
        std::string expected = "old", actual = "old";
        actual.reserve(static_cast<std::size_t>(cachedSize));
        RuntimeIniTextReadDetail::readToEnd(original, expected, true);
        RuntimeIniTextReadDetail::readToEnd(buffered, actual, false);
        if (!expect(actual == expected && actual == bytes,
            "post-stat growth/shrink read was truncated or left prior bytes")) return false;
        // This is the original provider's unchanged final empty/size decision.
        if (!expect((!actual.empty() && actual.size() <= limit) == (actualSize > 0 && actualSize <= limit)
                && (!actual.empty() && actual.size() <= limit)
                == (!expected.empty() && expected.size() <= limit),
            "post-stat final size/partial failure semantics differ")) return false;
        ++streamChecks;
    }
    const auto streamBytes = binaryPattern(chunk + 17);
    for (const std::size_t shortChunk : {std::size_t{0}, std::size_t{3}, chunk}) {
        for (const bool bad : {false, true}) {
            const std::size_t stop = shortChunk == 0 ? 29 : bad ? chunk + 3 : streamBytes.size();
            ShortReadBuffer a(streamBytes, shortChunk, stop, bad);
            ShortReadBuffer b(streamBytes, shortChunk, stop, bad);
            std::istream original(&a), buffered(&b);
            a.attach(original); b.attach(buffered);
            std::string expected = "old", actual = "old";
            RuntimeIniTextReadDetail::readToEnd(original, expected, true);
            RuntimeIniTextReadDetail::readToEnd(buffered, actual, false);
            if (!expect(actual == expected && actual == streamBytes.substr(0, stop)
                    && original.bad() == buffered.bad(),
                "short-read/badbit prefix changed iterator semantics")) return false;
            ++streamChecks;
        }
    }
    // A real MIX entry remains on the unchanged archive reader, including its
    // existing trailing-NUL trimming behavior (loose files retain those NULs).
    const auto mixPath = directory / "unchanged.mix";
    auto mixPayload = binaryPattern(29);
    mixPayload.append(2, '\0');
    std::ofstream mix(mixPath, std::ios::binary);
    const auto write16 = [&](std::uint16_t value) {
        mix.put(static_cast<char>(value)); mix.put(static_cast<char>(value >> 8));
    };
    const auto write32 = [&](std::uint32_t value) {
        for (int i = 0; i < 4; ++i) mix.put(static_cast<char>(value >> (i * 8)));
    };
    write16(1); write32(static_cast<std::uint32_t>(mixPayload.size()));
    write32(mixFileIdFromName("rules.ini")); write32(0); write32(static_cast<std::uint32_t>(mixPayload.size()));
    mix.write(mixPayload.data(), static_cast<std::streamsize>(mixPayload.size())); mix.close();
    if (!expect(!!mix, "could not write isolated MIX fixture")) return false;
    RuntimeIniSource mixSource;
    mixSource.backingPath = mixPath.generic_string(); mixSource.entryName = "rules.ini";
    mixSource.displayPath = mixSource.backingPath + "!rules.ini"; mixSource.mixEntry = true;
    const auto mixReference = observe(mixSource, true), mixActual = observe(mixSource, false);
    while (!mixPayload.empty() && mixPayload.back() == '\0') mixPayload.pop_back();
    if (!expect(sameObservation(mixActual, mixReference) && mixActual.success
            && mixActual.text == mixPayload && mixActual.error.empty(),
        "MIX output/trailing-NUL semantics changed")) return false;
    ++fileChecks;
    std::cout << "Runtime INI bulk-read contract: loose/MIXChecks=" << fileChecks
        << " changedSize/shortReadChecks=" << streamChecks
        << " exactBytes=yes exactErrors=yes partialBadbitPreserved=yes referenceInProcess=yes"
        << " fixtures=" << directory.generic_string() << '\n';
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: RuntimeCampaignDataSourceSmoke "
                     "<resource-root> <local-data-root>\n";
        return 2;
    }

    RuntimeCampaignDataSourceOptions baseOptions;
    baseOptions.resourceRootPath = argv[1];
    baseOptions.localDataRootPath = argv[2];
    baseOptions.sourceId = "ra2";
    RuntimeCampaignDataSourceOptions expansionOptions = baseOptions;
    expansionOptions.sourceId = "yr";

    const std::array<RuntimeCampaignIniKind, 5> kinds{
        RuntimeCampaignIniKind::Rules,
        RuntimeCampaignIniKind::Art,
        RuntimeCampaignIniKind::Ai,
        RuntimeCampaignIniKind::Sound,
        RuntimeCampaignIniKind::Eva
    };
    const std::array<const char*, 5> baseNames{
        "rules.ini", "art.ini", "ai.ini", "sound.ini", "eva.ini"
    };
    const std::array<const char*, 5> expansionNames{
        "rulesmd.ini", "artmd.ini", "aimd.ini", "soundmd.ini", "evamd.ini"
    };

    bool success = true;
    success &= bufferedTextContract();
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const RuntimeIniSource base = resolveRuntimeCampaignIniSource(
            baseOptions,
            kinds[index]
        );
        const RuntimeIniSource expansion = resolveRuntimeCampaignIniSource(
            expansionOptions,
            kinds[index]
        );
        IniFile baseIni;
        IniFile expansionIni;
        std::string baseError;
        std::string expansionError;
        success &= expect(
            base.available()
                && base.sourceId == "ra2"
                && sourceHasFileName(base, baseNames[index])
                && base.load(baseIni, &baseError),
            std::string("base ") + baseNames[index]
                + " did not resolve/load from the RA2 source: " + baseError
        );
        success &= expect(
            expansion.available()
                && expansion.sourceId == "yr"
                && sourceHasFileName(expansion, expansionNames[index])
                && expansion.load(expansionIni, &expansionError),
            std::string("expansion ") + expansionNames[index]
                + " did not resolve/load from the YR source: "
                + expansionError
        );
        success &= expect(
            lowerAscii(base.displayPath) != lowerAscii(expansion.displayPath),
            std::string(baseNames[index]) + " and " + expansionNames[index]
                + " crossed edition boundaries"
        );
    }

    const RuntimeIniSource baseTheater =
        resolveRuntimeCampaignTheaterSource(baseOptions, "TEMPERATE");
    const RuntimeIniSource expansionTheater =
        resolveRuntimeCampaignTheaterSource(expansionOptions, "TEMPERATE");
    IniFile baseTheaterIni;
    IniFile expansionTheaterIni;
    success &= expect(
        baseTheater.available()
            && baseTheater.sourceId == "ra2"
            && sourceHasFileName(baseTheater, "temperat.ini")
            && baseTheater.load(baseTheaterIni),
        "base TEMPERATE theater did not use temperat.ini"
    );
    success &= expect(
        expansionTheater.available()
            && expansionTheater.sourceId == "yr"
            && sourceHasFileName(expansionTheater, "temperatmd.ini")
            && expansionTheater.load(expansionTheaterIni),
        "YR TEMPERATE theater did not use temperatmd.ini"
    );

    RuntimeCampaignDataSourceOptions legacyOptions = baseOptions;
    legacyOptions.sourceId.clear();
    const RuntimeIniSource legacyRules = resolveRuntimeCampaignIniSource(
        legacyOptions,
        RuntimeCampaignIniKind::Rules
    );
    success &= expect(
        legacyRules.sourceId == "yr"
            && sourceHasFileName(legacyRules, "rulesmd.ini"),
        "empty sourceId no longer preserves the legacy YR-first behavior"
    );

    RuntimeCampaignDataSourceOptions invalidOptions = baseOptions;
    invalidOptions.sourceId = "unknown-edition";
    success &= expect(
        !resolveRuntimeCampaignIniSource(
            invalidOptions,
            RuntimeCampaignIniKind::Rules
        ).available(),
        "an unknown campaign source silently fell back across editions"
    );

    if (!success) {
        return 1;
    }
    std::cout << "Runtime campaign data source smoke: "
                 "ra2=rules/art/ai/sound/eva/theater "
                 "yr=rulesmd/artmd/aimd/soundmd/evamd/theatermd "
                 "strict=yes legacyYrFirst=yes\n";
    return 0;
}
