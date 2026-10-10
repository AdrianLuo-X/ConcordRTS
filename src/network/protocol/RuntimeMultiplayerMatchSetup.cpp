#include "network/protocol/RuntimeMultiplayerMatchSetup.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

class ByteWriter {
public:
    void writeU8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void writeU16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void writeU32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 24U));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 16U));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void writeU64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes_.push_back(static_cast<std::uint8_t>(
                value >> static_cast<unsigned int>(shift)
            ));
        }
    }

    void writeBool(bool value) {
        writeU8(value ? 1U : 0U);
    }

    void writeString(const std::string& value) {
        writeU16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    const std::vector<std::uint8_t>& bytes() const {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    bool readU8(std::uint8_t& value) {
        if (!require(1U)) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool readU16(std::uint16_t& value) {
        if (!require(2U)) return false;
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes_[offset_]) << 8U)
            | static_cast<std::uint16_t>(bytes_[offset_ + 1U])
        );
        offset_ += 2U;
        return true;
    }

    bool readU32(std::uint32_t& value) {
        if (!require(4U)) return false;
        value =
            (static_cast<std::uint32_t>(bytes_[offset_]) << 24U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 16U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 8U)
            | static_cast<std::uint32_t>(bytes_[offset_ + 3U]);
        offset_ += 4U;
        return true;
    }

    bool readU64(std::uint64_t& value) {
        if (!require(8U)) return false;
        value = 0;
        for (std::size_t index = 0; index < 8U; ++index) {
            value = (value << 8U)
                | static_cast<std::uint64_t>(bytes_[offset_ + index]);
        }
        offset_ += 8U;
        return true;
    }

    bool readBool(bool& value, const char* fieldName) {
        std::uint8_t encoded = 0;
        if (!readU8(encoded)) return false;
        if (encoded > 1U) {
            return fail(
                std::string("match setup ") + fieldName
                    + " boolean is invalid"
            );
        }
        value = encoded != 0U;
        return true;
    }

    bool readString(
        std::size_t maximumBytes,
        std::string& value,
        const char* fieldName
    ) {
        std::uint16_t length = 0;
        if (!readU16(length)) return false;
        if (static_cast<std::size_t>(length) > maximumBytes) {
            return fail(
                std::string("match setup ") + fieldName
                    + " exceeds the size limit"
            );
        }
        if (!require(static_cast<std::size_t>(length))) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            static_cast<std::size_t>(length)
        );
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    bool finished() const {
        return offset_ == bytes_.size();
    }

    bool fail(std::string message) {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }

    const std::string& error() const {
        return error_;
    }

private:
    bool require(std::size_t count) {
        if (count <= bytes_.size() - offset_) return true;
        return fail("match setup payload is truncated");
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
    std::string error_;
};

bool fail(std::string& errorMessage, std::string message) {
    errorMessage = std::move(message);
    return false;
}

bool isAsciiWhitespace(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

std::string trimAscii(std::string value) {
    std::size_t first = 0;
    while (first < value.size() && isAsciiWhitespace(value[first])) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && isAsciiWhitespace(value[last - 1U])) {
        --last;
    }
    return value.substr(first, last - first);
}

void normalizeToken(std::string& value) {
    value = trimAscii(std::move(value));
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
}

void normalizeResourceId(std::string& value) {
    normalizeToken(value);
    for (char& character : value) {
        if (character == '\\') character = '/';
    }
}

bool validPeerId(const std::string& value) {
    if (value.empty()
        || value.size() > runtimeMultiplayerMatchMaximumPeerIdBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        const bool alphaNumeric =
            (byte >= static_cast<unsigned char>('a')
                && byte <= static_cast<unsigned char>('z'))
            || (byte >= static_cast<unsigned char>('A')
                && byte <= static_cast<unsigned char>('Z'))
            || (byte >= static_cast<unsigned char>('0')
                && byte <= static_cast<unsigned char>('9'));
        return alphaNumeric
            || byte == static_cast<unsigned char>('-')
            || byte == static_cast<unsigned char>('_')
            || byte == static_cast<unsigned char>('.');
    });
}

bool validDisplayName(const std::string& value) {
    if (value.empty()
        || value.size() > runtimeMultiplayerMatchMaximumDisplayNameBytes
        || value != trimAscii(value)) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return byte >= 0x20U && byte != 0x7fU;
    });
}

bool validCanonicalToken(
    const std::string& value,
    std::size_t maximumBytes
) {
    if (value.empty() || value.size() > maximumBytes) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        const bool alphaNumeric =
            (byte >= static_cast<unsigned char>('a')
                && byte <= static_cast<unsigned char>('z'))
            || (byte >= static_cast<unsigned char>('0')
                && byte <= static_cast<unsigned char>('9'));
        return alphaNumeric
            || byte == static_cast<unsigned char>('-')
            || byte == static_cast<unsigned char>('_')
            || byte == static_cast<unsigned char>('.');
    });
}

bool validCanonicalResourceId(
    const std::string& value,
    std::size_t maximumBytes
) {
    if (value.empty() || value.size() > maximumBytes
        || value.front() == '/' || value.back() == '/') {
        return false;
    }

    std::size_t segmentStart = 0;
    while (segmentStart < value.size()) {
        const std::size_t slash = value.find('/', segmentStart);
        const std::size_t segmentEnd = slash == std::string::npos
            ? value.size()
            : slash;
        if (segmentEnd == segmentStart) return false;
        const std::string_view segment(
            value.data() + segmentStart,
            segmentEnd - segmentStart
        );
        if (segment == "." || segment == ".."
            || !std::all_of(
                segment.begin(),
                segment.end(),
                [](unsigned char byte) {
                    const bool alphaNumeric =
                        (byte >= static_cast<unsigned char>('a')
                            && byte <= static_cast<unsigned char>('z'))
                        || (byte >= static_cast<unsigned char>('0')
                            && byte <= static_cast<unsigned char>('9'));
                    return alphaNumeric
                        || byte == static_cast<unsigned char>('-')
                        || byte == static_cast<unsigned char>('_')
                        || byte == static_cast<unsigned char>('.');
                }
            )) {
            return false;
        }
        if (slash == std::string::npos) break;
        segmentStart = slash + 1U;
    }
    return true;
}

bool validMemberIdentity(RuntimeMultiplayerMatchMemberIdentity identity) {
    switch (identity) {
    case RuntimeMultiplayerMatchMemberIdentity::Combatant:
    case RuntimeMultiplayerMatchMemberIdentity::Spectator:
        return true;
    }
    return false;
}

bool validSlotController(RuntimeMultiplayerMatchSlotController controller) {
    switch (controller) {
    case RuntimeMultiplayerMatchSlotController::Human:
    case RuntimeMultiplayerMatchSlotController::Computer:
    case RuntimeMultiplayerMatchSlotController::Closed:
        return true;
    }
    return false;
}

const RuntimeMultiplayerMatchMember* findMember(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string_view peerId
) {
    const auto member = std::find_if(
        setup.members.begin(),
        setup.members.end(),
        [peerId](const RuntimeMultiplayerMatchMember& candidate) {
            return candidate.peerId.size() == peerId.size()
                && std::equal(
                    candidate.peerId.begin(),
                    candidate.peerId.end(),
                    peerId.begin()
                );
        }
    );
    return member == setup.members.end() ? nullptr : &*member;
}

#include "network/protocol/RuntimeMultiplayerCooperativeRules.inl"

bool validateRules(
    const RuntimeMultiplayerMatchRules& rules,
    std::string& errorMessage
) {
    if (!validCanonicalToken(
            rules.gameMode,
            runtimeMultiplayerMatchMaximumGameModeBytes
        )) {
        return fail(errorMessage, "match setup game mode is invalid");
    }
    if (!validCanonicalResourceId(
            rules.rulesOverride,
            runtimeMultiplayerMatchMaximumRulesOverrideBytes
        )) {
        return fail(errorMessage, "match setup rules override is invalid");
    }
    if (rules.randomSeed == 0U) {
        return fail(errorMessage, "match setup random seed must be nonzero");
    }
    if (rules.fixedTickHz <= 0
        || rules.fixedTickHz
            > runtimeMultiplayerMatchMaximumFixedTickHz) {
        return fail(errorMessage, "match setup fixed tick rate is invalid");
    }
    if (rules.startingCredits < 0
        || rules.startingCredits
            > runtimeMultiplayerMatchMaximumStartingCredits) {
        return fail(errorMessage, "match setup starting credits are invalid");
    }
    if (rules.startingUnits < 0
        || rules.startingUnits
            > runtimeMultiplayerMatchMaximumStartingUnits) {
        return fail(errorMessage, "match setup starting units are invalid");
    }
    if (rules.techLevel <= 0
        || rules.techLevel > runtimeMultiplayerMatchMaximumTechLevel) {
        return fail(errorMessage, "match setup tech level is invalid");
    }
    if (rules.gameSpeed < 0
        || rules.gameSpeed > runtimeMultiplayerMatchMaximumGameSpeed) {
        return fail(errorMessage, "match setup game speed is invalid");
    }
    return validateCooperativeRules(rules, errorMessage);
}

bool validateActiveSlotOptions(
    const RuntimeMultiplayerMatchSlot& slot,
    std::string& errorMessage
) {
    if (!validCanonicalToken(
            slot.countryId,
            runtimeMultiplayerMatchMaximumSlotOptionBytes
        )) {
        return fail(errorMessage, "match setup slot country is invalid");
    }
    if (!validCanonicalToken(
            slot.colorId,
            runtimeMultiplayerMatchMaximumSlotOptionBytes
        )) {
        return fail(errorMessage, "match setup slot color is invalid");
    }
    if (!validCanonicalToken(
            slot.teamId,
            runtimeMultiplayerMatchMaximumSlotOptionBytes
        )) {
        return fail(errorMessage, "match setup slot team is invalid");
    }
    if (!validCanonicalToken(
            slot.difficultyId,
            runtimeMultiplayerMatchMaximumSlotOptionBytes
        )) {
        return fail(errorMessage, "match setup slot difficulty is invalid");
    }
    if (slot.startPosition > runtimeMultiplayerMatchMaximumSlots) {
        return fail(errorMessage, "match setup slot start position is invalid");
    }
    return true;
}

void writeRules(
    ByteWriter& writer,
    const RuntimeMultiplayerMatchRules& rules,
    bool includeCooperative = true
) {
    writer.writeString(rules.gameMode);
    writer.writeString(rules.rulesOverride);
    writer.writeU64(rules.randomSeed);
    writer.writeU32(static_cast<std::uint32_t>(rules.fixedTickHz));
    writer.writeU32(static_cast<std::uint32_t>(rules.startingCredits));
    writer.writeU32(static_cast<std::uint32_t>(rules.startingUnits));
    writer.writeU32(static_cast<std::uint32_t>(rules.techLevel));
    writer.writeU32(static_cast<std::uint32_t>(rules.gameSpeed));
    writer.writeBool(rules.shortGame);
    writer.writeBool(rules.mcvRepacks);
    writer.writeBool(rules.crates);
    writer.writeBool(rules.superweapons);
    writer.writeBool(rules.buildOffAlly);
    if (includeCooperative) writeCooperativeRules(writer, rules.cooperativeMission);
}

bool readBoundedInt32(
    ByteReader& reader,
    std::int32_t& value,
    const char* fieldName
) {
    std::uint32_t encoded = 0;
    if (!reader.readU32(encoded)) return false;
    if (encoded > static_cast<std::uint32_t>(
            std::numeric_limits<std::int32_t>::max()
        )) {
        return reader.fail(
            std::string("match setup ") + fieldName + " is out of range"
        );
    }
    value = static_cast<std::int32_t>(encoded);
    return true;
}

bool readRules(ByteReader& reader, RuntimeMultiplayerMatchRules& rules, bool includeCooperative = true) {
    return reader.readString(
            runtimeMultiplayerMatchMaximumGameModeBytes,
            rules.gameMode,
            "game mode"
        )
        && reader.readString(
            runtimeMultiplayerMatchMaximumRulesOverrideBytes,
            rules.rulesOverride,
            "rules override"
        )
        && reader.readU64(rules.randomSeed)
        && readBoundedInt32(reader, rules.fixedTickHz, "fixed tick rate")
        && readBoundedInt32(
            reader,
            rules.startingCredits,
            "starting credits"
        )
        && readBoundedInt32(
            reader,
            rules.startingUnits,
            "starting units"
        )
        && readBoundedInt32(reader, rules.techLevel, "tech level")
        && readBoundedInt32(reader, rules.gameSpeed, "game speed")
        && reader.readBool(rules.shortGame, "short-game")
        && reader.readBool(rules.mcvRepacks, "MCV-repacks")
        && reader.readBool(rules.crates, "crates")
        && reader.readBool(rules.superweapons, "superweapons")
        && reader.readBool(rules.buildOffAlly, "build-off-ally")
        && (!includeCooperative || readCooperativeRules(reader, rules.cooperativeMission));
}

} // namespace

bool validateRuntimeMultiplayerMatchSetup(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (setup.revision == 0U) {
        return fail(errorMessage, "match setup revision must be nonzero");
    }
    if (!validPeerId(setup.hostPeerId)) {
        return fail(errorMessage, "match setup host peer id is invalid");
    }
    if (!validCanonicalResourceId(
            setup.map.canonicalId,
            runtimeMultiplayerMatchMaximumCanonicalMapIdBytes
        )) {
        return fail(errorMessage, "match setup canonical map id is invalid");
    }
    if (setup.map.contentFingerprint == 0U) {
        return fail(
            errorMessage,
            "match setup map content fingerprint must be nonzero"
        );
    }
    if (!validateRules(setup.rules, errorMessage)) return false;

    if (setup.members.empty()
        || setup.members.size() > runtimeMultiplayerMatchMaximumMembers) {
        return fail(errorMessage, "match setup member count is invalid");
    }

    std::string previousPeerId;
    bool havePreviousPeerId = false;
    std::size_t hostCount = 0;
    for (const RuntimeMultiplayerMatchMember& member : setup.members) {
        if (!validPeerId(member.peerId)) {
            return fail(errorMessage, "match setup member peer id is invalid");
        }
        if (havePreviousPeerId && member.peerId <= previousPeerId) {
            return fail(
                errorMessage,
                "match setup member peer ids are not unique canonical order"
            );
        }
        previousPeerId = member.peerId;
        havePreviousPeerId = true;
        if (!validDisplayName(member.displayName)) {
            return fail(
                errorMessage,
                "match setup member display name is invalid"
            );
        }
        if (!validMemberIdentity(member.identity)) {
            return fail(
                errorMessage,
                "match setup member identity is invalid"
            );
        }
        if (member.peerId == setup.hostPeerId) ++hostCount;
    }
    if (hostCount != 1U) {
        return fail(
            errorMessage,
            "match setup host must name exactly one room member"
        );
    }

    if (setup.slots.empty()
        || setup.slots.size() > runtimeMultiplayerMatchMaximumSlots) {
        return fail(errorMessage, "match setup slot count is invalid");
    }

    std::set<std::string> boundHumanPeers;
    std::set<std::string> reservedColors;
    std::set<std::uint8_t> reservedStarts;
    std::size_t combatantCount = 0;
    for (std::size_t index = 0; index < setup.slots.size(); ++index) {
        const RuntimeMultiplayerMatchSlot& slot = setup.slots[index];
        if (slot.slotIndex != static_cast<std::uint8_t>(index)) {
            return fail(
                errorMessage,
                "match setup slot indices are not contiguous canonical order"
            );
        }
        if (!validSlotController(slot.controller)) {
            return fail(
                errorMessage,
                "match setup slot controller is invalid"
            );
        }

        if (slot.controller == RuntimeMultiplayerMatchSlotController::Closed) {
            if (!slot.peerId.empty()
                || !slot.countryId.empty()
                || !slot.colorId.empty()
                || !slot.teamId.empty()
                || !slot.difficultyId.empty()
                || slot.startPosition != 0U) {
                return fail(
                    errorMessage,
                    "match setup closed slot carries combatant data"
                );
            }
            continue;
        }

        ++combatantCount;
        if (!validateActiveSlotOptions(slot, errorMessage)) return false;
        if (slot.colorId != "random"
            && !reservedColors.insert(slot.colorId).second) {
            return fail(errorMessage, "match setup slot color is duplicated");
        }
        if (slot.startPosition != 0U
            && !reservedStarts.insert(slot.startPosition).second) {
            return fail(
                errorMessage,
                "match setup slot start position is duplicated"
            );
        }

        if (slot.controller == RuntimeMultiplayerMatchSlotController::Human) {
            if (!validPeerId(slot.peerId)) {
                return fail(
                    errorMessage,
                    "match setup human slot peer id is invalid"
                );
            }
            const RuntimeMultiplayerMatchMember* member = findMember(
                setup,
                slot.peerId
            );
            if (member == nullptr) {
                return fail(
                    errorMessage,
                    "match setup human slot peer is not a room member"
                );
            }
            if (member->identity
                == RuntimeMultiplayerMatchMemberIdentity::Spectator) {
                return fail(
                    errorMessage,
                    "match setup spectator cannot own a combat slot"
                );
            }
            if (!boundHumanPeers.insert(slot.peerId).second) {
                return fail(
                    errorMessage,
                    "match setup human peer owns more than one combat slot"
                );
            }
            continue;
        }

        if (!slot.peerId.empty()) {
            const RuntimeMultiplayerMatchMember* member = findMember(
                setup,
                slot.peerId
            );
            if (member != nullptr
                && member->identity
                    == RuntimeMultiplayerMatchMemberIdentity::Spectator) {
                return fail(
                    errorMessage,
                    "match setup computer spectator is invalid"
                );
            }
            return fail(
                errorMessage,
                "match setup computer slot cannot bind a peer id"
            );
        }
    }

    if (!validateCooperativeSlots(setup, errorMessage)) return false;
    if (combatantCount < 2U) {
        return fail(
            errorMessage,
            "match setup requires at least two combatants"
        );
    }
    for (const RuntimeMultiplayerMatchMember& member : setup.members) {
        const bool hasHumanSlot = boundHumanPeers.contains(member.peerId);
        if (member.identity
                == RuntimeMultiplayerMatchMemberIdentity::Combatant
            && !hasHumanSlot) {
            return fail(
                errorMessage,
                "match setup combatant member must own exactly one human slot"
            );
        }
        if (member.identity
                == RuntimeMultiplayerMatchMemberIdentity::Spectator
            && hasHumanSlot) {
            return fail(
                errorMessage,
                "match setup spectator cannot own a combat slot"
            );
        }
    }
    return true;
}

bool canonicalizeRuntimeMultiplayerMatchSetup(
    RuntimeMultiplayerMatchSetup& setup,
    std::string& errorMessage
) {
    RuntimeMultiplayerMatchSetup candidate = setup;
    normalizeResourceId(candidate.map.canonicalId);
    normalizeToken(candidate.rules.gameMode);
    normalizeResourceId(candidate.rules.rulesOverride);
    normalizeToken(candidate.rules.cooperativeMission.mapId);
    for (auto* countries : {&candidate.rules.cooperativeMission.playerCountries,
                           &candidate.rules.cooperativeMission.enemyCountries})
        for (auto& country : *countries) normalizeToken(country);
    for (RuntimeMultiplayerMatchMember& member : candidate.members) {
        member.displayName = trimAscii(std::move(member.displayName));
    }
    for (RuntimeMultiplayerMatchSlot& slot : candidate.slots) {
        normalizeToken(slot.countryId);
        normalizeToken(slot.colorId);
        normalizeToken(slot.teamId);
        normalizeToken(slot.difficultyId);
    }
    std::sort(
        candidate.members.begin(),
        candidate.members.end(),
        [](const RuntimeMultiplayerMatchMember& left,
           const RuntimeMultiplayerMatchMember& right) {
            return left.peerId < right.peerId;
        }
    );
    std::sort(
        candidate.slots.begin(),
        candidate.slots.end(),
        [](const RuntimeMultiplayerMatchSlot& left,
           const RuntimeMultiplayerMatchSlot& right) {
            return left.slotIndex < right.slotIndex;
        }
    );
    if (!validateRuntimeMultiplayerMatchSetup(candidate, errorMessage)) {
        return false;
    }
    setup = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool encodeRuntimeMultiplayerMatchSetup(
    const RuntimeMultiplayerMatchSetup& setup,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage
) {
    RuntimeMultiplayerMatchSetup candidate = setup;
    if (!canonicalizeRuntimeMultiplayerMatchSetup(candidate, errorMessage)) {
        return false;
    }

    ByteWriter writer;
    writer.writeU32(runtimeMultiplayerMatchSetupMagic);
    writer.writeU16(runtimeMultiplayerMatchSetupVersion);
    writer.writeU64(candidate.revision);
    writer.writeString(candidate.hostPeerId);
    writer.writeString(candidate.map.canonicalId);
    writer.writeU64(candidate.map.contentFingerprint);
    writeRules(writer, candidate.rules);

    writer.writeU8(static_cast<std::uint8_t>(candidate.members.size()));
    for (const RuntimeMultiplayerMatchMember& member : candidate.members) {
        writer.writeString(member.peerId);
        writer.writeString(member.displayName);
        writer.writeU8(static_cast<std::uint8_t>(member.identity));
    }

    writer.writeU8(static_cast<std::uint8_t>(candidate.slots.size()));
    for (const RuntimeMultiplayerMatchSlot& slot : candidate.slots) {
        writer.writeU8(slot.slotIndex);
        writer.writeU8(static_cast<std::uint8_t>(slot.controller));
        writer.writeString(slot.peerId);
        writer.writeString(slot.countryId);
        writer.writeString(slot.colorId);
        writer.writeString(slot.teamId);
        writer.writeString(slot.difficultyId);
        writer.writeU8(slot.startPosition);
    }

    if (writer.bytes().size()
        > runtimeMultiplayerMatchMaximumPayloadBytes) {
        return fail(errorMessage, "match setup payload exceeds the size limit");
    }
    payload = writer.bytes();
    errorMessage.clear();
    return true;
}

bool decodeRuntimeMultiplayerMatchSetup(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerMatchSetup& setup,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (payload.size() > runtimeMultiplayerMatchMaximumPayloadBytes) {
        return fail(errorMessage, "match setup payload exceeds the size limit");
    }

    ByteReader reader(payload);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    RuntimeMultiplayerMatchSetup candidate;
    if (!reader.readU32(magic) || !reader.readU16(version)) {
        return fail(errorMessage, reader.error());
    }
    if (magic != runtimeMultiplayerMatchSetupMagic) {
        return fail(errorMessage, "match setup payload magic is invalid");
    }
    if (version != 2 && version != runtimeMultiplayerMatchSetupVersion) {
        return fail(errorMessage, "match setup payload version is unsupported");
    }
    if (!reader.readU64(candidate.revision)
        || !reader.readString(
            runtimeMultiplayerMatchMaximumPeerIdBytes,
            candidate.hostPeerId,
            "host peer id"
        )
        || !reader.readString(
            runtimeMultiplayerMatchMaximumCanonicalMapIdBytes,
            candidate.map.canonicalId,
            "canonical map id"
        )
        || !reader.readU64(candidate.map.contentFingerprint)
        || !readRules(reader, candidate.rules, version >= 3)) {
        return fail(errorMessage, reader.error());
    }

    std::uint8_t memberCount = 0;
    if (!reader.readU8(memberCount)) {
        return fail(errorMessage, reader.error());
    }
    if (memberCount > runtimeMultiplayerMatchMaximumMembers) {
        return fail(errorMessage, "match setup member count exceeds the limit");
    }
    candidate.members.reserve(static_cast<std::size_t>(memberCount));
    for (std::uint8_t index = 0; index < memberCount; ++index) {
        RuntimeMultiplayerMatchMember member;
        std::uint8_t identity = 0;
        if (!reader.readString(
                runtimeMultiplayerMatchMaximumPeerIdBytes,
                member.peerId,
                "member peer id"
            )
            || !reader.readString(
                runtimeMultiplayerMatchMaximumDisplayNameBytes,
                member.displayName,
                "member display name"
            )
            || !reader.readU8(identity)) {
            return fail(errorMessage, reader.error());
        }
        member.identity = static_cast<
            RuntimeMultiplayerMatchMemberIdentity>(identity);
        candidate.members.push_back(std::move(member));
    }

    std::uint8_t slotCount = 0;
    if (!reader.readU8(slotCount)) {
        return fail(errorMessage, reader.error());
    }
    if (slotCount > runtimeMultiplayerMatchMaximumSlots) {
        return fail(errorMessage, "match setup slot count exceeds the limit");
    }
    candidate.slots.reserve(static_cast<std::size_t>(slotCount));
    for (std::uint8_t index = 0; index < slotCount; ++index) {
        RuntimeMultiplayerMatchSlot slot;
        std::uint8_t controller = 0;
        if (!reader.readU8(slot.slotIndex)
            || !reader.readU8(controller)
            || !reader.readString(
                runtimeMultiplayerMatchMaximumPeerIdBytes,
                slot.peerId,
                "slot peer id"
            )
            || !reader.readString(
                runtimeMultiplayerMatchMaximumSlotOptionBytes,
                slot.countryId,
                "slot country"
            )
            || !reader.readString(
                runtimeMultiplayerMatchMaximumSlotOptionBytes,
                slot.colorId,
                "slot color"
            )
            || !reader.readString(
                runtimeMultiplayerMatchMaximumSlotOptionBytes,
                slot.teamId,
                "slot team"
            )
            || !reader.readString(
                runtimeMultiplayerMatchMaximumSlotOptionBytes,
                slot.difficultyId,
                "slot difficulty"
            )
            || !reader.readU8(slot.startPosition)) {
            return fail(errorMessage, reader.error());
        }
        slot.controller = static_cast<
            RuntimeMultiplayerMatchSlotController>(controller);
        candidate.slots.push_back(std::move(slot));
    }

    if (!reader.finished()) {
        return fail(errorMessage, "match setup payload has trailing bytes");
    }

    RuntimeMultiplayerMatchSetup canonical = candidate;
    if (!canonicalizeRuntimeMultiplayerMatchSetup(canonical, errorMessage)) {
        return false;
    }
    if (canonical != candidate) {
        return fail(errorMessage, "match setup payload is not canonical");
    }

    setup = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool runtimeMultiplayerMatchSetupHash(
    const RuntimeMultiplayerMatchSetup& setup,
    std::uint64_t& hash,
    std::string& errorMessage
) {
    std::vector<std::uint8_t> payload;
    if (!encodeRuntimeMultiplayerMatchSetup(setup, payload, errorMessage)) {
        return false;
    }
    std::uint64_t candidate = kFnvOffsetBasis;
    for (const std::uint8_t byte : payload) {
        candidate ^= byte;
        candidate *= kFnvPrime;
    }
    hash = candidate;
    errorMessage.clear();
    return true;
}

bool runtimeMultiplayerGameplaySetupFingerprint(
    const RuntimeMultiplayerMatchSetup& setup,
    std::uint64_t& fingerprint,
    std::string& errorMessage
) {
    RuntimeMultiplayerMatchSetup canonical = setup;
    if (!canonicalizeRuntimeMultiplayerMatchSetup(
            canonical,
            errorMessage
        )) {
        return false;
    }

    // This is a distinct canonical image, not a truncation or filtering of
    // the room setup wire payload. Keep an explicit domain/version so adding
    // a gameplay field cannot silently collide with the older schema.
    ByteWriter writer;
    writer.writeU32(0x43475346U); // "CGSF"
    const bool cooperative = canonical.rules.gameMode == "cooperative";
    // Ordinary saved games retain their existing gameplay identity. The new
    // cooperative image additionally binds the authored mission restrictions.
    writer.writeU16(cooperative ? 2U : 1U);
    writer.writeString(canonical.map.canonicalId);
    writer.writeU64(canonical.map.contentFingerprint);
    writeRules(writer, canonical.rules, cooperative);
    writer.writeU8(static_cast<std::uint8_t>(canonical.slots.size()));
    for (const RuntimeMultiplayerMatchSlot& slot : canonical.slots) {
        writer.writeU8(slot.slotIndex);
        writer.writeU8(static_cast<std::uint8_t>(slot.controller));
        // Deliberately omit slot.peerId. The authenticated room binding is
        // allowed to change when the same saved gameplay slots are resumed.
        writer.writeString(slot.countryId);
        writer.writeString(slot.colorId);
        writer.writeString(slot.teamId);
        writer.writeString(slot.difficultyId);
        writer.writeU8(slot.startPosition);
    }

    std::uint64_t candidate = kFnvOffsetBasis;
    for (const std::uint8_t byte : writer.bytes()) {
        candidate ^= byte;
        candidate *= kFnvPrime;
    }
    fingerprint = candidate;
    errorMessage.clear();
    return true;
}

bool makeRuntimeMultiplayerMatchLaunchDescription(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string_view localPeerId,
    RuntimeMultiplayerMatchLaunchDescription& launch,
    std::string& errorMessage
) {
    RuntimeMultiplayerMatchSetup canonical = setup;
    if (!canonicalizeRuntimeMultiplayerMatchSetup(canonical, errorMessage)) {
        return false;
    }
    const RuntimeMultiplayerMatchMember* localMember = findMember(
        canonical,
        localPeerId
    );
    if (localMember == nullptr) {
        return fail(
            errorMessage,
            "match launch local peer is not a room member"
        );
    }

    RuntimeMultiplayerMatchLaunchDescription candidate;
    if (canonical.rules.gameMode == "cooperative"
        && (canonical.slots[0].controller != RuntimeMultiplayerMatchSlotController::Human
            || canonical.slots[1].controller != RuntimeMultiplayerMatchSlotController::Human))
        return fail(errorMessage, "cooperative mission is waiting for both human players");
    candidate.setupRevision = canonical.revision;
    if (!runtimeMultiplayerMatchSetupHash(
            canonical,
            candidate.setupHash,
            errorMessage
        )) {
        return false;
    }
    candidate.hostPeerId = canonical.hostPeerId;
    candidate.localPeerIsHost = localMember->peerId == canonical.hostPeerId;
    candidate.localPeerIsSpectator = localMember->identity
        == RuntimeMultiplayerMatchMemberIdentity::Spectator;
    candidate.map = canonical.map;
    candidate.rules = canonical.rules;
    candidate.slots.reserve(canonical.slots.size());

    for (const RuntimeMultiplayerMatchSlot& source : canonical.slots) {
        RuntimeMultiplayerMatchLaunchSlot target;
        target.slotIndex = source.slotIndex;
        target.controller = source.controller;
        target.enabled = source.controller
            != RuntimeMultiplayerMatchSlotController::Closed;
        target.human = source.controller
            == RuntimeMultiplayerMatchSlotController::Human;
        target.local = target.human && source.peerId == localMember->peerId;
        target.peerId = source.peerId;
        target.countryId = source.countryId;
        target.colorId = source.colorId;
        target.teamId = source.teamId;
        target.difficultyId = source.difficultyId;
        target.startPosition = source.startPosition;
        if (target.human) {
            const RuntimeMultiplayerMatchMember* owner = findMember(
                canonical,
                source.peerId
            );
            if (owner == nullptr) {
                return fail(
                    errorMessage,
                    "match launch human slot owner is unavailable"
                );
            }
            target.displayName = owner->displayName;
        }
        candidate.slots.push_back(std::move(target));
    }

    launch = std::move(candidate);
    errorMessage.clear();
    return true;
}
