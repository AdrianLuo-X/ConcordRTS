#include "RuntimePsychicStatusSystem.h"

#include "IniFile.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

constexpr std::size_t kHardMaxStatuses = 4096;
constexpr std::size_t kHardMaxHitEvents = 16384;
constexpr std::size_t kHardMaxActions = 16384;
constexpr std::size_t kHardMaxInputRecords = 16384;
constexpr std::size_t kHardMaxTextLength = 256;
constexpr int kHardMaxDurationTicks = 1000000;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

RuntimePsychicStatusLimits sanitizeLimits(RuntimePsychicStatusLimits limits) {
    limits.maxStatuses = std::clamp<std::size_t>(
        limits.maxStatuses,
        1,
        kHardMaxStatuses
    );
    limits.maxHitEvents = std::clamp<std::size_t>(
        limits.maxHitEvents,
        1,
        kHardMaxHitEvents
    );
    limits.maxActionsPerOperation = std::clamp<std::size_t>(
        limits.maxActionsPerOperation,
        1,
        kHardMaxActions
    );
    limits.maxInputRecords = std::clamp<std::size_t>(
        limits.maxInputRecords,
        1,
        kHardMaxInputRecords
    );
    limits.maxTextLength = std::clamp<std::size_t>(
        limits.maxTextLength,
        1,
        kHardMaxTextLength
    );
    limits.maxRecursionDepth = std::clamp(
        limits.maxRecursionDepth,
        0,
        1
    );
    limits.maxDurationTicks = std::clamp(
        limits.maxDurationTicks,
        1,
        kHardMaxDurationTicks
    );
    return limits;
}

char lowerAscii(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAscii(left[index]) != lowerAscii(right[index])) {
            return false;
        }
    }
    return true;
}

bool textValid(
    const std::string& text,
    std::size_t maximumLength,
    bool emptyAllowed = false
) {
    return (emptyAllowed || !text.empty())
        && text.size() <= maximumLength
        && text.find('\0') == std::string::npos;
}

bool kindValueValid(int value) {
    return value >= static_cast<int>(RuntimePsychicObjectKind::Infantry)
        && value <= static_cast<int>(RuntimePsychicObjectKind::Other);
}

bool statusTargetKind(RuntimePsychicObjectKind kind) {
    return kind == RuntimePsychicObjectKind::Infantry
        || kind == RuntimePsychicObjectKind::Vehicle;
}

bool validObjectSnapshot(
    const RuntimePsychicObjectSnapshot& object,
    const RuntimePsychicStatusLimits& limits,
    bool requirePresent
) {
    if (object.objectId <= 0
        || !kindValueValid(static_cast<int>(object.kind))) {
        return false;
    }
    if (!object.present) {
        return !requirePresent
            && textValid(object.owner, limits.maxTextLength, true)
            && textValid(object.typeId, limits.maxTextLength, true);
    }
    return textValid(object.owner, limits.maxTextLength)
        && textValid(object.typeId, limits.maxTextLength);
}

bool validCachedSourceSnapshot(
    const RuntimePsychicObjectSnapshot& source,
    const RuntimePsychicStatusLimits& limits
) {
    return source.objectId > 0
        && kindValueValid(static_cast<int>(source.kind))
        && textValid(source.owner, limits.maxTextLength)
        && textValid(source.typeId, limits.maxTextLength);
}

bool sameIdentity(
    const RuntimePsychicStatusRecord& status,
    const RuntimePsychicObjectSnapshot& object,
    bool source
) {
    if (source) {
        return status.sourceObjectId == object.objectId
            && status.sourceKind == object.kind
            && equalsIgnoreCase(status.sourceOwner, object.owner)
            && equalsIgnoreCase(status.sourceTypeId, object.typeId);
    }
    return status.targetObjectId == object.objectId
        && status.targetKind == object.kind
        && equalsIgnoreCase(status.targetOwner, object.owner)
        && equalsIgnoreCase(status.targetTypeId, object.typeId);
}

bool targetEligible(const RuntimePsychicObjectSnapshot& target) {
    return target.present
        && target.alive
        && statusTargetKind(target.kind)
        && !target.airborne
        && !target.immuneToPsionics;
}

bool prepareObjects(
    std::vector<RuntimePsychicObjectSnapshot>& objects,
    const RuntimePsychicStatusLimits& limits,
    std::size_t inputRecordLimit
) {
    if (objects.size() > inputRecordLimit) {
        return false;
    }
    for (const RuntimePsychicObjectSnapshot& object : objects) {
        if (!validObjectSnapshot(object, limits, false)) {
            return false;
        }
    }
    std::sort(
        objects.begin(),
        objects.end(),
        [](const auto& left, const auto& right) {
            return left.objectId < right.objectId;
        }
    );
    return std::adjacent_find(
        objects.begin(),
        objects.end(),
        [](const auto& left, const auto& right) {
            return left.objectId == right.objectId;
        }
    ) == objects.end();
}

const RuntimePsychicObjectSnapshot* findObject(
    const std::vector<RuntimePsychicObjectSnapshot>& objects,
    int objectId
) {
    const auto iterator = std::lower_bound(
        objects.begin(),
        objects.end(),
        objectId,
        [](const RuntimePsychicObjectSnapshot& object, int id) {
            return object.objectId < id;
        }
    );
    return iterator != objects.end() && iterator->objectId == objectId
        ? &*iterator
        : nullptr;
}

RuntimePsychicStatusEndReason reconcileReason(
    const RuntimePsychicStatusRecord& status,
    const std::vector<RuntimePsychicObjectSnapshot>& objects,
    bool validateTargets
) {
    if (validateTargets) {
        const RuntimePsychicObjectSnapshot* target = findObject(
            objects,
            status.targetObjectId
        );
        if (target == nullptr || !target->present) {
            return RuntimePsychicStatusEndReason::TargetMissing;
        }
        if (!sameIdentity(status, *target, false)) {
            return RuntimePsychicStatusEndReason::TargetIdentityChanged;
        }
        if (!target->alive) {
            return RuntimePsychicStatusEndReason::TargetDead;
        }
        if (!targetEligible(*target)) {
            return RuntimePsychicStatusEndReason::TargetBecameIneligible;
        }
    }
    return RuntimePsychicStatusEndReason::None;
}

bool sourceIdentityChanged(
    const RuntimePsychicStatusRecord& status,
    const std::vector<RuntimePsychicObjectSnapshot>& objects,
    bool validateSources
) {
    if (!validateSources) {
        return false;
    }
    const RuntimePsychicObjectSnapshot* source = findObject(
        objects,
        status.sourceObjectId
    );
    return source != nullptr
        && source->present
        && !sameIdentity(status, *source, true);
}

RuntimePsychicStatusAction actionFor(
    const RuntimePsychicStatusRecord& status,
    RuntimePsychicStatusActionKind kind,
    int tick,
    RuntimePsychicStatusEndReason endReason =
        RuntimePsychicStatusEndReason::None
) {
    RuntimePsychicStatusAction action;
    action.kind = kind;
    action.endReason = endReason;
    action.tick = tick;
    action.statusId = status.statusId;
    action.hitEventId = status.lastHitEventId;
    action.sourceObjectId = status.sourceObjectId;
    action.targetObjectId = status.targetObjectId;
    action.sourceOwner = status.sourceOwner;
    action.targetOwner = status.targetOwner;
    action.warheadId = status.warheadId;
    action.startedTick = status.startedTick;
    action.expiresTick = status.expiresTick;
    action.attackOriginalAllies = status.attackOriginalAllies;
    action.sourceValidatedAtLaunch = status.sourceValidatedAtLaunch;
    return action;
}

bool exactEventReplay(
    const RuntimePsychicStatusHitEventRecord& event,
    const RuntimePsychicStatusHitRequest& request
) {
    return event.projectileActorId == request.hitEventId
        && event.targetObjectId == request.target.objectId
        && event.sourceObjectId == request.source.objectId
        && equalsIgnoreCase(event.sourceOwner, request.source.owner)
        && equalsIgnoreCase(event.sourceTypeId, request.source.typeId)
        && event.sourceKind == request.source.kind
        && equalsIgnoreCase(event.targetOwner, request.target.owner)
        && equalsIgnoreCase(event.targetTypeId, request.target.typeId)
        && event.targetKind == request.target.kind
        && equalsIgnoreCase(event.warheadId, request.warhead.warheadId);
}

RuntimePsychicStatusHitEventRecord eventFor(
    const RuntimePsychicStatusHitRequest& request,
    std::uint64_t statusId
) {
    RuntimePsychicStatusHitEventRecord event;
    event.projectileActorId = request.hitEventId;
    event.targetObjectId = request.target.objectId;
    event.statusId = statusId;
    event.acceptedTick = request.currentTick;
    event.sourceObjectId = request.source.objectId;
    event.sourceOwner = request.source.owner;
    event.sourceTypeId = request.source.typeId;
    event.sourceKind = request.source.kind;
    event.targetOwner = request.target.owner;
    event.targetTypeId = request.target.typeId;
    event.targetKind = request.target.kind;
    event.warheadId = request.warhead.warheadId;
    return event;
}

std::string hexEncode(std::string_view text) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(text.size() * 2);
    for (const unsigned char value : text) {
        encoded.push_back(digits[value >> 4]);
        encoded.push_back(digits[value & 0x0F]);
    }
    return encoded;
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool hexDecode(
    const std::string& encoded,
    std::size_t maximumLength,
    std::string& decoded
) {
    if ((encoded.size() % 2) != 0
        || encoded.size() / 2 > maximumLength) {
        return false;
    }
    std::string candidate;
    candidate.reserve(encoded.size() / 2);
    for (std::size_t index = 0; index < encoded.size(); index += 2) {
        const int high = hexDigit(encoded[index]);
        const int low = hexDigit(encoded[index + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        const char value = static_cast<char>((high << 4) | low);
        if (value == '\0') {
            return false;
        }
        candidate.push_back(value);
    }
    decoded = std::move(candidate);
    return true;
}

bool parseInt(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    int candidate = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, candidate);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    value = candidate;
    return true;
}

bool parseUint64(const std::string& text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t candidate = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, candidate);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    value = candidate;
    return true;
}

bool parseSize(const std::string& text, std::size_t& value) {
    std::uint64_t candidate = 0;
    if (!parseUint64(text, candidate)
        || candidate > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    value = static_cast<std::size_t>(candidate);
    return true;
}

bool parseBool(const std::string& text, bool& value) {
    if (text == "0") {
        value = false;
        return true;
    }
    if (text == "1") {
        value = true;
        return true;
    }
    return false;
}

bool loadedStatusValid(
    const RuntimePsychicStatusRecord& status,
    const RuntimePsychicStatusLimits& limits,
    int lastAdvancedTick
) {
    return status.statusId > 0
        && status.sourceObjectId > 0
        && status.targetObjectId > 0
        && status.sourceObjectId != status.targetObjectId
        && kindValueValid(static_cast<int>(status.sourceKind))
        && statusTargetKind(status.targetKind)
        && textValid(status.sourceOwner, limits.maxTextLength)
        && textValid(status.sourceTypeId, limits.maxTextLength)
        && textValid(status.targetOwner, limits.maxTextLength)
        && textValid(status.targetTypeId, limits.maxTextLength)
        && textValid(status.warheadId, limits.maxTextLength)
        && status.startedTick >= 0
        && status.lastRefreshTick >= status.startedTick
        && status.expiresTick > status.lastRefreshTick
        && status.lastHitEventId > 0
        && (lastAdvancedTick < 0 || status.expiresTick > lastAdvancedTick);
}

bool loadedEventValid(
    const RuntimePsychicStatusHitEventRecord& event,
    const RuntimePsychicStatusLimits& limits
) {
    return event.projectileActorId > 0
        && event.targetObjectId > 0
        && event.statusId > 0
        && event.acceptedTick >= 0
        && event.sourceObjectId > 0
        && kindValueValid(static_cast<int>(event.sourceKind))
        && statusTargetKind(event.targetKind)
        && textValid(event.sourceOwner, limits.maxTextLength)
        && textValid(event.sourceTypeId, limits.maxTextLength)
        && textValid(event.targetOwner, limits.maxTextLength)
        && textValid(event.targetTypeId, limits.maxTextLength)
        && textValid(event.warheadId, limits.maxTextLength);
}

void eraseEventsForStatus(
    std::vector<RuntimePsychicStatusHitEventRecord>& events,
    std::uint64_t statusId
) {
    events.erase(
        std::remove_if(
            events.begin(),
            events.end(),
            [statusId](const RuntimePsychicStatusHitEventRecord& event) {
                return event.statusId == statusId;
            }
        ),
        events.end()
    );
}

void hashByte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void hashUint64(std::uint64_t& hash, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        hashByte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hashInt(std::uint64_t& hash, int value) {
    hashUint64(hash, static_cast<std::uint64_t>(
        static_cast<std::int64_t>(value)
    ));
}

void hashBool(std::uint64_t& hash, bool value) {
    hashByte(hash, value ? 1 : 0);
}

void hashText(std::uint64_t& hash, const std::string& text) {
    hashUint64(hash, text.size());
    for (const unsigned char value : text) {
        hashByte(hash, value);
    }
}

}  // namespace

bool runtimePsychicLegacyFramesToFixedTicks(
    int authoredFrames,
    int fixedTickHz,
    int& outputTicks
) {
    if (authoredFrames < 0 || fixedTickHz <= 0) {
        return false;
    }
    if (authoredFrames == 0) {
        outputTicks = 0;
        return true;
    }
    const std::int64_t product = static_cast<std::int64_t>(authoredFrames)
        * static_cast<std::int64_t>(fixedTickHz);
    const std::int64_t ticks = product / 15
        + (product % 15 == 0 ? 0 : 1);
    if (ticks <= 0 || ticks > std::numeric_limits<int>::max()) {
        return false;
    }
    outputTicks = static_cast<int>(ticks);
    return true;
}

RuntimePsychicStatusSystemState::RuntimePsychicStatusSystemState()
    : limits_(sanitizeLimits({})) {}

RuntimePsychicStatusSystemState::RuntimePsychicStatusSystemState(
    RuntimePsychicStatusLimits limits
) : limits_(sanitizeLimits(limits)) {}

void RuntimePsychicStatusSystemState::reset(
    RuntimePsychicStatusLimits limits
) {
    limits_ = sanitizeLimits(limits);
    lastAdvancedTick_ = -1;
    nextStatusId_ = 1;
    statuses_.clear();
    hitEvents_.clear();
}

RuntimePsychicStatusHitResult RuntimePsychicStatusSystemState::applyHit(
    const RuntimePsychicStatusHitRequest& request
) {
    RuntimePsychicStatusHitResult result;
    auto veto = [&](RuntimePsychicStatusVeto reason) {
        result.veto = reason;
        return result;
    };

    if (request.currentTick < 0
        || request.currentTick < lastAdvancedTick_) {
        return veto(RuntimePsychicStatusVeto::InvalidTick);
    }
    if (request.hitEventId == 0) {
        return veto(RuntimePsychicStatusVeto::InvalidEventId);
    }
    if (request.recursionDepth < 0
        || request.recursionDepth > limits_.maxRecursionDepth) {
        return veto(RuntimePsychicStatusVeto::RecursiveRequest);
    }
    const bool sourceValid = request.sourceValidatedAtLaunch
        ? validCachedSourceSnapshot(request.source, limits_)
        : validObjectSnapshot(request.source, limits_, true)
            && request.source.alive;
    if (!sourceValid) {
        return veto(RuntimePsychicStatusVeto::InvalidSource);
    }
    if (!validObjectSnapshot(request.target, limits_, true)
        || !request.target.alive) {
        return veto(RuntimePsychicStatusVeto::InvalidTarget);
    }
    if (request.source.objectId == request.target.objectId) {
        return veto(RuntimePsychicStatusVeto::SelfTarget);
    }
    if (!statusTargetKind(request.target.kind)) {
        return veto(RuntimePsychicStatusVeto::IllegalTargetKind);
    }
    if (request.target.airborne) {
        return veto(RuntimePsychicStatusVeto::TargetAirborne);
    }
    if (request.target.immuneToPsionics) {
        return veto(RuntimePsychicStatusVeto::TargetImmune);
    }
    if (!request.warhead.warheadExists
        || !request.warhead.psychedelic
        || !textValid(
            request.warhead.warheadId,
            limits_.maxTextLength
        )) {
        return veto(RuntimePsychicStatusVeto::InvalidWarhead);
    }
    if (request.durationTicks < 1
        || request.durationTicks > limits_.maxDurationTicks
        || request.currentTick
            > std::numeric_limits<int>::max() - request.durationTicks) {
        return veto(RuntimePsychicStatusVeto::InvalidDuration);
    }

    for (const RuntimePsychicStatusHitEventRecord& event : hitEvents_) {
        if (event.projectileActorId != request.hitEventId
            || event.targetObjectId != request.target.objectId) {
            continue;
        }
        if (exactEventReplay(event, request)) {
            result.accepted = true;
            result.idempotent = true;
            result.statusId = event.statusId;
            return result;
        }
        return veto(RuntimePsychicStatusVeto::EventIdConflict);
    }

    RuntimePsychicStatusSystemState candidate = *this;
    auto found = std::lower_bound(
        candidate.statuses_.begin(),
        candidate.statuses_.end(),
        request.target.objectId,
        [](const RuntimePsychicStatusRecord& status, int targetId) {
            return status.targetObjectId < targetId;
        }
    );

    if (found != candidate.statuses_.end()
        && found->targetObjectId == request.target.objectId
        && (request.currentTick >= found->expiresTick
            || !sameIdentity(*found, request.target, false))) {
        const RuntimePsychicStatusEndReason reason =
            request.currentTick >= found->expiresTick
                ? RuntimePsychicStatusEndReason::Expired
                : RuntimePsychicStatusEndReason::TargetIdentityChanged;
        if (result.actions.size() >= limits_.maxActionsPerOperation) {
            return veto(RuntimePsychicStatusVeto::ActionLimitReached);
        }
        result.actions.push_back(actionFor(
            *found,
            RuntimePsychicStatusActionKind::Ended,
            request.currentTick,
            reason
        ));
        eraseEventsForStatus(candidate.hitEvents_, found->statusId);
        found = candidate.statuses_.erase(found);
    }

    if (found != candidate.statuses_.end()
        && found->targetObjectId == request.target.objectId) {
        if (result.actions.size() >= limits_.maxActionsPerOperation) {
            result.actions.clear();
            return veto(RuntimePsychicStatusVeto::ActionLimitReached);
        }
        found->sourceObjectId = request.source.objectId;
        found->sourceOwner = request.source.owner;
        found->sourceTypeId = request.source.typeId;
        found->sourceKind = request.source.kind;
        found->targetOwner = request.target.owner;
        found->targetTypeId = request.target.typeId;
        found->targetKind = request.target.kind;
        found->warheadId = request.warhead.warheadId;
        found->attackOriginalAllies = true;
        found->sourceValidatedAtLaunch =
            request.sourceValidatedAtLaunch;
        found->lastRefreshTick = request.currentTick;
        found->expiresTick = std::max(
            found->expiresTick,
            request.currentTick + request.durationTicks
        );
        found->lastHitEventId = request.hitEventId;
        result.statusId = found->statusId;
        result.actions.push_back(actionFor(
            *found,
            RuntimePsychicStatusActionKind::Refreshed,
            request.currentTick
        ));
    } else {
        if (candidate.statuses_.size() >= limits_.maxStatuses) {
            result.actions.clear();
            return veto(RuntimePsychicStatusVeto::CapacityReached);
        }
        if (candidate.nextStatusId_
            == std::numeric_limits<std::uint64_t>::max()) {
            result.actions.clear();
            return veto(RuntimePsychicStatusVeto::IdExhausted);
        }
        if (result.actions.size() >= limits_.maxActionsPerOperation) {
            result.actions.clear();
            return veto(RuntimePsychicStatusVeto::ActionLimitReached);
        }
        RuntimePsychicStatusRecord status;
        status.statusId = candidate.nextStatusId_++;
        status.sourceObjectId = request.source.objectId;
        status.sourceOwner = request.source.owner;
        status.sourceTypeId = request.source.typeId;
        status.sourceKind = request.source.kind;
        status.targetObjectId = request.target.objectId;
        status.targetOwner = request.target.owner;
        status.targetTypeId = request.target.typeId;
        status.targetKind = request.target.kind;
        status.warheadId = request.warhead.warheadId;
        status.attackOriginalAllies = true;
        status.sourceValidatedAtLaunch = request.sourceValidatedAtLaunch;
        status.startedTick = request.currentTick;
        status.lastRefreshTick = request.currentTick;
        status.expiresTick = request.currentTick + request.durationTicks;
        status.lastHitEventId = request.hitEventId;
        result.statusId = status.statusId;
        result.actions.push_back(actionFor(
            status,
            RuntimePsychicStatusActionKind::Applied,
            request.currentTick
        ));
        candidate.statuses_.insert(found, std::move(status));
    }

    if (candidate.hitEvents_.size() >= limits_.maxHitEvents) {
        result.actions.clear();
        return veto(RuntimePsychicStatusVeto::EventCapacityReached);
    }
    candidate.hitEvents_.push_back(eventFor(request, result.statusId));

    candidate.sortState();
    *this = std::move(candidate);
    result.accepted = true;
    return result;
}

RuntimePsychicStatusAdvanceResult RuntimePsychicStatusSystemState::advance(
    const RuntimePsychicStatusAdvanceInput& input
) {
    RuntimePsychicStatusAdvanceResult result;
    if (input.currentTick < 0
        || input.currentTick <= lastAdvancedTick_) {
        result.inputRejected = true;
        return result;
    }
    RuntimePsychicStatusAdvanceInput prepared = input;
    if ((input.validateSources || input.validateTargets)
        && !prepareObjects(prepared.objects, limits_, limits_.maxInputRecords)) {
        result.inputRejected = true;
        return result;
    }

    RuntimePsychicStatusSystemState candidate = *this;
    std::set<std::uint64_t> removeIds;
    for (const RuntimePsychicStatusRecord& status : candidate.statuses_) {
        if (sourceIdentityChanged(
                status,
                prepared.objects,
                input.validateSources
            )) {
            result.sourceIdentityChangedStatusIds.push_back(status.statusId);
        }
        RuntimePsychicStatusEndReason reason = reconcileReason(
            status,
            prepared.objects,
            input.validateTargets
        );
        if (reason == RuntimePsychicStatusEndReason::None
            && input.currentTick >= status.expiresTick) {
            reason = RuntimePsychicStatusEndReason::Expired;
        }
        if (reason == RuntimePsychicStatusEndReason::None) {
            ++result.retained;
            continue;
        }
        if (result.actions.size() >= limits_.maxActionsPerOperation) {
            result.actionLimitReached = true;
            result.actions.clear();
            result.sourceIdentityChangedStatusIds.clear();
            result.retained = 0;
            result.ended = 0;
            return result;
        }
        result.actions.push_back(actionFor(
            status,
            RuntimePsychicStatusActionKind::Ended,
            input.currentTick,
            reason
        ));
        removeIds.insert(status.statusId);
        ++result.ended;
    }
    candidate.statuses_.erase(
        std::remove_if(
            candidate.statuses_.begin(),
            candidate.statuses_.end(),
            [&](const RuntimePsychicStatusRecord& status) {
                return removeIds.contains(status.statusId);
            }
        ),
        candidate.statuses_.end()
    );
    candidate.hitEvents_.erase(
        std::remove_if(
            candidate.hitEvents_.begin(),
            candidate.hitEvents_.end(),
            [&](const RuntimePsychicStatusHitEventRecord& event) {
                return removeIds.contains(event.statusId);
            }
        ),
        candidate.hitEvents_.end()
    );
    candidate.lastAdvancedTick_ = input.currentTick;
    candidate.sortState();
    *this = std::move(candidate);
    result.advanced = true;
    return result;
}

RuntimePsychicStatusReconcileReport
RuntimePsychicStatusSystemState::reconcile(
    const RuntimePsychicStatusReconcileInput& input
) {
    return reconcileWithInputRecordLimit(input, limits_.maxInputRecords);
}

RuntimePsychicStatusReconcileReport
RuntimePsychicStatusSystemState::reconcileSavedWorld(
    const RuntimePsychicStatusReconcileInput& input,
    std::size_t validatedObjectCount
) {
    // This is the normal complete saved-object parser's existing hard bound.
    // Do not sanitize/store this per-operation count in limits_: limits also
    // participate in the authoritative state hash and generic API behavior.
    if (validatedObjectCount > 100000U
        || input.objects.size() != validatedObjectCount) {
        RuntimePsychicStatusReconcileReport rejected;
        rejected.inputRejected = true;
        return rejected;
    }
    return reconcileWithInputRecordLimit(input, validatedObjectCount);
}

RuntimePsychicStatusReconcileReport
RuntimePsychicStatusSystemState::reconcileWithInputRecordLimit(
    const RuntimePsychicStatusReconcileInput& input,
    std::size_t inputRecordLimit
) {
    RuntimePsychicStatusReconcileReport report;
    if (input.currentTick < 0
        || input.currentTick < lastAdvancedTick_) {
        report.inputRejected = true;
        return report;
    }
    RuntimePsychicStatusReconcileInput prepared = input;
    if ((input.validateSources || input.validateTargets)
        && !prepareObjects(prepared.objects, limits_, inputRecordLimit)) {
        report.inputRejected = true;
        return report;
    }

    RuntimePsychicStatusSystemState candidate = *this;
    std::set<std::uint64_t> removeIds;
    for (const RuntimePsychicStatusRecord& status : candidate.statuses_) {
        if (sourceIdentityChanged(
                status,
                prepared.objects,
                input.validateSources
            )) {
            report.sourceIdentityChangedStatusIds.push_back(status.statusId);
        }
        const RuntimePsychicStatusEndReason reason = reconcileReason(
            status,
            prepared.objects,
            input.validateTargets
        );
        if (reason == RuntimePsychicStatusEndReason::None) {
            ++report.retained;
            continue;
        }
        if (report.actions.size() >= limits_.maxActionsPerOperation) {
            report.actionLimitReached = true;
            report.actions.clear();
            report.sourceIdentityChangedStatusIds.clear();
            report.retained = 0;
            report.ended = 0;
            return report;
        }
        report.actions.push_back(actionFor(
            status,
            RuntimePsychicStatusActionKind::Ended,
            input.currentTick,
            reason
        ));
        removeIds.insert(status.statusId);
        ++report.ended;
    }
    candidate.statuses_.erase(
        std::remove_if(
            candidate.statuses_.begin(),
            candidate.statuses_.end(),
            [&](const RuntimePsychicStatusRecord& status) {
                return removeIds.contains(status.statusId);
            }
        ),
        candidate.statuses_.end()
    );
    candidate.hitEvents_.erase(
        std::remove_if(
            candidate.hitEvents_.begin(),
            candidate.hitEvents_.end(),
            [&](const RuntimePsychicStatusHitEventRecord& event) {
                return removeIds.contains(event.statusId);
            }
        ),
        candidate.hitEvents_.end()
    );
    candidate.sortState();
    *this = std::move(candidate);
    return report;
}

RuntimePsychicTargetingResult
RuntimePsychicStatusSystemState::queryTargeting(
    const RuntimePsychicTargetingQuery& query
) const {
    RuntimePsychicTargetingResult result;
    result.canAttack = query.hostTargetLegal;
    result.effectiveRelation = query.originalRelation;
    if (query.currentTick < 0 || query.actor.objectId <= 0) {
        return result;
    }
    const RuntimePsychicStatusRecord* status = findByTarget(
        query.actor.objectId
    );
    if (status == nullptr) {
        return result;
    }
    result.statusFound = true;
    result.statusId = status->statusId;
    result.expiresTick = status->expiresTick;
    if (query.currentTick >= status->expiresTick
        || !query.actor.present
        || !query.actor.alive
        || !sameIdentity(*status, query.actor, false)
        || !targetEligible(query.actor)) {
        return result;
    }

    result.berserkActive = true;
    result.overridesRelation = true;
    // Old saves retain their serialized metadata, but berserk always ignores alliances.
    result.attackOriginalAllies = true;
    bool hostile = false;
    switch (query.originalRelation) {
    case RuntimePsychicRelation::Enemy:
    case RuntimePsychicRelation::Neutral:
        hostile = true;
        break;
    case RuntimePsychicRelation::Allied:
        hostile = true;
        break;
    case RuntimePsychicRelation::Self:
    case RuntimePsychicRelation::Unknown:
        hostile = false;
        break;
    }
    if (!query.candidate.present
        || !query.candidate.alive
        || !query.candidate.attackable
        // Retail CanFire (0x6FC1CC): BerserkFriendly protects the candidate.
        || query.candidate.berserkFriendly
        || query.candidate.objectId <= 0
        || query.candidate.objectId == query.actor.objectId) {
        hostile = false;
    }
    result.canAttack = query.hostTargetLegal && hostile;
    if (hostile) {
        result.effectiveRelation = RuntimePsychicRelation::Enemy;
    }
    return result;
}

void RuntimePsychicStatusSystemState::save(std::ostream& output) const {
    output.imbue(std::locale::classic());
    output << "[RuntimePsychicStatus]\n"
           << "Schema=2\n"
           << "LastAdvancedTick=" << lastAdvancedTick_ << "\n"
           << "NextStatusId=" << nextStatusId_ << "\n"
           << "Count=" << statuses_.size() << "\n"
           << "HitEventCount=" << hitEvents_.size() << "\n\n";
    for (std::size_t index = 0; index < statuses_.size(); ++index) {
        const RuntimePsychicStatusRecord& status = statuses_[index];
        output << "[RuntimePsychicStatus.Record." << index << "]\n"
               << "StatusId=" << status.statusId << "\n"
               << "SourceObjectId=" << status.sourceObjectId << "\n"
               << "SourceOwner=" << hexEncode(status.sourceOwner) << "\n"
               << "SourceType=" << hexEncode(status.sourceTypeId) << "\n"
               << "SourceKind=" << static_cast<int>(status.sourceKind)
               << "\n"
               << "TargetObjectId=" << status.targetObjectId << "\n"
               << "TargetOwner=" << hexEncode(status.targetOwner) << "\n"
               << "TargetType=" << hexEncode(status.targetTypeId) << "\n"
               << "TargetKind=" << static_cast<int>(status.targetKind)
               << "\n"
               << "Warhead=" << hexEncode(status.warheadId) << "\n"
               << "AttackOriginalAllies="
               << (status.attackOriginalAllies ? 1 : 0) << "\n"
               << "SourceValidatedAtLaunch="
               << (status.sourceValidatedAtLaunch ? 1 : 0) << "\n"
               << "StartedTick=" << status.startedTick << "\n"
               << "LastRefreshTick=" << status.lastRefreshTick << "\n"
               << "ExpiresTick=" << status.expiresTick << "\n"
               << "LastHitEventId=" << status.lastHitEventId << "\n\n";
    }
    for (std::size_t index = 0; index < hitEvents_.size(); ++index) {
        const RuntimePsychicStatusHitEventRecord& event = hitEvents_[index];
        output << "[RuntimePsychicStatus.HitEvent." << index << "]\n"
               << "ProjectileActorId=" << event.projectileActorId << "\n"
               << "TargetObjectId=" << event.targetObjectId << "\n"
               << "StatusId=" << event.statusId << "\n"
               << "AcceptedTick=" << event.acceptedTick << "\n"
               << "SourceObjectId=" << event.sourceObjectId << "\n"
               << "SourceOwner=" << hexEncode(event.sourceOwner) << "\n"
               << "SourceType=" << hexEncode(event.sourceTypeId) << "\n"
               << "SourceKind=" << static_cast<int>(event.sourceKind)
               << "\n"
               << "TargetOwner=" << hexEncode(event.targetOwner) << "\n"
               << "TargetType=" << hexEncode(event.targetTypeId) << "\n"
               << "TargetKind=" << static_cast<int>(event.targetKind)
               << "\n"
               << "Warhead=" << hexEncode(event.warheadId) << "\n\n";
    }
}

bool RuntimePsychicStatusSystemState::load(
    const IniFile& ini,
    std::string* errorMessage
) {
    auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    const std::string schema = ini.getValue(
        "RuntimePsychicStatus",
        "Schema",
        ""
    );
    if (schema != "1" && schema != "2") {
        return fail("unsupported RuntimePsychicStatus schema");
    }
    const bool schemaTwo = schema == "2";

    RuntimePsychicStatusSystemState loaded(limits_);
    std::size_t count = 0;
    std::size_t hitEventCount = 0;
    if (!parseInt(
            ini.getValue(
                "RuntimePsychicStatus",
                "LastAdvancedTick",
                ""
            ),
            loaded.lastAdvancedTick_
        )
        || loaded.lastAdvancedTick_ < -1
        || !parseUint64(
            ini.getValue("RuntimePsychicStatus", "NextStatusId", ""),
            loaded.nextStatusId_
        )
        || loaded.nextStatusId_ == 0
        || !parseSize(
            ini.getValue("RuntimePsychicStatus", "Count", ""),
            count
        )
        || count > limits_.maxStatuses
        || (schemaTwo
            && !parseSize(
                ini.getValue(
                    "RuntimePsychicStatus",
                    "HitEventCount",
                    ""
                ),
                hitEventCount
            ))
        || (schemaTwo && hitEventCount > limits_.maxHitEvents)
        || (!schemaTwo && count > limits_.maxHitEvents)) {
        return fail("invalid RuntimePsychicStatus header");
    }
    if (!schemaTwo) {
        hitEventCount = count;
    }

    loaded.statuses_.reserve(count);
    std::set<std::uint64_t> statusIds;
    std::set<int> targetIds;
    std::uint64_t maximumStatusId = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const std::string section =
            "RuntimePsychicStatus.Record." + std::to_string(index);
        RuntimePsychicStatusRecord status;
        int sourceKind = -1;
        int targetKind = -1;
        if (!parseUint64(
                ini.getValue(section, "StatusId", ""),
                status.statusId
            )
            || !parseInt(
                ini.getValue(section, "SourceObjectId", ""),
                status.sourceObjectId
            )
            || !hexDecode(
                ini.getValue(section, "SourceOwner", ""),
                limits_.maxTextLength,
                status.sourceOwner
            )
            || !hexDecode(
                ini.getValue(section, "SourceType", ""),
                limits_.maxTextLength,
                status.sourceTypeId
            )
            || !parseInt(
                ini.getValue(section, "SourceKind", ""),
                sourceKind
            )
            || !kindValueValid(sourceKind)
            || !parseInt(
                ini.getValue(section, "TargetObjectId", ""),
                status.targetObjectId
            )
            || !hexDecode(
                ini.getValue(section, "TargetOwner", ""),
                limits_.maxTextLength,
                status.targetOwner
            )
            || !hexDecode(
                ini.getValue(section, "TargetType", ""),
                limits_.maxTextLength,
                status.targetTypeId
            )
            || !parseInt(
                ini.getValue(section, "TargetKind", ""),
                targetKind
            )
            || !kindValueValid(targetKind)
            || !hexDecode(
                ini.getValue(section, "Warhead", ""),
                limits_.maxTextLength,
                status.warheadId
            )
            || !parseBool(
                ini.getValue(section, "AttackOriginalAllies", ""),
                status.attackOriginalAllies
            )
            || !parseInt(
                ini.getValue(section, "StartedTick", ""),
                status.startedTick
            )
            || !parseInt(
                ini.getValue(section, "LastRefreshTick", ""),
                status.lastRefreshTick
            )
            || !parseInt(
                ini.getValue(section, "ExpiresTick", ""),
                status.expiresTick
            )
            || !parseUint64(
                ini.getValue(section, "LastHitEventId", ""),
                status.lastHitEventId
            )) {
            return fail("invalid RuntimePsychicStatus record "
                + std::to_string(index));
        }
        if (schemaTwo
            && !parseBool(
                ini.getValue(
                    section,
                    "SourceValidatedAtLaunch",
                    ""
                ),
                status.sourceValidatedAtLaunch
            )) {
            return fail("invalid RuntimePsychicStatus source attribution "
                + std::to_string(index));
        }
        status.sourceKind = static_cast<RuntimePsychicObjectKind>(sourceKind);
        status.targetKind = static_cast<RuntimePsychicObjectKind>(targetKind);
        if (!loadedStatusValid(status, limits_, loaded.lastAdvancedTick_)
            || !statusIds.insert(status.statusId).second
            || !targetIds.insert(status.targetObjectId).second) {
            return fail("unsafe RuntimePsychicStatus record "
                + std::to_string(index));
        }
        maximumStatusId = std::max(maximumStatusId, status.statusId);
        loaded.statuses_.push_back(std::move(status));
    }
    if (loaded.nextStatusId_ <= maximumStatusId) {
        return fail("RuntimePsychicStatus next status id is not monotonic");
    }

    loaded.hitEvents_.reserve(hitEventCount);
    if (!schemaTwo) {
        for (const RuntimePsychicStatusRecord& status : loaded.statuses_) {
            RuntimePsychicStatusHitEventRecord event;
            event.projectileActorId = status.lastHitEventId;
            event.targetObjectId = status.targetObjectId;
            event.statusId = status.statusId;
            event.acceptedTick = status.lastRefreshTick;
            event.sourceObjectId = status.sourceObjectId;
            event.sourceOwner = status.sourceOwner;
            event.sourceTypeId = status.sourceTypeId;
            event.sourceKind = status.sourceKind;
            event.targetOwner = status.targetOwner;
            event.targetTypeId = status.targetTypeId;
            event.targetKind = status.targetKind;
            event.warheadId = status.warheadId;
            loaded.hitEvents_.push_back(std::move(event));
        }
    } else {
        std::set<std::pair<std::uint64_t, int>> eventKeys;
        for (std::size_t index = 0; index < hitEventCount; ++index) {
            const std::string section =
                "RuntimePsychicStatus.HitEvent." + std::to_string(index);
            RuntimePsychicStatusHitEventRecord event;
            int sourceKind = -1;
            int targetKind = -1;
            if (!parseUint64(
                    ini.getValue(section, "ProjectileActorId", ""),
                    event.projectileActorId
                )
                || !parseInt(
                    ini.getValue(section, "TargetObjectId", ""),
                    event.targetObjectId
                )
                || !parseUint64(
                    ini.getValue(section, "StatusId", ""),
                    event.statusId
                )
                || !parseInt(
                    ini.getValue(section, "AcceptedTick", ""),
                    event.acceptedTick
                )
                || !parseInt(
                    ini.getValue(section, "SourceObjectId", ""),
                    event.sourceObjectId
                )
                || !hexDecode(
                    ini.getValue(section, "SourceOwner", ""),
                    limits_.maxTextLength,
                    event.sourceOwner
                )
                || !hexDecode(
                    ini.getValue(section, "SourceType", ""),
                    limits_.maxTextLength,
                    event.sourceTypeId
                )
                || !parseInt(
                    ini.getValue(section, "SourceKind", ""),
                    sourceKind
                )
                || !kindValueValid(sourceKind)
                || !hexDecode(
                    ini.getValue(section, "TargetOwner", ""),
                    limits_.maxTextLength,
                    event.targetOwner
                )
                || !hexDecode(
                    ini.getValue(section, "TargetType", ""),
                    limits_.maxTextLength,
                    event.targetTypeId
                )
                || !parseInt(
                    ini.getValue(section, "TargetKind", ""),
                    targetKind
                )
                || !kindValueValid(targetKind)
                || !hexDecode(
                    ini.getValue(section, "Warhead", ""),
                    limits_.maxTextLength,
                    event.warheadId
                )) {
                return fail("invalid RuntimePsychicStatus hit event "
                    + std::to_string(index));
            }
            event.sourceKind =
                static_cast<RuntimePsychicObjectKind>(sourceKind);
            event.targetKind =
                static_cast<RuntimePsychicObjectKind>(targetKind);
            const auto status = std::find_if(
                loaded.statuses_.begin(),
                loaded.statuses_.end(),
                [&](const RuntimePsychicStatusRecord& candidate) {
                    return candidate.statusId == event.statusId;
                }
            );
            if (!loadedEventValid(event, limits_)
                || event.sourceObjectId == event.targetObjectId
                || status == loaded.statuses_.end()
                || status->targetObjectId != event.targetObjectId
                || status->targetKind != event.targetKind
                || !equalsIgnoreCase(status->targetOwner, event.targetOwner)
                || !equalsIgnoreCase(status->targetTypeId, event.targetTypeId)
                || event.acceptedTick < status->startedTick
                || event.acceptedTick > status->lastRefreshTick
                || event.acceptedTick >= status->expiresTick
                || !eventKeys.emplace(
                    event.projectileActorId,
                    event.targetObjectId
                ).second) {
                return fail("unsafe RuntimePsychicStatus hit event "
                    + std::to_string(index));
            }
            loaded.hitEvents_.push_back(std::move(event));
        }

        for (const RuntimePsychicStatusRecord& status : loaded.statuses_) {
            const bool latestReceiptFound = std::any_of(
                loaded.hitEvents_.begin(),
                loaded.hitEvents_.end(),
                [&](const RuntimePsychicStatusHitEventRecord& event) {
                    return event.statusId == status.statusId
                        && event.projectileActorId
                            == status.lastHitEventId
                        && event.targetObjectId == status.targetObjectId
                        && event.acceptedTick == status.lastRefreshTick
                        && event.sourceObjectId == status.sourceObjectId
                        && event.sourceKind == status.sourceKind
                        && equalsIgnoreCase(
                            event.sourceOwner,
                            status.sourceOwner
                        )
                        && equalsIgnoreCase(
                            event.sourceTypeId,
                            status.sourceTypeId
                        )
                        && equalsIgnoreCase(event.warheadId, status.warheadId);
                }
            );
            if (!latestReceiptFound) {
                return fail("RuntimePsychicStatus is missing its latest hit "
                    "event receipt");
            }
        }
    }
    loaded.sortState();
    *this = std::move(loaded);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

std::uint64_t RuntimePsychicStatusSystemState::stateHash() const {
    std::uint64_t hash = kFnvOffset;
    hashInt(hash, lastAdvancedTick_);
    hashUint64(hash, nextStatusId_);
    hashUint64(hash, limits_.maxStatuses);
    hashUint64(hash, limits_.maxHitEvents);
    hashUint64(hash, limits_.maxActionsPerOperation);
    hashUint64(hash, limits_.maxInputRecords);
    hashUint64(hash, limits_.maxTextLength);
    hashInt(hash, limits_.maxRecursionDepth);
    hashInt(hash, limits_.maxDurationTicks);
    hashUint64(hash, statuses_.size());
    for (const RuntimePsychicStatusRecord& status : statuses_) {
        hashUint64(hash, status.statusId);
        hashInt(hash, status.sourceObjectId);
        hashText(hash, status.sourceOwner);
        hashText(hash, status.sourceTypeId);
        hashByte(hash, static_cast<std::uint8_t>(status.sourceKind));
        hashInt(hash, status.targetObjectId);
        hashText(hash, status.targetOwner);
        hashText(hash, status.targetTypeId);
        hashByte(hash, static_cast<std::uint8_t>(status.targetKind));
        hashText(hash, status.warheadId);
        hashBool(hash, status.attackOriginalAllies);
        hashBool(hash, status.sourceValidatedAtLaunch);
        hashInt(hash, status.startedTick);
        hashInt(hash, status.lastRefreshTick);
        hashInt(hash, status.expiresTick);
        hashUint64(hash, status.lastHitEventId);
    }
    hashUint64(hash, hitEvents_.size());
    for (const RuntimePsychicStatusHitEventRecord& event : hitEvents_) {
        hashUint64(hash, event.projectileActorId);
        hashInt(hash, event.targetObjectId);
        hashUint64(hash, event.statusId);
        hashInt(hash, event.acceptedTick);
        hashInt(hash, event.sourceObjectId);
        hashText(hash, event.sourceOwner);
        hashText(hash, event.sourceTypeId);
        hashByte(hash, static_cast<std::uint8_t>(event.sourceKind));
        hashText(hash, event.targetOwner);
        hashText(hash, event.targetTypeId);
        hashByte(hash, static_cast<std::uint8_t>(event.targetKind));
        hashText(hash, event.warheadId);
    }
    return hash;
}

const RuntimePsychicStatusRecord*
RuntimePsychicStatusSystemState::findByTarget(int targetObjectId) const {
    const auto iterator = std::lower_bound(
        statuses_.begin(),
        statuses_.end(),
        targetObjectId,
        [](const RuntimePsychicStatusRecord& status, int id) {
            return status.targetObjectId < id;
        }
    );
    return iterator != statuses_.end()
        && iterator->targetObjectId == targetObjectId
        ? &*iterator
        : nullptr;
}

const RuntimePsychicStatusLimits&
RuntimePsychicStatusSystemState::limits() const {
    return limits_;
}

const std::vector<RuntimePsychicStatusRecord>&
RuntimePsychicStatusSystemState::statuses() const {
    return statuses_;
}

const std::vector<RuntimePsychicStatusHitEventRecord>&
RuntimePsychicStatusSystemState::hitEvents() const {
    return hitEvents_;
}

int RuntimePsychicStatusSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

std::uint64_t RuntimePsychicStatusSystemState::nextStatusId() const {
    return nextStatusId_;
}

void RuntimePsychicStatusSystemState::sortState() {
    std::sort(
        statuses_.begin(),
        statuses_.end(),
        [](const auto& left, const auto& right) {
            if (left.targetObjectId != right.targetObjectId) {
                return left.targetObjectId < right.targetObjectId;
            }
            return left.statusId < right.statusId;
        }
    );
    std::sort(
        hitEvents_.begin(),
        hitEvents_.end(),
        [](const auto& left, const auto& right) {
            if (left.projectileActorId != right.projectileActorId) {
                return left.projectileActorId < right.projectileActorId;
            }
            if (left.targetObjectId != right.targetObjectId) {
                return left.targetObjectId < right.targetObjectId;
            }
            return left.statusId < right.statusId;
        }
    );
}

const char* runtimePsychicStatusVetoName(RuntimePsychicStatusVeto veto) {
    switch (veto) {
    case RuntimePsychicStatusVeto::None: return "none";
    case RuntimePsychicStatusVeto::InvalidTick: return "invalid-tick";
    case RuntimePsychicStatusVeto::InvalidEventId: return "invalid-event-id";
    case RuntimePsychicStatusVeto::InvalidSource: return "invalid-source";
    case RuntimePsychicStatusVeto::InvalidTarget: return "invalid-target";
    case RuntimePsychicStatusVeto::SelfTarget: return "self-target";
    case RuntimePsychicStatusVeto::IllegalTargetKind:
        return "illegal-target-kind";
    case RuntimePsychicStatusVeto::TargetAirborne: return "target-airborne";
    case RuntimePsychicStatusVeto::TargetImmune: return "target-immune";
    case RuntimePsychicStatusVeto::InvalidWarhead: return "invalid-warhead";
    case RuntimePsychicStatusVeto::InvalidDuration:
        return "invalid-duration";
    case RuntimePsychicStatusVeto::RecursiveRequest:
        return "recursive-request";
    case RuntimePsychicStatusVeto::CapacityReached:
        return "capacity-reached";
    case RuntimePsychicStatusVeto::IdExhausted: return "id-exhausted";
    case RuntimePsychicStatusVeto::EventIdConflict:
        return "event-id-conflict";
    case RuntimePsychicStatusVeto::EventCapacityReached:
        return "event-capacity-reached";
    case RuntimePsychicStatusVeto::ActionLimitReached:
        return "action-limit-reached";
    }
    return "unknown";
}

const char* runtimePsychicStatusEndReasonName(
    RuntimePsychicStatusEndReason reason
) {
    switch (reason) {
    case RuntimePsychicStatusEndReason::None: return "none";
    case RuntimePsychicStatusEndReason::Expired: return "expired";
    case RuntimePsychicStatusEndReason::SourceMissing:
        return "source-missing";
    case RuntimePsychicStatusEndReason::SourceDead: return "source-dead";
    case RuntimePsychicStatusEndReason::SourceIdentityChanged:
        return "source-identity-changed";
    case RuntimePsychicStatusEndReason::TargetMissing:
        return "target-missing";
    case RuntimePsychicStatusEndReason::TargetDead: return "target-dead";
    case RuntimePsychicStatusEndReason::TargetIdentityChanged:
        return "target-identity-changed";
    case RuntimePsychicStatusEndReason::TargetBecameIneligible:
        return "target-became-ineligible";
    }
    return "unknown";
}

const char* runtimePsychicStatusActionName(
    RuntimePsychicStatusActionKind kind
) {
    switch (kind) {
    case RuntimePsychicStatusActionKind::Applied: return "applied";
    case RuntimePsychicStatusActionKind::Refreshed: return "refreshed";
    case RuntimePsychicStatusActionKind::Ended: return "ended";
    }
    return "unknown";
}
