// Original gamemd.exe PipIndex table at 0x81B958..0x81B9AC.
// TechnoClass::DrawPips (0x709C27..0x709D17) uses frame 6 for an
// unoccupied slot and InfantryType::OccupyPip for each actual occupant.
int runtimeOccupantPipFrame(const std::string& authored) {
    const auto pip = toLowerAsciiLocal(trimAsciiLocal(authored));
    const std::array<const char*, 12> names{
        "green", "yellow", "white", "red", "blue", "",
        "persongreen", "personyellow", "personwhite", "personred", "personblue", "personpurple"
    };
    for (std::size_t i = 0; i < names.size(); ++i)
        if (!pip.empty() && pip == names[i]) return static_cast<int>(i) + 1;
    return 7;
}

std::vector<int> runtimeGarrisonPipFrames(
    const RuntimeWindowState& state, const WorldObjectInstance& host
) {
    const auto* building = findRuntimeBuildingType(state.rules, host);
    if (!host.alive || host.health <= 0 || host.rubble || building == nullptr) return {};
    const bool absorber = building->infantryAbsorb;
    const int capacity = absorber ? building->passengers
        : building->canBeOccupied ? building->maxNumberOccupants : 0;
    if (capacity <= 0) return {};
    std::vector<int> frames(static_cast<std::size_t>(capacity), 6);
    std::size_t slot = 0;
    for (int id : absorber ? host.transportPassengerIds : host.garrisonOccupantIds) {
        if (slot == frames.size()) break;
        const auto* occupant = findRuntimeObjectByIdIncludingInactive(state, id);
        if (occupant == nullptr || occupant->alive || occupant->health <= 0
            || (!absorber && occupant->garrisonHostObjectId != host.instanceId)) continue;
        const auto* unit = findRuntimeUnitType(state.rules, *occupant);
        if (unit != nullptr) frames[slot++] = runtimeOccupantPipFrame(unit->occupyPip);
    }
    return frames;
}

int drawRuntimeGarrisonPips(
    HDC dc, RuntimeWindowState& state, const WorldObjectInstance& host,
    const RuntimeStructureSelectionFrameGeometry& geometry
) {
    if (!geometry.valid) return 0;
    const auto frames = runtimeGarrisonPipFrames(state, host);
    const bool absorber = runtimeInfantryAbsorberType(state, host) != nullptr;
    // Native 0x6F6584..0x6F65C0 projects the left foundation corner at
    // ground height; 0x709A9D..0x709AB3 adds (6,-1), with a (4,2) step.
    // Preserve the SHP canvas and offsets so gray/colored people align.
    int draws = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto* sprite = loadRuntimeUiShpFrame(state, "sidebar", "pips", frames[i]);
        if (sprite == nullptr) continue;
        const int width = sprite->image.globalWidth;
        const int height = sprite->image.globalHeight;
        const int x = geometry.left.x + 6 + static_cast<int>(i) * 4 - width / 2;
        const int y = geometry.left.y - 1 + static_cast<int>(i) * 2 - height / 2;
        if (drawRuntimeUiShpFrameCanvasTransparent(dc, state, "sidebar", "pips", frames[i],
                "palette", RECT{x, y, x + width, y + height}, true)) {
            ++draws;
            if (absorber) {
                ++state.lastSelectedCargoPipSegments;
                ++state.lastSelectedCargoPipResourceDraws;
                if (frames[i] != 6) ++state.lastSelectedCargoPipFilledSegments;
            }
        }
    }
    if (absorber && draws > 0) ++state.lastSelectedCargoPipObjects;
    return draws;
}
