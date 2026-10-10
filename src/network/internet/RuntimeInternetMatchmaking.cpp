#include "network/internet/RuntimeInternetMatchmaking.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <optional>
#include <utility>

namespace {

constexpr std::size_t matchmakingIdentifierRandomBytes = 16U;
constexpr std::int32_t matchmakingMinimumSkillRating = 0;
constexpr std::int32_t matchmakingMaximumSkillRating = 10000;
constexpr std::uint8_t matchmakingMinimumPlayers = 2U;
constexpr std::uint8_t matchmakingMaximumPlayers = 8U;

RuntimeInternetMatchmakingOperationResult rejected(std::string reason) {
    RuntimeInternetMatchmakingOperationResult result;
    result.reason = std::move(reason);
    return result;
}

RuntimeInternetMatchmakingOperationResult accepted(
    bool changed,
    bool duplicate = false
) {
    RuntimeInternetMatchmakingOperationResult result;
    result.accepted = true;
    result.changed = changed;
    result.duplicate = duplicate;
    return result;
}

bool validIdentifier(std::string_view value, std::size_t maximumBytes) {
    if (value.empty() || value.size() > maximumBytes) return false;
    return std::all_of(value.begin(), value.end(), [](char valueCharacter) {
        const unsigned char character =
            static_cast<unsigned char>(valueCharacter);
        return std::isalnum(character) != 0
            || character == static_cast<unsigned char>('.')
            || character == static_cast<unsigned char>('_')
            || character == static_cast<unsigned char>('-');
    });
}

bool validPrintableText(std::string_view value, std::size_t maximumBytes) {
    if (value.empty() || value.size() > maximumBytes) return false;
    return std::all_of(value.begin(), value.end(), [](char valueCharacter) {
        const unsigned char character =
            static_cast<unsigned char>(valueCharacter);
        return character >= 0x21U && character <= 0x7eU;
    });
}

bool validDisplayText(std::string_view value, std::size_t maximumBytes) {
    if (value.empty() || value.size() > maximumBytes) return false;
    return std::all_of(value.begin(), value.end(), [](char valueCharacter) {
        const unsigned char character =
            static_cast<unsigned char>(valueCharacter);
        return character >= 0x20U && character <= 0x7eU;
    });
}

std::uint64_t saturatingAdd(
    std::uint64_t value,
    std::uint64_t increment
) {
    if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return value + increment;
}

std::string hexadecimal(std::span<const std::uint8_t> bytes) {
    static constexpr std::array<char, 16> digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    std::string result;
    result.resize(bytes.size() * 2U);
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        result[index * 2U] = digits[(bytes[index] >> 4U) & 0x0fU];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

bool constantTimeEquals(
    std::span<const std::uint8_t> left,
    std::span<const std::uint8_t> right
) {
    if (left.size() != right.size()) return false;
    std::uint8_t difference = 0U;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        difference = static_cast<std::uint8_t>(
            difference | static_cast<std::uint8_t>(left[index] ^ right[index])
        );
    }
    return difference == 0U;
}

void secureClear(std::vector<std::uint8_t>& bytes) noexcept {
    volatile std::uint8_t* data = bytes.data();
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        data[index] = 0U;
    }
    bytes.clear();
}

} // namespace

struct RuntimeInternetMatchmaking::Impl {
    struct QueueEntry {
        std::string peerId;
        RuntimeInternetMatchmakingRequest request;
        std::uint64_t enqueuedAtMilliseconds = 0;
        std::uint64_t expiresAtMilliseconds = 0;
    };

    struct Admission {
        std::uint64_t requestId = 0;
        std::string peerId;
        std::vector<std::uint8_t> proof;

        Admission() = default;
        ~Admission() { secureClear(proof); }
        Admission(Admission&&) noexcept = default;
        Admission& operator=(Admission&&) noexcept = default;
        Admission(const Admission&) = delete;
        Admission& operator=(const Admission&) = delete;
    };

    struct Match {
        std::string matchId;
        std::string roomId;
        std::string hostPeerId;
        std::string hostDisplayName;
        std::vector<std::string> peerIds;
        std::vector<Admission> admissions;
        std::uint64_t expiresAtMilliseconds = 0;
        bool activated = false;
    };

    RuntimeInternetMatchmakingConfig config;
    std::vector<QueueEntry> queue;
    std::vector<Match> matches;
    bool open = false;

    std::uint64_t now() const {
        return config.nowMilliseconds();
    }

    bool identityMatches(
        std::string_view serviceId,
        std::string_view peerId
    ) const {
        return open && serviceId == config.serviceId
            && validIdentifier(
                peerId,
                runtimeInternetMatchmakingMaximumPeerIdBytes
            );
    }

    void prune(std::uint64_t nowMilliseconds) {
        std::erase_if(queue, [&](const QueueEntry& entry) {
            return entry.expiresAtMilliseconds <= nowMilliseconds;
        });
        std::erase_if(matches, [&](const Match& match) {
            return match.expiresAtMilliseconds <= nowMilliseconds;
        });
    }

    const QueueEntry* queuedPeer(std::string_view peerId) const {
        const auto found = std::find_if(
            queue.begin(),
            queue.end(),
            [&](const QueueEntry& entry) { return entry.peerId == peerId; }
        );
        return found == queue.end() ? nullptr : &*found;
    }

    QueueEntry* queuedPeer(std::string_view peerId) {
        return const_cast<QueueEntry*>(
            std::as_const(*this).queuedPeer(peerId)
        );
    }

    const Match* matchForPeer(std::string_view peerId) const {
        const auto found = std::find_if(
            matches.begin(),
            matches.end(),
            [&](const Match& match) {
                return std::find(
                    match.peerIds.begin(),
                    match.peerIds.end(),
                    peerId
                ) != match.peerIds.end();
            }
        );
        return found == matches.end() ? nullptr : &*found;
    }

    bool compatibleCriteria(
        const RuntimeInternetMatchmakingRequest& left,
        const RuntimeInternetMatchmakingRequest& right
    ) const {
        return left.queueId == right.queueId
            && left.regionId == right.regionId
            && left.contentFingerprint == right.contentFingerprint
            && left.playerCount == right.playerCount;
    }

    std::int32_t skillWindow(
        const QueueEntry& anchor,
        std::uint64_t nowMilliseconds
    ) const {
        const std::uint64_t waitedMilliseconds =
            nowMilliseconds >= anchor.enqueuedAtMilliseconds
            ? nowMilliseconds - anchor.enqueuedAtMilliseconds
            : 0U;
        const std::uint64_t waitedSeconds = waitedMilliseconds / 1000U;
        const std::uint64_t growthPerSecond =
            static_cast<std::uint64_t>(config.skillWindowGrowthPerSecond);
        const std::uint64_t maximumGrowth =
            static_cast<std::uint64_t>(config.maximumSkillWindow)
            - static_cast<std::uint64_t>(config.baseSkillWindow);
        const std::uint64_t growth = growthPerSecond == 0U
            ? 0U
            : std::min(
                maximumGrowth,
                waitedSeconds > maximumGrowth / growthPerSecond
                    ? maximumGrowth
                    : waitedSeconds * growthPerSecond
            );
        const std::uint64_t expanded =
            static_cast<std::uint64_t>(config.baseSkillWindow) + growth;
        return static_cast<std::int32_t>(std::min<std::uint64_t>(
            expanded,
            static_cast<std::uint64_t>(config.maximumSkillWindow)
        ));
    }

    std::optional<std::vector<std::size_t>> nextCandidate(
        std::uint64_t nowMilliseconds
    ) const {
        for (std::size_t anchorIndex = 0U;
             anchorIndex < queue.size();
             ++anchorIndex) {
            const QueueEntry& anchor = queue[anchorIndex];
            const std::size_t required = anchor.request.playerCount;
            const std::int32_t window = skillWindow(anchor, nowMilliseconds);
            std::vector<std::size_t> selected{anchorIndex};
            std::int32_t minimumRating = anchor.request.skillRating;
            std::int32_t maximumRating = anchor.request.skillRating;
            for (std::size_t candidateIndex = anchorIndex + 1U;
                 candidateIndex < queue.size()
                    && selected.size() < required;
                 ++candidateIndex) {
                const QueueEntry& candidate = queue[candidateIndex];
                if (!compatibleCriteria(anchor.request, candidate.request)) {
                    continue;
                }
                const std::int32_t prospectiveMinimum = std::min(
                    minimumRating,
                    candidate.request.skillRating
                );
                const std::int32_t prospectiveMaximum = std::max(
                    maximumRating,
                    candidate.request.skillRating
                );
                if (prospectiveMaximum - prospectiveMinimum > window) {
                    continue;
                }
                selected.push_back(candidateIndex);
                minimumRating = prospectiveMinimum;
                maximumRating = prospectiveMaximum;
            }
            if (selected.size() == required) return selected;
        }
        return std::nullopt;
    }

    bool makeUniqueIdentifier(
        std::string_view prefix,
        std::string& identifier,
        std::string& errorMessage
    ) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::array<std::uint8_t, matchmakingIdentifierRandomBytes> bytes{};
            if (!config.secureRandomBytes(bytes.data(), bytes.size())) {
                errorMessage = "matchmaking secure random generation failed";
                return false;
            }
            std::string candidate(prefix);
            candidate += hexadecimal(bytes);
            const bool duplicate = std::any_of(
                matches.begin(),
                matches.end(),
                [&](const Match& match) {
                    return match.matchId == candidate
                        || match.roomId == candidate;
                }
            );
            if (!duplicate) {
                identifier = std::move(candidate);
                return true;
            }
        }
        errorMessage = "matchmaking identifier collision limit reached";
        return false;
    }

    bool buildMatch(
        const std::vector<std::size_t>& selected,
        std::uint64_t nowMilliseconds,
        Match& match,
        std::vector<RuntimeInternetMatchmakingAction>& actions,
        std::string& errorMessage
    ) {
        Match candidate;
        if (!makeUniqueIdentifier("match-", candidate.matchId, errorMessage)
            || !makeUniqueIdentifier("room-", candidate.roomId, errorMessage)) {
            return false;
        }
        candidate.expiresAtMilliseconds = saturatingAdd(
            nowMilliseconds,
            config.matchLeaseMilliseconds
        );
        candidate.hostPeerId = queue[selected.front()].peerId;
        candidate.hostDisplayName =
            queue[selected.front()].request.displayName;
        candidate.peerIds.reserve(selected.size());
        candidate.admissions.reserve(selected.size());
        for (const std::size_t index : selected) {
            const QueueEntry& entry = queue[index];
            Admission admission;
            admission.requestId = entry.request.requestId;
            admission.peerId = entry.peerId;
            admission.proof.resize(config.admissionProofBytes);
            if (!config.secureRandomBytes(
                    admission.proof.data(),
                    admission.proof.size()
                )) {
                errorMessage = "matchmaking admission proof generation failed";
                return false;
            }
            candidate.peerIds.push_back(entry.peerId);
            candidate.admissions.push_back(std::move(admission));
        }

        const auto hostAdmission = std::find_if(
            candidate.admissions.begin(),
            candidate.admissions.end(),
            [&](const Admission& admission) {
                return admission.peerId == candidate.hostPeerId;
            }
        );
        if (hostAdmission == candidate.admissions.end()) {
            errorMessage = "matchmaking host admission was not generated";
            return false;
        }
        std::vector<RuntimeInternetMatchmakingAction> candidateActions;
        candidateActions.push_back(makeAssignment(candidate, *hostAdmission));
        match = std::move(candidate);
        actions = std::move(candidateActions);
        return true;
    }

    static RuntimeInternetMatchmakingAction makeAssignment(
        const Match& match,
        const Admission& admission
    ) {
        RuntimeInternetMatchmakingAction action;
        action.targetPeerId = admission.peerId;
        action.assignment.requestId = admission.requestId;
        action.assignment.matchId = match.matchId;
        action.assignment.roomId = match.roomId;
        action.assignment.peerId = admission.peerId;
        action.assignment.hostPeerId = match.hostPeerId;
        action.assignment.hostDisplayName = match.hostDisplayName;
        action.assignment.peerIds = match.peerIds;
        action.assignment.admissionProof = admission.proof;
        action.assignment.expiresAtMilliseconds = match.expiresAtMilliseconds;
        return action;
    }
};

RuntimeInternetMatchmaking::RuntimeInternetMatchmaking()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetMatchmaking::~RuntimeInternetMatchmaking() = default;

RuntimeInternetMatchmaking::RuntimeInternetMatchmaking(
    RuntimeInternetMatchmaking&&
) noexcept = default;

RuntimeInternetMatchmaking& RuntimeInternetMatchmaking::operator=(
    RuntimeInternetMatchmaking&&
) noexcept = default;

bool RuntimeInternetMatchmaking::open(
    RuntimeInternetMatchmakingConfig config,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    const bool valid = validIdentifier(
            config.serviceId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        && config.queueLeaseMilliseconds > 0U
        && config.matchLeaseMilliseconds > 0U
        && config.maximumQueuedPeers > 0U
        && config.maximumQueuedPeers
            <= runtimeInternetMatchmakingMaximumQueuedPeers
        && config.maximumActiveMatches > 0U
        && config.maximumActiveMatches
            <= runtimeInternetMatchmakingMaximumActiveMatches
        && config.admissionProofBytes >= 16U
        && config.admissionProofBytes
            <= runtimeInternetMatchmakingMaximumProofBytes
        && config.baseSkillWindow >= 0
        && config.skillWindowGrowthPerSecond >= 0
        && config.maximumSkillWindow >= config.baseSkillWindow
        && config.maximumSkillWindow
            <= matchmakingMaximumSkillRating - matchmakingMinimumSkillRating
        && static_cast<bool>(config.nowMilliseconds)
        && static_cast<bool>(config.secureRandomBytes);
    if (!valid) {
        errorMessage = "invalid Internet matchmaking configuration";
        return false;
    }
    impl_->config = std::move(config);
    impl_->queue.clear();
    impl_->matches.clear();
    impl_->open = true;
    errorMessage.clear();
    return true;
}

RuntimeInternetMatchmakingOperationResult RuntimeInternetMatchmaking::enqueue(
    std::string_view authenticatedServiceId,
    std::string authenticatedPeerId,
    RuntimeInternetMatchmakingRequest request
) {
    if (!impl_ || !impl_->identityMatches(
            authenticatedServiceId,
            authenticatedPeerId
        )) {
        return rejected("matchmaking authenticated identity is invalid");
    }
    if (request.requestId == 0U
        || !validIdentifier(
            request.queueId,
            runtimeInternetMatchmakingMaximumQueueIdBytes
        )
        || !validIdentifier(
            request.regionId,
            runtimeInternetMatchmakingMaximumRegionIdBytes
        )
        || !validPrintableText(
            request.contentFingerprint,
            runtimeInternetMatchmakingMaximumContentFingerprintBytes
        )
        || !validDisplayText(
            request.displayName,
            runtimeInternetMatchmakingMaximumDisplayNameBytes
        )
        || request.playerCount < matchmakingMinimumPlayers
        || request.playerCount > matchmakingMaximumPlayers
        || request.skillRating < matchmakingMinimumSkillRating
        || request.skillRating > matchmakingMaximumSkillRating) {
        return rejected("invalid matchmaking request");
    }

    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->prune(nowMilliseconds);
    if (const Impl::QueueEntry* existing =
            impl_->queuedPeer(authenticatedPeerId)) {
        if (existing->request == request) return accepted(false, true);
        return rejected("peer already has a different queued request");
    }
    if (const Impl::Match* existingMatch =
            impl_->matchForPeer(authenticatedPeerId)) {
        const auto admission = std::find_if(
            existingMatch->admissions.begin(),
            existingMatch->admissions.end(),
            [&](const Impl::Admission& value) {
                return value.peerId == authenticatedPeerId;
            }
        );
        if (admission != existingMatch->admissions.end()
            && admission->requestId == request.requestId) {
            return accepted(false, true);
        }
        return rejected("peer already has an active match assignment");
    }
    if (impl_->queue.size() >= impl_->config.maximumQueuedPeers) {
        return rejected("matchmaking queue capacity reached");
    }

    Impl::QueueEntry entry;
    entry.peerId = authenticatedPeerId;
    entry.request = std::move(request);
    entry.enqueuedAtMilliseconds = nowMilliseconds;
    entry.expiresAtMilliseconds = saturatingAdd(
        nowMilliseconds,
        impl_->config.queueLeaseMilliseconds
    );
    impl_->queue.push_back(std::move(entry));
    return accepted(true);
}

RuntimeInternetMatchmakingOperationResult
RuntimeInternetMatchmaking::heartbeat(
    std::string_view authenticatedServiceId,
    std::string_view authenticatedPeerId,
    std::uint64_t requestId
) {
    if (!impl_ || !impl_->identityMatches(
            authenticatedServiceId,
            authenticatedPeerId
        )) {
        return rejected("matchmaking authenticated identity is invalid");
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->prune(nowMilliseconds);
    Impl::QueueEntry* entry = impl_->queuedPeer(authenticatedPeerId);
    if (!entry || entry->request.requestId != requestId) {
        return rejected("queued matchmaking request was not found");
    }
    const std::uint64_t renewed = saturatingAdd(
        nowMilliseconds,
        impl_->config.queueLeaseMilliseconds
    );
    if (renewed == entry->expiresAtMilliseconds) return accepted(false, true);
    entry->expiresAtMilliseconds = renewed;
    return accepted(true);
}

RuntimeInternetMatchmakingOperationResult RuntimeInternetMatchmaking::cancel(
    std::string_view authenticatedServiceId,
    std::string_view authenticatedPeerId,
    std::uint64_t requestId
) {
    if (!impl_ || !impl_->identityMatches(
            authenticatedServiceId,
            authenticatedPeerId
        )) {
        return rejected("matchmaking authenticated identity is invalid");
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->prune(nowMilliseconds);
    const auto found = std::find_if(
        impl_->queue.begin(),
        impl_->queue.end(),
        [&](const Impl::QueueEntry& entry) {
            return entry.peerId == authenticatedPeerId
                && entry.request.requestId == requestId;
        }
    );
    if (found == impl_->queue.end()) {
        return rejected("queued matchmaking request was not found");
    }
    impl_->queue.erase(found);
    return accepted(true);
}

RuntimeInternetMatchmakingOperationResult
RuntimeInternetMatchmaking::authenticatedPeerDisconnected(
    std::string_view authenticatedServiceId,
    std::string_view authenticatedPeerId
) {
    if (!impl_ || !impl_->identityMatches(
            authenticatedServiceId,
            authenticatedPeerId
        )) {
        return rejected("matchmaking authenticated identity is invalid");
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->prune(nowMilliseconds);
    const std::size_t previousQueueSize = impl_->queue.size();
    const std::size_t previousMatchSize = impl_->matches.size();
    std::erase_if(impl_->queue, [&](const Impl::QueueEntry& entry) {
        return entry.peerId == authenticatedPeerId;
    });
    std::erase_if(impl_->matches, [&](const Impl::Match& match) {
        return std::find(
            match.peerIds.begin(),
            match.peerIds.end(),
            authenticatedPeerId
        ) != match.peerIds.end();
    });
    const bool changed = impl_->queue.size() != previousQueueSize
        || impl_->matches.size() != previousMatchSize;
    return accepted(changed, !changed);
}

RuntimeInternetMatchmakingOperationResult
RuntimeInternetMatchmaking::activateMatch(
    std::string_view authenticatedServiceId,
    std::string_view authenticatedHostPeerId,
    std::string_view matchId,
    std::vector<RuntimeInternetMatchmakingAction>& actions
) {
    actions.clear();
    if (!impl_ || !impl_->identityMatches(
            authenticatedServiceId,
            authenticatedHostPeerId
        )) {
        return rejected("matchmaking authenticated identity is invalid");
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->prune(nowMilliseconds);
    const auto found = std::find_if(
        impl_->matches.begin(),
        impl_->matches.end(),
        [&](const Impl::Match& match) { return match.matchId == matchId; }
    );
    if (found == impl_->matches.end()) {
        return rejected("active matchmaking assignment was not found");
    }
    if (found->hostPeerId != authenticatedHostPeerId) {
        return rejected("only the assigned host may activate the match");
    }
    actions.reserve(found->admissions.size() - 1U);
    for (const Impl::Admission& admission : found->admissions) {
        if (admission.peerId == found->hostPeerId) continue;
        actions.push_back(Impl::makeAssignment(*found, admission));
    }
    const bool duplicate = found->activated;
    found->activated = true;
    return accepted(!duplicate, duplicate);
}

std::vector<RuntimeInternetMatchmakingAction>
RuntimeInternetMatchmaking::poll(std::string& errorMessage) {
    std::vector<RuntimeInternetMatchmakingAction> result;
    if (!impl_ || !impl_->open) {
        errorMessage = "matchmaking service is not open";
        return result;
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->prune(nowMilliseconds);
    while (impl_->matches.size() < impl_->config.maximumActiveMatches) {
        const std::optional<std::vector<std::size_t>> selected =
            impl_->nextCandidate(nowMilliseconds);
        if (!selected) break;

        Impl::Match match;
        std::vector<RuntimeInternetMatchmakingAction> actions;
        if (!impl_->buildMatch(
                *selected,
                nowMilliseconds,
                match,
                actions,
                errorMessage
            )) {
            return result;
        }
        impl_->matches.push_back(std::move(match));
        result.insert(
            result.end(),
            std::make_move_iterator(actions.begin()),
            std::make_move_iterator(actions.end())
        );

        std::vector<std::size_t> eraseIndices = *selected;
        std::sort(eraseIndices.rbegin(), eraseIndices.rend());
        for (const std::size_t index : eraseIndices) {
            impl_->queue.erase(impl_->queue.begin()
                + static_cast<std::ptrdiff_t>(index));
        }
    }
    errorMessage.clear();
    return result;
}

bool RuntimeInternetMatchmaking::authorizeAdmission(
    std::string_view roomId,
    std::string_view authenticatedPeerId,
    std::span<const std::uint8_t> admissionProof
) const {
    if (!impl_ || !impl_->open
        || !validIdentifier(
            authenticatedPeerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )) {
        return false;
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    const auto match = std::find_if(
        impl_->matches.begin(),
        impl_->matches.end(),
        [&](const Impl::Match& value) {
            return value.roomId == roomId && value.activated
                && value.expiresAtMilliseconds > nowMilliseconds;
        }
    );
    if (match == impl_->matches.end()) return false;
    const auto admission = std::find_if(
        match->admissions.begin(),
        match->admissions.end(),
        [&](const Impl::Admission& value) {
            return value.peerId == authenticatedPeerId;
        }
    );
    return admission != match->admissions.end()
        && constantTimeEquals(admission->proof, admissionProof);
}

RuntimeInternetMatchmakingOperationResult
RuntimeInternetMatchmaking::closeMatch(
    std::string_view authenticatedServiceId,
    std::string_view authenticatedHostPeerId,
    std::string_view matchId
) {
    if (!impl_ || !impl_->identityMatches(
            authenticatedServiceId,
            authenticatedHostPeerId
        )) {
        return rejected("matchmaking authenticated identity is invalid");
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->prune(nowMilliseconds);
    const auto found = std::find_if(
        impl_->matches.begin(),
        impl_->matches.end(),
        [&](const Impl::Match& match) {
            return match.matchId == matchId;
        }
    );
    if (found == impl_->matches.end()) {
        return rejected("active matchmaking assignment was not found");
    }
    if (found->hostPeerId != authenticatedHostPeerId) {
        return rejected("only the assigned host may close the match");
    }
    impl_->matches.erase(found);
    return accepted(true);
}

bool RuntimeInternetMatchmaking::isOpen() const {
    return impl_ && impl_->open;
}

std::size_t RuntimeInternetMatchmaking::queuedPeerCount() const {
    return impl_ ? impl_->queue.size() : 0U;
}

std::size_t RuntimeInternetMatchmaking::activeMatchCount() const {
    return impl_ ? impl_->matches.size() : 0U;
}

void RuntimeInternetMatchmaking::close() {
    if (!impl_) return;
    impl_->config = {};
    impl_->queue.clear();
    impl_->matches.clear();
    impl_->open = false;
}
