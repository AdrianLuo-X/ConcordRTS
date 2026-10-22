#pragma once

#include "ui/input/RuntimePlayerCommunication.h"

#include <cstdint>
#include <string>
#include <string_view>

struct RuntimeChatTextCharacterInput {
    std::uint64_t rawCharacter = 0;
};

struct RuntimeChatTextKeyInput {
    std::int32_t key = 0;
    bool repeated = false;
};

// Synchronous borrowed UI queries and effects. No callback is retained.
class RuntimeChatTextHost {
public:
    virtual ~RuntimeChatTextHost() = default;
    virtual bool menuOpen() = 0;
    // The original query may clear a stale selected-beacon cell.
    virtual bool selectedBeaconValid() = 0;
    virtual RuntimeMultiplayerGameplayCell selectedBeaconCell() = 0;
    virtual std::uint8_t outgoingChatMask() = 0;
    virtual bool dispatchCommunication(RuntimeMultiplayerGameplayPlayerCommunicationCommand command) = 0;
    virtual void requestRepaint() = 0;
    virtual void invalidate() = 0;
};

// Strict UTF-16, including explicit embedded NUL units. Invalid input or an
// encoded result above the existing communication identifier limit returns empty.
std::string encodeRuntimeChatTextUtf8(std::u16string_view text);

bool handleRuntimeChatTextCharacter(RuntimeChatTextCharacterInput input,
    RuntimePlayerCommunicationState& state, RuntimeChatTextHost& host);
bool handleRuntimeChatTextKey(RuntimeChatTextKeyInput input,
    RuntimePlayerCommunicationState& state, RuntimeChatTextHost& host);
