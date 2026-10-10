#include "simulation/buildings/RuntimeGateSystem.h"

#include "core/io/IniFile.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr std::size_t maximumSavedGates = 4096;
constexpr int maximumTransitionTicks = 3600000;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](char character) {
            return lowerAsciiCharacter(character);
        }
    );
    return value;
}

bool equalsAsciiIgnoreCase(
    const std::string& left,
    const std::string& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAsciiCharacter(left[index])
            != lowerAsciiCharacter(right[index])) {
            return false;
        }
    }
    return true;
}

const WorldObjectInstance* findObject(
    const RuntimeGateContext& context,
    int objectId
) {
    if (context.objects == nullptr) {
        return nullptr;
    }
    for (const WorldObjectInstance& object : *context.objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

const BuildingType* liveGateRules(
    const RuntimeGateContext& context,
    const WorldObjectInstance* object
) {
    if (context.rules == nullptr
        || object == nullptr
        || object->instanceId < 0
        || object->kind != WorldObjectKind::Structure
        || !object->alive
        || object->health <= 0
        || object->rubble) {
        return nullptr;
    }
    const BuildingType* building =
        context.rules->findBuildingType(object->type);
    return building != nullptr && building->gate ? building : nullptr;
}

bool ownersAreAllied(
    const RuntimeGateContext& context,
    const std::string& left,
    const std::string& right
) {
    if (equalsAsciiIgnoreCase(left, right)) {
        return true;
    }
    return context.areAllied && context.areAllied(left, right);
}

bool hasLimboMission(const WorldObjectInstance& object) {
    const std::string mission = lowerAscii(object.mission);
    return mission == "insidetransport"
        || mission == "garrisoned"
        || mission == "limbo";
}

bool defaultGroundMover(
    const RuntimeGateContext& context,
    const WorldObjectInstance& object
) {
    if (context.rules == nullptr) {
        return false;
    }
    const char* kind = nullptr;
    if (object.kind == WorldObjectKind::Infantry) {
        kind = "Infantry";
    } else if (object.kind == WorldObjectKind::Unit) {
        kind = "Vehicle";
    } else {
        return false;
    }
    const UnitType* unit = context.rules->findUnitType(kind, object.type);
    if (unit == nullptr) {
        return false;
    }
    return !object.airborne
        && !object.authoredAircraft
        && !object.submerged
        && !unit->jumpJet
        && !unit->consideredAircraft
        && !unit->balloonHover
        && !unit->naval
        && !unit->waterBound;
}

bool isGroundMover(
    const RuntimeGateContext& context,
    const WorldObjectInstance* object
) {
    if (object == nullptr
        || object->instanceId < 0
        || !object->alive
        || object->health <= 0
        || (object->kind != WorldObjectKind::Unit
            && object->kind != WorldObjectKind::Infantry)
        || object->garrisonHostObjectId >= 0
        || hasLimboMission(*object)) {
        return false;
    }
    if (context.isGroundMover) {
        return context.isGroundMover(*object);
    }
    return defaultGroundMover(context, *object);
}

int saturatedAdd(int left, int right) {
    const long long sum = static_cast<long long>(left)
        + static_cast<long long>(right);
    return sum >= static_cast<long long>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(sum);
}

int gateCloseDelayTicks(
    int fixedTickHz,
    const BuildingType& building
) {
    if (!std::isfinite(building.gateCloseDelay)
        || building.gateCloseDelay <= 0.0) {
        return 0;
    }
    const long double ticks =
        static_cast<long double>(building.gateCloseDelay)
        * 60.0L
        * static_cast<long double>(std::max(1, fixedTickHz));
    if (ticks >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return std::max(0, static_cast<int>(std::ceil(ticks - 1.0e-12L)));
}

int authoredTransitionTicks(
    int fixedTickHz,
    double deployTime,
    int fallbackTicks
) {
    if (!std::isfinite(deployTime) || deployTime <= 0.0) {
        return std::clamp(fallbackTicks, 1, maximumTransitionTicks);
    }
    const long double ticks = static_cast<long double>(deployTime)
        * 60.0L
        * static_cast<long double>(std::max(1, fixedTickHz));
    if (ticks >= static_cast<long double>(maximumTransitionTicks)) {
        return maximumTransitionTicks;
    }
    // INI decimal values that mathematically land on an integer can arrive a
    // few ulps above it after double parsing.  Remove only that representation
    // noise before ceil; real fractional authored durations still round up.
    const long double tolerance = std::max(
        1.0e-12L,
        std::abs(ticks)
            * static_cast<long double>(std::numeric_limits<double>::epsilon())
            * 8.0L
    );
    return std::clamp(
        static_cast<int>(std::ceil(ticks - tolerance)),
        1,
        maximumTransitionTicks
    );
}

int gateTransitionTicks(
    const RuntimeGateConfig& config,
    const RuntimeGateContext& context,
    int gateId,
    int fallbackTicks
) {
    const BuildingType* building = liveGateRules(
        context,
        findObject(context, gateId)
    );
    return authoredTransitionTicks(
        config.fixedTickHz,
        building == nullptr ? 0.0 : building->deployTime,
        fallbackTicks
    );
}

RuntimeGateAction makeAction(
    const RuntimeGateContext& context,
    RuntimeGateActionKind kind,
    const RuntimeGateInstanceState& gate,
    RuntimeGatePhase fromPhase,
    RuntimeGatePhase toPhase,
    int eventTick,
    const std::string& reason
) {
    RuntimeGateAction action;
    action.kind = kind;
    action.gateId = gate.gateId;
    action.owner = gate.owner;
    action.type = gate.type;
    action.fromPhase = fromPhase;
    action.toPhase = toPhase;
    action.eventTick = eventTick;
    action.reason = reason;
    if (kind == RuntimeGateActionKind::BeginOpening) {
        action.soundKey = "GateUp";
    } else if (kind == RuntimeGateActionKind::BeginClosing) {
        action.soundKey = "GateDown";
    }
    if (!action.soundKey.empty() && context.rules != nullptr) {
        action.soundId = context.rules->getAudioVisualValue(action.soundKey);
    }
    return action;
}

void beginOpening(
    RuntimeGateInstanceState& gate,
    const RuntimeGateConfig& config,
    const RuntimeGateContext& context,
    int currentTick,
    const std::string& reason,
    std::vector<RuntimeGateAction>& actions
) {
    const RuntimeGatePhase from = gate.phase;
    int durationTicks = gateTransitionTicks(
        config,
        context,
        gate.gateId,
        config.openingTicks
    );
    int startedTick = currentTick;
    int completesTick = saturatedAdd(currentTick, durationTicks);
    if (from == RuntimeGatePhase::Closing) {
        // Preserve the exact old closing fraction.  Keeping the same full
        // transition span lets renderers use (tick-start)/(complete-start)
        // without an extra saved progress field, and means reopening consumes
        // only the number of ticks already spent closing.
        const long long oldDuration =
            static_cast<long long>(gate.phaseCompletesTick)
            - static_cast<long long>(gate.phaseStartedTick);
        durationTicks = static_cast<int>(std::clamp(
            oldDuration,
            1LL,
            static_cast<long long>(maximumTransitionTicks)
        ));
        const long long oldClosedTicks =
            static_cast<long long>(currentTick)
            - static_cast<long long>(gate.phaseStartedTick);
        const int closedTicks = static_cast<int>(std::clamp(
            oldClosedTicks,
            0LL,
            static_cast<long long>(durationTicks)
        ));
        completesTick = saturatedAdd(currentTick, closedTicks);
        startedTick = completesTick - durationTicks;
    }
    gate.phase = RuntimeGatePhase::Opening;
    gate.phaseStartedTick = startedTick;
    gate.phaseCompletesTick = completesTick;
    gate.lastActivityTick = std::max(gate.lastActivityTick, currentTick);
    actions.push_back(makeAction(
        context,
        RuntimeGateActionKind::BeginOpening,
        gate,
        from,
        RuntimeGatePhase::Opening,
        currentTick,
        reason
    ));
}

void beginClosing(
    RuntimeGateInstanceState& gate,
    const RuntimeGateConfig& config,
    const RuntimeGateContext& context,
    int currentTick,
    const std::string& reason,
    std::vector<RuntimeGateAction>& actions
) {
    const RuntimeGatePhase from = gate.phase;
    const int durationTicks = gateTransitionTicks(
        config,
        context,
        gate.gateId,
        config.closingTicks
    );
    gate.phase = RuntimeGatePhase::Closing;
    gate.phaseStartedTick = currentTick;
    gate.phaseCompletesTick = saturatedAdd(
        currentTick,
        durationTicks
    );
    actions.push_back(makeAction(
        context,
        RuntimeGateActionKind::BeginClosing,
        gate,
        from,
        RuntimeGatePhase::Closing,
        currentTick,
        reason
    ));
}

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char character : value) {
        encoded.push_back(digits[character >> 4]);
        encoded.push_back(digits[character & 0x0f]);
    }
    return encoded;
}

int hexNibble(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool hexDecode(const std::string& value, std::string& output) {
    if ((value.size() % 2U) != 0U || value.size() > 8192U) {
        return false;
    }
    output.clear();
    output.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = hexNibble(value[index]);
        const int low = hexNibble(value[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        output.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

bool parseIntStrict(const std::string& text, int& output) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    int parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    output = parsed;
    return true;
}

bool phaseFromName(const std::string& name, RuntimeGatePhase& phase) {
    const std::string lowered = lowerAscii(name);
    if (lowered == "closed") {
        phase = RuntimeGatePhase::Closed;
    } else if (lowered == "opening") {
        phase = RuntimeGatePhase::Opening;
    } else if (lowered == "open") {
        phase = RuntimeGatePhase::Open;
    } else if (lowered == "closing") {
        phase = RuntimeGatePhase::Closing;
    } else {
        return false;
    }
    return true;
}

std::uint64_t hashText(std::string_view text) {
    std::uint64_t hash = fnvOffset;
    for (const unsigned char character : text) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= fnvPrime;
    }
    return hash;
}

} // namespace

const char* runtimeGatePhaseName(RuntimeGatePhase phase) {
    switch (phase) {
    case RuntimeGatePhase::Closed: return "Closed";
    case RuntimeGatePhase::Opening: return "Opening";
    case RuntimeGatePhase::Open: return "Open";
    case RuntimeGatePhase::Closing: return "Closing";
    }
    return "Closed";
}

const char* runtimeGateActionName(RuntimeGateActionKind kind) {
    switch (kind) {
    case RuntimeGateActionKind::BeginOpening: return "begin-opening";
    case RuntimeGateActionKind::FullyOpen: return "fully-open";
    case RuntimeGateActionKind::BeginClosing: return "begin-closing";
    case RuntimeGateActionKind::FullyClosed: return "fully-closed";
    }
    return "unknown-gate-action";
}

RuntimeGateSystemState::RuntimeGateSystemState() {
    reset();
}

RuntimeGateSystemState::RuntimeGateSystemState(RuntimeGateConfig config) {
    reset(config);
}

void RuntimeGateSystemState::reset(RuntimeGateConfig config) {
    config.fixedTickHz = std::clamp(config.fixedTickHz, 1, 1000);
    config.openingTicks = std::clamp(
        config.openingTicks,
        1,
        maximumTransitionTicks
    );
    config.closingTicks = std::clamp(
        config.closingTicks,
        1,
        maximumTransitionTicks
    );
    config_ = config;
    lastAdvancedTick_ = -1;
    gates_.clear();
}

const RuntimeGateConfig& RuntimeGateSystemState::config() const {
    return config_;
}

RuntimeGateReconcileReport RuntimeGateSystemState::reconcile(
    const RuntimeGateContext& context
) {
    RuntimeGateReconcileReport report;
    if (context.rules == nullptr || context.objects == nullptr) {
        return report;
    }
    report.contextValid = true;

    struct LiveGate {
        const WorldObjectInstance* object = nullptr;
    };
    std::map<int, LiveGate> live;
    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = liveGateRules(context, &object);
        if (building == nullptr) {
            continue;
        }
        // Instance ids are authoritative and expected to be unique.  Keeping
        // the first duplicate mirrors findObject() and gives dirty worlds a
        // deterministic recovery path without mixing two host records.
        live.emplace(object.instanceId, LiveGate{&object});
    }

    sortState();
    std::vector<RuntimeGateInstanceState> reconciled;
    reconciled.reserve(std::min(maximumSavedGates, live.size()));
    std::set<int> retainedIds;
    for (RuntimeGateInstanceState gate : gates_) {
        if (!retainedIds.insert(gate.gateId).second) {
            ++report.removedGates;
            report.removedGateIds.push_back(gate.gateId);
            continue;
        }
        const auto found = live.find(gate.gateId);
        if (found == live.end()) {
            ++report.removedGates;
            report.removedGateIds.push_back(gate.gateId);
            continue;
        }
        const WorldObjectInstance& object = *found->second.object;
        if (!equalsAsciiIgnoreCase(gate.type, object.type)) {
            gate = {};
            gate.gateId = object.instanceId;
            gate.owner = object.owner;
            gate.type = object.type;
            ++report.resetGates;
            report.resetGateIds.push_back(gate.gateId);
        } else {
            if (gate.owner != object.owner) {
                gate.owner = object.owner;
                ++report.refreshedOwners;
            }
            gate.type = object.type;
            ++report.retainedGates;
        }
        reconciled.push_back(std::move(gate));
        live.erase(found);
    }

    for (const auto& [gateId, liveGate] : live) {
        if (reconciled.size() >= maximumSavedGates) {
            ++report.skippedByLimit;
            continue;
        }
        RuntimeGateInstanceState gate;
        gate.gateId = gateId;
        gate.owner = liveGate.object->owner;
        gate.type = liveGate.object->type;
        reconciled.push_back(std::move(gate));
        ++report.addedGates;
        report.addedGateIds.push_back(gateId);
    }
    gates_ = std::move(reconciled);
    sortState();
    std::sort(report.addedGateIds.begin(), report.addedGateIds.end());
    std::sort(report.removedGateIds.begin(), report.removedGateIds.end());
    std::sort(report.resetGateIds.begin(), report.resetGateIds.end());
    return report;
}

std::vector<RuntimeGateAction> RuntimeGateSystemState::tick(
    const RuntimeGateContext& context,
    int currentTick
) {
    const RuntimeGateReconcileReport report = reconcile(context);
    std::vector<RuntimeGateAction> actions;
    if (!report.contextValid
        || currentTick < 0
        || currentTick <= lastAdvancedTick_) {
        return actions;
    }

    struct RequestSummary {
        bool anyGroundRequest = false;
        bool alliedGroundRequest = false;
    };
    std::map<int, RequestSummary> requestsByGate;
    if (context.passageRequests != nullptr) {
        std::vector<std::pair<int, int>> requests;
        requests.reserve(context.passageRequests->size());
        for (const RuntimeGatePassageRequest& request
             : *context.passageRequests) {
            if (request.gateId >= 0 && request.moverId >= 0) {
                requests.emplace_back(request.gateId, request.moverId);
            }
        }
        std::sort(requests.begin(), requests.end());
        requests.erase(
            std::unique(requests.begin(), requests.end()),
            requests.end()
        );
        for (const auto& [gateId, moverId] : requests) {
            const RuntimeGateInstanceState* gate = findGate(gateId);
            const WorldObjectInstance* host = findObject(context, gateId);
            const WorldObjectInstance* mover = findObject(context, moverId);
            if (gate == nullptr
                || liveGateRules(context, host) == nullptr
                || !isGroundMover(context, mover)) {
                continue;
            }
            RequestSummary& summary = requestsByGate[gateId];
            summary.anyGroundRequest = true;
            summary.alliedGroundRequest =
                summary.alliedGroundRequest
                || ownersAreAllied(context, host->owner, mover->owner);
        }
    }

    lastAdvancedTick_ = currentTick;
    for (RuntimeGateInstanceState& gate : gates_) {
        const RequestSummary request = requestsByGate[gate.gateId];
        const bool occupied = context.doorwayOccupied
            && context.doorwayOccupied(gate.gateId);

        if (gate.phase == RuntimeGatePhase::Closed) {
            if (request.alliedGroundRequest) {
                beginOpening(
                    gate,
                    config_,
                    context,
                    currentTick,
                    "allied ground mover requested passage",
                    actions
                );
            }
            continue;
        }

        if (gate.phase == RuntimeGatePhase::Closing) {
            if (occupied || request.alliedGroundRequest) {
                beginOpening(
                    gate,
                    config_,
                    context,
                    currentTick,
                    occupied
                        ? "gate doorway became occupied"
                        : "allied ground mover requested passage",
                    actions
                );
                continue;
            }
            if (currentTick >= gate.phaseCompletesTick) {
                const RuntimeGatePhase from = gate.phase;
                const int completedTick = gate.phaseCompletesTick;
                gate.phase = RuntimeGatePhase::Closed;
                gate.phaseStartedTick = completedTick;
                gate.phaseCompletesTick = -1;
                actions.push_back(makeAction(
                    context,
                    RuntimeGateActionKind::FullyClosed,
                    gate,
                    from,
                    RuntimeGatePhase::Closed,
                    completedTick,
                    "gate close transition completed"
                ));
            }
            continue;
        }

        if (gate.phase == RuntimeGatePhase::Opening) {
            if (occupied || request.anyGroundRequest) {
                gate.lastActivityTick = std::max(
                    gate.lastActivityTick,
                    currentTick
                );
            }
            if (currentTick < gate.phaseCompletesTick) {
                continue;
            }
            const RuntimeGatePhase from = gate.phase;
            const int completedTick = gate.phaseCompletesTick;
            gate.phase = RuntimeGatePhase::Open;
            gate.phaseStartedTick = completedTick;
            gate.phaseCompletesTick = -1;
            gate.lastActivityTick = std::max(
                gate.lastActivityTick,
                completedTick
            );
            actions.push_back(makeAction(
                context,
                RuntimeGateActionKind::FullyOpen,
                gate,
                from,
                RuntimeGatePhase::Open,
                completedTick,
                "gate open transition completed"
            ));
        }

        if (gate.phase != RuntimeGatePhase::Open) {
            continue;
        }
        if (occupied || request.anyGroundRequest) {
            gate.lastActivityTick = std::max(
                gate.lastActivityTick,
                currentTick
            );
            continue;
        }

        const int delayTicks = closeDelayTicks(context, gate.gateId);
        const long long idleTicks = static_cast<long long>(currentTick)
            - static_cast<long long>(gate.lastActivityTick);
        if (gate.lastActivityTick >= 0
            && idleTicks >= static_cast<long long>(delayTicks)) {
            beginClosing(
                gate,
                config_,
                context,
                currentTick,
                "gate close delay elapsed",
                actions
            );
        }
    }
    return actions;
}

bool RuntimeGateSystemState::allowsPassage(
    const RuntimeGateContext& context,
    int gateId,
    int moverId
) const {
    const RuntimeGateInstanceState* gate = findGate(gateId);
    return gate != nullptr
        && gate->phase == RuntimeGatePhase::Open
        && liveGateRules(context, findObject(context, gateId)) != nullptr
        && isGroundMover(context, findObject(context, moverId));
}

RuntimeGatePhase RuntimeGateSystemState::phaseOf(int gateId) const {
    const RuntimeGateInstanceState* gate = findGate(gateId);
    return gate == nullptr ? RuntimeGatePhase::Closed : gate->phase;
}

const RuntimeGateInstanceState* RuntimeGateSystemState::findGate(
    int gateId
) const {
    const auto found = std::lower_bound(
        gates_.begin(),
        gates_.end(),
        gateId,
        [](const RuntimeGateInstanceState& gate, int id) {
            return gate.gateId < id;
        }
    );
    return found != gates_.end() && found->gateId == gateId
        ? &*found
        : nullptr;
}

const std::vector<RuntimeGateInstanceState>&
RuntimeGateSystemState::gates() const {
    return gates_;
}

int RuntimeGateSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

int RuntimeGateSystemState::closeDelayTicks(
    const RuntimeGateContext& context,
    int gateId
) const {
    const WorldObjectInstance* gate = findObject(context, gateId);
    const BuildingType* building = liveGateRules(context, gate);
    return building == nullptr
        ? 0
        : gateCloseDelayTicks(config_.fixedTickHz, *building);
}

void RuntimeGateSystemState::save(std::ostream& output) const {
    std::vector<RuntimeGateInstanceState> gates = gates_;
    std::sort(
        gates.begin(),
        gates.end(),
        [](const RuntimeGateInstanceState& left,
           const RuntimeGateInstanceState& right) {
            return left.gateId < right.gateId;
        }
    );

    output << "[GateRuntime]\n";
    output << "Version=1\n";
    output << "FixedTickHz=" << config_.fixedTickHz << "\n";
    output << "OpeningTicks=" << config_.openingTicks << "\n";
    output << "ClosingTicks=" << config_.closingTicks << "\n";
    output << "LastAdvancedTick=" << lastAdvancedTick_ << "\n";
    output << "GateCount=" << gates.size() << "\n\n";
    for (std::size_t index = 0; index < gates.size(); ++index) {
        const RuntimeGateInstanceState& gate = gates[index];
        output << "[GateRuntime.Gate." << index << "]\n";
        output << "GateId=" << gate.gateId << "\n";
        output << "OwnerHex=" << hexEncode(gate.owner) << "\n";
        output << "TypeHex=" << hexEncode(gate.type) << "\n";
        output << "Phase=" << runtimeGatePhaseName(gate.phase) << "\n";
        output << "PhaseStartedTick=" << gate.phaseStartedTick << "\n";
        output << "PhaseCompletesTick=" << gate.phaseCompletesTick << "\n";
        output << "LastActivityTick=" << gate.lastActivityTick << "\n\n";
    }
}

bool RuntimeGateSystemState::load(
    const IniFile& ini,
    const RuntimeGateContext& context,
    std::string* errorMessage,
    RuntimeGateReconcileReport* reconcileReport
) {
    if (ini.getSection("GateRuntime") == nullptr) {
        gates_.clear();
        lastAdvancedTick_ = -1;
        RuntimeGateReconcileReport report = reconcile(context);
        if (reconcileReport != nullptr) {
            *reconcileReport = std::move(report);
        }
        return true;
    }
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    const auto value = [&ini](
        const std::string& section,
        const std::string& key,
        const std::string& fallback = std::string()
    ) {
        return ini.getValue(section, key, fallback);
    };

    int version = 0;
    int fixedTickHz = 0;
    int openingTicks = 0;
    int closingTicks = 0;
    int lastAdvancedTick = -1;
    int gateCount = 0;
    if (!parseIntStrict(value("GateRuntime", "Version"), version)
        || version != 1
        || !parseIntStrict(
            value("GateRuntime", "FixedTickHz"),
            fixedTickHz
        )
        || fixedTickHz < 1
        || fixedTickHz > 1000
        || !parseIntStrict(
            value("GateRuntime", "OpeningTicks"),
            openingTicks
        )
        || openingTicks < 1
        || openingTicks > maximumTransitionTicks
        || !parseIntStrict(
            value("GateRuntime", "ClosingTicks"),
            closingTicks
        )
        || closingTicks < 1
        || closingTicks > maximumTransitionTicks
        || !parseIntStrict(
            value("GateRuntime", "LastAdvancedTick"),
            lastAdvancedTick
        )
        || lastAdvancedTick < -1
        || !parseIntStrict(value("GateRuntime", "GateCount"), gateCount)
        || gateCount < 0
        || static_cast<std::size_t>(gateCount) > maximumSavedGates) {
        return fail("gate runtime save has invalid header values");
    }

    RuntimeGateSystemState loaded({
        fixedTickHz,
        openingTicks,
        closingTicks
    });
    loaded.lastAdvancedTick_ = lastAdvancedTick;
    std::set<int> gateIds;
    for (int index = 0; index < gateCount; ++index) {
        const std::string section = "GateRuntime.Gate."
            + std::to_string(index);
        RuntimeGateInstanceState gate;
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "GateId"), gate.gateId)
            || gate.gateId < 0
            || !hexDecode(value(section, "OwnerHex"), gate.owner)
            || !hexDecode(value(section, "TypeHex"), gate.type)
            || gate.type.empty()
            || !phaseFromName(value(section, "Phase"), gate.phase)
            || !parseIntStrict(
                value(section, "PhaseStartedTick"),
                gate.phaseStartedTick
            )
            || !parseIntStrict(
                value(section, "PhaseCompletesTick"),
                gate.phaseCompletesTick
            )
            || !parseIntStrict(
                value(section, "LastActivityTick"),
                gate.lastActivityTick
            )
            || gate.phaseStartedTick < -maximumTransitionTicks
            || gate.phaseCompletesTick < -1
            || gate.lastActivityTick < -1
            || (gate.phaseStartedTick > lastAdvancedTick
                && lastAdvancedTick >= 0)
            || gate.lastActivityTick > lastAdvancedTick
            || !gateIds.insert(gate.gateId).second) {
            return fail("gate runtime save has invalid gate ["
                + section + "]");
        }

        const bool transition = gate.phase == RuntimeGatePhase::Opening
            || gate.phase == RuntimeGatePhase::Closing;
        if (transition) {
            const long long transitionTicks =
                static_cast<long long>(gate.phaseCompletesTick)
                - static_cast<long long>(gate.phaseStartedTick);
            bool durationMatchesLiveRules = true;
            const WorldObjectInstance* liveHost = findObject(context, gate.gateId);
            const BuildingType* liveBuilding = liveGateRules(context, liveHost);
            if (liveBuilding != nullptr
                && equalsAsciiIgnoreCase(liveHost->type, gate.type)) {
                const int expected = authoredTransitionTicks(
                    fixedTickHz,
                    liveBuilding->deployTime,
                    gate.phase == RuntimeGatePhase::Opening
                        ? openingTicks
                        : closingTicks
                );
                durationMatchesLiveRules = transitionTicks == expected;
                if (gate.phase == RuntimeGatePhase::Opening
                    && (!std::isfinite(liveBuilding->deployTime)
                        || liveBuilding->deployTime <= 0.0)) {
                    // A reversed fallback transition preserves the closing
                    // span so its progress is exact even when the two legacy
                    // fallback values differ.
                    durationMatchesLiveRules = durationMatchesLiveRules
                        || transitionTicks == closingTicks;
                }
            }
            if ((gate.phase == RuntimeGatePhase::Closing
                    && gate.phaseStartedTick < 0)
                || gate.phaseCompletesTick <= gate.phaseStartedTick
                || transitionTicks < 1
                || transitionTicks > maximumTransitionTicks
                || !durationMatchesLiveRules
                || gate.phaseCompletesTick <= lastAdvancedTick
                || gate.lastActivityTick < 0
                || (gate.phase == RuntimeGatePhase::Opening
                    && gate.lastActivityTick < gate.phaseStartedTick)) {
                return fail("gate runtime transition has invalid ticks ["
                    + section + "]");
            }
        } else if (gate.phaseCompletesTick != -1
            || gate.phaseStartedTick < -1) {
            return fail("stable gate phase has a completion tick ["
                + section + "]");
        }
        if (gate.phase == RuntimeGatePhase::Open
            && (gate.phaseStartedTick < 0
                || gate.lastActivityTick < gate.phaseStartedTick)) {
            return fail("open gate has invalid activity ticks ["
                + section + "]");
        }
        if (lastAdvancedTick < 0
            && (gate.phase != RuntimeGatePhase::Closed
                || gate.phaseStartedTick != -1
                || gate.phaseCompletesTick != -1
                || gate.lastActivityTick != -1)) {
            return fail("unadvanced gate runtime contains active state ["
                + section + "]");
        }
        loaded.gates_.push_back(std::move(gate));
    }

    loaded.sortState();
    RuntimeGateReconcileReport report = loaded.reconcile(context);
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = std::move(report);
    }
    return true;
}

std::uint64_t RuntimeGateSystemState::stateHash() const {
    std::ostringstream saved;
    save(saved);
    return hashText(saved.str());
}

void RuntimeGateSystemState::sortState() {
    std::sort(
        gates_.begin(),
        gates_.end(),
        [](const RuntimeGateInstanceState& left,
           const RuntimeGateInstanceState& right) {
            return left.gateId < right.gateId;
        }
    );
}
