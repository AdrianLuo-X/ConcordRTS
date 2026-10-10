#include "data/campaign/CampaignDatabase.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Campaign map-selection smoke failure: "
                  << message << '\n';
    }
    return condition;
}

std::filesystem::path createUniqueRoot(bool& success) {
    std::error_code error;
    const std::filesystem::path temporaryRoot =
        std::filesystem::temp_directory_path(error);
    if (error) {
        success &= expect(false, "could not locate the temporary directory");
        return {};
    }
    const auto timestamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    for (unsigned int attempt = 0; attempt < 64U; ++attempt) {
        const std::filesystem::path candidate = temporaryRoot / (
            "concord-campaign-map-selection-smoke-"
            + std::to_string(timestamp) + '-' + std::to_string(attempt)
        );
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            return candidate;
        }
        if (error) {
            success &= expect(
                false,
                "could not create isolated smoke directory: "
                    + error.message()
            );
            return {};
        }
    }
    success &= expect(false, "unique smoke-directory attempts were exhausted");
    return {};
}

bool writeFixture(
    const std::filesystem::path& path,
    const std::string& contents
) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    return expect(
        output.good(),
        "could not write fixture " + path.filename().string()
    );
}

bool createFixtureDirectory(const std::filesystem::path& path) {
    std::error_code error;
    const bool created = std::filesystem::create_directory(path, error);
    return expect(
        created && !error,
        "could not create fixture directory " + path.filename().string()
    );
}

bool hasWarning(
    const CampaignDatabase& database,
    const std::string& fragment
) {
    for (const std::string& warning : database.warnings) {
        if (warning.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool removeKnownPath(const std::filesystem::path& path, bool directory) {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    return expect(
        removed && !error,
        std::string{"could not remove known smoke "}
            + (directory ? "directory " : "file ")
            + path.filename().string()
    );
}

} // namespace

int main() {
    bool success = true;
    const std::filesystem::path root = createUniqueRoot(success);
    if (root.empty()) {
        return 1;
    }

    const std::filesystem::path resourcesPath = root / "resources";
    const std::filesystem::path mixExportsPath = resourcesPath / "mix_exports";
    const std::filesystem::path yrPath = mixExportsPath / "ra2md";
    const std::filesystem::path yrLocalPath = yrPath / "localmd";
    const std::filesystem::path yrTextPath = mixExportsPath / "langmd";
    const std::filesystem::path yrMapsPath = mixExportsPath / "mapsmd03";
    const std::filesystem::path ra2Path = mixExportsPath / "ra2";
    const std::filesystem::path ra2LocalPath = ra2Path / "local";
    const std::filesystem::path ra2TextPath = mixExportsPath / "language";
    const std::filesystem::path ra2AlliedMapsPath = mixExportsPath / "maps01";
    const std::filesystem::path ra2SovietMapsPath = mixExportsPath / "maps02";

    success &= createFixtureDirectory(resourcesPath);
    success &= createFixtureDirectory(mixExportsPath);
    success &= createFixtureDirectory(yrPath);
    success &= createFixtureDirectory(yrLocalPath);
    success &= createFixtureDirectory(yrTextPath);
    success &= createFixtureDirectory(yrMapsPath);
    success &= createFixtureDirectory(ra2Path);
    success &= createFixtureDirectory(ra2LocalPath);
    success &= createFixtureDirectory(ra2TextPath);
    success &= createFixtureDirectory(ra2AlliedMapsPath);
    success &= createFixtureDirectory(ra2SovietMapsPath);

    const std::filesystem::path battlePath = yrLocalPath / "battlemd.ini";
    const std::filesystem::path mapSelectionPath = yrLocalPath / "mapselmd.ini";
    const std::filesystem::path missionPath = yrLocalPath / "missionmd.ini";
    const std::filesystem::path textPath = yrTextPath / "ra2md.csf";
    const std::filesystem::path all01Path = yrMapsPath / "all01.map";
    const std::filesystem::path all02Path = yrMapsPath / "all02.map";
    const std::filesystem::path sov01Path = yrMapsPath / "sov01.map";

    const std::filesystem::path ra2BattlePath = ra2LocalPath / "battle.ini";
    const std::filesystem::path ra2MapSelectionPath =
        ra2LocalPath / "mapsel.ini";
    const std::filesystem::path ra2MissionPath = ra2LocalPath / "mission.ini";
    const std::filesystem::path ra2CatalogPath = ra2TextPath / "ra2.csf";
    const std::filesystem::path trn01Path =
        ra2AlliedMapsPath / "trn01t.map";
    const std::filesystem::path trn02Path =
        ra2AlliedMapsPath / "trn02t.map";
    const std::filesystem::path ra2All01Path =
        ra2AlliedMapsPath / "all01t.map";
    const std::filesystem::path ra2All02Path =
        ra2AlliedMapsPath / "all02s.map";
    const std::filesystem::path ra2Sov01Path =
        ra2SovietMapsPath / "sov01t.map";
    const std::filesystem::path ra2Sov02Path =
        ra2SovietMapsPath / "sov02t.map";

    success &= writeFixture(battlePath, R"ini([Battles]
1=ALL1
2=SOV1
3=ALL02

[ALL1]
Scenario=all01.map
Description=DESC:YRALL

[SOV1]
Scenario=sov01.map
Description=DESC:YRSOV

[ALL02]
Scenario=all02.map
Description=DESC:YRDEBUG
DebugOnly=yes
)ini");

    success &= writeFixture(mapSelectionPath, R"ini([GDI]
Anims=MapAnimations
Sounds=GDISFX
1=ALL01
2=ALL02

[Nod]
Anims=MapAnimations
Sounds=NODSFX
1=Sov01

[MapAnimations]
Palette=MapSel.pal
TextRect=92,322,332,78
1=SMLOGO.SHP,16,322,5
2=GLOBE.SHP,545,168,7

[GDISFX]
Overlay=GSWEEP.AUD,60
TargetFlyIn=BESTBOX.AUD,75
EnterRegion=EFFICIEN.AUD,40
ExitRegion=
ClickRegion=CLICK.AUD,90

[NODSFX]
Overlay=NSWEEP.AUD,55
TargetFlyIn=BESTBOX.AUD,70
EnterRegion=EFFICIEN.AUD,35
ExitRegion=
ClickRegion=

[ALL00]
Scenario=
Description=
VoiceOver=
MapVQ=GDIMAP01.VQA
Overlays=RG01A.SHP,RN01A.SHP
Targets=1,144,70
ClickMap=GDICLK01.PCX
1=ALL01

[ALL01]
Scenario=all01.map
Description=768
VoiceOver=GDI-01.AUD
MapVQ=GDIMAP01.VQA
Overlays=RG02A.SHP,RN02A.SHP
Targets=1,180,80,220,90
ClickMap=GDICLK01.PCX
2=ALL02
2=MissingDuplicateTarget
260=MissingOutOfRangeTarget

[ALL02]
Scenario=all02.map
Description=769
VoiceOver=GDI-02.AUD
MapVQ=GDIMAP02.VQA
Overlays=RG03A.SHP,RN03A.SHP
Targets=2,290,88,bad,108,400
ClickMap=GDICLK02.PCX

[Sov01]
Scenario=sov01.map
Description=787
VoiceOver=NOD-01.AUD
MapVQ=NODMAP01.VQA
Overlays=TN02A.SHP,TG02A.SHP
Targets=1,190,100
ClickMap=NODCLK01.PCX
)ini");
    success &= writeFixture(missionPath, R"ini([all01.map]
UIName=Name:YRAll01
Briefing=Brief:YRAll01

[all02.map]
UIName=Name:YRAll02
Briefing=Brief:YRAll02

[sov01.map]
UIName=Name:YRSov01
Briefing=Brief:YRSov01
)ini");
    success &= writeFixture(textPath, "synthetic YR catalog\n");
    success &= writeFixture(all01Path, R"ini([Basic]
Player=Americans
Credits=100
)ini");
    success &= writeFixture(all02Path, R"ini([Basic]
Player=Americans
Credits=120
)ini");
    success &= writeFixture(sov01Path, R"ini([Basic]
Player=Russians
Credits=90
)ini");

    success &= writeFixture(ra2BattlePath, R"ini([Battles]
1=TUT1
2=ALL1
3=SOV1
4=E31
5=TRN02
6=ALL02
7=SOV02

[TUT1]
Scenario=trn01t.map
Description=DESC:RA2TUT

[ALL1]
Scenario=all01t.map
Description=DESC:RA2ALL

[SOV1]
Scenario=sov01t.map
Description=DESC:RA2SOV

[E31]
Scenario=e31.map
Description=DESC:RA2ORPHAN

[TRN02]
Scenario=trn02t.map
Description=DESC:RA2DEBUGTRN
DebugOnly=yes

[ALL02]
Scenario=all02s.map
Description=DESC:RA2DEBUGALL
DebugOnly=yes

[SOV02]
Scenario=sov02t.map
Description=DESC:RA2DEBUGSOV
DebugOnly=yes
)ini");
    success &= writeFixture(ra2MapSelectionPath, R"ini([GDI]
Anims=BaseAnimations
Sounds=BaseGDISFX
1=Trn01
2=Trn02
3=ALL01
4=ALL02

[Nod]
Anims=BaseAnimations
Sounds=BaseNODSFX
1=Sov01
2=Sov02

[BaseAnimations]
Palette=BaseMapSel.pal
TextRect=91,321,330,76
1=BASELOGO.SHP,15,321,4

[BaseGDISFX]
Overlay=BASEGDI.AUD,61
TargetFlyIn=BASETARGET.AUD,76
EnterRegion=BASEENTER.AUD,41
ExitRegion=
ClickRegion=BASECLICK.AUD,91

[BaseNODSFX]
Overlay=BASENOD.AUD,56
TargetFlyIn=BASETARGET.AUD,71
EnterRegion=BASEENTER.AUD,36
ExitRegion=
ClickRegion=

[Trn01]
Scenario=trn01t.map
Description=100
VoiceOver=TRN-01.AUD
MapVQ=TRNMAP01.VQA
Targets=1,100,60
ClickMap=TRNCLK01.PCX
1=Trn02

[Trn02]
Scenario=trn02t.map
Description=101
VoiceOver=TRN-02.AUD
MapVQ=TRNMAP02.VQA
Targets=1,120,70
ClickMap=TRNCLK02.PCX
2=ALL01

[ALL00]
Scenario=
Description=
MapVQ=BASEMAP01.VQA
Targets=1,140,75
ClickMap=BASECLK01.PCX
1=ALL01

[ALL01]
Scenario=all01t.map
Description=200
VoiceOver=BASE-GDI-01.AUD
MapVQ=BASEMAP01.VQA
Targets=1,180,80
ClickMap=BASECLK01.PCX
2=ALL02

[ALL02]
Scenario=all02s.map
Description=201
VoiceOver=BASE-GDI-02.AUD
MapVQ=BASEMAP02.VQA
Targets=1,220,90
ClickMap=BASECLK02.PCX

[Sov01]
Scenario=sov01t.map
Description=300
VoiceOver=BASE-NOD-01.AUD
MapVQ=BASENOD01.VQA
Targets=1,190,100
ClickMap=BASENOD01.PCX
2=Sov02

[Sov02]
Scenario=sov02t.map
Description=301
VoiceOver=BASE-NOD-02.AUD
MapVQ=BASENOD02.VQA
Targets=1,210,110
ClickMap=BASENOD02.PCX
)ini");
    success &= writeFixture(ra2MissionPath, R"ini([trn01t.map]
UIName=Name:RA2Trn01
Briefing=Brief:RA2Trn01

[trn02t.map]
UIName=Name:RA2Trn02
Briefing=Brief:RA2Trn02

[all01t.map]
UIName=Name:RA2All01
Briefing=Brief:RA2All01

[all02s.map]
UIName=Name:RA2All02
Briefing=Brief:RA2All02

[sov01t.map]
UIName=Name:RA2Sov01
Briefing=Brief:RA2Sov01

[sov02t.map]
UIName=Name:RA2Sov02
Briefing=Brief:RA2Sov02
)ini");
    success &= writeFixture(ra2CatalogPath, "synthetic RA2 catalog\n");
    success &= writeFixture(trn01Path, R"ini([Basic]
Player=Americans
Credits=201
)ini");
    success &= writeFixture(trn02Path, R"ini([Basic]
Player=Americans
Credits=202
)ini");
    success &= writeFixture(ra2All01Path, R"ini([Basic]
Player=French
Credits=211
)ini");
    success &= writeFixture(ra2All02Path, R"ini([Basic]
Player=Germans
Credits=212
)ini");
    success &= writeFixture(ra2Sov01Path, R"ini([Basic]
Player=Russians
Credits=221
)ini");
    success &= writeFixture(ra2Sov02Path, R"ini([Basic]
Player=Confederation
Credits=222
)ini");

    CampaignDatabaseOptions options;
    options.resourceRootPath = root.string();
    const CampaignDatabase database = loadCampaignDatabase(options);
    success &= expect(database.loaded, "fixture database did not load");
    success &= expect(
        database.campaigns.size() == 5U,
        "dual-edition fixture did not retain five independent campaigns"
    );
    success &= expect(
        database.sources.size() == 2U
            && database.textCatalogPaths.size() == 2U,
        "RA2 and YR source provenance was not retained"
    );
    success &= expect(
        database.missionCount() == 9U,
        "dual-edition mission total included a stage, debug entry, or orphan"
    );
    success &= expect(
        database.availableMissionCount() == 9U,
        "map-selection warnings made a mission unavailable"
    );

    const CampaignDefinition* gdi = database.findCampaign("GDI");
    const CampaignDefinition* nod = database.findCampaign("nod");
    const CampaignDefinition* ra2Gdi = database.findCampaign("ra2-gdi");
    const CampaignDefinition* ra2Nod = database.findCampaign("ra2-nod");
    const CampaignDefinition* tutorial = database.findCampaign(
        "ra2-tutorial"
    );
    success &= expect(gdi != nullptr, "GDI campaign was not discovered");
    success &= expect(nod != nullptr, "Nod campaign was not discovered");
    success &= expect(ra2Gdi != nullptr, "RA2 Allied campaign was not discovered");
    success &= expect(ra2Nod != nullptr, "RA2 Soviet campaign was not discovered");
    success &= expect(tutorial != nullptr, "RA2 tutorial was not discovered");
    if (gdi != nullptr && nod != nullptr) {
        success &= expect(
            gdi->id == "gdi" && nod->id == "nod"
                && gdi->sourceId == "yr" && nod->sourceId == "yr",
            "legacy YR gdi/nod identities were displaced by RA2 aliases"
        );
    }
    if (gdi != nullptr && ra2Gdi != nullptr && ra2Nod != nullptr
        && tutorial != nullptr) {
        success &= expect(
            ra2Gdi->sourceId == "ra2"
                && ra2Nod->sourceId == "ra2"
                && tutorial->sourceId == "ra2"
                && !ra2Gdi->edition.empty()
                && ra2Gdi->edition != gdi->edition,
            "RA2 campaign provenance was not isolated from YR"
        );
        success &= expect(
            ra2Gdi->missions.size() == 2U
                && ra2Nod->missions.size() == 2U
                && tutorial->missions.size() == 2U,
            "RA2 tutorial and the two main campaigns were not partitioned"
        );
        success &= expect(
            database.findMission("ra2-gdi", "trn01") == nullptr
                && database.findMission("ra2-gdi", "trn02") == nullptr
                && database.findMission("ra2-tutorial", "all01") == nullptr
                && database.findMission("ra2-tutorial", "all02") == nullptr,
            "tutorial stages leaked into the RA2 Allied main campaign"
        );
        success &= expect(
            std::filesystem::path(ra2Gdi->textCatalogPath).filename()
                    == "ra2.csf"
                && std::filesystem::path(gdi->textCatalogPath).filename()
                    == "ra2md.csf",
            "campaigns did not retain their edition-specific CSF"
        );
    }

    const CampaignMissionDefinition* yrAll01 =
        database.findMission("gdi", "all01");
    const CampaignMissionDefinition* baseAll01 =
        database.findMission("ra2-gdi", "all01");
    success &= expect(
        yrAll01 != nullptr && baseAll01 != nullptr
            && yrAll01 != baseAll01
            && yrAll01->sourceId == "yr"
            && baseAll01->sourceId == "ra2"
            && yrAll01->displayNameKey == "Name:YRAll01"
            && yrAll01->briefingKey == "Brief:YRAll01"
            && baseAll01->displayNameKey == "Name:RA2All01"
            && baseAll01->briefingKey == "Brief:RA2All01"
            && yrAll01->mapFileName == "all01.map"
            && baseAll01->mapFileName == "all01t.map"
            && yrAll01->startingCredits == 100
            && baseAll01->startingCredits == 211,
        "same-named RA2/YR mission metadata crossed source boundaries"
    );
    if (gdi != nullptr) {
        success &= expect(
            gdi->mapSelectionStages.size() == 3U,
            "GDI did not retain exactly ALL00/ALL01/ALL02"
        );
        const CampaignMapSelectionStage* all00 =
            gdi->findMapSelectionStage("ALL00");
        const CampaignMapSelectionStage* all01 =
            gdi->findMapSelectionStage("all01");
        const CampaignMapSelectionStage* all02 =
            gdi->findMapSelectionStage("ALL02");
        success &= expect(all00 != nullptr, "ALL00 predecessor stage was dropped");
        success &= expect(all01 != nullptr, "ALL01 stage was dropped");
        success &= expect(all02 != nullptr, "ALL02 stage was dropped");
        if (all00 != nullptr) {
            success &= expect(
                all00->scenario.empty()
                    && all00->choices.size() == 1U
                    && all00->choices[0].targetStageId == "all01"
                    && all00->choices[0].targetMissionId == "all01",
                "ALL00 -> ALL01 was not resolved"
            );
        }
        if (all01 != nullptr) {
            success &= expect(
                all01->overlays.size() == 2U
                    && all01->overlays[0] == "RG02A.SHP"
                    && all01->declaredTargetCount == 1
                    && all01->targets.size() == 2U
                    && all01->targets[1].x == 220
                    && all01->targets[1].y == 90,
                "overlays or mismatched Targets were not preserved"
            );
            success &= expect(
                !all01->choices.empty()
                    && all01->choices[0].targetMissionId == "all02",
                "ALL01 -> ALL02 mission edge was not resolved"
            );
        }
        if (all02 != nullptr) {
            success &= expect(
                all02->declaredTargetCount == 2
                    && all02->targets.size() == 1U
                    && all02->targets[0].x == 290,
                "valid coordinate pairs were not retained around bad Targets"
            );
        }

        success &= expect(
            gdi->mapSelectionAnims.sourceSection == "MapAnimations"
                && gdi->mapSelectionAnims.palette == "MapSel.pal"
                && gdi->mapSelectionAnims.textRect.valid
                && gdi->mapSelectionAnims.textRect.width == 332
                && gdi->mapSelectionAnims.animations.size() == 2U
                && gdi->mapSelectionAnims.animations[1].shpFile
                    == "GLOBE.SHP"
                && gdi->mapSelectionAnims.animations[1].rate == 7,
            "animation config was not parsed"
        );
        success &= expect(
            gdi->mapSelectionSounds.sourceSection == "GDISFX"
                && gdi->mapSelectionSounds.overlay.sample == "GSWEEP.AUD"
                && gdi->mapSelectionSounds.overlay.volumeSpecified
                && gdi->mapSelectionSounds.overlay.volume == 60
                && gdi->mapSelectionSounds.targetFlyIn.volume == 75
                && gdi->mapSelectionSounds.enterRegion.volume == 40
                && gdi->mapSelectionSounds.exitRegion.sample.empty()
                && gdi->mapSelectionSounds.clickRegion.volume == 90,
            "sound config was not parsed"
        );

        const CampaignMissionDefinition* all01Mission =
            database.findMission("gdi", "all01");
        const CampaignMissionDefinition* all02Mission =
            database.findMission("gdi", "all02");
        success &= expect(
            all01Mission != nullptr
                && all01Mission->incomingMapSelectionStages.size() == 1U
                && all01Mission->incomingMapSelectionStages[0] == "all00"
                && all01Mission->targetCells.size() == 5U,
            "ALL01 incoming edge or raw compatibility fields were lost"
        );
        success &= expect(
            all02Mission != nullptr
                && all02Mission->incomingMapSelectionStages.size() == 1U
                && all02Mission->incomingMapSelectionStages[0] == "all01",
            "ALL02 incoming stage was not populated"
        );
    }
    if (nod != nullptr) {
        success &= expect(
            nod->mapSelectionStages.size() == 1U
                && nod->findMapSelectionStage("Sov01") != nullptr,
            "Nod graph invented or dropped a stage"
        );
        const CampaignMissionDefinition* sov01Mission =
            database.findMission("nod", "sov01");
        success &= expect(
            sov01Mission != nullptr
                && sov01Mission->incomingMapSelectionStages.empty(),
            "missing Sov00 produced a synthetic incoming edge"
        );
    }

    success &= expect(
        hasWarning(database, "Targets count mismatch"),
        "Targets count mismatch was not diagnosed"
    );
    success &= expect(
        hasWarning(database, "odd coordinate value count")
            && hasWarning(database, "invalid coordinate pair"),
        "malformed Targets were not diagnosed"
    );
    success &= expect(
        hasWarning(database, "duplicate color edge")
            && hasWarning(database, "color index out of range")
            && hasWarning(database, "dangling edge"),
        "bad choice edges were not diagnosed"
    );

    success &= removeKnownPath(battlePath, false);
    success &= removeKnownPath(mapSelectionPath, false);
    success &= removeKnownPath(missionPath, false);
    success &= removeKnownPath(textPath, false);
    success &= removeKnownPath(all01Path, false);
    success &= removeKnownPath(all02Path, false);
    success &= removeKnownPath(sov01Path, false);
    success &= removeKnownPath(ra2BattlePath, false);
    success &= removeKnownPath(ra2MapSelectionPath, false);
    success &= removeKnownPath(ra2MissionPath, false);
    success &= removeKnownPath(ra2CatalogPath, false);
    success &= removeKnownPath(trn01Path, false);
    success &= removeKnownPath(trn02Path, false);
    success &= removeKnownPath(ra2All01Path, false);
    success &= removeKnownPath(ra2All02Path, false);
    success &= removeKnownPath(ra2Sov01Path, false);
    success &= removeKnownPath(ra2Sov02Path, false);
    success &= removeKnownPath(ra2SovietMapsPath, true);
    success &= removeKnownPath(ra2AlliedMapsPath, true);
    success &= removeKnownPath(ra2TextPath, true);
    success &= removeKnownPath(ra2LocalPath, true);
    success &= removeKnownPath(ra2Path, true);
    success &= removeKnownPath(yrMapsPath, true);
    success &= removeKnownPath(yrTextPath, true);
    success &= removeKnownPath(yrLocalPath, true);
    success &= removeKnownPath(yrPath, true);
    success &= removeKnownPath(mixExportsPath, true);
    success &= removeKnownPath(resourcesPath, true);
    success &= removeKnownPath(root, true);

    if (!success) {
        return 1;
    }
    std::cout << "Campaign map-selection smoke passed\n";
    return 0;
}
