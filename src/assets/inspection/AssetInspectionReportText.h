#pragma once

#include "assets/inspection/AssetInspector.h"

#include <cstddef>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace concord::assets::inspection {

static inline std::string joinStrings(
    const std::vector<std::string>& values,
    const std::string& separator = ","
) {
    if (values.empty()) {
        return "(none)";
    }

    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            output << separator;
        }
        output << values[i];
    }
    return output.str();
}

static inline std::string getInspectionField(
    const AssetInspectionReport& report,
    const std::string& name
) {
    for (const AssetField& field : report.fields) {
        if (field.name == name) {
            return field.value;
        }
    }
    return "";
}

static inline std::string summarizeInspection(const AssetInspectionReport& report) {
    if (!report.success) {
        return "decode=fail " + report.errorMessage;
    }

    std::ostringstream output;
    output << "decode=ok kind=" << report.kind << " bytes=" << report.fileSize;

    if (report.extension == ".shp") {
        output << " frameCount=" << getInspectionField(report, "frameCount")
               << " visibleMainFrames=" << getInspectionField(report, "visibleMainFrames")
               << " globalSize="
               << getInspectionField(report, "width")
               << "x" << getInspectionField(report, "height")
               << " firstOffset="
               << getInspectionField(report, "firstFrameOffsetX")
               << "," << getInspectionField(report, "firstFrameOffsetY")
               << " firstSize="
               << getInspectionField(report, "firstFrameWidth")
               << "x" << getInspectionField(report, "firstFrameHeight")
               << " firstFormat=" << getInspectionField(report, "firstFrameFormat")
               << " format0Frames=" << getInspectionField(report, "format0Frames")
               << " format0Raw="
               << getInspectionField(report, "format0RawDecodeStatus")
               << " rawValidated="
               << getInspectionField(report, "format0RawValidatedFrames")
               << " unsupportedFormatFrames=" << getInspectionField(report, "unsupportedFormatFrames")
               << " decoderSupport=" << getInspectionField(report, "decoderSupport");
    } else if (report.extension == ".vxl") {
        output << " sections=" << getInspectionField(report, "sectionNames");
    } else if (report.extension == ".hva") {
        output << " frames=" << getInspectionField(report, "frameCount")
               << " sections=" << getInspectionField(report, "sectionNames");
    } else if (report.extension == ".pal") {
        output << " colors=" << getInspectionField(report, "colorCount");
    } else if (report.extension == ".pcx") {
        output << " size="
               << getInspectionField(report, "width")
               << "x" << getInspectionField(report, "height")
               << " bpp=" << getInspectionField(report, "bitsPerPixel")
               << " planes=" << getInspectionField(report, "colorPlanes")
               << " bytesPerLine=" << getInspectionField(report, "bytesPerLine")
               << " vgaPalette=" << getInspectionField(report, "hasTrailingVgaPalette")
               << " rle=" << getInspectionField(report, "pcxRleDecodeStatus")
               << " decoderSupport=" << getInspectionField(report, "decoderSupport");
    } else if (
        report.extension == ".tmp"
        || report.extension == ".tem"
        || report.extension == ".sno"
        || report.extension == ".urb"
        || report.extension == ".des"
        || report.extension == ".lun"
        || report.extension == ".ubn"
    ) {
        output << " layout=" << getInspectionField(report, "layout")
               << " slots=" << getInspectionField(report, "slotCount")
               << " present=" << getInspectionField(report, "presentTileCount");
    }

    if (!report.warnings.empty()) {
        output << " warnings=" << joinStrings(report.warnings, " | ");
    }

    return output.str();
}

struct CapabilityProbeReportInput {
    bool found;
    bool unsupported;
    const std::string& label;
    const std::string& candidateSummary;
    const std::string& source;
    const std::string& decodeSummary;
    const std::string& paletteStatus;
};

struct CapabilityProbeReportState {
    std::set<std::string> summaryProblemKeys;
    unsigned int presentDecodableCount = 0;
    unsigned int presentUnsupportedCount = 0;
    unsigned int missingCount = 0;
    unsigned int suppressedDetailCount = 0;
    unsigned int suppressedDuplicateProblemCount = 0;
};

static inline void appendCapabilityProbeLine(
    std::vector<std::string>& warnings,
    CapabilityProbeReportState& state,
    const CapabilityProbeReportInput& probe,
    bool verbose,
    bool countForCoreCoverage
) {
    const std::string category = !probe.found
        ? "[missing]"
        : (probe.unsupported ? "[exists-unsupported]" : "[exists-decodable]");

    if (countForCoreCoverage) {
        if (!probe.found) {
            ++state.missingCount;
        } else if (probe.unsupported) {
            ++state.presentUnsupportedCount;
        } else {
            ++state.presentDecodableCount;
        }
    }

    if (!verbose && probe.found && !probe.unsupported) {
        ++state.suppressedDetailCount;
        return;
    }

    if (!verbose) {
        const std::string problemKey =
            category
            + "|"
            + (probe.source.empty() ? probe.candidateSummary : probe.source)
            + "|"
            + probe.decodeSummary
            + "|"
            + probe.paletteStatus;
        if (!state.summaryProblemKeys.insert(problemKey).second) {
            ++state.suppressedDuplicateProblemCount;
            return;
        }
    }

    warnings.push_back(
        category
        + " "
        + probe.label
        + " noReexportNeeded="
        + (probe.found || !countForCoreCoverage ? "yes" : "no")
        + " candidates="
        + probe.candidateSummary
        + " source="
        + (probe.source.empty() ? "missing" : probe.source)
        + " "
        + probe.decodeSummary
        + " palette="
        + (probe.paletteStatus.empty() ? "not-applicable" : probe.paletteStatus)
        + " coreResource="
        + (countForCoreCoverage ? "yes" : "no")
    );
}

static inline std::string yesNo(bool value) {
    return value ? "yes" : "no";
}

static inline void addField(
    AssetInspectionReport& report,
    const std::string& name,
    const std::string& value
) {
    report.fields.push_back({name, value});
}

static inline void addField(
    AssetInspectionReport& report,
    const std::string& name,
    unsigned long long value
) {
    addField(report, name, std::to_string(value));
}

struct CapabilityReportInputs {
    const std::string& rulesPath;
    const std::string& artPath;
    const std::string& assetRootPath;
    const std::string& mapPath;
    const std::string& theaterIniPath;
    const std::string& tileRootPath;
    const std::string& reportMode;
    const std::string& theater;
    const std::vector<std::string>& objectIds;
    bool includeUi;
};

struct CapabilityIndexReport {
    std::size_t looseResourceNames;
    std::size_t looseResourceCount;
    std::size_t mixArchives;
    std::size_t mixArchivePaths;
    std::size_t mixEntryRefCount;
    std::size_t mixEntryHashBuckets;
    bool processCacheHit;
    std::size_t processCacheEntries;
    const std::string& cacheKey;
    const std::string& cacheStatus;
    bool verbose;
    const std::vector<std::string>& warnings;
};

struct CapabilityReportCounts {
    unsigned int requiredResourceCount;
    unsigned int logicalCount;
    unsigned int resourceIndexHits;
    unsigned int resourceIndexMisses;
    std::size_t resolutionCacheEntries;
    unsigned int resolutionCacheHits;
    unsigned int resolutionCacheMisses;
    std::size_t decodeCacheEntries;
    unsigned int decodeCacheHits;
    unsigned int decodeCacheMisses;
    bool processIndexCacheHit;
};

static inline void appendCapabilityReportInputs(AssetInspectionReport& report, const CapabilityReportInputs& options) {
    addField(report, "rulesPath", options.rulesPath.empty() ? "(missing)" : options.rulesPath);
    addField(report, "artPath", options.artPath.empty() ? "(missing)" : options.artPath);
    addField(report, "assetRootPath", options.assetRootPath.empty() ? "(missing)" : options.assetRootPath);
    addField(report, "mapPath", options.mapPath.empty() ? "(not requested)" : options.mapPath);
    addField(report, "theaterIniPath", options.theaterIniPath.empty() ? "(not requested)" : options.theaterIniPath);
    addField(report, "tileRootPath", options.tileRootPath.empty() ? "(not requested)" : options.tileRootPath);
    addField(report, "reportMode", options.reportMode);
    addField(report, "theater", options.theater);
    addField(report, "objects", joinStrings(options.objectIds));
    addField(report, "includeUi", yesNo(options.includeUi));
}

static inline void appendCapabilityIndexReport(AssetInspectionReport& report, const CapabilityIndexReport& index) {
    addField(report, "looseResourceNames", static_cast<unsigned long long>(index.looseResourceNames));
    addField(report, "resourceIndexLooseResources", static_cast<unsigned long long>(index.looseResourceCount));
    addField(report, "mixArchives", static_cast<unsigned long long>(index.mixArchives));
    addField(report, "resourceIndexMixPathEntries", static_cast<unsigned long long>(index.mixArchivePaths));
    addField(report, "resourceIndexMixEntryRefs", static_cast<unsigned long long>(index.mixEntryRefCount));
    addField(report, "resourceIndexMixHashBuckets", static_cast<unsigned long long>(index.mixEntryHashBuckets));
    addField(report, "resourceIndexProcessCacheHit", yesNo(index.processCacheHit));
    addField(report, "resourceIndexProcessCacheEntries", static_cast<unsigned long long>(index.processCacheEntries));
    addField(report, "resourceIndexCacheKey", index.cacheKey);
    addField(report, "resourceIndexCacheStatus", index.cacheStatus);
    addField(
        report,
        "performanceColdPath",
        "assetRoot recursive scan and MIX directory indexing happen once per CLI process; decode/resolution caches are per capability report; no persistent cache is written"
    );
    addField(
        report,
        "summaryDecodePolicy",
        index.verbose
            ? "verbose reports every probed resource; unique source bytes still decode once"
            : "summary suppresses decodable detail; unique source bytes decode once only when needed for coverage or unsupported classification"
    );
    for (const std::string& warning : index.warnings) {
        report.warnings.push_back("[index-warning] " + warning);
    }
}

static inline void appendCapabilityReportCoverage(
    AssetInspectionReport& report,
    const CapabilityProbeReportState& probe,
    const CapabilityReportCounts& counts,
    const std::string& mode
) {
    addField(report, "requiredResources", counts.requiredResourceCount);
    addField(report, "resourcesPresentDecodable", probe.presentDecodableCount);
    addField(report, "resourcesPresentUnsupportedOrPalette", probe.presentUnsupportedCount);
    addField(report, "resourcesMissing", probe.missingCount);
    addField(report, "logicalReferences", counts.logicalCount);
    addField(report, "suppressedSummaryDetails", probe.suppressedDetailCount);
    addField(report, "suppressedDuplicateProblems", probe.suppressedDuplicateProblemCount);
    addField(report, "resourceIndexHits", counts.resourceIndexHits);
    addField(report, "resourceIndexMisses", counts.resourceIndexMisses);
    addField(report, "resolutionCacheEntries", static_cast<unsigned long long>(counts.resolutionCacheEntries));
    addField(report, "resolutionCacheHits", counts.resolutionCacheHits);
    addField(report, "resolutionCacheMisses", counts.resolutionCacheMisses);
    addField(report, "decodeCacheEntries", static_cast<unsigned long long>(counts.decodeCacheEntries));
    addField(report, "decodeCacheHits", counts.decodeCacheHits);
    addField(report, "decodeCacheMisses", counts.decodeCacheMisses);
    const unsigned int covered =
        probe.presentDecodableCount + probe.presentUnsupportedCount;
    const unsigned int percent =
        counts.requiredResourceCount == 0
            ? 100U
            : (covered * 100U) / counts.requiredResourceCount;
    addField(report, "capabilityResourceCoverage", std::to_string(covered)
        + "/"
        + std::to_string(counts.requiredResourceCount)
        + " ("
        + std::to_string(percent)
        + "% present, including decoder/palette gaps)"
    );
    addField(
        report,
        "mixReexportNeeded",
        probe.missingCount == 0
            ? "no for inspected resources"
            : "only for resources listed under [missing], after checking optional/logical cases"
    );
    report.warnings.push_back(
        "[capability-summary] mode="
        + mode
        + " resources="
        + std::to_string(covered)
        + "/"
        + std::to_string(counts.requiredResourceCount)
        + " unsupported="
        + std::to_string(probe.presentUnsupportedCount)
        + " missing="
        + std::to_string(probe.missingCount)
        + " decodeCache="
        + std::to_string(counts.decodeCacheHits)
        + "/"
        + std::to_string(counts.decodeCacheHits + counts.decodeCacheMisses)
        + " decodeCacheEntries="
        + std::to_string(counts.decodeCacheEntries)
        + " resolutionCache="
        + std::to_string(counts.resolutionCacheHits)
        + "/"
        + std::to_string(counts.resolutionCacheHits + counts.resolutionCacheMisses)
        + " resolutionCacheEntries="
        + std::to_string(counts.resolutionCacheEntries)
        + " indexHits="
        + std::to_string(counts.resourceIndexHits)
        + " processIndexCache="
        + (counts.processIndexCacheHit ? "hit" : "miss")
        + " suppressedDuplicateProblems="
        + std::to_string(probe.suppressedDuplicateProblemCount)
        + " noReexportNeeded="
        + (probe.missingCount == 0 ? "yes" : "check-missing")
    );
}

} // namespace concord::assets::inspection
