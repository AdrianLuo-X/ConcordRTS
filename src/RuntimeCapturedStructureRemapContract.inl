bool runRuntimeCapturedStructureRemapContractCheck(std::string& errorMessage) {
    IniFile rulesIni;
    IniFile artIni;
    RulesDatabase rules;
    ArtDatabase art;
    if (!rulesIni.loadFromString(
            "[InfantryTypes]\n0=ENGINEER\n"
            "[BuildingTypes]\n0=TECH\n1=GARRISON\n2=DECOR\n3=TERRAINTECH\n"
            "[ENGINEER]\nEngineer=yes\n"
            "[TECH]\nCapturable=yes\nNeedsEngineer=yes\n"
            "[GARRISON]\nCanBeOccupied=yes\n"
            "[DECOR]\nCapturable=no\n"
            "[TERRAINTECH]\nCapturable=yes\nNeedsEngineer=yes\n"
        )
        || !artIni.loadFromString(
            "[ENGINEER]\nRemapable=yes\n"
            "[TECH]\nFoundation=2x2\nRemapable=no\nActiveAnim=FLAG\n"
            "[GARRISON]\nFoundation=2x2\nRemapable=no\n"
            "[DECOR]\nFoundation=2x2\nRemapable=no\n"
            "[TERRAINTECH]\nFoundation=2x2\nTerrainPalette=yes\n"
            "[FLAG]\nRemapable=no\n"
            "[TERRAINFLAG]\nTerrainPalette=yes\n"
            "[ANIMFLAG]\nAnimPalette=yes\n"
        )
        || !rules.loadFromIni(rulesIni) || !art.loadFromIni(artIni)) {
        errorMessage = "could not load captured structure remap fixtures";
        return false;
    }
    std::unordered_map<std::string, COLORREF> colors{
        {"playera", RGB(10, 210, 30)}, {"playerb", RGB(210, 20, 170)}
    };
    RuntimeMapRenderContext context;
    context.rules = &rules;
    context.art = &art;
    context.ownerRemapColors = &colors;
    std::array<std::uint32_t, 2> pixels{};
    context.surfacePixels = pixels.data();
    context.surfaceWidth = 2;
    context.surfaceHeight = 1;
    RuntimeMapCamera camera;
    camera.tileHeight = 2;
    RuntimeShpFrameImage sprite;
    sprite.width = 2;
    sprite.height = 1;
    sprite.pixels = {16, 42};
    std::vector<RuntimePaletteColor> palette(256);
    palette[16] = {180, 30, 10};
    palette[42] = {11, 22, 33};
    const auto draw = [&](const WorldObjectInstance& object, bool remap) {
        pixels.fill(0);
        return drawRuntimeShpFrame(
            nullptr, RECT{0, 0, 2, 1}, POINT{1, 0}, context, camera,
            sprite, palette, remap, runtimeOwnerRemapColor(context, object.owner)
        );
    };
    // Reuse the same cached sprite while capturing, recapturing and returning
    // to neutral. Building bodies and flag layers must match unit remap pixels.
    for (const char* owner : {"Neutral", "PlayerA", "PlayerB", "Neutral"}) {
        WorldObjectInstance engineer;
        engineer.kind = WorldObjectKind::Infantry;
        engineer.type = "ENGINEER";
        engineer.owner = owner;
        if (!draw(engineer, runtimeObjectShouldOwnerRemap(engineer, context))) {
            errorMessage = "could not draw engineer remap reference";
            return false;
        }
        const auto expected = pixels;
        for (const char* type : {"TECH", "GARRISON"}) {
            WorldObjectInstance structure;
            structure.type = type;
            structure.owner = owner;
            structure.health = 100;
            if (!runtimeObjectShouldOwnerRemap(structure, context)
                || !draw(structure, runtimeObjectShouldOwnerRemap(structure, context))
                || pixels != expected || pixels[1] != 0x000B1621U
                || !runtimeStructureLayerShouldOwnerRemap(structure, context, "FLAG")
                || !draw(structure, runtimeStructureLayerShouldOwnerRemap(
                    structure, context, "FLAG"
                ))
                || pixels != expected
                || runtimeStructureLayerShouldOwnerRemap(structure, context, "TERRAINFLAG")
                || runtimeStructureLayerShouldOwnerRemap(structure, context, "ANIMFLAG")) {
                errorMessage = std::string(type) + " remap differs from engineer/flag: " + owner;
                return false;
            }
        }
    }
    for (const char* type : {"DECOR", "TERRAINTECH"}) {
        WorldObjectInstance fixed;
        fixed.type = type;
        fixed.owner = "PlayerA";
        if (runtimeObjectShouldOwnerRemap(fixed, context)) {
            errorMessage = std::string(type) + " fixed/terrain palette was remapped";
            return false;
        }
    }
    return true;
}
