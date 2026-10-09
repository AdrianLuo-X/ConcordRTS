bool runRuntimeBioReactorSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "bioreactor: " + message;
        return false;
    };
    const auto* stock = live.rules.findBuildingType("YAPOWR");
    if (stock == nullptr || !stock->infantryAbsorb || stock->passengers != 5
        || stock->power != 150 || stock->extraPower != 100 || stock->sizeLimit != 15) {
        return fail("stock reactor rules were not parsed");
    }
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config;
    probe.report = live.report;
    probe.rules = live.rules;
    probe.rulesLoaded = true;
    probe.art = live.art;
    probe.artLoaded = true;
    probe.theater = live.theater;
    probe.theaterLoaded = live.theaterLoaded;
    probe.world = live.world;
    probe.world.accessObjects().clear();
    probe.world.accessTerrainCells().clear();
    probe.world.accessTerrainObjects().clear();
    probe.world.accessSmudges().clear();
    probe.economy.owner = "YuriCountry";
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.triggers.bootstrapComplete = true;
    probe.economy.skirmishStartApplied = true;
    for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 28; ++x) {
        TerrainCell cell;
        cell.x = x;
        cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y);
        cell.tileIndex = 0;
        probe.world.accessTerrainCells().push_back(cell);
    }
    WorldObjectInstance reactor;
    reactor.instanceId = 1;
    reactor.kind = WorldObjectKind::Structure;
    reactor.type = "YAPOWR";
    reactor.owner = probe.economy.owner;
    reactor.health = stock->strength;
    reactor.alive = true;
    reactor.x = reactor.y = 12;
    reactor.cell = runtimeCellKey(12, 12);
    reactor.mission = "Guard";
    auto consumer = reactor;
    consumer.instanceId = 9;
    consumer.type = "GACSPH";
    consumer.x = consumer.y = 23;
    consumer.cell = runtimeCellKey(23, 23);
    consumer.health = probe.rules.findBuildingType(consumer.type)->strength;
    auto infantry = reactor;
    infantry.instanceId = 2;
    infantry.kind = WorldObjectKind::Infantry;
    infantry.type = "E1";
    infantry.x = 4;
    infantry.y = 10;
    infantry.cell = runtimeCellKey(infantry.x, infantry.y);
    infantry.health = 125;
    infantry.mission = "Guard";
    probe.world.accessObjects() = {reactor, infantry, consumer};
    initializeRuntimeNavigation(probe);
    rebuildRuntimeEconomyPower(probe);
    const auto powerMatches = [&](int count) {
        const auto* host = findObjectById(probe, 1);
        const int expected = 150 + count * 100;
        const auto pips = runtimeCargoPipDisplayForObject(probe, *host);
        return probe.economy.powerProduced == expected
            && runtimeBuildingPowerOutput(*stock, *host) == expected
            && pips.available && pips.totalUnits == 5 && pips.filledUnits == count
            && runtimeCampaignPowerNetForHouse(probe, probe.economy.owner)
                == expected - probe.economy.powerUsed
            && runtimeSuperWeaponContext(probe).ownerPowered(probe.economy.owner)
                == (expected >= probe.economy.powerUsed);
    };
    if (!powerMatches(0)) return fail("empty reactor did not provide only 150 power / zero pips");

    // Every stock InfantryType uses absorption, regardless of Occupier,
    // Engineer, C4, animal, spy, IFV mode or flying movement capabilities.
    int infantryTypesChecked = 0;
    for (const auto& type : live.rules.getInfantryTypes()) {
        auto* foot = findMutableObjectById(probe, 2);
        foot->type = type.id;
        foot->health = std::max(1, type.strength);
        foot->airborne = type.jumpJet;
        foot->flightLevel = foot->airborne ? 4 : 0;
        foot->level = foot->flightLevel;
        clearRuntimeRenderPosition(*foot);
        probe.selectedObjectIds = {2};
        const auto& host = *findObjectById(probe, 1);
        if (type.size > stock->sizeLimit) {
            // Stock mission dinosaurs are InfantryTypes with Size=30;
            // the original SizeLimit=15 still applies to those exceptions.
            if (runtimeObjectCanEnterTransport(probe, *foot, host, true)
                || runtimeTargetCursorIntent(probe, host) != RuntimeTargetCursorIntent::NoEnter) {
                return fail(type.id + " bypassed the stock size limit");
            }
            continue;
        }
        if (!runtimeObjectCanEnterTransport(probe, *foot, host, true)
            || runtimeTargetCursorIntent(probe, host) != RuntimeTargetCursorIntent::InfantryAbsorb
            || !queueRuntimeTargetAbility(probe, host)
            || !probe.pendingTransportTargets.contains(2)) {
            return fail(type.id + " did not receive the absorption cursor / entry command");
        }
        if (completeRuntimeTransportEntry(probe, 2) || !powerMatches(0)) {
            return fail(type.id + " supplied power before reaching the reactor");
        }
        eraseRuntimeBehavior(probe, 2);
        ++infantryTypesChecked;
    }
    auto* foot = findMutableObjectById(probe, 2);
    *foot = infantry;
    foot->kind = WorldObjectKind::Unit;
    foot->type = "MTNK";
    if (runtimeObjectCanEnterTransport(probe, *foot, *findObjectById(probe, 1), true)) {
        return fail("a vehicle entered an infantry-only reactor");
    }
    *foot = infantry;
    foot->owner = "Americans";
    if (runtimeObjectCanEnterTransport(probe, *foot, *findObjectById(probe, 1), true)) {
        return fail("enemy infantry could enter the reactor");
    }
    *foot = infantry;
    auto* host = findMutableObjectById(probe, 1);
    host->buildAnimTicksRemaining = 10;
    if (runtimeObjectCanEnterTransport(probe, *foot, *host, true)) {
        return fail("unfinished reactor accepted an entrant");
    }
    host->buildAnimTicksRemaining = 0;

    const char* output = std::getenv("CONCORD_BIOREACTOR_OUTPUT");
    if (output != nullptr) std::filesystem::create_directories(output);
    const auto renderReactor = [&](const std::string& name, const std::string& image,
            bool hover = false, std::vector<std::uint32_t>* captured = nullptr) {
        const int savedTick = probe.tick;
        const auto savedSelection = probe.selectedObjectIds;
        const bool savedTooltips = probe.runtimeTooltipsEnabled;
        // Compare occupancy levels at the same animation phase, without a selection.
        probe.tick = 0;
        probe.selectedObjectIds.clear();
        probe.runtimeTooltipsEnabled = true;
        const auto stats = drawRuntimeStateOffscreenAtCell(probe, {12, 12}, 640, 420,
            error, output == nullptr ? "" : std::string(output) + "/" + name + ".bmp",
            false, hover, hover ? 1 : -1, captured, hover, nullptr, hover);
        probe.tick = savedTick;
        probe.selectedObjectIds = savedSelection;
        probe.runtimeTooltipsEnabled = savedTooltips;
        if (stats.has_value() && stats->firstBuildingArtAnimationImage != image) {
            error = "expected " + image + ", rendered " + stats->firstBuildingArtAnimationImage;
        }
        if (hover) {
            const int count = static_cast<int>(findObjectById(probe, 1)->transportPassengerIds.size());
            if (probe.lastHoveredStructureHealthBars != 1 || probe.lastSelectedHealthBars != 0
                || probe.lastSelectedCargoPipObjects != 1 || probe.lastSelectedCargoPipSegments != 5
                || probe.lastSelectedCargoPipFilledSegments != count
                || probe.lastSelectedCargoPipResourceDraws != 5
                || probe.lastSelectedCargoPipFallbackDraws != 0
                || probe.lastGameplayTooltipsDrawn != 1
                || probe.lastGameplayTooltipText.find(L"已进驻 " + std::to_wstring(count) + L"/5")
                    == std::wstring::npos) {
                error = "hover did not show the actual occupant count / native person pips: "
                    + std::to_string(count) + " people, "
                    + std::to_string(probe.lastSelectedCargoPipFilledSegments) + " filled pips, "
                    + std::to_string(probe.lastSelectedCargoPipResourceDraws) + " resource draws, "
                    + std::to_string(probe.lastGameplayTooltipsDrawn) + " tooltip";
                return false;
            }
        }
        return stats.has_value() && stats->firstBuildingArtAnimationImage == image;
    };
    if (!renderReactor("empty", "YAPOWR_A")) {
        return fail("empty reactor did not render its normal active animation: " + error);
    }

    // Real movement and reservations must admit five and leave the sixth outside.
    const std::array<std::string, 6> group{"E1", "ENGINEER", "DOG", "BRUTE", "JUMPJET", "IVAN"};
    probe.world.accessObjects() = {reactor, consumer};
    for (int index = 0; index < static_cast<int>(group.size()); ++index) {
        auto member = infantry;
        member.instanceId = index + 2;
        member.type = group[index];
        member.x = 5;
        member.y = 7 + index;
        member.cell = runtimeCellKey(member.x, member.y);
        member.health = probe.rules.findUnitType("Infantry", member.type)->strength;
        member.airborne = member.type == "JUMPJET";
        member.level = member.flightLevel = member.airborne ? 4 : 0;
        probe.world.accessObjects().push_back(member);
    }
    initializeRuntimeNavigation(probe);
    probe.selectedObjectIds = {2, 3, 4, 5, 6, 7};
    if (!queueRuntimeTargetAbility(probe, *findObjectById(probe, 1))
        || probe.pendingTransportTargets.size() != 5U || !powerMatches(0)) {
        return fail("six-unit selection did not reserve exactly five unpowered slots");
    }
    if (!renderReactor("reserved-hover", "YAPOWR_A", true))
        return fail("queued infantry were counted before arrival: " + error);
    probe.selectedObjectIds = {7};
    if (runtimeTargetCursorIntent(probe, *findObjectById(probe, 1)) != RuntimeTargetCursorIntent::NoEnter
        || !queueRuntimeTargetAbility(probe, *findObjectById(probe, 1))
        || probe.pendingTransportTargets.contains(7)) {
        return fail("full reservation did not block the sixth unit's special ability");
    }
    int previousCount = 0;
    for (int tick = 0; tick < 800 && !probe.pendingTransportTargets.empty(); ++tick) {
        ++probe.tick;
        advanceRuntimeSimulation(probe);
        const int count = static_cast<int>(findObjectById(probe, 1)->transportPassengerIds.size());
        if (count < previousCount || count > 5 || !powerMatches(count)) {
            return fail("arrival count and power disagreed during simulation");
        }
        previousCount = count;
    }
    if (!probe.pendingTransportTargets.empty() || !powerMatches(5)
        || findObjectById(probe, 7) == nullptr) {
        return fail("five infantry did not finish entering / sixth was consumed");
    }
    if (!renderReactor("occupied", "YAPOWR_B")) {
        return fail("occupied reactor did not render its enhanced active animation: " + error);
    }
    // Isolate all six real manifest sizes on the same scene and animation tick.
    // Only the liquid chamber pixels are compared; walking infantry, shadows,
    // selection pips and the numeric tooltip cannot satisfy this visual check.
    host = findMutableObjectById(probe, 1);
    const auto fullManifest = host->transportPassengerIds;
    for (bool damaged : {false, true}) {
        host->health = damaged ? stock->strength / 2 - 1 : stock->strength;
        std::vector<std::uint32_t> previousChambers;
        for (int count = 0; count <= 5; ++count) {
            host->transportPassengerIds.assign(fullManifest.begin(), fullManifest.begin() + count);
            rebuildRuntimeEconomyPower(probe);
            if (!powerMatches(count)) return fail("visual level disagreed with generated power / count");
            const std::string image = count == 5
                ? (damaged ? "YAPOWR_BD" : "YAPOWR_B") : (damaged ? "YAPOWR_AD" : "YAPOWR_A");
            const std::string name = std::string(damaged ? "damaged-level-" : "level-") + std::to_string(count);
            std::vector<std::uint32_t> pixels;
            if (!renderReactor(name, image, false, &pixels)) return fail(name + ": " + error);
            if (pixels.size() != 640U * 420U) return fail(name + " capture was incomplete");
            std::vector<std::uint32_t> chambers;
            // Original 174x142 SHP canvas centered at (320,210), liquid at
            // canvas x=53..116 and y=51..89, above the ground-level entrants.
            for (int y = 190; y < 228; ++y) for (int x = 286; x < 350; ++x)
                chambers.push_back(pixels[static_cast<std::size_t>(y) * 640 + x]);
            if (count > 0 && chambers == previousChambers)
                return fail(name + " did not change the actual liquid chamber rendering");
            previousChambers = std::move(chambers);
            if (!renderReactor(name + "-hover", image, true)) return fail(name + " hover: " + error);
        }
    }
    host->health = stock->strength;
    host->transportPassengerIds = fullManifest;
    rebuildRuntimeEconomyPower(probe);
    host = findMutableObjectById(probe, 1);
    if (runtimeSecondaryAbilityForObject(probe, *host) != RuntimeSecondaryAbility::TransportUnload) {
        return fail("loaded reactor did not offer unloading");
    }

    const auto savePath = std::filesystem::temp_directory_path() / ("concord_bioreactor_smoke_" + std::to_string(GetCurrentProcessId()) + ".ini");
    if (!saveRuntimeState(probe, pathString(savePath), error)
        || !loadRuntimeState(probe, pathString(savePath), error) || !powerMatches(5)) {
        return fail("save/load did not preserve occupants and generated power: " + error);
    }
    if (!renderReactor("loaded-hover", "YAPOWR_B", true)) return fail("loaded hover: " + error);
    host = findMutableObjectById(probe, 1);
    if (unloadRuntimeTransport(probe, *host) != 5 || !powerMatches(0)) {
        return fail("unloading failed to restore the empty reactor's 150 power");
    }
    if (!renderReactor("unloaded-hover", "YAPOWR_A", true)) return fail("unloaded appearance/count: " + error);
    for (int id = 2; id <= 6; ++id) {
        const auto* released = findObjectById(probe, id);
        if (released == nullptr || released->health <= 0) return fail("unload lost an occupant");
    }

    // AI action 62 shares the same arrival path and never teleports a team inside.
    RuntimeTeamMissionState mission;
    mission.objectIds = {2, 3, 4, 5, 6};
    const RuntimeScriptStep step{62, 0};
    if (issueRuntimeTeamScriptStep(probe, mission, step) != RuntimeTeamStepDisposition::Waiting
        || !powerMatches(0)) {
        return fail("AI could not queue ordinary reactor entry");
    }
    for (int tick = 0; tick < 800 && !probe.pendingTransportTargets.empty(); ++tick) {
        ++probe.tick;
        advanceRuntimeSimulation(probe);
    }
    if (!powerMatches(5)
        || runtimeTeamScriptStepCompletion(probe, mission, step) != RuntimeTeamStepCompletion::Complete) {
        return fail("AI did not complete five real arrivals");
    }
    host = findMutableObjectById(probe, 1);
    host->owner = "Americans";
    if (!evacuateRuntimeOwnerTransitionTransports(probe, error)) {
        return fail("captured reactor could not release its original occupants: " + error);
    }
    rebuildRuntimeEconomyPower(probe);
    if (probe.economy.powerProduced != 0 || !host->transportPassengerIds.empty()
        || findObjectById(probe, 6) == nullptr || !findObjectById(probe, 6)->airborne) {
        return fail("capture retained old-owner power / lost a flying infantry exit");
    }
    host->owner = probe.economy.owner;
    rebuildRuntimeEconomyPower(probe);
    if (issueRuntimeTeamScriptStep(probe, mission, step) != RuntimeTeamStepDisposition::Waiting
        || !powerMatches(0)) return fail("could not refill after owner-transition evacuation");
    for (int tick = 0; tick < 800 && !probe.pendingTransportTargets.empty(); ++tick) {
        ++probe.tick;
        advanceRuntimeSimulation(probe);
    }
    if (!powerMatches(5)) return fail("refill after evacuation did not complete");
    host = findMutableObjectById(probe, 1);
    destroyRuntimeTransportPassengers(probe, *host, "Destroyed");
    host->alive = false;
    host->health = 0;
    rebuildRuntimeEconomyPower(probe);
    if (probe.economy.powerProduced != 0) return fail("destroyed reactor retained its power");
    for (int id = 2; id <= 6; ++id) {
        if (findRuntimeObjectByIdIncludingInactive(probe, id)->health != 0) {
            return fail("destroyed reactor left an orphaned occupant");
        }
    }

    // Decode every original absorb frame using the actual UI palette.
    constexpr int scale = 3, tile = 56, width = 9 * tile * scale, height = 48 * scale;
    std::vector<std::uint32_t> pixels(width * height, 0x00242B32U);
    for (int index = 0; index < 9; ++index) {
        auto* entry = loadRuntimeUiShpFrame(live, "", "mouse", 422 + index);
        const auto* palette = runtimeUiPaletteForEntry(live, entry, "mousepal");
        if (entry == nullptr || palette == nullptr || entry->image.pixels.empty()) {
            return fail("original absorption cursor frame " + std::to_string(422 + index) + " missing");
        }
        const auto& frame = entry->image;
        for (int y = 0; y < frame.height; ++y) for (int x = 0; x < frame.width; ++x) {
            const auto colorIndex = frame.pixels[y * frame.width + x];
            if (colorIndex == 0 || colorIndex >= palette->size()) continue;
            const auto& color = (*palette)[colorIndex];
            for (int sy = 0; sy < scale; ++sy) for (int sx = 0; sx < scale; ++sx) {
                const int px = (index * tile + x + frame.offsetX) * scale + sx;
                const int py = (y + frame.offsetY) * scale + sy;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    pixels[py * width + px] = (color.r << 16) | (color.g << 8) | color.b;
                }
            }
        }
    }
    if (output != nullptr && !writeRuntimeSmokeBmp(std::string(output) + "/absorb-cursor.bmp",
        pixels.data(), width, height, error)) return false;
    std::cout << "Bio reactor smoke passed: " << infantryTypesChecked
        << " infantry types, 150..650 power, six normal/damaged visual levels, "
        << "hover occupant count and original person pips, reservation/arrival/unload/save/AI, original cursor.\n";
    return true;
}
