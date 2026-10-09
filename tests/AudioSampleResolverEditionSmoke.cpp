#include "AudioSampleResolver.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

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

std::string sourceFileName(const AudioSampleResolveResult& result) {
    return lowerAscii(
        std::filesystem::path(result.sourcePath).filename().string()
    );
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "audio edition resolver smoke failure: "
                  << message << '\n';
    }
    return condition;
}

std::set<std::string> sampleNames(
    const std::vector<AudioIndexedSample>& samples
) {
    std::set<std::string> names;
    for (const AudioIndexedSample& sample : samples) {
        names.insert(sample.name);
    }
    return names;
}

bool everySampleUsesArchive(
    const std::vector<AudioIndexedSample>& samples,
    const std::string& expectedArchive
) {
    return std::all_of(
        samples.begin(),
        samples.end(),
        [&](const AudioIndexedSample& sample) {
            return lowerAscii(
                std::filesystem::path(sample.sourceMixPath)
                    .filename()
                    .string()
            ) == expectedArchive;
        }
    );
}

std::uint64_t fileFingerprint(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return 0U;
    std::uint64_t hash = 1469598103934665603ULL;
    char character = 0;
    std::size_t bytes = 0U;
    while (input.get(character)) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
        ++bytes;
    }
    return bytes == 0U ? 0U : hash;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr
            << "usage: concord_audio_sample_resolver_edition_smoke "
            << "<mix-export-root> <cache-root>\n";
        return 2;
    }

    const std::string assetRoot = argv[1];
    const std::filesystem::path cacheRoot(argv[2]);
    const std::vector<AudioIndexedSample> ra2Samples =
        collectIndexedAudioSamples(assetRoot, " RA2 ");
    const std::vector<AudioIndexedSample> yrSamples =
        collectIndexedAudioSamples(assetRoot, "YR");

    bool ok = true;
    ok &= expect(!ra2Samples.empty(), "RA2 audio.mix index was not loaded");
    ok &= expect(!yrSamples.empty(), "YR audiomd.mix index was not loaded");
    ok &= expect(
        everySampleUsesArchive(ra2Samples, "audio.mix"),
        "RA2 selection admitted a non-RA2 audio archive"
    );
    ok &= expect(
        everySampleUsesArchive(yrSamples, "audiomd.mix"),
        "YR selection admitted a non-YR audio archive"
    );
    if (!ok) {
        return 1;
    }

    const std::set<std::string> ra2Names = sampleNames(ra2Samples);
    const std::set<std::string> yrNames = sampleNames(yrSamples);
    std::vector<std::string> overlap;
    std::set_intersection(
        ra2Names.begin(),
        ra2Names.end(),
        yrNames.begin(),
        yrNames.end(),
        std::back_inserter(overlap)
    );
    ok &= expect(
        !overlap.empty(),
        "stock RA2/YR audio indexes had no overlapping sample to probe"
    );

    AudioSampleResolveResult ra2Resolved;
    AudioSampleResolveResult yrResolved;
    AudioSampleResolveResult ra2ResolvedAgain;
    std::uint64_t ra2PayloadFingerprint = 0U;
    std::uint64_t yrPayloadFingerprint = 0U;
    std::uint64_t ra2PayloadFingerprintAgain = 0U;
    std::string resolvedOverlap;
    const std::filesystem::path sharedRuntimeCache =
        cacheRoot / "runtime_audio_cache";
    for (const std::string& sample : overlap) {
        ra2Resolved = resolveAudioSampleToWave(
            assetRoot,
            sharedRuntimeCache.string(),
            sample,
            "ra2"
        );
        yrResolved = resolveAudioSampleToWave(
            assetRoot,
            sharedRuntimeCache.string(),
            sample,
            "yr"
        );
        ra2ResolvedAgain = resolveAudioSampleToWave(
            assetRoot,
            sharedRuntimeCache.string(),
            sample,
            "ra2"
        );
        if (ra2Resolved.success && yrResolved.success
            && ra2ResolvedAgain.success
            && sourceFileName(ra2Resolved) == "audio.mix"
            && sourceFileName(yrResolved) == "audiomd.mix"
            && sourceFileName(ra2ResolvedAgain) == "audio.mix"
            && lowerAscii(ra2Resolved.wavPath)
                != lowerAscii(yrResolved.wavPath)
            && lowerAscii(ra2Resolved.wavPath)
                == lowerAscii(ra2ResolvedAgain.wavPath)) {
            ra2PayloadFingerprint = fileFingerprint(ra2Resolved.wavPath);
            yrPayloadFingerprint = fileFingerprint(yrResolved.wavPath);
            ra2PayloadFingerprintAgain = fileFingerprint(
                ra2ResolvedAgain.wavPath
            );
        }
        if (ra2PayloadFingerprint != 0U
            && yrPayloadFingerprint != 0U
            && ra2PayloadFingerprint != yrPayloadFingerprint
            && ra2PayloadFingerprintAgain == ra2PayloadFingerprint) {
            resolvedOverlap = sample;
            break;
        }
    }

    ok &= expect(
        !resolvedOverlap.empty(),
        "no overlapping stock sample produced distinct RA2/YR payloads"
    );
    if (!resolvedOverlap.empty()) {
        ok &= expect(
            sourceFileName(ra2Resolved) == "audio.mix",
            "RA2 overlapping sample resolved outside audio.mix"
        );
        ok &= expect(
            sourceFileName(yrResolved) == "audiomd.mix",
            "YR overlapping sample resolved outside audiomd.mix"
        );
        ok &= expect(
            lowerAscii(ra2Resolved.sourcePath)
                != lowerAscii(yrResolved.sourcePath),
            "RA2 and YR overlapping samples resolved to the same source"
        );
        ok &= expect(
            lowerAscii(ra2Resolved.wavPath)
                    != lowerAscii(yrResolved.wavPath)
                && lowerAscii(ra2Resolved.wavPath)
                    == lowerAscii(ra2ResolvedAgain.wavPath)
                && ra2ResolvedAgain.fromCache,
            "RA2 -> YR -> RA2 did not isolate/reuse the shared runtime cache"
        );
        ok &= expect(
            ra2PayloadFingerprint != 0U
                && yrPayloadFingerprint != 0U
                && ra2PayloadFingerprint != yrPayloadFingerprint
                && ra2PayloadFingerprintAgain == ra2PayloadFingerprint,
            "RA2 -> YR -> RA2 shared-cache payload fingerprints crossed"
        );

        const AudioSampleResolveResult legacyResolved =
            resolveAudioSampleToWave(
                assetRoot,
                (cacheRoot / "legacy").string(),
                resolvedOverlap
            );
        ok &= expect(
            legacyResolved.success
                && sourceFileName(legacyResolved) == "audiomd.mix",
            "empty edition selector no longer preserves YR-first behavior"
        );

        const AudioSampleResolveResult unsupportedResolved =
            resolveAudioSampleToWave(
                assetRoot,
                (cacheRoot / "unsupported").string(),
                resolvedOverlap,
                "ts"
            );
        ok &= expect(
            !unsupportedResolved.success,
            "unsupported edition selector fell through to another edition"
        );
    }

    if (!ok) {
        return 1;
    }

    std::cout << "audio edition resolver smoke passed: overlap="
              << overlap.size()
              << " sample=" << resolvedOverlap
              << " ra2=" << ra2Resolved.sourcePath
              << " yr=" << yrResolved.sourcePath
              << " sharedCache=" << sharedRuntimeCache.string()
              << " ra2Hash=0x" << std::hex << ra2PayloadFingerprint
              << " yrHash=0x" << yrPayloadFingerprint << std::dec
              << " ra2Reload=cache"
              << '\n';
    return 0;
}
