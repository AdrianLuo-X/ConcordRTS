// Physical mouths independently decoded from occupied original VXL cells and
// the original HVA, then checked in raw side/top views. These three weapons
// share a section with a hull or tall antenna, so the generic long-thin barrel
// classifier intentionally cannot identify them. Do not extend this to noses.
// Evidence: build/smoke/feature-set-15/03-muzzles/model-research/fixed-guns/.
bool runtimeNativeVoxelMuzzleAnchor(const std::string& imageId,
    const std::vector<unsigned char>& vxlBytes,
    const std::vector<unsigned char>& hvaBytes, std::array<double, 3>& point) {
    struct NativeAnchor {
        const char* image;
        std::uint64_t vxlFingerprint, hvaFingerprint;
        std::array<double, 3> point;
    };
    static constexpr NativeAnchor anchors[] = {
        {"tnkd", 13076620797034030814ULL, 18166117387921168761ULL,
            {35.12815093994141, -0.3403994292020798, 19.580945380032063}},
        {"hwtz", 2469757275160035724ULL, 18166117387921168761ULL,
            {34.07476806640625, -0.10188573406588652, 26.993845669475075}},
        {"robotur", 12013696318534157993ULL, 7520472233040768239ULL,
            {22.79626102851443, -2.8119160199288435, 13.628916614510903}},
    };
    const std::string image = toLowerAscii(imageId);
    for (const auto& anchor : anchors) {
        if (image != anchor.image) continue;
        const auto fingerprint = [](const std::vector<unsigned char>& bytes) {
            std::uint64_t value = 14695981039346656037ULL;
            for (unsigned char byte : bytes) value = (value ^ byte) * 1099511628211ULL;
            return value;
        };
        if (fingerprint(vxlBytes) != anchor.vxlFingerprint
            || fingerprint(hvaBytes) != anchor.hvaFingerprint) return false;
        point = anchor.point;
        return true;
    }
    return false;
}
