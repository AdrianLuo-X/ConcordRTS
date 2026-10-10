#include "data/campaign/CooperativeScenarioDatabase.h"

#include "core/io/IniFile.h"
#include "data/maps/MapDatabase.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "cooperative scenario smoke failure: " << message << '\n';
    }
    return condition;
}

bool hasDiagnostic(
    const std::vector<CooperativeScenarioDiagnostic>& diagnostics,
    const std::string& code
) {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [&code](const CooperativeScenarioDiagnostic& diagnostic) {
            return diagnostic.code == code;
        }
    );
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

std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool expectRejectedScenario(
    const std::string& text,
    const std::string& diagnosticCode
) {
    IniFile ini;
    CooperativeScenarioDatabase database;
    const bool parsed = ini.loadFromString(text);
    const bool loaded = parsed && database.loadFromIni(ini);
    return expect(
        !loaded && hasDiagnostic(database.diagnostics(), diagnosticCode),
        "malformed scenario was not rejected with " + diagnosticCode
    );
}

bool expectRejectedMap(
    const CooperativeScenarioDatabase& database,
    const CooperativeScenarioDefinition& scenario,
    const std::string& text,
    const std::string& diagnosticCode
) {
    IniFile ini;
    MapDatabase map;
    if (!ini.loadFromString(text) || !map.loadFromIni(ini)) {
        return expect(false, "malformed map fixture could not be parsed");
    }
    const CooperativeScenarioMapValidation validation =
        database.validateMap(scenario, map, ini);
    return expect(
        !validation.valid
            && hasDiagnostic(validation.diagnostics, diagnosticCode),
        "malformed map was not rejected with " + diagnosticCode
    );
}

const char* validSyntheticScenario = R"ini(
[Campaigns]
Campaign1=Fixture
[Fixture]
NumberOfCampaignMaps=1
CampaignName=Name:Fixture
CampaignPlayer1=Americans,British
CampaignEnemy1=Russians,YuriCountry
CampaignAI=Normal
Map1=COOPFIX,COOPFIX,COOPFIX
)ini";

const char* validSyntheticMap = R"map(
[Header]
StartX=0
StartY=0
Width=64
Height=64
NumberStartingPoints=4
NumCoopHumanStartSpots=2
[Map]
Theater=TEMPERATE
Size=0,0,64,64
LocalSize=0,0,64,64
[Waypoints]
0=10010
1=10020
2=20010
3=20020
)map";

bool runFixtureSmoke(const std::string& scenarioPath, const std::string& mapPath) {
    bool success = true;
    IniFile scenarioIni;
    IniFile mapIni;
    success &= expect(
        scenarioIni.loadFromFile(scenarioPath),
        "scenario fixture did not load"
    );
    success &= expect(mapIni.loadFromFile(mapPath), "map fixture did not load");
    if (!success) {
        return false;
    }

    CooperativeScenarioDatabase database;
    success &= expect(
        database.loadFromIni(scenarioIni),
        "valid scenario fixture was rejected"
    );
    success &= expect(
        database.loaded() && database.scenarioCount() == 1,
        "valid scenario database state"
    );
    success &= expect(
        database.fingerprint() != 0
            && database.identity().starts_with("coop-database-v2:"),
        "database identity"
    );

    const CooperativeScenarioDefinition* scenario =
        database.findByMapId("coopfix.map");
    success &= expect(scenario != nullptr, "case-insensitive map lookup");
    if (scenario == nullptr) {
        return false;
    }
    success &= expect(
        scenario->campaignId == "Campaign1"
            && scenario->campaignSection == "FixtureCoalition"
            && scenario->campaignOrdinal == 1
            && scenario->missionOrdinal == 1
            && scenario->mapId == "COOPFIX"
            && scenario->authoredMapIds.size() == 3,
        "scenario identity fields"
    );
    success &= expect(
        scenario->playerCountries
                == std::vector<std::string>{"Americans", "British"}
            && scenario->enemyCountries
                == std::vector<std::string>{"Russians", "YuriCountry"},
        "scenario country constraints"
    );

    const auto& slots = scenario->slots;
    success &= expect(
        slots[0].slotIndex == 0
            && slots[0].label == 'A'
            && slots[0].authoredOwnerId == 4475
            && slots[0].waypointId == 0
            && slots[0].role == CooperativeSlotRole::LocalHuman
            && slots[0].coalitionId == 0
            && slots[0].authoredHouse == "<Player @ A>"
            && slots[0].allowedCountries == scenario->playerCountries
            && slots[0].defaultCountry == "Americans",
        "slot A contract"
    );
    success &= expect(
        slots[1].label == 'B'
            && slots[1].authoredOwnerId == 4476
            && slots[1].role == CooperativeSlotRole::AiAlly
            && slots[1].coalitionId == 0
            && slots[1].allowedCountries == scenario->playerCountries
            && slots[1].defaultCountry == "Americans",
        "slot B contract"
    );
    success &= expect(
        slots[2].label == 'C'
            && slots[2].authoredOwnerId == 4477
            && slots[2].role == CooperativeSlotRole::AiEnemy
            && slots[2].coalitionId == 1
            && slots[2].allowedCountries == scenario->enemyCountries
            && slots[2].defaultCountry == "Russians"
            && slots[3].label == 'D'
            && slots[3].authoredOwnerId == 4478
            && slots[3].role == CooperativeSlotRole::AiEnemy
            && slots[3].coalitionId == 1
            && slots[3].allowedCountries == scenario->enemyCountries
            && slots[3].defaultCountry == "Russians",
        "enemy slot contracts"
    );

    for (int left = 0; left < 4; ++left) {
        for (int right = 0; right < 4; ++right) {
            success &= expect(
                scenario->allianceMatrix[static_cast<std::size_t>(left)]
                                        [static_cast<std::size_t>(right)]
                    == cooperativeSlotsAreAllied(left, right),
                "fixed A-D alliance matrix"
            );
        }
    }
    success &= expect(
        cooperativeSlotAllowsCountry(slots[0], "americans")
            && cooperativeSlotAllowsCountry(slots[1], "British")
            && !cooperativeSlotAllowsCountry(slots[0], "Russians")
            && cooperativeSlotAllowsCountry(slots[2], "yuricountry")
            && !cooperativeSlotAllowsCountry(slots[2], "British"),
        "slot country constraints and defaults"
    );

    constexpr std::array<const char*, 4> numericAliases{
        "4475", "4476", "4477", "4478"
    };
    constexpr std::array<const char*, 4> houseAliases{
        "<Player @ A>", "<Player @ B>",
        "<Player @ C>", "<Player @ D>"
    };
    for (int index = 0; index < 4; ++index) {
        success &= expect(
            cooperativeSlotIndexFromAlias(
                numericAliases[static_cast<std::size_t>(index)]
            ) == index
                && cooperativeSlotIndexFromAlias(
                    houseAliases[static_cast<std::size_t>(index)]
                ) == index,
            "exact numeric and house slot aliases"
        );
    }
    success &= expect(
        !cooperativeSlotIndexFromAlias("8997").has_value()
            && !cooperativeSlotIndexFromAlias("4479").has_value()
            && !cooperativeSlotIndexFromAlias(" 4475").has_value()
            && !cooperativeSlotIndexFromAlias("<Player @ a>").has_value()
            && !cooperativeSlotIndexFromAlias("<Player @ A>junk").has_value(),
        "unknown and inexact aliases are safely rejected"
    );

    MapDatabase map;
    success &= expect(map.loadFromIni(mapIni), "valid map fixture parse");
    const CooperativeScenarioMapValidation validation =
        database.validateMap(*scenario, map, mapIni);
    success &= expect(
        validation.valid
            && validation.contentFingerprint != 0
            && validation.contentIdentity.starts_with("coop-map-content-v1:")
            && validation.identityFingerprint != 0
            && validation.identity.starts_with("coop-map-v2:")
            && validation.waypoints[0].cell == 10010
            && validation.waypoints[1].cell == 10020
            && validation.waypoints[2].cell == 20010
            && validation.waypoints[3].cell == 20020,
        "valid cooperative map identity"
    );

    const std::string originalMapText = readTextFile(mapPath);
    const std::array<std::pair<const char*, const char*>, 4>
        contentMutations{{
            {"ProbeTrigger=FixtureEvent,FixtureAction,0,1,1",
             "ProbeTrigger=FixtureEvent,FixtureAction,0,1,2"},
            {"ProbeAction=1,14,0,4475,0,0,0,0,A",
             "ProbeAction=1,14,0,4476,0,0,0,0,A"},
            {"ProbeTeam=0,0,7,0,0,FixtureScript,0,0,0,0,0,0",
             "ProbeTeam=0,0,8,0,0,FixtureScript,0,0,0,0,0,0"},
            {"FixtureScript=1,0,0",
             "FixtureScript=1,0,1"}
        }};
    for (const auto& [before, after] : contentMutations) {
        std::string changedMapText = originalMapText;
        const std::size_t position = changedMapText.find(before);
        success &= expect(
            position != std::string::npos,
            std::string("fixture mutation source exists: ") + before
        );
        if (position == std::string::npos) {
            continue;
        }
        changedMapText.replace(position, std::string(before).size(), after);
        IniFile changedMapIni;
        success &= expect(
            changedMapIni.loadFromString(changedMapText),
            "changed map INI parses"
        );
        const CooperativeScenarioMapValidation changedValidation =
            database.validateMap(*scenario, map, changedMapIni);
        success &= expect(
            changedValidation.valid
                && changedValidation.contentFingerprint
                    != validation.contentFingerprint
                && changedValidation.identityFingerprint
                    != validation.identityFingerprint,
            std::string("full map fingerprint detects mission content change: ")
                + before
        );
    }

    CooperativeScenarioDatabase repeat;
    success &= expect(
        repeat.loadFromIni(scenarioIni)
            && repeat.fingerprint() == database.fingerprint()
            && repeat.identity() == database.identity()
            && repeat.scenarios()[0].fingerprint == scenario->fingerprint,
        "deterministic scenario fingerprints"
    );

    IniFile changedIni;
    CooperativeScenarioDatabase changed;
    const std::string changedText = R"ini(
[Campaigns]
Campaign1=FixtureCoalition
[FixtureCoalition]
NumberOfCampaignMaps=1
CampaignName=Name:Fixture_Coop
CampaignPlayer1=Americans,French
CampaignEnemy1=Russians,YuriCountry
CampaignAI=Normal
Map1=COOPFIX,COOPFIX,COOPFIX
)ini";
    success &= expect(
        changedIni.loadFromString(changedText)
            && changed.loadFromIni(changedIni)
            && changed.fingerprint() != database.fingerprint()
            && changed.scenarios()[0].fingerprint != scenario->fingerprint,
        "country constraint changes fingerprints"
    );

    success &= expectRejectedScenario(
        R"ini(
[Campaigns]
Campaign1=Fixture
[Fixture]
NumberOfCampaignMaps=1
CampaignName=Name:Fixture
CampaignPlayer1=Americans
CampaignAI=Normal
Map1=COOPFIX,COOPFIX,COOPFIX
)ini",
        "missing-field"
    );
    success &= expectRejectedScenario(
        R"ini(
[Campaigns]
Campaign1=Fixture
[Fixture]
NumberOfCampaignMaps=1
CampaignName=Name:Fixture
CampaignPlayer1=Americans,British
CampaignEnemy1=Russians,Americans
CampaignAI=Normal
Map1=COOPFIX,COOPFIX,COOPFIX
)ini",
        "country-coalition-overlap"
    );
    success &= expectRejectedScenario(
        R"ini(
[Campaigns]
Campaign1=Fixture
[Fixture]
NumberOfCampaignMaps=1
CampaignName=Name:Fixture
CampaignPlayer1=Americans,americans
CampaignEnemy1=Russians
CampaignAI=Normal
Map1=COOPFIX,COOPFIX,COOPFIX
)ini",
        "duplicate-country"
    );
    success &= expectRejectedScenario(
        R"ini(
[Campaigns]
Campaign1=First
Campaign2=Second
[First]
NumberOfCampaignMaps=1
CampaignName=Name:First
CampaignPlayer1=Americans
CampaignEnemy1=Russians
CampaignAI=Normal
Map1=SHARED,SHARED,SHARED
[Second]
NumberOfCampaignMaps=1
CampaignName=Name:Second
CampaignPlayer1=British
CampaignEnemy1=YuriCountry
CampaignAI=Normal
Map1=SHARED,SHARED,SHARED
)ini",
        "map-scenario-overlap"
    );
    success &= expectRejectedScenario(
        R"ini(
[Campaigns]
Campaign1=Fixture
[Fixture]
NumberOfCampaignMaps=one
CampaignName=Name:Fixture
CampaignPlayer1=Americans
CampaignEnemy1=Russians
CampaignAI=Normal
Map1=COOPFIX,COOPFIX,COOPFIX
)ini",
        "invalid-map-count"
    );
    success &= expectRejectedScenario(
        R"ini(
[Campaigns]
Campaign1=Fixture
[Fixture]
NumberOfCampaignMaps=1
CampaignName=Name:Fixture
CampaignPlayer1=Americans
CampaignEnemy1=Russians
CampaignAI=Normal
Map1=../COOPFIX,COOPFIX,COOPFIX
)ini",
        "invalid-map-id"
    );

    success &= expectRejectedMap(
        database,
        *scenario,
        R"map(
[Header]
StartX=0
StartY=0
Width=64
Height=64
NumberStartingPoints=3
NumCoopHumanStartSpots=1
[Map]
Theater=TEMPERATE
Size=0,0,64,64
LocalSize=0,0,64,64
[Waypoints]
0=10010
1=10020
2=20010
3=20020
)map",
        "invalid-start-count"
    );
    success &= expectRejectedMap(
        database,
        *scenario,
        R"map(
[Header]
StartX=0
StartY=0
Width=64
Height=64
NumberStartingPoints=4
NumCoopHumanStartSpots=3
[Map]
Theater=TEMPERATE
Size=0,0,64,64
LocalSize=0,0,64,64
[Waypoints]
0=10010
1=10020
2=20010
3=20020
)map",
        "invalid-human-start-count"
    );
    success &= expectRejectedMap(
        database,
        *scenario,
        R"map(
[Header]
StartX=0
StartY=0
Width=64
Height=64
NumberStartingPoints=4
NumCoopHumanStartSpots=2
[Map]
Theater=TEMPERATE
Size=0,0,64,64
LocalSize=0,0,64,64
[Waypoints]
0=10010
1=10020
2=20010
)map",
        "missing-waypoint"
    );
    success &= expectRejectedMap(
        database,
        *scenario,
        R"map(
[Header]
StartX=0
StartY=0
Width=64
Height=64
NumberStartingPoints=4
NumCoopHumanStartSpots=2
[Map]
Theater=TEMPERATE
Size=0,0,64,64
LocalSize=0,0,64,64
[Waypoints]
0=10010
1=10020
2=20010
3=20020
3=20030
)map",
        "duplicate-waypoint"
    );
    success &= expectRejectedMap(
        database,
        *scenario,
        R"map(
[Header]
StartX=0
StartY=0
Width=64
Height=64
NumberStartingPoints=4
NumCoopHumanStartSpots=2
[Map]
Theater=TEMPERATE
Size=0,0,64,64
LocalSize=0,0,64,64
[Waypoints]
0=10010
1=10010
2=20010
3=20020
)map",
        "overlapping-waypoint"
    );
    success &= expectRejectedMap(
        database,
        *scenario,
        R"map(
[Header]
StartX=0
StartY=0
Width=64
Height=64
NumberStartingPoints=4
NumCoopHumanStartSpots=2
[Map]
Theater=TEMPERATE
Size=0,0,64,64
LocalSize=0,0,64,64
[Waypoints]
0=10010
1=10020
2=20010
3=999999
)map",
        "invalid-waypoint-cell"
    );

    IniFile syntheticIni;
    IniFile syntheticMapIni;
    CooperativeScenarioDatabase syntheticDatabase;
    MapDatabase syntheticMap;
    success &= expect(
        syntheticIni.loadFromString(validSyntheticScenario)
            && syntheticDatabase.loadFromIni(syntheticIni)
            && syntheticMapIni.loadFromString(validSyntheticMap)
            && syntheticMap.loadFromIni(syntheticMapIni)
            && syntheticDatabase.validateMap(
                "COOPFIX",
                syntheticMap,
                syntheticMapIni
            ).valid
            && !syntheticDatabase.validateMap(
                "UNKNOWN",
                syntheticMap,
                syntheticMapIni
            ).valid,
        "inline valid and unknown-map contracts"
    );

    return success;
}

struct StockExpectation {
    const char* mapId;
    const char* campaignSection;
    int campaignOrdinal;
    int missionOrdinal;
    std::vector<std::string> players;
    std::vector<std::string> enemies;
    std::array<int, 4> waypointCells;
};

bool runStockSmoke(const std::string& scenarioPath, const std::string& mapRoot) {
    const std::vector<StockExpectation> expected{
        {"C1A01MD", "Allied", 1, 1,
            {"Americans"}, {"Russians", "Confederation"},
            {107034, 115073, 67133, 49104}},
        {"C1A02MD", "Allied", 1, 2,
            {"Americans", "British", "French"}, {"YuriCountry"},
            {172128, 127170, 123094, 71074}},
        {"C1A03MD", "Allied", 1, 3,
            {"Americans", "Alliance", "Germans", "French", "British"},
            {"Russians", "YuriCountry"}, {97091, 83094, 169077, 40133}},
        {"C2S01MD", "Soviet", 2, 1,
            {"Russians", "Arabs", "Confederation"}, {"Americans"},
            {109050, 77044, 114116, 114142}},
        {"C2S02MD", "Soviet", 2, 2,
            {"Arabs", "Africans"}, {"YuriCountry"},
            {114145, 163117, 82041, 32092}},
        {"C2S03MD", "Soviet", 2, 3,
            {"Russians"}, {"Germans", "YuriCountry"},
            {116126, 103163, 35113, 104034}},
        {"C3Y01MD", "Yuri", 3, 1,
            {"YuriCountry"}, {"Americans"},
            {109171, 100116, 95030, 44076}},
        {"C3Y02MD", "Yuri", 3, 2,
            {"YuriCountry"}, {"Confederation", "Russians"},
            {101111, 121144, 84070, 49050}},
        {"C3Y03MD", "Yuri", 3, 3,
            {"YuriCountry"}, {"Russians", "British"},
            {146119, 81126, 56062, 95032}},
        {"C4W01MD", "World", 4, 1,
            {"Americans", "Alliance", "French", "Germans", "British",
             "Africans", "Arabs", "Confederation", "Russians"},
            {"YuriCountry"}, {104131, 139071, 31088, 76058}}
    };

    IniFile scenarioIni;
    bool success = expect(
        scenarioIni.loadFromFile(scenarioPath),
        "stock coopcampmd.ini did not load"
    );
    CooperativeScenarioDatabase database;
    success &= expect(
        database.loadFromIni(scenarioIni),
        "stock coopcampmd.ini was rejected"
    );
    success &= expect(
        database.scenarioCount() == expected.size(),
        "stock cooperative scenario count"
    );
    if (!success) {
        for (const CooperativeScenarioDiagnostic& diagnostic :
             database.diagnostics()) {
            std::cerr
                << diagnostic.code << " " << diagnostic.source
                << " " << diagnostic.message << '\n';
        }
        return false;
    }

    std::set<std::uint64_t> mapFingerprints;
    for (const StockExpectation& item : expected) {
        const CooperativeScenarioDefinition* scenario =
            database.findByMapId(item.mapId);
        success &= expect(scenario != nullptr, std::string("stock lookup ") + item.mapId);
        if (scenario == nullptr) {
            continue;
        }
        success &= expect(
            scenario->campaignSection == item.campaignSection
                && scenario->campaignOrdinal == item.campaignOrdinal
                && scenario->missionOrdinal == item.missionOrdinal
                && scenario->playerCountries == item.players
                && scenario->enemyCountries == item.enemies,
            std::string("stock metadata ") + item.mapId
        );
        for (std::size_t slotIndex = 0; slotIndex < scenario->slots.size(); ++slotIndex) {
            const CooperativeScenarioSlot& slot = scenario->slots[slotIndex];
            success &= expect(
                slot.slotIndex == static_cast<int>(slotIndex)
                    && slot.label == static_cast<char>('A' + slotIndex)
                    && slot.authoredOwnerId
                        == 4475 + static_cast<int>(slotIndex)
                    && slot.waypointId == static_cast<int>(slotIndex)
                    && slot.coalitionId == (slotIndex < 2 ? 0 : 1)
                    && slot.role == (slotIndex == 0
                        ? CooperativeSlotRole::LocalHuman
                        : slotIndex == 1
                            ? CooperativeSlotRole::AiAlly
                            : CooperativeSlotRole::AiEnemy)
                    && slot.allowedCountries
                        == (slotIndex < 2 ? item.players : item.enemies)
                    && slot.defaultCountry
                        == (slotIndex < 2
                            ? item.players.front()
                            : item.enemies.front())
                    && cooperativeSlotAllowsCountry(
                        slot,
                        slot.defaultCountry
                    ),
                std::string("stock slot contract ") + item.mapId
            );
            for (std::size_t other = 0;
                 other < scenario->slots.size();
                 ++other) {
                success &= expect(
                    scenario->allianceMatrix[slotIndex][other]
                        == ((slotIndex < 2) == (other < 2)),
                    std::string("stock alliance matrix ") + item.mapId
                );
            }
        }

        const std::filesystem::path mapPath =
            std::filesystem::path(mapRoot)
            / (lowerAscii(item.mapId) + ".map");
        IniFile mapIni;
        MapDatabase map;
        success &= expect(
            mapIni.loadFromFile(mapPath.string()) && map.loadFromIni(mapIni),
            std::string("stock map load ") + item.mapId
        );
        const CooperativeScenarioMapValidation validation =
            database.validateMap(*scenario, map, mapIni);
        success &= expect(
            validation.valid
                && validation.contentFingerprint != 0
                && validation.identityFingerprint != 0
                && validation.identity.starts_with("coop-map-v2:"),
            std::string("stock map validation ") + item.mapId
        );
        for (std::size_t index = 0; index < item.waypointCells.size(); ++index) {
            success &= expect(
                validation.waypoints[index].cell == item.waypointCells[index],
                std::string("stock waypoint ") + item.mapId
                    + ":" + std::to_string(index)
            );
        }
        success &= expect(
            mapFingerprints.insert(validation.identityFingerprint).second,
            std::string("unique stock map fingerprint ") + item.mapId
        );

        const std::string stockMapText = readTextFile(mapPath.string());
        IniFile changedMapIni;
        const std::string actionProbe = stockMapText
            + "\n[Actions]\n__CoopFingerprintProbe="
            + std::to_string(item.missionOrdinal)
            + ",14,0,4475,0,0,0,0,A\n";
        success &= expect(
            !stockMapText.empty()
                && changedMapIni.loadFromString(actionProbe),
            std::string("stock fingerprint mutation parses ") + item.mapId
        );
        const CooperativeScenarioMapValidation changedValidation =
            database.validateMap(*scenario, map, changedMapIni);
        success &= expect(
            changedValidation.valid
                && changedValidation.contentFingerprint
                    != validation.contentFingerprint
                && changedValidation.identityFingerprint
                    != validation.identityFingerprint,
            std::string("stock action mutation changes fingerprint ")
                + item.mapId
        );
    }

    CooperativeScenarioDatabase repeat;
    success &= expect(
        repeat.loadFromIni(scenarioIni)
            && repeat.fingerprint() == database.fingerprint()
            && repeat.identity() == database.identity(),
        "stock database deterministic fingerprint"
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
        << "usage: concord_cooperative_scenario_smoke "
        << "(--fixture <coop.ini> <map> | --stock <coopcampmd.ini> <map-root>)\n";
    return 2;
}
