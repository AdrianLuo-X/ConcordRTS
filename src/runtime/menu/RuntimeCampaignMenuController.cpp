#include "runtime/menu/RuntimeCampaignMenuController.h"
#include <algorithm>
#include <cctype>
#include <utility>
#include <set>

namespace concord::campaign_menu {
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

std::string trimAscii(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

const LoadSaveEntry* selectedSave(const RuntimeCampaignMenuController& state) {
    if (state.saveIndex < 0
        || state.saveIndex >= static_cast<int>(state.saves.size())) {
        return nullptr;
    }
    return &state.saves[static_cast<std::size_t>(state.saveIndex)];
}

const std::vector<std::string>& difficultyNames() {
    static const std::vector<std::string> names{
        "Easy",
        "Normal",
        "Hard",
        "Brutal"
    };
    return names;
}

const CampaignDefinition* selectedCampaign(const RuntimeCampaignMenuController& state) {
    if (state.campaignIndex < 0
        || state.campaignIndex >= static_cast<int>(state.database.campaigns.size())) {
        return nullptr;
    }
    return &state.database.campaigns[static_cast<std::size_t>(state.campaignIndex)];
}

bool missionUnlocked(
    const RuntimeCampaignMenuController& state,
    const CampaignDefinition& campaign,
    const CampaignMissionDefinition& mission
) {
    if (!mission.available) {
        return false;
    }
    if (lowerAscii(state.progress.campaignId) != lowerAscii(campaign.id)) {
        return mission.ordinal == 1;
    }
    if (std::any_of(
            state.progress.completedMissionIds.begin(),
            state.progress.completedMissionIds.end(),
            [&mission](const std::string& completed) {
                return lowerAscii(completed) == lowerAscii(mission.id);
            }
        )) {
        return true;
    }
    if (!state.progress.currentMissionId.empty()) {
        return lowerAscii(state.progress.currentMissionId)
            == lowerAscii(mission.id);
    }
    return mission.ordinal == 1;
}

const CampaignMissionDefinition* selectedMission(const RuntimeCampaignMenuController& state) {
    const CampaignDefinition* campaign = selectedCampaign(state);
    if (campaign == nullptr
        || state.missionIndex < 0
        || state.missionIndex >= static_cast<int>(campaign->missions.size())) {
        return nullptr;
    }
    return &campaign->missions[static_cast<std::size_t>(state.missionIndex)];
}

const CampaignMapSelectionStage* selectedMapSelectionStage(
    const RuntimeCampaignMenuController& state
) {
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    if (campaign == nullptr || mission == nullptr
        || !missionUnlocked(state, *campaign, *mission)) {
        return nullptr;
    }
    for (const std::string& stageId : mission->incomingMapSelectionStages) {
        const CampaignMapSelectionStage* stage =
            state.services.findMapStage(*campaign, stageId);
        if (stage != nullptr) return stage;
    }
    return nullptr;
}

const CampaignMapSelectionStage* currentMapSelectionStage(
    const RuntimeCampaignMenuController& state
) {
    const CampaignDefinition* campaign = selectedCampaign(state);
    if (campaign == nullptr || state.mapPresentation.stageSection.empty()) {
        return nullptr;
    }
    return state.services.findMapStage(*campaign,
        state.mapPresentation.stageSection
    );
}

int missionIndexById(
    const CampaignDefinition& campaign,
    const std::string& missionId
) {
    for (std::size_t index = 0; index < campaign.missions.size(); ++index) {
        if (lowerAscii(campaign.missions[index].id)
            == lowerAscii(trimAscii(missionId))) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

const CampaignMapSelectionChoice* mapChoiceByColor(
    const CampaignMapSelectionStage* stage,
    int colorIndex
) {
    if (stage == nullptr || colorIndex <= 0 || colorIndex > 255) {
        return nullptr;
    }
    for (const CampaignMapSelectionChoice& choice : stage->choices) {
        if (choice.colorIndex == colorIndex) return &choice;
    }
    return nullptr;
}

bool isSovietCampaign(const CampaignDefinition* campaign) {
    if (campaign == nullptr) return false;
    const std::string identity = lowerAscii(
        campaign->id + " " + campaign->side + " " + campaign->sourceSection
    );
    return identity.find("nod") != std::string::npos
        || identity.find("sov") != std::string::npos;
}

bool isTutorialCampaign(const CampaignDefinition* campaign) {
    if (campaign == nullptr) return false;
    const std::string identity = lowerAscii(
        campaign->battleSection + " " + campaign->id + " "
            + campaign->sourceSection
    );
    return identity.find("tut") != std::string::npos
        || identity.find("trn") != std::string::npos;
}

bool campaignSourceIs(
    const CampaignDefinition* campaign,
    const std::string& sourceId
) {
    if (campaign == nullptr) return false;
    const std::string wanted = lowerAscii(trimAscii(sourceId));
    const std::string actual = lowerAscii(trimAscii(campaign->sourceId));
    if (!actual.empty()) return actual == wanted;

    // Keep custom/older campaign fixtures usable while sourceId is absent.
    const std::string identity = lowerAscii(
        campaign->id + " " + campaign->edition + " "
            + campaign->textCatalogPath
    );
    if (wanted == "ra2") {
        return identity.rfind("ra2-", 0) == 0
            || identity.find("red alert 2") != std::string::npos
            || (identity.find("ra2.csf") != std::string::npos
                && identity.find("ra2md.csf") == std::string::npos);
    }
    if (wanted == "yr") {
        return identity.find("yuri") != std::string::npos
            || identity.find("ra2md.csf") != std::string::npos;
    }
    return false;
}

void chooseFirstUnlockedMission(RuntimeCampaignMenuController& state) {
    const CampaignDefinition* campaign = selectedCampaign(state);
    if (campaign == nullptr) {
        state.missionIndex = 0;
        return;
    }
    state.missionIndex = 0;
    const bool sameCampaign = lowerAscii(state.progress.campaignId)
        == lowerAscii(campaign->id);
    if (sameCampaign && !state.progress.currentMissionId.empty()) {
        const int current = missionIndexById(
            *campaign,
            state.progress.currentMissionId
        );
        if (current >= 0
            && missionUnlocked(
                state,
                *campaign,
                campaign->missions[static_cast<std::size_t>(current)]
            )) {
            state.missionIndex = current;
            return;
        }
    }
    if (sameCampaign) {
        for (std::size_t reverse = campaign->missions.size(); reverse > 0;
            --reverse) {
            const CampaignMissionDefinition& mission =
                campaign->missions[reverse - 1];
            const bool completed = std::any_of(
                state.progress.completedMissionIds.begin(),
                state.progress.completedMissionIds.end(),
                [&mission](const std::string& completedId) {
                    return lowerAscii(completedId) == lowerAscii(mission.id);
                }
            );
            if (completed && mission.available) {
                state.missionIndex = static_cast<int>(reverse - 1);
                return;
            }
        }
    }
    for (std::size_t index = 0; index < campaign->missions.size(); ++index) {
        if (missionUnlocked(state, *campaign, campaign->missions[index])) {
            state.missionIndex = static_cast<int>(index);
            return;
        }
    }
}

void appendMapPresentationDiagnostic(
    CampaignMapPresentationState& presentation,
    const std::string& message
) {
    if (trimAscii(message).empty()) return;
    if (!presentation.diagnostic.empty()) presentation.diagnostic += "; ";
    presentation.diagnostic += message;
}

int firstUsableMapChoiceColor(
    const RuntimeCampaignMenuController& state,
    const CampaignMapSelectionStage& stage
) {
    const CampaignDefinition* campaign = selectedCampaign(state);
    if (campaign == nullptr) return 0;
    for (const CampaignMapSelectionChoice& choice : stage.choices) {
        const int missionIndex = missionIndexById(
            *campaign,
            choice.targetMissionId
        );
        if (choice.colorIndex > 0 && choice.colorIndex <= 255
            && missionIndex >= 0
            && campaign->missions[static_cast<std::size_t>(missionIndex)]
                .available) {
            return choice.colorIndex;
        }
    }
    return 0;
}

std::wstring resolvedCampaignName(
    const RuntimeCampaignMenuController& state,
    const CampaignDefinition* campaign
) {
    if (campaign == nullptr) return L"";
    std::wstring name = state.services.text(campaign->displayNameKey);
    if (name.empty()) {
        name = state.services.text(
            isTutorialCampaign(campaign)
                ? "DESC:TUT1"
                : (isSovietCampaign(campaign) ? "DESC:SOV1" : "DESC:ALL1")
        );
    }
    if (name.empty()) {
        if (!campaign->displayName.empty()) {
            name = state.services.widen(campaign->displayName);
        } else {
            if (isTutorialCampaign(campaign)) {
                name = L"训练战役";
            } else {
                name = isSovietCampaign(campaign) ? L"苏军战役" : L"盟军战役";
            }
        }
    }
    const std::wstring edition = state.services.editionSuffix(campaign);
    if (!edition.empty() && name.find(edition) == std::wstring::npos) {
        name += edition;
    }
    return name;
}

std::wstring campaignPageTitle(const RuntimeCampaignMenuController& state) {
    switch (state.page) {
    case CampaignPage::Campaigns:
        return state.services.text("GUI:NewCampaign", L"新战役");
    case CampaignPage::Missions:
        return state.services.text("GUI:SelectMission", L"选择任务");
    case CampaignPage::Difficulty:
        return state.services.text("GUI:GameDifficulty", L"难度");
    case CampaignPage::Briefing:
        return state.services.text("GUI:Briefing", L"任务简报");
    case CampaignPage::Saves:
        return state.services.text("GUI:LoadSavedGame", L"载入游戏");
    }
    return L"战役";
}

std::wstring difficultyDisplayName(
    const RuntimeCampaignMenuController& state,
    int index
) {
    static const std::vector<std::pair<const char*, const wchar_t*>> labels{
        {"GUI:Easy", L"简单"},
        {"GUI:Normal", L"普通"},
        {"GUI:Hard", L"困难"},
        {"GUI:Brutal", L"残酷"}
    };
    index = std::clamp(index, 0, static_cast<int>(labels.size()) - 1);
    return state.services.text(
        labels[static_cast<std::size_t>(index)].first,
        labels[static_cast<std::size_t>(index)].second
    );
}

void closeMenu(RuntimeCampaignMenuController& state, bool accepted) {
    if (state.closing) {
        return;
    }
    state.closing = true;
    state.accepted = accepted;
    state.services.releaseMapCapture();
    state.services.stopVoice();
    state.services.stopMapEffect();
    state.services.destroyWindow();
}

void fillSelectionFromMission(
    RuntimeCampaignMenuController& state,
    const CampaignMissionDefinition& mission,
    bool loadedFromSave,
    const std::string& savePath,
    const std::string& difficulty
) {
    state.selection = RuntimeCampaignSelection{};
    state.selection.accepted = true;
    state.selection.loadedFromSave = loadedFromSave;
    state.selection.savePath = savePath.empty()
        ? state.services.savePath(
            state.config.localDataRootPath,
            mission.campaignId,
            mission.id
        )
        : savePath;
    state.selection.mission = mission;
    state.selection.campaignId = mission.campaignId;
    state.selection.missionId = mission.id;
    state.selection.sourceId = mission.sourceId;
    state.selection.difficulty = difficulty.empty() ? "Normal" : difficulty;
    state.selection.playerHouse = mission.playerHouse.empty()
        ? mission.playerCountry
        : mission.playerHouse;
    state.selection.nextMissionId = mission.nextMissionId;
    state.selection.introVideoPath = mission.introVideoPath;
    state.selection.postScoreVideoPath = mission.postScoreVideoPath;
    state.selection.briefingKey = mission.briefingKey;
    state.selection.mapPath = mission.mapPath;
    state.selection.playerCountry = mission.playerCountry.empty()
        ? (state.selection.playerHouse.empty() ? "Americans" : state.selection.playerHouse)
        : mission.playerCountry;
    state.selection.playerCountryDisplayName = state.services.missionLabel(mission);
    state.selection.playerCountryBrief = state.services.text(
        mission.briefingKey,
        state.services.text(mission.loadBriefingKey, state.services.widen(mission.id))
    );
    state.selection.loadingScreenAssetId = state.services.loadingScreenAsset(
        state.selection.playerCountry
    );
}

void fillSelectionFromSkirmishSave(
    RuntimeCampaignMenuController& state,
    const SkirmishSaveInfo& save
) {
    state.selection = RuntimeCampaignSelection{};
    state.selection.accepted = true;
    state.selection.loadedFromSave = true;
    state.selection.loadedSkirmishSave = true;
    state.selection.loadedSkirmishFixedTickHz = save.fixedTickHz;
    state.selection.loadedSkirmishTechLevel = save.techLevel;
    state.selection.savePath = save.path;

    RuntimeSkirmishSelection& selection = state.selection.skirmish;
    selection.accepted = true;
    selection.mapPath = save.mapPath;
    selection.gameMode = save.gameMode;
    selection.gameModeRulesOverride = save.gameModeRulesOverride;
    selection.playerCountry = save.playerCountry;
    selection.playerColor = save.playerColor;
    selection.playerCountryDisplayName = state.services.widen(save.playerCountry);
    selection.loadingScreenAssetId = state.services.loadingScreenAsset(
        save.playerCountry
    );
    selection.startingCredits = save.startingCredits;
    selection.unitCount = save.startingUnits;
    selection.gameSpeed = save.gameSpeed;
    selection.shortGame = save.shortGame;
    selection.mcvRepacks = save.mcvRepacks;
    selection.crates = save.crates;
    selection.superweapons = save.superweapons;
    selection.buildOffAlly = save.buildOffAlly;
    selection.computerAiEnabled = save.computerAiEnabled;
    selection.noCooldown = save.noCooldown;
    selection.slots.reserve(save.players.size());
    for (const SkirmishSavePlayerInfo& savedPlayer : save.players) {
        RuntimeSkirmishPlayerSlot player;
        player.enabled = savedPlayer.enabled;
        player.human = savedPlayer.human;
        player.name = savedPlayer.name;
        player.country = savedPlayer.country;
        player.color = savedPlayer.color;
        player.team = savedPlayer.team;
        player.difficulty = savedPlayer.difficulty;
        player.startPosition = savedPlayer.startPosition;
        selection.slots.push_back(std::move(player));
    }
}

void acceptMission(RuntimeCampaignMenuController& state) {
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    if (campaign == nullptr || mission == nullptr
        || !missionUnlocked(state, *campaign, *mission)) {
        state.status = L"Mission is locked or its authored map is unavailable.";
        return;
    }

    const std::string difficulty = difficultyNames()[
        static_cast<std::size_t>(std::clamp(state.difficultyIndex, 0, 3))
    ];
    fillSelectionFromMission(state, *mission, false, "", difficulty);
    CampaignProgressData progress = state.progress;
    progress.campaignId = campaign->id;
    progress.difficulty = difficulty;
    progress.currentMissionId = mission->id;
    std::string error;
    if (!state.services.saveProgress(state.config.localDataRootPath, progress, error)) {
        state.status = L"Progress could not be saved: " + state.services.widen(error);
        return;
    }
    closeMenu(state, true);
}

void acceptSave(RuntimeCampaignMenuController& state) {
    const LoadSaveEntry* entry = selectedSave(state);
    if (entry == nullptr) {
        state.status = L"Select a valid saved game.";
        return;
    }
    if (entry->kind == LoadSaveKind::Skirmish) {
        std::string pathError;
        if (!state.services.validateSavePaths(
                state.config,
                entry->skirmish,
                pathError
            )) {
            state.status = L"遭遇战存档无法载入：" + state.services.widen(pathError);
            return;
        }
        fillSelectionFromSkirmishSave(state, entry->skirmish);
        closeMenu(state, true);
        return;
    }

    const CampaignSaveInfo& save = entry->campaign;
    const CampaignMissionDefinition* mission = state.services.findMission(
        save.campaignId,
        save.missionId
    );
    if (mission == nullptr || !mission->available) {
        state.status = L"Save refers to a mission missing from the mounted resources.";
        return;
    }
    fillSelectionFromMission(
        state,
        *mission,
        true,
        save.path,
        save.difficulty
    );
    closeMenu(state, true);
}

bool confirmCampaignMapChoice(RuntimeCampaignMenuController& state, int colorIndex) {
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    const CampaignMapSelectionChoice* choice = mapChoiceByColor(
        stage,
        colorIndex
    );
    if (campaign == nullptr || stage == nullptr || choice == nullptr) {
        return false;
    }
    const int targetIndex = missionIndexById(
        *campaign,
        choice->targetMissionId
    );
    if (targetIndex < 0) {
        state.status = L"地图选择目标没有对应的可运行任务。";
        return false;
    }
    const CampaignMissionDefinition& target =
        campaign->missions[static_cast<std::size_t>(targetIndex)];
    if (!target.available) {
        state.status = L"该地图目标的任务资源不可用。";
        return false;
    }
    state.services.stopVoice();
    state.missionIndex = targetIndex;
    state.page = CampaignPage::Difficulty;
    state.status.clear();
    state.services.refreshBackground();
    state.services.refreshPresentation();
    state.services.playMapSound(campaign->mapSelectionSounds.clickRegion);
    return true;
}

void skipCampaignMapIntro(RuntimeCampaignMenuController& state) {
    CampaignMapPresentationState& presentation = state.mapPresentation;
    if (presentation.phase != CampaignMapPresentationPhase::VqaIntro
        && presentation.phase != CampaignMapPresentationPhase::OverlayFade) {
        return;
    }
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    const CampaignDefinition* campaign = selectedCampaign(state);
    presentation.vqa.clear();
    if (presentation.hasClickMap() && stage != nullptr) {
        presentation.keyboardColor = firstUsableMapChoiceColor(state, *stage);
        if (presentation.keyboardColor == 0) {
            presentation.phase = CampaignMapPresentationPhase::FallbackList;
            presentation.phaseStartedAt = state.presentationClock->nowMs();
            appendMapPresentationDiagnostic(
                presentation,
                "stage has no available map choice"
            );
            return;
        }
        presentation.phase = CampaignMapPresentationPhase::Interactive;
        if (!presentation.targetSoundPlayed && campaign != nullptr) {
            state.services.playMapSound(campaign->mapSelectionSounds.targetFlyIn
            );
            presentation.targetSoundPlayed = true;
        }
        presentation.phaseStartedAt = state.presentationClock->nowMs();
    } else {
        presentation.phase = CampaignMapPresentationPhase::FallbackList;
        presentation.phaseStartedAt = state.presentationClock->nowMs();
    }
}

void cycleCampaignMapChoice(RuntimeCampaignMenuController& state, int delta) {
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    const CampaignDefinition* campaign = selectedCampaign(state);
    if (stage == nullptr || campaign == nullptr) return;
    std::vector<int> colors;
    for (const CampaignMapSelectionChoice& choice : stage->choices) {
        const int targetIndex = missionIndexById(
            *campaign,
            choice.targetMissionId
        );
        if (choice.colorIndex <= 0 || choice.colorIndex > 255
            || targetIndex < 0) {
            continue;
        }
        const CampaignMissionDefinition& target =
            campaign->missions[static_cast<std::size_t>(targetIndex)];
        if (target.available) {
            colors.push_back(choice.colorIndex);
        }
    }
    if (colors.empty()) return;
    const int currentColor = state.mapPresentation.keyboardColor != 0
        ? state.mapPresentation.keyboardColor
        : state.mapPresentation.hoverColor;
    auto current = std::find(colors.begin(), colors.end(), currentColor);
    int index = current == colors.end()
        ? (delta > 0 ? -1 : 0)
        : static_cast<int>(std::distance(colors.begin(), current));
    index = (index + delta + static_cast<int>(colors.size()))
        % static_cast<int>(colors.size());
    state.mapPresentation.keyboardColor =
        colors[static_cast<std::size_t>(index)];
    state.mapPresentation.hoverColor = 0;
}

void updateCampaignMapHover(
    RuntimeCampaignMenuController& state,
    int baseX,
    int baseY
) {
    if (state.page != CampaignPage::Missions
        || state.mapPresentation.phase
            != CampaignMapPresentationPhase::Interactive) {
        return;
    }
    const int previous = state.mapPresentation.hoverColor;
    const int current = state.services.mapColorAt(baseX, baseY);
    if (current == previous) return;
    state.mapPresentation.hoverColor = current;
    if (current != 0) state.mapPresentation.keyboardColor = current;
    const CampaignDefinition* campaign = selectedCampaign(state);
    if (campaign == nullptr) return;
    if (current != 0) {
        state.services.playMapSound(campaign->mapSelectionSounds.enterRegion
        );
    } else if (previous != 0) {
        state.services.playMapSound(campaign->mapSelectionSounds.exitRegion
        );
    }
}

void moveSelection(RuntimeCampaignMenuController& state, int delta) {
    if (state.page == CampaignPage::Campaigns) {
        if (state.database.campaigns.empty()) return;
        const int count = static_cast<int>(state.database.campaigns.size());
        state.campaignIndex = (state.campaignIndex + delta + count) % count;
        chooseFirstUnlockedMission(state);
    } else if (state.page == CampaignPage::Missions) {
        if (campaignShowsMap(state) && state.mapPresentation.phase
            == CampaignMapPresentationPhase::Interactive) {
            cycleCampaignMapChoice(state, delta);
            state.services.repaintValidWindow();
            return;
        }
        if (campaignShowsMap(state) && (state.mapPresentation.phase
                == CampaignMapPresentationPhase::VqaIntro
            || state.mapPresentation.phase
                == CampaignMapPresentationPhase::OverlayFade)) {
            return;
        }
        const CampaignDefinition* campaign = selectedCampaign(state);
        if (campaign == nullptr || campaign->missions.empty()) return;
        const int count = static_cast<int>(campaign->missions.size());
        state.missionIndex = (state.missionIndex + delta + count) % count;
    } else if (state.page == CampaignPage::Difficulty) {
        const int count = static_cast<int>(difficultyNames().size());
        state.difficultyIndex = (state.difficultyIndex + delta + count) % count;
    } else if (state.page == CampaignPage::Briefing) {
        state.briefingScroll = std::max(0, state.briefingScroll + delta * 40);
    } else if (state.page == CampaignPage::Saves) {
        if (state.saves.empty()) return;
        const int count = static_cast<int>(state.saves.size());
        state.saveIndex = (state.saveIndex + delta + count) % count;
    }
    state.services.refreshBackground();
    state.services.refreshPresentation();
}

void activateSelection(RuntimeCampaignMenuController& state) {
    switch (state.page) {
    case CampaignPage::Campaigns:
        if (selectedCampaign(state) != nullptr) {
            chooseFirstUnlockedMission(state);
            state.page = CampaignPage::Missions;
        }
        break;
    case CampaignPage::Missions:
        if (campaignShowsMap(state) && (state.mapPresentation.phase
                == CampaignMapPresentationPhase::VqaIntro
            || state.mapPresentation.phase
                == CampaignMapPresentationPhase::OverlayFade)) {
            skipCampaignMapIntro(state);
        } else if (campaignShowsMap(state) && state.mapPresentation.phase
            == CampaignMapPresentationPhase::Interactive) {
            int color = state.mapPresentation.keyboardColor;
            if (color == 0) {
                const CampaignMapSelectionStage* stage =
                    currentMapSelectionStage(state);
                if (stage != nullptr) {
                    color = firstUsableMapChoiceColor(state, *stage);
                }
            }
            confirmCampaignMapChoice(state, color);
        } else if (const CampaignMissionDefinition* mission =
                selectedMission(state)) {
            const CampaignDefinition* campaign = selectedCampaign(state);
            if (campaign != nullptr
                && missionUnlocked(state, *campaign, *mission)) {
                state.page = CampaignPage::Difficulty;
                state.status.clear();
            } else {
                state.status = L"任务尚未解锁或地图资源不可用。";
            }
        }
        break;
    case CampaignPage::Difficulty:
        state.briefingScroll = 0;
        state.page = CampaignPage::Briefing;
        break;
    case CampaignPage::Briefing:
        acceptMission(state);
        break;
    case CampaignPage::Saves:
        acceptSave(state);
        break;
    }
    state.services.refreshBackground();
    state.services.refreshPresentation();
    state.services.repaintValidWindow();
}

void goBack(RuntimeCampaignMenuController& state) {
    switch (state.page) {
    case CampaignPage::Campaigns:
    case CampaignPage::Saves:
        closeMenu(state, false);
        break;
    case CampaignPage::Missions:
        state.page = CampaignPage::Campaigns;
        break;
    case CampaignPage::Difficulty:
        state.page = CampaignPage::Missions;
        break;
    case CampaignPage::Briefing:
        state.page = CampaignPage::Difficulty;
        break;
    }
    state.services.refreshBackground();
    state.services.refreshPresentation();
    state.services.repaintValidWindow();
}

bool campaignShowsMap(const RuntimeCampaignMenuController& state) {
    return state.page == CampaignPage::Missions
        && state.mapPresentation.usesClassicSurface()
        && !state.mapPresentation.authoredReconstruction
        && state.mapPresentation.vqaFrame.valid();
}
bool campaignPointInRect(RuntimeInputPixelRect rect, RuntimeInputPixelPoint point) noexcept {
    return point.x >= rect.left && point.x < rect.right
        && point.y >= rect.top && point.y < rect.bottom;
}
RuntimeInputPixelRect campaignShellClientRect(const RuntimeInputPixelRect& client) {
    const int width = std::max<std::int32_t>(1, client.right - client.left);
    const int height = std::max<std::int32_t>(1, client.bottom - client.top);
    const double scale = std::min(width / 800.0, height / 600.0);
    const int fittedWidth = std::max(1, static_cast<int>(800 * scale));
    const int fittedHeight = std::max(1, static_cast<int>(600 * scale));
    const int left = client.left + (width - fittedWidth) / 2;
    const int top = client.top + (height - fittedHeight) / 2;
    return RuntimeInputPixelRect{left, top, left + fittedWidth, top + fittedHeight};
}

RuntimeInputPixelPoint campaignShellPoint(const RuntimeInputPixelRect& client, int x, int y) {
    const RuntimeInputPixelRect fit = campaignShellClientRect(client);
    if (x < fit.left || x >= fit.right || y < fit.top || y >= fit.bottom) {
        return RuntimeInputPixelPoint{-1, -1};
    }
    return RuntimeInputPixelPoint{(x - fit.left) * 800 / std::max<std::int32_t>(1, fit.right - fit.left),
                 (y - fit.top) * 600 / std::max<std::int32_t>(1, fit.bottom - fit.top)};
}

RuntimeInputPixelPoint campaignMapPoint(RuntimeInputPixelPoint shell) {
    if (!campaignPointInRect(kCampaignMapViewport, shell)) return RuntimeInputPixelPoint{-1, -1};
    return RuntimeInputPixelPoint{(shell.x - kCampaignMapViewport.left) * 640
                     / (kCampaignMapViewport.right - kCampaignMapViewport.left),
                 (shell.y - kCampaignMapViewport.top) * 480
                     / (kCampaignMapViewport.bottom - kCampaignMapViewport.top)};
}

int campaignListStart(int selected, int count) {
    return std::clamp(selected - 4, 0, std::max(0, count - 9));
}

std::vector<CampaignControl> campaignControls(const RuntimeCampaignMenuController& state) {
    using Action = CampaignControlAction;
    std::vector<CampaignControl> controls;
    controls.push_back({kCampaignBackButton, Action::Back, 0, L"返回"});
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    const auto button = [&](int slot, Action action, int index,
                            const std::wstring& label, bool enabled = true,
                            bool selected = false) {
        controls.push_back({RuntimeInputPixelRect{653, 207 + slot * 42, 779, 233 + slot * 42},
                            action, index, label, enabled, selected});
    };
    bool canContinue = campaign != nullptr;
    if (state.page == CampaignPage::Saves) canContinue = !state.saves.empty();
    else if (state.page != CampaignPage::Campaigns && !campaignShowsMap(state)) {
        canContinue = campaign && mission && missionUnlocked(state, *campaign, *mission);
    }
    button(state.page == CampaignPage::Campaigns ? 6 : 0, Action::Confirm, 0,
        state.page == CampaignPage::Briefing ? L"开始任务"
        : state.page == CampaignPage::Saves ? L"载入"
        : campaignShowsMap(state)
            && state.mapPresentation.phase != CampaignMapPresentationPhase::Interactive
            ? L"跳过演出" : L"继续", canContinue);

    if (state.page == CampaignPage::Campaigns) {
        std::set<std::string> sources;
        int sourceSlot = 0;
        int iconSlot = 0;
        for (int index = 0; index < static_cast<int>(state.database.campaigns.size()); ++index) {
            const auto& candidate = state.database.campaigns[static_cast<std::size_t>(index)];
            if (!isTutorialCampaign(&candidate) && sources.insert(lowerAscii(candidate.sourceId)).second) {
                button(sourceSlot++, Action::Edition, index,
                    campaignSourceIs(&candidate, "yr") ? L"尤里的复仇"
                    : campaignSourceIs(&candidate, "ra2") ? L"红色警戒 2" : state.services.widen(candidate.sourceId),
                    true, campaign && lowerAscii(campaign->sourceId) == lowerAscii(candidate.sourceId));
            }
            if (!campaign || lowerAscii(candidate.sourceId) != lowerAscii(campaign->sourceId)) continue;
            if (isTutorialCampaign(&candidate)) {
                button(3, Action::Campaign, index, L"新兵训练");
            } else {
                const int top = 51 + iconSlot++ * 286;
                controls.push_back({RuntimeInputPixelRect{187, top, 448, top + 130}, Action::Campaign,
                    index, resolvedCampaignName(state, &candidate), true, index == state.campaignIndex});
            }
        }
    } else if (state.page == CampaignPage::Missions && !campaignShowsMap(state) && campaign) {
        const int count = static_cast<int>(campaign->missions.size());
        const int start = campaignListStart(state.missionIndex, count);
        for (int row = 0; row < 9 && start + row < count; ++row) {
            const int index = start + row;
            const auto& item = campaign->missions[static_cast<std::size_t>(index)];
            const bool unlocked = missionUnlocked(state, *campaign, item);
            controls.push_back({RuntimeInputPixelRect{42, 66 + row * 34, 590, 98 + row * 34}, Action::Row,
                index, std::to_wstring(item.ordinal) + L". " + state.services.missionLabel(item)
                    + (unlocked ? L"" : L"  [未解锁]"), item.available, index == state.missionIndex});
        }
        button(1, Action::Previous, 0, L"上一任务", count > 1);
        button(2, Action::Next, 0, L"下一任务", count > 1);
    } else if (state.page == CampaignPage::Saves) {
        const int count = static_cast<int>(state.saves.size());
        const int start = campaignListStart(state.saveIndex, count);
        for (int row = 0; row < 9 && start + row < count; ++row) {
            const int index = start + row;
            controls.push_back({RuntimeInputPixelRect{42, 66 + row * 34, 590, 98 + row * 34}, Action::Row,
                index, state.services.saveTitle(state.saves[static_cast<std::size_t>(index)]), true,
                index == state.saveIndex});
        }
        button(1, Action::Previous, 0, L"上一存档", count > 1);
        button(2, Action::Next, 0, L"下一存档", count > 1);
    } else if (state.page == CampaignPage::Briefing) {
        button(1, Action::Previous, 0, L"向上滚动");
        button(2, Action::Next, 0, L"向下滚动");
    }
    if (state.page == CampaignPage::Campaigns || state.page == CampaignPage::Difficulty) {
        for (int index = 0; index < static_cast<int>(difficultyNames().size()); ++index) {
            const int x = 178 + index * 71;
            controls.push_back({RuntimeInputPixelRect{x, 267, x + 69, 295}, Action::Difficulty, index,
                difficultyDisplayName(state, index), true, index == state.difficultyIndex});
        }
        // Four contiguous slider regions share the same actions as the labels.
        for (int index = 0; index < 4; ++index) {
            const std::int32_t left = kCampaignSlider.left + index * 54;
            controls.push_back({RuntimeInputPixelRect{left, kCampaignSlider.top, left + 54, kCampaignSlider.bottom},
                Action::Difficulty, index, L"", true, index == state.difficultyIndex});
        }
    }
    return controls;
}

int campaignControlAt(const RuntimeCampaignMenuController& state, RuntimeInputPixelPoint point) {
    const auto controls = campaignControls(state);
    for (int index = 0; index < static_cast<int>(controls.size()); ++index) {
        if (controls[static_cast<std::size_t>(index)].enabled
            && campaignPointInRect(controls[static_cast<std::size_t>(index)].rect, point)) return index;
    }
    return -1;
}


void resetCampaignMenuInput(RuntimeCampaignMenuController& state) {
    const bool highlighted = state.pressedControl >= 0
        || state.hoveredControl >= 0 || state.focusedControl >= 0;
    state.pressedControl = -1;
    state.hoveredControl = -1;
    state.focusedControl = -1;
    state.services.releaseMapCapture();
    if (highlighted) state.services.repaintValidWindow();
}
void activateCampaignControl(RuntimeCampaignMenuController& state, int index) {
    const auto controls = campaignControls(state);
    if (index < 0 || index >= static_cast<int>(controls.size())) return;
    const CampaignControl control = controls[static_cast<std::size_t>(index)];
    if (!control.enabled) return;
    resetCampaignMenuInput(state);
    using Action = CampaignControlAction;
    switch (control.action) {
    case Action::Back: goBack(state); break;
    case Action::Confirm: activateSelection(state); break;
    case Action::Edition:
    case Action::Campaign:
        state.campaignIndex = control.index;
        chooseFirstUnlockedMission(state);
        if (control.action == Action::Campaign) activateSelection(state);
        break;
    case Action::Difficulty: state.difficultyIndex = control.index; break;
    case Action::Previous: moveSelection(state, -1); break;
    case Action::Next: moveSelection(state, 1); break;
    case Action::Row:
        if (state.page == CampaignPage::Saves) state.saveIndex = control.index;
        else state.missionIndex = control.index;
        break;
    }
    if (!state.closing) {
        state.services.refreshBackground();
        state.services.refreshPresentation();
    }
}

bool campaignKeyDown(RuntimeCampaignMenuController& state, CampaignMenuKey key, bool repeated) {
    if (key == CampaignMenuKey::Escape) {
        if (!repeated) { resetCampaignMenuInput(state); goBack(state); }
    } else if (key == CampaignMenuKey::Tab) {
        const auto controls = campaignControls(state);
        const int direction = state.services.shiftPressed() ? -1 : 1;
        for (std::size_t tries = 0; tries < controls.size(); ++tries) {
            state.focusedControl = (state.focusedControl + direction
                + static_cast<int>(controls.size())) % static_cast<int>(controls.size());
            const auto& control = controls[static_cast<std::size_t>(state.focusedControl)];
            if (control.enabled && !control.label.empty()) break;
        }
    } else if (key == CampaignMenuKey::Up || key == CampaignMenuKey::Down) {
        resetCampaignMenuInput(state);
        moveSelection(state, key == CampaignMenuKey::Up ? -1 : 1);
    } else if ((key == CampaignMenuKey::Left || key == CampaignMenuKey::Right)
        && (state.page == CampaignPage::Campaigns || state.page == CampaignPage::Difficulty)) {
        resetCampaignMenuInput(state);
        state.difficultyIndex = std::clamp(state.difficultyIndex
            + (key == CampaignMenuKey::Left ? -1 : 1), 0, static_cast<int>(difficultyNames().size()) - 1);
    } else if (key == CampaignMenuKey::Confirm) {
        if (!repeated) {
            if (state.focusedControl >= 0) activateCampaignControl(state, state.focusedControl);
            else { resetCampaignMenuInput(state); activateSelection(state); }
        }
    } else return false;
    state.services.repaintEvent(true);
    return true;
}
void campaignWheel(RuntimeCampaignMenuController& state, bool positive) {
    resetCampaignMenuInput(state);
    moveSelection(state, positive ? -1 : 1);
    state.services.repaintEvent(false);
}
void campaignMouseMove(RuntimeCampaignMenuController& state, RuntimeInputPixelPoint clientPoint) {
    state.services.trackMouseLeave();
    const auto point = campaignShellPoint(state.services.eventClientRect(), clientPoint.x, clientPoint.y);
    const int previousControl = state.hoveredControl;
    const int previousMapColor = state.mapPresentation.hoverColor;
    state.hoveredControl = campaignControlAt(state, point);
    if (campaignShowsMap(state)) {
        const auto map = campaignMapPoint(point);
        updateCampaignMapHover(state, map.x, map.y);
    }
    state.services.setHandCursor(state.hoveredControl >= 0
        || (campaignShowsMap(state) && state.mapPresentation.hoverColor != 0));
    if (previousControl != state.hoveredControl || previousMapColor != state.mapPresentation.hoverColor) {
        state.services.repaintEvent(false);
    }
}
void campaignMouseLeave(RuntimeCampaignMenuController& state) {
    const bool highlighted = state.hoveredControl >= 0 || state.mapPresentation.hoverColor != 0;
    state.hoveredControl = -1;
    updateCampaignMapHover(state, -1, -1);
    if (highlighted) state.services.repaintEvent(false);
}
void campaignMouseDown(RuntimeCampaignMenuController& state, RuntimeInputPixelPoint clientPoint) {
    state.services.focusEvent();
    state.focusedControl = -1;
    const auto point = campaignShellPoint(state.services.eventClientRect(), clientPoint.x, clientPoint.y);
    state.pressedControl = campaignControlAt(state, point);
    const auto map = campaignMapPoint(point);
    state.mapPresentation.pressedColor = campaignShowsMap(state)
        ? state.services.mapColorAt(map.x, map.y) : 0;
    if (state.pressedControl >= 0 || state.mapPresentation.pressedColor != 0) state.services.captureEvent();
    state.services.repaintEvent(false);
}
void campaignMouseUp(RuntimeCampaignMenuController& state, RuntimeInputPixelPoint clientPoint) {
    const auto point = campaignShellPoint(state.services.eventClientRect(), clientPoint.x, clientPoint.y);
    const int pressed = state.pressedControl;
    const int color = state.mapPresentation.pressedColor;
    state.pressedControl = -1;
    state.mapPresentation.pressedColor = 0;
    state.services.releaseEventCapture();
    if (pressed >= 0 && campaignControlAt(state, point) == pressed) {
        activateCampaignControl(state, pressed);
    } else if (pressed < 0 && color != 0 && campaignShowsMap(state)) {
        const auto map = campaignMapPoint(point);
        if (state.services.mapColorAt(map.x, map.y) == color) {
            resetCampaignMenuInput(state);
            confirmCampaignMapChoice(state, color);
        }
    }
    state.services.repaintEvent(true);
}
void campaignCaptureChanged(RuntimeCampaignMenuController& state) {
    const bool pressed = state.pressedControl >= 0;
    state.pressedControl = -1;
    state.mapPresentation.pressedColor = 0;
    if (pressed) state.services.repaintEvent(false);
}
void campaignSize(RuntimeCampaignMenuController& state) {
    resetCampaignMenuInput(state);
    state.services.repaintEvent(false);
}
void campaignTimer(RuntimeCampaignMenuController& state, bool timerOne) {
    if (!timerOne || state.page != CampaignPage::Missions) return;
    const bool animatedBefore = campaignShowsMap(state);
    state.services.updatePresentation();
    if (animatedBefore || campaignShowsMap(state)) state.services.repaintEvent(false);
}
void campaignDestroy(RuntimeCampaignMenuController& state) {
    resetCampaignMenuInput(state);
    state.services.stopVoice();
    state.services.stopMapEffect();
}

void initializeCampaignMenu(RuntimeCampaignMenuController& state, const RuntimeConfig& config, bool loadGame) {
    state.config = config;
    state.loadGame = loadGame;
    state.page = loadGame ? CampaignPage::Saves : CampaignPage::Campaigns;

    CampaignDatabaseOptions options;
    options.resourceRootPath = config.resourceRootPath;
    options.localDataRootPath = config.localDataRootPath;
    options.modRootPath = config.modRootPath;
    state.database = state.services.loadDatabase(options);
    std::string progressError;
    if (!state.services.loadProgress(config.localDataRootPath, state.progress, progressError)
        && progressError.empty() == false) {
        state.status = L"Progress read failed: " + state.services.widen(progressError);
    }
    if (loadGame) {
        for (CampaignSaveInfo& save :
                state.services.campaignSaves(config.localDataRootPath)) {
            LoadSaveEntry entry;
            entry.kind = LoadSaveKind::Campaign;
            entry.campaign = std::move(save);
            state.saves.push_back(std::move(entry));
        }
        for (SkirmishSaveInfo& save :
                state.services.skirmishSaves(config.localDataRootPath)) {
            std::string pathError;
            if (!state.services.validateSavePaths(
                    config,
                    save,
                    pathError
                )) {
                state.skirmishSaveWarning =
                    L"遭遇战存档不可从主菜单载入：" + state.services.widen(pathError);
                continue;
            }
            LoadSaveEntry entry;
            entry.kind = LoadSaveKind::Skirmish;
            entry.skirmish = std::move(save);
            state.saves.push_back(std::move(entry));
        }
        std::stable_sort(
            state.saves.begin(),
            state.saves.end(),
            [](const LoadSaveEntry& left, const LoadSaveEntry& right) {
                if (left.kind != right.kind) {
                    return left.kind == LoadSaveKind::Skirmish;
                }
                const int leftTick = left.kind == LoadSaveKind::Skirmish
                    ? left.skirmish.tick
                    : left.campaign.tick;
                const int rightTick = right.kind == LoadSaveKind::Skirmish
                    ? right.skirmish.tick
                    : right.campaign.tick;
                if (leftTick != rightTick) {
                    return leftTick > rightTick;
                }
                const std::string leftPath = left.kind == LoadSaveKind::Skirmish
                    ? left.skirmish.path
                    : left.campaign.path;
                const std::string rightPath = right.kind == LoadSaveKind::Skirmish
                    ? right.skirmish.path
                    : right.campaign.path;
                return lowerAscii(leftPath) < lowerAscii(rightPath);
            }
        );

        const std::wstring warning = state.services.incompatibleSaveWarning(config.localDataRootPath);
        if (!warning.empty()) state.skirmishSaveWarning = warning;
        if (state.saves.empty()) {
            state.status = state.skirmishSaveWarning.empty()
                ? L"No compatible campaign or skirmish saves were found."
                : state.skirmishSaveWarning;
        } else if (!state.skirmishSaveWarning.empty()) {
            state.status = state.skirmishSaveWarning;
        }
    } else if (state.database.campaigns.empty()) {
        state.status = L"Campaign database is empty: " + state.services.widen(state.database.status);
    } else {
        chooseFirstUnlockedMission(state);
    }
}

} // namespace concord::campaign_menu
