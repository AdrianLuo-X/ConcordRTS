#pragma once

#include "data/art/ArtDatabase.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace concord::assets::preview {

struct StructureSpriteReport {
    int offsetX;
    int offsetY;
    int globalWidth;
    int globalHeight;
    int width;
    int height;
    int frameIndex;
    int format;
    unsigned int dataOffset;
};

static inline std::string formatCandidateList(const std::vector<std::string>& candidates) {
    if (candidates.empty()) {
        return "(none)";
    }

    std::string result;
    const std::size_t previewCount = std::min<std::size_t>(candidates.size(), 5);
    for (std::size_t i = 0; i < previewCount; ++i) {
        if (!result.empty()) {
            result += ",";
        }

        result += candidates[i];
    }

    if (candidates.size() > previewCount) {
        result += ",...";
    }

    return result;
}

static inline std::string formatFullCandidateList(const std::vector<std::string>& candidates) {
    if (candidates.empty()) {
        return "(none)";
    }

    std::string result;
    for (const std::string& candidate : candidates) {
        if (!result.empty()) {
            result += ",";
        }

        result += candidate;
    }

    return result;
}

static inline std::string formatAnimationLayer(
    const ArtType::ActiveAnimation& animation,
    const std::string& label,
    std::size_t index
) {
    std::string result =
        label
        + std::to_string(index + 1)
        + "="
        + animation.image;

    if (!animation.damagedImage.empty()) {
        result += "/damaged=" + animation.damagedImage;
    }

    result += "@";
    result += std::to_string(animation.offsetX);
    result += ",";
    result += std::to_string(animation.offsetY);
    result += " ySort=";
    result += std::to_string(animation.ySort);
    result += " zAdjust=";
    result += std::to_string(animation.zAdjust);
    result += animation.powered ? " powered=yes" : " powered=no";
    return result;
}

static inline std::string formatActiveAnimationSummary(const ArtType* artType) {
    if (artType == nullptr
        || (artType->activeAnimations.empty() && artType->specialAnimations.empty())) {
        return "-";
    }

    std::string summary;
    for (std::size_t i = 0; i < artType->activeAnimations.size(); ++i) {
        if (!summary.empty()) {
            summary += "; ";
        }

        summary += formatAnimationLayer(artType->activeAnimations[i], "active", i);
    }

    for (std::size_t i = 0; i < artType->specialAnimations.size(); ++i) {
        if (!summary.empty()) {
            summary += "; ";
        }

        summary += formatAnimationLayer(artType->specialAnimations[i], "special", i);
    }

    return summary;
}

static inline std::string formatExpectedStructureRect(const StructureSpriteReport& image) {
    const int tileHeight = 30;
    const int left = (-image.globalWidth / 2) + image.offsetX;
    const int top = (tileHeight / 2) - image.globalHeight + image.offsetY;
    return "expectedRectRelativeToRearCell="
        + std::to_string(left)
        + ","
        + std::to_string(top)
        + ","
        + std::to_string(image.globalWidth)
        + "x"
        + std::to_string(image.globalHeight)
        + " visibleFrame="
        + std::to_string(image.width)
        + "x"
        + std::to_string(image.height)
        + " tileH="
        + std::to_string(tileHeight);
}

static inline std::string formatStructureAnchorWarning(
    const std::string& structureType,
    const ArtObjectLink& link,
    const ArtType* artType,
    const StructureSpriteReport& image,
    const std::vector<std::string>& candidateFiles,
    int anchorDx,
    int anchorDy,
    const std::string& fileName
) {
    return "structure art anchor "
        + structureType
        + ": art="
        + (link.artId.empty() ? "-" : link.artId)
        + " image="
        + (link.image.empty() ? "-" : link.image)
        + " foundation="
        + (link.foundation.empty() ? "-" : link.foundation)
        + " shp="
        + fileName
        + " frame="
        + std::to_string(image.frameIndex)
        + " size="
        + std::to_string(image.width)
        + "x"
        + std::to_string(image.height)
        + " global="
        + std::to_string(image.globalWidth)
        + "x"
        + std::to_string(image.globalHeight)
        + " offset="
        + std::to_string(image.offsetX)
        + ","
        + std::to_string(image.offsetY)
        + " format="
        + std::to_string(image.format)
        + " dataOffset="
        + std::to_string(image.dataOffset)
        + " activeAnim="
        + formatActiveAnimationSummary(artType)
        + " rendererAnchor=footprintRearCell(+"
        + std::to_string(anchorDx)
        + ",+"
        + std::to_string(anchorDy)
        + ")"
        + " spriteTopLeft=project(x+"
        + std::to_string(anchorDx)
        + ",y+"
        + std::to_string(anchorDy)
        + ")+(-globalWidth/2+frameOffsetX,+tileH/2-globalHeight+frameOffsetY)"
        + " "
        + formatExpectedStructureRect(image)
        + " candidates="
        + formatCandidateList(candidateFiles);
}

static inline std::string formatStructureAnchorAdvice(
    const StructureSpriteReport& image,
    int anchorDx,
    int anchorDy
) {
    return "footprintRearCell(+"
        + std::to_string(anchorDx)
        + ",+"
        + std::to_string(anchorDy)
        + ") topLeft=project(anchor)+("
        + std::to_string((-image.globalWidth / 2) + image.offsetX)
        + ","
        + "tileH/2-"
        + std::to_string(image.globalHeight)
        + "+"
        + std::to_string(image.offsetY)
        + ") "
        + formatExpectedStructureRect(image);
}

} // namespace concord::assets::preview
