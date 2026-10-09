#include "GameResourceMount.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <system_error>

namespace {

std::string toLowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

std::string normalizeExtension(const std::filesystem::path& path) {
    std::string extension = toLowerAscii(path.extension().string());
    if (extension.empty()) {
        return "(none)";
    }
    return extension;
}

bool isMapExtension(const std::string& extension) {
    return extension == ".map"
        || extension == ".mpr"
        || extension == ".yrm"
        || extension == ".mmx"
        || extension == ".yro";
}

bool isTheaterIniName(const std::string& name) {
    return name == "temperat.ini"
        || name == "temperatmd.ini"
        || name == "snow.ini"
        || name == "snowmd.ini"
        || name == "urban.ini"
        || name == "urbanmd.ini"
        || name == "desert.ini"
        || name == "desertmd.ini"
        || name == "lunar.ini"
        || name == "lunarmd.ini"
        || name == "newurban.ini"
        || name == "urbann.ini";
}

void addTopExtensions(
    ResourceMountLayerReport& layer,
    const std::map<std::string, int>& extensionCounts
) {
    std::vector<ResourceExtensionCount> counts;
    counts.reserve(extensionCounts.size());

    for (const auto& [extension, count] : extensionCounts) {
        counts.push_back({extension, count});
    }

    std::sort(
        counts.begin(),
        counts.end(),
        [](const ResourceExtensionCount& left, const ResourceExtensionCount& right) {
            if (left.count != right.count) {
                return left.count > right.count;
            }
            return left.extension < right.extension;
        }
    );

    if (counts.size() > 8) {
        counts.resize(8);
    }

    layer.topExtensions = counts;
}

void addSampleFile(
    ResourceMountLayerReport& layer,
    const std::filesystem::path& root,
    const std::filesystem::path& filePath
) {
    if (layer.sampleFiles.size() >= 12) {
        return;
    }

    std::error_code errorCode;
    const std::filesystem::path relative =
        std::filesystem::relative(filePath, root, errorCode);
    layer.sampleFiles.push_back(
        errorCode ? filePath.string() : relative.generic_string()
    );
}

void classifyFile(
    ResourceMountLayerReport& layer,
    const std::filesystem::path& root,
    const std::filesystem::directory_entry& entry,
    std::map<std::string, int>& extensionCounts
) {
    std::error_code errorCode;
    const std::filesystem::path path = entry.path();
    const std::string extension = normalizeExtension(path);
    const std::string filename = toLowerAscii(path.filename().string());

    ++layer.fileCount;
    const std::uintmax_t fileSize = entry.file_size(errorCode);
    if (!errorCode) {
        layer.totalBytes += fileSize;
    }
    errorCode.clear();
    ++extensionCounts[extension];

    if (extension == ".mix") {
        ++layer.mixCount;
        layer.hasMix = true;
        addSampleFile(layer, root, path);
    } else if (extension == ".ini") {
        ++layer.iniCount;
        addSampleFile(layer, root, path);
    } else if (isMapExtension(extension)) {
        ++layer.mapCount;
        layer.hasMaps = true;
        addSampleFile(layer, root, path);
    } else if (extension == ".pal") {
        ++layer.palCount;
        layer.hasPalette = true;
        addSampleFile(layer, root, path);
    } else if (extension == ".shp") {
        ++layer.shpCount;
    } else if (extension == ".tmp" || extension == ".tem" || extension == ".urb") {
        ++layer.tmpCount;
    } else if (extension == ".vxl") {
        ++layer.vxlCount;
    } else if (extension == ".hva") {
        ++layer.hvaCount;
    }

    if (filename == "rules.ini" || filename == "rulesmd.ini") {
        layer.hasRules = true;
        addSampleFile(layer, root, path);
    } else if (filename == "art.ini" || filename == "artmd.ini") {
        layer.hasArt = true;
        addSampleFile(layer, root, path);
    } else if (isTheaterIniName(filename)) {
        layer.hasTheaterIni = true;
        addSampleFile(layer, root, path);
    }
}

ResourceMountLayerReport scanLayer(const ResourceMountRequest& request) {
    ResourceMountLayerReport layer;
    layer.kind = request.kind;
    layer.name = request.name;
    layer.rootPath = request.rootPath;

    std::error_code errorCode;
    const std::filesystem::path root(request.rootPath);
    layer.exists =
        std::filesystem::exists(root, errorCode)
        && std::filesystem::is_directory(root, errorCode);

    if (!layer.exists) {
        layer.warnings.push_back("mount root does not exist: " + request.rootPath);
        return layer;
    }

    std::map<std::string, int> extensionCounts;

    if (request.recursive) {
        std::filesystem::recursive_directory_iterator iterator(
            root,
            std::filesystem::directory_options::skip_permission_denied,
            errorCode
        );
        const std::filesystem::recursive_directory_iterator end;

        while (!errorCode && iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;

            if (entry.is_directory(errorCode)) {
                ++layer.directoryCount;
            } else if (entry.is_regular_file(errorCode)) {
                classifyFile(layer, root, entry, extensionCounts);
            }

            iterator.increment(errorCode);
        }

        if (errorCode) {
            layer.warnings.push_back("stopped scan early: " + errorCode.message());
        }
    } else {
        std::filesystem::directory_iterator iterator(
            root,
            std::filesystem::directory_options::skip_permission_denied,
            errorCode
        );
        const std::filesystem::directory_iterator end;

        while (!errorCode && iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;

            if (entry.is_directory(errorCode)) {
                ++layer.directoryCount;
            } else if (entry.is_regular_file(errorCode)) {
                classifyFile(layer, root, entry, extensionCounts);
            }

            iterator.increment(errorCode);
        }

        if (errorCode) {
            layer.warnings.push_back("stopped scan early: " + errorCode.message());
        }
    }

    addTopExtensions(layer, extensionCounts);
    layer.indexed = true;
    return layer;
}

void accumulateLayer(
    ResourceMountReport& report,
    const ResourceMountLayerReport& layer
) {
    ++report.mountedLayerCount;

    if (layer.exists) {
        ++report.existingLayerCount;
    }

    report.totalFiles += layer.fileCount;
    report.totalDirectories += layer.directoryCount;
    report.totalBytes += layer.totalBytes;
    report.totalMixFiles += layer.mixCount;
    report.totalIniFiles += layer.iniCount;
    report.totalMapFiles += layer.mapCount;
    report.totalPalFiles += layer.palCount;
    report.totalShpFiles += layer.shpCount;
    report.totalTmpFiles += layer.tmpCount;
    report.totalVxlFiles += layer.vxlCount;
    report.totalHvaFiles += layer.hvaCount;
    report.hasRules = report.hasRules || layer.hasRules;
    report.hasArt = report.hasArt || layer.hasArt;
    report.hasMaps = report.hasMaps || layer.hasMaps;
    report.hasTheaterIni = report.hasTheaterIni || layer.hasTheaterIni;
    report.hasPalette = report.hasPalette || layer.hasPalette;
    report.hasMix = report.hasMix || layer.hasMix;

    for (const std::string& warning : layer.warnings) {
        report.warnings.push_back(layer.name + ": " + warning);
    }
}

} // namespace

std::string toString(ResourceMountKind kind) {
    switch (kind) {
    case ResourceMountKind::OriginalGame:
        return "OriginalGame";
    case ResourceMountKind::LocalData:
        return "LocalData";
    case ResourceMountKind::Mod:
        return "Mod";
    }

    return "Unknown";
}

ResourceMountReport buildResourceMountReport(
    const std::vector<ResourceMountRequest>& requests
) {
    ResourceMountReport report;

    for (const ResourceMountRequest& request : requests) {
        ResourceMountLayerReport layer = scanLayer(request);
        accumulateLayer(report, layer);
        report.layers.push_back(layer);
    }

    report.success =
        report.existingLayerCount > 0
        && (report.hasRules || report.hasArt || report.hasMaps || report.hasMix);

    if (!report.hasRules) {
        report.warnings.push_back("no rules.ini/rulesmd.ini found in mounted layers");
    }
    if (!report.hasArt) {
        report.warnings.push_back("no art.ini/artmd.ini found in mounted layers");
    }
    if (!report.hasMaps) {
        report.warnings.push_back(
            "no map/mpr/yrm/mmx/yro files found in mounted layers"
        );
    }
    if (!report.hasPalette) {
        report.warnings.push_back("no pal files found in mounted layers");
    }
    if (!report.hasMix) {
        report.warnings.push_back("no mix files found in mounted layers");
    }

    return report;
}
