unsigned int readUInt16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        return 0;
    }

    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
}

int runtimeShpFrameCount(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < 8) {
        return 0;
    }

    return static_cast<int>(readUInt16(bytes, 6));
}

int runtimeVisibleMainFrameCount(int frameCount, bool splitShadow) {
    if (splitShadow && frameCount >= 2 && frameCount % 2 == 0) {
        return std::max(1, frameCount / 2);
    }

    return std::max(1, frameCount);
}

std::string toLowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

bool runtimeFileNameHasExtension(
    const std::string& fileName,
    const std::string& extension) {
    if (fileName.empty() || extension.empty()) {
        return false;
    }

    return toLowerAscii(std::filesystem::path(fileName).extension().string())
        == toLowerAscii(extension);
}

std::string fileNameForShpImageId(const std::string& imageId) {
    if (imageId.empty()) {
        return "";
    }

    const std::string lower = toLowerAscii(imageId);
    if (!std::filesystem::path(lower).extension().empty()) {
        return imageId;
    }

    return imageId + ".shp";
}

bool runtimeAuxiliaryResourceUsesShpBytes(
    const std::string& fileName,
    const ArtType* artType) {
    if (runtimeFileNameHasExtension(fileName, ".shp")
        || runtimeFileNameHasExtension(fileName, ".sha")) {
        return true;
    }
    if (artType == nullptr || !artType->theater) {
        return false;
    }

    static constexpr std::array<const char*, 6> theaterExtensions{
        ".tem", ".sno", ".urb", ".des", ".lun", ".ubn"
    };
    return std::any_of(
        theaterExtensions.begin(),
        theaterExtensions.end(),
        [&fileName](const char* extension) {
            return runtimeFileNameHasExtension(fileName, extension);
        }
    );
}

bool isRuntimeSidebarResourceFileName(const std::string& fileName) {
    const std::string lowerFile = toLowerAscii(fileName);
    return lowerFile == "credits.shp"
        || lowerFile == "top.shp"
        || lowerFile == "side1.shp"
        || lowerFile == "side2.shp"
        || lowerFile == "side2b.shp"
        || lowerFile == "side3.shp"
        || lowerFile == "addon.shp"
        || lowerFile == "tabs.shp"
        || lowerFile == "tab00.shp"
        || lowerFile == "tab01.shp"
        || lowerFile == "tab02.shp"
        || lowerFile == "tab03.shp"
        || lowerFile == "diplobtn.shp"
        || lowerFile == "optbtn.shp"
        || lowerFile == "repair.shp"
        || lowerFile == "sell.shp"
        || lowerFile == "r-dn.shp"
        || lowerFile == "r-up.shp"
        || lowerFile == "gclock2.shp"
        || lowerFile == "power.shp"
        || lowerFile == "powerp.shp"
        || lowerFile == "radar.shp"
        || lowerFile == "pips.shp"
        || lowerFile == "sidebar.pal";
}

std::chrono::steady_clock::time_point runtimeMaybeTimingStart(bool enabled) {
    return enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
}

double runtimeMaybeElapsedMs(
    bool enabled,
    const std::chrono::steady_clock::time_point& start) {
    return enabled
        ? runtimeElapsedMs(start, std::chrono::steady_clock::now())
        : 0.0;
}

double runtimeElapsedMs(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}
