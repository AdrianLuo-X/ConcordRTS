#pragma once

#include <string>
#include <vector>

struct AssetField {
    std::string name;
    std::string value;
};

struct AssetInspectionReport {
    std::string path;
    std::string extension;
    std::string kind;
    unsigned long long fileSize = 0;
    bool success = false;
    std::string errorMessage;
    std::vector<AssetField> fields;
    std::vector<std::string> warnings;
};

AssetInspectionReport inspectAssetFile(const std::string& path);

AssetInspectionReport inspectVxlHvaPair(
    const std::string& vxlPath,
    const std::string& hvaPath
);

bool exportShpFirstFrameToPpm(
    const std::string& shpPath,
    const std::string& palettePath,
    const std::string& outputPath,
    std::string& errorMessage
);

bool exportTmpFirstTileToPpm(
    const std::string& tmpPath,
    const std::string& palettePath,
    const std::string& outputPath,
    std::string& errorMessage
);
