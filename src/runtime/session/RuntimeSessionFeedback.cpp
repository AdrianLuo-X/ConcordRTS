#include "runtime/session/detail/RuntimeProductionOperations.h"

namespace concord::runtime::session_detail {

bool runtimePresentationReferenceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("CONCORD_PRESENTATION_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

std::uint64_t runtimeWorldAudioClockNow(const RuntimeProductionHostState& state) noexcept {
    return state.worldAudioClock->nowMs();
}

long long runtimeSidebarPowerAnimationElapsedSteps(
    const RuntimeProductionHostState& state) {
    const RuntimeSidebarPowerAnimation& animation =
        state.sidebarPowerAnimation;
    const long long elapsedTicks = std::max(
        0LL,
        static_cast<long long>(state.tick)
            - static_cast<long long>(animation.startTick)
    );
    const long long tickRate = std::max(1, state.config.fixedTickHz);
    return (elapsedTicks * 1000LL)
        / (tickRate * runtimeSidebarPowerAnimationStepMs);
}

int runtimeAdvanceSidebarPowerValue(
    int start,
    int target,
    long long elapsedSteps) {
    if (elapsedSteps <= 0 || start == target) {
        return start;
    }
    const long long distance = std::abs(
        static_cast<long long>(target) - static_cast<long long>(start)
    );
    const long long travel = std::min(
        distance,
        elapsedSteps * runtimeSidebarPowerAnimationUnitsPerStep
    );
    const long long value = static_cast<long long>(start)
        + (target > start ? travel : -travel);
    return static_cast<int>(value);
}

RuntimeSidebarPowerDisplay runtimeSidebarPowerDisplay(
    const RuntimeProductionHostState& state) {
    const RuntimeSidebarPowerAnimation& animation =
        state.sidebarPowerAnimation;
    if (!animation.initialized) {
        return {
            state.economy.powerProduced,
            state.economy.powerUsed,
            false
        };
    }

    const long long elapsedSteps =
        runtimeSidebarPowerAnimationElapsedSteps(state);
    const int produced = runtimeAdvanceSidebarPowerValue(
            animation.startProduced,
            animation.targetProduced,
            elapsedSteps
        );
    const int used = runtimeAdvanceSidebarPowerValue(
            animation.startUsed,
            animation.targetUsed,
            elapsedSteps
        );
    return {produced, used, produced != animation.targetProduced
        || used != animation.targetUsed};
}

void snapRuntimeSidebarPowerAnimation(RuntimeProductionHostState& state) {
    RuntimeSidebarPowerAnimation& animation = state.sidebarPowerAnimation;
    animation.initialized = true;
    animation.startProduced = state.economy.powerProduced;
    animation.startUsed = state.economy.powerUsed;
    animation.targetProduced = state.economy.powerProduced;
    animation.targetUsed = state.economy.powerUsed;
    animation.startTick = state.tick;
}

void retargetRuntimeSidebarPowerAnimation(
    RuntimeProductionHostState& state,
    int produced,
    int used) {
    RuntimeSidebarPowerAnimation& animation = state.sidebarPowerAnimation;
    if (!animation.initialized) {
        return;
    }

    produced = std::max(0, produced);
    used = std::max(0, used);
    if (animation.targetProduced == produced
        && animation.targetUsed == used) {
        return;
    }

    const RuntimeSidebarPowerDisplay current =
        runtimeSidebarPowerDisplay(state);
    animation.startProduced = current.produced;
    animation.startUsed = current.used;
    animation.targetProduced = produced;
    animation.targetUsed = used;
    animation.startTick = state.tick;
    state.repaintRequested = true;
}

bool runtimeSidebarPowerAnimationActive(const RuntimeProductionHostState& state) {
    return runtimeSidebarPowerDisplay(state).animating;
}

RuntimeIniSource findRuntimeSoundIniSource(
    const RuntimeGameSessionState& state) {
    return resolveRuntimeIniSource(
        state.config,
        RuntimeCampaignIniKind::Sound
    );
}

RuntimeIniSource findRuntimeEvaIniSource(
    const RuntimeGameSessionState& state) {
    return resolveRuntimeIniSource(
        state.config,
        RuntimeCampaignIniKind::Eva
    );
}

RuntimeWorldAudioContext runtimeWorldAudioContext(
    const RuntimeProductionHostState& state) {
    RuntimeWorldAudioContext context;
    context.catalog = &state.worldAudioCatalog;
    RuntimeCellPoint listener{-1, -1};
    const RuntimeInputPixelRect viewport = state.cameraClampViewport;
    if (viewport.right > viewport.left && viewport.bottom > viewport.top) {
        RuntimeInputPixelPoint center{
            viewport.left + (viewport.right - viewport.left) / 2,
            viewport.top + (viewport.bottom - viewport.top) / 2
        };
        const RuntimeInputPixelPoint cell = approximateCellFromScreenPixelPoint(
            state,
            viewport,
            center
        );
        listener = RuntimeCellPoint{cell.x, cell.y};
    } else if (!state.world.getWaypoints().empty()) {
        listener = RuntimeCellPoint{
            state.world.getWaypoints().front().x,
            state.world.getWaypoints().front().y
        };
    } else if (!state.world.getTerrainCells().empty()) {
        listener = RuntimeCellPoint{
            state.world.getTerrainCells().front().x,
            state.world.getTerrainCells().front().y
        };
    }
    if (listener.x >= 0 && listener.y >= 0) {
        context.listenerCellValid = true;
        context.listenerCell.x = listener.x;
        context.listenerCell.y = listener.y;
        const auto terrain = state.terrainByCell.find(
            runtimeCellKey(listener.x, listener.y)
        );
        context.listenerCell.level = terrain != state.terrainByCell.end()
                && terrain->second != nullptr
            ? terrain->second->level
            : 0;
    }
    return context;
}

void initializeRuntimeWorldAudio(RuntimeProductionHostState& state) {
    state.worldAudioCatalog = RuntimeWorldAudioCatalog{};
    state.worldAudioCatalogLoaded = false;
    if (state.soundLoaded) {
        std::string catalogError;
        state.worldAudioCatalogLoaded = state.worldAudioCatalog.loadFromIni(
            state.soundIni,
            &catalogError,
            nullptr
        );
        if (!state.worldAudioCatalogLoaded && !catalogError.empty()) {
            state.lastRuntimeSound = "world audio catalog: " + catalogError;
        }
    }
    state.worldAudio.reset(RuntimeWorldAudioConfig{
        std::max(1, state.config.fixedTickHz),
        4096U
    });
    state.worldAudioInitialized = true;
}

bool loadRuntimeSoundDatabase(RuntimeProductionHostState& state) {
    stopRuntimeWorldAudioBackend(state);
    stopRuntimeSfxPlayback(state);
    state.creditAudio.samplesPrepared = false;
    state.soundLoaded = false;
    state.soundPath.clear();
    state.evaLoaded = false;
    state.evaPath.clear();
    state.runtimeEvaQueue.clear();
    state.runtimeEvaBusyUntilTick = 0;
    state.runtimeEvaSubtitleEvents = 0;
    state.runtimeEvaActivePriority = -1;
    state.runtimeEvaMciChannelActive = false;
    state.runtimeEvaUsingPlaySoundFallback = false;
    state.runtimeEvaSubtitleUntilTick = 0;
    state.runtimeEvaSubtitleSection.clear();
    state.runtimeEvaSubtitleText.clear();
    state.runtimeSfxMciChannelActive.fill(false);
    state.runtimeSfxBusyUntilTick.fill(0);
    state.runtimeNextSfxChannel = 0;
    state.runtimeSfxChannelSteals = 0;
    state.runtimeSoundSamplePathCache.clear();
    state.runtimeSoundSamplePathCacheLookups = 0;
    state.runtimeSoundSamplePathCacheHits = 0;
    state.runtimeSoundSamplePathNegativeHits = 0;
    state.runtimeSoundSamplePathResolverCalls = 0;

    const RuntimeIniSource soundSource = findRuntimeSoundIniSource(state);
    if (soundSource.available()) {
        IniFile soundIni;
        if (soundSource.load(soundIni)) {
            state.soundIni = std::move(soundIni);
            state.soundLoaded = true;
            state.soundPath = soundSource.displayPath;
        }
    }

    const RuntimeIniSource evaSource = findRuntimeEvaIniSource(state);
    if (evaSource.available()) {
        IniFile evaIni;
        if (evaSource.load(evaIni)) {
            state.evaIni = std::move(evaIni);
            state.evaLoaded = true;
            state.evaPath = evaSource.displayPath;
        }
    }

    if (!state.soundLoaded && !state.evaLoaded) {
        state.lastRuntimeSound = "sound and EVA INIs missing";
    } else if (!state.soundLoaded) {
        state.lastRuntimeSound = "sound INI missing";
    } else if (!state.evaLoaded) {
        state.lastRuntimeSound = "EVA INI missing";
    }
    initializeRuntimeWorldAudio(state);
    return state.soundLoaded;
}

std::string stripRuntimeSoundComment(const std::string& value) {
    const std::size_t comment = value.find(';');
    return trimAsciiLocal(
        comment == std::string::npos ? value : value.substr(0, comment)
    );
}

bool isRuntimeSemanticSidebarSoundIntent(const std::string& intent) {
    return intent == runtimeSoundIntentSidebarButtonClick
        || intent == runtimeSoundIntentSidebarTabClick
        || intent == runtimeSoundIntentBuildQueued
        || intent == runtimeSoundIntentBuildStarted
        || intent == runtimeSoundIntentTrainingStarted
        || intent == runtimeSoundIntentBuildHeld
        || intent == runtimeSoundIntentBuildComplete
        || intent == runtimeSoundIntentBuildCanceled
        || intent == runtimeSoundIntentPlacementConfirmed
        || intent == runtimeSoundIntentCannotDeployHere
        || intent == runtimeSoundIntentSidebarScold
        || intent == runtimeSoundIntentLowPower
        || intent == runtimeSoundIntentPowerOn
        || intent == runtimeSoundIntentPowerOff
        || intent == runtimeSoundIntentInsufficientFunds
        || intent == runtimeSoundIntentUnitReady
        || intent == runtimeSoundIntentRadarOn
        || intent == runtimeSoundIntentRadarOff
        || intent == runtimeSoundIntentNewConstructionOptions
        || intent == runtimeSoundIntentBaseUnderAttack
        || intent == runtimeSoundIntentOreMinerUnderAttack
        || intent == runtimeSoundIntentUnitLost
        || intent == runtimeSoundIntentBuildingCaptured
        || intent == runtimeSoundIntentStructureGarrisoned
        || intent == runtimeSoundIntentStructureGarrisonedSfx
        || intent == runtimeSoundIntentBridgeRepaired
        || intent == runtimeSoundIntentBuildingRepaired
        || intent == runtimeSoundIntentStructureRepaired
        || intent == runtimeSoundIntentSellStructure
        || intent == runtimeSoundIntentMenuOpen
        || intent == runtimeSoundIntentMenuClose
        || intent == "repair"
        || intent == "sell";
}

std::string runtimeRulesSoundSection(
    const RuntimeGameSessionState& state,
    const std::string& key,
    const std::string& fallback) {
    const std::string configured =
        stripRuntimeSoundComment(state.rules.getAudioVisualValue(key));
    return configured.empty() ? fallback : configured;
}

std::string runtimeEvaSectionFromEvent(const std::string& eventName) {
    const std::string normalized =
        toLowerAsciiLocal(trimAsciiLocal(eventName));
    if (normalized == "buildstartedeva" || normalized == "building") {
        return "EVA_Building";
    }
    if (normalized == "trainingstartedeva" || normalized == "training") {
        return "EVA_Training";
    }
    if (normalized == "buildheld" || normalized == "hold") {
        return "EVA_OnHold";
    }
    if (normalized == "buildcomplete" || normalized == "complete") {
        return "EVA_ConstructionComplete";
    }
    if (normalized == "buildcanceled" || normalized == "cancel") {
        return "EVA_Canceled";
    }
    if (normalized == "cannotdeployhere") {
        return "EVA_CannotDeployHere";
    }
    if (normalized == "lowpower") {
        return "EVA_LowPower";
    }
    if (normalized == "insufficientfunds") {
        return "EVA_InsufficientFunds";
    }
    if (normalized == "unitready" || normalized == "ready") {
        return "EVA_UnitReady";
    }
    if (normalized == "repairing") {
        return "EVA_Repairing";
    }
    if (normalized == "structuresold") {
        return "EVA_StructureSold";
    }
    if (normalized == "unitlost") {
        return "EVA_UnitLost";
    }
    if (normalized == "baseunderattack") {
        return "EVA_OurBaseIsUnderAttack";
    }
    if (normalized == "oreminerunderattack") {
        return "EVA_OreMinerUnderAttack";
    }
    if (normalized == "buildingcaptured") {
        return "EVA_BuildingCaptured";
    }
    if (normalized == "structuregarrisoned") {
        return "EVA_StructureGarrisoned";
    }
    if (normalized == "structurerepaired") {
        return "EVA_StructureRepaired";
    }
    if (normalized == "buildingoffline") {
        return "EVA_BuildingOffLine";
    }
    if (normalized == "buildingonline") {
        return "EVA_BuildingOnLine";
    }
    if (normalized == "newconstructionoptions") {
        return "EVA_NewConstructionOptions";
    }
    if (normalized == "selecttarget") {
        return "EVA_SelectTarget";
    }
    if (normalized.rfind("eva_", 0) == 0) {
        return trimAsciiLocal(eventName);
    }
    return "";
}

std::string runtimeSoundSectionFromEvent(
    const RuntimeGameSessionState& state,
    const std::string& eventName) {
    const std::size_t separator = eventName.find(':');
    if (separator != std::string::npos) {
        return trimAsciiLocal(eventName.substr(separator + 1));
    }

    const std::string evaSection = runtimeEvaSectionFromEvent(eventName);
    if (!evaSection.empty()) {
        return evaSection;
    }

    const std::string normalized =
        toLowerAsciiLocal(trimAsciiLocal(eventName));
    if (normalized == "queue") {
        return runtimeRulesSoundSection(
            state,
            "GUIBuildSound",
            "MenuClick"
        );
    }
    if (normalized == "sidebarbuttonclick") {
        return runtimeRulesSoundSection(
            state,
            "GUIMainButtonSound",
            "MenuClick"
        );
    }
    if (normalized == "sidebartabclick") {
        return runtimeRulesSoundSection(state, "GUITabSound", "MenuTab");
    }
    if (normalized == "buildqueued") {
        return runtimeRulesSoundSection(
            state,
            "GUIBuildSound",
            "MenuClick"
        );
    }
    if (normalized == "placementconfirmed") {
        return runtimeRulesSoundSection(
            state,
            "BuildingSlam",
            "PlaceBuilding"
        );
    }
    if (normalized == "sidebarscold") {
        return runtimeRulesSoundSection(state, "ScoldSound", "MenuScold");
    }
    if (normalized == "poweron") {
        return "PowerOn";
    }
    if (normalized == "poweroff") {
        return "PowerOff";
    }
    if (normalized == "radaron") {
        return runtimeRulesSoundSection(state, "RadarOn", "RadarOn");
    }
    if (normalized == "radaroff") {
        return runtimeRulesSoundSection(state, "RadarOff", "RadarOff");
    }
    if (normalized == "buildingrepaired") {
        return runtimeRulesSoundSection(
            state,
            "BuildingRepairedSound",
            "BuildingRepaired"
        );
    }
    if (normalized == "buildingdamaged") {
        return runtimeRulesSoundSection(
            state,
            "BuildingDamageSound",
            "BuildingDamaged"
        );
    }
    if (normalized == "buildingdestroyed") {
        return runtimeRulesSoundSection(
            state,
            "BuildingDieSound",
            "BuildingGenericDie"
        );
    }
    if (normalized == "bridgerepaired") {
        return runtimeRulesSoundSection(
            state,
            "RepairBridgeSound",
            "BridgeRepaired"
        );
    }
    if (normalized == "structuregarrisonedsfx") {
        return runtimeRulesSoundSection(
            state,
            "BuildingGarrisonedSound",
            "BuildingGarrisoned"
        );
    }
    if (normalized == "sellstructure") {
        return runtimeRulesSoundSection(state, "SellSound", "SellBuilding");
    }
    if (normalized == "baseunderattacksiren") {
        return runtimeRulesSoundSection(
            state,
            "BaseUnderAttackSound",
            "BaseUnderAttackSiren"
        );
    }
    if (normalized == "menuopen") {
        return runtimeRulesSoundSection(state, "GUIOpenSound", "MenuACBOpen");
    }
    if (normalized == "menuclose") {
        return runtimeRulesSoundSection(state, "GUICloseSound", "MenuACBClose");
    }
    if (normalized == "place") {
        return runtimeRulesSoundSection(
            state,
            "BuildingSlam",
            "PlaceBuilding"
        );
    }
    if (normalized == "blocked") {
        return runtimeRulesSoundSection(state, "ScoldSound", "MenuScold");
    }
    if (normalized == "sell" || normalized == "repair") {
        return runtimeRulesSoundSection(
            state,
            "GUITabSound",
            "MenuTab"
        );
    }
    if (normalized == "harvest") {
        return "OreTruckHarvest";
    }
    if (normalized == "harvestunload" || normalized == "harvest unload") {
        return "OreRefineryProcessing";
    }
    if (normalized == "slavesfreed") {
        return runtimeRulesSoundSection(
            state,
            "SlavesFreeSound",
            "SlaveWorkerLiberated"
        );
    }
    if (normalized == "save" || normalized == "load") {
        return runtimeRulesSoundSection(
            state,
            "GUIMainButtonSound",
            "MenuClick"
        );
    }
    if (normalized == "warning") {
        return "WarningAlarm";
    }
    if (normalized == "command move" || normalized == "command target") {
        return "CommandBar";
    }
    if (normalized == "command stop") {
        return runtimeRulesSoundSection(state, "StopSound", "CommandBar");
    }
    if (normalized == "command guard") {
        return runtimeRulesSoundSection(state, "GuardSound", "CommandBar");
    }
    if (normalized == "command scatter") {
        return runtimeRulesSoundSection(state, "ScatterSound", "CommandBar");
    }
    return trimAsciiLocal(eventName);
}

bool runtimeSoundSectionIsEva(
    const RuntimeProductionHostState& state,
    const std::string& sectionName) {
    const std::string section = trimAsciiLocal(sectionName);
    return toLowerAsciiLocal(section).rfind("eva_", 0) == 0
        || (state.evaLoaded && state.evaIni.getSection(section) != nullptr);
}

std::string runtimeEvaSubtitleTextFromSection(
    const RuntimeProductionHostState& state,
    const std::string& sectionName) {
    if (!state.evaLoaded || trimAsciiLocal(sectionName).empty()) {
        return {};
    }
    return trimAsciiLocal(
        state.evaIni.getValue(sectionName, "Text", "")
    );
}

int runtimeSoundVolumePercent(
    const RuntimeProductionHostState& state,
    const std::string& sectionName) {
    const bool eva = runtimeSoundSectionIsEva(state, sectionName);
    const int authoredVolume = !state.soundLoaded || eva
        ? 100
        : std::clamp(
            parseRuntimeIntOrDefault(
                stripRuntimeSoundComment(
                    state.soundIni.getValue(sectionName, "Volume", "100")
                ),
                100
            ),
            0,
            100
        );
    const int userVolume = eva
        ? state.runtimeVoiceVolume
        : state.runtimeSoundVolume;
    return std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(authoredVolume)
                * static_cast<double>(userVolume)
                / 100.0
        )),
        0,
        100
    );
}

std::string runtimeEvaVoiceKeyForOwner(const RuntimeGameSessionState& state) {
    const std::string owner =
        toUpperAsciiLocal(
            runtimeCountryForOwner(state, state.economy.owner)
        );
    if (owner == "YURI" || owner == "YURICOUNTRY") {
        return "Yuri";
    }
    if (owner == "RUSSIANS"
        || owner == "CONFEDERATION"
        || owner == "AFRICANS"
        || owner == "ARABS"
        || owner == "SOVIET") {
        return "Russian";
    }

    for (const SideDefinition& side : state.rules.getSides()) {
        bool ownerInSide = false;
        for (const std::string& member : side.members) {
            if (toUpperAsciiLocal(trimAsciiLocal(member)) == owner) {
                ownerInSide = true;
                break;
            }
        }
        if (!ownerInSide) {
            continue;
        }

        const std::string sideName =
            toLowerAsciiLocal(trimAsciiLocal(side.name));
        if (sideName.find("yuri") != std::string::npos
            || sideName.find("third") != std::string::npos) {
            return "Yuri";
        }
        if (sideName.find("sov") != std::string::npos
            || sideName.find("nod") != std::string::npos
            || sideName.find("russ") != std::string::npos) {
            return "Russian";
        }
    }
    return "Allied";
}

AudioBackgroundMusicFaction runtimeMusicFaction(
    const RuntimeGameSessionState& state) {
    const std::string voice = runtimeEvaVoiceKeyForOwner(state);
    if (voice == "Russian") {
        return AudioBackgroundMusicFaction::Soviet;
    }
    if (voice == "Yuri") {
        return AudioBackgroundMusicFaction::Yuri;
    }
    return AudioBackgroundMusicFaction::Allied;
}

std::string runtimeMusicFactionName(AudioBackgroundMusicFaction faction) {
    if (faction == AudioBackgroundMusicFaction::Soviet) {
        return "Soviet";
    }
    if (faction == AudioBackgroundMusicFaction::Yuri) {
        return "Yuri";
    }
    if (faction == AudioBackgroundMusicFaction::Allied) {
        return "Allied";
    }
    return "Any";
}

std::uint64_t advanceRuntimeMusicRandom(std::uint64_t& state) {
    // xorshift64* gives a compact independent RNG for presentation-only music;
    // it never consumes the deterministic simulation random stream.
    if (state == 0) {
        state = 0x9E3779B97F4A7C15ULL;
    }
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    return state * 0x2545F4914F6CDD1DULL;
}

std::size_t chooseRuntimeMusicIndex(
    std::size_t trackCount,
    std::optional<std::size_t> previousIndex,
    std::uint64_t& randomState) {
    if (trackCount == 0U) {
        return 0U;
    }
    std::size_t selected = static_cast<std::size_t>(
        advanceRuntimeMusicRandom(randomState) % trackCount
    );
    if (trackCount > 1U
        && previousIndex.has_value()
        && selected == *previousIndex) {
        selected = (selected + 1U + static_cast<std::size_t>(
            advanceRuntimeMusicRandom(randomState) % (trackCount - 1U)
        )) % trackCount;
    }
    return selected;
}

void prepareRuntimeMusicPlaylist(RuntimeProductionHostState& state) {
    if (!state.runtimeMusicThemeIds.empty()) {
        return;
    }
    const std::string assetRootPath = runtimeAssetRootPath(state.config);
    if (assetRootPath.empty()) {
        return;
    }
    state.runtimeMusicThemeIds = collectBackgroundMusicThemeIds(
        assetRootPath,
        runtimeMusicFaction(state)
    );
    if (state.runtimeMusicThemeIds.empty()) {
        state.runtimeMusicThemeIds = collectBackgroundMusicThemeIds(
            assetRootPath,
            AudioBackgroundMusicFaction::Any
        );
    }
    if (state.runtimeMusicRandomState == 0) {
        std::uint64_t seed = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count()
        );
        const auto mixText = [&seed](const std::string& text) {
            for (unsigned char character : text) {
                seed ^= static_cast<std::uint64_t>(character);
                seed *= 1099511628211ULL;
            }
        };
        mixText(state.config.playerCountry);
        mixText(state.config.defaultMapPath);
        seed ^= state.constructionUiClock->nowMs();
        state.runtimeMusicRandomState = seed == 0
            ? 0x9E3779B97F4A7C15ULL
            : seed;
    }
}

std::string runtimeEvaFallbackSample(
    const std::string& sectionName,
    const std::string& voiceKey) {
    static const std::unordered_map<std::string, std::string> suffixBySection = {
        {"eva_constructioncomplete", "048"},
        {"eva_newconstructionoptions", "049"},
        {"eva_insufficientfunds", "050"},
        {"eva_canceled", "051"},
        {"eva_building", "052"},
        {"eva_lowpower", "053"},
        {"eva_ourbaseisunderattack", "054"},
        {"eva_oreminerunderattack", "037"},
        {"eva_primarybuildingselected", "055"},
        {"eva_onhold", "056"},
        {"eva_repairing", "057"},
        {"eva_structuresold", "058"},
        {"eva_buildingoffline", "060"},
        {"eva_buildingonline", "061"},
        {"eva_unitready", "062"},
        {"eva_cannotdeployhere", "063"},
        {"eva_unitlost", "064"},
        {"eva_selecttarget", "065"},
        {"eva_training", "066"},
        {"eva_buildingcaptured", "072"},
        {"eva_structuregarrisoned", "107"},
        {"eva_structurerepaired", "123"}
    };
    const auto suffix = suffixBySection.find(
        toLowerAsciiLocal(trimAsciiLocal(sectionName))
    );
    if (suffix == suffixBySection.end()) {
        return "";
    }

    const std::string lowerVoice =
        toLowerAsciiLocal(trimAsciiLocal(voiceKey));
    const char* prefix = lowerVoice == "russian"
        ? "csof"
        : (lowerVoice == "yuri" ? "cyur" : "ceva");
    return std::string(prefix) + suffix->second;
}

int runtimeEvaPriorityFromSection(
    const RuntimeProductionHostState& state,
    const std::string& sectionName) {
    if (!state.evaLoaded) {
        return 1;
    }
    const std::string priority = toUpperAsciiLocal(
        stripRuntimeSoundComment(
            state.evaIni.getValue(sectionName, "Priority", "NORMAL")
        )
    );
    if (priority == "CRITICAL") {
        return 3;
    }
    if (priority == "IMPORTANT" || priority == "HIGH") {
        return 2;
    }
    if (priority == "LOW") {
        return 0;
    }
    return 1;
}

bool runtimeEvaQueueRequestedBySection(
    const RuntimeProductionHostState& state,
    const std::string& sectionName) {
    if (!state.evaLoaded) {
        return false;
    }
    return toUpperAsciiLocal(
        stripRuntimeSoundComment(
            state.evaIni.getValue(sectionName, "Type", "")
        )
    ) == "QUEUE";
}

std::optional<std::string> resolveRuntimeSoundSampleName(
    const RuntimeProductionHostState& state,
    const std::string& eventName,
    const std::string& sectionName) {
    if (sectionName.empty()) {
        return std::nullopt;
    }

    if (runtimeSoundSectionIsEva(state, sectionName)) {
        const std::string voiceKey = runtimeEvaVoiceKeyForOwner(state);
        if (state.evaLoaded) {
            const std::vector<std::string> samples =
                splitRuntimeWhitespaceTokens(
                    state.evaIni.getValue(sectionName, voiceKey, "")
                );
            if (!samples.empty()) {
                return samples.front();
            }
        }

        const std::string fallback =
            runtimeEvaFallbackSample(sectionName, voiceKey);
        return fallback.empty()
            ? std::optional<std::string>{}
            : std::optional<std::string>{fallback};
    }

    if (state.soundLoaded) {
        const IniFile::Section* section = state.soundIni.getSection(sectionName);
        if (section != nullptr) {
            const std::vector<std::string> samples =
                splitRuntimeWhitespaceTokens(
                    state.soundIni.getValue(sectionName, "Sounds", "")
                );
            if (!samples.empty()) {
                const std::size_t index =
                    static_cast<std::size_t>(state.runtimeSoundEvents)
                    % samples.size();
                return samples[index];
            }
        }
    }

    (void)eventName;
    return sectionName;
}

std::optional<std::string> findRuntimeSoundSamplePath(
    RuntimeProductionHostState& state,
    const std::string& sampleName) {
    ++state.runtimeSoundSamplePathCacheLookups;
    const std::string campaignSourceId =
        normalizeRuntimeCampaignSourceId(state.config.campaignSourceId);
    const std::string cacheKey = campaignSourceId
        + "|"
        + toLowerAsciiLocal(trimAsciiLocal(sampleName));
    const auto cached = state.runtimeSoundSamplePathCache.find(cacheKey);
    if (cached != state.runtimeSoundSamplePathCache.end()) {
        ++state.runtimeSoundSamplePathCacheHits;
        if (!cached->second.has_value()) {
            ++state.runtimeSoundSamplePathNegativeHits;
        }
        return cached->second;
    }

    const std::string assetRootPath = runtimeAssetRootPath(state.config);
    if (assetRootPath.empty() || sampleName.empty()) {
        state.runtimeSoundSamplePathCache.emplace(cacheKey, std::nullopt);
        return std::nullopt;
    }

    ++state.runtimeSoundSamplePathResolverCalls;
    const std::filesystem::path cacheRoot =
        std::filesystem::path(state.config.localDataRootPath)
            / "runtime_audio_cache";
    const AudioSampleResolveResult resolved =
        resolveAudioSampleToWave(
            assetRootPath,
            pathString(cacheRoot),
            sampleName,
            campaignSourceId
    );
    if (resolved.success) {
        return state.runtimeSoundSamplePathCache
            .emplace(cacheKey, resolved.wavPath)
            .first->second;
    }

    state.runtimeSoundSamplePathCache.emplace(cacheKey, std::nullopt);
    return std::nullopt;
}

void rememberRuntimeSoundPlayback(
    RuntimeProductionHostState& state,
    RuntimeSoundPlaybackRecord record) {
    record.tick = state.tick;
    if (state.runtimeSoundPlaybackCapture != nullptr) {
        state.runtimeSoundPlaybackCapture->push_back(record);
    }
    state.runtimeSoundPlaybackHistory.push_back(std::move(record));
    constexpr std::size_t maxRuntimeSoundPlaybackHistory = 32;
    while (state.runtimeSoundPlaybackHistory.size()
        > maxRuntimeSoundPlaybackHistory) {
        state.runtimeSoundPlaybackHistory.pop_front();
    }
}

int runtimeEvaBusyTicksForSample(
    const std::optional<std::string>& samplePath,
    int fixedTickHz) {
    const int baseTicks = std::max(1, fixedTickHz);
    if (samplePath.has_value()) {
        return std::max(
            baseTicks,
            runtimeWavDurationTicks(*samplePath, fixedTickHz) + 2
        );
    }
    return baseTicks;
}

int runtimeEvaSubtitleDurationTicks(
    const std::string& text,
    const std::optional<std::string>& samplePath,
    int fixedTickHz) {
    const int tickHz = std::max(1, fixedTickHz);
    const int readingSeconds = std::clamp(
        2 + static_cast<int>((text.size() + 17U) / 18U),
        3,
        10
    );
    return std::max(
        readingSeconds * tickHz,
        runtimeEvaBusyTicksForSample(samplePath, tickHz)
    );
}

void clearRuntimeEvaSubtitle(RuntimeProductionHostState& state) {
    state.runtimeEvaSubtitleUntilTick = 0;
    state.runtimeEvaSubtitleSection.clear();
    state.runtimeEvaSubtitleText.clear();
    state.repaintRequested = true;
}

void presentRuntimeEvaSubtitle(
    RuntimeProductionHostState& state,
    const RuntimeSoundPlaybackRecord& record) {
    const std::string text = trimAsciiLocal(record.subtitleText);
    if (!record.eva || text.empty()) {
        return;
    }
    const std::optional<std::string> samplePath = record.path.empty()
        ? std::optional<std::string>{}
        : std::optional<std::string>{record.path};
    state.runtimeEvaSubtitleText = text;
    state.runtimeEvaSubtitleSection = record.section;
    state.runtimeEvaSubtitleUntilTick = state.tick
        + runtimeEvaSubtitleDurationTicks(
            text,
            samplePath,
            state.config.fixedTickHz
        );
    ++state.runtimeEvaSubtitleEvents;
    state.repaintRequested = true;
}

void closeRuntimeEvaMciChannel() {
    runtimeEvaWaveDevice().stopAndClose();
}

RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForObject(const WorldObjectInstance& object) {
    return {object.hasRenderPosition ? object.renderX : double(object.x),
        object.hasRenderPosition ? object.renderY : double(object.y),
        object.renderMoveActive ? object.renderLevel : double(object.level)};
}

RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForImpact(const RuntimeImpactEffect& impact) {
    return {impact.hasWorldPosition ? impact.worldX : double(impact.cell.x),
        impact.hasWorldPosition ? impact.worldY : double(impact.cell.y),
        impact.hasWorldPosition ? double(impact.worldLevel) : double(impact.level)};
}

RuntimeSpatialAudioResult runtimeSpatialAudioAt(const RuntimeProductionHostState& state,
    const RuntimeSpatialAudioRules& rules, const RuntimeSpatialAudioPosition& source) {
    const bool valid = std::isfinite(source.x) && std::isfinite(source.y) && std::isfinite(source.level)
        && source.x >= -1048576 && source.x <= 1048576 && source.y >= -1048576 && source.y <= 1048576
        && source.level >= -1048576 && source.level <= 1048576;
    if (!valid) return {};
    const auto projected = runtimeProjectCellXY(source.x, source.y, source.level, state.camera);
    double width = state.cameraClampViewport.right - state.cameraClampViewport.left;
    double height = state.cameraClampViewport.bottom - state.cameraClampViewport.top;
    double x = double(projected.x) - state.camera.worldOffsetX;
    double y = double(projected.y) - state.camera.worldOffsetY;
    if (width <= 0 || height <= 0) {
        // No window during command smoke/loading: preserve the established
        // waypoint listener, with an explicit 640x400 reference viewport.
        const auto context = runtimeWorldAudioContext(state);
        if (!context.listenerCellValid) return {};
        const auto center = runtimeProjectCellXY(context.listenerCell.x,
            context.listenerCell.y, context.listenerCell.level, state.camera);
        width = 640; height = 400;
        x = projected.x - center.x + width / 2; y = projected.y - center.y + height / 2;
    }
    std::string observer = state.economy.owner;
    for (const auto& house : state.humanHouseAuthorities) if (house.localFacade) {
        observer = house.canonicalOwner; break;
    }
    const bool mapped = !state.visionEnabled || state.vision.visibilityFor(observer,
        static_cast<int>(source.x), static_cast<int>(source.y)) != RuntimeCellVisibility::Shrouded;
    return runtimeSpatialAudioAtProjection(rules, x, y, width, height, mapped,
        std::max(1, state.camera.tileWidth) / 60.0);
}

RuntimeSpatialAudioResult runtimeSpatialAudioAt(const RuntimeProductionHostState& state,
    const std::string& event, const RuntimeSpatialAudioPosition& source) {
    const auto* sound = state.worldAudioCatalogLoaded ? state.worldAudioCatalog.find(event) : nullptr;
    return runtimeSpatialAudioAt(state, sound ? sound->spatial : runtimeSpatialAudioRules(state.soundIni, event), source);
}

int runtimeSpatialAudioCombinedGain(const RuntimeProductionHostState& state, int authoredPermille, int spatial16384) {
    const auto authored = std::clamp(authoredPermille, 0, 1000) * 16384 / 1000;
    const auto user = std::clamp(state.runtimeSoundVolume, 0, 100) * 16384 / 100;
    return int((std::int64_t(authored) * std::clamp(spatial16384, 0, 16384) / 16384) * user / 16384);
}

int runtimeSpatialAudioGainPermille(const RuntimeProductionHostState& state,
    const RuntimeWorldAudioSoundSpec& sound, const RuntimeSpatialAudioPosition& source) {
    if (!state.runtimeSoundEffectsEnabled || state.runtimeSoundVolume <= 0) return 0;
    return std::clamp(sound.volumePercent, 0, 100) * 10
        * runtimeSpatialAudioAt(state, sound.spatial, source).gain16384 / 16384;
}

std::uint64_t runtimeSfxVoiceHandle(std::size_t channel) {
    return 0x6000000000000000ULL | channel;
}

// Player communication already supplies its independent voice setting.
bool playRuntimeSfxOnDedicatedChannel(RuntimeProductionHostState& state, const std::string& path, int volume) {
    RuntimeSoundPlaybackRecord record; record.volumePercent = std::clamp(volume, 0, 100);
    record.gain16384 = record.volumePercent * 16384 / 100;
    record.authoredGainPermille = -1;
    return playRuntimeSfxOnDedicatedChannel(state, path, record);
}

bool playRuntimeSfxOnDedicatedChannel(RuntimeProductionHostState& state,
    const std::string& path, const RuntimeSoundPlaybackRecord& record) {
    RuntimePerfTraceScope trace("playRuntimeSfxOnDedicatedChannel", state.tick);
    RuntimeSfxVoiceBackend backend{runtimeVoiceAudioFor(state)};
    const bool played = playRuntimeSfxPooled(state.runtimeSfxMciChannelCache,
        state.runtimeSfxMciChannelActive, state.runtimeSfxBusyUntilTick,
        state.runtimeNextSfxChannel, state.runtimeSfxChannelSteals,
        state.tick, state.config.fixedTickHz, path, record.volumePercent, backend,
        record.pan16384, record.gain16384);
    if (played && record.gain16384 > 0) {
        const auto channel = (state.runtimeNextSfxChannel + runtimeSfxChannelCount - 1) % runtimeSfxChannelCount;
        state.runtimeSfxSpatialRecords[channel] = record;
    }
    return played;
}

void stopRuntimeSfxPlayback(RuntimeProductionHostState& state) {
    if (state.runtimeVoiceAudio) {
        RuntimeSfxVoiceBackend backend{*state.runtimeVoiceAudio};
        stopRuntimeSfxPooled(state.runtimeSfxMciChannelCache,
            state.runtimeSfxMciChannelActive, state.runtimeSfxBusyUntilTick, backend);
    } else {
        state.runtimeSfxMciChannelCache = {}; state.runtimeSfxMciChannelActive = {};
        state.runtimeSfxBusyUntilTick = {};
    }
    state.runtimeSfxSpatialRecords = {};
}

void applyRuntimeAudioVariation(RuntimeProductionHostState& state, RuntimeSoundPlaybackRecord& record) {
    if (record.eva || !state.soundLoaded) return;
    const auto* catalogSound = state.worldAudioCatalogLoaded
        ? state.worldAudioCatalog.find(record.section) : nullptr;
    const auto spec = catalogSound ? catalogSound->variation
        : runtimeAudioVariationRules(state.soundIni, record.section);
    const auto variation = runtimeAudioVariationFor(spec,
        runtimeAudioVariationSeed(record.section,
            static_cast<std::uint64_t>(state.runtimeSoundEvents), state.tick));
    const int authoredVolume = catalogSound ? catalogSound->volumePercent
        : std::clamp(parseRuntimeIntOrDefault(stripRuntimeSoundComment(
            state.soundIni.getValue(record.section, "Volume",
                state.soundIni.getValue("Defaults", "Volume", "80"))), 80), 0, 100);
    const int varied = std::clamp(authoredVolume * 10 + variation.volumeDeltaPermille, 0, 1000);
    record.authoredGainPermille = varied;
    record.gain16384 = runtimeSpatialAudioCombinedGain(state, varied, 16384);
    record.volumePercent = std::clamp((varied * state.runtimeSoundVolume + 500) / 1000, 0, 100);
    if (!record.path.empty() && state.runtimeSoundsEnabled) {
        record.path = runtimeAudioPitchVariantPath(record.path,
            std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache",
            variation.frequencyPermille);
    }
}

std::optional<bool> runtimeEvaMciChannelIsPlaying() {
    return runtimeEvaWaveDevice().isPlaying();
}

bool playRuntimeEvaOnDedicatedChannel(
    const std::string& samplePath,
    int volumePercent) {
    return runtimeEvaWaveDevice().playFromStart(samplePath, volumePercent);
}

void stopRuntimeEvaPlayback(RuntimeProductionHostState& state) {
    closeRuntimeEvaMciChannel();
    if (state.runtimeEvaUsingPlaySoundFallback) {
        (void)runtimeAsyncSoundDevice().stopGlobal();
    }
    state.runtimeEvaMciChannelActive = false;
    state.runtimeEvaUsingPlaySoundFallback = false;
    state.runtimeEvaBusyUntilTick = 0;
    state.runtimeEvaActivePriority = -1;
    clearRuntimeEvaSubtitle(state);
}

void enforceRuntimeSoundVoiceLimit(RuntimeProductionHostState& state,
    const std::string& event, std::uint64_t excludedHandle) {
    if (!state.runtimeVoiceAudio) return;
    const auto* spec = state.worldAudioCatalog.find(event);
    if (!spec || spec->simultaneousLimit <= 0) return;
    struct PlayingSound { std::uint64_t handle; int tick; std::uint64_t order; int channel; };
    std::vector<PlayingSound> playing;
    const auto key = toLowerAsciiLocal(spec->eventId);
    for (std::size_t channel = 0; channel < runtimeSfxChannelCount; ++channel) {
        const auto handle = runtimeSfxVoiceHandle(channel);
        const auto& previous = state.runtimeSfxSpatialRecords[channel];
        if (handle != excludedHandle && toLowerAsciiLocal(previous.section) == key
            && state.runtimeVoiceAudio->isPlaying(handle).value_or(false))
            playing.push_back({handle, previous.tick, previous.playbackOrder, int(channel)});
    }
    for (const auto& [handle, binding] : state.runtimeWorldAudioBackendBindings)
        if (handle != excludedHandle && toLowerAsciiLocal(binding.action.eventId) == key
            && state.runtimeVoiceAudio->isPlaying(handle).value_or(false))
            playing.push_back({handle, binding.action.eventTick, binding.playbackOrder, -1});
    while (playing.size() >= std::size_t(spec->simultaneousLimit)) {
        const auto oldest = std::min_element(playing.begin(), playing.end(), [](const auto& a, const auto& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.order != b.order ? a.order < b.order : a.handle < b.handle;
        });
        state.runtimeVoiceAudio->stop(oldest->handle);
        if (oldest->channel >= 0) {
            state.runtimeSfxMciChannelActive[oldest->channel] = false;
            state.runtimeSfxBusyUntilTick[oldest->channel] = 0;
        } else {
            const auto binding = state.runtimeWorldAudioBackendBindings.find(oldest->handle);
            if (binding != state.runtimeWorldAudioBackendBindings.end()
                && binding->second.action.sourceObjectId >= 0 && binding->second.action.queueAfterCurrent)
                state.runtimeWorldAudioLimitEvictedHandles.insert(oldest->handle);
            state.runtimeVoiceAudio->close(oldest->handle);
            state.runtimeWorldAudioBackendBindings.erase(oldest->handle);
        }
        playing.erase(oldest);
    }
}

void playRuntimeResolvedSound(
    RuntimeProductionHostState& state,
    RuntimeSoundPlaybackRecord record) {
    if (record.eva) {
        // A queued EVA record may outlive a menu volume change. Resolve the
        // current setting at playback time instead of replaying its stale
        // enqueue-time volume.
        record.volumePercent = runtimeSoundVolumePercent(state, record.section);
    }
    const std::optional<std::string> samplePath = record.path.empty()
        ? std::optional<std::string>{}
        : std::optional<std::string>{record.path};
    if (record.eva) {
        presentRuntimeEvaSubtitle(state, record);
    }
    if (record.eva && record.volumePercent <= 0) {
        record.status = "eva-subtitle-only-muted";
        state.lastRuntimeSound = "eva muted " + record.section;
        state.runtimeEvaMciChannelActive = false;
        state.runtimeEvaUsingPlaySoundFallback = false;
        if (!record.subtitleText.empty()) {
            state.runtimeEvaActivePriority = record.priority;
            state.runtimeEvaBusyUntilTick = std::max(
                state.tick + std::max(1, state.config.fixedTickHz),
                state.runtimeEvaSubtitleUntilTick
            );
        }
        rememberRuntimeSoundPlayback(state, std::move(record));
        return;
    }
    if (!samplePath.has_value()) {
        record.status = record.sample.empty() ? "no-sample" : "missing";
        state.lastRuntimeSound = "sound missing " + record.sample;
        if (record.eva) {
            state.runtimeEvaMciChannelActive = false;
            state.runtimeEvaUsingPlaySoundFallback = false;
            state.runtimeEvaActivePriority = record.priority;
            state.runtimeEvaBusyUntilTick =
                record.subtitleText.empty()
                    ? state.tick + std::max(1, state.config.fixedTickHz)
                    : std::max(
                        state.tick + std::max(1, state.config.fixedTickHz),
                        state.runtimeEvaSubtitleUntilTick
                    );
        }
        rememberRuntimeSoundPlayback(state, std::move(record));
        return;
    }

    if (!state.runtimeSoundsEnabled) {
        record.status = "resolved";
        state.lastRuntimeSound =
            "sound resolved "
            + std::filesystem::path(*samplePath).filename().string();
        if (record.eva) {
            state.runtimeEvaMciChannelActive = false;
            state.runtimeEvaUsingPlaySoundFallback = false;
            state.runtimeEvaActivePriority = record.priority;
            state.runtimeEvaBusyUntilTick =
                state.tick
                + runtimeEvaBusyTicksForSample(
                    samplePath,
                    state.config.fixedTickHz
                );
        }
        rememberRuntimeSoundPlayback(state, std::move(record));
        return;
    }

    if (!record.eva && record.gain16384 <= 0) {
        record.status = "muted";
        state.lastRuntimeSound = "sound muted " + record.section;
        rememberRuntimeSoundPlayback(state, std::move(record));
        return;
    }

    if (!record.eva) {
        record.playbackOrder = ++state.runtimeSoundVoiceOrder;
        enforceRuntimeSoundVoiceLimit(state, record.section, 0);
    }
    bool played = false;
    if (record.eva) {
        played = playRuntimeEvaOnDedicatedChannel(
            *samplePath,
            record.volumePercent
        );
        state.runtimeEvaMciChannelActive = played;
        state.runtimeEvaUsingPlaySoundFallback = false;
        if (!played) {
            const std::wstring widePath = widenUtf8(concord::audio::runtimeVoiceUtf8PathFromLegacyNative(*samplePath));
            played = runtimeAsyncSoundDevice().playFile(widePath);
            state.runtimeEvaUsingPlaySoundFallback = played;
        }
    } else {
        played = playRuntimeSfxOnDedicatedChannel(state, *samplePath, record);
        // A positionless PlaySound fallback would discard volume/pan and
        // could interrupt an EVA fallback. Preserve a failed-play record.
    }

    if (played) {
        record.status = "played";
        record.played = true;
        if (record.eva) {
            state.runtimeEvaActivePriority = record.priority;
            state.runtimeEvaBusyUntilTick =
                state.tick
                + runtimeEvaBusyTicksForSample(
                    samplePath,
                    state.config.fixedTickHz
                );
        }
        state.lastRuntimeSound =
            "sound " + std::filesystem::path(*samplePath).filename().string();
    } else if (!record.eva
        && state.runtimeEvaUsingPlaySoundFallback
        && state.tick < state.runtimeEvaBusyUntilTick) {
        record.status = "eva-protected";
        record.suppressed = true;
        state.lastRuntimeSound = "eva protected " + record.sample;
    } else {
        record.status = "play-failed";
        state.lastRuntimeSound = "sound busy " + record.sample;
        if (record.eva && !record.subtitleText.empty()) {
            // Device failure must not collapse a readable subtitle into a
            // one-tick flash or let the next queued line overwrite it.
            state.runtimeEvaActivePriority = record.priority;
            state.runtimeEvaBusyUntilTick = std::max(
                state.tick + std::max(1, state.config.fixedTickHz),
                state.runtimeEvaSubtitleUntilTick
            );
        }
    }
    rememberRuntimeSoundPlayback(state, std::move(record));
}

void queueRuntimeEvaSound(
    RuntimeProductionHostState& state,
    RuntimeSoundPlaybackRecord record) {
    const auto duplicate = std::find_if(
        state.runtimeEvaQueue.begin(),
        state.runtimeEvaQueue.end(),
        [&record](const RuntimeSoundPlaybackRecord& pending) {
            return pending.intent == record.intent
                && pending.section == record.section
                && pending.sample == record.sample;
        }
    );
    if (duplicate != state.runtimeEvaQueue.end()) {
        record.status = "eva-suppressed-duplicate";
        record.suppressed = true;
        ++state.runtimeEvaSuppressedEvents;
        state.lastRuntimeSound = "eva duplicate " + record.sample;
        rememberRuntimeSoundPlayback(state, std::move(record));
        return;
    }

    constexpr std::size_t maxRuntimeEvaQueueDepth = 12;
    if (state.runtimeEvaQueue.size() >= maxRuntimeEvaQueueDepth) {
        const auto lowest = std::min_element(
            state.runtimeEvaQueue.begin(),
            state.runtimeEvaQueue.end(),
            [](const RuntimeSoundPlaybackRecord& left,
               const RuntimeSoundPlaybackRecord& right) {
                return left.priority < right.priority;
            }
        );
        if (lowest == state.runtimeEvaQueue.end()
            || lowest->priority >= record.priority) {
            record.status = "eva-suppressed-full";
            record.suppressed = true;
            ++state.runtimeEvaSuppressedEvents;
            state.lastRuntimeSound = "eva queue full " + record.sample;
            rememberRuntimeSoundPlayback(state, std::move(record));
            return;
        }
        state.runtimeEvaQueue.erase(lowest);
        ++state.runtimeEvaSuppressedEvents;
    }

    record.status = "eva-queued";
    record.queued = true;
    RuntimeSoundPlaybackRecord pending = record;
    pending.status.clear();
    state.runtimeEvaQueue.push_back(std::move(pending));
    ++state.runtimeEvaQueuedEvents;
    state.lastRuntimeSound = "eva queued " + record.sample;
    rememberRuntimeSoundPlayback(state, std::move(record));
}

void suppressRuntimeEvaSound(
    RuntimeProductionHostState& state,
    RuntimeSoundPlaybackRecord record,
    const std::string& reason) {
    record.status = "eva-suppressed-" + reason;
    record.suppressed = true;
    ++state.runtimeEvaSuppressedEvents;
    state.lastRuntimeSound = "eva suppressed " + record.sample;
    rememberRuntimeSoundPlayback(state, std::move(record));
}

void processRuntimeEvaQueue(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("processRuntimeEvaQueue", state.tick);
    if (state.runtimeEvaMciChannelActive) {
        const std::optional<bool> playing =
            runtimeEvaMciChannelIsPlaying();
        if (playing.value_or(
                state.tick < state.runtimeEvaBusyUntilTick
            )) {
            return;
        }
        closeRuntimeEvaMciChannel();
        state.runtimeEvaMciChannelActive = false;
        state.runtimeEvaBusyUntilTick = state.tick;
    } else if (state.tick < state.runtimeEvaBusyUntilTick) {
        return;
    }
    state.runtimeEvaUsingPlaySoundFallback = false;
    state.runtimeEvaActivePriority = -1;
    if (state.runtimeEvaQueue.empty()) {
        return;
    }

    auto selected = std::max_element(
        state.runtimeEvaQueue.begin(),
        state.runtimeEvaQueue.end(),
        [](const RuntimeSoundPlaybackRecord& left,
           const RuntimeSoundPlaybackRecord& right) {
            return left.priority < right.priority;
        }
    );
    RuntimeSoundPlaybackRecord record = std::move(*selected);
    state.runtimeEvaQueue.erase(selected);
    record.queued = false;
    ++state.runtimeEvaDequeuedEvents;
    playRuntimeResolvedSound(state, std::move(record));
}

void noteRuntimeSoundIntent(RuntimeProductionHostState& state, const std::string& eventName) {
    state.lastRuntimeSound = eventName;
    ++state.runtimeSoundEvents;
    if (!eventName.empty()) {
        state.runtimeSoundIntentHistory.push_back(eventName);
        constexpr std::size_t maxRuntimeSoundIntentHistory = 16;
        while (state.runtimeSoundIntentHistory.size()
            > maxRuntimeSoundIntentHistory) {
            state.runtimeSoundIntentHistory.pop_front();
        }
    }
}

std::string runtimeArtAnimationSound(const RuntimeProductionHostState& state, const ArtType* art) {
    if (!art) return {};
    const auto resolve = [&](const std::string& authored) -> std::string {
        const auto id = trimAsciiLocal(authored);
        if (id.empty() || id == "-1" || toLowerAsciiLocal(id) == "none") return {};
        // Runtime startup loads the catalog before creating world effects.
        // Resource-only diagnostic fixtures may intentionally omit audio.
        if (!state.worldAudioCatalogLoaded) return id;
        if (const auto* sound = state.worldAudioCatalog.find(id)) return sound->eventId;
        std::istringstream input(id); int index = -1; char tail = 0;
        if ((input >> index) && !(input >> tail))
            if (const auto* sound = state.worldAudioCatalog.findBySoundListIndex(index)) return sound->eventId;
        return {};
    };
    // Retail 428359: StartSound and Report feed the same AnimType slot.
    // Report is read only when StartSound could not resolve to a sound.
    const auto start = resolve(art->startSound);
    return start.empty() ? resolve(art->reportSound) : start;
}

void emitRuntimeSound(RuntimeProductionHostState& state, const std::string& eventName,
    std::optional<RuntimeSpatialAudioPosition> source,
    bool trackCamera) {
    // House-local commands remain private. Physical sounds use the local
    // listener even while another house's simulation is being advanced.
    if (state.remoteHumanHouseScopeActive && !source) return;
    noteRuntimeSoundIntent(state, eventName);
    RuntimeSoundPlaybackRecord record;
    record.tick = state.tick;
    record.playbackOrder = state.runtimeSoundEvents;
    record.intent = eventName;
    record.section = runtimeSoundSectionFromEvent(state, eventName);
    record.semantic = isRuntimeSemanticSidebarSoundIntent(eventName);
    record.eva = runtimeSoundSectionIsEva(state, record.section);
    if (source && !record.eva) {
        record.spatial = true; record.sourcePosition = *source; record.trackCamera = trackCamera;
    }
    if (!record.eva && !state.runtimeSoundEffectsEnabled) {
        return;
    }
    record.volumePercent = runtimeSoundVolumePercent(state, record.section);
    record.gain16384 = record.volumePercent * 16384 / 100;
    if (record.eva) {
        ++state.runtimeEvaEvents;
        record.subtitleText = runtimeEvaSubtitleTextFromSection(
            state,
            record.section
        );
        record.priority = runtimeEvaPriorityFromSection(state, record.section);
        record.queueRequested =
            runtimeEvaQueueRequestedBySection(state, record.section);
    }

    const std::optional<std::string> sampleName =
        resolveRuntimeSoundSampleName(state, eventName, record.section);
    if (sampleName.has_value()) {
        record.sample = *sampleName;
        const std::optional<std::string> samplePath =
            findRuntimeSoundSamplePath(state, *sampleName);
        if (samplePath.has_value()) {
            record.path = *samplePath;
            record.playable = true;
            if (record.eva) {
                ++state.runtimeEvaPlayableEvents;
            }
        }
    }

    applyRuntimeAudioVariation(state, record);
    if (record.spatial) {
        const auto spatial = runtimeSpatialAudioAt(state, record.section, record.sourcePosition);
        record.spatialGain16384 = spatial.gain16384; record.pan16384 = spatial.pan16384;
        record.gain16384 = runtimeSpatialAudioCombinedGain(state, record.authoredGainPermille, spatial.gain16384);
        record.volumePercent = (record.gain16384 * 100 + 8192) / 16384;
        if (spatial.gain16384 == 0) {
            record.suppressed = true; record.status = "spatial-inaudible";
            rememberRuntimeSoundPlayback(state, std::move(record)); return;
        }
    }

    if (record.eva && !record.playable && record.subtitleText.empty()) {
        // A silent/malformed EVA section has no presentation to arbitrate.
        // Record the miss without dropping authority over an EVA channel that
        // may still be playing a previous line.
        record.status = record.sample.empty() ? "no-sample" : "missing";
        state.lastRuntimeSound = "sound missing " + record.sample;
        rememberRuntimeSoundPlayback(state, std::move(record));
        return;
    }

    if (record.eva
        && (record.playable || !record.subtitleText.empty())
        && (state.runtimeEvaMciChannelActive
            || state.tick < state.runtimeEvaBusyUntilTick)) {
        if (record.queueRequested) {
            queueRuntimeEvaSound(state, std::move(record));
            return;
        }
        if (record.priority > state.runtimeEvaActivePriority) {
            stopRuntimeEvaPlayback(state);
        } else {
            suppressRuntimeEvaSound(state, std::move(record), "busy");
            return;
        }
    }

    playRuntimeResolvedSound(state, std::move(record));
}

std::string runtimeCaptureEvaSection(const RuntimeProductionHostState& state, const BuildingType& type) {
    const std::string wanted = toLowerAsciiLocal(trimAsciiLocal(type.captureEvaEvent));
    if (!state.evaLoaded || wanted.empty()) return {};
    // Retail's INI reader resolves names against the allocated DialogList array.
    // Missing, unknown and explicit <none> names all resolve to the silent -1.
    const auto* list = state.evaIni.getSection("DialogList");
    if (!list) return {};
    for (const auto& [key, value] : *list) {
        (void)key;
        const std::string candidate = trimAsciiLocal(value);
        if (toLowerAsciiLocal(candidate) == wanted) return candidate;
    }
    return {};
}

void emitRuntimeEngineerCaptureEva(RuntimeProductionHostState& state, const BuildingType* type,
    const std::string& previousOwner, const std::string& captureOwner) {
    const auto identity = [&](const std::string& owner) {
        auto key = runtimeCanonicalHumanHouseOwner(owner);
        if (state.campaignMode) {
            const auto alias = state.triggers.campaignCanonicalHouseByAlias.find(key);
            if (alias != state.triggers.campaignCanonicalHouseByAlias.end()) key = runtimeCanonicalHumanHouseOwner(alias->second);
        }
        return key;
    };
    if (state.remoteHumanHouseScopeActive || runtimeVisionOwnerIsNeutral(identity(captureOwner))
        || identity(previousOwner) == identity(captureOwner)) return;
    // This existing canonical-owner helper also maps campaign PlayerControl houses
    // to the local player. Multiplayer peers remain distinct.
    const auto local = runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner);
    const bool gained = runtimeRepairDepotCanonicalOwnerId(state, captureOwner) == local;
    const bool lost = runtimeRepairDepotCanonicalOwnerId(state, previousOwner) == local;
    if (!gained && !lost) return;
    if (type && type->needsEngineer) {
        if (lost) emitRuntimeSound(state, "EVA_TechBuildingLost");
        if (gained) {
            const auto section = runtimeCaptureEvaSection(state, *type);
            if (!section.empty()) emitRuntimeSound(state, "sound:" + section);
        }
    } else {
        emitRuntimeSound(state, runtimeSoundIntentBuildingCaptured);
    }
}

void snapRuntimeCreditAudio(RuntimeProductionHostState& state) {
    auto& audio = state.creditAudio;
    audio.observed = true;
    audio.owner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    audio.balance = state.economy.credits;
    audio.pendingDirection = 0;
    audio.lastObservationMs = 0;
    audio.nextSoundMs = 0;
    audio.burstEndMs = 0;
    audio.burstPlayed = false;
}

std::optional<std::string> selectRuntimeMusicPath(
    RuntimeProductionHostState& state,
    bool advanceTrack) {
    // Result music is a phase theme; playlist advancement must replay it.
    if (state.skirmishMatchFinished && state.resultPresentation.scoreMusicSelected) advanceTrack = false;
    prepareRuntimeMusicPlaylist(state);
    const auto currentPathValid = [&state]() {
        if (state.runtimeMusicPath.empty()) {
            return false;
        }
        std::error_code errorCode;
        return std::filesystem::exists(state.runtimeMusicPath, errorCode)
            && std::filesystem::is_regular_file(
                state.runtimeMusicPath,
                errorCode
            );
    };
    if (!advanceTrack && currentPathValid()) {
        return state.runtimeMusicPath;
    }
    if (state.runtimeMusicThemeIds.empty()) {
        return std::nullopt;
    }

    std::optional<std::size_t> previousIndex;
    for (std::size_t index = 0;
            index < state.runtimeMusicThemeIds.size();
            ++index) {
        if (toLowerAsciiLocal(state.runtimeMusicThemeIds[index])
            == toLowerAsciiLocal(state.runtimeMusicThemeId)) {
            previousIndex = index;
            break;
        }
    }
    const std::size_t firstIndex = chooseRuntimeMusicIndex(
        state.runtimeMusicThemeIds.size(),
        advanceTrack ? previousIndex : std::nullopt,
        state.runtimeMusicRandomState
    );
    const std::string assetRootPath = runtimeAssetRootPath(state.config);
    const std::filesystem::path cacheRoot =
        std::filesystem::path(state.config.localDataRootPath)
            / "runtime_audio_cache";
    for (std::size_t offset = 0;
            offset < state.runtimeMusicThemeIds.size();
            ++offset) {
        const std::size_t index =
            (firstIndex + offset) % state.runtimeMusicThemeIds.size();
        const std::string& themeId = state.runtimeMusicThemeIds[index];
        if (advanceTrack
            && state.runtimeMusicThemeIds.size() > 1U
            && toLowerAsciiLocal(themeId)
                == toLowerAsciiLocal(state.runtimeMusicThemeId)) {
            continue;
        }
        const AudioBackgroundMusicResolveResult music =
            resolveBackgroundMusicThemeToWave(
                assetRootPath,
                pathString(cacheRoot),
                themeId
            );
        if (!music.success) {
            continue;
        }
        if (!state.runtimeMusicThemeId.empty()
            && toLowerAsciiLocal(state.runtimeMusicThemeId)
                != toLowerAsciiLocal(music.themeId)) {
            ++state.runtimeMusicTrackChanges;
        }
        state.runtimeMusicThemeId = music.themeId;
        state.runtimeMusicPath = music.wavPath;
        return state.runtimeMusicPath;
    }
    return currentPathValid()
        ? std::optional<std::string>{state.runtimeMusicPath}
        : std::nullopt;
}

bool runtimeMusicThemeIsUnspecified(const std::string& rawThemeId) {
    const std::string themeId = toLowerAsciiLocal(
        stripRuntimeSoundComment(rawThemeId)
    );
    return themeId.empty()
        || themeId == "none"
        || themeId == "<none>"
        || themeId == "no theme";
}

bool selectRuntimeMusicThemePath(
    RuntimeProductionHostState& state,
    const std::string& rawThemeId,
    std::string* errorMessage) {
    const std::string themeId = stripRuntimeSoundComment(rawThemeId);
    if (runtimeMusicThemeIsUnspecified(themeId)) {
        if (errorMessage != nullptr) {
            *errorMessage = "music theme is empty or unspecified";
        }
        return false;
    }

    const std::filesystem::path cacheRoot =
        std::filesystem::path(state.config.localDataRootPath)
            / "runtime_audio_cache";
    const AudioBackgroundMusicResolveResult music =
        resolveBackgroundMusicThemeToWave(
            runtimeAssetRootPath(state.config),
            pathString(cacheRoot),
            themeId
        );
    if (!music.success || music.wavPath.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = music.errorMessage.empty()
                ? "music theme did not resolve to a playable track"
                : music.errorMessage;
        }
        return false;
    }

    if (!state.runtimeMusicThemeId.empty()
        && toLowerAsciiLocal(state.runtimeMusicThemeId)
            != toLowerAsciiLocal(music.themeId)) {
        ++state.runtimeMusicTrackChanges;
    }
    state.runtimeMusicThemeId = music.themeId;
    state.runtimeMusicPath = music.wavPath;
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

std::optional<std::string> findRuntimeMusicPath(
    RuntimeProductionHostState& state,
    bool advanceTrack) {
    return selectRuntimeMusicPath(state, advanceTrack);
}

void startRuntimeMusic(
    RuntimeProductionHostState& state,
    bool advanceTrack) {
    if (!state.runtimeSoundsEnabled
        || !state.runtimeMusicEnabled
        || state.runtimeMusicStarted) {
        return;
    }

    const std::optional<std::string> musicPath =
        findRuntimeMusicPath(state, advanceTrack);
    if (!musicPath.has_value()) {
        state.lastRuntimeSound = "music missing";
        return;
    }

    const bool played = runtimeShellMusicWaveDevice().playFromStart(
        *musicPath, state.runtimeMusicVolume);

    if (played) {
        state.runtimeMusicStarted = true;
        state.runtimeMusicPath = *musicPath;
        state.lastRuntimeSound = "music "
            + runtimeMusicFactionName(runtimeMusicFaction(state))
            + " " + state.runtimeMusicThemeId;
    } else {
        state.lastRuntimeSound = "music failed";
    }
}

void stopRuntimeMusic(RuntimeProductionHostState& state) {
    if (!state.runtimeMusicStarted) {
        return;
    }
    runtimeShellMusicWaveDevice().stopAndClose();
    state.runtimeMusicStarted = false;
}

bool playRuntimeMusicTheme(
    RuntimeProductionHostState& state,
    const std::string& themeId,
    std::string* errorMessage) {
    std::string resolveError;
    if (!selectRuntimeMusicThemePath(state, themeId, &resolveError)) {
        if (errorMessage != nullptr) {
            *errorMessage = resolveError;
        }
        return false;
    }

    stopRuntimeMusic(state);
    if (state.runtimeSoundsEnabled && state.runtimeMusicEnabled) {
        startRuntimeMusic(state);
    } else {
        state.lastRuntimeSound = "music queued " + state.runtimeMusicThemeId;
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

std::optional<RuntimeRadarMapSize> parseRuntimeRadarMapSize(
    const std::string& value) {
    const std::vector<std::string> tokens = splitRuntimeTokenList(value);
    if (tokens.size() < 4) {
        return std::nullopt;
    }
    RuntimeRadarMapSize size;
    size.x = parseRuntimeIntOrDefault(tokens[0], -1);
    size.y = parseRuntimeIntOrDefault(tokens[1], -1);
    size.width = parseRuntimeIntOrDefault(tokens[2], -1);
    size.height = parseRuntimeIntOrDefault(tokens[3], -1);
    if (size.x < 0 || size.y < 0 || size.width <= 0 || size.height <= 0) {
        return std::nullopt;
    }
    return size;
}

RuntimeRadarDxyPoint runtimeRadarDxyPoint(
    double x,
    double y,
    int fullMapWidth) {
    return {
        x - y + static_cast<double>(fullMapWidth) - 1.0,
        x + y - static_cast<double>(fullMapWidth) - 1.0
    };
}

bool runtimeRadarTerrainCellIntersectsProjection(
    const RuntimeRadarProjection& projection,
    const TerrainCell& terrain) {
    if (!projection.valid) {
        return false;
    }
    const RuntimeRadarDxyPoint tileDxy = runtimeRadarDxyPoint(
        terrain.x,
        terrain.y,
        projection.fullMapWidth
    );
    // A terrain cell is a two-unit DXY diamond: left/right are one unit from
    // its top X and its bottom is two units below its top Y.  Use the same
    // intersection rule for the overview and the camera's playable envelope
    // so the two views cannot silently disagree about which edge cells exist.
    return tileDxy.x + 1.0 >= projection.minX
        && tileDxy.x - 1.0 < projection.maxX
        && tileDxy.y + 2.0 >= projection.minY
        && tileDxy.y < projection.maxY;
}

RuntimeRadarProjection buildRuntimeRadarProjection(
    const RuntimeProductionHostState& state) {
    const std::optional<RuntimeRadarMapSize> fullSize =
        parseRuntimeRadarMapSize(state.report.map.size);
    const int fullMapWidth = fullSize.has_value()
        ? fullSize->width
        : state.report.map.width;
    std::ostringstream cacheKeyStream;
    cacheKeyStream
        << state.report.map.path
        << "|size=" << state.report.map.size
        << "|local=" << state.report.map.localSize
        << "|headerWidth=" << state.report.map.width
        << "|terrain=" << state.world.getTerrainCells().size();
    const std::string cacheKey = cacheKeyStream.str();
    if (state.radarProjectionCacheValid
        && state.radarProjectionCacheKey == cacheKey) {
        return RuntimeRadarProjection{
            true,
            state.radarProjectionMinX,
            state.radarProjectionMinY,
            state.radarProjectionMaxX,
            state.radarProjectionMaxY,
            state.radarProjectionFullMapWidth
        };
    }

    RuntimeRadarProjection projection;
    projection.fullMapWidth = fullMapWidth;
    if (fullMapWidth <= 0) {
        return projection;
    }

    std::optional<RuntimeRadarMapSize> localSize =
        parseRuntimeRadarMapSize(state.report.map.localSize);
    if (!localSize.has_value()) {
        localSize = fullSize;
    }
    if (localSize.has_value()) {
        const int clampedX = std::max(2, localSize->x);
        const int clampedWidth = std::min(
            fullMapWidth - 2 - clampedX,
            localSize->width
        );
        if (clampedWidth > 0) {
            projection.minX = 2 * clampedX;
            projection.minY = (2 * localSize->y) + 4;
            projection.maxX = projection.minX + (2 * clampedWidth);
            projection.maxY = projection.minY
                + (2 * localSize->height)
                + 8;
            projection.valid = projection.maxX > projection.minX
                && projection.maxY > projection.minY;
        }
    }

    bool hasPoint = false;
    if (!projection.valid) {
        for (const TerrainCell& terrain : state.world.getTerrainCells()) {
            const RuntimeRadarDxyPoint point = runtimeRadarDxyPoint(
                terrain.x,
                terrain.y,
                fullMapWidth
            );
            const int pointX = static_cast<int>(std::llround(point.x));
            const int pointY = static_cast<int>(std::llround(point.y));
            if (!hasPoint) {
                projection.minX = pointX;
                projection.maxX = pointX + 1;
                projection.minY = pointY;
                projection.maxY = pointY + 1;
                hasPoint = true;
            } else {
                projection.minX = std::min(projection.minX, pointX);
                projection.maxX = std::max(projection.maxX, pointX + 1);
                projection.minY = std::min(projection.minY, pointY);
                projection.maxY = std::max(projection.maxY, pointY + 1);
            }
        }
        projection.valid = hasPoint;
    }

    if (projection.valid) {
        projection.maxX = std::max(projection.maxX, projection.minX + 1);
        projection.maxY = std::max(projection.maxY, projection.minY + 1);
        state.radarProjectionCacheValid = true;
        state.radarProjectionCacheKey = cacheKey;
        state.radarProjectionFullMapWidth = projection.fullMapWidth;
        state.radarProjectionMinX = projection.minX;
        state.radarProjectionMinY = projection.minY;
        state.radarProjectionMaxX = projection.maxX;
        state.radarProjectionMaxY = projection.maxY;
    }
    return projection;
}

void recordRuntimeMatchBuild(RuntimeGameSessionState& state, const std::string& owner, int count) {
    if (state.campaignMode || state.skirmishMatchFinished || count <= 0
        || trimAsciiLocal(owner).empty() || runtimeOwnerIsNeutralForAutoTarget(owner)) return;
    recordRuntimeMatchScoreBuild(state.matchScores, {owner, count});
}

void recordRuntimeMatchObjectDestruction(
    RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const std::string& sourceOwner) {
    if (state.campaignMode || state.skirmishMatchFinished
        || runtimeOwnerIsNeutralForAutoTarget(object.owner)
        || !runtimeVeterancyObjectAwardsExperience(state, object)) return;
    const bool credited = !trimAsciiLocal(sourceOwner).empty()
        && !runtimeOwnerIsNeutralForAutoTarget(sourceOwner)
        && !runtimeOwnersAreAllied(state, sourceOwner, object.owner);
    recordRuntimeMatchScoreDestruction(state.matchScores, {
        object.owner, credited ? sourceOwner : std::string{},
        object.kind == WorldObjectKind::Structure,
        credited ? runtimeVeterancyObjectCost(state, object) : 0});
}

void writeRuntimeMatchScore(const RuntimeGameSessionState& state, std::ostream& output) {
    writeRuntimeMatchScore(state.matchScores, output);
}

bool loadRuntimeMatchScore(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    return loadRuntimeMatchScore(state.matchScores, ini, error);
}

void hashRuntimeMatchScore(const RuntimeGameSessionState& state, std::uint64_t& hash) {
    hashRuntimeMatchScore(state.matchScores, hash);
}

RuntimeProductionRate runtimeSidebarProductionRate(
    RuntimeBuildKind kind,
    RuntimeProductionRate authoredRate) {
    constexpr std::uint64_t maximumRateTerm = 1000;
    if (authoredRate.numerator == 0
        || authoredRate.denominator == 0
        || authoredRate.numerator > maximumRateTerm
        || authoredRate.denominator > maximumRateTerm) {
        return {0, 0};
    }
    const int speedMultiplier =
        kind == RuntimeBuildKind::Building || kind == RuntimeBuildKind::Defense
            ? runtimeSidebarBuildingProductionSpeedMultiplier
            : runtimeSidebarProductionSpeedMultiplier;
    std::uint64_t numerator =
        static_cast<std::uint64_t>(authoredRate.numerator)
        * speedMultiplier;
    std::uint64_t denominator = authoredRate.denominator;
    const std::uint64_t divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
    if (numerator <= maximumRateTerm) {
        return {
            static_cast<std::uint32_t>(numerator),
            static_cast<std::uint32_t>(denominator)
        };
    }
    // RuntimeProductionLanes bounds both rational terms to 1000. Preserve
    // the scaled ratio at that resolution when an extreme authored modifier
    // cannot be represented exactly.
    const std::uint64_t approximateDenominator = std::clamp<std::uint64_t>(
        (maximumRateTerm * denominator + numerator / 2) / numerator,
        1,
        maximumRateTerm
    );
    return {
        static_cast<std::uint32_t>(maximumRateTerm),
        static_cast<std::uint32_t>(approximateDenominator)
    };
}

int runtimeScaleResourceValue(int value, int acceptedUnits, int totalUnits) {
    return runtimeEconomyScaleStoredValue(value, acceptedUnits, totalUnits);
}

bool runtimeHasOwnedRadar(const RuntimeGameSessionState& state) {
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.owner != state.economy.owner
            || object.kind != WorldObjectKind::Structure) {
            continue;
        }

        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr
            && (building->radar
                || building->spySat
                || building->factory == "Radar")) {
            return true;
        }
    }

    return false;
}

bool runtimeRadarHasPower(const RuntimeGameSessionState& state) {
    return state.economy.powerProduced - state.economy.powerUsed >= 0
        && !runtimeDiskDrainOwnerBlackout(state, state.economy.owner);
}

bool runtimeRadarAvailable(const RuntimeGameSessionState& state) {
    if (!state.campaignMode && runtimeLocalObserver(state.config)) return true;
    const bool spyBlackout = state.spies.blackoutUntilTick(
        runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner)
    ) > state.tick;
    return runtimeHasOwnedRadar(state)
        && runtimeRadarHasPower(state)
        && !spyBlackout
        && !state.superWeapons.isEnemyStormActive(state.economy.owner, state.tick,
            [&state](const auto& a, const auto& b) { return runtimeOwnersAreAllied(state, a, b); })
        && (!state.campaignMode
            || state.tick >= state.triggers.campaignRadarBlackoutUntilTick);
}

RuntimeTechTreeDecision evaluateRuntimeTechTreeDecisionUnmemoized(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    RuntimeTechTreeDecision decision;
    const bool secretLabUnlock = runtimeBuildItemHasSecretLabUnlock(
        state,
        state.economy.owner,
        item
    );
    decision.staticAllowed =
        (item.techTreeAllowed && !item.secretLabInjected)
        || secretLabUnlock;
    decision.ownerAllowed = secretLabUnlock
        || runtimeBuildItemVisibleForPlayerCountry(state, item);
    decision.techLevel = item.techLevel;
    decision.maxTechLevel = state.economy.techLevel;
    decision.techAllowed = secretLabUnlock
        || (item.techLevel >= 0
            && item.techLevel <= state.economy.techLevel);
    decision.factoryAvailable = runtimeOwnerHasCompatibleFactory(
        state,
        state.economy.owner,
        item
    );
    decision.enoughCredits = state.economy.credits >= item.cost;
    decision.powerNet = runtimePowerNet(state);
    decision.powerNetAfterBuild = runtimePowerNetAfterBuildItem(state, item);
    decision.lowPower = decision.powerNet < 0;
    decision.lowPowerAfterBuild = decision.powerNetAfterBuild < 0;

    if (!decision.staticAllowed) {
        decision.reason = item.techTreeBlockedReason.empty()
            ? "blocked by tech tree"
            : item.techTreeBlockedReason;
        return decision;
    }
    if (!decision.ownerAllowed) {
        decision.reason = "wrong country or side";
        return decision;
    }
    if (!decision.techAllowed) {
        decision.reason =
            "requires tech "
            + std::to_string(item.techLevel)
            + "/"
            + std::to_string(state.economy.techLevel);
        return decision;
    }
    if (!runtimeBuildItemMeetsStolenTechRequirements(state, state.economy.owner, item)) {
        decision.reason = "requires stolen technology";
        return decision;
    }
    if (!decision.factoryAvailable) {
        decision.missingFactory = item.requiredFactory;
        decision.reason = item.requiredFactory.empty()
            ? "missing factory"
            : "missing " + item.requiredFactory;
        return decision;
    }

    decision.prerequisitesMet = secretLabUnlock
        || runtimePrerequisitesMet(
            state,
            item.prerequisite,
            &decision.missingPrerequisite
        );
    if (!decision.prerequisitesMet
        && runtimePrerequisiteOverrideMet(
            state,
            item.prerequisiteOverride
        )) {
        decision.prerequisitesMet = true;
        decision.missingPrerequisite.clear();
    }
    if (!decision.prerequisitesMet) {
        decision.reason =
            "missing prerequisite " + decision.missingPrerequisite;
        return decision;
    }

    decision.canQueue = true;
    decision.canStart = decision.enoughCredits;
    if (!decision.enoughCredits) {
        decision.reason = "waiting credits";
    } else if (decision.lowPower) {
        decision.reason = "ready, low power";
    } else if (decision.lowPowerAfterBuild) {
        decision.reason = "ready, will drain power";
    } else {
        decision.reason = "ready";
    }
    return decision;
}

RuntimeTechTreeDecision evaluateRuntimeTechTreeDecision(
    const RuntimeGameSessionState& state, const RuntimeBuildCatalogItem& item) {
    static const bool reference = [] {
        const char* flag = std::getenv("CONCORD_PAINT_TECH_REFERENCE");
        return flag != nullptr && flag[0] == '1' && flag[1] == '\0';
    }();
    auto* memo = RuntimePaintTechTreeMemoScope::active;
    const auto& catalog = state.economy.catalog;
    if (reference || memo == nullptr || catalog.empty())
        return evaluateRuntimeTechTreeDecisionUnmemoized(state, item);
    // An unrelated/temporary catalog item keeps the original path. Comparing
    // integer addresses avoids subtraction/ordering of unrelated pointers.
    const auto begin = reinterpret_cast<std::uintptr_t>(catalog.data());
    const auto address = reinterpret_cast<std::uintptr_t>(&item);
    if (address < begin || address - begin >= catalog.size() * sizeof(RuntimeBuildCatalogItem)
        || (address - begin) % sizeof(RuntimeBuildCatalogItem) != 0)
        return evaluateRuntimeTechTreeDecisionUnmemoized(state, item);
    if (memo->state != &state || memo->catalogData != catalog.data()
        || memo->catalogCount != catalog.size()) {
        memo->state = &state;
        memo->catalogData = catalog.data();
        memo->catalogCount = catalog.size();
        memo->decisions.clear();
        memo->decisions.resize(catalog.size());
    }
    auto& cached = memo->decisions[(address - begin) / sizeof(RuntimeBuildCatalogItem)];
    if (!cached) cached = evaluateRuntimeTechTreeDecisionUnmemoized(state, item);
    else ++memo->hits;
    return *cached;
}

bool runtimeBuildItemUnlockedForSidebar(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    // Progression gates decide whether the cameo exists. Credits, low power,
    // queue occupancy, and hold state are temporary and must not hide it.
    return evaluateRuntimeTechTreeDecision(state, item).canQueue;
}

bool runtimeUnitUsesFlyAuxSounds(const UnitType& type) {
    const auto locomotor = toLowerAsciiLocal(trimAsciiLocal(type.locomotor));
    // Jumpjet and Rocket have different code paths. In particular, Rocket
    // already uses AuxSound1 through its existing launchSound profile.
    return locomotor.find("4a582746-9839-11d1-b709-00a024ddafd1") != std::string::npos
        || locomotor == "fly";
}

void queueRuntimeAircraftAuxSound(const RuntimeGameSessionState& state,
    WorldObjectInstance& object, bool takingOff, int relativeHeightLeptons) {
    if (!object.alive || object.health <= 0 || (!takingOff && relativeHeightLeptons >= 300)) return;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!type || !runtimeUnitUsesFlyAuxSounds(*type)) return;
    const auto event = trimAsciiLocal(takingOff ? type->auxSound1 : type->auxSound2);
    if (event.empty()) return;
    auto& events = object.pendingAircraftAuxSounds;
    if (events.size() >= 32) events.erase(events.begin());
    events.push_back({event, state.tick, object.x, object.y, object.level, takingOff});
}

bool runtimeDiskMoveSoundIsLoop(const RuntimeProductionHostState& state, const UnitType& unit) {
    const auto* sound = state.worldAudioCatalog.find(trimAsciiLocal(unit.moveSound));
    if (!sound || !sound->loop) return false;
    if (toUpperAsciiLocal(unit.id) == "DISK") return true;
    // Support aircraft enter outside the viewport. Their engine is owned by
    // the moving plane, so it becomes audible when that plane reaches range.
    return std::any_of(state.rules.getSuperWeaponTypes().begin(), state.rules.getSuperWeaponTypes().end(),
        [&](const auto& rule) { return !rule.planeType.empty()
            && toLowerAsciiLocal(rule.planeType) == toLowerAsciiLocal(unit.id); });
}

std::optional<std::string> runtimeMoveSoundForSelectedUnits(
    const RuntimeProductionHostState& state) {
    for (int objectId : state.selectedObjectIds) {
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object == nullptr || object->owner != state.economy.owner) {
            continue;
        }
        const UnitType* unit = findRuntimeUnitType(state.rules, *object);
        if (unit != nullptr && !trimAsciiLocal(unit->moveSound).empty() && !runtimeDiskMoveSoundIsLoop(state, *unit)) {
            return trimAsciiLocal(unit->moveSound);
        }
    }
    return std::nullopt;
}

std::string runtimeIdleVariantMissionForObjectReference(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (runtimeObjectHasDeployedState(state, object)) {
        return "";
    }

    const ArtType* art = runtimeArtTypeForObject(state, object);
    if (art == nullptr) {
        return "";
    }

    const bool idle1Safe = runtimeIdleSequenceLooksSafe(*art, "Idle1");
    const bool idle2Safe = runtimeIdleSequenceLooksSafe(*art, "Idle2");
    if (!idle1Safe && !idle2Safe) {
        return "";
    }

    const int periodTicks =
        std::max(90, state.config.fixedTickHz * 6);
    const int activeTicks =
        std::max(18, state.config.fixedTickHz);
    const int phase =
        (state.tick + std::max(0, object.instanceId) * 37)
        % periodTicks;
    if (phase >= activeTicks) {
        return "";
    }
    if (idle1Safe && idle2Safe) {
        return ((object.instanceId + state.tick / periodTicks) % 2 == 0)
            ? "Idle1"
            : "Idle2";
    }
    return idle1Safe ? "Idle1" : "Idle2";
}

std::optional<RuntimeIdleVariantPhase> runtimeSafeIdleVariantPhase(
    int tick, int fixedTickHz, int instanceId) {
    // Hoisting must not evaluate an overflowing expression which the original
    // art/deployed guards could have skipped. Abnormal inputs keep that order.
    const std::int64_t periodProduct = std::int64_t{fixedTickHz} * 6;
    const std::int64_t idProduct = std::int64_t{std::max(0, instanceId)} * 37;
    const std::int64_t numerator = std::int64_t{tick} + idProduct;
    const auto fitsInt = [](std::int64_t value) {
        return value >= std::numeric_limits<int>::min()
            && value <= std::numeric_limits<int>::max();
    };
    if (!fitsInt(periodProduct) || !fitsInt(idProduct) || !fitsInt(numerator)) {
        return std::nullopt;
    }
    RuntimeIdleVariantPhase result;
    result.periodTicks = std::max(90, static_cast<int>(periodProduct));
    result.activeTicks = std::max(18, fixedTickHz);
    result.phase = static_cast<int>(numerator) % result.periodTicks;
    return result;
}

std::string runtimeIdleVariantMissionForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    bool forceReference,
    RuntimeIdleVariantPass* pass) {
    if (forceReference || runtimeFrameScalingReferenceMovement()) {
        return runtimeIdleVariantMissionForObjectReference(state, object);
    }
    if (runtimeObjectHasDeployedState(state, object)) {
        return "";
    }
    const auto phase = runtimeSafeIdleVariantPhase(
        state.tick, state.config.fixedTickHz, object.instanceId);
    if (!phase) {
        return runtimeIdleVariantMissionForObjectReference(state, object);
    }
    if (phase->phase >= phase->activeTicks) {
        return "";
    }
    const ArtType* art = runtimeArtTypeForObject(state, object);
    if (art == nullptr) {
        return "";
    }
    const auto safety = pass ? pass->safetyFor(*art) : std::pair<bool, bool>{
        runtimeIdleSequenceLooksSafe(*art, "Idle1"),
        runtimeIdleSequenceLooksSafe(*art, "Idle2")};
    if (safety.first && safety.second) {
        return ((object.instanceId + state.tick / phase->periodTicks) % 2 == 0)
            ? "Idle1" : "Idle2";
    }
    return safety.first ? "Idle1" : (safety.second ? "Idle2" : "");
}

RuntimeSpatialAudioPosition runtimeWorldAudioSourcePosition(const RuntimeGameSessionState& state,
    const RuntimeWorldAudioAction& action) {
    if (action.sourceObjectId >= 0) if (const auto* object = findObjectById(state, action.sourceObjectId))
        return runtimeSpatialAudioPositionForObject(*object);
    return {double(action.cell.x), double(action.cell.y), double(action.cell.level)};
}

void rememberRuntimeWorldAudioBackendAction(RuntimeProductionHostState& state, const RuntimeWorldAudioAction& action) {
    state.runtimeWorldAudioBackendActionHistory.push_back(action);
    while (state.runtimeWorldAudioBackendActionHistory.size() > 32U)
        state.runtimeWorldAudioBackendActionHistory.pop_front();
}

void applyRuntimeWorldAudioBackendAction(RuntimeProductionHostState& state, const RuntimeWorldAudioAction& input) {
    RuntimeWorldAudioAction action = input;
    if (action.kind == RuntimeWorldAudioActionKind::Stop) {
        rememberRuntimeWorldAudioBackendAction(state, action);
        if (state.runtimeVoiceAudio) state.runtimeVoiceAudio->close(action.handleId);
        state.runtimeWorldAudioBackendBindings.erase(action.handleId);
        state.runtimeWorldAudioLimitEvictedHandles.erase(action.handleId); return;
    }
    auto found = state.runtimeWorldAudioBackendBindings.find(action.handleId);
    if (action.kind == RuntimeWorldAudioActionKind::SetGain && found != state.runtimeWorldAudioBackendBindings.end()) {
        const auto& previous = found->second.action;
        if (action.eventId.empty()) {action.eventId = previous.eventId; action.cell = previous.cell;}
        if (action.sourceObjectId < 0) action.sourceObjectId = previous.sourceObjectId;
        if (action.authoredGainPermille < 0) action.authoredGainPermille = previous.authoredGainPermille;
    }
    const auto position = runtimeWorldAudioSourcePosition(state, action);
    const auto spatial = runtimeSpatialAudioAt(state, action.eventId, position);
    action.pan16384 = spatial.pan16384; action.spatialGain16384 = spatial.gain16384;
    const int authored = action.authoredGainPermille >= 0 ? action.authoredGainPermille : action.gainPermille;
    action.authoredGainPermille = authored;
    action.gainPermille = authored * spatial.gain16384 / 16384;
    rememberRuntimeWorldAudioBackendAction(state, action);
    if (!state.runtimeSoundsEnabled) return;
    const int gain = !state.runtimeSoundEffectsEnabled ? 0
        : runtimeSpatialAudioCombinedGain(state, authored, spatial.gain16384);
    if (action.kind == RuntimeWorldAudioActionKind::SetGain) {
        if (found == state.runtimeWorldAudioBackendBindings.end()) return;
        auto& binding = found->second;
        if (state.runtimeVoiceAudio && state.runtimeVoiceAudio->setMix(action.handleId, gain, action.pan16384)) {
            binding.action.authoredGainPermille = authored;
            binding.action.cell = action.cell; binding.action.sourceObjectId = action.sourceObjectId;
            binding.action.spatialGain16384 = action.spatialGain16384; binding.action.pan16384 = action.pan16384;
            binding.position = position; binding.gainPermille = action.gainPermille;
            binding.gain16384 = gain; binding.pan16384 = action.pan16384;
        }
        return;
    }
    if (action.queueAfterCurrent && action.sourceObjectId >= 0
        && state.runtimeWorldAudioLimitEvictedHandles.contains(action.handleId)) return;
    std::optional<std::string> path = findRuntimeSoundSamplePath(state, action.sampleName);
    if (path) path = runtimeAudioPitchVariantPath(*path,
        std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache", action.frequencyPermille);
    auto& audio = runtimeVoiceAudioFor(state);
    if (action.queueAfterCurrent && path && found != state.runtimeWorldAudioBackendBindings.end()
        && audio.queue(action.handleId, concord::audio::runtimeVoiceUtf8PathFromLegacyNative(*path))) return;
    audio.close(action.handleId); state.runtimeWorldAudioBackendBindings.erase(action.handleId);
    if (!path || (spatial.gain16384 == 0 && action.backendLoopIterations != -1)) return;
    if (spatial.gain16384 > 0)
        enforceRuntimeSoundVoiceLimit(state, action.eventId, action.handleId);
    if (!audio.open(action.handleId, concord::audio::runtimeVoiceUtf8PathFromLegacyNative(*path)) || !audio.setMix(action.handleId, gain, action.pan16384)) {
        audio.close(action.handleId); return;
    }
    const bool active = spatial.gain16384 > 0;
    if (active && !audio.playFromStart(action.handleId, action.backendLoopIterations == -1, action.queueAfterCurrent)) {
        audio.close(action.handleId); return;
    }
    RuntimeWorldAudioBackendBinding binding;
    binding.handleId = action.handleId; binding.playbackOrder = ++state.runtimeSoundVoiceOrder;
    binding.active = active; binding.action = action;
    binding.position = position; binding.gainPermille = action.gainPermille;
    binding.gain16384 = gain; binding.pan16384 = action.pan16384;
    state.runtimeWorldAudioBackendBindings.emplace(action.handleId, std::move(binding));
}

void applyRuntimeWorldAudioBackendActions(RuntimeProductionHostState& state, const std::vector<RuntimeWorldAudioAction>& actions) {
    for (const auto& action : actions) applyRuntimeWorldAudioBackendAction(state, action);
}

void stopRuntimeWorldAudioBackend(RuntimeProductionHostState& state, bool preserveChronosphereSelection) {
    if (state.runtimeVoiceAudio) for (const auto& [id, binding] : state.runtimeWorldAudioBackendBindings)
        state.runtimeVoiceAudio->close(id);
    state.runtimeWorldAudioBackendBindings.clear();
    state.runtimeWorldAudioLimitEvictedHandles.clear();
    state.superWeaponBuildingAudio = {}; state.diskDrainAudio = {}; state.diskMoveAudio = {};
    state.gattlingAudio = {};
    if (preserveChronosphereSelection && state.chronosphereSelectionAudio.selected) {
        auto& sound = state.chronosphereSelectionAudio.sound;
        sound.active = false; sound.gainPermille = 0;
        sound.nextSampleMilliseconds = sound.queuedUntilMilliseconds = 0;
    } else {
        state.chronosphereSelectionAudio = {};
    }
    state.objectAmbientAudio = {}; state.aircraftAuxAudio = {}; state.turretSoundAudio = {};
    for (auto& object : state.world.accessObjects()) {
        object.pendingTurretSounds.clear(); object.pendingAircraftAuxSounds.clear();
    }
}

void reapRuntimeWorldAudioBackendBindings(RuntimeProductionHostState& state) {
    if (!state.runtimeVoiceAudio) return;
    auto& audio = *state.runtimeVoiceAudio;
    for (auto it = state.runtimeWorldAudioBackendBindings.begin(); it != state.runtimeWorldAudioBackendBindings.end();) {
        auto& binding = it->second;
        if (binding.active && !audio.isPlaying(it->first).value_or(false)) {
            audio.close(it->first); it = state.runtimeWorldAudioBackendBindings.erase(it); continue;
        }
        ++it;
    }
}

std::uint64_t runtimeDiskMoveSoundHandle(int objectId, int channel) {
    return 0x0800000000000000ULL | (std::uint64_t(std::uint32_t(objectId)) << 2) | unsigned(channel);
}

bool advanceRuntimeDiskLoopSources(RuntimeProductionHostState& state, RuntimeSuperWeaponBuildingAudioState& audio,
    const std::vector<RuntimeDiskLoopSource>& sources, std::uint64_t nowMilliseconds,
    std::uint64_t (*handle)(int,int)) {
    bool changed = false;
    std::unordered_set<int> live;
    const auto stop = [&](int id, int channel) {
        RuntimeWorldAudioAction action; action.kind = RuntimeWorldAudioActionKind::Stop;
        action.handleId = handle(id,channel); action.eventTick = state.tick;
        action.reason = "disk loop source stopped";
        applyRuntimeWorldAudioBackendAction(state,action); changed = true;
    };
    const auto play = [&](const WorldObjectInstance& source,const RuntimeWorldAudioSoundSpec& sound,
        const std::string& sample,int channel,int ordinal,int gain) {
        RuntimeWorldAudioAction action; action.handleId = handle(source.instanceId,channel);
        action.eventId = sound.eventId; action.sampleName = sample;
        action.cell = {source.x,source.y,source.level}; action.sourceObjectId = source.instanceId;
        action.eventTick = state.tick; action.playbackOrdinal = ordinal;
        action.gainPermille = gain; action.authoredGainPermille = sound.volumePercent * 10;
        action.queueAfterCurrent = channel == 0 && sound.loop;
        action.reason = channel == 0 ? "continuous disk sound sequence" : "disk loop decay";
        applyRuntimeWorldAudioBackendAction(state,action); changed = true;
        const auto path = findRuntimeSoundSamplePath(state,sample);
        return std::uint64_t(path ? std::max(1,runtimeWavDurationTicks(*path,1000)) : 1000);
    };
    for(const auto& source:sources) {
        const auto* object = source.object;
        const auto* spec = state.worldAudioCatalog.find(source.event);
        if(!object || !spec || spec->samples.empty()) continue;
        const int id = object->instanceId; live.insert(id);
        const bool newlyCreated = audio.newlyCreatedSources.erase(id) > 0;
        auto [it,inserted] = audio.sources.try_emplace(id); auto& previous = it->second;
        const bool transition = inserted || previous.stage != source.stage
            || previous.owner != object->owner || previous.event != source.event;
        if(transition) {
            stop(id,0); stop(id,1); previous = {};
            previous.stage = source.stage; previous.owner = object->owner; previous.event = source.event;
        }
        const int gain = runtimeSpatialAudioGainPermille(state,*spec,runtimeSpatialAudioPositionForObject(*object));
        if(gain <= 0) {
            if(previous.active) stop(id,0);
            previous.active = false; previous.gainPermille = 0; continue;
        }
        if(previous.active && previous.gainPermille != gain) {
            RuntimeWorldAudioAction action; action.kind = RuntimeWorldAudioActionKind::SetGain;
            action.handleId = handle(id,0); action.eventTick = state.tick; action.gainPermille = gain;
            applyRuntimeWorldAudioBackendAction(state,action);
        }
        previous.gainPermille = gain;
        // A voice evicted by soundmd Limit stays silent until this motor run
        // ends or leaves hearing range; it must not steal another handle
        // on every presentation frame.
        if(state.runtimeSoundsEnabled && state.runtimeWorldAudioLimitEvictedHandles.contains(handle(id,0))) continue;
        const auto controls = toLowerAsciiLocal(state.soundIni.getValue(source.event,"Control",""));
        const bool attack = controls.find("attack") != std::string::npos && spec->samples.size() > 2;
        const bool decay = controls.find("decay") != std::string::npos && spec->samples.size() > 2;
        const bool all = controls.find("all") != std::string::npos;
        const std::size_t first = attack ? 1 : 0, last = spec->samples.size() - (decay ? 1 : 0);
        const auto loopSample = [&]() {
            if(previous.remainingLoopSamples.empty())
                for(std::size_t index=first;index<last;++index) previous.remainingLoopSamples.push_back(index);
            const std::size_t pick = spec->randomSample
                ? runtimeAudioVariationSeed(source.event,id,previous.ordinal) % previous.remainingLoopSamples.size() : 0;
            const auto sample = spec->samples[previous.remainingLoopSamples[pick]];
            previous.remainingLoopSamples.erase(previous.remainingLoopSamples.begin()+pick);
            if(!all) previous.remainingLoopSamples.clear();
            return sample;
        };
        if(!previous.active) {
            previous.sample = transition && (audio.primed || newlyCreated) && attack ? spec->samples.front() : loopSample();
            previous.nextSampleMilliseconds = nowMilliseconds + play(*object,*spec,previous.sample,0,previous.ordinal++,gain);
            previous.queuedUntilMilliseconds = previous.nextSampleMilliseconds;
            previous.active = true;
            // Queue the first body segment now: the audio device crosses the
            // boundary even when the next presentation frame arrives late.
            const auto next = loopSample();
            previous.queuedUntilMilliseconds += play(*object,*spec,next,0,previous.ordinal++,gain);
            previous.sample = next;
            continue;
        }
        bool needsBuffer = nowMilliseconds >= previous.nextSampleMilliseconds;
        if(state.runtimeSoundsEnabled && state.runtimeVoiceAudio && state.runtimeVoiceAudio->ready())
            needsBuffer = state.runtimeVoiceAudio->queuedBuffers(handle(id,0)) < 2;
        if(!needsBuffer) continue;
        previous.sample = loopSample();
        previous.nextSampleMilliseconds = std::max(nowMilliseconds,previous.queuedUntilMilliseconds);
        previous.queuedUntilMilliseconds = previous.nextSampleMilliseconds
            + play(*object,*spec,previous.sample,0,previous.ordinal++,gain);
    }
    for(auto it=audio.sources.begin();it!=audio.sources.end();) {
        if(live.contains(it->first)){++it;continue;}
        const bool evicted = state.runtimeWorldAudioLimitEvictedHandles.contains(handle(it->first,0));
        stop(it->first,0);
        const auto* object = findObjectById(state,it->first);
        const auto* old = state.worldAudioCatalog.find(it->second.event);
        const auto controls = toLowerAsciiLocal(state.soundIni.getValue(it->second.event,"Control",""));
        if(!evicted && object && object->alive && object->owner == it->second.owner && it->second.active
            && old && !old->samples.empty() && controls.find("decay") != std::string::npos) {
            const int gain = runtimeSpatialAudioGainPermille(state,*old,runtimeSpatialAudioPositionForObject(*object));
            if(gain > 0) play(*object,*old,old->samples.back(),1,it->second.ordinal,gain);
        }
        it = audio.sources.erase(it);
    }
    audio.primed = true;
    return changed;
}

bool advanceRuntimeDiskMoveAudio(RuntimeProductionHostState& state,std::uint64_t nowMilliseconds) {
    if(!state.rulesLoaded || !state.worldAudioCatalogLoaded) return false;
    std::vector<RuntimeDiskLoopSource> sources;
    for(const auto& object:state.world.getObjects()) {
        const auto* unit = findRuntimeUnitType(state.rules,object);
        if(!unit || !runtimeDiskMoveSoundIsLoop(state,*unit) || !object.alive || object.health<=0
            || runtimeGateObjectIsLimbo(object) || object.garrisonHostObjectId>=0) continue;
        if(state.specialMechanismsInitialized && (state.specialMechanisms.isChronoInTransit(object.instanceId)
            || state.specialMechanisms.isTemporalFrozen(object.instanceId))) continue;
        const auto behavior = state.behaviors.find(object.instanceId);
        bool moving = object.renderMoveActive || (behavior!=state.behaviors.end()
            && (behavior->second.state==RuntimeBehaviorState::Moving || behavior->second.state==RuntimeBehaviorState::Chasing
                || (behavior->second.kind==RuntimeBehaviorKind::Move
                    && (behavior->second.state==RuntimeBehaviorState::Queued
                        // Movement's cadence wait still belongs to the same
                        // motor run while another path step remains.
                        || (behavior->second.state==RuntimeBehaviorState::Cooldown && !behavior->second.path.empty())))));
        if(!moving) continue;
        sources.push_back({&object,trimAsciiLocal(unit->moveSound),object.type});
    }
    return advanceRuntimeDiskLoopSources(state,state.diskMoveAudio,sources,nowMilliseconds,runtimeDiskMoveSoundHandle);
}

std::uint64_t runtimeGattlingSoundHandle(int objectId, int channel) {
    return 0x0400000000000000ULL
        | (std::uint64_t(std::uint32_t(objectId)) << 2) | unsigned(channel);
}

bool advanceRuntimeGattlingWeaponAudio(RuntimeProductionHostState& state, std::uint64_t nowMilliseconds) {
    if (!state.rulesLoaded || !state.worldAudioCatalogLoaded) return false;
    std::vector<RuntimeDiskLoopSource> sources;
    for (const auto& [id, behavior] : state.behaviors) {
        if (behavior.kind != RuntimeBehaviorKind::Attack || !runtimeGattlingFiringRunActive(state, id)) continue;
        const auto* object = findObjectById(state, id);
        if (!object) continue;
        // A load rehydrates an already-hot run from saved gameplay clocks.
        // Otherwise the first successful discharge arms the local voice.
        const auto heat = state.gattlingWeaponTimers.find(id);
        const bool restoredHotRun = !state.gattlingAudio.primed
            && heat != state.gattlingWeaponTimers.end() && heat->second > 0;
        if (!restoredHotRun && !state.gattlingAudio.sources.contains(id)
            && !state.gattlingAudio.newlyCreatedSources.contains(id)) continue;
        std::optional<WorldObjectInstance> terrain;
        if (behavior.attackGround)
            terrain = runtimeGroundAttackTargetObject(state, behavior.attackBridgeCell);
        else if (behavior.attackBridge)
            terrain = runtimeBridgeAttackTargetObject(state, behavior.attackBridgeCell, 0);
        const auto* target = terrain ? &*terrain : findObjectById(state, behavior.targetObjectId);
        const auto profile = runtimeWeaponProfileForObject(state, *object, target, behavior.forceFire);
        if (!profile.weapon) continue;
        const auto* sound = state.worldAudioCatalog.find(profile.weapon->report);
        if (!sound || !sound->loop || sound->samples.empty()) continue;
        sources.push_back({object, sound->eventId, sound->eventId});
    }
    // The existing source-voice sequencer queues body buffers ahead of their
    // device boundary and honors the original random/all/attack/decay data.
    return advanceRuntimeDiskLoopSources(state, state.gattlingAudio, sources,
        nowMilliseconds, runtimeGattlingSoundHandle);
}

bool manageRuntimeGattlingWeaponReport(RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker, const WeaponType& weapon) {
    if (!runtimeCachedCapabilityProfileForObject(state, attacker).gattling) return false;
    const auto* sound = state.worldAudioCatalog.find(weapon.report);
    if (!sound || !sound->loop || sound->samples.empty()) return false;
    const auto heat = state.gattlingWeaponTimers.find(attacker.instanceId);
    const bool restoredHotRun = !state.gattlingAudio.primed
        && heat != state.gattlingWeaponTimers.end() && heat->second > 0;
    // A resumed pending burst can fire before the first presentation pass.
    // Saved heat must still resume the body instead of replaying the intro.
    if (!restoredHotRun && !state.gattlingAudio.sources.contains(attacker.instanceId))
        state.gattlingAudio.newlyCreatedSources.insert(attacker.instanceId);
    // Retail 0x70DE70 owns the report for the run/stage; 0x6FF365 skips
    // ordinary per-round reports. A successful first shot starts the voice.
    advanceRuntimeGattlingWeaponAudio(state, runtimeWorldAudioClockNow(state));
    return true;
}

std::string runtimeTurretSoundForObject(const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    if (object.kind == WorldObjectKind::Structure) {
        const auto* type = findRuntimeBuildingType(state.rules, object);
        return type && type->turret ? trimAsciiLocal(type->turretRotateSound) : std::string();
    }
    const auto* type = findRuntimeUnitType(state.rules, object);
    return type && type->turret ? trimAsciiLocal(type->turretRotateSound) : std::string();
}

bool runtimeTurretSoundObjectPresent(const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    return object.alive && object.health > 0 && !object.rubble && !runtimeGateObjectIsLimbo(object)
        && object.garrisonHostObjectId < 0
        && !(state.specialMechanismsInitialized && (state.specialMechanisms.isChronoInTransit(object.instanceId)
            || state.specialMechanisms.isCarrierChildDocked(object.instanceId)));
}

void queueRuntimeTurretSoundActivity(const RuntimeGameSessionState& state, WorldObjectInstance& object,
    const std::string& event, bool rotating) {
    if (event.empty()) return;
    if (object.pendingTurretSounds.size() >= 32) object.pendingTurretSounds.erase(object.pendingTurretSounds.begin());
    object.pendingTurretSounds.push_back({event, state.tick, object.x, object.y, object.level, rotating});
}

void markRuntimeTurretSoundRotation(const RuntimeGameSessionState& state, WorldObjectInstance& object) {
    if (!runtimeTurretSoundObjectPresent(state, object)) return;
    const auto event = runtimeTurretSoundForObject(state, object);
    if (event.empty()) return;
    if (object.activeTurretSoundEvent != event) {
        queueRuntimeTurretSoundActivity(state, object, object.activeTurretSoundEvent, false);
        object.activeTurretSoundEvent = event;
        queueRuntimeTurretSoundActivity(state, object, event, true);
    }
    object.turretSoundActivityTick = state.tick;
}

void finishRuntimeTurretSoundActivities(const RuntimeGameSessionState& state, GameWorld& world) {
    for (auto& object : world.accessObjects()) {
        if (object.activeTurretSoundEvent.empty()) continue;
        if (object.turretSoundActivityTick != state.tick || !runtimeTurretSoundObjectPresent(state, object)
            || runtimeTurretSoundForObject(state, object) != object.activeTurretSoundEvent) {
            queueRuntimeTurretSoundActivity(state, object, object.activeTurretSoundEvent, false);
            object.activeTurretSoundEvent.clear(); object.turretSoundActivityTick = -1;
        }
    }
}

bool advanceRuntimeWorldAudio(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeWorldAudio", state.tick);
    if (!state.worldAudioInitialized) {
        initializeRuntimeWorldAudio(state);
    }
    const std::uint64_t before = state.worldAudio.stateHash();
    // An empty authority has no attenuation or playback to calculate. Keep
    // its normal clock/reconciliation/backend housekeeping without resolving
    // the listener's terrain surface on every otherwise idle simulation tick.
    RuntimeWorldAudioContext audioContext;
    if (state.worldAudio.handles().empty() && !runtimePresentationReferenceEnabled()) {
        audioContext.catalog = &state.worldAudioCatalog;
    } else {
        audioContext = runtimeWorldAudioContext(state);
    }
    const std::vector<RuntimeWorldAudioAction> actions = state.worldAudio.tick(
        audioContext,
        state.tick
    );
    applyRuntimeWorldAudioBackendActions(state, actions);
    reapRuntimeWorldAudioBackendBindings(state);
    return before != state.worldAudio.stateHash() || !actions.empty();
}

bool prepareRuntimeAudiovisualSaveCandidates(
    const RuntimeProductionHostState& state,
    RuntimeWorldAudioSystemState& worldAudio,
    RuntimeLightingSystemState& lighting,
    RuntimeParticleSystemState& particles,
    std::string& errorMessage) {
    worldAudio = state.worldAudioInitialized
        ? state.worldAudio
        : RuntimeWorldAudioSystemState(RuntimeWorldAudioConfig{
            std::max(1, state.config.fixedTickHz), 4096U
        });
    if (worldAudio.lastAdvancedTick() > state.tick) {
        errorMessage = "world audio clock is ahead of the runtime tick";
        return false;
    }
    if (worldAudio.lastAdvancedTick() < state.tick) {
        if (state.worldAudioInitialized
            && !worldAudio.handles().empty()) {
            errorMessage = "active world audio authority is behind runtime";
            return false;
        }
        (void)worldAudio.tick(runtimeWorldAudioContext(state), state.tick);
    }

    lighting = state.lightingInitialized
        ? state.lighting
        : RuntimeLightingSystemState(
            state.mapFacts.lighting,
            runtimeLightingConfigForState(state)
        );
    if (lighting.lastAdvancedTick() > state.tick) {
        errorMessage = "lighting clock is ahead of the runtime tick";
        return false;
    }
    if (lighting.lastAdvancedTick() < state.tick) {
        (void)lighting.tick(state.tick);
    }

    particles = state.particlesInitialized
        ? state.particles
        : RuntimeParticleSystemState(std::max(1, state.config.fixedTickHz));
    if (particles.currentTick() > state.tick
        || particles.fixedTickHz() != std::max(1, state.config.fixedTickHz)) {
        errorMessage = "particle authority clock/config does not match runtime";
        return false;
    }
    if (particles.currentTick() < state.tick
        && !particles.alignEmptyStateTick(state.tick)) {
        errorMessage = "active particle authority is behind the runtime tick";
        return false;
    }
    return true;
}

int runtimeResourceValueForCell(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain) {
    if (!terrain.hasOverlay || (!terrain.overlayIsOre && !terrain.overlayIsGem)) {
        return 0;
    }

    const int image = runtimeTiberiumImageForResourceFamily(
        runtimeResourceFamilyForTerrain(terrain)
    );
    const TiberiumType* tiberium =
        runtimeTiberiumTypeForImage(state.rules, image);
    if (tiberium != nullptr && tiberium->value > 0) {
        return tiberium->value;
    }
    return runtimeTiberiumFallbackValueForImage(image);
}

int runtimeResourceVariantFromOverlayIndex(const TerrainCell& terrain) {
    if (terrain.overlayIndex >= 27 && terrain.overlayIndex <= 38) {
        return terrain.overlayIndex - 26;
    }
    if (terrain.overlayIndex >= 102 && terrain.overlayIndex <= 121) {
        return terrain.overlayIndex - 101;
    }
    if (terrain.overlayIndex >= 127 && terrain.overlayIndex <= 146) {
        return terrain.overlayIndex - 126;
    }
    if (terrain.overlayIndex >= 147 && terrain.overlayIndex <= 166) {
        return terrain.overlayIndex - 146;
    }
    return -1;
}

int runtimeResourceVariantForTerrain(const TerrainCell& terrain) {
    const std::string family = runtimeResourceFamilyForTerrain(terrain);
    const int maxVariant = family == "GEM" ? 12 : 20;
    int variant = runtimeResourceVariantFromOverlayIndex(terrain);
    if (variant <= 0) {
        variant = runtimeTrailingResourceNumber(terrain.overlayType);
    }
    if (variant <= 0) {
        variant = runtimeTrailingResourceNumber(terrain.overlayImage);
    }
    return std::clamp(variant, 1, maxVariant);
}

int runtimeGeneratedResourceVariant(
    const TerrainCell& terrain,
    const std::string& family) {
    std::uint64_t hash = runtimeStableHashString("runtimeOreOverlayVariant");
    runtimeStableHashMix(hash, terrain.x);
    runtimeStableHashMix(hash, terrain.y);
    runtimeStableHashMixString(hash, family);
    const int maxVariant = family == "GEM"
        ? 12
        : runtimeGeneratedOreVariantCount;
    return 1 + static_cast<int>(hash % static_cast<std::uint64_t>(maxVariant));
}

std::string runtimeImpactBodyCenterPresentationRecord(
    const RuntimeParticleAirbornePresentation& presentation,
    int spriteOffsetY) {
    std::ostringstream output;
    output << runtimeBoolText(presentation.enabled)
           << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(presentation.sourceCellSum)
           << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(presentation.sourceLevel)
           << '|' << presentation.factoryLiftCancellation
           << '|' << spriteOffsetY << '|' << presentation.roofs.size();
    for (const auto& roof : presentation.roofs) {
        output << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(roof.cellSum)
               << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(roof.level)
               << '|' << roof.authoredHeight << '|' << roof.nativeSpriteHeight
               << '|' << runtimeBoolText(roof.rubble);
    }
    output << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(presentation.clearanceFraction);
    if (presentation.resolvedClearanceLevel >= 0.0)
        output << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(presentation.resolvedClearanceLevel);
    return output.str();
}

bool parseRuntimeImpactBodyCenterPresentationRecord(
    const std::string& value,
    RuntimeParticleAirbornePresentation& presentation,
    int& spriteOffsetY) {
    const auto fields = splitRuntimeSaveFields(value);
    RuntimeParticleAirbornePresentation candidate;
    int offset = 0, count = 0;
    if (fields.size() < 6U
        || !parseRuntimeStrictBool(fields[0], candidate.enabled)
        || !runtimeMultiplayerSnapshotDoubleFromBits(fields[1], candidate.sourceCellSum)
        || !runtimeMultiplayerSnapshotDoubleFromBits(fields[2], candidate.sourceLevel)
        || !parseRuntimeInt(fields[3], candidate.factoryLiftCancellation)
        || !parseRuntimeInt(fields[4], offset)
        || !parseRuntimeInt(fields[5], count)
        || count < 0 || static_cast<std::size_t>(count) > runtimeParticleMaximumClearanceRoofs
        || (fields.size() != 6U + 5U * static_cast<std::size_t>(count)
            && fields.size() != 7U + 5U * static_cast<std::size_t>(count)
            && fields.size() != 8U + 5U * static_cast<std::size_t>(count))
        || offset < -4096 || offset > 4096
        || candidate.factoryLiftCancellation < -1 || candidate.factoryLiftCancellation > 34) return false;
    // Same bounded geometry contract as frozen airborne particle presentation.
    const auto coordinate = [](double number, double scale) {
        return std::isfinite(number) && std::abs(number) <= scale * 1048576.0;
    };
    if (!coordinate(candidate.sourceCellSum, 2.0) || !coordinate(candidate.sourceLevel, 1.0)) return false;
    candidate.roofs.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const auto base = 6U + 5U * static_cast<std::size_t>(index);
        RuntimeParticleClearanceRoof roof;
        if (!runtimeMultiplayerSnapshotDoubleFromBits(fields[base], roof.cellSum)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[base + 1U], roof.level)
            || !parseRuntimeInt(fields[base + 2U], roof.authoredHeight)
            || !parseRuntimeInt(fields[base + 3U], roof.nativeSpriteHeight)
            || !parseRuntimeStrictBool(fields[base + 4U], roof.rubble)
            || !coordinate(roof.cellSum, 2.0) || !coordinate(roof.level, 1.0)
            || roof.authoredHeight < 0 || roof.authoredHeight > 65536
            || roof.nativeSpriteHeight < 0 || roof.nativeSpriteHeight > 65536) return false;
        candidate.roofs.push_back(roof);
    }
    const std::size_t extras = 6U + 5U * static_cast<std::size_t>(count);
    if (fields.size() >= extras + 1U
        && (!runtimeMultiplayerSnapshotDoubleFromBits(fields[extras], candidate.clearanceFraction)
            || !std::isfinite(candidate.clearanceFraction) || candidate.clearanceFraction < 0.0
            || candidate.clearanceFraction > 1.0)) return false;
    if (fields.size() == extras + 2U
        && (!runtimeMultiplayerSnapshotDoubleFromBits(fields[extras + 1U], candidate.resolvedClearanceLevel)
            || !std::isfinite(candidate.resolvedClearanceLevel)
            || candidate.resolvedClearanceLevel < 0.0
            || candidate.resolvedClearanceLevel > 64.0)) return false;
    if (!candidate.enabled && (candidate != RuntimeParticleAirbornePresentation{} || offset != 0)) return false;
    presentation = std::move(candidate);
    spriteOffsetY = offset;
    return true;
}

bool runtimeCameraBoundsPassReference() {
    if (runtimeCameraBoundsPassReferenceOverride >= 0)
        return runtimeCameraBoundsPassReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_CAMERA_BOUNDS_PASS_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

RuntimeCombinedCameraBounds runtimeCombinedCameraWorldBounds(
    const RuntimeProductionHostState& state,
    const RuntimeRadarProjection* completedProjection) {
    RuntimeCombinedCameraBounds result;
    const RuntimeRadarProjection projection = completedProjection != nullptr
        ? *completedProjection : buildRuntimeRadarProjection(state);
    const int halfWidth = std::max(1, state.camera.tileWidth / 2);
    const int halfHeight = std::max(1, state.camera.tileHeight / 2);
    const auto append = [](RuntimeCameraWorldBounds& bounds,
                           const RuntimeCameraWorldBounds& cell) {
        if (!bounds.valid) {
            bounds = cell;
        } else {
            bounds.left = std::min(bounds.left, cell.left);
            bounds.top = std::min(bounds.top, cell.top);
            bounds.right = std::max(bounds.right, cell.right);
            bounds.bottom = std::max(bounds.bottom, cell.bottom);
        }
    };
    for (const TerrainCell& terrain : state.world.getTerrainCells()) {
        // Preserve the original integer projection and truncation order.
        const int projectedX = ((terrain.x - terrain.y) * state.camera.tileWidth) / 2;
        const int projectedY = ((terrain.x + terrain.y) * state.camera.tileHeight) / 2
            - ((terrain.level * state.camera.tileHeight) / 2);
        const RuntimeCameraWorldBounds cell{projectedX - halfWidth,
            projectedY - halfHeight, projectedX + halfWidth,
            projectedY + halfHeight, true};
        append(result.full, cell);
        if (!projection.valid || runtimeRadarTerrainCellIntersectsProjection(projection, terrain))
            append(result.playable, cell);
    }
    // A disjoint LocalSize uses the original complete decoded envelope.
    if (!result.playable.valid) result.playable = result.full;
    return result;
}

RuntimeCombinedCameraBounds runtimeMemoizedCameraWorldBounds(const RuntimeProductionHostState& state) {
    // Preserve the projection builder's original cache behavior and its actual
    // returned values. Every terrain geometry value is checked on every hit.
    const auto projection = buildRuntimeRadarProjection(state);
    const RuntimeCameraGeometryProjection key{projection.valid, projection.minX,
        projection.minY, projection.maxX, projection.maxY, projection.fullMapWidth};
    const auto& cells = state.world.getTerrainCells();
    const auto fromMemo = [](const RuntimeCameraGeometryBounds& b) {
        return RuntimeCameraWorldBounds{b.left,b.top,b.right,b.bottom,b.valid};
    };
    if (const auto hit = state.cameraGeometryMemo.find(cells,state.camera.tileWidth,
        state.camera.tileHeight,key))
        return {fromMemo(hit->playable),fromMemo(hit->full)};
    const auto result = runtimeCombinedCameraWorldBounds(state,&projection);
    const auto toMemo = [](const RuntimeCameraWorldBounds& b) {
        return RuntimeCameraGeometryBounds{b.left,b.top,b.right,b.bottom,b.valid};
    };
    state.cameraGeometryMemo.store(cells,state.camera.tileWidth,state.camera.tileHeight,key,
        {toMemo(result.playable),toMemo(result.full)});
    return result;
}

bool runtimeInfantryWaterContextReference() {
    if (runtimeInfantryWaterContextReferenceOverride >= 0)
        return runtimeInfantryWaterContextReferenceOverride != 0;
    static const bool environmentReference = [] {
        const char* value = std::getenv("CONCORD_INFANTRY_WATER_CONTEXT_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return environmentReference;
}

bool runtimeInfantryWaterBatchContextReference() {
    if (runtimeInfantryWaterBatchContextReferenceOverride >= 0)
        return runtimeInfantryWaterBatchContextReferenceOverride != 0;
    static const bool environmentReference = [] {
        const char* value = std::getenv("CONCORD_INFANTRY_WATER_BATCH_CONTEXT_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return environmentReference;
}

void runtimeObserveInfantryWaterContextConstruction() {
    if (runtimeInfantryWaterContextConstructionObserver)
        ++*runtimeInfantryWaterContextConstructionObserver;
}

RuntimeInfantryWaterPose runtimeInfantryWaterPose(
    RuntimeProductionHostState& state, const WorldObjectInstance& object) {
    RuntimeInfantryWaterPose pose;
    if (object.kind != WorldObjectKind::Infantry || object.airborne
        || !object.alive || object.health <= 0) return pose;
    const auto* unit = findRuntimeUnitType(state.rules, object);
    if (!unit || (unit->enterWaterSound.empty() && unit->leaveWaterSound.empty())) return pose;
    pose.x = object.hasRenderPosition ? object.renderX : object.x;
    pose.y = object.hasRenderPosition ? object.renderY : object.y;
    if (!std::isfinite(pose.x) || !std::isfinite(pose.y)
        || pose.x < 0 || pose.x >= 511.5 || pose.y < 0 || pose.y >= 511.5
        || !MapQuery(state.mapFacts).getCell(static_cast<int>(std::floor(pose.x + 0.5)),
            static_cast<int>(std::floor(pose.y + 0.5)))) return pose;
    auto context = runtimeTerrainMetadataContext(state);
    runtimeObserveInfantryWaterContextConstruction();
    context.mapFacts = &state.mapFacts;
    pose.valid = true;
    pose.swimming = resolveRuntimeInfantrySwimming(object, context, state.renderCache);
    return pose;
}

const RuntimeMapRenderContext* runtimeBorrowInfantryWaterMetadataContext(RuntimeProductionHostState& state) {
    // An absent or disabled inner scope masks the outer scope. The general
    // C16 query does no source comparisons without an active batch scope.
    if (!runtimeActiveInfantryWaterBehaviorContext || runtimeInfantryWaterContextReference()
        || runtimeInfantryWaterBatchContextReference()) return nullptr;
    return runtimeActiveInfantryWaterBehaviorContext->borrow(state);
}

void emitRuntimeInfantryWaterTransition(
    RuntimeProductionHostState& state, const WorldObjectInstance& object,
    const RuntimeInfantryWaterPose& before,
    RuntimeInfantryWaterPoseQuery* query) {
    if (!before.valid) return;
    const auto after = query && query->matches(state)
        ? query->pose(object) : runtimeInfantryWaterPose(state, object);
    // Only a real walking/swimming step can emit. Loading, spawning, bridge
    // changes in place and teleportation do not replay an entry announcement.
    if (!after.valid || before.swimming == after.swimming
        || (before.x == after.x && before.y == after.y)
        || std::abs(after.x - before.x) > 1.0 || std::abs(after.y - before.y) > 1.0
        || !runtimeObjectVisibleToOwner(state, state.economy.owner, object)) return;
    const auto* unit = findRuntimeUnitType(state.rules, object);
    const auto sound = trimAsciiLocal(after.swimming ? unit->enterWaterSound : unit->leaveWaterSound);
    if (!sound.empty() && toLowerAsciiLocal(sound) != "none" && toLowerAsciiLocal(sound) != "<none>")
        emitRuntimeSound(state, "sound:" + sound, runtimeSpatialAudioPositionForObject(object));
}

bool runtimeChronosphereSelectionVisibleToLocalOwner(
    const RuntimeGameSessionState& state, const RuntimeImpactEffect& effect) {
    if (effect.lifecycleState != "ChronosphereSelection") return true;
    const auto owner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    if (owner.empty() || owner != toLowerAsciiLocal(trimAsciiLocal(effect.owner))) return false;
    const auto* weapon = state.superWeapons.findSidebarSnapshot(effect.owner, effect.weaponId);
    return weapon && weapon->state == SuperWeaponRuntimeState::AwaitingSecondTarget
        && weapon->firstTargetCell.x == effect.cell.x && weapon->firstTargetCell.y == effect.cell.y;
}

std::optional<RuntimeImpactRenderActor> runtimeLocalChronosphereRenderActor(
    const RuntimeProductionHostState& state) {
    if (!state.localChronosphereMarker) return std::nullopt;
    const auto& animation = state.localChronosphereMarker->animation;
    const auto& intent = state.multiplayerIntentRouter.superWeapon();
    const auto* source = state.superWeapons.findSidebarSnapshot(animation.owner, animation.weaponId);
    const bool sourceAvailable = source && (source->state == SuperWeaponRuntimeState::Ready
        || source->state == SuperWeaponRuntimeState::SelectingTarget
        || source->state == SuperWeaponRuntimeState::AwaitingSecondTarget);
    if (!sourceAvailable || !intent || intent->expectedTargetCount != 2
        || intent->targets.empty()
        || toLowerAsciiLocal(intent->superWeaponId) != toLowerAsciiLocal(animation.weaponId)
        || intent->targets.front().cell.x != animation.cell.x
        || intent->targets.front().cell.y != animation.cell.y) {
        state.localChronosphereMarker.reset();
        return std::nullopt;
    }
    // An observer/allied view never inherits another player's private marker.
    // Keep its clock intact if a temporary view switches back to the caster.
    const auto localOwner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    if (localOwner.empty() || localOwner != toLowerAsciiLocal(trimAsciiLocal(animation.owner)))
        return std::nullopt;
    const int step = std::max(0, state.tick - state.localChronosphereMarker->startedTick)
        / std::max(1, animation.animationFrameDelayTicks);
    const int intro = std::max(0, animation.animationLoopStartFrame - animation.animationStartFrame);
    const int span = std::max(1, animation.animationLoopEndFrame - animation.animationLoopStartFrame);
    auto actor = makeRuntimeImpactRenderActor(animation);
    actor.frameIndex = step < intro ? animation.animationStartFrame + step
        : animation.animationLoopStartFrame + (step - intro) % span;
    // A presentation marker has no finite effect lifetime or damage actor.
    actor.totalTicks = actor.remainingTicks = 1;
    actor.damage = 0;
    return actor;
}

bool advanceRuntimeLocalChronospherePresentation(RuntimeProductionHostState& state) {
    if (!state.localChronosphereMarker) return false;
    const auto actor = runtimeLocalChronosphereRenderActor(state);
    if (!state.localChronosphereMarker) {
        state.repaintRequested = true;
        return true;
    }
    if (!actor) return false;
    auto& marker = *state.localChronosphereMarker;
    const int step = std::max(0, state.tick - marker.startedTick)
        / std::max(1, marker.animation.animationFrameDelayTicks);
    if (step == marker.lastAnimationStep) return false;
    marker.lastAnimationStep = step;
    state.repaintRequested = true;
    return true;
}

std::uint64_t runtimeChronosphereSelectionSoundHandle(bool decay) {
    return decay ? 0xD000000000000002ULL : 0xD000000000000001ULL;
}

bool runtimeChronosphereSelectionAudioOwnerVisible(const RuntimeGameSessionState& state,
    const RuntimeChronosphereSelectionAudioState& audio) {
    const auto owner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    return !state.remoteHumanHouseScopeActive && !owner.empty()
        && owner == toLowerAsciiLocal(trimAsciiLocal(audio.owner));
}

bool runtimeChronosphereSelectionAudioBindingValid(const RuntimeProductionHostState& state) {
    const auto& audio = state.chronosphereSelectionAudio;
    const auto matches = [&](const RuntimeImpactEffect& marker) {
        return toLowerAsciiLocal(trimAsciiLocal(marker.owner))
                == toLowerAsciiLocal(trimAsciiLocal(audio.owner))
            && toLowerAsciiLocal(trimAsciiLocal(marker.weaponId))
                == toLowerAsciiLocal(trimAsciiLocal(audio.weaponId))
            && marker.cell.x == audio.cell.x && marker.cell.y == audio.cell.y;
    };
    const auto* source = state.superWeapons.findSidebarSnapshot(audio.owner, audio.weaponId);
    if (!source) return false;
    if (state.localChronosphereMarker && matches(state.localChronosphereMarker->animation)) {
        const auto& intent = state.multiplayerIntentRouter.superWeapon();
        const bool available = source->state == SuperWeaponRuntimeState::Ready
            || source->state == SuperWeaponRuntimeState::SelectingTarget
            || source->state == SuperWeaponRuntimeState::AwaitingSecondTarget;
        if (available && intent && intent->expectedTargetCount == 2 && !intent->targets.empty()
            && toLowerAsciiLocal(trimAsciiLocal(intent->superWeaponId))
                == toLowerAsciiLocal(trimAsciiLocal(audio.weaponId))
            && intent->targets.front().cell.x == audio.cell.x
            && intent->targets.front().cell.y == audio.cell.y) return true;
    }
    if (source->state != SuperWeaponRuntimeState::AwaitingSecondTarget
        || source->firstTargetCell.x != audio.cell.x
        || source->firstTargetCell.y != audio.cell.y) return false;
    return std::any_of(state.impactEffects.begin(), state.impactEffects.end(),
        [&](const RuntimeImpactEffect& effect) {
            return effect.lifecycleState == "ChronosphereSelection" && matches(effect);
        });
}

void hardStopRuntimeChronosphereSelectionAudio(RuntimeProductionHostState& state) {
    for (bool decay : {false, true}) {
        RuntimeWorldAudioAction action;
        action.kind = RuntimeWorldAudioActionKind::Stop;
        action.handleId = runtimeChronosphereSelectionSoundHandle(decay);
        action.eventTick = state.tick;
        action.reason = "private Chronosphere selection stopped";
        applyRuntimeWorldAudioBackendAction(state, action);
    }
    auto& sound = state.chronosphereSelectionAudio.sound;
    sound.active = false;
    sound.gainPermille = 0;
    sound.nextSampleMilliseconds = sound.queuedUntilMilliseconds = 0;
}

std::uint64_t playRuntimeChronosphereSelectionSample(RuntimeProductionHostState& state,
    const RuntimeWorldAudioSoundSpec& spec, const std::string& sample, bool decay) {
    auto& audio = state.chronosphereSelectionAudio;
    auto& sound = audio.sound;
    const auto variation = runtimeAudioVariationFor(spec.variation,
        runtimeAudioVariationSeed(spec.eventId, runtimeCellKey(audio.cell.x, audio.cell.y),
            sound.ordinal));
    RuntimeWorldAudioAction action;
    action.handleId = runtimeChronosphereSelectionSoundHandle(decay);
    action.eventId = spec.eventId;
    action.sampleName = sample;
    action.cell = audio.cell;
    action.eventTick = state.tick;
    action.playbackOrdinal = sound.ordinal++;
    action.frequencyPermille = variation.frequencyPermille;
    action.authoredGainPermille = std::clamp(spec.volumePercent * 10
        + variation.volumeDeltaPermille, 0, 1000);
    action.gainPermille = sound.gainPermille;
    action.local = spec.local;
    action.queueAfterCurrent = !decay && spec.loop;
    action.reason = decay ? "Chronosphere selection decay" : "Chronosphere selection sequence";
    applyRuntimeWorldAudioBackendAction(state, action);
    const auto path = findRuntimeSoundSamplePath(state, sample);
    const auto duration = std::uint64_t(path ? std::max(1, runtimeWavDurationTicks(*path, 1000)) : 1000);
    return std::max<std::uint64_t>(1,
        (duration * 1000 + std::max(1, variation.frequencyPermille) - 1)
            / std::max(1, variation.frequencyPermille));
}

std::string nextRuntimeChronosphereSelectionLoopSample(RuntimeProductionHostState& state,
    const RuntimeWorldAudioSoundSpec& spec) {
    auto& audio = state.chronosphereSelectionAudio;
    auto& sound = audio.sound;
    const auto controls = toLowerAsciiLocal(state.soundIni.getValue(spec.eventId, "Control", ""));
    const bool attack = controls.find("attack") != std::string::npos && spec.samples.size() > 2;
    const bool decay = controls.find("decay") != std::string::npos && spec.samples.size() > 2;
    const bool all = controls.find("all") != std::string::npos;
    const std::size_t first = attack ? 1 : 0;
    const std::size_t last = spec.samples.size() - (decay ? 1 : 0);
    if (sound.remainingLoopSamples.empty())
        for (std::size_t index = first; index < last; ++index)
            sound.remainingLoopSamples.push_back(index);
    const std::size_t pick = spec.randomSample
        ? runtimeAudioVariationSeed(spec.eventId, runtimeCellKey(audio.cell.x, audio.cell.y),
            sound.ordinal) % sound.remainingLoopSamples.size() : 0;
    const auto sample = spec.samples[sound.remainingLoopSamples[pick]];
    sound.remainingLoopSamples.erase(sound.remainingLoopSamples.begin() + pick);
    if (!all) sound.remainingLoopSamples.clear();
    return sample;
}

void beginRuntimeChronosphereSelectionSequence(RuntimeProductionHostState& state,
    const RuntimeWorldAudioSoundSpec& spec, std::uint64_t nowMilliseconds, bool firstSelection) {
    auto& sound = state.chronosphereSelectionAudio.sound;
    const auto controls = toLowerAsciiLocal(state.soundIni.getValue(spec.eventId, "Control", ""));
    const bool attack = spec.loop && controls.find("attack") != std::string::npos
        && spec.samples.size() > 2;
    sound.sample = firstSelection && attack ? spec.samples.front()
        : nextRuntimeChronosphereSelectionLoopSample(state, spec);
    sound.nextSampleMilliseconds = nowMilliseconds
        + playRuntimeChronosphereSelectionSample(state, spec, sound.sample, false);
    sound.queuedUntilMilliseconds = sound.nextSampleMilliseconds;
    sound.active = true;
    if (!spec.loop) return;
    // Prequeue a body segment so the device crosses the attack boundary
    // without relying on the next paint or on the simulation's pause state.
    sound.sample = nextRuntimeChronosphereSelectionLoopSample(state, spec);
    sound.queuedUntilMilliseconds += playRuntimeChronosphereSelectionSample(state, spec, sound.sample, false);
}

void stopRuntimeChronosphereSelectionAudio(RuntimeProductionHostState& state, bool decay) {
    const auto previous = state.chronosphereSelectionAudio;
    // Clearing both the private marker and the host event in one command
    // must not cut off the decay that the first clear already requested.
    if (decay && !previous.selected && previous.sound.stage == "decay"
        && previous.sound.active) return;
    hardStopRuntimeChronosphereSelectionAudio(state);
    state.chronosphereSelectionAudio = {};
    const auto* spec = state.worldAudioCatalog.find(previous.sound.event);
    const auto controls = toLowerAsciiLocal(state.soundIni.getValue(previous.sound.event, "Control", ""));
    if (!decay || !previous.selected || !previous.sound.active || !previous.sound.loop
        || !runtimeChronosphereSelectionAudioOwnerVisible(state, previous)
        || state.runtimeSuppressPlayerAudioFeedback || !spec || spec->samples.size() <= 2
        || controls.find("decay") == std::string::npos) return;
    const int gain = runtimeSpatialAudioGainPermille(state, *spec,
        {double(previous.cell.x), double(previous.cell.y), double(previous.cell.level)});
    if (gain <= 0) return;
    auto& audio = state.chronosphereSelectionAudio;
    audio = previous;
    audio.selected = false;
    audio.sound.stage = "decay";
    audio.sound.gainPermille = gain;
    audio.sound.sample = spec->samples.back();
    audio.sound.nextSampleMilliseconds = runtimeWorldAudioClockNow(state)
        + playRuntimeChronosphereSelectionSample(state, *spec, audio.sound.sample, true);
    audio.sound.queuedUntilMilliseconds = audio.sound.nextSampleMilliseconds;
    audio.sound.active = true;
}

void startRuntimeChronosphereSelectionAudio(RuntimeProductionHostState& state,
    const RuntimeImpactEffect& marker, std::uint64_t nowMilliseconds) {
    if (state.runtimeSuppressPlayerAudioFeedback || state.remoteHumanHouseScopeActive
        || trimAsciiLocal(marker.owner).empty()
        || toLowerAsciiLocal(trimAsciiLocal(marker.owner))
            != toLowerAsciiLocal(trimAsciiLocal(state.economy.owner))) return;
    // Resource-only smoke fixtures may create their first marker before the
    // normal startup has loaded soundmd. Loading clears only audio playback.
    if (!state.worldAudioCatalogLoaded) loadRuntimeSoundDatabase(state);
    const auto event = runtimeArtAnimationSound(state, state.art.findArtType(marker.animId));
    const auto* spec = state.worldAudioCatalogLoaded ? state.worldAudioCatalog.find(event) : nullptr;
    if (!spec || spec->samples.empty()) return;
    const auto& previous = state.chronosphereSelectionAudio;
    if (previous.selected && toLowerAsciiLocal(trimAsciiLocal(previous.owner))
            == toLowerAsciiLocal(trimAsciiLocal(marker.owner))
        && toLowerAsciiLocal(trimAsciiLocal(previous.weaponId))
            == toLowerAsciiLocal(trimAsciiLocal(marker.weaponId))
        && previous.cell.x == marker.cell.x && previous.cell.y == marker.cell.y
        && previous.sound.event == spec->eventId) return;
    stopRuntimeChronosphereSelectionAudio(state, false);
    auto& audio = state.chronosphereSelectionAudio;
    audio.owner = marker.owner;
    audio.weaponId = marker.weaponId;
    audio.cell = {marker.cell.x, marker.cell.y, marker.level};
    audio.selected = true;
    audio.sound.stage = "selection";
    audio.sound.owner = marker.owner;
    audio.sound.event = spec->eventId;
    audio.sound.loop = spec->loop;
    noteRuntimeSoundIntent(state, "sound:" + spec->eventId);
    audio.sound.gainPermille = runtimeSpatialAudioGainPermille(state, *spec,
        {double(audio.cell.x), double(audio.cell.y), double(audio.cell.level)});
    if (audio.sound.gainPermille > 0)
        beginRuntimeChronosphereSelectionSequence(state, *spec, nowMilliseconds, true);
}

bool advanceRuntimeChronosphereSelectionAudio(RuntimeProductionHostState& state, std::uint64_t nowMilliseconds) {
    // Remote-house simulation temporarily borrows the facade owner. It is
    // not an actual listener/view switch and must not restart this loop.
    if (state.remoteHumanHouseScopeActive) return false;
    auto& audio = state.chronosphereSelectionAudio;
    auto& sound = audio.sound;
    if (!audio.selected) {
        // Only the already-requested decay is managed here. A restored
        // authoritative first marker never recreates a selection attack.
        if (sound.stage != "decay" || !sound.active) return false;
        const auto* spec = state.worldAudioCatalog.find(sound.event);
        const int gain = spec ? runtimeSpatialAudioGainPermille(state, *spec,
            {double(audio.cell.x), double(audio.cell.y), double(audio.cell.level)}) : 0;
        if (nowMilliseconds >= sound.nextSampleMilliseconds || gain <= 0
            || state.runtimeSuppressPlayerAudioFeedback
            || !runtimeChronosphereSelectionAudioOwnerVisible(state, audio)) {
            hardStopRuntimeChronosphereSelectionAudio(state);
            audio = {};
            return true;
        }
        return false;
    }
    if (!runtimeChronosphereSelectionAudioBindingValid(state)) {
        stopRuntimeChronosphereSelectionAudio(state, true);
        return true;
    }
    const auto* spec = state.worldAudioCatalogLoaded ? state.worldAudioCatalog.find(sound.event) : nullptr;
    const int gain = spec ? runtimeSpatialAudioGainPermille(state, *spec,
        {double(audio.cell.x), double(audio.cell.y), double(audio.cell.level)}) : 0;
    if (!spec || spec->samples.empty() || gain <= 0 || state.runtimeSuppressPlayerAudioFeedback
        || !runtimeChronosphereSelectionAudioOwnerVisible(state, audio)) {
        if (!sound.active) return false;
        hardStopRuntimeChronosphereSelectionAudio(state);
        return true;
    }
    bool changed = false;
    if (sound.active && sound.gainPermille != gain) {
        RuntimeWorldAudioAction action;
        action.kind = RuntimeWorldAudioActionKind::SetGain;
        action.handleId = runtimeChronosphereSelectionSoundHandle();
        action.eventId = spec->eventId;
        action.cell = audio.cell;
        action.eventTick = state.tick;
        action.authoredGainPermille = spec->volumePercent * 10;
        action.gainPermille = gain;
        action.reason = "Chronosphere selection listener gain changed";
        applyRuntimeWorldAudioBackendAction(state, action);
        changed = true;
    }
    sound.gainPermille = gain;
    if (!spec->loop) {
        if (sound.active && nowMilliseconds >= sound.nextSampleMilliseconds) {
            sound.active = false;
            changed = true;
        }
        return changed;
    }
    if (!sound.active) {
        beginRuntimeChronosphereSelectionSequence(state, *spec, nowMilliseconds, false);
        return true;
    }
    bool needsBuffer = nowMilliseconds >= sound.nextSampleMilliseconds;
    if (state.runtimeSoundsEnabled && state.runtimeVoiceAudio && state.runtimeVoiceAudio->ready())
        needsBuffer = state.runtimeVoiceAudio->queuedBuffers(runtimeChronosphereSelectionSoundHandle()) < 2;
    if (!needsBuffer) return changed;
    sound.sample = nextRuntimeChronosphereSelectionLoopSample(state, *spec);
    sound.nextSampleMilliseconds = std::max(nowMilliseconds, sound.queuedUntilMilliseconds);
    sound.queuedUntilMilliseconds = sound.nextSampleMilliseconds
        + playRuntimeChronosphereSelectionSample(state, *spec, sound.sample, false);
    return true;
}

std::optional<RuntimeOpenTransportMuzzle> runtimeOpenTransportMuzzle(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const RuntimeWeaponProfile& profile) {
    const RuntimeOpenTransportSourceQueries queries(state);
    return ::runtimeOpenTransportMuzzle(queries, passenger, profile.openTransport);
}

WorldObjectInstance runtimeWeaponFiringPose(const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile) {
    WorldObjectInstance pose = attacker;
    if (pose.kind != WorldObjectKind::Infantry) return pose;
    bool secondary = profile.slot == RuntimeWeaponSlot::Secondary
        || profile.slot == RuntimeWeaponSlot::EliteSecondary
        || ((profile.slot == RuntimeWeaponSlot::Indexed || profile.slot == RuntimeWeaponSlot::EliteIndexed)
            && profile.indexedWeaponIndex == 1);
    if (const auto* unit = findRuntimeUnitType(state.rules, attacker)) {
        if (profile.slot == RuntimeWeaponSlot::OpenTransport)
            secondary = unit->openTransportWeapon == 1 || unit->openTransportWeapon == 3;
    }
    pose.mission = pose.deployed || profile.deployed
        || profile.slot == RuntimeWeaponSlot::Deploy || profile.slot == RuntimeWeaponSlot::EliteDeploy
        ? "DeployedFire" : secondary ? "SecondaryFire" : "Attack";
    return pose;
}

void alignRuntimeWeaponMuzzleAtCamera(RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile,
    int firingFacing, RuntimeProjectileSourcePosition& source, const RuntimeMapCamera& camera) {
    if (!runtimeSessionGeometryAvailable(state)) return;
    if (!state.artLoaded) return;
    const auto flh = runtimeWeaponFireFlh(state, attacker, profile, runtimeArtTypeForObject(state, attacker));
    if (!flh && !findRuntimeUnitType(state.rules, attacker)
        && !findRuntimeBuildingType(state.rules, attacker)) return;
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = &state.art;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;
    context.impactActors = runtimeImpactRenderActorsForRender(state);
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    const auto firingPose = runtimeWeaponFiringPose(state, attacker, profile);
    const auto port = runtimeOpenTransportMuzzle(state, attacker, profile);
    const auto& visibleSource = port ? *port->host : firingPose;
    const auto attachmentFlh = port ? port->flh : flh.value_or(RuntimeWeaponFireFlh{});
    // The caller supplies an unscrolled reference projection.
    const RuntimeInputPixelRect viewport{0, 0, 1024, 768};
    const auto placement = (state.geometry->runtimeObjectWorldRenderPlacement(visibleSource, state.world,
        viewport, context, camera, state.renderCache));
    if (profile.occupied) {
        const auto* art = runtimeArtTypeForObject(state, attacker);
        if (art && profile.occupantMuzzleIndex >= 0
            && profile.occupantMuzzleIndex < static_cast<int>(art->muzzleFlashes.size())) {
            const auto pixel = art->muzzleFlashes[profile.occupantMuzzleIndex];
            RuntimeProjectileRenderActor anchor;
            anchor.hasCurrentPosition = true;
            anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
            const auto logical = state.geometry->runtimeProjectileActorScreenPoint(anchor, viewport, camera);
            // ART window positions are native bitmap pixels relative to
            // the center-authored structure canvas at every camera zoom.
            source.screenOffsetX = placement.center.x + pixel.x - logical.x;
            source.screenOffsetY = placement.center.y + pixel.y - logical.y;
        }
        return;
    }
    if (!port && attacker.kind == WorldObjectKind::Infantry) {
        RuntimeInputPixelPoint native{};
        if (state.geometry->resolveRuntimeInfantryWeaponMuzzlePoint(firingPose, placement.center, context, camera, state.renderCache, native)) {
            RuntimeProjectileRenderActor anchor;
            anchor.hasCurrentPosition = true;
            anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
            const auto logical = state.geometry->runtimeProjectileActorScreenPoint(anchor, viewport, camera);
            source.screenOffsetX = native.x - logical.x;
            source.screenOffsetY = native.y - logical.y;
            return;
        }
        RuntimeInputPixelRect body{};
        if (state.geometry->resolveRuntimeObjectShpSpriteBounds(firingPose, {0, 0}, context,
                camera, state.renderCache, body))
            source.screenOffsetY += (body.top + body.bottom) / 2;
        const auto ordinary = state.geometry->runtimeObjectBodyAnchorForRender(firingPose, viewport, context, camera);
        source.screenOffsetX += placement.center.x - ordinary.x;
        source.screenOffsetY += placement.center.y - ordinary.y;
        return;
    }
    RuntimeInputPixelPoint marine{};
    if (!port && state.geometry->resolveRuntimeMarineWeaponMuzzlePoint(firingPose, placement.center, context,
            camera, state.renderCache, marine)) {
        RuntimeProjectileRenderActor anchor;
        anchor.hasCurrentPosition = true;
        anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
        const auto logical = state.geometry->runtimeProjectileActorScreenPoint(anchor, viewport, camera);
        source.screenOffsetX = marine.x - logical.x;
        source.screenOffsetY = marine.y - logical.y;
        return;
    }
    RuntimeInputPixelPoint structure{};
    const bool secondary = profile.slot == RuntimeWeaponSlot::Secondary
        || profile.slot == RuntimeWeaponSlot::EliteSecondary
        || ((profile.slot == RuntimeWeaponSlot::Indexed || profile.slot == RuntimeWeaponSlot::EliteIndexed)
            && profile.indexedWeaponIndex == 1);
    if (!port && state.geometry->resolveRuntimeStructureWeaponMuzzlePoint(firingPose, placement.center, context,
            camera, state.renderCache, attachmentFlh.forwardLeptons, attachmentFlh.lateralLeptons,
            attachmentFlh.heightLeptons, firingFacing, secondary, structure)) {
        RuntimeProjectileRenderActor anchor;
        anchor.hasCurrentPosition = true;
        anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
        const auto logical = state.geometry->runtimeProjectileActorScreenPoint(anchor, viewport, camera);
        source.screenOffsetX = structure.x - logical.x;
        source.screenOffsetY = structure.y - logical.y;
        return;
    }
    RuntimeInputPixelPoint modelMuzzle{};
    int lateral = attachmentFlh.lateralLeptons;
    if (runtimeCachedCapabilityProfileForObject(state, attacker).gattling) {
        const auto burst = state.combatDetails.bursts.find(attacker.instanceId);
        if (burst != state.combatDetails.bursts.end() && profile.weapon
            && burst->second.weaponId == profile.weapon->id && burst->second.nextShotIndex % 2 != 0)
            lateral = -lateral;
    }
    if (state.geometry->resolveRuntimeVoxelWeaponMuzzlePoint(visibleSource, placement.center, context,
            camera, state.renderCache, attachmentFlh.forwardLeptons, lateral, attachmentFlh.heightLeptons,
            port ? std::max(0, visibleSource.facing) : firingFacing, modelMuzzle, !port)) {
        RuntimeProjectileRenderActor anchor;
        anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
        anchor.hasCurrentPosition = true;
        const RuntimeInputPixelPoint logical = state.geometry->runtimeProjectileActorScreenPoint(anchor, viewport, camera);
        source.screenOffsetX = modelMuzzle.x - logical.x;
        source.screenOffsetY = modelMuzzle.y - logical.y;
    } else {
        const auto ordinary = state.geometry->runtimeObjectBodyAnchorForRender(visibleSource, viewport, context, camera);
        source.screenOffsetX += placement.center.x - ordinary.x;
        source.screenOffsetY += placement.center.y - ordinary.y;
    }
}

void alignRuntimeWeaponMuzzleToSprite(RuntimeProductionHostState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile,
    int firingFacing, RuntimeProjectileSourcePosition& source) {
    if (!runtimeSessionGeometryAvailable(state)) return;
    RuntimeMapCamera camera; camera.initialized = true;
    auto reference = source;
    alignRuntimeWeaponMuzzleAtCamera(state, attacker, profile, firingFacing, reference, camera);
    auto doubled = source;
    doubled.screenOffsetX = 2 * source.screenOffsetX - source.nativeFixedX;
    doubled.screenOffsetY = 2 * source.screenOffsetY - source.nativeFixedY;
    camera.tileWidth = 120; camera.tileHeight = 60;
    alignRuntimeWeaponMuzzleAtCamera(state, attacker, profile, firingFacing, doubled, camera);
    source.screenOffsetX = reference.screenOffsetX;
    source.screenOffsetY = reference.screenOffsetY;
    source.nativeFixedX = 2 * reference.screenOffsetX - doubled.screenOffsetX;
    source.nativeFixedY = 2 * reference.screenOffsetY - doubled.screenOffsetY;
}

bool evacuateRuntimeOwnerTransitionGarrisons(
    RuntimeProductionHostState& scratch,
    std::string& error) {
    std::map<int, std::vector<int>> occupantsByHost;
    for (const WorldObjectInstance& host : scratch.world.getObjects()) {
        if (host.garrisonOccupantIds.empty()) {
            continue;
        }
        for (int occupantId : host.garrisonOccupantIds) {
            const WorldObjectInstance* occupant =
                findRuntimeObjectByIdIncludingInactive(scratch, occupantId);
            if (occupant == nullptr) {
                error = "owner transition found a missing garrison occupant";
                return false;
            }
            if (occupant->owner != host.owner) {
                occupantsByHost[host.instanceId].push_back(occupantId);
            }
        }
    }

    std::unordered_set<int> reservedCells;
    const RuntimeGarrisonExitResolver resolver =
        [&scratch, &reservedCells](
            const WorldObjectInstance& occupant,
            const WorldObjectInstance& host,
            RuntimeGarrisonEvacuationReason,
            std::size_t
        ) -> std::optional<RuntimeGarrisonExitPlacement> {
            const std::optional<RuntimeCellPoint> exitCell =
                findRuntimePassableCellNear(
                    scratch,
                    occupant,
                    runtimeCellPointForObject(host),
                    4,
                    -1,
                    &reservedCells
                );
            if (!exitCell.has_value()) {
                return std::nullopt;
            }
            reservedCells.insert(runtimeCellKey(exitCell->x, exitCell->y));
            return RuntimeGarrisonExitPlacement{
                exitCell->x,
                exitCell->y,
                runtimeCellKey(exitCell->x, exitCell->y),
                terrainLevelAtRuntimeCell(scratch, exitCell->x, exitCell->y),
                runtimeCellIsBridge(scratch, exitCell->x, exitCell->y)
            };
        };

    for (const auto& [hostId, occupantIds] : occupantsByHost) {
        const RuntimeGarrisonSelectiveEvacuationResult result =
            scratch.garrisons.forceEvacuateOccupants(
                scratch.world.accessObjects(),
                hostId,
                occupantIds,
                RuntimeGarrisonEvacuationReason::Forced,
                resolver,
                false
            );
        if (!result.applied()) {
            error = "selective garrison evacuation rejected the owner batch";
            return false;
        }
        for (const RuntimeGarrisonEvacuationAction& action : result.actions) {
            if (action.disposition
                    == RuntimeGarrisonEvacuationDisposition::KeptInside) {
                error = "owner transition has no legal garrison exit";
                return false;
            }
            if (action.disposition
                    != RuntimeGarrisonEvacuationDisposition::Exited) {
                error = "owner transition produced a destructive evacuation";
                return false;
            }
            eraseRuntimeBehavior(scratch, action.occupantId);
            removeRuntimeSelection(scratch, action.occupantId);
            WorldObjectInstance* occupant =
                findMutableRuntimeObjectByIdIncludingInactive(
                    scratch,
                    action.occupantId
                );
            if (occupant == nullptr || !occupant->alive) {
                error = "garrison exit did not restore its occupant";
                return false;
            }
            assignRuntimeInfantrySubcellAtCurrentLocation(
                scratch,
                *occupant
            );
            if (occupant->subCell < 0) {
                error = "garrison exit could not reserve a subcell";
                return false;
            }
        }
        WorldObjectInstance* host =
            findMutableRuntimeObjectByIdIncludingInactive(scratch, hostId);
        if (host != nullptr) {
            setRuntimeMissionIntent(
                scratch,
                *host,
                host->garrisonOccupantIds.empty()
                    ? RuntimeMissionIntent::Idle
                    : RuntimeMissionIntent::Occupying,
                "ownerTransitionGarrison"
            );
        }
    }
    return true;
}

bool evacuateRuntimeOwnerTransitionTransports(
    RuntimeProductionHostState& scratch,
    std::string& error) {
    std::unordered_set<int> reservedCells;
    for (WorldObjectInstance& transport : scratch.world.accessObjects()) {
        if (!transport.alive || transport.transportPassengerIds.empty()) {
            continue;
        }
        std::vector<int> retainedPassengerIds;
        retainedPassengerIds.reserve(transport.transportPassengerIds.size());
        bool releasedAny = false;
        for (int passengerId : transport.transportPassengerIds) {
            WorldObjectInstance* passenger =
                findMutableRuntimeObjectByIdIncludingInactive(
                    scratch,
                    passengerId
                );
            if (passenger == nullptr || passenger->health <= 0) {
                error = "owner transition found an invalid transport manifest";
                return false;
            }
            if (runtimeOwnersAreAllied(
                    scratch,
                    transport.owner,
                    passenger->owner
                )) {
                retainedPassengerIds.push_back(passengerId);
                continue;
            }
            const std::optional<RuntimeCellPoint> exitCell =
                findRuntimePassableCellNear(
                    scratch,
                    *passenger,
                    runtimeCellPointForObject(transport),
                    2,
                    -1,
                    &reservedCells
                );
            if (!exitCell.has_value()) {
                error = "owner transition has no legal transport exit";
                return false;
            }
            eraseRuntimeBehavior(scratch, passengerId);
            passenger->x = exitCell->x;
            passenger->y = exitCell->y;
            passenger->cell = runtimeCellKey(exitCell->x, exitCell->y);
            passenger->level = terrainLevelAtRuntimeCell(
                scratch,
                exitCell->x,
                exitCell->y
            );
            passenger->onBridge = runtimeCellIsBridge(
                scratch,
                exitCell->x,
                exitCell->y
            );
            activateRuntimeWorldObject(scratch, *passenger);
            passenger->facing = transport.facing;
            passenger->turretFacing = -1;
            passenger->mission = runtimeMissionName(RuntimeMissionIntent::Idle);
            if (runtimeInfantryAbsorberType(scratch, transport) != nullptr) {
                departRuntimeAircraftDock(scratch, *passenger);
            }
            clearRuntimeRenderPosition(*passenger);
            assignRuntimeInfantrySubcellAtCurrentLocation(
                scratch,
                *passenger
            );
            if (passenger->kind == WorldObjectKind::Infantry
                && !passenger->airborne && passenger->subCell < 0) {
                error = "transport exit could not reserve a subcell";
                return false;
            }
            reservedCells.insert(runtimeCellKey(exitCell->x, exitCell->y));
            releasedAny = true;
        }
        transport.transportPassengerIds = std::move(retainedPassengerIds);
        refreshRuntimeTransportGunnerState(scratch, transport);
        if (releasedAny) {
            setRuntimeMissionIntent(
                scratch,
                transport,
                RuntimeMissionIntent::Idle,
                "ownerTransitionTransport"
            );
        }
    }
    return true;
}

std::string runtimeCrateAwardSound(
    const RuntimeGameSessionState& state,
    const std::string& type) {
    if (runtimeCrateTypeIs(type, "HealBase")) {
        return stripRuntimeSoundComment(
            state.rules.getCrateRules().healSound
        );
    }
    if (runtimeCrateTypeIs(type, "Veteran")) {
        return runtimeRulesSoundSection(
            state,
            "CratePromoteSound",
            "CratePromoted"
        );
    }
    if (runtimeCrateTypeIs(type, "Money")) {
        return runtimeRulesSoundSection(
            state,
            "CrateMoneySound",
            "CrateMoney"
        );
    }
    if (runtimeCrateTypeIs(type, "Reveal")) {
        return runtimeRulesSoundSection(
            state,
            "CrateRevealSound",
            "CrateReveal"
        );
    }
    if (runtimeCrateTypeIs(type, "Firepower")) {
        return runtimeRulesSoundSection(
            state,
            "CrateFireSound",
            "CrateFirePower"
        );
    }
    if (runtimeCrateTypeIs(type, "Armor")) {
        return runtimeRulesSoundSection(
            state,
            "CrateArmourSound",
            "CrateArmor"
        );
    }
    if (runtimeCrateTypeIs(type, "Speed")) {
        return runtimeRulesSoundSection(
            state,
            "CrateSpeedSound",
            "CrateSpeed"
        );
    }
    if (runtimeCrateTypeIs(type, "Unit")) {
        return runtimeRulesSoundSection(
            state,
            "CrateUnitSound",
            "CrateFreeUnit"
        );
    }
    return {};
}

void resetRuntimePlayerDefeatNotices(RuntimeProductionHostState& state) noexcept {
    state.playerDefeatNotices.lines.clear();
    state.playerDefeatNotices.announcedOwners.clear();
    state.playerDefeatNotices.suppressExisting = false;
}

void prepareRuntimePlayerDefeatNoticeLoadBaseline(RuntimeProductionHostState& state) {
    resetRuntimePlayerDefeatNotices(state);
    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode)) == "CAMPAIGNMISSION"
        || !state.economy.skirmishStartApplied) return;

    const bool authoritativeOutcome = state.config.multiplayerActive
        || runtimeLocalObserver(state.config) || state.cooperativeSession.active();
    if (!authoritativeOutcome && state.skirmishCountryByOwner.empty()) return;

    for (std::size_t index = 0; index < state.config.playerSlots.size(); ++index) {
        const auto& player = state.config.playerSlots[index];
        if (!player.enabled || runtimeObserverSlot(player)) continue;
        const std::string owner = runtimeSkirmishOwnerForSlot(index);
        const std::string ownerKey = runtimeCanonicalHumanHouseOwner(owner);
        const bool defeated = authoritativeOutcome
            ? state.multiplayerDefeatedOwners.contains(ownerKey)
            : runtimeSkirmishOwnerDefeated(state, owner);
        if (defeated) state.playerDefeatNotices.announcedOwners.insert(ownerKey);
    }
}

bool expireRuntimePlayerDefeatNotices(RuntimeProductionHostState& state, std::uint64_t nowMs) {
    auto& lines = state.playerDefeatNotices.lines;
    const auto previousSize = lines.size();
    std::erase_if(lines, [nowMs](const RuntimePlayerDefeatNotice& notice) {
        return nowMs >= notice.untilMs;
    });
    const bool changed = lines.size() != previousSize;
    if (changed) state.repaintRequested = true;
    return changed;
}

bool appendRuntimePlayerDefeatNotice(RuntimeProductionHostState& state,
    const std::string& ownerKey, std::uint64_t nowMs) {
    auto& notices = state.playerDefeatNotices;
    const auto inserted = notices.announcedOwners.insert(ownerKey);
    if (!inserted.second) return false;
    const std::uint64_t untilMs = runtimePlayerDefeatNoticeDeadline(nowMs);
    try {
        notices.lines.push_back({ownerKey, untilMs});
    } catch (...) {
        notices.announcedOwners.erase(inserted.first);
        throw;
    }
    state.repaintRequested = true;
    return true;
}

bool observeRuntimePlayerDefeatNotices(RuntimeProductionHostState& state,
    std::uint64_t nowMs) {
    bool changed = expireRuntimePlayerDefeatNotices(state, nowMs);
    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode)) == "CAMPAIGNMISSION"
        || !state.economy.skirmishStartApplied || state.tick < 5 || state.tick % 10 != 0)
        return changed;

    const bool authoritativeOutcome = state.config.multiplayerActive
        || runtimeLocalObserver(state.config) || state.cooperativeSession.active();
    if (!authoritativeOutcome && state.skirmishCountryByOwner.empty()) return changed;

    auto& notices = state.playerDefeatNotices;
    const bool suppressExisting = notices.suppressExisting;
    bool hasParticipant = false;
    // Roster order is stable in both builds and naturally bounds notices by the
    // actual combatants, without a hard-coded eight/twelve-player capacity.
    for (std::size_t index = 0; index < state.config.playerSlots.size(); ++index) {
        const auto& player = state.config.playerSlots[index];
        if (!player.enabled || runtimeObserverSlot(player)) continue;
        hasParticipant = true;
        const std::string owner = runtimeSkirmishOwnerForSlot(index);
        const std::string ownerKey = runtimeCanonicalHumanHouseOwner(owner);
        if (notices.announcedOwners.contains(ownerKey)) continue;
        const bool defeated = authoritativeOutcome
            ? state.multiplayerDefeatedOwners.contains(ownerKey)
            : runtimeSkirmishOwnerDefeated(state, owner);
        if (!defeated) continue;
        if (suppressExisting) notices.announcedOwners.insert(ownerKey);
        else changed = appendRuntimePlayerDefeatNotice(state, ownerKey, nowMs) || changed;
    }
    if (hasParticipant) notices.suppressExisting = false;
    // Terminal outcome ticks must still announce their newly defeated players.
    return changed;
}

bool observeRuntimePlayerDefeatNotices(RuntimeProductionHostState& state) {
    const std::uint64_t nowMs = runtimeUiClockNow(state);
    return observeRuntimePlayerDefeatNotices(state, nowMs);
}

void emitRuntimeUnitDeathSound(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& object,
    std::optional<RuntimeSpatialAudioPosition> source,
    bool includeCrashingSound) {
    if (object.kind == WorldObjectKind::Structure
        || std::any_of(state.wrecks.begin(), state.wrecks.end(),
            [&](const auto& wreck) {
                return wreck.object.instanceId == object.instanceId;
            })) {
        return;
    }
    const UnitType* unit = findRuntimeUnitTypeById(state.rules, object.type);
    const auto position = source.value_or(runtimeSpatialAudioPositionForObject(object));
    // Some damage modules mark victims inactive before notifying their exit.
    // Preserve the native surface-ship replacement cue even when no wreck was
    // retained; existing wrecks above already own that sound exactly once.
    const auto terrain = state.terrainByCell.find(runtimeCellKey(object.x, object.y));
    const TerrainCell* ground = terrain == state.terrainByCell.end() ? nullptr : terrain->second;
    const auto terrainInfo = ground ? runtimeTerrainInfoForCell(state, *ground) : RuntimeTerrainInfo{};
    const bool sinkingWithoutWreck = includeCrashingSound && unit != nullptr
        && object.garrisonHostObjectId < 0 && object.mission != "Passenger"
        && ground != nullptr && terrainInfo.water && !terrainInfo.bridge && !object.onBridge
        && unit->naval && !unit->underwater && !unit->organic
        && unit->weight >= unit->shipSinkingWeight && !object.airborne;
    if (sinkingWithoutWreck) {
        if (!trimAsciiLocal(unit->sinkingSound).empty()) {
            emitRuntimeSound(state, "sound:" + trimAsciiLocal(unit->sinkingSound), position, true);
        }
        return;
    }
    const bool crashingWithoutWreck = includeCrashingSound && unit != nullptr
        && object.airborne && !unit->missileSpawn
        && (unit->crashable
            || state.rules.findUnitType("AircraftType", object.type) != nullptr);
    if (crashingWithoutWreck && !trimAsciiLocal(unit->crashingSound).empty()) {
        emitRuntimeSound(state, "sound:" + trimAsciiLocal(unit->crashingSound), position, true);
    }
    if (unit != nullptr
        && runtimeObjectMovementMode(state, object) == PathMovementMode::Air) {
        const auto voices = splitRuntimeTokenList(unit->voiceCrashing);
        if (!voices.empty()) {
            const std::size_t seed =
                static_cast<std::size_t>(std::max(0, object.instanceId)) * 131U
                + static_cast<std::size_t>(std::max(0, state.tick)) * 17U
                + static_cast<std::size_t>(state.runtimeSoundEvents);
            emitRuntimeSound(state, voices[seed % voices.size()], position, true);
        }
    }
    if (crashingWithoutWreck) return;
    const std::string authored = unit == nullptr
        ? std::string{} : trimAsciiLocal(unit->dieSound);
    if (!authored.empty()) {
        auto sounds = splitRuntimeTokenList(authored);
        sounds.erase(std::remove_if(sounds.begin(), sounds.end(),
            [](const std::string& sound) {
                const std::string key = toLowerAsciiLocal(trimAsciiLocal(sound));
                return key == "none" || key == "<none>" || key == "-1";
            }), sounds.end());
        if (!sounds.empty()) {
            const std::size_t seed =
                static_cast<std::size_t>(std::max(0, object.instanceId)) * 131U
                + static_cast<std::size_t>(std::max(0, state.tick)) * 17U
                + static_cast<std::size_t>(state.runtimeSoundEvents) * 29U;
            emitRuntimeSound(state, "sound:" + sounds[seed % sounds.size()], position);
        }
    } else if (object.kind == WorldObjectKind::Unit
        && (unit == nullptr || !unit->missileSpawn)) {
        emitRuntimeSound(state, "sound:GenVehicleDie", position);
    }
}

std::vector<RuntimeGarrisonEvacuationAction>
evacuateRuntimeGarrisonHost(
    RuntimeProductionHostState& state,
    int hostObjectId,
    RuntimeGarrisonEvacuationReason reason,
    bool destroyWhenBlocked) {
    ensureRuntimeGarrisonsInitialized(state);
    WorldObjectInstance* host =
        findMutableRuntimeObjectByIdIncludingInactive(state, hostObjectId);
    if (host == nullptr || host->garrisonOccupantIds.empty()) {
        return {};
    }
    const std::string hostOwner = host->owner;

    std::unordered_set<int> reservedCells;
    ensureRuntimeSubcellsInitialized(state);
    std::unordered_map<int, RuntimeCellPoint> exitFromCells;
    std::unordered_map<int, int> exitSubcells;
    std::unordered_map<int, std::vector<int>> remainingExitSubcells;
    const RuntimeGarrisonExitResolver resolver =
        [&state, &reservedCells, &exitFromCells, &exitSubcells, &remainingExitSubcells](
            const WorldObjectInstance& occupant,
            const WorldObjectInstance& exitHost,
            RuntimeGarrisonEvacuationReason,
            std::size_t
        ) -> std::optional<RuntimeGarrisonExitPlacement> {
            // An exit must touch the foundation. Never materialize infantry
            // beyond a wall / blocking unit just because a farther cell is free.
            const auto footprint = runtimeStructureFootprintCellsForObject(state, exitHost);
            std::vector<std::pair<RuntimeCellPoint, RuntimeCellPoint>> exits;
            for (const auto& inside : footprint) {
                for (const RuntimeCellPoint delta : {RuntimeCellPoint{-1, 0},
                        RuntimeCellPoint{0, -1}, RuntimeCellPoint{1, 0}, RuntimeCellPoint{0, 1}}) {
                    const RuntimeCellPoint outside{inside.x + delta.x, inside.y + delta.y};
                    if (std::find(footprint.begin(), footprint.end(), outside) == footprint.end())
                        exits.emplace_back(outside, inside);
                }
            }
            const auto anchor = runtimeCellPointForObject(exitHost);
            std::sort(exits.begin(), exits.end(), [&anchor](const auto& a, const auto& b) {
                const double da = runtimeCellDistance(anchor, a.first);
                const double db = runtimeCellDistance(anchor, b.first);
                if (da != db) return da < db;
                if (a.first.y != b.first.y) return a.first.y < b.first.y;
                if (a.first.x != b.first.x) return a.first.x < b.first.x;
                if (a.second.y != b.second.y) return a.second.y < b.second.y;
                return a.second.x < b.second.x;
            });
            std::optional<RuntimeCellPoint> exitCell;
            for (const auto& [outside, inside] : exits) {
                const int key = runtimeCellKey(outside.x, outside.y);
                auto slots = remainingExitSubcells.find(key);
                if (slots == remainingExitSubcells.end()) {
                    if (!findRuntimePassableCellNear(state, occupant, outside, 0, -1, &reservedCells))
                        continue;
                    // Snapshot the free subcells before the core activates any
                    // occupants here. A 1x1 stock building can contain 10 people.
                    std::vector<int> available{-1};
                    if (runtimeInfantryUsesGroundSubcells(occupant)) {
                        available = state.subcells.availableSubCells(
                            state.world.getObjects(), state.world.getTerrainObjects(),
                            {outside.x, outside.y, key, runtimeCellIsBridge(state, outside.x, outside.y)},
                            {occupant.instanceId},
                            runtimeGateSubcellContext(state, &occupant, RuntimeGatePassageMode::Commit));
                    }
                    slots = remainingExitSubcells.emplace(key, std::move(available)).first;
                }
                if (slots->second.empty()) continue;
                exitCell = outside;
                exitFromCells.emplace(occupant.instanceId, inside);
                exitSubcells.emplace(occupant.instanceId, slots->second.front());
                slots->second.erase(slots->second.begin());
                if (slots->second.empty()) reservedCells.insert(key);
                break;
            }
            if (!exitCell.has_value()) return std::nullopt;
            RuntimeGarrisonExitPlacement placement;
            placement.x = exitCell->x;
            placement.y = exitCell->y;
            placement.cell = runtimeCellKey(exitCell->x, exitCell->y);
            placement.level = terrainLevelAtRuntimeCell(
                state,
                exitCell->x,
                exitCell->y
            );
            placement.onBridge = runtimeCellIsBridge(
                state,
                exitCell->x,
                exitCell->y
            );
            return placement;
        };

    std::vector<RuntimeGarrisonEvacuationAction> actions =
        reason == RuntimeGarrisonEvacuationReason::ConditionRed
            ? state.garrisons.evacuateIfConditionRed(
                state.rules,
                state.world.accessObjects(),
                hostObjectId,
                resolver,
                destroyWhenBlocked
              )
            : state.garrisons.forceEvacuate(
                state.world.accessObjects(),
                hostObjectId,
                reason,
                resolver,
                destroyWhenBlocked
              );
    // Publish every chosen slot together before normal subcell reconciliation.
    for (const auto& [occupantId, subCell] : exitSubcells) {
        if (auto* occupant = findMutableObjectById(state, occupantId))
            occupant->subCell = subCell;
    }
    bool queuedDestroyedOccupantExit = false;
    for (const RuntimeGarrisonEvacuationAction& action : actions) {
        eraseRuntimeBehavior(state, action.occupantId);
        removeSelectedObject(state, action.occupantId);
        if (WorldObjectInstance* occupant =
                findMutableRuntimeObjectByIdIncludingInactive(
                    state,
                    action.occupantId
                )) {
            if (occupant->alive) {
                assignRuntimeInfantrySubcellAtCurrentLocation(state, *occupant);
                const auto fromIt = exitFromCells.find(action.occupantId);
                if (fromIt != exitFromCells.end()) {
                    const auto destination = runtimeCellPointForObject(*occupant);
                    RuntimeObjectBehavior walk;
                    walk.kind = RuntimeBehaviorKind::Move;
                    walk.state = RuntimeBehaviorState::Moving;
                    walk.moveTarget = destination;
                    walk.moveCooldownTicks = consumeRuntimeMovementSegmentIntervalTicks(
                        state, *occupant, walk, fromIt->second, destination);
                    beginRuntimeRenderMove(*occupant, walk, fromIt->second, destination,
                        terrainLevelAtRuntimeCell(state, fromIt->second.x, fromIt->second.y),
                        host->onBridge, walk.moveCooldownTicks);
                    setRuntimeMissionIntent(state, *occupant, RuntimeMissionIntent::Moving,
                        "garrisonExitWalk");
                    state.behaviors[occupant->instanceId] = std::move(walk);
                }
            } else {
                occupant->subCell = -1;
            }
        }
        if (action.disposition
                == RuntimeGarrisonEvacuationDisposition::DestroyedNoExit) {
            RuntimeObjectExitRequest objectExit;
            objectExit.objectId = action.occupantId;
            objectExit.kind = RuntimeObjectExitKind::Destroyed;
            objectExit.tick = state.tick;
            objectExit.mission = "DestroyedWhileEvacuating";
            objectExit.reason = "garrison evacuation had no legal exit";
            objectExit.sourceObjectId = hostObjectId;
            objectExit.sourceOwner = hostOwner;
            const RuntimeObjectExitQueueResult queued =
                queueRuntimeObjectExit(state, std::move(objectExit));
            queuedDestroyedOccupantExit = queuedDestroyedOccupantExit
                || queued == RuntimeObjectExitQueueResult::Queued;
        }
    }
    host = findMutableRuntimeObjectByIdIncludingInactive(state, hostObjectId);
    if (host != nullptr) {
        if (host->garrisonOccupantIds.empty()) {
            if (toLowerAsciiLocal(host->mission) == "occupying") {
                setRuntimeMissionIntent(
                    state,
                    *host,
                    RuntimeMissionIntent::Idle,
                    "garrisonEvacuated"
                );
            }
        } else {
            setRuntimeMissionIntent(
                state,
                *host,
                RuntimeMissionIntent::Occupying,
                "garrisonEvacuationPartial"
            );
        }
    }
    if (!actions.empty()) {
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        state.navigationRefreshPending = true;
        invalidateRuntimeObjectProjectionCache(state);
    }
    if (queuedDestroyedOccupantExit && !state.drainingObjectExits) {
        (void)drainRuntimeObjectExitQueue(state);
    }
    return actions;
}

std::string runtimePlayerFactoryCategory(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    if (!object.alive || object.kind != WorldObjectKind::Structure) return {};
    const auto* building = findRuntimeBuildingTypeById(state.rules, object.type);
    if (!building || building->factory.empty()) return {};
    return toUpperAsciiLocal(building->factory)
        + ((building->naval || building->waterBound) ? ":NAVAL" : ":LAND");
}

void prioritizeRuntimePlayerFactory(const RuntimeGameSessionState& state,
    const std::string& owner, std::vector<const WorldObjectInstance*>& candidates) {
    const auto ownerKey = runtimeCanonicalHumanHouseOwner(owner);
    std::stable_partition(candidates.begin(), candidates.end(), [&](const auto* object) {
        if (!object) return false;
        const auto found = state.playerCommands.primaryFactories.find({
            ownerKey, runtimePlayerFactoryCategory(state, *object)});
        return found != state.playerCommands.primaryFactories.end()
            && found->second == object->instanceId;
    });
}

void queueRuntimePlayerFactoryRally(RuntimeGameSessionState& state, int factoryId, int producedId) {
    const auto* object = findObjectById(state, producedId);
    const auto rally = state.playerCommands.rallyPoints.find(factoryId);
    if (!object || rally == state.playerCommands.rallyPoints.end()
        || runtimeCanonicalHumanHouseOwner(object->owner) != rally->second.owner) return;
    RuntimeMultiplayerGameplayTargetOrderCommand order;
    order.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
    order.objectIds = {producedId};
    order.target = RuntimeMultiplayerGameplayCellTarget{rally->second.cell};
    // The factory's exit lease still owns the first move. Execute the player's
    // rally only after clearing it, so a far rally cannot hold the factory door.
    const bool freshQueue = !state.playerCommands.queues.contains(producedId);
    if (state.playerCommands.append(rally->second.owner, order) && freshQueue) {
        state.playerCommands.queues.at(producedId).factoryRallyOrderIndex = 0;
    }
}

bool runtimePlayerActorTemporarilyCommandLocked(
    const RuntimeGameSessionState& state, const WorldObjectInstance& actor) {
    if (runtimeObjectDisabledByParasite(state, actor.instanceId)
        || runtimeMissionIsLandingToDeploy(actor)
        || runtimeMissionIsTakingOffAfterUndeploy(actor)
        || runtimeMissionIsDeploying(actor) || runtimeMissionIsUndeploying(actor)
        || runtimeStructureIsRepacking(actor)
        || actor.mission == "ParadropDrop"
        || runtimeGateObjectIsLimbo(actor)
        || runtimeObjectManagedByAirstrike(state, actor.instanceId)
        || !runtimePoweredUnitIsOperational(state, actor)) return true;
    return state.specialMechanismsInitialized
        && (state.specialMechanisms.isChronoMovementLocked(actor.instanceId, state.tick)
            || state.specialMechanisms.isTemporalFrozen(actor.instanceId)
            || state.specialMechanisms.isCarrierChildDocked(actor.instanceId));
}

bool beginRuntimePlayerTravelOrder(RuntimeProductionHostState& state, const std::string& owner,
    const RuntimeMultiplayerGameplayTargetOrderCommand& command) {
    const auto* target = std::get_if<RuntimeMultiplayerGameplayCellTarget>(&command.target);
    if (!target) return false;
    std::vector<std::int32_t> actors;
    const std::string canonicalOwner = runtimeCanonicalHumanHouseOwner(owner);
    for (int id : command.objectIds) {
        const auto* actor = findObjectById(state, id);
        if (actor && actor->alive && !runtimeObjectIsEnslavedSlaveWorker(state, *actor)
            && runtimeObjectCanMove(state, *actor)
            && runtimeCanonicalHumanHouseOwner(actor->owner) == canonicalOwner
            && !runtimePlayerActorTemporarilyCommandLocked(state, *actor)) actors.push_back(id);
    }
    if (actors.empty()) return false;
    std::map<int, RuntimeCellPoint> acceptedActors;
    const bool accepted = applyRuntimePlayerSelectionCommand(state, owner, actors, [&]() {
        issueRuntimeMoveCommand(state, {target->cell.x, target->cell.y}, &acceptedActors);
        return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
    });
    if (!accepted) return false;
    for (const auto& [id, destination] : acceptedActors) {
        const auto* actor = findObjectById(state, id);
        if (!actor) continue;
        const auto* unit = findRuntimeUnitType(state.rules, *actor);
        // Retail engineers, spies and pad aircraft explicitly disable attack
        // move. They still travel; their contextual weapons must not auto-fire.
        if (unit && (unit->airportBound || (unit->preventAttackMove
                && command.order == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove))) continue;
        state.playerCommands.travelling[id] = {
            canonicalOwner,
            {destination.x, destination.y},
            command.order, false, state.tick};
    }
    state.interactionStatus = command.order == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove
        ? "attack-move command accepted" : "guard destination command accepted";
    return true;
}

bool runtimePlayerGuardDestinationAllowsTarget(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target) {
    const auto found = state.playerCommands.travelling.find(actor.instanceId);
    if (found == state.playerCommands.travelling.end()
        || found->second.kind != RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination) return true;
    if (!found->second.guarding) return false;
    const auto profile = runtimeWeaponProfileForObject(state, actor, &target);
    if (!profile.weapon) return false;
    const double radius = std::max(runtimeCachedCapabilityProfileForObject(state, actor).guardScanRange,
        runtimeEffectiveWeaponRange(state, actor, target, *profile.weapon));
    return runtimeCellDistanceToObjectFootprint(state,
        {found->second.destination.x, found->second.destination.y}, target) <= radius + 0.001;
}

bool runtimePlayerTravellingCanAcquire(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor) {
    const auto found = state.playerCommands.travelling.find(actor.instanceId);
    if (found == state.playerCommands.travelling.end()
        || runtimeObjectIsEnslavedSlaveWorker(state, actor)
        || runtimeCanonicalHumanHouseOwner(actor.owner) != found->second.owner
        || runtimePlayerActorTemporarilyCommandLocked(state, actor)
        || runtimeFactoryDeliveryOwnsEgress(state, actor.instanceId)) return false;
    if (found->second.kind == RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination
        && !found->second.guarding) return false;
    const auto behavior = state.behaviors.find(actor.instanceId);
    return behavior == state.behaviors.end()
        || behavior->second.kind == RuntimeBehaviorKind::Move
        || behavior->second.state == RuntimeBehaviorState::Complete
        || behavior->second.state == RuntimeBehaviorState::Idle;
}

bool advanceRuntimePlayerTravelOrders(RuntimeProductionHostState& state) {
    bool changed = false;
    std::vector<int> ids;
    for (const auto& [id, travel] : state.playerCommands.travelling) ids.push_back(id);
    for (int id : ids) {
        const auto found = state.playerCommands.travelling.find(id);
        if (found == state.playerCommands.travelling.end()) continue;
        const RuntimePlayerTravelOrder travel = found->second;
        auto* actor = findMutableObjectById(state, id);
        if (!actor || !actor->alive || runtimeObjectIsEnslavedSlaveWorker(state, *actor)
            || runtimeCanonicalHumanHouseOwner(actor->owner) != travel.owner) {
            state.playerCommands.travelling.erase(id); changed = true; continue;
        }
        if (runtimePlayerActorTemporarilyCommandLocked(state, *actor)
            || runtimeFactoryDeliveryOwnsEgress(state, id)
            || state.combatDetails.hasPendingBurst(id)) continue;
        auto behavior = state.behaviors.find(id);
        if (behavior != state.behaviors.end()
            && behavior->second.kind == RuntimeBehaviorKind::Attack
            && behavior->second.state != RuntimeBehaviorState::Complete) {
            const auto* target = findObjectById(state, behavior->second.targetObjectId);
            if (target && target->alive && runtimeObjectVisibleToOwner(state, actor->owner, *target)
                && runtimeOwnersAreHostileForAutoTarget(state, actor->owner, target->owner)
                && runtimePlayerGuardDestinationAllowsTarget(state, *actor, *target)) continue;
            // The enemy died, left vision, or escaped the guarded area. Stop
            // the temporary combat action and resume the retained destination.
            cancelRuntimeCombatFire(state, id);
            eraseRuntimeBehavior(state, id);
            behavior = state.behaviors.end();
            changed = true;
        }
        if (behavior != state.behaviors.end()
            && behavior->second.state != RuntimeBehaviorState::Complete
            && behavior->second.state != RuntimeBehaviorState::Idle
            && behavior->second.kind != RuntimeBehaviorKind::None) continue;
        const bool arrived = runtimeCellPointForObject(*actor)
            == RuntimeCellPoint{travel.destination.x, travel.destination.y};
        if (arrived) {
            if (travel.kind == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove) {
                state.playerCommands.travelling.erase(id);
            } else if (travel.guarding) {
                continue;
            } else {
                state.playerCommands.travelling.at(id).guarding = true;
            }
            cancelRuntimeCombatFire(state, id);
            eraseRuntimeBehavior(state, id);
            actor->mission = runtimeMissionName(RuntimeMissionIntent::Idle);
            changed = true;
            continue;
        }
        if (state.tick < travel.nextMoveAttemptTick || !runtimeObjectCanMove(state, *actor)) continue;
        // Resuming a standing order is not another player click or voice reply.
        struct QuietResume {
            RuntimeProductionHostState& state;
            bool previous;
            explicit QuietResume(RuntimeProductionHostState& value)
                : state(value), previous(value.runtimeSuppressPlayerAudioFeedback) {
                state.runtimeSuppressPlayerAudioFeedback = true;
            }
            ~QuietResume() { state.runtimeSuppressPlayerAudioFeedback = previous; }
        } quiet(state);
        std::map<int, RuntimeCellPoint> acceptedDestinations;
        const bool accepted = applyRuntimePlayerSelectionCommand(state, travel.owner, {id}, [&]() {
            issueRuntimeMoveCommand(state, {travel.destination.x, travel.destination.y}, &acceptedDestinations);
            return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
        });
        const auto resumed = state.playerCommands.travelling.find(id);
        if (resumed != state.playerCommands.travelling.end()) {
            resumed->second.guarding = false;
            resumed->second.nextMoveAttemptTick = state.tick + std::max(1, state.config.fixedTickHz);
            const auto destination = acceptedDestinations.find(id);
            if (accepted && destination != acceptedDestinations.end())
                resumed->second.destination = {destination->second.x, destination->second.y};
        }
        changed = accepted || changed;
    }
    return changed;
}

bool executeRuntimePlayerTargetOrder(RuntimeProductionHostState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayTargetOrderCommand& body,
    bool useCurrentUiSelection,
    bool suppressCommandVoiceFeedback) {
    std::string actorError;
    if (!validateRuntimeGameplayOwnedObjects(state, owner, body.objectIds, actorError)) return false;
    const auto* cell = std::get_if<RuntimeMultiplayerGameplayCellTarget>(&body.target);
    const auto* targetId = std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&body.target);
    const auto* target = targetId ? findObjectById(state, targetId->objectId) : nullptr;
    if (targetId && (!target || !runtimeGameplayObjectVisibleToOwner(state, owner, *target))) return false;
    for (int id : body.objectIds) state.playerCommands.travelling.erase(id);
    if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove
        || body.order == RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination)
        return beginRuntimePlayerTravelOrder(state, owner, body);
    if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::Follow) {
        if (!target || !runtimeOwnersAreAllied(state, owner, target->owner)) return false;
        const int followedId = target->instanceId;
        bool accepted = false;
        for (int id : body.objectIds) {
            const auto* object = findObjectById(state, id);
            if (!object || id == followedId || !runtimeObjectCanMove(state, *object)
                || runtimeCanonicalHumanHouseOwner(object->owner) != runtimeCanonicalHumanHouseOwner(owner)) continue;
            // Stop through the existing command executor so old movement,
            // pending weapon bursts and contextual entry intents end together.
            // Call the executor directly: the UI Stop wrapper would erase the
            // subsequent route while a queued Follow is being activated.
            const bool stopped = applyRuntimePlayerSelectionCommand(state, owner, {id}, [&]() {
                issueRuntimeStopCommand(state);
                return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
            });
            if (!stopped) continue;
            state.playerCommands.following[id] = {
                runtimeCanonicalHumanHouseOwner(owner), followedId, {-1, -1}};
            accepted = true;
        }
        return accepted;
    }
    const auto apply = [&]() {
        if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::Move && cell)
            issueRuntimeMoveCommand(state, {cell->cell.x, cell->cell.y}, nullptr, suppressCommandVoiceFeedback);
        else if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::Attack && target)
            issueRuntimeAttackCommand(state, *target, true);
        else if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell && cell)
            return issueRuntimeCellForceFireCommand(state, {cell->cell.x, cell->cell.y});
        else if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::Primary)
            issueRuntimeCommand(state, cell ? RuntimeCellPoint{cell->cell.x, cell->cell.y}
                : runtimeCellPointForObject(*target), target);
        else return false;
        return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
    };
    // Immediate local UI commands may replace a selected construction yard
    // with its new MCV. Preserve the executor's selection change in that path;
    // queued/authoritative commands still use a temporary actor selection.
    if (useCurrentUiSelection && !state.multiplayerBridge) return apply();
    return applyRuntimePlayerSelectionCommand(state, owner, body.objectIds, apply);
}

bool advanceRuntimePlayerWaypointOrders(RuntimeProductionHostState& state) {
    std::vector<int> actors;
    for (const auto& [id, route] : state.playerCommands.waypoints.routes) actors.push_back(id);
    bool changed = false;
    for (int id : actors) {
        auto found = state.playerCommands.waypoints.routes.find(id);
        if (found == state.playerCommands.waypoints.routes.end()) continue;
        const auto* actor = findObjectById(state, id);
        if (!actor || !actor->alive || runtimeObjectIsEnslavedSlaveWorker(state, *actor)
            || runtimeCanonicalHumanHouseOwner(actor->owner) != found->second.owner) {
            state.playerCommands.waypoints.routes.erase(id); changed = true; continue;
        }
        if (state.playerCommands.waypoints.planningOwners.contains(found->second.owner)
            || runtimePlayerActorTemporarilyCommandLocked(state, *actor)
            || runtimeFactoryDeliveryOwnsEgress(state, id)
            || state.combatDetails.hasPendingBurst(id)
            || state.pendingEngineerCaptures.contains(id) || state.pendingEngineerActions.contains(id)
            || state.pendingOccupyTargets.contains(id) || state.pendingTransportTargets.contains(id)
            || state.pendingTimedBombIntents.contains(id)) continue;
        const auto travel = state.playerCommands.travelling.find(id);
        if ((travel != state.playerCommands.travelling.end() && !travel->second.guarding)
            || state.playerCommands.following.contains(id)) continue;
        const auto behavior = state.behaviors.find(id);
        const bool idle = behavior == state.behaviors.end()
            || behavior->second.kind == RuntimeBehaviorKind::None
            || behavior->second.state == RuntimeBehaviorState::Complete
            || behavior->second.state == RuntimeBehaviorState::Idle
            || (behavior->second.autoAcquired && behavior->second.kind == RuntimeBehaviorKind::Attack);
        if (!idle) continue;
        auto& route = found->second;
        if (route.active) { route.finishPoint(); changed = true; }
        if (route.points.empty()) { state.playerCommands.waypoints.routes.erase(id); continue; }
        if (state.tick < route.nextAttemptTick) continue;
        auto next = route.points[route.cursor].order;
        if (const auto* targetId = std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&next.target)) {
            const auto* target = findObjectById(state, targetId->objectId);
            if (!target || !runtimeGameplayObjectVisibleToOwner(state, route.owner, *target)) {
                route.finishPoint();
                if (route.points.empty()) state.playerCommands.waypoints.routes.erase(id);
                changed = true; continue;
            }
        }
        // Patrol legs use the attack-move executor, which resumes the leg after combat.
        if (route.loopStart && std::holds_alternative<RuntimeMultiplayerGameplayCellTarget>(next.target)
            && (next.order == RuntimeMultiplayerGameplayTargetOrderKind::Move
                || next.order == RuntimeMultiplayerGameplayTargetOrderKind::Primary))
            next.order = RuntimeMultiplayerGameplayTargetOrderKind::AttackMove;
        const auto pointId = route.points[route.cursor].id;
        const auto owner = route.owner;
        struct QuietWaypoint {
            RuntimeProductionHostState& state; bool previous;
            explicit QuietWaypoint(RuntimeProductionHostState& value)
                : state(value), previous(value.runtimeSuppressPlayerAudioFeedback) {
                state.runtimeSuppressPlayerAudioFeedback = true;
            }
            ~QuietWaypoint() { state.runtimeSuppressPlayerAudioFeedback = previous; }
        } quiet(state);
        const bool accepted = executeRuntimePlayerTargetOrder(state, owner, next);
        // Contextual orders may replace command state or remove the acting unit.
        found = state.playerCommands.waypoints.routes.find(id);
        if (found != state.playerCommands.waypoints.routes.end()
            && found->second.owner == owner && found->second.points[found->second.cursor].id == pointId) {
            found->second.active = accepted;
            found->second.nextAttemptTick = accepted ? 0 : state.tick + std::max(1, state.config.fixedTickHz);
        }
        changed = accepted || changed;
    }
    return changed;
}

bool advanceRuntimePlayerCommands(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimePlayerCommands", state.tick);
    bool changed = advanceRuntimePlayerTravelOrders(state);
    const auto actorValid = [&](int id, const std::string& owner) {
        const auto* object = findObjectById(state, id);
        return object && object->alive
            && !runtimeObjectIsEnslavedSlaveWorker(state, *object)
            && runtimeCanonicalHumanHouseOwner(object->owner) == owner;
    };
    const auto snapshotActorIds = [](const auto& commands) {
        std::vector<int> ids;
        ids.reserve(commands.size());
        for (const auto& [id, command] : commands) {
            (void)command; ids.push_back(id);
        }
        return ids;
    };
    // Contextual executors may atomically replace playerCommands (for example,
    // a grinding transaction). Never carry its iterators or references across
    // a command call; the actor snapshot preserves stable deterministic order.
    for (const int actorId : snapshotActorIds(state.playerCommands.following)) {
        const auto found = state.playerCommands.following.find(actorId);
        if (found == state.playerCommands.following.end()) continue;
        const auto follow = found->second;
        const auto* actor = findObjectById(state, actorId);
        const auto* target = findObjectById(state, follow.targetId);
        if (!actorValid(actorId, follow.owner) || !target
            || !runtimeOwnersAreAllied(state, follow.owner, target->owner)) {
            state.playerCommands.following.erase(actorId); changed = true; continue;
        }
        if (runtimePlayerActorTemporarilyCommandLocked(state, *actor)
            || runtimeGateObjectIsLimbo(*target)) continue;
        const RuntimeMultiplayerGameplayCell destination{target->x, target->y};
        const int distance = std::max(std::abs(actor->x - target->x), std::abs(actor->y - target->y));
        const auto behavior = state.behaviors.find(actorId);
        const bool idle = behavior == state.behaviors.end()
            || behavior->second.kind == RuntimeBehaviorKind::None
            || behavior->second.state == RuntimeBehaviorState::Complete
            || behavior->second.state == RuntimeBehaviorState::Idle;
        if (distance > 2 && (destination != follow.lastTargetCell || idle)) {
            const RuntimeMultiplayerGameplayTargetOrderCommand order{
                RuntimeMultiplayerGameplayTargetOrderKind::Move, {actorId},
                RuntimeMultiplayerGameplayCellTarget{destination}};
            if (executeRuntimePlayerTargetOrder(state, follow.owner, order)) {
                const auto resumed = state.playerCommands.following.find(actorId);
                if (resumed != state.playerCommands.following.end()
                    && resumed->second.owner == follow.owner
                    && resumed->second.targetId == follow.targetId)
                    resumed->second.lastTargetCell = destination;
                changed = true;
            }
        }
    }
    for (const int id : snapshotActorIds(state.playerCommands.queues)) {
        const auto it = state.playerCommands.queues.find(id);
        if (it == state.playerCommands.queues.end()) continue;
        if (!actorValid(id, it->second.owner)) {
            state.playerCommands.queues.erase(id); changed = true; continue;
        }
        if (state.playerCommands.waypoints.routes.contains(id)) continue;
        const auto travel = state.playerCommands.travelling.find(id);
        if ((travel != state.playerCommands.travelling.end() && !travel->second.guarding)
            || state.playerCommands.following.contains(id) || state.combatDetails.hasPendingBurst(id)
            || runtimeFactoryDeliveryOwnsEgress(state, id)
            || state.pendingEngineerCaptures.contains(id)
            || state.pendingEngineerActions.contains(id)
            || state.pendingOccupyTargets.contains(id)
            || state.pendingTransportTargets.contains(id)
            || state.pendingTimedBombIntents.contains(id)) {
            continue;
        }
        const auto behavior = state.behaviors.find(id);
        const bool idle = behavior == state.behaviors.end()
            || behavior->second.kind == RuntimeBehaviorKind::None
            || behavior->second.state == RuntimeBehaviorState::Complete
            || behavior->second.state == RuntimeBehaviorState::Idle
            || (behavior->second.autoAcquired && behavior->second.kind == RuntimeBehaviorKind::Attack);
        if (!idle) continue;
        const auto next = it->second.orders.front();
        const std::string owner = it->second.owner;
        bool targetValid = true;
        if (const auto* targetId = std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&next.target)) {
            const auto* target = findObjectById(state, targetId->objectId);
            targetValid = target && runtimeGameplayObjectVisibleToOwner(state, owner, *target)
                && (next.order != RuntimeMultiplayerGameplayTargetOrderKind::Follow
                    || runtimeOwnersAreAllied(state, owner, target->owner));
        }
        // Expired targets may be discarded even during a freeze; a valid
        // route survives temporary action locks without consuming a step.
        const auto* actor = findObjectById(state, id);
        if (targetValid && actor && runtimePlayerActorTemporarilyCommandLocked(state, *actor)) {
            continue;
        }
        const bool factoryRally = it->second.factoryRallyOrderIndex == 0;
        if (it->second.factoryRallyOrderIndex >= 0) --it->second.factoryRallyOrderIndex;
        it->second.orders.pop_front();
        if (it->second.orders.empty()) state.playerCommands.queues.erase(id);
        // Missing/dead targets are skipped; a queued attack cannot track a
        // target after it leaves the issuer's vision.
        if (targetValid) (void)executeRuntimePlayerTargetOrder(state, owner, next, false, factoryRally);
        changed = true;
    }
    changed = advanceRuntimePlayerWaypointOrders(state) || changed;
    for (auto it = state.playerCommands.primaryFactories.begin(); it != state.playerCommands.primaryFactories.end();) {
        const auto* factory = findObjectById(state, it->second);
        if (!factory || !actorValid(it->second, it->first.first)
            || runtimePlayerFactoryCategory(state, *factory) != it->first.second) {
            it = state.playerCommands.primaryFactories.erase(it); changed = true;
        } else ++it;
    }
    for (auto it = state.playerCommands.rallyPoints.begin(); it != state.playerCommands.rallyPoints.end();) {
        if (!actorValid(it->first, it->second.owner)) {
            it = state.playerCommands.rallyPoints.erase(it); changed = true;
        } else ++it;
    }
    return changed;
}

std::optional<std::string> runtimeTeamIndexedAudioId(
    const IniFile& ini,
    const std::string& sectionName,
    int zeroBasedIndex) {
    if (zeroBasedIndex < 0) return std::nullopt;
    const IniFile::Section* section = ini.getSection(sectionName);
    if (section == nullptr) return std::nullopt;

    bool numericKeySeen = false;
    bool zeroKeySeen = false;
    for (const auto& [key, value] : *section) {
        (void)value;
        int numericKey = -1;
        if (parseRuntimeInt(key, numericKey)) {
            numericKeySeen = true;
            zeroKeySeen = zeroKeySeen || numericKey == 0;
        }
    }
    if (numericKeySeen) {
        // RA2/YR script arguments address the original list slots. Empty or
        // commented slots remain significant and must not shift later IDs.
        const int authoredKey = zeroKeySeen
            ? zeroBasedIndex
            : zeroBasedIndex + 1;
        const std::string id = stripRuntimeSoundComment(
            ini.getValue(sectionName, std::to_string(authoredKey), "")
        );
        return id.empty()
            ? std::optional<std::string>{}
            : std::optional<std::string>{id};
    }

    // Non-numeric Mod lists have no authored slots, so retain declaration
    // order as a compatibility fallback.
    const std::vector<std::string> ids = runtimeIndexedIds(ini, sectionName);
    if (static_cast<std::size_t>(zeroBasedIndex) >= ids.size()) {
        return std::nullopt;
    }
    const std::string id = stripRuntimeSoundComment(
        ids[static_cast<std::size_t>(zeroBasedIndex)]
    );
    return id.empty()
        ? std::optional<std::string>{}
        : std::optional<std::string>{id};
}

bool runtimeComputerAiHasRadar(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Structure
            || !runtimeComputerAiOwnerMatches(object, owner)) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr
            && (building->radar
                || building->spySat
                || toUpperAsciiLocal(building->factory) == "RADAR")) {
            return true;
        }
    }
    return false;
}

bool advanceRuntimeIdleVariants(RuntimeProductionHostState& state, bool forceReference) {
    RuntimePerfTraceScope trace("advanceRuntimeIdleVariants", state.tick);
    if (!runtimeRuleIntervalCrossed(state.tick, 4, state.config.fixedTickHz)) {
        return false;
    }

    const bool useReference = forceReference || runtimeFrameScalingReferenceMovement();
    RuntimeIdleVariantPass idlePass;
    std::optional<RuntimeMapRenderContext> sharedWaterContext;
    bool changed = false;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive
            || !runtimeObjectHasOperationalPower(state, object)
            || object.hasRenderPosition
            || state.behaviors.find(object.instanceId) != state.behaviors.end()
            || runtimeObjectHasDeployedState(state, object)
            || runtimeMissionIsDeploying(object)
            || runtimeMissionIsUndeploying(object)) {
            continue;
        }

        const std::string mission = toLowerAsciiLocal(object.mission);
        if (mission != "guard"
            && mission != "idle"
            && mission != "idle1"
            && mission != "idle2") {
            continue;
        }

        // A resting swimmer keeps treading. The automatic land-idle timer
        // must not periodically switch it to a wet idle stunt or dive.
        bool swimming = false;
        if (object.kind == WorldObjectKind::Infantry && !object.airborne && !object.onBridge) {
            if (useReference) {
                auto waterContext = runtimeTerrainMetadataContext(state);
                waterContext.mapFacts = &state.mapFacts;
                swimming = resolveRuntimeInfantrySwimming(object, waterContext, state.renderCache);
            } else {
                if (!sharedWaterContext) {
                    sharedWaterContext.emplace(runtimeTerrainMetadataContext(state));
                    sharedWaterContext->mapFacts = &state.mapFacts;
                }
                swimming = resolveRuntimeInfantrySwimming(object, *sharedWaterContext, state.renderCache);
            }
        }
        const std::string variant = swimming ? std::string{}
            : runtimeIdleVariantMissionForObject(state, object, useReference, &idlePass);
        const std::string nextMission =
            variant.empty()
                ? runtimeMissionName(RuntimeMissionIntent::Idle)
                : variant;
        if (object.mission == nextMission) {
            continue;
        }

        const std::string previousMission = object.mission;
        object.mission = nextMission;
        recordRuntimeMissionTransition(
            state,
            object,
            previousMission,
            nextMission,
            variant.empty() ? "idleVariantComplete" : "idleVariant"
        );
        changed = true;
    }
    return changed;
}

bool runtimeUsesGameplayIntentRouter(const RuntimeProductionHostState& state) {
    return state.multiplayerBridge != nullptr
        || (!state.campaignMode && runtimeLocalObserver(state.config))
        || state.offlineGameplayIntentRouting;
}

void updateRuntimeRadarUiState(RuntimeProductionHostState& state, bool radarAvailable) {
    if (!state.runtimeRadarStateObserved) {
        state.runtimeRadarStateObserved = true;
        state.runtimeLastRadarAvailable = radarAvailable;
        state.runtimeRadarTransitionStartTick =
            radarAvailable ? state.tick : -100000;
        state.runtimeRadarTransitionDirection = radarAvailable ? 1 : 0;
        return;
    }

    if (state.runtimeLastRadarAvailable == radarAvailable) {
        return;
    }

    state.runtimeLastRadarAvailable = radarAvailable;
    state.runtimeRadarTransitionStartTick = state.tick;
    state.runtimeRadarTransitionDirection = radarAvailable ? 1 : -1;
    emitRuntimeSound(
        state,
        radarAvailable
            ? runtimeSoundIntentRadarOn
            : runtimeSoundIntentRadarOff
    );
}

RuntimeVoiceAudio& runtimeVoiceAudioFor(RuntimeProductionHostState& state) {
    if (!state.runtimeVoiceAudio) state.runtimeVoiceAudio = std::make_unique<RuntimeVoiceAudio>();
    return *state.runtimeVoiceAudio;
}

}
