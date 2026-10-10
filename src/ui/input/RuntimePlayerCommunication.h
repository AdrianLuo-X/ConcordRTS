#pragma once

#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct RuntimePlayerChatLine {
    std::string owner;
    std::string text;
    bool allies = false;
    int untilTick = 0;
};
struct RuntimePlayerBeacon {
    std::string owner;
    RuntimeMultiplayerGameplayCell cell;
    int untilTick = 0;
    std::string text;
    int level = 0;
};

// Local presentation only: none of these fields changes simulation, saves or hashes.
struct RuntimePlayerCommunicationState {
    bool chatOpen = false;
    bool chatAllies = false;
    std::wstring chatDraft;
    bool diplomacyOpen = false;
    int diplomacyRow = 0;
    int diplomacyPressedControl = 0;
    int diplomacyHoveredControl = 0;
    bool chatRecipientsExplicit = false;
    std::uint8_t chatRecipients = 255;
    bool tauntsEnabled = true;
    std::string diplomacyBackgroundSource;
    std::vector<std::string> diplomacyControlSources;
    bool beaconTargeting = false;
    std::optional<RuntimeMultiplayerGameplayCell> selectedBeacon;
    bool beaconEditing = false;
    std::deque<RuntimePlayerChatLine> lines;
    std::vector<RuntimePlayerBeacon> beacons;
    std::unordered_map<int, int> cheerUntilTick;
    std::map<std::pair<std::string, RuntimePlayerCommunicationKind>, int> nextFeedbackTick;
};
