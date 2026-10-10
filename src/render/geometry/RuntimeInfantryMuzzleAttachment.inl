#include "render/geometry/RuntimeInfantryMuzzleAnchors.inl"
// Native anchors must never apply to a same-named replacement SHP.
bool resolveRuntimeInfantryWeaponMuzzlePoint(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, POINT& result) {
    auto* sprite = loadRuntimeObjectShpSpriteFromCache(object, context, cache);
    if (!sprite || !sprite->loaded) return false;
    static const std::map<std::string, std::uint64_t> nativeFingerprints{
        {"boris", 0x330f8f63a9311b35ULL},
        {"gi", 0x9d010ac9e9d34eedULL},
        {"ggi", 0x81587c23d9097644ULL},
        {"shk", 0xb4863621a7227fa0ULL},
        {"cleg", 0x1f170ca21a3bfa74ULL},
        {"arnd", 0xe3a20e6b98dbf157ULL},
        {"ccomand", 0x55da0e70a1d44193ULL},
        {"civ3", 0xf45edd46d5958fabULL},
        {"civa", 0xeb953e98bc962ba4ULL},
        {"civb", 0x36c983407486d42dULL},
        {"civc", 0xe4db717e485a255aULL},
        {"cons", 0xdbb0ec7bfa42070bULL},
        {"deso", 0xa1b7f41c769d206bULL},
        {"flakt", 0xb945c41284ef3be4ULL},
        {"init", 0x027bbedf24a066daULL},
        {"lunr", 0xf4b59b3a29f67ffbULL},
        {"pentgen", 0xa4c2688cd444b84aULL},
        {"ptroop", 0x0dfee7a883436701ULL},
        {"rock", 0xcd33d1b1d1d66d08ULL},
        {"seal", 0xcab025522f485f51ULL},
        {"snipe", 0x236cab8588d57b29ULL},
        {"ssrv", 0xa44d043c45b68c1aULL},
        {"stln", 0x5c5b5fb7c9dc9325ULL},
        {"tany", 0x9bb8ab5909e84d57ULL},
        {"virus", 0x8b5d2e56dc7f859bULL},
        {"vladimir", 0xb9ba3e62148ddac8ULL},
        {"yuri", 0x0938eec502c8ff51ULL},
        {"yurix", 0x179e5767f870c346ULL},
    };
    const auto found = nativeFingerprints.find(toLowerAscii(sprite->resolvedImageId));
    if (found == nativeFingerprints.end() || found->second != sprite->sourceFingerprint) return false;
    const auto& image = sprite->image;
    const int width = image.globalWidth > 0 ? image.globalWidth : image.width;
    const int height = image.globalHeight > 0 ? image.globalHeight : image.height;
    POINT point{};
    if (!runtimeFindInfantryMuzzleAnchor(sprite->resolvedImageId, sprite->sequenceName,
            sprite->frameIndex, width, height, point)) return false;
    result = {center.x - width / 2 + point.x,
        center.y + camera.tileHeight / 2 - height + point.y};
    return true;
}
