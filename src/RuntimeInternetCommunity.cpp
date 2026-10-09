#include "RuntimeInternetCommunity.h"
#include "RuntimeInternetCommunityState.h"
#include "RuntimeAtomicFile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

constexpr std::uint8_t communityRequestFrame = 1U;
constexpr std::uint8_t communityResponseFrame = 2U;
constexpr std::size_t maximumIdentityBytes = 128U;
constexpr std::size_t maximumMatchIdBytes = 128U;
constexpr std::size_t maximumNewsIdBytes = 64U;
constexpr std::size_t maximumNewsTitleBytes = 128U;
constexpr std::size_t maximumNewsBodyBytes = 2048U;

bool fail(std::string& errorMessage, std::string reason) {
    errorMessage = std::move(reason);
    return false;
}

bool validToken(std::string_view value, std::size_t maximumBytes) {
    if (value.empty() || value.size() > maximumBytes) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z')
            || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9')
            || byte == '-' || byte == '_' || byte == '.';
    });
}

bool validUtf8(std::string_view value) {
    for (std::size_t offset = 0U; offset < value.size();) {
        const auto lead = static_cast<unsigned char>(value[offset]);
        if (lead <= 0x7fU) {
            if (lead == 0U) return false;
            ++offset;
            continue;
        }
        std::size_t continuationCount = 0U;
        std::uint32_t codePoint = 0U;
        if (lead >= 0xc2U && lead <= 0xdfU) {
            continuationCount = 1U;
            codePoint = lead & 0x1fU;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            continuationCount = 2U;
            codePoint = lead & 0x0fU;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            continuationCount = 3U;
            codePoint = lead & 0x07U;
        } else {
            return false;
        }
        if (offset + continuationCount >= value.size()) return false;
        for (std::size_t index = 0U; index < continuationCount; ++index) {
            const auto byte = static_cast<unsigned char>(
                value[offset + index + 1U]
            );
            if ((byte & 0xc0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (byte & 0x3fU);
        }
        if ((continuationCount == 1U && codePoint < 0x80U)
            || (continuationCount == 2U && codePoint < 0x800U)
            || (continuationCount == 3U && codePoint < 0x10000U)
            || codePoint > 0x10ffffU
            || (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
            return false;
        }
        offset += continuationCount + 1U;
    }
    return true;
}

bool validText(
    std::string_view value,
    std::size_t minimumBytes,
    std::size_t maximumBytes,
    bool allowLineBreaks = false
) {
    if (value.size() < minimumBytes || value.size() > maximumBytes
        || !validUtf8(value)) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [&](unsigned char byte) {
        if (byte >= 0x80U) return true;
        if (byte == '\r' || byte == '\n') return allowLineBreaks;
        return byte >= 0x20U && byte != 0x7fU;
    });
}

std::string lowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](char byte) {
        return byte >= 'A' && byte <= 'Z'
            ? static_cast<char>(byte - 'A' + 'a')
            : byte;
    });
    return result;
}

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void u16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    }

    void u32(std::uint32_t value) {
        for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
            bytes_.push_back(static_cast<std::uint8_t>(
                (value >> shift) & 0xffU
            ));
        }
    }

    void i32(std::int32_t value) {
        u32(static_cast<std::uint32_t>(value));
    }

    void u64(std::uint64_t value) {
        for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
            bytes_.push_back(static_cast<std::uint8_t>(
                (value >> shift) & 0xffU
            ));
        }
    }

    bool text(std::string_view value) {
        if (value.size() > (std::numeric_limits<std::uint16_t>::max)()) {
            return false;
        }
        u16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }

    std::vector<std::uint8_t> take() { return std::move(bytes_); }
    std::size_t size() const noexcept { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t& value) {
        if (offset_ >= bytes_.size()) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool u16(std::uint16_t& value) {
        if (bytes_.size() - offset_ < 2U) return false;
        value = static_cast<std::uint16_t>(bytes_[offset_])
            | (static_cast<std::uint16_t>(bytes_[offset_ + 1U]) << 8U);
        offset_ += 2U;
        return true;
    }

    bool u32(std::uint32_t& value) {
        if (bytes_.size() - offset_ < 4U) return false;
        value = 0U;
        for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
            value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
        }
        return true;
    }

    bool i32(std::int32_t& value) {
        std::uint32_t unsignedValue = 0U;
        if (!u32(unsignedValue)) return false;
        value = static_cast<std::int32_t>(unsignedValue);
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (bytes_.size() - offset_ < 8U) return false;
        value = 0U;
        for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
            value |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
        }
        return true;
    }

    bool text(std::string& value, std::size_t maximumBytes) {
        std::uint16_t size = 0U;
        if (!u16(size) || size > maximumBytes
            || bytes_.size() - offset_ < size) {
            return false;
        }
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            size
        );
        offset_ += size;
        return true;
    }

    bool done() const noexcept { return offset_ == bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0U;
};

bool encodeProfile(Writer& writer, const RuntimeInternetCommunityProfile& value) {
    return writer.text(value.peerId)
        && writer.text(value.displayName)
        && (writer.u64(value.registeredAtMilliseconds), true)
        && (writer.u64(value.lastSeenMilliseconds), true)
        && (writer.u32(value.wins), true)
        && (writer.u32(value.losses), true)
        && (writer.u32(value.disconnects), true)
        && (writer.i32(value.points), true)
        && (writer.u32(value.rank), true)
        && (writer.u8(value.online ? 1U : 0U), true);
}

bool decodeProfile(Reader& reader, RuntimeInternetCommunityProfile& value) {
    std::uint8_t online = 0U;
    if (!reader.text(value.peerId, maximumIdentityBytes)
        || !reader.text(
            value.displayName,
            runtimeInternetCommunityMaximumDisplayNameBytes
        )
        || !reader.u64(value.registeredAtMilliseconds)
        || !reader.u64(value.lastSeenMilliseconds)
        || !reader.u32(value.wins)
        || !reader.u32(value.losses)
        || !reader.u32(value.disconnects)
        || !reader.i32(value.points)
        || !reader.u32(value.rank)
        || !reader.u8(online)
        || online > 1U) {
        return false;
    }
    value.online = online != 0U;
    return true;
}

bool encodeFriend(Writer& writer, const RuntimeInternetCommunityFriend& value) {
    return writer.text(value.peerId)
        && writer.text(value.displayName)
        && (writer.i32(value.points), true)
        && (writer.u32(value.rank), true)
        && (writer.u8(value.online ? 1U : 0U), true);
}

bool decodeFriend(Reader& reader, RuntimeInternetCommunityFriend& value) {
    std::uint8_t online = 0U;
    if (!reader.text(value.peerId, maximumIdentityBytes)
        || !reader.text(
            value.displayName,
            runtimeInternetCommunityMaximumDisplayNameBytes
        )
        || !reader.i32(value.points)
        || !reader.u32(value.rank)
        || !reader.u8(online)
        || online > 1U) {
        return false;
    }
    value.online = online != 0U;
    return true;
}

bool encodeChannel(Writer& writer, const RuntimeInternetCommunityChannel& value) {
    return writer.text(value.channelId)
        && writer.text(value.title)
        && writer.text(value.topic)
        && (writer.u32(value.participantCount), true);
}

bool decodeChannel(Reader& reader, RuntimeInternetCommunityChannel& value) {
    return reader.text(
            value.channelId,
            runtimeInternetCommunityMaximumChannelIdBytes
        )
        && reader.text(
            value.title,
            runtimeInternetCommunityMaximumChannelTitleBytes
        )
        && reader.text(
            value.topic,
            runtimeInternetCommunityMaximumChannelTopicBytes
        )
        && reader.u32(value.participantCount);
}

bool encodeChat(
    Writer& writer,
    const RuntimeInternetCommunityChatMessage& value
) {
    writer.u64(value.messageId);
    writer.u64(value.sentAtMilliseconds);
    return writer.text(value.channelId)
        && writer.text(value.senderPeerId)
        && writer.text(value.senderDisplayName)
        && writer.text(value.text);
}

bool decodeChat(
    Reader& reader,
    RuntimeInternetCommunityChatMessage& value
) {
    return reader.u64(value.messageId)
        && reader.u64(value.sentAtMilliseconds)
        && reader.text(
            value.channelId,
            runtimeInternetCommunityMaximumChannelIdBytes
        )
        && reader.text(value.senderPeerId, maximumIdentityBytes)
        && reader.text(
            value.senderDisplayName,
            runtimeInternetCommunityMaximumDisplayNameBytes
        )
        && reader.text(value.text, runtimeInternetCommunityMaximumChatBytes);
}

bool encodeLadder(
    Writer& writer,
    const RuntimeInternetCommunityLadderEntry& value
) {
    writer.u32(value.rank);
    return writer.text(value.peerId)
        && writer.text(value.displayName)
        && (writer.i32(value.points), true)
        && (writer.u32(value.wins), true)
        && (writer.u32(value.losses), true)
        && (writer.u32(value.disconnects), true)
        && (writer.u8(value.online ? 1U : 0U), true);
}

bool decodeLadder(
    Reader& reader,
    RuntimeInternetCommunityLadderEntry& value
) {
    std::uint8_t online = 0U;
    if (!reader.u32(value.rank)
        || !reader.text(value.peerId, maximumIdentityBytes)
        || !reader.text(
            value.displayName,
            runtimeInternetCommunityMaximumDisplayNameBytes
        )
        || !reader.i32(value.points)
        || !reader.u32(value.wins)
        || !reader.u32(value.losses)
        || !reader.u32(value.disconnects)
        || !reader.u8(online)
        || online > 1U) {
        return false;
    }
    value.online = online != 0U;
    return true;
}

bool encodeNews(Writer& writer, const RuntimeInternetCommunityNewsItem& value) {
    return writer.text(value.newsId)
        && (writer.u64(value.publishedAtMilliseconds), true)
        && writer.text(value.title)
        && writer.text(value.body);
}

bool decodeNews(Reader& reader, RuntimeInternetCommunityNewsItem& value) {
    return reader.text(value.newsId, maximumNewsIdBytes)
        && reader.u64(value.publishedAtMilliseconds)
        && reader.text(value.title, maximumNewsTitleBytes)
        && reader.text(value.body, maximumNewsBodyBytes);
}

bool encodeClan(Writer& writer, const RuntimeInternetCommunityClan& value) {
    return writer.text(value.clanId)
        && writer.text(value.name)
        && writer.text(value.tag)
        && writer.text(value.ownerPeerId)
        && (writer.u64(value.createdAtMilliseconds), true)
        && (writer.u32(value.wins), true)
        && (writer.u32(value.losses), true)
        && (writer.i32(value.points), true)
        && (writer.u32(value.rank), true)
        && (writer.u32(value.memberCount), true);
}

bool decodeClan(Reader& reader, RuntimeInternetCommunityClan& value) {
    return reader.text(
            value.clanId,
            runtimeInternetCommunityMaximumClanIdBytes
        )
        && reader.text(
            value.name,
            runtimeInternetCommunityMaximumClanNameBytes
        )
        && reader.text(
            value.tag,
            runtimeInternetCommunityMaximumClanTagBytes
        )
        && reader.text(value.ownerPeerId, maximumIdentityBytes)
        && reader.u64(value.createdAtMilliseconds)
        && reader.u32(value.wins)
        && reader.u32(value.losses)
        && reader.i32(value.points)
        && reader.u32(value.rank)
        && reader.u32(value.memberCount);
}

bool encodeClanMember(
    Writer& writer,
    const RuntimeInternetCommunityClanMember& value
) {
    return writer.text(value.peerId)
        && writer.text(value.displayName)
        && (writer.u64(value.joinedAtMilliseconds), true)
        && (writer.u32(value.wins), true)
        && (writer.u32(value.losses), true)
        && (writer.u32(value.disconnects), true)
        && (writer.i32(value.points), true)
        && (writer.u8(value.owner ? 1U : 0U), true)
        && (writer.u8(value.online ? 1U : 0U), true);
}

bool decodeClanMember(
    Reader& reader,
    RuntimeInternetCommunityClanMember& value
) {
    std::uint8_t owner = 0U;
    std::uint8_t online = 0U;
    if (!reader.text(value.peerId, maximumIdentityBytes)
        || !reader.text(
            value.displayName,
            runtimeInternetCommunityMaximumDisplayNameBytes
        )
        || !reader.u64(value.joinedAtMilliseconds)
        || !reader.u32(value.wins)
        || !reader.u32(value.losses)
        || !reader.u32(value.disconnects)
        || !reader.i32(value.points)
        || !reader.u8(owner)
        || owner > 1U
        || !reader.u8(online)
        || online > 1U) {
        return false;
    }
    value.owner = owner != 0U;
    value.online = online != 0U;
    return true;
}

template<typename T, typename Encoder>
bool encodeVector(
    Writer& writer,
    const std::vector<T>& values,
    std::size_t maximumCount,
    Encoder encoder
) {
    if (values.size() > maximumCount
        || values.size() > (std::numeric_limits<std::uint16_t>::max)()) {
        return false;
    }
    writer.u16(static_cast<std::uint16_t>(values.size()));
    return std::all_of(values.begin(), values.end(), [&](const T& value) {
        return encoder(writer, value);
    });
}

template<typename T, typename Decoder>
bool decodeVector(
    Reader& reader,
    std::vector<T>& values,
    std::size_t maximumCount,
    Decoder decoder
) {
    std::uint16_t count = 0U;
    if (!reader.u16(count) || count > maximumCount) return false;
    std::vector<T> candidate;
    candidate.reserve(count);
    for (std::uint16_t index = 0U; index < count; ++index) {
        T value;
        if (!decoder(reader, value)) return false;
        candidate.push_back(std::move(value));
    }
    values = std::move(candidate);
    return true;
}

bool encodeRankedRecord(Writer& writer, const RuntimeInternetRankedRecord& record) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    return encodeRuntimeInternetRankedRecord(record, bytes, error)
        && writer.text(std::string(bytes.begin(), bytes.end()));
}

bool decodeRankedRecord(Reader& reader, RuntimeInternetRankedRecord& record) {
    std::string bytes, error;
    return reader.text(bytes, runtimeInternetRankedRecordMaximumBytes)
        && decodeRuntimeInternetRankedRecord(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()), record, error);
}

bool validMatchHistory(const RuntimeInternetCommunityResponse& response) {
    if (response.matchHistory.size() > runtimeInternetCommunityMaximumMatchHistory) return false;
    std::set<std::string> ids;
    for (const auto& record : response.matchHistory) {
        std::string error;
        if (!validateRuntimeInternetRankedRecord(record, error) || !ids.insert(record.matchId).second
            || (record.peerIds[0] != response.targetPeerId && record.peerIds[1] != response.targetPeerId)) return false;
    }
    return true;
}

bool encodeSnapshot(
    Writer& writer,
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    writer.u8(snapshot.registeredProfile ? 1U : 0U);
    if (!encodeProfile(writer, snapshot.profile)
        || !writer.text(snapshot.selectedChannelId)
        || !encodeVector(
            writer,
            snapshot.channels,
            runtimeInternetCommunityMaximumChannels,
            encodeChannel
        )
        || !encodeVector(
            writer,
            snapshot.participants,
            runtimeInternetCommunityMaximumParticipants,
            encodeProfile
        )
        || !encodeVector(
            writer,
            snapshot.chatHistory,
            runtimeInternetCommunityMaximumChatHistory,
            encodeChat
        )
        || !encodeVector(
            writer,
            snapshot.friends,
            runtimeInternetCommunityMaximumFriends,
            encodeFriend
        )
        || !encodeVector(
            writer,
            snapshot.ladder,
            runtimeInternetCommunityMaximumLadderEntries,
            encodeLadder
        )
        || !encodeVector(
            writer,
            snapshot.news,
            runtimeInternetCommunityMaximumNewsItems,
            encodeNews
        )) {
        return false;
    }
    writer.u8(snapshot.clanMember ? 1U : 0U);
    if (!encodeClan(writer, snapshot.clan)
        || !encodeVector(
            writer,
            snapshot.clanMembers,
            runtimeInternetCommunityMaximumClanMembers,
            encodeClanMember
        )
        || !encodeVector(
            writer,
            snapshot.clanInvitations,
            runtimeInternetCommunityMaximumClanInvitations,
            encodeClan
        )
        || !encodeVector(
            writer,
            snapshot.clanLadder,
            runtimeInternetCommunityMaximumClanLadderEntries,
            encodeClan
        )) {
        return false;
    }
    return true;
}

bool decodeSnapshot(
    Reader& reader,
    RuntimeInternetCommunitySnapshot& snapshot
) {
    std::uint8_t registered = 0U;
    std::uint8_t clanMember = 0U;
    if (!reader.u8(registered) || registered > 1U
        || !decodeProfile(reader, snapshot.profile)
        || !reader.text(
            snapshot.selectedChannelId,
            runtimeInternetCommunityMaximumChannelIdBytes
        )
        || !decodeVector(
            reader,
            snapshot.channels,
            runtimeInternetCommunityMaximumChannels,
            decodeChannel
        )
        || !decodeVector(
            reader,
            snapshot.participants,
            runtimeInternetCommunityMaximumParticipants,
            decodeProfile
        )
        || !decodeVector(
            reader,
            snapshot.chatHistory,
            runtimeInternetCommunityMaximumChatHistory,
            decodeChat
        )
        || !decodeVector(
            reader,
            snapshot.friends,
            runtimeInternetCommunityMaximumFriends,
            decodeFriend
        )
        || !decodeVector(
            reader,
            snapshot.ladder,
            runtimeInternetCommunityMaximumLadderEntries,
            decodeLadder
        )
        || !decodeVector(
            reader,
            snapshot.news,
            runtimeInternetCommunityMaximumNewsItems,
            decodeNews
        )
        || !reader.u8(clanMember)
        || clanMember > 1U
        || !decodeClan(reader, snapshot.clan)
        || !decodeVector(
            reader,
            snapshot.clanMembers,
            runtimeInternetCommunityMaximumClanMembers,
            decodeClanMember
        )
        || !decodeVector(
            reader,
            snapshot.clanInvitations,
            runtimeInternetCommunityMaximumClanInvitations,
            decodeClan
        )
        || !decodeVector(
            reader,
            snapshot.clanLadder,
            runtimeInternetCommunityMaximumClanLadderEntries,
            decodeClan
        )) {
        return false;
    }
    snapshot.registeredProfile = registered != 0U;
    snapshot.clanMember = clanMember != 0U;
    return true;
}

bool validProfile(const RuntimeInternetCommunityProfile& value) {
    return (value.peerId.empty() || validToken(value.peerId, maximumIdentityBytes))
        && (value.displayName.empty() || validText(
            value.displayName,
            1U,
            runtimeInternetCommunityMaximumDisplayNameBytes
        ))
        && value.points >= 0;
}

bool validChat(const RuntimeInternetCommunityChatMessage& value) {
    return value.messageId != 0U
        && validToken(
            value.channelId,
            runtimeInternetCommunityMaximumChannelIdBytes
        )
        && validToken(value.senderPeerId, maximumIdentityBytes)
        && validText(
            value.senderDisplayName,
            1U,
            runtimeInternetCommunityMaximumDisplayNameBytes
        )
        && validText(
            value.text,
            1U,
            runtimeInternetCommunityMaximumChatBytes
        );
}

bool validClanTag(std::string_view value) {
    return value.size() >= 2U
        && value.size() <= runtimeInternetCommunityMaximumClanTagBytes
        && std::all_of(value.begin(), value.end(), [](unsigned char byte) {
            return (byte >= 'a' && byte <= 'z')
                || (byte >= 'A' && byte <= 'Z')
                || (byte >= '0' && byte <= '9');
        });
}

bool validClan(
    const RuntimeInternetCommunityClan& value,
    bool allowEmpty = false
) {
    if (value.clanId.empty()) {
        return allowEmpty
            && value.name.empty()
            && value.tag.empty()
            && value.ownerPeerId.empty()
            && value.createdAtMilliseconds == 0U
            && value.wins == 0U
            && value.losses == 0U
            && value.points == 1000
            && value.rank == 0U
            && value.memberCount == 0U;
    }
    return validToken(
            value.clanId,
            runtimeInternetCommunityMaximumClanIdBytes
        )
        && validText(
            value.name,
            1U,
            runtimeInternetCommunityMaximumClanNameBytes
        )
        && validClanTag(value.tag)
        && validToken(value.ownerPeerId, maximumIdentityBytes)
        && value.createdAtMilliseconds != 0U
        && value.points >= 0
        && value.rank != 0U
        && value.memberCount != 0U
        && value.memberCount <= runtimeInternetCommunityMaximumClanMembers;
}

bool validClanMember(const RuntimeInternetCommunityClanMember& value) {
    return validToken(value.peerId, maximumIdentityBytes)
        && validText(
            value.displayName,
            1U,
            runtimeInternetCommunityMaximumDisplayNameBytes
        )
        && value.joinedAtMilliseconds != 0U
        && value.points >= 0;
}

bool validSnapshot(const RuntimeInternetCommunitySnapshot& snapshot) {
    if (!validProfile(snapshot.profile)
        || (!snapshot.selectedChannelId.empty()
            && !validToken(
                snapshot.selectedChannelId,
                runtimeInternetCommunityMaximumChannelIdBytes
            ))
        || snapshot.channels.size() > runtimeInternetCommunityMaximumChannels
        || snapshot.participants.size()
            > runtimeInternetCommunityMaximumParticipants
        || snapshot.chatHistory.size()
            > runtimeInternetCommunityMaximumChatHistory
        || snapshot.friends.size() > runtimeInternetCommunityMaximumFriends
        || snapshot.ladder.size()
            > runtimeInternetCommunityMaximumLadderEntries
        || snapshot.news.size() > runtimeInternetCommunityMaximumNewsItems
        || snapshot.clanMembers.size()
            > runtimeInternetCommunityMaximumClanMembers
        || snapshot.clanInvitations.size()
            > runtimeInternetCommunityMaximumClanInvitations
        || snapshot.clanLadder.size()
            > runtimeInternetCommunityMaximumClanLadderEntries) {
        return false;
    }
    for (const auto& channel : snapshot.channels) {
        if (!validToken(
                channel.channelId,
                runtimeInternetCommunityMaximumChannelIdBytes
            )
            || !validText(
                channel.title,
                1U,
                runtimeInternetCommunityMaximumChannelTitleBytes
            )
            || !validText(
                channel.topic,
                1U,
                runtimeInternetCommunityMaximumChannelTopicBytes
            )) {
            return false;
        }
    }
    for (const auto& profile : snapshot.participants) {
        if (!validProfile(profile) || profile.peerId.empty()
            || profile.displayName.empty()) return false;
    }
    for (const auto& chat : snapshot.chatHistory) {
        if (!validChat(chat)) return false;
    }
    for (const auto& buddy : snapshot.friends) {
        if (!validToken(buddy.peerId, maximumIdentityBytes)
            || !validText(
                buddy.displayName,
                1U,
                runtimeInternetCommunityMaximumDisplayNameBytes
            )
            || buddy.points < 0) return false;
    }
    for (const auto& entry : snapshot.ladder) {
        if (entry.rank == 0U
            || !validToken(entry.peerId, maximumIdentityBytes)
            || !validText(
                entry.displayName,
                1U,
                runtimeInternetCommunityMaximumDisplayNameBytes
            )
            || entry.points < 0) return false;
    }
    for (const auto& news : snapshot.news) {
        if (!validToken(news.newsId, maximumNewsIdBytes)
            || !validText(news.title, 1U, maximumNewsTitleBytes)
            || !validText(news.body, 1U, maximumNewsBodyBytes, true)) {
            return false;
        }
    }
    if (snapshot.clanMember) {
        if (!validClan(snapshot.clan)
            || snapshot.clanMembers.empty()
            || snapshot.clan.memberCount != snapshot.clanMembers.size()) {
            return false;
        }
        std::set<std::string> memberIds;
        std::size_t owners = 0U;
        for (const auto& member : snapshot.clanMembers) {
            if (!validClanMember(member)
                || !memberIds.insert(member.peerId).second) {
                return false;
            }
            if (member.owner) ++owners;
        }
        if (owners != 1U
            || !memberIds.contains(snapshot.clan.ownerPeerId)) {
            return false;
        }
    } else if (!validClan(snapshot.clan, true)
        || !snapshot.clanMembers.empty()) {
        return false;
    }
    for (const auto& invitation : snapshot.clanInvitations) {
        if (!validClan(invitation)) return false;
    }
    for (const auto& clan : snapshot.clanLadder) {
        if (!validClan(clan)) return false;
    }
    return true;
}

std::string hexEncode(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

std::optional<std::string> hexDecode(std::string_view value) {
    if ((value.size() & 1U) != 0U) return std::nullopt;
    const auto digit = [](char byte) -> int {
        if (byte >= '0' && byte <= '9') return byte - '0';
        if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
        if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
        return -1;
    };
    std::string result;
    result.reserve(value.size() / 2U);
    for (std::size_t offset = 0U; offset < value.size(); offset += 2U) {
        const int high = digit(value[offset]);
        const int low = digit(value[offset + 1U]);
        if (high < 0 || low < 0) return std::nullopt;
        result.push_back(static_cast<char>((high << 4) | low));
    }
    return result;
}

template<typename T>
bool parseInteger(std::string_view text, T& value) {
    T candidate{};
    const auto parsed = std::from_chars(
        text.data(),
        text.data() + text.size(),
        candidate
    );
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return false;
    }
    value = candidate;
    return true;
}

std::vector<std::string> splitTabs(std::string_view line) {
    std::vector<std::string> values;
    std::size_t offset = 0U;
    while (offset <= line.size()) {
        const std::size_t next = line.find('\t', offset);
        values.emplace_back(line.substr(
            offset,
            next == std::string_view::npos
                ? line.size() - offset
                : next - offset
        ));
        if (next == std::string_view::npos) break;
        offset = next + 1U;
    }
    return values;
}

} // namespace

bool encodeRuntimeInternetCommunityRequest(
    const RuntimeInternetCommunityRequest& request,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (request.transactionId == 0U
        || !validToken(request.serviceId, maximumIdentityBytes)
        || !validToken(request.senderPeerId, maximumIdentityBytes)
        || (!request.displayName.empty() && !validText(
            request.displayName,
            1U,
            runtimeInternetCommunityMaximumDisplayNameBytes
        ))
        || (!request.channelId.empty() && !validToken(
            request.channelId,
            runtimeInternetCommunityMaximumChannelIdBytes
        ))
        || (!request.text.empty() && !validText(
            request.text,
            1U,
            runtimeInternetCommunityMaximumChatBytes
        ))
        || (!request.target.empty()
            && !(validToken(request.target, maximumIdentityBytes)
                || validText(
                    request.target,
                    1U,
                    runtimeInternetCommunityMaximumDisplayNameBytes
                )))
        || (!request.matchId.empty()
            && !validToken(request.matchId, maximumMatchIdBytes))
        || (!request.clanId.empty()
            && !validToken(
                request.clanId,
                runtimeInternetCommunityMaximumClanIdBytes
            ))
        || (!request.clanName.empty()
            && !validText(
                request.clanName,
                1U,
                runtimeInternetCommunityMaximumClanNameBytes
            ))
        || (!request.clanTag.empty() && !validClanTag(request.clanTag))) {
        return fail(errorMessage, "community request contains invalid fields");
    }
    const auto operationValue = static_cast<std::uint8_t>(request.operation);
    if (operationValue < static_cast<std::uint8_t>(
            RuntimeInternetCommunityOperation::Login)
        || operationValue > static_cast<std::uint8_t>(
            RuntimeInternetCommunityOperation::AbandonQuickMatch)) {
        return fail(errorMessage, "community request operation is invalid");
    }
    Writer writer;
    writer.u16(runtimeInternetCommunityWireVersion);
    writer.u8(communityRequestFrame);
    writer.u8(operationValue);
    writer.u64(request.transactionId);
    if (!writer.text(request.serviceId)
        || !writer.text(request.senderPeerId)
        || !writer.text(request.displayName)
        || !writer.text(request.channelId)
        || !writer.text(request.text)
        || !writer.text(request.target)
        || !writer.text(request.matchId)
        || !writer.text(request.clanId)
        || !writer.text(request.clanName)
        || !writer.text(request.clanTag)) {
        return fail(errorMessage, "community request text is too large");
    }
    writer.u8(static_cast<std::uint8_t>(request.quickMatchResult));
    if (writer.size() > runtimeInternetCommunityMaximumPacketBytes) {
        return fail(errorMessage, "community request exceeds packet limit");
    }
    RuntimeNetworkPacket candidate;
    candidate.type = runtimeInternetCommunityPacketType;
    candidate.payload = writer.take();
    packet = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool decodeRuntimeInternetCommunityRequest(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetCommunityRequest& request,
    std::string& errorMessage
) {
    if (packet.type != runtimeInternetCommunityPacketType
        || packet.payload.size() > runtimeInternetCommunityMaximumPacketBytes) {
        return fail(errorMessage, "packet is not a bounded community request");
    }
    Reader reader(packet.payload);
    std::uint16_t version = 0U;
    std::uint8_t frame = 0U;
    std::uint8_t operation = 0U;
    std::uint8_t quickResult = 0U;
    RuntimeInternetCommunityRequest candidate;
    if (!reader.u16(version) || version != runtimeInternetCommunityWireVersion
        || !reader.u8(frame) || frame != communityRequestFrame
        || !reader.u8(operation)
        || operation < static_cast<std::uint8_t>(
            RuntimeInternetCommunityOperation::Login)
        || operation > static_cast<std::uint8_t>(
            RuntimeInternetCommunityOperation::AbandonQuickMatch)
        || !reader.u64(candidate.transactionId)
        || !reader.text(candidate.serviceId, maximumIdentityBytes)
        || !reader.text(candidate.senderPeerId, maximumIdentityBytes)
        || !reader.text(
            candidate.displayName,
            runtimeInternetCommunityMaximumDisplayNameBytes
        )
        || !reader.text(
            candidate.channelId,
            runtimeInternetCommunityMaximumChannelIdBytes
        )
        || !reader.text(
            candidate.text,
            runtimeInternetCommunityMaximumChatBytes
        )
        || !reader.text(candidate.target, maximumIdentityBytes)
        || !reader.text(candidate.matchId, maximumMatchIdBytes)
        || !reader.text(
            candidate.clanId,
            runtimeInternetCommunityMaximumClanIdBytes
        )
        || !reader.text(
            candidate.clanName,
            runtimeInternetCommunityMaximumClanNameBytes
        )
        || !reader.text(
            candidate.clanTag,
            runtimeInternetCommunityMaximumClanTagBytes
        )
        || !reader.u8(quickResult)
        || quickResult > static_cast<std::uint8_t>(
            RuntimeInternetCommunityQuickMatchResult::Loss)
        || !reader.done()) {
        return fail(errorMessage, "community request payload is malformed");
    }
    candidate.operation = static_cast<RuntimeInternetCommunityOperation>(
        operation
    );
    candidate.quickMatchResult =
        static_cast<RuntimeInternetCommunityQuickMatchResult>(quickResult);
    RuntimeNetworkPacket canonical;
    std::string validationError;
    if (!encodeRuntimeInternetCommunityRequest(
            candidate,
            canonical,
            validationError
        )
        || canonical.payload != packet.payload) {
        return fail(errorMessage, "community request failed canonical validation");
    }
    request = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool encodeRuntimeInternetCommunityResponse(
    const RuntimeInternetCommunityResponse& response,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    const auto kindValue = static_cast<std::uint8_t>(response.kind);
    if (response.transactionId == 0U
        || kindValue < static_cast<std::uint8_t>(
            RuntimeInternetCommunityResponseKind::Snapshot)
        || kindValue > static_cast<std::uint8_t>(
            RuntimeInternetCommunityResponseKind::MatchHistory)
        || !validToken(response.serviceId, maximumIdentityBytes)
        || response.senderPeerId != response.serviceId
        || !validToken(response.targetPeerId, maximumIdentityBytes)
        || (!response.status.empty() && !validText(
            response.status,
            1U,
            runtimeInternetCommunityMaximumStatusBytes
        ))
        || (response.kind == RuntimeInternetCommunityResponseKind::Snapshot
            && !validSnapshot(response.snapshot))
        || (response.kind
                == RuntimeInternetCommunityResponseKind::ChannelMessage
            && !validChat(response.channelMessage))
        || (response.kind == RuntimeInternetCommunityResponseKind::MatchHistory && !validMatchHistory(response))) {
        return fail(errorMessage, "community response contains invalid fields");
    }
    Writer writer;
    writer.u16(runtimeInternetCommunityWireVersion);
    writer.u8(communityResponseFrame);
    writer.u8(kindValue);
    writer.u64(response.transactionId);
    if (!writer.text(response.serviceId)
        || !writer.text(response.senderPeerId)
        || !writer.text(response.targetPeerId)
        || !writer.text(response.status)) {
        return fail(errorMessage, "community response text is too large");
    }
    if (response.kind == RuntimeInternetCommunityResponseKind::Snapshot
        && !encodeSnapshot(writer, response.snapshot)) {
        return fail(errorMessage, "community snapshot exceeds collection limits");
    }
    if (response.kind
            == RuntimeInternetCommunityResponseKind::ChannelMessage
        && !encodeChat(writer, response.channelMessage)) {
        return fail(errorMessage, "community chat response is invalid");
    }
    if (response.kind == RuntimeInternetCommunityResponseKind::MatchHistory
        && !encodeVector(writer, response.matchHistory, runtimeInternetCommunityMaximumMatchHistory, encodeRankedRecord))
        return fail(errorMessage, "ranked history response is invalid");
    if (writer.size() > runtimeInternetCommunityMaximumPacketBytes) {
        return fail(errorMessage, "community response exceeds packet limit");
    }
    RuntimeNetworkPacket candidate;
    candidate.type = runtimeInternetCommunityPacketType;
    candidate.payload = writer.take();
    packet = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool decodeRuntimeInternetCommunityResponse(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetCommunityResponse& response,
    std::string& errorMessage
) {
    if (packet.type != runtimeInternetCommunityPacketType
        || packet.payload.size() > runtimeInternetCommunityMaximumPacketBytes) {
        return fail(errorMessage, "packet is not a bounded community response");
    }
    Reader reader(packet.payload);
    std::uint16_t version = 0U;
    std::uint8_t frame = 0U;
    std::uint8_t kind = 0U;
    RuntimeInternetCommunityResponse candidate;
    if (!reader.u16(version) || version != runtimeInternetCommunityWireVersion
        || !reader.u8(frame) || frame != communityResponseFrame
        || !reader.u8(kind)
        || kind < static_cast<std::uint8_t>(
            RuntimeInternetCommunityResponseKind::Snapshot)
        || kind > static_cast<std::uint8_t>(
            RuntimeInternetCommunityResponseKind::MatchHistory)
        || !reader.u64(candidate.transactionId)
        || !reader.text(candidate.serviceId, maximumIdentityBytes)
        || !reader.text(candidate.senderPeerId, maximumIdentityBytes)
        || !reader.text(candidate.targetPeerId, maximumIdentityBytes)
        || !reader.text(
            candidate.status,
            runtimeInternetCommunityMaximumStatusBytes
        )) {
        return fail(errorMessage, "community response payload is malformed");
    }
    candidate.kind = static_cast<RuntimeInternetCommunityResponseKind>(kind);
    if (candidate.kind == RuntimeInternetCommunityResponseKind::Snapshot
        && !decodeSnapshot(reader, candidate.snapshot)) {
        return fail(errorMessage, "community snapshot payload is malformed");
    }
    if (candidate.kind
            == RuntimeInternetCommunityResponseKind::ChannelMessage
        && !decodeChat(reader, candidate.channelMessage)) {
        return fail(errorMessage, "community chat payload is malformed");
    }
    if (candidate.kind == RuntimeInternetCommunityResponseKind::MatchHistory
        && !decodeVector(reader, candidate.matchHistory, runtimeInternetCommunityMaximumMatchHistory, decodeRankedRecord))
        return fail(errorMessage, "ranked history payload is malformed");
    if (!reader.done()) {
        return fail(errorMessage, "community response has trailing bytes");
    }
    RuntimeNetworkPacket canonical;
    std::string validationError;
    if (!encodeRuntimeInternetCommunityResponse(
            candidate,
            canonical,
            validationError
        )
        || canonical.payload != packet.payload) {
        return fail(errorMessage, "community response failed canonical validation");
    }
    response = std::move(candidate);
    errorMessage.clear();
    return true;
}

struct RuntimeInternetCommunityService::Impl {
    struct Account {
        RuntimeInternetCommunityProfile profile;
        std::set<std::string> friends;
        std::string channelId;
        std::string clanId;
        std::uint64_t clanJoinedAtMilliseconds = 0U;
        std::set<std::string> clanInvitations;
    };

    struct ClanState {
        RuntimeInternetCommunityClan projection;
        std::set<std::string> members;
    };

    struct ChannelState {
        RuntimeInternetCommunityChannel projection;
        std::set<std::string> members;
        std::deque<RuntimeInternetCommunityChatMessage> history;
    };

    using AuthorizedQuickMatch = RuntimeInternetRankedMatch;

    RuntimeInternetCommunityServiceConfig config;
    std::map<std::string, Account> accounts;
    std::map<std::string, ClanState> clans;
    std::map<std::string, ChannelState> channels;
    std::vector<std::string> channelOrder;
    std::set<std::string> online;
    std::map<std::string, std::deque<std::uint64_t>> replayTransactions;
    std::map<std::string, AuthorizedQuickMatch> authorizedMatches;
    std::deque<std::string> authorizedMatchOrder;
    std::uint64_t nextMessageId = 1U;
    bool opened = false;

    std::uint64_t now() const { return config.nowMilliseconds(); }
    std::uint64_t utc() const { return config.utcMilliseconds(); }

    void initializeChannels() {
        channels.clear();
        channelOrder = {"lobby", "ra2", "yr", "tournament"};
        const std::array<RuntimeInternetCommunityChannel, 4U> defaults{{
            {"lobby", "Command & Conquer", "General community lobby", 0U},
            {"ra2", "Red Alert 2", "Red Alert 2 custom matches", 0U},
            {"yr", "Yuri's Revenge", "Yuri's Revenge custom matches", 0U},
            {"tournament", "Tournament", "Quick Match ladder discussion", 0U}
        }};
        for (const auto& channel : defaults) {
            ChannelState state;
            state.projection = channel;
            channels.emplace(channel.channelId, std::move(state));
        }
    }

    void initializeNews() {
        if (!config.news.empty()) return;
        const std::uint64_t published = now();
        config.news = {
            {
                "welcome",
                published,
                "Welcome to Concord Online",
                "The self-hosted Westwood Online community shell is ready. "
                "Create a profile, join a channel, find buddies, and launch "
                "a custom or Quick Match from the Games page."
            },
            {
                "ladder",
                published,
                "Quick Match Ladder",
                "Only service-authorized Quick Matches affect wins, losses, "
                "points, and ladder rank. Both authenticated players must "
                "report opposite results before standings change."
            },
            {
                "security",
                published,
                "Community Identity",
                "Public handles are profiles attached to authenticated TLS "
                "peer identities. Chat, Buddy List, and ladder updates cannot "
                "claim another account's identity."
            }
        };
    }

    std::vector<const Account*> rankedAccounts() const {
        std::vector<const Account*> result;
        result.reserve(accounts.size());
        for (const auto& entry : accounts) result.push_back(&entry.second);
        std::sort(result.begin(), result.end(), [](const Account* left,
                                                   const Account* right) {
            return std::tuple{
                -left->profile.points,
                -static_cast<std::int64_t>(left->profile.wins),
                left->profile.losses,
                lowerAscii(left->profile.displayName),
                left->profile.peerId
            } < std::tuple{
                -right->profile.points,
                -static_cast<std::int64_t>(right->profile.wins),
                right->profile.losses,
                lowerAscii(right->profile.displayName),
                right->profile.peerId
            };
        });
        return result;
    }

    std::uint32_t rankFor(
        std::string_view peerId,
        const std::vector<const Account*>& ranked
    ) const {
        const auto found = std::find_if(
            ranked.begin(),
            ranked.end(),
            [&](const Account* account) {
                return account->profile.peerId == peerId;
            }
        );
        return found == ranked.end()
            ? 0U
            : static_cast<std::uint32_t>(
                std::distance(ranked.begin(), found) + 1
            );
    }

    std::vector<const ClanState*> rankedClans() const {
        std::vector<const ClanState*> result;
        result.reserve(clans.size());
        for (const auto& entry : clans) result.push_back(&entry.second);
        std::sort(result.begin(), result.end(), [](const ClanState* left,
                                                   const ClanState* right) {
            return std::tuple{
                -static_cast<std::int64_t>(left->projection.points),
                -static_cast<std::int64_t>(left->projection.wins),
                left->projection.losses,
                lowerAscii(left->projection.name),
                left->projection.clanId
            } < std::tuple{
                -static_cast<std::int64_t>(right->projection.points),
                -static_cast<std::int64_t>(right->projection.wins),
                right->projection.losses,
                lowerAscii(right->projection.name),
                right->projection.clanId
            };
        });
        return result;
    }

    std::uint32_t clanRankFor(
        std::string_view clanId,
        const std::vector<const ClanState*>& ranked
    ) const {
        const auto found = std::find_if(
            ranked.begin(),
            ranked.end(),
            [&](const ClanState* clan) {
                return clan->projection.clanId == clanId;
            }
        );
        return found == ranked.end()
            ? 0U
            : static_cast<std::uint32_t>(
                std::distance(ranked.begin(), found) + 1
            );
    }

    RuntimeInternetCommunityClan projectClan(
        const ClanState& clan,
        const std::vector<const ClanState*>& ranked
    ) const {
        RuntimeInternetCommunityClan projection = clan.projection;
        projection.rank = clanRankFor(projection.clanId, ranked);
        projection.memberCount = static_cast<std::uint32_t>(
            clan.members.size()
        );
        return projection;
    }

    RuntimeInternetCommunitySnapshot snapshotFor(
        std::string_view peerId
    ) const {
        RuntimeInternetCommunitySnapshot snapshot;
        const std::vector<const Account*> ranked = rankedAccounts();
        const std::vector<const ClanState*> rankedClanList = rankedClans();
        const auto accountIterator = accounts.find(std::string(peerId));
        if (accountIterator != accounts.end()) {
            snapshot.registeredProfile = true;
            snapshot.profile = accountIterator->second.profile;
            snapshot.profile.online = online.contains(std::string(peerId));
            snapshot.profile.rank = rankFor(peerId, ranked);
            snapshot.selectedChannelId = accountIterator->second.channelId;
        }

        for (const std::string& channelId : channelOrder) {
            const auto found = channels.find(channelId);
            if (found == channels.end()) continue;
            RuntimeInternetCommunityChannel channel = found->second.projection;
            channel.participantCount = static_cast<std::uint32_t>(
                found->second.members.size()
            );
            snapshot.channels.push_back(std::move(channel));
        }

        if (accountIterator != accounts.end()) {
            const auto selected = channels.find(
                accountIterator->second.channelId
            );
            if (selected != channels.end()) {
                for (const std::string& memberId : selected->second.members) {
                    const auto member = accounts.find(memberId);
                    if (member == accounts.end() || !online.contains(memberId)) {
                        continue;
                    }
                    RuntimeInternetCommunityProfile profile =
                        member->second.profile;
                    profile.online = true;
                    profile.rank = rankFor(memberId, ranked);
                    snapshot.participants.push_back(std::move(profile));
                    if (snapshot.participants.size()
                        >= runtimeInternetCommunityMaximumParticipants) break;
                }
                snapshot.chatHistory.assign(
                    selected->second.history.begin(),
                    selected->second.history.end()
                );
            }
            for (const std::string& friendId : accountIterator->second.friends) {
                const auto buddy = accounts.find(friendId);
                if (buddy == accounts.end()) continue;
                RuntimeInternetCommunityFriend projection;
                projection.peerId = buddy->second.profile.peerId;
                projection.displayName = buddy->second.profile.displayName;
                projection.points = buddy->second.profile.points;
                projection.rank = rankFor(friendId, ranked);
                projection.online = online.contains(friendId);
                snapshot.friends.push_back(std::move(projection));
            }
            if (!accountIterator->second.clanId.empty()) {
                const auto clan = clans.find(accountIterator->second.clanId);
                if (clan != clans.end()) {
                    snapshot.clanMember = true;
                    snapshot.clan = projectClan(clan->second, rankedClanList);
                    for (const std::string& memberId : clan->second.members) {
                        const auto member = accounts.find(memberId);
                        if (member == accounts.end()) continue;
                        RuntimeInternetCommunityClanMember projection;
                        projection.peerId = memberId;
                        projection.displayName =
                            member->second.profile.displayName;
                        projection.joinedAtMilliseconds =
                            member->second.clanJoinedAtMilliseconds;
                        projection.wins = member->second.profile.wins;
                        projection.losses = member->second.profile.losses;
                        projection.disconnects =
                            member->second.profile.disconnects;
                        projection.points = member->second.profile.points;
                        projection.owner = memberId
                            == clan->second.projection.ownerPeerId;
                        projection.online = online.contains(memberId);
                        snapshot.clanMembers.push_back(std::move(projection));
                    }
                    std::sort(
                        snapshot.clanMembers.begin(),
                        snapshot.clanMembers.end(),
                        [](const RuntimeInternetCommunityClanMember& left,
                           const RuntimeInternetCommunityClanMember& right) {
                            return std::tuple{
                                !left.owner,
                                lowerAscii(left.displayName),
                                left.peerId
                            } < std::tuple{
                                !right.owner,
                                lowerAscii(right.displayName),
                                right.peerId
                            };
                        }
                    );
                }
            } else {
                for (const std::string& clanId :
                        accountIterator->second.clanInvitations) {
                    const auto clan = clans.find(clanId);
                    if (clan == clans.end()) continue;
                    snapshot.clanInvitations.push_back(
                        projectClan(clan->second, rankedClanList)
                    );
                    if (snapshot.clanInvitations.size()
                        >= runtimeInternetCommunityMaximumClanInvitations) {
                        break;
                    }
                }
            }
        }

        const std::size_t ladderCount = std::min(
            ranked.size(),
            runtimeInternetCommunityMaximumLadderEntries
        );
        for (std::size_t index = 0U; index < ladderCount; ++index) {
            const Account& account = *ranked[index];
            RuntimeInternetCommunityLadderEntry entry;
            entry.rank = static_cast<std::uint32_t>(index + 1U);
            entry.peerId = account.profile.peerId;
            entry.displayName = account.profile.displayName;
            entry.points = account.profile.points;
            entry.wins = account.profile.wins;
            entry.losses = account.profile.losses;
            entry.disconnects = account.profile.disconnects;
            entry.online = online.contains(entry.peerId);
            snapshot.ladder.push_back(std::move(entry));
        }
        snapshot.news = config.news;
        const std::size_t clanLadderCount = std::min(
            rankedClanList.size(),
            runtimeInternetCommunityMaximumClanLadderEntries
        );
        for (std::size_t index = 0U;
            index < clanLadderCount;
            ++index) {
            snapshot.clanLadder.push_back(
                projectClan(*rankedClanList[index], rankedClanList)
            );
        }
        return snapshot;
    }

    bool queueResponse(
        RuntimeInternetCommunityResponse response,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) const {
        RuntimeNetworkPacket packet;
        if (!encodeRuntimeInternetCommunityResponse(
                response,
                packet,
                errorMessage
            )) {
            return false;
        }
        actions.push_back({response.targetPeerId, std::move(packet)});
        return true;
    }

    bool queueSnapshot(
        std::string_view peerId,
        std::uint64_t transactionId,
        std::string status,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) const {
        RuntimeInternetCommunityResponse response;
        response.kind = RuntimeInternetCommunityResponseKind::Snapshot;
        response.transactionId = transactionId;
        response.serviceId = config.serviceId;
        response.senderPeerId = config.serviceId;
        response.targetPeerId = std::string(peerId);
        response.status = std::move(status);
        response.snapshot = snapshotFor(peerId);
        return queueResponse(std::move(response), actions, errorMessage);
    }

    bool queueError(
        std::string_view peerId,
        std::uint64_t transactionId,
        std::string reason,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) const {
        RuntimeInternetCommunityResponse response;
        response.kind = RuntimeInternetCommunityResponseKind::Error;
        response.transactionId = transactionId;
        response.serviceId = config.serviceId;
        response.senderPeerId = config.serviceId;
        response.targetPeerId = std::string(peerId);
        response.status = std::move(reason);
        return queueResponse(std::move(response), actions, errorMessage);
    }

    bool queueBuddyPresenceSnapshots(
        std::string_view subjectPeerId,
        std::uint64_t transactionId,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) const {
        for (const auto& entry : accounts) {
            if (!online.contains(entry.first)
                || !entry.second.friends.contains(
                    std::string(subjectPeerId)
                )) {
                continue;
            }
            if (!queueSnapshot(
                    entry.first,
                    transactionId,
                    "Buddy presence updated.",
                    actions,
                    errorMessage
                )) {
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> resolveAccount(std::string_view target) const {
        if (const auto direct = accounts.find(std::string(target));
            direct != accounts.end()) {
            return direct->first;
        }
        const std::string folded = lowerAscii(target);
        for (const auto& entry : accounts) {
            if (lowerAscii(entry.second.profile.displayName) == folded) {
                return entry.first;
            }
        }
        return std::nullopt;
    }

    bool displayNameAvailable(
        std::string_view displayName,
        std::string_view excludingPeer = {}
    ) const {
        const std::string folded = lowerAscii(displayName);
        return std::none_of(
            accounts.begin(),
            accounts.end(),
            [&](const auto& entry) {
                return entry.first != excludingPeer
                    && lowerAscii(entry.second.profile.displayName) == folded;
            }
        );
    }

    bool clanNameAvailable(
        std::string_view name,
        std::string_view excludingClan = {}
    ) const {
        const std::string folded = lowerAscii(name);
        return std::none_of(
            clans.begin(),
            clans.end(),
            [&](const auto& entry) {
                return entry.first != excludingClan
                    && lowerAscii(entry.second.projection.name) == folded;
            }
        );
    }

    bool clanTagAvailable(
        std::string_view tag,
        std::string_view excludingClan = {}
    ) const {
        const std::string folded = lowerAscii(tag);
        return std::none_of(
            clans.begin(),
            clans.end(),
            [&](const auto& entry) {
                return entry.first != excludingClan
                    && lowerAscii(entry.second.projection.tag) == folded;
            }
        );
    }

    void leaveChannel(Account& account) {
        if (account.channelId.empty()) return;
        const auto channel = channels.find(account.channelId);
        if (channel != channels.end()) {
            channel->second.members.erase(account.profile.peerId);
        }
    }

    void enterChannel(Account& account, const std::string& channelId) {
        leaveChannel(account);
        account.channelId = channelId;
        channels[channelId].members.insert(account.profile.peerId);
    }

    bool replayed(std::string_view peerId, std::uint64_t transactionId) {
        auto& transactions = replayTransactions[std::string(peerId)];
        if (std::find(
                transactions.begin(),
                transactions.end(),
                transactionId
            ) != transactions.end()) {
            return true;
        }
        if (transactions.size()
            >= config.maximumReplayTransactionsPerPeer) {
            transactions.pop_front();
        }
        transactions.push_back(transactionId);
        return false;
    }

    bool stateNeedsUpgrade = false;
    bool loadState(std::string& errorMessage) {
        stateNeedsUpgrade = false;
        if (config.statePath.empty()) return true;
        std::error_code filesystemError;
        if (!std::filesystem::exists(config.statePath, filesystemError)) {
            if (filesystemError) {
                return fail(
                    errorMessage,
                    "could not inspect community state file: "
                        + filesystemError.message()
                );
            }
            return true;
        }
        std::ifstream input(config.statePath, std::ios::binary);
        if (!input) {
            return fail(errorMessage, "could not open community state file");
        }
        if (!verifyRuntimeInternetCommunityState(input, stateNeedsUpgrade, errorMessage)) return false;
        std::string line;
        if (!std::getline(input, line)
            || (line != "CONCORD_WOL_COMMUNITY_V1"
                && line != "CONCORD_WOL_COMMUNITY_V2"
                && line != "CONCORD_WOL_COMMUNITY_V3"
                && line != "CONCORD_WOL_COMMUNITY_V4")) {
            return fail(errorMessage, "community state header is invalid");
        }
        const bool clanStateSupported = line != "CONCORD_WOL_COMMUNITY_V1";
        const bool sealedState = line == "CONCORD_WOL_COMMUNITY_V4";
        const bool rankedStateSupported = line == "CONCORD_WOL_COMMUNITY_V3" || sealedState;
        std::vector<std::pair<std::string, std::string>> pendingFriends;
        std::vector<std::tuple<std::string, std::string, std::uint64_t>>
            pendingClanMembers;
        std::vector<std::pair<std::string, std::string>>
            pendingClanInvitations;
        std::size_t lineNumber = 1U;
        while (std::getline(input, line)) {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            const std::vector<std::string> fields = splitTabs(line);
            if (sealedState && fields.front() == "Z") continue; // Already verified before any state was loaded.
            if (fields.front() == "R") {
                if (!rankedStateSupported || !loadRankedRecord(fields, errorMessage)) return false;
                continue;
            }
            if (fields.empty()) continue;
            if (fields[0] == "A") {
                if (fields.size() != 9U
                    || accounts.size() >= config.maximumAccounts) {
                    return fail(
                        errorMessage,
                        "invalid account record in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                const auto peer = hexDecode(fields[1]);
                const auto display = hexDecode(fields[2]);
                Account account;
                if (!peer || !display
                    || !validToken(*peer, maximumIdentityBytes)
                    || !validText(
                        *display,
                        1U,
                        runtimeInternetCommunityMaximumDisplayNameBytes
                    )
                    || !parseInteger(
                        fields[3],
                        account.profile.registeredAtMilliseconds
                    )
                    || !parseInteger(
                        fields[4],
                        account.profile.lastSeenMilliseconds
                    )
                    || !parseInteger(fields[5], account.profile.wins)
                    || !parseInteger(fields[6], account.profile.losses)
                    || !parseInteger(fields[7], account.profile.disconnects)
                    || !parseInteger(fields[8], account.profile.points)
                    || account.profile.points < 0
                    || accounts.contains(*peer)
                    || !displayNameAvailable(*display)) {
                    return fail(
                        errorMessage,
                        "invalid account value in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                account.profile.peerId = *peer;
                account.profile.displayName = *display;
                account.channelId = "lobby";
                accounts.emplace(*peer, std::move(account));
            } else if (fields[0] == "F") {
                if (fields.size() != 3U) {
                    return fail(
                        errorMessage,
                        "invalid Buddy record in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                const auto owner = hexDecode(fields[1]);
                const auto buddy = hexDecode(fields[2]);
                if (!owner || !buddy) {
                    return fail(
                        errorMessage,
                        "invalid Buddy encoding in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                pendingFriends.emplace_back(*owner, *buddy);
            } else if (fields[0] == "C") {
                if (!clanStateSupported
                    || fields.size() != 9U
                    || clans.size() >= config.maximumClans) {
                    return fail(
                        errorMessage,
                        "invalid Clan record in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                const auto clanId = hexDecode(fields[1]);
                const auto name = hexDecode(fields[2]);
                const auto tag = hexDecode(fields[3]);
                const auto owner = hexDecode(fields[4]);
                ClanState clan;
                if (!clanId || !name || !tag || !owner
                    || !validToken(
                        *clanId,
                        runtimeInternetCommunityMaximumClanIdBytes
                    )
                    || !validText(
                        *name,
                        1U,
                        runtimeInternetCommunityMaximumClanNameBytes
                    )
                    || !validClanTag(*tag)
                    || lowerAscii(*tag) != *clanId
                    || !validToken(*owner, maximumIdentityBytes)
                    || !parseInteger(
                        fields[5],
                        clan.projection.createdAtMilliseconds
                    )
                    || clan.projection.createdAtMilliseconds == 0U
                    || !parseInteger(fields[6], clan.projection.wins)
                    || !parseInteger(fields[7], clan.projection.losses)
                    || !parseInteger(fields[8], clan.projection.points)
                    || clan.projection.points < 0
                    || clans.contains(*clanId)
                    || !clanNameAvailable(*name)
                    || !clanTagAvailable(*tag)) {
                    return fail(
                        errorMessage,
                        "invalid Clan value in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                clan.projection.clanId = *clanId;
                clan.projection.name = *name;
                clan.projection.tag = *tag;
                clan.projection.ownerPeerId = *owner;
                clans.emplace(*clanId, std::move(clan));
            } else if (fields[0] == "M") {
                if (!clanStateSupported || fields.size() != 4U) {
                    return fail(
                        errorMessage,
                        "invalid Clan member record in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                const auto clanId = hexDecode(fields[1]);
                const auto member = hexDecode(fields[2]);
                std::uint64_t joinedAt = 0U;
                if (!clanId || !member
                    || !parseInteger(fields[3], joinedAt)
                    || joinedAt == 0U) {
                    return fail(
                        errorMessage,
                        "invalid Clan member value in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                pendingClanMembers.emplace_back(
                    *clanId,
                    *member,
                    joinedAt
                );
            } else if (fields[0] == "I") {
                if (!clanStateSupported || fields.size() != 3U) {
                    return fail(
                        errorMessage,
                        "invalid Clan invitation record in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                const auto clanId = hexDecode(fields[1]);
                const auto invitee = hexDecode(fields[2]);
                if (!clanId || !invitee) {
                    return fail(
                        errorMessage,
                        "invalid Clan invitation value in community state at line "
                            + std::to_string(lineNumber)
                    );
                }
                pendingClanInvitations.emplace_back(*clanId, *invitee);
            } else {
                return fail(
                    errorMessage,
                    "unknown community state record at line "
                        + std::to_string(lineNumber)
                );
            }
        }
        if (!input.eof()) {
            return fail(errorMessage, "community state could not be read");
        }
        for (const auto& relation : pendingFriends) {
            const auto owner = accounts.find(relation.first);
            if (owner == accounts.end()
                || !accounts.contains(relation.second)
                || relation.first == relation.second
                || owner->second.friends.size()
                    >= runtimeInternetCommunityMaximumFriends) {
                return fail(errorMessage, "community state Buddy relation is invalid");
            }
            owner->second.friends.insert(relation.second);
        }
        for (const auto& relation : pendingClanMembers) {
            const std::string& clanId = std::get<0>(relation);
            const std::string& peerId = std::get<1>(relation);
            const auto clan = clans.find(clanId);
            const auto account = accounts.find(peerId);
            if (clan == clans.end()
                || account == accounts.end()
                || !account->second.clanId.empty()
                || clan->second.members.size()
                    >= runtimeInternetCommunityMaximumClanMembers
                || !clan->second.members.insert(peerId).second) {
                return fail(
                    errorMessage,
                    "community state Clan membership is invalid"
                );
            }
            account->second.clanId = clanId;
            account->second.clanJoinedAtMilliseconds = std::get<2>(relation);
        }
        for (const auto& entry : clans) {
            if (entry.second.members.empty()
                || !entry.second.members.contains(
                    entry.second.projection.ownerPeerId
                )) {
                return fail(
                    errorMessage,
                    "community state Clan owner is not a member"
                );
            }
        }
        for (const auto& invitation : pendingClanInvitations) {
            const auto clan = clans.find(invitation.first);
            const auto account = accounts.find(invitation.second);
            if (clan == clans.end()
                || account == accounts.end()
                || !account->second.clanId.empty()
                || clan->second.members.contains(invitation.second)
                || account->second.clanInvitations.size()
                    >= runtimeInternetCommunityMaximumClanInvitations
                || !account->second.clanInvitations
                    .insert(invitation.first).second) {
                return fail(
                    errorMessage,
                    "community state Clan invitation is invalid"
                );
            }
        }
        return true;
    }

    bool saveState(std::string& errorMessage) const {
        if (config.statePath.empty()) return true;
        return runtimeWriteFileAtomically(config.statePath, [&](std::ostream& destination, std::string& writerError) {
            RuntimeInternetCommunityStateBuffer buffer(destination);
            std::ostream output(&buffer);
            output << "CONCORD_WOL_COMMUNITY_V4\n";
            for (const auto& entry : accounts) {
                const RuntimeInternetCommunityProfile& profile =
                    entry.second.profile;
                output << "A\t" << hexEncode(profile.peerId)
                       << '\t' << hexEncode(profile.displayName)
                       << '\t' << profile.registeredAtMilliseconds
                       << '\t' << profile.lastSeenMilliseconds
                       << '\t' << profile.wins
                       << '\t' << profile.losses
                       << '\t' << profile.disconnects
                       << '\t' << profile.points << '\n';
            }
            for (const auto& entry : accounts) {
                for (const std::string& buddy : entry.second.friends) {
                    output << "F\t" << hexEncode(entry.first)
                           << '\t' << hexEncode(buddy) << '\n';
                }
            }
            for (const auto& entry : clans) {
                const RuntimeInternetCommunityClan& clan =
                    entry.second.projection;
                output << "C\t" << hexEncode(clan.clanId)
                       << '\t' << hexEncode(clan.name)
                       << '\t' << hexEncode(clan.tag)
                       << '\t' << hexEncode(clan.ownerPeerId)
                       << '\t' << clan.createdAtMilliseconds
                       << '\t' << clan.wins
                       << '\t' << clan.losses
                       << '\t' << clan.points << '\n';
                for (const std::string& member : entry.second.members) {
                    const auto account = accounts.find(member);
                    if (account == accounts.end()) continue;
                    output << "M\t" << hexEncode(clan.clanId)
                           << '\t' << hexEncode(member)
                           << '\t'
                           << account->second.clanJoinedAtMilliseconds
                           << '\n';
                }
            }
            for (const auto& entry : accounts) {
                for (const std::string& clanId :
                        entry.second.clanInvitations) {
                    output << "I\t" << hexEncode(clanId)
                           << '\t' << hexEncode(entry.first) << '\n';
                }
            }
            if (!writeRankedRecords(output, writerError)) return false;
            output.flush();
            if (!output) return fail(writerError, "community state writer exceeded its bound or failed");
            return buffer.finish(destination, writerError);
        }, errorMessage);
    }

    bool handleRegister(
        const RuntimeInternetCommunityRequest& request,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        if (!validText(
                request.displayName,
                1U,
                runtimeInternetCommunityMaximumDisplayNameBytes
            )) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Profile handle must be 1-24 UTF-8 bytes without control characters.",
                actions,
                errorMessage
            );
        }
        if (const auto existing = accounts.find(request.senderPeerId);
            existing != accounts.end()) {
            if (existing->second.profile.displayName != request.displayName) {
                return queueError(
                    request.senderPeerId,
                    request.transactionId,
                    "This authenticated account already owns a different profile handle.",
                    actions,
                    errorMessage
                );
            }
            return queueSnapshot(
                request.senderPeerId,
                request.transactionId,
                "Existing Westwood Online profile signed in.",
                actions,
                errorMessage
            );
        }
        if (accounts.size() >= config.maximumAccounts) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "The community account capacity has been reached.",
                actions,
                errorMessage
            );
        }
        if (!displayNameAvailable(request.displayName)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "That profile handle is already registered.",
                actions,
                errorMessage
            );
        }
        Account account;
        account.profile.peerId = request.senderPeerId;
        account.profile.displayName = request.displayName;
        account.profile.registeredAtMilliseconds = now();
        account.profile.lastSeenMilliseconds = account.profile.registeredAtMilliseconds;
        account.profile.points = 1000;
        account.profile.online = true;
        account.channelId = "lobby";
        accounts.emplace(request.senderPeerId, account);
        channels["lobby"].members.insert(request.senderPeerId);
        online.insert(request.senderPeerId);
        if (!saveState(errorMessage)) {
            channels["lobby"].members.erase(request.senderPeerId);
            accounts.erase(request.senderPeerId);
            return false;
        }
        return queueSnapshot(
            request.senderPeerId,
            request.transactionId,
            "Westwood Online profile registered.",
            actions,
            errorMessage
        );
    }

    bool handleJoinChannel(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        if (!channels.contains(request.channelId)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "The selected chat channel does not exist.",
                actions,
                errorMessage
            );
        }
        enterChannel(account, request.channelId);
        return queueSnapshot(
            request.senderPeerId,
            request.transactionId,
            "Joined " + channels[request.channelId].projection.title + ".",
            actions,
            errorMessage
        );
    }

    bool handleChat(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        if (!validText(
                request.text,
                1U,
                runtimeInternetCommunityMaximumChatBytes
            )
            || account.channelId.empty()
            || !channels.contains(account.channelId)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Chat text or selected channel is invalid.",
                actions,
                errorMessage
            );
        }
        RuntimeInternetCommunityChatMessage message;
        message.messageId = nextMessageId++;
        if (nextMessageId == 0U) nextMessageId = 1U;
        message.sentAtMilliseconds = now();
        message.channelId = account.channelId;
        message.senderPeerId = account.profile.peerId;
        message.senderDisplayName = account.profile.displayName;
        message.text = request.text;
        ChannelState& channel = channels[account.channelId];
        if (channel.history.size()
            >= runtimeInternetCommunityMaximumChatHistory) {
            channel.history.pop_front();
        }
        channel.history.push_back(message);
        for (const std::string& member : channel.members) {
            if (!online.contains(member)) continue;
            RuntimeInternetCommunityResponse response;
            response.kind = RuntimeInternetCommunityResponseKind::ChannelMessage;
            response.transactionId = request.transactionId;
            response.serviceId = config.serviceId;
            response.senderPeerId = config.serviceId;
            response.targetPeerId = member;
            response.status = "Channel message received.";
            response.channelMessage = message;
            if (!queueResponse(std::move(response), actions, errorMessage)) {
                return false;
            }
        }
        return true;
    }

    bool handleAddFriend(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        const auto resolved = resolveAccount(request.target);
        if (!resolved.has_value()) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "No registered profile matches that Buddy handle.",
                actions,
                errorMessage
            );
        }
        if (*resolved == request.senderPeerId) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "A profile cannot add itself to its Buddy List.",
                actions,
                errorMessage
            );
        }
        if (!account.friends.contains(*resolved)
            && account.friends.size()
                >= runtimeInternetCommunityMaximumFriends) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "The Buddy List is full.",
                actions,
                errorMessage
            );
        }
        const bool inserted = account.friends.insert(*resolved).second;
        if (inserted && !saveState(errorMessage)) {
            account.friends.erase(*resolved);
            return false;
        }
        return queueSnapshot(
            request.senderPeerId,
            request.transactionId,
            inserted ? "Buddy added." : "That profile is already a Buddy.",
            actions,
            errorMessage
        );
    }

    bool handleRemoveFriend(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        const bool removed = account.friends.erase(request.target) != 0U;
        if (removed && !saveState(errorMessage)) {
            account.friends.insert(request.target);
            return false;
        }
        return queueSnapshot(
            request.senderPeerId,
            request.transactionId,
            removed ? "Buddy removed." : "That profile is not on the Buddy List.",
            actions,
            errorMessage
        );
    }

    bool queueSnapshotsForPeers(
        const std::set<std::string>& peerIds,
        std::uint64_t transactionId,
        const std::string& status,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) const {
        for (const std::string& peerId : peerIds) {
            if (!online.contains(peerId)) continue;
            if (!queueSnapshot(
                    peerId,
                    transactionId,
                    status,
                    actions,
                    errorMessage
                )) {
                return false;
            }
        }
        return true;
    }

    bool handleCreateClan(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        if (!account.clanId.empty()) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Leave the current Clan before creating another one.",
                actions,
                errorMessage
            );
        }
        if (!validText(
                request.clanName,
                1U,
                runtimeInternetCommunityMaximumClanNameBytes
            )
            || !validClanTag(request.clanTag)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Clan name must be 1-32 UTF-8 bytes and its tag must be 2-8 letters or digits.",
                actions,
                errorMessage
            );
        }
        const std::string clanId = lowerAscii(request.clanTag);
        if (clans.size() >= config.maximumClans) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "The Clan capacity has been reached.",
                actions,
                errorMessage
            );
        }
        if (clans.contains(clanId)
            || !clanTagAvailable(request.clanTag)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "That Clan tag is already registered.",
                actions,
                errorMessage
            );
        }
        if (!clanNameAvailable(request.clanName)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "That Clan name is already registered.",
                actions,
                errorMessage
            );
        }

        ClanState clan;
        clan.projection.clanId = clanId;
        clan.projection.name = request.clanName;
        clan.projection.tag = request.clanTag;
        clan.projection.ownerPeerId = request.senderPeerId;
        clan.projection.createdAtMilliseconds = std::max<std::uint64_t>(
            1U,
            now()
        );
        clan.projection.points = 1000;
        clan.members.insert(request.senderPeerId);
        const std::set<std::string> oldInvitations =
            account.clanInvitations;
        account.clanId = clanId;
        account.clanJoinedAtMilliseconds =
            clan.projection.createdAtMilliseconds;
        account.clanInvitations.clear();
        clans.emplace(clanId, std::move(clan));
        if (!saveState(errorMessage)) {
            clans.erase(clanId);
            account.clanId.clear();
            account.clanJoinedAtMilliseconds = 0U;
            account.clanInvitations = oldInvitations;
            return false;
        }
        return queueSnapshot(
            request.senderPeerId,
            request.transactionId,
            "Clan created.",
            actions,
            errorMessage
        );
    }

    bool handleInviteClanMember(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        const auto clan = clans.find(account.clanId);
        if (clan == clans.end()
            || clan->second.projection.ownerPeerId
                != request.senderPeerId) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Only the Clan owner can invite members.",
                actions,
                errorMessage
            );
        }
        const auto resolved = resolveAccount(request.target);
        if (!resolved.has_value() || *resolved == request.senderPeerId) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "No eligible registered profile matches that Clan invitation.",
                actions,
                errorMessage
            );
        }
        Account& invitee = accounts[*resolved];
        if (!invitee.clanId.empty()) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "That profile already belongs to a Clan.",
                actions,
                errorMessage
            );
        }
        if (clan->second.members.size()
            >= runtimeInternetCommunityMaximumClanMembers) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "The Clan member roster is full.",
                actions,
                errorMessage
            );
        }
        if (!invitee.clanInvitations.contains(account.clanId)
            && invitee.clanInvitations.size()
                >= runtimeInternetCommunityMaximumClanInvitations) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "That profile's Clan invitation list is full.",
                actions,
                errorMessage
            );
        }
        const bool inserted =
            invitee.clanInvitations.insert(account.clanId).second;
        if (inserted && !saveState(errorMessage)) {
            invitee.clanInvitations.erase(account.clanId);
            return false;
        }
        return queueSnapshotsForPeers(
            {request.senderPeerId, *resolved},
            request.transactionId,
            inserted ? "Clan invitation sent." : "Clan invitation is pending.",
            actions,
            errorMessage
        );
    }

    bool handleAcceptClanInvitation(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        if (!account.clanId.empty()) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Leave the current Clan before accepting an invitation.",
                actions,
                errorMessage
            );
        }
        const std::string clanId = lowerAscii(request.clanId);
        const auto clan = clans.find(clanId);
        if (clan == clans.end()
            || !account.clanInvitations.contains(clanId)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "That Clan invitation is not available.",
                actions,
                errorMessage
            );
        }
        if (clan->second.members.size()
            >= runtimeInternetCommunityMaximumClanMembers) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "The Clan member roster is full.",
                actions,
                errorMessage
            );
        }
        const std::set<std::string> oldInvitations =
            account.clanInvitations;
        account.clanId = clanId;
        account.clanJoinedAtMilliseconds = std::max<std::uint64_t>(1U, now());
        account.clanInvitations.clear();
        clan->second.members.insert(request.senderPeerId);
        if (!saveState(errorMessage)) {
            clan->second.members.erase(request.senderPeerId);
            account.clanId.clear();
            account.clanJoinedAtMilliseconds = 0U;
            account.clanInvitations = oldInvitations;
            return false;
        }
        return queueSnapshotsForPeers(
            clan->second.members,
            request.transactionId,
            "Clan invitation accepted.",
            actions,
            errorMessage
        );
    }

    bool handleRemoveClanMember(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        const auto clan = clans.find(account.clanId);
        const auto resolved = resolveAccount(request.target);
        if (clan == clans.end()
            || clan->second.projection.ownerPeerId
                != request.senderPeerId) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Only the Clan owner can remove members.",
                actions,
                errorMessage
            );
        }
        if (!resolved.has_value()
            || *resolved == request.senderPeerId
            || !clan->second.members.contains(*resolved)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Select another member of this Clan to remove.",
                actions,
                errorMessage
            );
        }
        Account& removed = accounts[*resolved];
        const std::uint64_t oldJoinedAt =
            removed.clanJoinedAtMilliseconds;
        clan->second.members.erase(*resolved);
        removed.clanId.clear();
        removed.clanJoinedAtMilliseconds = 0U;
        if (!saveState(errorMessage)) {
            clan->second.members.insert(*resolved);
            removed.clanId = account.clanId;
            removed.clanJoinedAtMilliseconds = oldJoinedAt;
            return false;
        }
        std::set<std::string> recipients = clan->second.members;
        recipients.insert(*resolved);
        return queueSnapshotsForPeers(
            recipients,
            request.transactionId,
            "Clan member removed.",
            actions,
            errorMessage
        );
    }

    bool handleLeaveClan(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        const auto clan = clans.find(account.clanId);
        if (clan == clans.end()) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "This profile does not belong to a Clan.",
                actions,
                errorMessage
            );
        }
        if (clan->second.projection.ownerPeerId == request.senderPeerId
            && clan->second.members.size() > 1U) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Transfer Clan ownership before leaving a Clan with other members.",
                actions,
                errorMessage
            );
        }

        const std::string clanId = account.clanId;
        const std::uint64_t oldJoinedAt = account.clanJoinedAtMilliseconds;
        if (clan->second.members.size() == 1U) {
            const ClanState oldClan = clan->second;
            std::map<std::string, std::set<std::string>> oldInvitations;
            std::set<std::string> recipients{request.senderPeerId};
            for (auto& entry : accounts) {
                if (entry.second.clanInvitations.contains(clanId)) {
                    oldInvitations.emplace(
                        entry.first,
                        entry.second.clanInvitations
                    );
                    entry.second.clanInvitations.erase(clanId);
                    recipients.insert(entry.first);
                }
            }
            account.clanId.clear();
            account.clanJoinedAtMilliseconds = 0U;
            clans.erase(clanId);
            if (!saveState(errorMessage)) {
                clans.emplace(clanId, oldClan);
                account.clanId = clanId;
                account.clanJoinedAtMilliseconds = oldJoinedAt;
                for (const auto& entry : oldInvitations) {
                    accounts[entry.first].clanInvitations = entry.second;
                }
                return false;
            }
            return queueSnapshotsForPeers(
                recipients,
                request.transactionId,
                "Clan dissolved.",
                actions,
                errorMessage
            );
        }

        clan->second.members.erase(request.senderPeerId);
        account.clanId.clear();
        account.clanJoinedAtMilliseconds = 0U;
        if (!saveState(errorMessage)) {
            clan->second.members.insert(request.senderPeerId);
            account.clanId = clanId;
            account.clanJoinedAtMilliseconds = oldJoinedAt;
            return false;
        }
        std::set<std::string> recipients = clan->second.members;
        recipients.insert(request.senderPeerId);
        return queueSnapshotsForPeers(
            recipients,
            request.transactionId,
            "Clan left.",
            actions,
            errorMessage
        );
    }

    bool handleTransferClanOwnership(
        const RuntimeInternetCommunityRequest& request,
        Account& account,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    ) {
        const auto clan = clans.find(account.clanId);
        const auto resolved = resolveAccount(request.target);
        if (clan == clans.end()
            || clan->second.projection.ownerPeerId
                != request.senderPeerId) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Only the Clan owner can transfer ownership.",
                actions,
                errorMessage
            );
        }
        if (!resolved.has_value()
            || *resolved == request.senderPeerId
            || !clan->second.members.contains(*resolved)) {
            return queueError(
                request.senderPeerId,
                request.transactionId,
                "Select another member of this Clan as the new owner.",
                actions,
                errorMessage
            );
        }
        const std::string oldOwner = clan->second.projection.ownerPeerId;
        clan->second.projection.ownerPeerId = *resolved;
        if (!saveState(errorMessage)) {
            clan->second.projection.ownerPeerId = oldOwner;
            return false;
        }
        return queueSnapshotsForPeers(
            clan->second.members,
            request.transactionId,
            "Clan ownership transferred.",
            actions,
            errorMessage
        );
    }

#include "RuntimeInternetRankedCommunity.inl"
};

RuntimeInternetCommunityService::RuntimeInternetCommunityService()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetCommunityService::~RuntimeInternetCommunityService() = default;
RuntimeInternetCommunityService::RuntimeInternetCommunityService(
    RuntimeInternetCommunityService&&
) noexcept = default;
RuntimeInternetCommunityService& RuntimeInternetCommunityService::operator=(
    RuntimeInternetCommunityService&&
) noexcept = default;

bool RuntimeInternetCommunityService::open(
    RuntimeInternetCommunityServiceConfig config,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->opened) {
        return fail(errorMessage, "community service is already open");
    }
    if (!validToken(config.serviceId, maximumIdentityBytes)
        || !config.nowMilliseconds
        || config.maximumAccounts == 0U
        || config.maximumAccounts > 65535U
        || config.maximumClans == 0U
        || config.maximumClans > 65535U
        || config.maximumAuthorizedQuickMatches == 0U
        || config.maximumAuthorizedQuickMatches > 65535U
        || config.maximumReplayTransactionsPerPeer == 0U) {
        return fail(errorMessage, "community service configuration is invalid");
    }
    if (!validateRuntimeInternetRankedPolicy(config.rankedPolicy, errorMessage)) return false;
    if (!config.utcMilliseconds) config.utcMilliseconds = [] {
        return static_cast<std::uint64_t>(std::max<std::int64_t>(1,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
    };
    if (config.news.size() > runtimeInternetCommunityMaximumNewsItems) {
        return fail(errorMessage, "community news capacity is invalid");
    }
    for (const auto& news : config.news) {
        if (!validToken(news.newsId, maximumNewsIdBytes)
            || !validText(news.title, 1U, maximumNewsTitleBytes)
            || !validText(news.body, 1U, maximumNewsBodyBytes, true)) {
            return fail(errorMessage, "community news item is invalid");
        }
    }
    impl_->config = std::move(config);
    impl_->initializeChannels();
    impl_->initializeNews();
    bool stateLoaded = impl_->loadState(errorMessage);
    if (stateLoaded) {
        bool changed = impl_->stateNeedsUpgrade;
        for (auto& [id, match] : impl_->authorizedMatches) {
            (void)id;
            if (runtimeInternetRankedScored(match.record.status)
                && (!impl_->accounts.contains(match.record.peerIds[0]) || !impl_->accounts.contains(match.record.peerIds[1]))) {
                stateLoaded = fail(errorMessage, "ranked state references missing scored accounts"); break;
            }
            changed |= match.serviceRestarted(impl_->utc());
        }
        if (stateLoaded && changed) stateLoaded = impl_->saveState(errorMessage);
    }
    if (!stateLoaded) {
        impl_->accounts.clear();
        impl_->clans.clear();
        impl_->channels.clear();
        impl_->channelOrder.clear();
        impl_->authorizedMatches.clear();
        impl_->authorizedMatchOrder.clear();
        impl_->config = {};
        return false;
    }
    impl_->opened = true;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetCommunityService::receive(
    const RuntimeInternetCommunityServiceEnvelope& envelope,
    std::vector<RuntimeInternetCommunityServiceAction>& actions,
    std::string& errorMessage
) {
    if (!impl_ || !impl_->opened) {
        return fail(errorMessage, "community service is not open");
    }
    RuntimeInternetCommunityRequest request;
    if (!decodeRuntimeInternetCommunityRequest(
            envelope.packet,
            request,
            errorMessage
        )
        || envelope.authenticatedServiceId != impl_->config.serviceId
        || request.serviceId != envelope.authenticatedServiceId
        || request.senderPeerId != envelope.authenticatedPeerId
        || !validToken(envelope.authenticatedPeerId, maximumIdentityBytes)) {
        return fail(errorMessage, "community request identity is not authenticated");
    }
    std::vector<RuntimeInternetCommunityServiceAction> candidateActions;
    if (!poll(candidateActions, errorMessage)) return false;
    if (impl_->replayed(request.senderPeerId, request.transactionId)) {
        if (!impl_->queueError(
                request.senderPeerId,
                request.transactionId,
                "Duplicate community transaction was rejected.",
                candidateActions,
                errorMessage
            )) {
            return false;
        }
        actions.insert(
            actions.end(),
            std::make_move_iterator(candidateActions.begin()),
            std::make_move_iterator(candidateActions.end())
        );
        errorMessage.clear();
        return true;
    }
    impl_->online.insert(request.senderPeerId);
    auto account = impl_->accounts.find(request.senderPeerId);
    if (account != impl_->accounts.end()) {
        account->second.profile.lastSeenMilliseconds = impl_->now();
        account->second.profile.online = true;
        if (account->second.channelId.empty()) {
            impl_->enterChannel(account->second, "lobby");
        } else {
            impl_->channels[account->second.channelId].members.insert(
                request.senderPeerId
            );
        }
    }

    bool handled = false;
    if (request.operation
        == RuntimeInternetCommunityOperation::RegisterProfile) {
        handled = impl_->handleRegister(
            request,
            candidateActions,
            errorMessage
        );
    } else if (request.operation == RuntimeInternetCommunityOperation::Login
        || request.operation
            == RuntimeInternetCommunityOperation::Refresh) {
        handled = impl_->queueSnapshot(
            request.senderPeerId,
            request.transactionId,
            account == impl_->accounts.end()
                ? "Profile registration is required."
                : "Community profile synchronized.",
            candidateActions,
            errorMessage
        );
    } else if (account == impl_->accounts.end()) {
        handled = impl_->queueError(
            request.senderPeerId,
            request.transactionId,
            "Register a Westwood Online profile before using community features.",
            candidateActions,
            errorMessage
        );
    } else {
        switch (request.operation) {
        case RuntimeInternetCommunityOperation::JoinChannel:
            handled = impl_->handleJoinChannel(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::SendChannelMessage:
            handled = impl_->handleChat(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::AddFriend:
            handled = impl_->handleAddFriend(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::RemoveFriend:
            handled = impl_->handleRemoveFriend(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::ReportQuickMatch:
        case RuntimeInternetCommunityOperation::AbandonQuickMatch:
            handled = impl_->handleQuickMatchReport(
                request,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::QuickMatchHistory:
            handled = impl_->queueMatchHistory(request.senderPeerId, request.transactionId, candidateActions, errorMessage);
            break;
        case RuntimeInternetCommunityOperation::CreateClan:
            handled = impl_->handleCreateClan(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::InviteClanMember:
            handled = impl_->handleInviteClanMember(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::AcceptClanInvitation:
            handled = impl_->handleAcceptClanInvitation(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::RemoveClanMember:
            handled = impl_->handleRemoveClanMember(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::LeaveClan:
            handled = impl_->handleLeaveClan(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        case RuntimeInternetCommunityOperation::TransferClanOwnership:
            handled = impl_->handleTransferClanOwnership(
                request,
                account->second,
                candidateActions,
                errorMessage
            );
            break;
        default:
            handled = impl_->queueError(
                request.senderPeerId,
                request.transactionId,
                "Community operation is not available.",
                candidateActions,
                errorMessage
            );
            break;
        }
    }
    if (!handled) return false;
    if ((request.operation == RuntimeInternetCommunityOperation::RegisterProfile
            || request.operation == RuntimeInternetCommunityOperation::Login
            || request.operation == RuntimeInternetCommunityOperation::Refresh)
        && impl_->accounts.contains(request.senderPeerId)
        && !impl_->queueMatchHistory(request.senderPeerId, request.transactionId, candidateActions, errorMessage)) return false;
    actions.insert(
        actions.end(),
        std::make_move_iterator(candidateActions.begin()),
        std::make_move_iterator(candidateActions.end())
    );
    errorMessage.clear();
    return true;
}

bool RuntimeInternetCommunityService::authenticatedPeerConnected(
    std::string_view authenticatedServiceId,
    std::string_view authenticatedPeerId,
    std::vector<RuntimeInternetCommunityServiceAction>& actions,
    std::string& errorMessage
) {
    if (!impl_ || !impl_->opened
        || authenticatedServiceId != impl_->config.serviceId
        || !validToken(authenticatedPeerId, maximumIdentityBytes)) {
        return fail(errorMessage, "community connection identity is invalid");
    }
    impl_->online.insert(std::string(authenticatedPeerId));
    const auto account = impl_->accounts.find(std::string(authenticatedPeerId));
    if (account == impl_->accounts.end()) {
        errorMessage.clear();
        return true;
    }
    account->second.profile.online = true;
    account->second.profile.lastSeenMilliseconds = impl_->now();
    if (account->second.channelId.empty()) {
        impl_->enterChannel(account->second, "lobby");
    } else {
        impl_->channels[account->second.channelId].members.insert(
            account->first
        );
    }
    const std::uint64_t transactionId = std::max<std::uint64_t>(1U, impl_->now());
    if (!poll(actions, errorMessage)
        || !impl_->queueMatchHistory(account->first, transactionId, actions, errorMessage)) return false;
    if (!impl_->queueSnapshot(
            account->first,
            transactionId,
            "Westwood Online profile signed in.",
            actions,
            errorMessage
        )
        || !impl_->queueBuddyPresenceSnapshots(
            account->first,
            transactionId,
            actions,
            errorMessage
        )) {
        return false;
    }
    if (!account->second.clanId.empty()) {
        const auto clan = impl_->clans.find(account->second.clanId);
        if (clan != impl_->clans.end()) {
            std::set<std::string> clanPeers = clan->second.members;
            clanPeers.erase(account->first);
            if (!impl_->queueSnapshotsForPeers(
                    clanPeers,
                    transactionId,
                    "Clan member presence updated.",
                    actions,
                    errorMessage
                )) {
                return false;
            }
        }
    }
    errorMessage.clear();
    return true;
}

#include "RuntimeInternetRankedService.inl"

RuntimeInternetCommunityServiceStats RuntimeInternetCommunityService::stats() const {
    RuntimeInternetCommunityServiceStats result;
    if (!impl_ || !impl_->opened) return result;
    result.registeredAccounts = impl_->accounts.size();
    result.onlineAccounts = static_cast<std::size_t>(std::count_if(
        impl_->accounts.begin(),
        impl_->accounts.end(),
        [&](const auto& entry) { return impl_->online.contains(entry.first); }
    ));
    result.authorizedQuickMatches = impl_->authorizedMatches.size();
    result.pendingQuickMatchReports = static_cast<std::size_t>(std::count_if(
        impl_->authorizedMatches.begin(),
        impl_->authorizedMatches.end(),
        [](const auto& entry) {
            return entry.second.record.status == RuntimeInternetRankedStatus::AwaitingReport
                || entry.second.record.status == RuntimeInternetRankedStatus::ConflictingReports;
        }
    ));
    result.registeredClans = impl_->clans.size();
    result.unresolvedQuickMatches = static_cast<std::size_t>(std::count_if(impl_->authorizedMatches.begin(), impl_->authorizedMatches.end(),
        [](const auto& entry) { return !runtimeInternetRankedTerminal(entry.second.record.status); }));
    result.finalizedQuickMatches = result.authorizedQuickMatches - result.unresolvedQuickMatches;
    return result;
}

bool RuntimeInternetCommunityService::isOpen() const {
    return impl_ && impl_->opened;
}

void RuntimeInternetCommunityService::close() noexcept {
    if (!impl_) return;
    impl_->accounts.clear();
    impl_->clans.clear();
    impl_->channels.clear();
    impl_->channelOrder.clear();
    impl_->online.clear();
    impl_->replayTransactions.clear();
    impl_->authorizedMatches.clear();
    impl_->authorizedMatchOrder.clear();
    impl_->config = {};
    impl_->nextMessageId = 1U;
    impl_->opened = false;
}

bool RuntimeInternetCommunityClient::open(
    RuntimeInternetCommunityClientConfig config,
    std::string& errorMessage
) {
    if (!validToken(config.serviceId, maximumIdentityBytes)
        || !validToken(config.localPeerId, maximumIdentityBytes)
        || config.serviceId == config.localPeerId) {
        return fail(errorMessage, "community client identity is invalid");
    }
    config_ = std::move(config);
    snapshot_ = {};
    matchHistory_.clear();
    events_.clear();
    opened_ = true;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetCommunityClient::makeRequest(
    RuntimeInternetCommunityRequest request,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    if (!opened_) return fail(errorMessage, "community client is not open");
    request.serviceId = config_.serviceId;
    request.senderPeerId = config_.localPeerId;
    return encodeRuntimeInternetCommunityRequest(
        request,
        packet,
        errorMessage
    );
}

bool RuntimeInternetCommunityClient::makeLoginRequest(
    std::uint64_t transactionId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::Login;
    request.transactionId = transactionId;
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeRegisterProfileRequest(
    std::uint64_t transactionId,
    std::string displayName,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::RegisterProfile;
    request.transactionId = transactionId;
    request.displayName = std::move(displayName);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeRefreshRequest(
    std::uint64_t transactionId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::Refresh;
    request.transactionId = transactionId;
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeJoinChannelRequest(
    std::uint64_t transactionId,
    std::string channelId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::JoinChannel;
    request.transactionId = transactionId;
    request.channelId = std::move(channelId);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeSendChannelMessageRequest(
    std::uint64_t transactionId,
    std::string text,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::SendChannelMessage;
    request.transactionId = transactionId;
    request.text = std::move(text);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeAddFriendRequest(
    std::uint64_t transactionId,
    std::string handleOrPeerId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::AddFriend;
    request.transactionId = transactionId;
    request.target = std::move(handleOrPeerId);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeRemoveFriendRequest(
    std::uint64_t transactionId,
    std::string peerId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::RemoveFriend;
    request.transactionId = transactionId;
    request.target = std::move(peerId);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeReportQuickMatchRequest(
    std::uint64_t transactionId,
    std::string matchId,
    std::string opponentPeerId,
    RuntimeInternetCommunityQuickMatchResult result,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::ReportQuickMatch;
    request.transactionId = transactionId;
    request.matchId = std::move(matchId);
    request.target = std::move(opponentPeerId);
    request.quickMatchResult = result;
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeQuickMatchHistoryRequest(std::uint64_t transactionId,
    RuntimeNetworkPacket& packet, std::string& error) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::QuickMatchHistory; request.transactionId = transactionId;
    return makeRequest(std::move(request), packet, error);
}

bool RuntimeInternetCommunityClient::makeAbandonQuickMatchRequest(std::uint64_t transactionId, std::string matchId,
    std::string opponentPeerId, RuntimeNetworkPacket& packet, std::string& error) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::AbandonQuickMatch; request.transactionId = transactionId;
    request.matchId = std::move(matchId); request.target = std::move(opponentPeerId);
    return makeRequest(std::move(request), packet, error);
}

const std::vector<RuntimeInternetRankedRecord>& RuntimeInternetCommunityClient::matchHistory() const noexcept {
    return matchHistory_;
}

bool RuntimeInternetCommunityClient::makeCreateClanRequest(
    std::uint64_t transactionId,
    std::string clanName,
    std::string clanTag,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::CreateClan;
    request.transactionId = transactionId;
    request.clanName = std::move(clanName);
    request.clanTag = std::move(clanTag);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeInviteClanMemberRequest(
    std::uint64_t transactionId,
    std::string handleOrPeerId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::InviteClanMember;
    request.transactionId = transactionId;
    request.target = std::move(handleOrPeerId);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeAcceptClanInvitationRequest(
    std::uint64_t transactionId,
    std::string clanId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation =
        RuntimeInternetCommunityOperation::AcceptClanInvitation;
    request.transactionId = transactionId;
    request.clanId = std::move(clanId);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeRemoveClanMemberRequest(
    std::uint64_t transactionId,
    std::string peerId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::RemoveClanMember;
    request.transactionId = transactionId;
    request.target = std::move(peerId);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeLeaveClanRequest(
    std::uint64_t transactionId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation = RuntimeInternetCommunityOperation::LeaveClan;
    request.transactionId = transactionId;
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::makeTransferClanOwnershipRequest(
    std::uint64_t transactionId,
    std::string peerId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetCommunityRequest request;
    request.operation =
        RuntimeInternetCommunityOperation::TransferClanOwnership;
    request.transactionId = transactionId;
    request.target = std::move(peerId);
    return makeRequest(std::move(request), packet, errorMessage);
}

bool RuntimeInternetCommunityClient::receive(
    const RuntimeNetworkPacket& packet,
    std::string_view authenticatedServiceId,
    std::string& errorMessage
) {
    if (!opened_) return fail(errorMessage, "community client is not open");
    RuntimeInternetCommunityResponse response;
    if (!decodeRuntimeInternetCommunityResponse(packet, response, errorMessage)
        || authenticatedServiceId != config_.serviceId
        || response.serviceId != config_.serviceId
        || response.senderPeerId != config_.serviceId
        || response.targetPeerId != config_.localPeerId) {
        return fail(errorMessage, "community response identity is not authenticated");
    }
    RuntimeInternetCommunityClientEvent event;
    event.transactionId = response.transactionId;
    event.status = response.status;
    if (response.kind == RuntimeInternetCommunityResponseKind::Snapshot) {
        snapshot_ = std::move(response.snapshot);
        event.kind = snapshot_.registeredProfile
            ? RuntimeInternetCommunityClientEventKind::SnapshotUpdated
            : RuntimeInternetCommunityClientEventKind::RegistrationRequired;
    } else if (response.kind == RuntimeInternetCommunityResponseKind::MatchHistory) {
        matchHistory_ = std::move(response.matchHistory);
        event.kind = RuntimeInternetCommunityClientEventKind::SnapshotUpdated;
    } else if (response.kind
        == RuntimeInternetCommunityResponseKind::ChannelMessage) {
        event.kind =
            RuntimeInternetCommunityClientEventKind::ChannelMessageReceived;
        event.channelMessage = response.channelMessage;
        if (snapshot_.registeredProfile
            && snapshot_.selectedChannelId
                == response.channelMessage.channelId
            && std::none_of(
                snapshot_.chatHistory.begin(),
                snapshot_.chatHistory.end(),
                [&](const RuntimeInternetCommunityChatMessage& message) {
                    return message.messageId
                        == response.channelMessage.messageId;
                }
            )) {
            if (snapshot_.chatHistory.size()
                >= runtimeInternetCommunityMaximumChatHistory) {
                snapshot_.chatHistory.erase(snapshot_.chatHistory.begin());
            }
            snapshot_.chatHistory.push_back(response.channelMessage);
        }
    } else {
        event.kind = RuntimeInternetCommunityClientEventKind::OperationRejected;
    }
    events_.push_back(std::move(event));
    errorMessage.clear();
    return true;
}

bool RuntimeInternetCommunityClient::isOpen() const noexcept {
    return opened_;
}

bool RuntimeInternetCommunityClient::registeredProfile() const noexcept {
    return opened_ && snapshot_.registeredProfile;
}

const RuntimeInternetCommunitySnapshot&
RuntimeInternetCommunityClient::snapshot() const noexcept {
    return snapshot_;
}

std::vector<RuntimeInternetCommunityClientEvent>
RuntimeInternetCommunityClient::takeEvents() {
    std::vector<RuntimeInternetCommunityClientEvent> result =
        std::move(events_);
    events_.clear();
    return result;
}

void RuntimeInternetCommunityClient::close() noexcept {
    config_ = {};
    snapshot_ = {};
    matchHistory_.clear();
    events_.clear();
    opened_ = false;
}
