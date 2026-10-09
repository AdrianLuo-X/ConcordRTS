#include "EngineStatus.h"

#include "CampaignDatabase.h"

#include <algorithm>
#include <filesystem>
#include <ostream>
#include <string>

namespace {

bool pathExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code errorCode;
    return std::filesystem::exists(path, errorCode);
}

int countFilesWithExtension(const std::string& rootPath, const std::string& extension) {
    if (rootPath.empty()) {
        return 0;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(rootPath, errorCode)) {
        return 0;
    }

    const std::string wanted = extension;
    int count = 0;
    std::filesystem::recursive_directory_iterator it(
        rootPath,
        std::filesystem::directory_options::skip_permission_denied,
        errorCode
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!errorCode && it != end) {
        if (it->is_regular_file(errorCode)
            && it->path().extension() == wanted) {
            ++count;
        }
        it.increment(errorCode);
    }
    return count;
}

int countTheaterTileFiles(const std::string& rootPath) {
    return countFilesWithExtension(rootPath, ".tmp")
        + countFilesWithExtension(rootPath, ".tem")
        + countFilesWithExtension(rootPath, ".des")
        + countFilesWithExtension(rootPath, ".urb")
        + countFilesWithExtension(rootPath, ".ubn")
        + countFilesWithExtension(rootPath, ".sno")
        + countFilesWithExtension(rootPath, ".lun");
}

std::string countText(const std::string& label, int count) {
    return label + "=" + std::to_string(count);
}

EngineSubsystemStatus makeStatus(
    std::string name,
    EngineSubsystemState state,
    std::vector<std::string> evidence,
    std::vector<std::string> gaps
) {
    EngineSubsystemStatus status;
    status.name = std::move(name);
    status.state = state;
    status.evidence = std::move(evidence);
    status.gaps = std::move(gaps);
    return status;
}

const char* stateText(EngineSubsystemState state) {
    switch (state) {
    case EngineSubsystemState::Ready:
        return "Ready";
    case EngineSubsystemState::Partial:
        return "Partial";
    case EngineSubsystemState::Missing:
        return "Missing";
    }
    return "Missing";
}

void addSubsystem(EngineStatusReport& report, EngineSubsystemStatus status) {
    switch (status.state) {
    case EngineSubsystemState::Ready:
        ++report.readyCount;
        break;
    case EngineSubsystemState::Partial:
        ++report.partialCount;
        break;
    case EngineSubsystemState::Missing:
        ++report.missingCount;
        break;
    }
    report.subsystems.push_back(std::move(status));
    report.totalCount = static_cast<int>(report.subsystems.size());
}

} // namespace

EngineStatusReport buildEngineStatusReport(const EngineStatusInput& input) {
    EngineStatusReport report;
    const RulesDatabase* rules = input.rules;
    const ArtDatabase* art = input.art;
    const MapDatabase* map = input.map;
    const IniFile* soundIni = input.soundIni;
    const IniFile* evaIni = input.evaIni;

    const int mixCount = countFilesWithExtension(input.assetRootPath, ".mix");
    const int shpCount = countFilesWithExtension(input.assetRootPath, ".shp");
    const int vxlCount = countFilesWithExtension(input.assetRootPath, ".vxl");
    const int hvaCount = countFilesWithExtension(input.assetRootPath, ".hva");
    const int tmpCount = countTheaterTileFiles(input.assetRootPath);
    const int palCount = countFilesWithExtension(input.assetRootPath, ".pal");
    const int wavCount = countFilesWithExtension(input.assetRootPath, ".wav");
    const int audCount = countFilesWithExtension(input.assetRootPath, ".aud");

    addSubsystem(
        report,
        makeStatus(
            "Core",
            EngineSubsystemState::Partial,
            {
                "CLI command router and smoke tests exist",
                "Runtime has fixed tick and platform window loop",
                "Performance plan exposes worker/core budget"
            },
            {
                "central logger",
                "memory allocator/arena",
                "real task scheduler",
                "portable platform layer beyond Win32 runtime"
            }
        )
    );

    const bool assetReady =
        mixCount > 0 && palCount > 0 && shpCount > 0 && vxlCount > 0
        && hvaCount > 0 && tmpCount > 0;
    addSubsystem(
        report,
        makeStatus(
            "Asset",
            assetReady ? EngineSubsystemState::Ready : EngineSubsystemState::Partial,
            {
                countText("mix", mixCount),
                countText("pal", palCount),
                countText("shp", shpCount),
                countText("vxl", vxlCount),
                countText("hva", hvaCount),
                countText("tmp/theater tiles", tmpCount),
                countText("wav", wavCount),
                countText("aud", audCount)
            },
            audCount > 0
                ? std::vector<std::string>{"AUD decoding/playback still needs full runtime mixer integration"}
                : std::vector<std::string>{"AUD samples still inside audio.mix/audiomd.mix or not extracted"}
        )
    );

    const int superWeaponRuleCount = rules == nullptr
        ? 0
        : static_cast<int>(rules->getSuperWeaponTypes().size());
    const bool rulesReady =
        rules != nullptr
        && !rules->getInfantryTypes().empty()
        && !rules->getVehicleTypes().empty()
        && !rules->getBuildingTypes().empty()
        && !rules->getWeaponTypes().empty()
        && !rules->getProjectileTypes().empty()
        && !rules->getWarheadTypes().empty()
        && superWeaponRuleCount > 0;
    addSubsystem(
        report,
        makeStatus(
            "Rules",
            rulesReady ? EngineSubsystemState::Ready : EngineSubsystemState::Partial,
            {
                "rules=" + input.rulesPath,
                "art=" + input.artPath,
                "sound=" + input.soundPath,
                "eva=" + input.evaPath,
                "tech tree static report is available",
                rules == nullptr ? "rules not loaded" : countText("infantry", static_cast<int>(rules->getInfantryTypes().size())),
                rules == nullptr ? "" : countText("vehicles", static_cast<int>(rules->getVehicleTypes().size())),
                rules == nullptr ? "" : countText("buildings", static_cast<int>(rules->getBuildingTypes().size())),
                rules == nullptr ? "" : countText("weapons", static_cast<int>(rules->getWeaponTypes().size())),
                rules == nullptr ? "" : countText("projectiles", static_cast<int>(rules->getProjectileTypes().size())),
                rules == nullptr ? "" : countText("warheads", static_cast<int>(rules->getWarheadTypes().size())),
                countText("super weapon definitions", superWeaponRuleCount),
                art == nullptr ? "art not loaded" : countText("art", static_cast<int>(art->getArtTypes().size())),
                soundIni == nullptr ? "sound not loaded" : countText("sound sections", static_cast<int>(soundIni->getSectionNames().size())),
                evaIni == nullptr ? "eva not loaded" : countText("eva sections", static_cast<int>(evaIni->getSectionNames().size()))
            },
            {
                "runtime prerequisite unlock feedback still needs deeper sidebar integration",
                superWeaponRuleCount > 0
                    ? "some super weapon cross-section fields still need mod-specific validation"
                    : "super weapon rules",
                "mission-script-specific EVA event hooks"
            }
        )
    );

    const bool mapReady =
        map != nullptr
        && map->getSummary().isoMapPackLineCount > 0
        && !map->getIsoTiles().empty();
    addSubsystem(
        report,
        makeStatus(
            "Map",
            mapReady ? EngineSubsystemState::Ready : EngineSubsystemState::Partial,
            {
                "map=" + input.mapPath,
                map == nullptr ? "map not loaded" : "theater=" + map->getSummary().theater,
                map == nullptr ? "" : countText("iso tiles", static_cast<int>(map->getIsoTiles().size())),
                map == nullptr ? "" : countText("overlay", static_cast<int>(map->getOverlayCells().size())),
                map == nullptr ? "" : countText("structures", static_cast<int>(map->getStructures().size())),
                map == nullptr ? "" : countText("units", static_cast<int>(map->getUnits().size())),
                map == nullptr ? "" : countText("infantry", static_cast<int>(map->getInfantry().size())),
                map == nullptr ? "" : countText("triggers", static_cast<int>(map->getSummary().triggerCount))
            },
            {
                "bridge damage semantics still need more original-rule parity",
                "rare mod-only ScriptType side effects need broader scenario coverage"
            }
        )
    );

    const CampaignDatabase* campaign = input.campaign;
    int campaignIntroMissionCount = 0;
    int campaignPostScoreMissionCount = 0;
    int campaignMissionTriggerCount = 0;
    int campaignUnsupportedEventTypeCount = 0;
    int campaignUnsupportedActionTypeCount = 0;
    if (campaign != nullptr) {
        for (const CampaignDefinition& definition : campaign->campaigns) {
            for (const CampaignMissionDefinition& mission : definition.missions) {
                campaignIntroMissionCount +=
                    !mission.introVideoPath.empty() ? 1 : 0;
                campaignPostScoreMissionCount +=
                    !mission.postScoreVideoPath.empty() ? 1 : 0;
                campaignMissionTriggerCount += mission.triggerCount;
                campaignUnsupportedEventTypeCount += static_cast<int>(
                    mission.unsupportedEventTypes.size()
                );
                campaignUnsupportedActionTypeCount += static_cast<int>(
                    mission.unsupportedActionTypes.size()
                );
            }
        }
    }
    const bool campaignLoaded =
        campaign != nullptr
        && campaign->loaded
        && campaign->missionCount() > 0
        && campaign->availableMissionCount() > 0;
    addSubsystem(
        report,
        makeStatus(
            "Campaign",
            campaignLoaded
                ? EngineSubsystemState::Partial
                : EngineSubsystemState::Missing,
            {
                campaign == nullptr ? "campaign database not supplied" : campaign->status,
                campaign == nullptr ? "" : countText("campaigns", static_cast<int>(campaign->campaigns.size())),
                campaign == nullptr ? "" : countText("missions", static_cast<int>(campaign->missionCount())),
                campaign == nullptr ? "" : countText("available missions", static_cast<int>(campaign->availableMissionCount())),
                countText("missions with authored intro video", campaignIntroMissionCount),
                countText("missions with authored post-score video", campaignPostScoreMissionCount),
                countText("authored trigger rows", campaignMissionTriggerCount),
                countText("unsupported event type occurrences", campaignUnsupportedEventTypeCount),
                countText("unsupported action type occurrences", campaignUnsupportedActionTypeCount),
                campaign == nullptr ? "" : "campaign selector/mission metadata are discovered from local RA2/YR files",
                campaign == nullptr ? "" : "persistent TeamType ScriptType execution and save/resume are connected",
                campaign == nullptr ? "" : "campaign save/progress and CampaignMission runtime smoke tests exist"
            },
            {
                "some authored event types remain diagnostic-only until their exact retail semantics are implemented",
                "mission-triggered in-game movie sequencing and rare mod-only ScriptType actions still need expansion",
                "campaign shell art, EVA/voice queue, and all authored intro videos depend on local resources"
            }
        )
    );

    addSubsystem(
        report,
        makeStatus(
            "Simulation",
            EngineSubsystemState::Partial,
            {
                "fixed tick runtime",
                "units/infantry/vehicles/buildings represented",
                "weapons/projectiles/warheads/damage connected",
                "production, power, harvesting, AI trigger smoke tests exist",
                "super weapon state machine, target validation, AI fire and trigger actions are connected",
                "victory condition sandbox is available"
            },
            {
                "aircraft behavior",
                "retail-specific super weapon AI scoring and plane/aircraft spawn parity",
                "runtime UI victory/defeat screen integration",
                "complete harvester refinery loop"
            }
        )
    );

    int poweredSuperWeapons = 0;
    int targetableSuperWeapons = 0;
    int superWeaponsWithSidebarArt = 0;
    int superWeaponsWithAudio = 0;
    if (rules != nullptr) {
        for (const SuperWeaponRule& superWeapon : rules->getSuperWeaponTypes()) {
            poweredSuperWeapons += superWeapon.isPowered ? 1 : 0;
            targetableSuperWeapons += superWeapon.targetMode.empty()
                || superWeapon.targetMode != "None" ? 1 : 0;
            superWeaponsWithSidebarArt +=
                !superWeapon.sidebarImage.empty() ? 1 : 0;
            superWeaponsWithAudio +=
                !superWeapon.specialSound.empty()
                    || !superWeapon.startSound.empty()
                    || !superWeapon.rechargeVoice.empty()
                    || !superWeapon.chargingVoice.empty()
                    || !superWeapon.impatientVoice.empty()
                    || !superWeapon.suspendVoice.empty()
                    ? 1
                    : 0;
        }
    }
    addSubsystem(
        report,
        makeStatus(
            "SuperWeapons",
            superWeaponRuleCount > 0
                ? EngineSubsystemState::Partial
                : EngineSubsystemState::Missing,
            {
                countText("definitions", superWeaponRuleCount),
                countText("powered", poweredSuperWeapons),
                countText("targetable", targetableSuperWeapons),
                countText("sidebar art references", superWeaponsWithSidebarArt),
                countText("audio/voice references", superWeaponsWithAudio),
                "fixed-tick charge/suspend/ready/launch/active/cooldown states",
                "save/load sections, replay event fields and deterministic state hash",
                "nuke, lightning, iron curtain, force shield, chrono, dominator, mutator, paradrop and reveal paths"
            },
            {
                "full retail aircraft ingress/egress and aircraft combat behavior",
                "some local resources/cursors/audio samples may be absent or unresolved",
                "mod-specific target filters, AI scoring and trigger coordinates need broader scenario coverage"
            }
        )
    );

    int mindControlWarheads = 0;
    int temporalWarheads = 0;
    int radiationWarheads = 0;
    int electricWarheads = 0;
    int electricWeapons = 0;
    int camouflageUnits = 0;
    int permanentDisguiseUnits = 0;
    int teleporterUnits = 0;
    int carrierUnits = 0;
    int overpowerableBuildings = 0;
    if (rules != nullptr) {
        for (const WarheadType& warhead : rules->getWarheadTypes()) {
            mindControlWarheads += warhead.mindControl ? 1 : 0;
            temporalWarheads += warhead.temporal ? 1 : 0;
            radiationWarheads += warhead.radiation ? 1 : 0;
            electricWarheads += warhead.electricAssault ? 1 : 0;
        }
        for (const WeaponType& weapon : rules->getWeaponTypes()) {
            electricWeapons += weapon.electricBolt ? 1 : 0;
        }
        for (const BuildingType& building : rules->getBuildingTypes()) {
            overpowerableBuildings += building.overpowerable ? 1 : 0;
        }
        const auto countUnits = [&](const std::vector<UnitType>& units) {
            for (const UnitType& unit : units) {
                camouflageUnits +=
                    unit.canDisguise
                        && (unit.disguiseWhenStill || unit.permaDisguise)
                        ? 1
                        : 0;
                permanentDisguiseUnits += unit.permaDisguise ? 1 : 0;
                teleporterUnits += unit.teleporter ? 1 : 0;
                carrierUnits +=
                    !unit.spawns.empty() && unit.spawnsNumber > 0 ? 1 : 0;
            }
        };
        countUnits(rules->getInfantryTypes());
        countUnits(rules->getVehicleTypes());
        countUnits(rules->getAircraftTypes());
    }
    const bool specialRulesPresent =
        rules != nullptr
        && (mindControlWarheads + temporalWarheads + radiationWarheads
            + electricWarheads + electricWeapons + camouflageUnits
            + teleporterUnits + carrierUnits + overpowerableBuildings) > 0;
    addSubsystem(
        report,
        makeStatus(
            "SpecialMechanisms",
            specialRulesPresent
                ? EngineSubsystemState::Partial
                : EngineSubsystemState::Missing,
            {
                countText("mind-control warheads", mindControlWarheads),
                countText("temporal warheads", temporalWarheads),
                countText("radiation warheads", radiationWarheads),
                countText("electric warheads", electricWarheads),
                countText("electric weapons", electricWeapons),
                countText("still-camouflage units", camouflageUnits),
                countText("permanent-disguise units", permanentDisguiseUnits),
                countText("teleporter units", teleporterUnits),
                countText("parent/spawn units", carrierUnits),
                countText("overpowerable buildings", overpowerableBuildings),
                "DefaultMirageDisguises resolves through TREE01-TREE04 theater assets",
                "fixed-tick state, save sections, replay events and state hash"
            },
            {
                "original Mirage disguise selection for Spy/MakeupKit still needs a dedicated target-disguise path",
                "full original particle/light mixer, EVA queue, and animation timing coverage",
                "rare mod-only parent-child and one-shot missile lifecycle variants need broader scenario coverage",
                "runtime status is not yet promoted to Ready until end-to-end smoke coverage is complete"
            }
        )
    );

    addSubsystem(
        report,
        makeStatus(
            "Pathfinding",
            EngineSubsystemState::Partial,
            {
                "tile passability and A* are used by runtime commands",
                "formation targets and bridge/water/cliff classes exist",
                "path request queue and cache sandbox are available"
            },
            {
                "hierarchical pathfinding",
                "multi-threaded path worker",
                "high-density local avoidance"
            }
        )
    );

    addSubsystem(
        report,
        makeStatus(
            "Renderer",
            EngineSubsystemState::Partial,
            {
                "isometric TMP map renderer",
                "SHP object sprites",
                "VXL proxy/sprite path",
                "depth sorting, shadows, sidebar, minimap"
            },
            {
                "real particles",
                "full palette lighting/effects",
                "GPU backend",
                "complete original sidebar skin"
            }
        )
    );

    const bool audioReady = soundIni != nullptr && wavCount > 0;
    addSubsystem(
        report,
        makeStatus(
            "Audio",
            audioReady ? EngineSubsystemState::Partial : EngineSubsystemState::Missing,
            {
                soundIni == nullptr ? "soundmd.ini missing" : "soundmd.ini loaded",
                evaIni == nullptr ? "eva ini missing" : "eva ini loaded",
                countText("wav", wavCount),
                countText("aud", audCount),
                "audio catalog report is available",
                "AUD/IDX/BAG decode and runtime WAV cache",
                "EVA voices resolve from audiomd MIX by faction",
                "EVA queue, priority, and duplicate coalescing",
                "music playback has WAV path"
            },
            {
                "multi-channel runtime mixer",
                "mission-script-specific EVA event hooks"
            }
        )
    );

    addSubsystem(
        report,
        makeStatus(
            "Multiplayer",
            EngineSubsystemState::Partial,
            {
                "replay sandbox",
                "lockstep deterministic command stream",
                "desync detection test",
                "LAN UDP room discovery and framed TCP transport core",
                "bounded nonblocking peer queues and receive-event budgets",
                "host-authoritative multiplayer room/session state core",
                "versioned multiplayer wire messages with peer-source binding",
                "authoritative tick windows and per-peer command admission",
                "compatibility, state-hash, and abortable save/load consensus"
            },
            {
                "production LAN lobby UI and two-process lifecycle",
                "runtime command queue and remote input application",
                "multi-human ownership, economy, and production authority",
                "complete authoritative world hash",
                "snapshot transfer and staged runtime save/load commit",
                "Internet matchmaking, relay, and NAT traversal"
            }
        )
    );

    addSubsystem(
        report,
        makeStatus(
            "Tools",
            EngineSubsystemState::Ready,
            {
                "MIX list/extract",
                "SHP frame export",
                "VXL/HVA inspect",
                "TMP tile export",
                "map preview/html",
                "INI validator",
                "MOD compatibility report",
                "performance plan",
                "engine status, tech tree, victory, pathfinding, audio reports"
            },
            {
                "interactive asset browser UI",
                "profiling timeline output"
            }
        )
    );

    for (EngineSubsystemStatus& subsystem : report.subsystems) {
        subsystem.evidence.erase(
            std::remove(subsystem.evidence.begin(), subsystem.evidence.end(), ""),
            subsystem.evidence.end()
        );
    }

    return report;
}

void printEngineStatusReport(std::ostream& output, const EngineStatusReport& report) {
    output << "\n========== Engine Status ==========\n";
    output << "Ready:   " << report.readyCount << "\n";
    output << "Partial: " << report.partialCount << "\n";
    output << "Missing: " << report.missingCount << "\n";
    output << "Total:   " << report.totalCount << "\n";

    for (const EngineSubsystemStatus& subsystem : report.subsystems) {
        output << "\n[" << subsystem.name << "] " << stateText(subsystem.state) << "\n";
        output << "Evidence:\n";
        for (const std::string& evidence : subsystem.evidence) {
            output << "- " << evidence << "\n";
        }
        output << "Gaps:\n";
        for (const std::string& gap : subsystem.gaps) {
            output << "- " << gap << "\n";
        }
    }
}
