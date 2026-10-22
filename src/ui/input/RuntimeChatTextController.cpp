#include "ui/input/RuntimeChatTextController.h"

#include <cctype>

namespace {
std::string trimChatText(std::string value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}
}

std::string encodeRuntimeChatTextUtf8(std::u16string_view text) {
    std::string encoded;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t value = text[i];
        if (value >= 0xd800 && value <= 0xdbff) {
            if (i + 1 == text.size()) return {};
            const std::uint32_t low = text[++i];
            if (low < 0xdc00 || low > 0xdfff) return {};
            value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
        } else if (value >= 0xdc00 && value <= 0xdfff) return {};
        if (value < 0x80) encoded.push_back(static_cast<char>(value));
        else if (value < 0x800) {
            encoded.push_back(static_cast<char>(0xc0 | (value >> 6)));
            encoded.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else if (value < 0x10000) {
            encoded.push_back(static_cast<char>(0xe0 | (value >> 12)));
            encoded.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else {
            encoded.push_back(static_cast<char>(0xf0 | (value >> 18)));
            encoded.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            encoded.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
        if (encoded.size() > runtimeMultiplayerGameplayMaximumIdentifierBytes) return {};
    }
    return encoded;
}

bool handleRuntimeChatTextCharacter(RuntimeChatTextCharacterInput input,
    RuntimePlayerCommunicationState& state, RuntimeChatTextHost& host) {
    if (!state.chatOpen || host.menuOpen()) return false;
    auto& draft = state.chatDraft;
    if (input.rawCharacter >= 32 && input.rawCharacter != 127 && draft.size() < 80)
        draft.push_back(static_cast<char16_t>(input.rawCharacter));
    host.requestRepaint();
    host.invalidate();
    return true;
}

bool handleRuntimeChatTextKey(RuntimeChatTextKeyInput input,
    RuntimePlayerCommunicationState& state, RuntimeChatTextHost& host) {
    if (!state.chatOpen) return false;
    if (input.key == 27) {
        state.chatOpen = state.beaconEditing = false;
        state.chatDraft.clear();
    } else if (input.key == 8 && !state.chatDraft.empty()) {
        const char16_t last = state.chatDraft.back();
        state.chatDraft.pop_back();
        if (last >= 0xdc00 && last <= 0xdfff && !state.chatDraft.empty()
            && state.chatDraft.back() >= 0xd800 && state.chatDraft.back() <= 0xdbff)
            state.chatDraft.pop_back();
    } else if (input.key == 13 && !input.repeated) {
        const auto text = trimChatText(encodeRuntimeChatTextUtf8(state.chatDraft));
        if (state.beaconEditing && !host.selectedBeaconValid()) {
            state.chatOpen = state.beaconEditing = false;
            state.chatDraft.clear();
        } else if (text.empty() && !state.beaconEditing) {
            state.chatOpen = false;
            state.chatDraft.clear();
        } else {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = state.beaconEditing ? RuntimePlayerCommunicationKind::BeaconText
                : state.chatAllies ? RuntimePlayerCommunicationKind::ChatAllies : RuntimePlayerCommunicationKind::ChatAll;
            if (state.beaconEditing) command.cell = host.selectedBeaconCell();
            else command.value = host.outgoingChatMask();
            command.text = text;
            if (host.dispatchCommunication(command)) {
                state.chatOpen = state.beaconEditing = false;
                state.chatDraft.clear();
            }
        }
    }
    host.requestRepaint();
    return true;
}
