#include "data/campaign/CooperativeScenarioDatabase.h"
#include "core/io/IniFile.h"
#include "data/maps/MapDatabase.h"
#include "network/cooperative/RuntimeCooperativeSessionSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "runtime cooperative session smoke failure: "
            << message << '\n';
    }
    return condition;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

bool replaceOnce(
    std::string& text,
    const std::string& before,
    const std::string& after
) {
    const std::size_t position = text.find(before);
    if (position == std::string::npos) {
        return false;
    }
    text.replace(position, before.size(), after);
    return true;
}

bool serializeAndParse(
    const RuntimeCooperativeSessionState& state,
    std::string& serialized,
    IniFile& ini
) {
    std::ostringstream output;
    state.save(output);
    serialized = output.str();
    return ini.loadFromString(serialized);
}

bool verifyFixedSessionContract(
    const CooperativeScenarioDefinition& scenario,
    const CooperativeScenarioMapValidation& validation,
    const RuntimeCooperativeSessionState& session,
    const std::string& scope
) {
    bool success = true;
    success &= expect(
        session.active()
            && session.status()
                == RuntimeCooperativeSessionStatus::Running
            && session.scenarioId() == scenario.identity
            && session.mapId() == scenario.mapId
            && session.scenarioFingerprint() == scenario.fingerprint
            && session.mapFingerprint()
                == validation.identityFingerprint,
        scope + " session identity"
    );

    const RuntimeCooperativeBootstrapPlan plan = session.bootstrapPlan();
    success &= expect(
        plan.policy.useAuthoredTriggers
            && plan.policy.requiresCooperativeStartBootstrap
            && !plan.policy.coreCreatesWorldObjects,
        scope + " authored trigger and value-only bootstrap policy"
    );
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const RuntimeCooperativeSessionSlot& slot = session.slots()[index];
        const RuntimeCooperativeStartBootstrapInstruction& start =
            plan.starts[index];
        const std::vector<std::string>& coalitionCountries = index < 2
            ? scenario.playerCountries
            : scenario.enemyCountries;
        success &= expect(
            slot.slotIndex == static_cast<int>(index)
                && slot.label == static_cast<char>('A' + index)
                && slot.authoredOwnerId
                    == 4475 + static_cast<int>(index)
                && slot.authoredHouse
                    == std::string("<Player @ ")
                        + static_cast<char>('A' + index) + ">"
                && slot.role == (index == 0
                    ? CooperativeSlotRole::LocalHuman
                    : index == 1
                        ? CooperativeSlotRole::AiAlly
                        : CooperativeSlotRole::AiEnemy)
                && slot.coalitionId == (index < 2 ? 0 : 1)
                && slot.teamId == slot.coalitionId
                && slot.waypointId == static_cast<int>(index)
                && slot.start.waypointId == static_cast<int>(index)
                && slot.start.cell == validation.waypoints[index].cell
                && slot.allowedCountries == coalitionCountries
                && std::find(
                    coalitionCountries.begin(),
                    coalitionCountries.end(),
                    slot.country
                ) != coalitionCountries.end(),
            scope + " fixed slot and coalition country contract "
                + std::to_string(index)
        );
        success &= expect(
            start.slotIndex == static_cast<int>(index)
                && start.owner == slot.owner
                && start.country == slot.country
                && start.waypointId == static_cast<int>(index)
                && start.start.cell == validation.waypoints[index].cell
                && start.includeMcv
                && start.includeBasicStartingUnits,
            scope + " fixed cooperative start bootstrap "
                + std::to_string(index)
        );
        for (std::size_t other = 0;
             other < runtimeCooperativeSlotCount;
             ++other) {
            success &= expect(
                session.slotsAreAllied(
                    static_cast<int>(index),
                    static_cast<int>(other)
                ) == ((index < 2) == (other < 2)),
                scope + " A-B/C-D alliance matrix"
            );
        }
    }
    return success;
}

bool runFixtureSmoke(
    const std::string& scenarioPath,
    const std::string& mapPath
) {
    IniFile scenarioIni;
    IniFile mapIni;
    MapDatabase map;
    CooperativeScenarioDatabase database;
    bool success = expect(
        scenarioIni.loadFromFile(scenarioPath)
            && database.loadFromIni(scenarioIni)
            && mapIni.loadFromFile(mapPath)
            && map.loadFromIni(mapIni),
        "fixture inputs load"
    );
    const CooperativeScenarioDefinition* scenario =
        database.findByMapId("COOPFIX");
    success &= expect(scenario != nullptr, "fixture scenario lookup");
    if (!success || scenario == nullptr) {
        return false;
    }
    const CooperativeScenarioMapValidation validation =
        database.validateMap(*scenario, map, mapIni);
    success &= expect(validation.valid, "fixture map validation");
    if (!success) {
        return false;
    }

    RuntimeCooperativeSessionBuildOptions options;
    options.owners = {
        "LocalHouse", "AllyHouse", "EnemyHouseC", "EnemyHouseD"
    };
    options.countries = {
        "americans", "British", "russians", "YuriCountry"
    };
    RuntimeCooperativeSessionState session;
    const RuntimeCooperativeSessionBuildResult build = session.begin(
        *scenario,
        validation,
        options
    );
    success &= expect(build.accepted, "fixture session build");
    success &= verifyFixedSessionContract(
        *scenario,
        validation,
        session,
        "fixture"
    );
    success &= expect(
        session.slots()[0].country == "Americans"
            && session.slots()[1].country == "British"
            && session.slots()[2].country == "Russians"
            && session.slots()[3].country == "YuriCountry"
            && session.slots()[0].allowedCountries
                == session.slots()[1].allowedCountries
            && session.slots()[2].allowedCountries
                == session.slots()[3].allowedCountries,
        "country CSV is a shared coalition constraint, not positional slots"
    );

    constexpr std::array<const char*, 4> numericAliases{
        "4475", "4476", "4477", "4478"
    };
    constexpr std::array<const char*, 4> houseAliases{
        "<Player @ A>", "<Player @ B>",
        "<Player @ C>", "<Player @ D>"
    };
    for (std::size_t index = 0; index < runtimeCooperativeSlotCount; ++index) {
        success &= expect(
            session.resolveAuthoredSlotAlias(numericAliases[index])
                    == static_cast<int>(index)
                && session.resolveAuthoredSlotAlias(houseAliases[index])
                    == static_cast<int>(index)
                && runtimeCooperativeScenarioSlotIndexFromAlias(
                    *scenario,
                    numericAliases[index]
                ) == static_cast<int>(index)
                && runtimeCooperativeScenarioSlotIndexFromAlias(
                    *scenario,
                    houseAliases[index]
                ) == static_cast<int>(index),
            "exact authored alias " + std::to_string(index)
        );
    }
    const std::vector<std::string> fuzzyAliases{
        " 4475", "4475 ", "4479", "<Player @ a>",
        "<Player @ A>junk", "Player @ A", "LocalHouse"
    };
    for (const std::string& alias : fuzzyAliases) {
        success &= expect(
            !session.resolveAuthoredSlotAlias(alias).has_value()
                && !runtimeCooperativeScenarioSlotIndexFromAlias(
                    *scenario,
                    alias
                ).has_value(),
            "fuzzy or runtime owner alias rejected: " + alias
        );
    }

    const std::uint64_t hashBeforeOutcome = session.stateHash();
    const RuntimeCooperativeOutcomeAdvanceResult allySurvives =
        session.advanceOutcome({10, {0, 2, 1, 1}});
    success &= expect(
        allySurvives.accepted
            && !allySurvives.changed
            && allySurvives.after
                == RuntimeCooperativeSessionStatus::Running
            && allySurvives.playerCoalitionLiveTechnos == 2
            && allySurvives.enemyCoalitionLiveTechnos == 2
            && session.stateHash() != hashBeforeOutcome,
        "A eliminated while B remains does not defeat player coalition"
    );

    std::string saved;
    IniFile savedIni;
    success &= expect(
        serializeAndParse(session, saved, savedIni),
        "fixture mid-session save parses"
    );
    RuntimeCooperativeSessionState loaded;
    std::string loadError;
    success &= expect(
        loaded.load(savedIni, *scenario, validation, &loadError)
            && loadError.empty()
            && loaded.stateHash() == session.stateHash()
            && loaded.slots()[0].owner == "LocalHouse"
            && loaded.slots()[1].owner == "AllyHouse"
            && loaded.hasOutcomeSnapshot()
            && loaded.lastOutcomeSnapshot().tick == 10,
        "mid-session save/load/hash roundtrip"
    );

    const RuntimeCooperativeOutcomeAdvanceResult playerVictory =
        loaded.advanceOutcome({11, {0, 1, 0, 0}});
    success &= expect(
        playerVictory.accepted
            && playerVictory.changed
            && playerVictory.after
                == RuntimeCooperativeSessionStatus::PlayerCoalitionVictory,
        "A-B coalition victory uses all live technos"
    );

    RuntimeCooperativeSessionState enemyVictory;
    success &= expect(
        enemyVictory.begin(*scenario, validation, options).accepted
            && enemyVictory.advanceOutcome({3, {0, 0, 2, 0}}).after
                == RuntimeCooperativeSessionStatus::EnemyCoalitionVictory,
        "C-D coalition victory"
    );
    RuntimeCooperativeSessionState draw;
    success &= expect(
        draw.begin(*scenario, validation, options).accepted
            && draw.advanceOutcome({3, {0, 0, 0, 0}}).after
                == RuntimeCooperativeSessionStatus::Draw,
        "simultaneous coalition elimination is deterministic draw"
    );

    const std::uint64_t atomicHash = session.stateHash();
    std::string corrupt = saved;
    IniFile corruptIni;
    success &= expect(
        replaceOnce(corrupt, "AllowedCountryCount=2", "AllowedCountryCount=999")
            && corruptIni.loadFromString(corrupt)
            && !session.load(corruptIni, *scenario, validation, &loadError)
            && session.stateHash() == atomicHash,
        "bad save caps reject atomically"
    );

    CooperativeScenarioMapValidation changedMap = validation;
    changedMap.identityFingerprint ^= 0x1234ull;
    changedMap.identity += ":changed";
    success &= expect(
        !session.load(savedIni, *scenario, changedMap, &loadError)
            && session.stateHash() == atomicHash,
        "map fingerprint mismatch rejects load atomically"
    );

    CooperativeScenarioMapValidation invalidMap = validation;
    invalidMap.valid = false;
    RuntimeCooperativeSessionState reconcileProbe = session;
    const std::uint64_t reconcileHash = reconcileProbe.stateHash();
    const RuntimeCooperativeSessionReconcileReport invalidReport =
        reconcileProbe.reconcile(*scenario, invalidMap);
    success &= expect(
        !invalidReport.contextValid
            && reconcileProbe.stateHash() == reconcileHash,
        "invalid reconcile context does not mutate session"
    );
    const RuntimeCooperativeSessionReconcileReport changedReport =
        reconcileProbe.reconcile(*scenario, changedMap);
    success &= expect(
        changedReport.contextValid
            && changedReport.resetForMapChange
            && !reconcileProbe.active(),
        "map change reconcile clears stale cooperative session"
    );

    RuntimeCooperativeSessionBuildOptions duplicateOwners = options;
    duplicateOwners.owners[1] = "localhouse";
    RuntimeCooperativeSessionState atomicBuild = session;
    const std::uint64_t buildHash = atomicBuild.stateHash();
    success &= expect(
        !atomicBuild.begin(
            *scenario,
            validation,
            duplicateOwners
        ).accepted
            && atomicBuild.stateHash() == buildHash,
        "invalid owner bindings reject begin atomically"
    );
    return success;
}

bool runStockSmoke(
    const std::string& scenarioPath,
    const std::string& mapRoot
) {
    IniFile scenarioIni;
    CooperativeScenarioDatabase database;
    bool success = expect(
        scenarioIni.loadFromFile(scenarioPath)
            && database.loadFromIni(scenarioIni)
            && database.scenarioCount() == 10,
        "ten stock cooperative definitions load"
    );
    if (!success) {
        return false;
    }

    const std::vector<std::string> incompleteAuthoredStartMaps{
        "C1A03MD", "C2S01MD", "C3Y01MD"
    };
    int lockedBootstrapMaps = 0;
    for (const CooperativeScenarioDefinition& scenario :
         database.scenarios()) {
        const std::filesystem::path mapPath =
            std::filesystem::path(mapRoot)
            / (lowerAscii(scenario.mapId) + ".map");
        IniFile mapIni;
        MapDatabase map;
        success &= expect(
            mapIni.loadFromFile(mapPath.string())
                && map.loadFromIni(mapIni),
            "stock map load " + scenario.mapId
        );
        if (!success) {
            continue;
        }
        const CooperativeScenarioMapValidation validation =
            database.validateMap(scenario, map, mapIni);
        RuntimeCooperativeSessionState session;
        success &= expect(
            validation.valid
                && session.begin(scenario, validation).accepted,
            "stock session build " + scenario.mapId
        );
        if (!session.active()) {
            continue;
        }
        success &= verifyFixedSessionContract(
            scenario,
            validation,
            session,
            "stock " + scenario.mapId
        );
        if (std::find(
                incompleteAuthoredStartMaps.begin(),
                incompleteAuthoredStartMaps.end(),
                scenario.mapId
            ) != incompleteAuthoredStartMaps.end()) {
            const RuntimeCooperativeBootstrapPlan plan =
                session.bootstrapPlan();
            const bool allSlotsBootstrapped = std::all_of(
                plan.starts.begin(),
                plan.starts.end(),
                [](const RuntimeCooperativeStartBootstrapInstruction& start) {
                    return start.slotIndex >= 0
                        && start.includeMcv
                        && start.includeBasicStartingUnits;
                }
            );
            success &= expect(
                plan.policy.useAuthoredTriggers
                    && plan.policy.requiresCooperativeStartBootstrap
                    && allSlotsBootstrapped,
                "stock incomplete authored force gets A-D bootstrap "
                    + scenario.mapId
            );
            lockedBootstrapMaps += allSlotsBootstrapped ? 1 : 0;
        }

        std::string saved;
        IniFile savedIni;
        RuntimeCooperativeSessionState loaded;
        success &= expect(
            serializeAndParse(session, saved, savedIni)
                && loaded.load(savedIni, scenario, validation)
                && loaded.stateHash() == session.stateHash(),
            "stock session save/hash " + scenario.mapId
        );
    }
    success &= expect(
        lockedBootstrapMaps
            == static_cast<int>(incompleteAuthoredStartMaps.size()),
        "known stock incomplete-force maps retain cooperative bootstrap"
    );
    return success;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--fixture") {
        return runFixtureSmoke(argv[2], argv[3]) ? 0 : 1;
    }
    if (argc == 4 && std::string(argv[1]) == "--stock") {
        return runStockSmoke(argv[2], argv[3]) ? 0 : 1;
    }
    std::cerr
        << "usage: concord_cooperative_session_system_smoke "
        << "(--fixture <coop.ini> <map> | --stock <coopcampmd.ini> <map-root>)\n";
    return 2;
}
