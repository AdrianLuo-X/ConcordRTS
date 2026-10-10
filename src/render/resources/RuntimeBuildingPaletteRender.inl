// These retained building SHPs predate the palettes normally used by RA2.
// Match both their effective ART identity and the complete loaded file, so a
// replacement SHP or an unrelated ART alias keeps its declared palette.
int runtimeLegacyBuildingPaletteIdentity(
    const ArtType& art, const RuntimeObjectShpSpriteCacheEntry& sprite
) {
    const std::string artId = toLowerAscii(art.id);
    const bool city = artId.size() == 6 && artId.compare(0, 5, "city0") == 0
        && artId[5] >= '1' && artId[5] <= '6';
    const bool civilianCity = artId.size() == 8 && artId.compare(0, 7, "cacity0") == 0
        && artId[7] >= '1' && artId[7] <= '4';
    const bool civilianNewYork = artId == "canewy05";
    if (!city && !civilianCity && !civilianNewYork && artId != "galite") return 0;
    const std::string image = toLowerAscii(
        std::filesystem::path(sprite.resolvedImageId).stem().string());
    constexpr std::uint64_t cityFingerprints[] = {
        0xB4D34D4AD5298C6EULL, 0xBB95C728019FEA67ULL,
        0xD546B613B9811542ULL, 0xF78333271269E458ULL,
        0x8646DB7C7233EB03ULL, 0xF74FECF98276BAB7ULL
    };
    if (city && image == artId) {
        const int index = artId[5] - '1';
        if (sprite.sourceFingerprint == cityFingerprints[index]) return index + 1;
    }
    if (artId == "galite" && image == "gglite"
        && sprite.sourceFingerprint == 0x3A0CE389C55B083AULL) return 7;
    if (artId == "galite" && image == "galite"
        && sprite.sourceFingerprint == 0x19C580F3CAC53578ULL) return 8;
    if (civilianCity || civilianNewYork) {
        // These are distinct six-frame civilian SHPs, not aliases of CITY01-06.
        // All six stock theater copies of each file have identical bytes and
        // citytem indices. Match only the independently verified image names.
        struct CivilianIdentity {
            std::uint64_t fingerprint;
            std::array<const char*, 6> images;
        };
        constexpr CivilianIdentity civilians[] = {
            {0x8D33126A988145A5ULL,
                {"ctcity01", "cacity01", "cucity01", "cdcity01", "clcity01", "cncity01"}},
            {0x52CB9902B5DEC752ULL,
                {"ctcity02", "cacity02", "cucity02", "cdcity02", "clcity02", "cncity02"}},
            {0xC22D414D0EF127FFULL,
                {"ctcity03", "cacity03", "cucity03", "cdcity03", "clcity03", "cncity03"}},
            {0xD5D5AB42AE91914FULL,
                {"ctcity04", "cacity04", "cucity04", "cdcity04", "clcity04", "cncity04"}},
            {0xB332A38A8FDB937EULL,
                {"ctnewy05", "canewy05", "cunewy05", "cdnewy05", "clnewy05", "cnnewy05"}}
        };
        const int index = civilianNewYork ? 4 : artId[7] - '1';
        const auto& identity = civilians[index];
        if (sprite.sourceFingerprint == identity.fingerprint) {
            for (const char* originalImage : identity.images) {
                if (image == originalImage) return 9 + index;
            }
        }
    }
    return 0;
}

const std::vector<RuntimePaletteColor>* runtimeBuildingTerrainPaletteForRender(
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache
) {
    if (context.palettePath.empty()) return &cache.structurePalette;
    // Packed SHP colors retain the palette's address. Give each full ISO path
    // an immutable entry, rather than reusing cache.palette's mutable buffer
    // when a caller renders several theaters with one render cache.
    const std::string key = context.assetRootPath + "|building-terrain|" + context.palettePath;
    auto& entry = cache.namedPalettesByKey[key];
    if (!entry.loadAttempted) {
        entry.key = key;
        entry.loadAttempted = true;
        std::string error;
        entry.loaded = loadRuntimeConfiguredPalette(
            context, cache, context.palettePath, entry.palette, error,
            &entry.sourcePath);
    }
    return entry.loaded ? &entry.palette : &cache.structurePalette;
}

const std::vector<RuntimePaletteColor>* runtimeBuildingPaletteForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, const RuntimeObjectShpSpriteCacheEntry& sprite
) {
    const auto* art = findArtTypeForObject(object, context);
    const auto* ordinary = &cache.structurePalette;
    if (!art) return ordinary;

    // Explicit declarations never fall through to legacy compatibility, even
    // when their requested file is absent. Keep the existing ordinary fallback.
    if (!art->palette.empty()) {
        const auto* palette = runtimeBuildingCustomPaletteForRender(object, context, cache);
        return palette ? palette : ordinary;
    }
    if (!art->customPalette.empty()) {
        const auto* palette = runtimeNamedPaletteForRender(context, cache, art->customPalette);
        return palette ? palette : ordinary;
    }
    if (art->animPalette) {
        const auto* palette = runtimeAnimPaletteForRender(context, cache);
        return palette ? palette : ordinary;
    }
    if (runtimeArtTypeUsesUnitPalette(art))
        return runtimeUnitPaletteForRender(context, cache, false);

    const bool independentDeclaration = art->animPaletteSpecified
        || !trimAsciiForRender(art->altPalette).empty();
    const int legacyIdentity = independentDeclaration
        ? 0 : runtimeLegacyBuildingPaletteIdentity(*art, sprite);
    if (art->terrainPalette) {
        // Honor the loaded game's ART and theater palette. Do not substitute
        // palettes downloaded from another game for retained legacy artwork.
        return runtimeBuildingTerrainPaletteForRender(context, cache);
    }
    if (!independentDeclaration && !art->terrainPaletteSpecified
        && ((legacyIdentity >= 1 && legacyIdentity <= 5)
            || (legacyIdentity >= 9 && legacyIdentity <= 13))) {
        // The stock ;Palette=city lines are disabled. Restore only the exact
        // retained artwork; an explicit Palette=city still uses theater rules.
        if (const auto* palette = runtimeNamedPaletteForRender(context, cache, "citytem.pal"))
            return palette;
    }
    return ordinary;
}
