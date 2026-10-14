#include "runtime/config/RuntimeConfig.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void writeFixture(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    require(!output.fail(), "could not write fixture: " + path.string());
}

std::string readFixture(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "could not read fixture: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct CurrentDirectoryGuard {
    fs::path previous = fs::current_path();
    ~CurrentDirectoryGuard() {
        std::error_code ignored;
        fs::current_path(previous, ignored);
    }
};

void requirePath(const std::string& actual, const fs::path& expected,
    const std::string& message) {
    require(fs::weakly_canonical(fs::path(actual)) == fs::weakly_canonical(expected),
        message + ": got " + actual + ", expected " + expected.string());
}

RuntimeConfig loadFixture(const fs::path& path, const std::string& text) {
    writeFixture(path, text);
    RuntimeConfigOverrides overrides;
    overrides.configPath = path.string();
    return loadRuntimeConfig(overrides);
}

void checkDefaults(const fs::path& project) {
    // This fixture never creates runtime_config.ini, including on repeated runs.
    const RuntimeConfig config = loadRuntimeConfig({});
    requirePath(config.configPath, project / "local_data/runtime_config.ini", "default INI path");
    requirePath(config.resourceRootPath, project / "RedAlert2", "default resources");
    requirePath(config.localDataRootPath, project / "local_data", "default local data");
    require(config.modRootPath.empty(), "default mod root must remain empty");
    require(config.windowWidth == 1024 && config.windowHeight == 640
        && config.fixedTickHz == 30 && config.targetFps == 60, "default timing/display changed");
    require(config.gameMode == "Battle" && config.startingCredits == 200000
        && config.startingUnits == 0 && config.techLevel == 10 && config.gameSpeed == 4,
        "default skirmish parameters changed");
    require(config.shortGame && !config.mcvRepacks && !config.crates && config.superweapons
        && !config.buildOffAlly && !config.computerAiEnabled && !config.noCooldown,
        "default skirmish switches changed");
    require(config.showMainMenu && config.showSkirmishMenu && config.musicEnabled
        && config.soundEffectsEnabled && config.musicVolume == 100 && config.soundVolume == 100
        && config.voiceVolume == 100, "default menus/audio changed");
    require(config.scrollRate == 3 && config.visualDetail == 2 && config.tooltipsEnabled
        && config.targetLinesEnabled && config.scrollCoastingEnabled && config.seeHiddenObjects,
        "default input/presentation changed");
    require(config.playerSlots.empty() && config.mapStartingPointLimit == 0
        && !config.multiplayerActive && config.keyBindings.empty(),
        "loading defaults must not invent player slots, map limits or bindings");
}

void checkParsing(const fs::path& fixture) {
    const RuntimeConfig config = loadFixture(fixture,
        "[Runtime]\nWindowWidth=1280pixels\nWindowHeight=0\nFixedTickHz=-1\nTargetFps=oops\n"
        "ShowMainMenu=TRUE\n[Skirmish]\nCredits=9000credits\nUnitCount=-3\nTechLevel=50\n"
        "GameSpeed=2fast\nShowSetup=unknown\nShortGame=NO\nMCVRepacks=1\nCrates=yes\n"
        "Superweapons=false\nBuildOffAlly=true\nComputerAI=Yes\nNoCooldown=1\n"
        "[Options]\nScrollRate=-4\nDetailLevel=9\nToolTips=0\nUnitActionLines=yes\n"
        "ScrollCoasting=true\nSeeHiddenObjects=invalid\n"
        "[Campaign]\nSourceId=  YR  \nMission=ALL01\nDifficulty=Hard\nSavePath=saves/campaign.sav\n"
        "[Hotkey]\nReferenceLayout=20260913\nOptions=27\nStopObject=1619\n"
        "GuardObject=512\nScatterObject=2049\nDeployObject=65junk\nCenterView=-1\n"
        "TeamCreate_1=561\nTaunt_8=123\nTaunt_9=124\nUnknownCommand=65\n");
    require(config.windowWidth == 1280 && config.windowHeight == 640
        && config.fixedTickHz == 30 && config.targetFps == 60 && config.startingCredits == 9000,
        "positive integers must retain stoi prefix acceptance and invalid-value fallbacks");
    require(config.startingUnits == 0 && config.techLevel == 10 && config.gameSpeed == 4
        && config.scrollRate == 0 && config.visualDetail == 2,
        "strict integer parsing or clamping changed");
    require(config.showMainMenu && !config.showSkirmishMenu && !config.shortGame
        && config.mcvRepacks && config.crates && !config.superweapons && config.buildOffAlly
        && config.computerAiEnabled && config.noCooldown && !config.tooltipsEnabled
        && config.targetLinesEnabled && config.scrollCoastingEnabled && !config.seeHiddenObjects,
        "boolean parsing must accept only yes/true/1, ignoring case and whitespace");
    require(config.campaignSourceId == "yr" && config.campaignMissionId == "ALL01"
        && config.campaignDifficulty == "Hard", "campaign fields or source normalization changed");
    const std::vector<std::pair<std::string, int>> expected{
        {"Options", 27}, {"StopObject", 1619}, {"TeamCreate_1", 561}, {"Taunt_8", 123}};
    require(config.keyboardReferenceLayout && config.keyBindings == expected,
        "hotkey filter must preserve modifier bits and eight taunts, rejecting unsupported/invalid keys");
    const auto fallback = loadFixture(fixture,
        "[Skirmish]\nUnitCount=5x\nTechLevel=-2\nGameSpeed=100\n"
        "[Options]\nScrollRate=4x\nDetailLevel=-1\n"
        "[Campaign]\nSourceId= Custom-Pack \n[Hotkey]\nReferenceLayout=other\n");
    require(fallback.startingUnits == 0 && fallback.techLevel == 1 && fallback.gameSpeed == 6
        && fallback.scrollRate == 3 && fallback.visualDetail == 0,
        "strict integer fallback and opposite clamp boundaries changed");
    require(fallback.campaignSourceId == "custom-pack" && !fallback.keyboardReferenceLayout,
        "custom campaign source IDs must remain lowercase and unknown layouts disabled");
}

void checkPercentages(const fs::path& fixture) {
    const std::vector<std::pair<std::string, int>> cases{
        {"0.375", 38}, {"1.0", 100}, {"1", 1}, {"27.6", 28}, {"-2", 0},
        {"130", 100}, {"nan", 100}, {"inf", 100}, {"25junk", 100}, {"", 100}};
    for (const auto& [text, expected] : cases) {
        const auto config = loadFixture(fixture,
            "[Audio]\nMusicVolume=" + text + "\nSoundVolume=" + text + "\nVoiceVolume=" + text + "\n");
        require(config.musicVolume == expected && config.soundVolume == expected
            && config.voiceVolume == expected, "audio percentage conversion failed for '" + text + "'");
    }
}

void checkPathsAndOverrides(const fs::path& project, const fs::path& nested) {
    const fs::path fixture = project / "paths.ini";
    writeFixture(project / "shared.map", "project map");
    writeFixture(nested / "shared.map", "current directory map");
    writeFixture(project / "project-only.map", "project map");
    const auto config = loadFixture(fixture,
        "[Runtime]\nResourceRoot=assets\nLocalDataRoot=data\nModRoot=mods/test\n"
        "DefaultMap=shared.map\nWindowWidth=1111\nWindowHeight=777\nFixedTickHz=22\nTargetFps=44\n"
        "RuntimeReplayEvents=logs/record.txt\n[Campaign]\nSavePath=saves/campaign.sav\n");
    requirePath(config.defaultMapPath, nested / "shared.map", "existing cwd file must win over project file");
    requirePath(config.resourceRootPath, project / "assets", "missing relative resource path");
    requirePath(config.modRootPath, project / "mods/test", "relative mod root");
    requirePath(config.campaignSavePath, project / "saves/campaign.sav", "campaign save path");
    requirePath(config.runtimeReplayEventOutputPath, project / "logs/record.txt", "replay output path");
    RuntimeConfigOverrides overrides;
    overrides.configPath = "paths.ini";
    overrides.resourceRootPath = (project / "absolute-assets").string();
    overrides.localDataRootPath = "override-data";
    overrides.defaultMapPath = "project-only.map";
    overrides.runtimeReplayEventOutputPath = "override-events.txt";
    overrides.windowWidth = 1600;
    overrides.windowHeight = -1;
    overrides.fixedTickHz = 0;
    overrides.targetFps = 120;
    const auto overridden = loadRuntimeConfig(overrides);
    requirePath(overridden.configPath, fixture, "relative config path must resolve against project root");
    requirePath(overridden.resourceRootPath, project / "absolute-assets", "absolute override path");
    requirePath(overridden.localDataRootPath, project / "override-data", "relative override path");
    requirePath(overridden.defaultMapPath, project / "project-only.map", "project-only map override");
    requirePath(overridden.runtimeReplayEventOutputPath, project / "override-events.txt", "replay override");
    require(overridden.windowWidth == 1600 && overridden.windowHeight == 777
        && overridden.fixedTickHz == 22 && overridden.targetFps == 120,
        "positive CLI values must override INI; zero/negative overrides must preserve it");
    require(overridden.modRootPath == config.modRootPath, "empty CLI string must preserve INI mod root");
    overrides.modRootPath = "override-mod";
    requirePath(loadRuntimeConfig(overrides).modRootPath, project / "override-mod", "nonempty mod override");
}

void checkPreferenceRoundTrip(const fs::path& fixture) {
    auto config = loadFixture(fixture,
        "; keep this comment\n[Runtime]\nResourceRoot=custom-assets\nFixedTickHz=25\n"
        "[Skirmish]\nCredits=7000\nCrates=no\n[Audio]\nMusicVolume=12\n"
        "[Preserved]\nSentinel=keep\n[Audio]\nMusicVolume=23\nCustomAudio=keep-too\n");
    config.windowWidth = 1500;
    config.windowHeight = 850;
    config.targetFps = 90;
    config.gameSpeed = 5;
    config.scrollRate = 99;
    config.visualDetail = -1;
    config.musicVolume = 31;
    config.soundVolume = -10;
    config.voiceVolume = 110;
    config.tooltipsEnabled = false;
    config.targetLinesEnabled = false;
    config.scrollCoastingEnabled = false;
    config.seeHiddenObjects = false;
    config.musicEnabled = false;
    config.soundEffectsEnabled = false;
    config.keyboardReferenceLayout = true;
    config.keyBindings = {{"StopObject", 1619}, {"TeamCreate_1", 561}, {"Invalid", 512}};
    std::string error;
    require(saveRuntimeConfigPreferences(config, error), "preference save failed: " + error);
    RuntimeConfigOverrides overrides;
    overrides.configPath = fixture.string();
    const auto reloaded = loadRuntimeConfig(overrides);
    require(reloaded.windowWidth == 1500 && reloaded.windowHeight == 850 && reloaded.targetFps == 90
        && reloaded.gameSpeed == 5 && reloaded.scrollRate == 6 && reloaded.visualDetail == 0
        && reloaded.musicVolume == 31 && reloaded.soundVolume == 0 && reloaded.voiceVolume == 100,
        "saved display/input/audio preferences must reload with clamping and final-section precedence");
    require(!reloaded.tooltipsEnabled && !reloaded.targetLinesEnabled && !reloaded.scrollCoastingEnabled
        && !reloaded.seeHiddenObjects && !reloaded.musicEnabled && !reloaded.soundEffectsEnabled,
        "saved boolean preferences did not round-trip");
    require(reloaded.keyboardReferenceLayout && reloaded.keyBindings.size() == 2
        && reloaded.keyBindings[0] == std::pair<std::string, int>{"StopObject", 1619}
        && reloaded.keyBindings[1] == std::pair<std::string, int>{"TeamCreate_1", 561},
        "saved hotkeys did not round-trip");
    require(reloaded.fixedTickHz == 25 && reloaded.startingCredits == 7000 && !reloaded.crates,
        "saving preferences must preserve match and simulation fields");
    const std::string saved = readFixture(fixture);
    for (const std::string& preserved : {"; keep this comment", "ResourceRoot=custom-assets",
        "Sentinel=keep", "CustomAudio=keep-too"})
        require(saved.find(preserved) != std::string::npos, "preference save lost: " + preserved);
    require(saved.find("Invalid=") == std::string::npos, "modifier-only hotkey was saved");
    require(saveRuntimeSkirmishCratePreference(config, true, error), "crate save failed: " + error);
    const auto withCrates = loadRuntimeConfig(overrides);
    require(withCrates.crates && withCrates.musicVolume == 31 && withCrates.startingCredits == 7000,
        "crate-only save changed unrelated preferences or failed to enable crates");
    require(saveRuntimeSkirmishCratePreference(config, false, error), "crate disable failed: " + error);
    require(!loadRuntimeConfig(overrides).crates, "crate preference did not persist when disabled");
    config.configPath.clear();
    require(!saveRuntimeConfigPreferences(config, error) && !error.empty(), "empty save path must fail clearly");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2 && fs::path(argv[1]).is_absolute(),
            "usage: concord_runtime_config_smoke <absolute isolated output directory>");
        const fs::path project = fs::path(argv[1]) / "fixture-project";
        const fs::path nested = project / "nested";
        fs::create_directories(project / "local_data");
        fs::create_directories(project / "RedAlert2");
        fs::create_directories(nested);
        writeFixture(project / "CMakeLists.txt", "# isolated configuration test root\n");
        CurrentDirectoryGuard restore;
        fs::current_path(nested);
        checkDefaults(project);
        checkParsing(project / "parsing.ini");
        checkPercentages(project / "percentages.ini");
        checkPathsAndOverrides(project, nested);
        checkPreferenceRoundTrip(project / "preferences.ini");
        std::cout << "Runtime config: defaults, parsing, paths, overrides and preference round-trips passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime config smoke failed: " << error.what() << '\n';
        return 1;
    }
}
