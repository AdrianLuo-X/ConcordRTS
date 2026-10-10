#include "data/campaign/CampaignProgress.h"
#include "core/io/IniFile.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Campaign progress smoke failure: " << message << '\n';
    }
    return condition;
}

std::uint64_t processId() {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::filesystem::path createUniqueRoot(bool& success) {
    std::error_code error;
    const std::filesystem::path temporaryRoot =
        std::filesystem::temp_directory_path(error);
    if (error) {
        success &= expect(false, "could not locate the temporary directory");
        return {};
    }
    const auto timestamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    for (std::uint64_t attempt = 0; attempt < 64U; ++attempt) {
        const std::filesystem::path candidate = temporaryRoot / (
            "concord-campaign-progress-smoke-"
            + std::to_string(processId()) + '-'
            + std::to_string(timestamp) + '-'
            + std::to_string(attempt)
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

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

bool progressMatches(
    const CampaignProgressData& actual,
    const CampaignProgressData& expected
) {
    return actual.version == expected.version
        && actual.campaignId == expected.campaignId
        && actual.difficulty == expected.difficulty
        && actual.currentMissionId == expected.currentMissionId
        && actual.campaignComplete == expected.campaignComplete
        && actual.completedMissionIds == expected.completedMissionIds
        && actual.attemptsByMission == expected.attemptsByMission;
}

class FailingStreamBuffer final : public std::streambuf {
protected:
    int_type overflow(int_type) override {
        return traits_type::eof();
    }

    std::streamsize xsputn(const char_type*, std::streamsize) override {
        return 0;
    }

    int sync() override {
        return -1;
    }
};

bool noAtomicTemporaryFiles(const std::filesystem::path& directory) {
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        const std::string leaf = iterator->path().filename().string();
        if (leaf.rfind(".concord-atomic-", 0) == 0) {
            return expect(false, "atomic temporary file was not cleaned: " + leaf);
        }
    }
    return expect(!error, "could not inspect atomic temporary cleanup");
}

bool removeKnownPath(const std::filesystem::path& path, bool directory) {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    return expect(
        removed && !error,
        std::string{"could not remove known smoke "}
            + (directory ? "directory" : "file") + ": "
            + path.filename().string()
            + (error ? " (" + error.message() + ')' : "")
    );
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--inspect-skirmish-save") {
        SkirmishSaveInfo info;
        if (!inspectSkirmishSave(argv[2], info)) {
            std::cerr << "Skirmish metadata inspection failed: " << info.error << '\n';
            return 1;
        }
        std::cout << "Skirmish metadata: country=" << info.playerCountry
            << " slots=" << info.players.size() << " valid\n";
        return 0;
    }
    bool success = true;
    const std::filesystem::path root = createUniqueRoot(success);
    if (root.empty()) {
        return 1;
    }
    const std::filesystem::path localDataRoot = root / "nested-local-data";
    const std::filesystem::path progressPath =
        campaignProgressPath(localDataRoot.string());
    const std::filesystem::path encodedSavePath =
        root / "encoded-campaign-save.ini";
    const std::filesystem::path skirmishSave =
        skirmishSavePath(localDataRoot.string());
    const std::filesystem::path legacySkirmishSave =
        root / "legacy-skirmish-save.ini";
    const std::filesystem::path campaignMasqueradingAsSkirmish =
        root / "campaign-mode-skirmish-save.ini";
    const std::filesystem::path observerSkirmishSave =
        root / "observer-skirmish-save.ini";

    CampaignProgressData first;
    first.campaignId = "allied";
    first.difficulty = "Normal";
    first.currentMissionId = "all01umd";
    first.completedMissionIds = {"all01umd"};
    first.attemptsByMission = {{"all01umd", 1}};

    std::string error;
    success &= expect(
        saveCampaignProgress(localDataRoot.string(), first, error),
        "first save or parent creation failed: " + error
    );
    const std::string firstText = readText(progressPath);
    success &= expect(
        firstText.find('\r') == std::string::npos
            && firstText.find("Campaign=allied\n") != std::string::npos,
        "first save was not canonical LF text"
    );

    CampaignProgressData second;
    second.campaignId = "soviet";
    second.difficulty = "Hard";
    second.currentMissionId = "sov02umd";
    second.completedMissionIds = {"sov01umd", "sov02umd"};
    second.attemptsByMission = {{"sov01umd", 2}, {"sov02umd", 3}};
    error.clear();
    success &= expect(
        saveCampaignProgress(localDataRoot.string(), second, error),
        "atomic replacement failed: " + error
    );
    const std::string secondText = readText(progressPath);
    success &= expect(
        secondText.find("Campaign=soviet\n") != std::string::npos
            && secondText.find("Campaign=allied") == std::string::npos
            && secondText.find("Attempts=sov01umd:2,sov02umd:3\n")
                != std::string::npos
            && secondText.find('\r') == std::string::npos,
        "replacement did not publish one complete canonical payload"
    );

    CampaignProgressData loaded;
    error.clear();
    success &= expect(
        loadCampaignProgress(localDataRoot.string(), loaded, error)
            && progressMatches(loaded, second),
        "replacement did not round-trip: " + error
    );

    FailingStreamBuffer failingBuffer;
    std::ostream failingOutput(&failingBuffer);
    std::string writerError;
    success &= expect(
        !writeCampaignProgress(failingOutput, second, writerError)
            && writerError == "campaign progress write failed",
        "stream failure was not observable to the atomic writer callback"
    );

#ifdef _WIN32
    HANDLE lock = CreateFileW(
        progressPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    success &= expect(
        lock != INVALID_HANDLE_VALUE,
        "could not lock campaign progress target"
    );
    if (lock != INVALID_HANDLE_VALUE) {
        CampaignProgressData blocked = second;
        blocked.campaignId = "must-not-publish";
        error.clear();
        const bool blockedSave = saveCampaignProgress(
            localDataRoot.string(),
            blocked,
            error
        );
        CloseHandle(lock);
        success &= expect(
            !blockedSave
                && error.find("replacing") != std::string::npos
                && readText(progressPath) == secondText,
            "replace failure did not preserve the previous progress file"
        );
    }
#endif

    success &= noAtomicTemporaryFiles(localDataRoot);

    const std::string encodedMap = "//server/share/maps/a;b.map";
    const std::string encodedCampaign = "allied;campaign";
    const std::string encodedMission = "all//01";
    {
        std::ofstream output(encodedSavePath, std::ios::binary | std::ios::trunc);
        output << "[RuntimeSave]\n"
               << "Version=13\n"
               << "Map=legacy-value-that-must-not-win\n"
               << "MapHex=" << iniHexEncode(encodedMap) << "\n"
               << "Tick=42\n\n"
               << "[Campaign]\n"
               << "Mode=CampaignMission\n"
               << "Campaign=legacy-campaign\n"
               << "CampaignHex=" << iniHexEncode(encodedCampaign) << "\n"
               << "Mission=legacy-mission\n"
               << "MissionHex=" << iniHexEncode(encodedMission) << "\n"
               << "Difficulty=Hard\n";
    }
    CampaignSaveInfo encodedInfo;
    success &= expect(
        inspectCampaignSave(encodedSavePath.string(), encodedInfo)
            && encodedInfo.mapPath == encodedMap
            && encodedInfo.campaignId == encodedCampaign
            && encodedInfo.missionId == encodedMission
            && encodedInfo.tick == 42,
        "hex-encoded save identity did not preserve comment-like path text"
    );

    {
        std::error_code directoryError;
        std::filesystem::create_directories(
            skirmishSave.parent_path(),
            directoryError
        );
        success &= expect(
            !directoryError,
            "could not create skirmish-save fixture directory"
        );
        std::ofstream output(skirmishSave, std::ios::binary | std::ios::trunc);
        output << "[RuntimeSave]\n"
               << "Version=14\n"
               << "Map=legacy-map-that-must-not-win\n"
               << "MapHex=" << iniHexEncode(encodedMap) << "\n"
               << "Tick=314\n\n"
               << "[SkirmishSession]\n"
               << "Schema=1\n"
               << "GameModeHex=" << iniHexEncode("Meat Grinder") << "\n"
               << "GameModeRulesOverrideHex="
               << iniHexEncode("MPMeatMD.ini") << "\n"
               << "PlayerCountryHex=" << iniHexEncode("Americans") << "\n"
               << "PlayerColorHex=" << iniHexEncode("Gold") << "\n"
               << "FixedTickHz=30\n"
               << "GameSpeed=2\n"
               << "TechLevel=9\n"
               << "StartingCredits=15000\n"
               << "StartingUnits=5\n"
               << "ShortGame=yes\n"
               << "McvRepacks=no\n"
               << "Crates=yes\n"
               << "Superweapons=no\n"
               << "BuildOffAlly=yes\n"
               << "ComputerAI=no\n"
               << "SlotCount=2\n"
               << "0=yes|yes|" << iniHexEncode("Commander") << '|'
               << iniHexEncode("Americans") << '|'
               << iniHexEncode("Gold") << '|'
               << iniHexEncode("A") << '|'
               << iniHexEncode("Medium") << "|1\n"
               << "1=yes|no|" << iniHexEncode("Computer") << '|'
               << iniHexEncode("Russians") << '|'
               << iniHexEncode("Red") << '|'
               << iniHexEncode("B") << '|'
               << iniHexEncode("Hard") << "|2\n\n"
               << "[Economy]\nOwner=SkirmishPlayer1\n";
    }
    SkirmishSaveInfo skirmishInfo;
    success &= expect(
        inspectSkirmishSave(skirmishSave.string(), skirmishInfo)
            && skirmishInfo.valid
            && skirmishInfo.mapPath == encodedMap
            && skirmishInfo.tick == 314
            && skirmishInfo.gameMode == "Meat Grinder"
            && skirmishInfo.gameModeRulesOverride == "MPMeatMD.ini"
            && skirmishInfo.playerCountry == "Americans"
            && skirmishInfo.playerColor == "Gold"
            && skirmishInfo.fixedTickHz == 30
            && skirmishInfo.gameSpeed == 2
            && skirmishInfo.techLevel == 9
            && skirmishInfo.startingCredits == 15000
            && skirmishInfo.startingUnits == 5
            && skirmishInfo.shortGame
            && !skirmishInfo.mcvRepacks
            && skirmishInfo.crates
            && !skirmishInfo.superweapons
            && skirmishInfo.buildOffAlly
            && !skirmishInfo.computerAiEnabled
            && skirmishInfo.players.size() == 2U
            && skirmishInfo.players[0].human
            && skirmishInfo.players[0].name == "Commander"
            && skirmishInfo.players[1].country == "Russians"
            && skirmishInfo.players[1].difficulty == "Hard"
            && listSkirmishSaves(localDataRoot.string()).size() == 1U,
        "modern skirmish startup context did not round-trip"
    );

    {
        const std::string ordinarySave = readText(skirmishSave);
        const auto writeObserverCase = [&](const char* country, bool human) {
            std::string text = ordinarySave;
            const std::string oldHuman = "0=yes|yes|" + iniHexEncode("Commander") + '|'
                + iniHexEncode("Americans") + '|';
            const std::string newHuman = std::string("0=yes|") + (human ? "yes|" : "no|")
                + iniHexEncode("Commander") + '|' + iniHexEncode(country) + '|';
            const std::size_t playerPosition = text.find(oldHuman);
            const std::string oldStart = iniHexEncode("Hard") + "|2\n";
            const std::size_t startPosition = text.find(oldStart);
            if (playerPosition == std::string::npos || startPosition == std::string::npos)
                return false;
            // Change the later field first, retaining the earlier offset.
            text.replace(startPosition, oldStart.size(), iniHexEncode("Hard") + "|1\n");
            text.replace(playerPosition, oldHuman.size(), newHuman);
            std::ofstream output(observerSkirmishSave, std::ios::binary | std::ios::trunc);
            output << text;
            return output.good();
        };
        SkirmishSaveInfo observerInfo;
        success &= expect(writeObserverCase("Observer", true)
                && inspectSkirmishSave(observerSkirmishSave.string(), observerInfo)
                && observerInfo.players[0].country == "Observer"
                && observerInfo.players[0].startPosition == 1
                && observerInfo.players[1].startPosition == 1,
            "legacy observer start reserved the same location as a combatant");
        success &= expect(writeObserverCase(" obs ", true)
                && inspectSkirmishSave(observerSkirmishSave.string(), observerInfo),
            "observer country alias is not recognized in saved metadata");
        success &= expect(writeObserverCase("Americans", true)
                && !inspectSkirmishSave(observerSkirmishSave.string(), observerInfo)
                && observerInfo.error == "skirmish start positions are not unique",
            "duplicate combatant starts were accepted after observer support");
        success &= expect(writeObserverCase("Observer", false)
                && !inspectSkirmishSave(observerSkirmishSave.string(), observerInfo)
                && observerInfo.error == "skirmish start positions are not unique",
            "AI country name bypassed combatant start validation");
    }

    {
        std::string masqueradingText = readText(skirmishSave);
        const std::string original =
            "GameModeHex=" + iniHexEncode("Meat Grinder");
        const std::size_t modePosition = masqueradingText.find(original);
        success &= expect(
            modePosition != std::string::npos,
            "could not prepare campaign-mode skirmish rejection fixture"
        );
        if (modePosition != std::string::npos) {
            masqueradingText.replace(
                modePosition,
                original.size(),
                "GameModeHex=" + iniHexEncode("CampaignMission")
            );
        }
        std::ofstream output(
            campaignMasqueradingAsSkirmish,
            std::ios::binary | std::ios::trunc
        );
        output << masqueradingText;
    }
    SkirmishSaveInfo masqueradingInfo;
    success &= expect(
        !inspectSkirmishSave(
            campaignMasqueradingAsSkirmish.string(),
            masqueradingInfo
        )
            && masqueradingInfo.error.find("campaign mode")
                != std::string::npos,
        "CampaignMission masquerading as a skirmish save was accepted"
    );

    {
        std::ofstream output(
            legacySkirmishSave,
            std::ios::binary | std::ios::trunc
        );
        output << "[RuntimeSave]\n"
               << "Version=14\n"
               << "MapHex=" << iniHexEncode(encodedMap) << "\n"
               << "Tick=12\n\n"
               << "[Economy]\nOwner=SkirmishPlayer1\n";
    }
    SkirmishSaveInfo legacyInfo;
    success &= expect(
        !inspectSkirmishSave(legacySkirmishSave.string(), legacyInfo)
            && legacyInfo.error.find("no main-menu startup context")
                != std::string::npos,
        "legacy skirmish save was silently accepted without startup context"
    );

    success &= removeKnownPath(progressPath, false);
    success &= removeKnownPath(encodedSavePath, false);
    success &= removeKnownPath(skirmishSave, false);
    success &= removeKnownPath(legacySkirmishSave, false);
    success &= removeKnownPath(campaignMasqueradingAsSkirmish, false);
    success &= removeKnownPath(observerSkirmishSave, false);
    success &= removeKnownPath(localDataRoot, true);
    success &= removeKnownPath(root, true);

    if (!success) {
        return 1;
    }
    std::cout << "Campaign progress smoke passed\n";
    return 0;
}
