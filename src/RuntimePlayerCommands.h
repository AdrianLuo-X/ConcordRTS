#pragma once

#include "RuntimeMultiplayerGameplayCommand.h"
#include "RuntimePlayerWaypoints.h"
#include <algorithm>
#include <array>
#include <deque>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Retail keyboardmd.ini uses virtual-key bits 0..7 followed by Shift/Ctrl/Alt.
inline constexpr int runtimePlayerShiftKey = 0x100;
inline constexpr int runtimePlayerControlKey = 0x200;
inline constexpr int runtimePlayerAltKey = 0x400;
inline int runtimePlayerKeyCode(int key, bool shift, bool control, bool alt) {
    return (key & 0xff) | (shift ? runtimePlayerShiftKey : 0)
        | (control ? runtimePlayerControlKey : 0) | (alt ? runtimePlayerAltKey : 0);
}
inline bool runtimePlayerValidKeyCode(int key) {
    return (key & 0xff) != 0 && (key & ~0x7ff) == 0;
}
inline std::vector<std::pair<std::string, int>> runtimePlayerCommandBindings() {
    std::vector<std::pair<std::string, int>> result{
        {"TypeSelect", 'T'}, {"CombatantSelect", 'P'}, {"VeterancyNav", 'Y'},
        {"HealthNav", 'U'}, {"PreviousObject", 'M'}, {"NextObject", 'N'},
        {"PlanningMode", 'Z'}, {"Follow", 'F'},
        {"CenterOnRadarEvent", 32}, {"CenterBase", 'H'},
        {"StructureTab", 'Q'}, {"DefenseTab", 'W'}, {"InfantryTab", 'E'}, {"UnitTab", 'R'},
        {"ToggleRepair", 'K'}, {"ToggleSell", 'L'}, {"ShowCoordinates", 'O'},
        {"Diplomacy", 9}, {"ToggleAlliance", 'A'}, {"PlaceBeacon", 'B'},
        {"DeleteBeacon", 46}, {"AllToCheer", 'C'}, {"ChatToAll", 13}, {"ChatToAllies", 36},
        {"SetPrimaryFactory", 'U' | runtimePlayerControlKey}, {"SetRallyPoint", 'J'}
    };
    for (int number = 1; number <= 10; ++number) {
        const std::string suffix = "_" + std::to_string(number);
        const int key = number == 10 ? '0' : '0' + number;
        result.emplace_back("TeamSelect" + suffix, key);
        result.emplace_back("TeamAddSelect" + suffix, key | runtimePlayerShiftKey);
        result.emplace_back("TeamCreate" + suffix, key | runtimePlayerControlKey);
        result.emplace_back("TeamCenter" + suffix, key | runtimePlayerAltKey);
    }
    for (int i = 1; i <= 4; ++i) {
        result.emplace_back("View" + std::to_string(i), 111 + i);
        result.emplace_back("SetView" + std::to_string(i), (111 + i) | runtimePlayerControlKey);
    }
    for (int i = 1; i <= 8; ++i)
        result.emplace_back("Taunt_" + std::to_string(i), 115 + i);
    return result;
}

struct RuntimePlayerSelectionCandidate {
    int objectId = 0;
    std::string type;
    int veterancy = 0;
    bool combatant = false;
    bool onScreen = false;
    int health = 1;
    int maximumHealth = 1;
};
enum class RuntimePlayerQuickSelection { Type, Combatant, Veterancy, Health };
struct RuntimePlayerLocalCommands {
    std::array<std::vector<int>, 10> teams;
    std::array<std::optional<RuntimeMultiplayerGameplayCell>, 4> views;
    bool planning = false;
    std::uint64_t selectedWaypoint = 0;
    bool followTargeting = false;
    bool rallyTargeting = false;
    bool cameraFollow = false;
    bool showCoordinates = false;
    int lastSelectionKey = 0;
    std::uint64_t lastSelectionAtMs = 0;
    int veterancyRank = -1;
    int healthRank = -1;

    static int healthBand(const RuntimePlayerSelectionCandidate& candidate) {
        const auto health = static_cast<std::int64_t>(candidate.health) * 3;
        const auto maximum = std::max(1, candidate.maximumHealth);
        return health > 2LL * maximum ? 0 : health > maximum ? 1 : 2;
    }

    template<class Eligible>
    void teamCreate(int index, const std::vector<int>& selected, Eligible eligible) {
        if (index < 0 || index >= 10) return;
        std::set<int> valid;
        for (int id : selected) if (eligible(id)) valid.insert(id);
        teams[static_cast<std::size_t>(index)] = {valid.begin(), valid.end()};
    }
    template<class Eligible>
    std::vector<int> teamSelect(int index, const std::vector<int>& selected,
        bool additive, Eligible eligible) {
        if (index < 0 || index >= 10) return selected;
        auto& team = teams[static_cast<std::size_t>(index)];
        std::erase_if(team, [&](int id) { return !eligible(id); });
        std::set<int> valid(team.begin(), team.end());
        if (additive) for (int id : selected) if (eligible(id)) valid.insert(id);
        return {valid.begin(), valid.end()};
    }
    std::vector<int> quickSelect(RuntimePlayerQuickSelection mode,
        const std::vector<int>& selected,
        const std::vector<RuntimePlayerSelectionCandidate>& candidates,
        bool wholeMap, bool additive, bool expandPreviousRank = false) {
        std::set<std::string> types;
        for (const auto& candidate : candidates) {
            if (std::find(selected.begin(), selected.end(), candidate.objectId) != selected.end())
                types.insert(candidate.type);
        }
        const bool ranked = mode == RuntimePlayerQuickSelection::Veterancy
            || mode == RuntimePlayerQuickSelection::Health;
        int& rank = mode == RuntimePlayerQuickSelection::Health ? healthRank : veterancyRank;
        const auto candidateRank = [&](const auto& c) {
            return mode == RuntimePlayerQuickSelection::Health ? healthBand(c) : c.veterancy;
        };
        if (ranked && !(expandPreviousRank && rank >= 0)) {
            // Cycle only through ranks that actually have eligible units.
            for (int attempt = 0; attempt < 3; ++attempt) {
                rank = (rank + 1) % 3;
                if (std::any_of(candidates.begin(), candidates.end(), [&](const auto& c) {
                    return candidateRank(c) == rank
                        && (wholeMap || c.onScreen);
                })) break;
            }
        }
        std::set<int> output;
        if (additive) output.insert(selected.begin(), selected.end());
        for (const auto& candidate : candidates) {
            if (!wholeMap && !candidate.onScreen) continue;
            const bool matches = mode == RuntimePlayerQuickSelection::Type
                ? types.contains(candidate.type)
                : mode == RuntimePlayerQuickSelection::Combatant ? candidate.combatant
                : candidateRank(candidate) == rank;
            if (matches) output.insert(candidate.objectId);
        }
        return {output.begin(), output.end()};
    }
};

struct RuntimePlayerQueuedOrders {
    std::string owner;
    std::deque<RuntimeMultiplayerGameplayTargetOrderCommand> orders;
    // The automatic rally order retains its position across a factory resume prepend.
    int factoryRallyOrderIndex = -1;
};
struct RuntimePlayerFollowOrder {
    std::string owner;
    int targetId = 0;
    RuntimeMultiplayerGameplayCell lastTargetCell{-1, -1};
};
struct RuntimePlayerFactoryRally {
    std::string owner;
    RuntimeMultiplayerGameplayCell cell;
};

struct RuntimePlayerTravelOrder {
    std::string owner;
    RuntimeMultiplayerGameplayCell destination;
    RuntimeMultiplayerGameplayTargetOrderKind kind = RuntimeMultiplayerGameplayTargetOrderKind::AttackMove;
    bool guarding = false;
    int nextMoveAttemptTick = 0;
};

inline RuntimeMultiplayerGameplayTargetOrderKind runtimePlayerModifiedTargetOrder(
    bool control, bool shift, bool alt, bool objectTarget
) {
    using Kind = RuntimeMultiplayerGameplayTargetOrderKind;
    if (alt) return control ? (objectTarget ? Kind::Follow : Kind::GuardDestination) : Kind::Move;
    if (control && shift) return Kind::AttackMove;
    if (control) return objectTarget ? Kind::Attack : Kind::ForceFireCell;
    return Kind::Primary;
}

// Authoritative state only. UI teams, selection and camera never enter this
// representation. std::map gives identical save/hash/tick iteration order.
struct RuntimePlayerCommandState {
    static constexpr std::size_t maximumOrdersPerObject = 64;
    std::map<int, RuntimePlayerQueuedOrders> queues;
    std::map<int, RuntimePlayerFollowOrder> following;
    std::map<int, RuntimePlayerTravelOrder> travelling;
    RuntimePlayerWaypointState waypoints;
    std::map<std::pair<std::string, std::string>, int> primaryFactories;
    std::map<int, RuntimePlayerFactoryRally> rallyPoints;
    // Directed alliance offers; an explicit false also breaks a starting team.
    std::map<std::pair<std::string, std::string>, bool> alliances;

    friend void swap(RuntimePlayerCommandState& left, RuntimePlayerCommandState& right) noexcept {
        left.queues.swap(right.queues);
        left.following.swap(right.following);
        left.travelling.swap(right.travelling);
        std::swap(left.waypoints, right.waypoints);
        left.primaryFactories.swap(right.primaryFactories);
        left.rallyPoints.swap(right.rallyPoints);
        left.alliances.swap(right.alliances);
    }

    void cancel(const std::vector<std::int32_t>& objectIds) {
        waypoints.cancel(objectIds);
        for (int id : objectIds) { queues.erase(id); following.erase(id); travelling.erase(id); }
    }
    bool append(const std::string& owner,
        const RuntimeMultiplayerGameplayTargetOrderCommand& command) {
        // Check the entire selection before mutating; partial acceptance would
        // give an unintuitive route and complicate authoritative acknowledgments.
        for (int id : command.objectIds) {
            const auto found = queues.find(id);
            if (found != queues.end() && found->second.owner == owner
                && found->second.orders.size() >= maximumOrdersPerObject) return false;
        }
        for (int id : command.objectIds) {
            auto& queue = queues[id];
            if (queue.owner != owner || queue.orders.empty()) {
                queue.orders.clear();
                queue.factoryRallyOrderIndex = -1;
            }
            queue.owner = owner;
            auto single = command;
            single.objectIds = {id};
            single.append = false;
            queue.orders.push_back(std::move(single));
        }
        return true;
    }

    std::string serialize() const {
        std::ostringstream out;
        out << 5 << ' ' << queues.size();
        for (const auto& [id, queue] : queues) {
            out << ' ' << id << ' ' << std::quoted(queue.owner) << ' ' << queue.orders.size()
                << ' ' << queue.factoryRallyOrderIndex;
            for (const auto& order : queue.orders) {
                std::vector<std::uint8_t> bytes;
                std::string error;
                RuntimeMultiplayerGameplayCommand command; command.body = order;
                if (!encodeRuntimeMultiplayerGameplayCommand(command, bytes, error)) return {};
                out << ' ';
                constexpr char hex[] = "0123456789abcdef";
                for (auto byte : bytes) out << hex[byte >> 4] << hex[byte & 15];
            }
        }
        out << ' ' << following.size();
        for (const auto& [id, follow] : following)
            out << ' ' << id << ' ' << std::quoted(follow.owner) << ' ' << follow.targetId
                << ' ' << follow.lastTargetCell.x << ' ' << follow.lastTargetCell.y;
        out << ' ' << primaryFactories.size();
        for (const auto& [key, id] : primaryFactories)
            out << ' ' << std::quoted(key.first) << ' ' << std::quoted(key.second) << ' ' << id;
        out << ' ' << rallyPoints.size();
        for (const auto& [id, rally] : rallyPoints)
            out << ' ' << id << ' ' << std::quoted(rally.owner) << ' ' << rally.cell.x << ' ' << rally.cell.y;
        out << ' ' << alliances.size();
        for (const auto& [owners, allied] : alliances)
            out << ' ' << std::quoted(owners.first) << ' ' << std::quoted(owners.second) << ' ' << allied;
        out << ' ' << travelling.size();
        for (const auto& [id, travel] : travelling)
            out << ' ' << id << ' ' << std::quoted(travel.owner)
                << ' ' << travel.destination.x << ' ' << travel.destination.y
                << ' ' << static_cast<int>(travel.kind) << ' ' << travel.guarding
                << ' ' << travel.nextMoveAttemptTick;
        out << ' ' << std::quoted(waypoints.serialize());
        return out.str();
    }
    std::uint64_t stateHash() const {
        std::uint64_t hash = 14695981039346656037ULL;
        for (unsigned char byte : serialize()) { hash ^= byte; hash *= 1099511628211ULL; }
        return hash;
    }
    bool restore(const std::string& encoded, std::string& error) {
        if (encoded.empty()) { *this = {}; error.clear(); return true; }
        if (encoded.size() > 32U * 1024U * 1024U) { error = "player command save is too large"; return false; }
        std::istringstream in(encoded);
        RuntimePlayerCommandState candidate;
        int schema = 0;
        std::size_t count = 0;
        const auto readCount = [&]() { return static_cast<bool>(in >> count) && count <= 65536U; };
        const auto validOwner = [](const std::string& owner) { return !owner.empty() && owner.size() <= 256U; };
        const auto validCell = [](const RuntimeMultiplayerGameplayCell& cell) {
            return cell.x >= 0 && cell.x <= runtimeMultiplayerGameplayMaximumCellCoordinate
                && cell.y >= 0 && cell.y <= runtimeMultiplayerGameplayMaximumCellCoordinate;
        };
        const auto reject = [&]() { error = "invalid player command save"; return false; };
        if (!(in >> schema) || schema < 1 || schema > 5 || !readCount()) return reject();
        const std::size_t queuesCount = count;
        for (std::size_t i = 0; i < queuesCount; ++i) {
            int id = 0; RuntimePlayerQueuedOrders queue; std::size_t length = 0;
            if (!(in >> id >> std::quoted(queue.owner) >> length) || id <= 0
                || !validOwner(queue.owner) || length == 0 || length > maximumOrdersPerObject) return reject();
            if (schema >= 5
                && (!(in >> queue.factoryRallyOrderIndex) || queue.factoryRallyOrderIndex < -1
                    || queue.factoryRallyOrderIndex >= static_cast<int>(length))) return reject();
            for (std::size_t j = 0; j < length; ++j) {
                std::string hex; if (!(in >> hex) || hex.size() % 2 != 0
                    || hex.size() > 2 * runtimeMultiplayerGameplayMaximumPayloadBytes) return reject();
                std::vector<std::uint8_t> bytes;
                for (std::size_t k = 0; k < hex.size(); k += 2) {
                    const auto nibble = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        return -1;
                    };
                    const int high = nibble(hex[k]), low = nibble(hex[k + 1]);
                    if (high < 0 || low < 0) return reject();
                    bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
                }
                RuntimeMultiplayerGameplayCommand command; std::string decodeError;
                // v15-v18 saved target orders retain the same wire fields.
                // Upgrade only the save payload; network decoding stays strict.
                if (bytes.size() >= 8 && bytes[4] == 0 && bytes[5] >= 15 && bytes[5] <= 18) {
                    bytes[4] = static_cast<std::uint8_t>(runtimeMultiplayerGameplayCommandVersion >> 8);
                    bytes[5] = static_cast<std::uint8_t>(runtimeMultiplayerGameplayCommandVersion & 255);
                }
                if (!decodeRuntimeMultiplayerGameplayCommand(bytes, command, decodeError)) return reject();
                const auto* order = std::get_if<RuntimeMultiplayerGameplayTargetOrderCommand>(&command.body);
                if (!order || order->objectIds != std::vector<std::int32_t>{id} || order->append) return reject();
                queue.orders.push_back(*order);
            }
            if (queue.factoryRallyOrderIndex >= 0) {
                const auto& rally = queue.orders[static_cast<std::size_t>(queue.factoryRallyOrderIndex)];
                if (rally.order != RuntimeMultiplayerGameplayTargetOrderKind::Move
                    || rally.objectIds != std::vector<std::int32_t>{id}
                    || !std::holds_alternative<RuntimeMultiplayerGameplayCellTarget>(rally.target))
                    return reject();
            }
            if (!candidate.queues.emplace(id, std::move(queue)).second) return reject();
        }
        if (!readCount()) return reject();
        for (std::size_t i = 0; i < count; ++i) {
            int id = 0; RuntimePlayerFollowOrder follow;
            if (!(in >> id >> std::quoted(follow.owner) >> follow.targetId
                >> follow.lastTargetCell.x >> follow.lastTargetCell.y) || id <= 0
                || follow.targetId <= 0 || id == follow.targetId || !validOwner(follow.owner)
                || !(validCell(follow.lastTargetCell) || follow.lastTargetCell == RuntimeMultiplayerGameplayCell{-1, -1})
                || !candidate.following.emplace(id, std::move(follow)).second) return reject();
        }
        if (!readCount()) return reject();
        for (std::size_t i = 0; i < count; ++i) {
            std::string owner, category; int id = 0;
            if (!(in >> std::quoted(owner) >> std::quoted(category) >> id) || id <= 0
                || !validOwner(owner) || category.empty() || category.size() > 256U
                || !candidate.primaryFactories.emplace(std::make_pair(owner, category), id).second) return reject();
        }
        if (!readCount()) return reject();
        for (std::size_t i = 0; i < count; ++i) {
            int id = 0; RuntimePlayerFactoryRally rally;
            if (!(in >> id >> std::quoted(rally.owner) >> rally.cell.x >> rally.cell.y) || id <= 0
                || !validOwner(rally.owner) || !validCell(rally.cell)
                || !candidate.rallyPoints.emplace(id, std::move(rally)).second) return reject();
        }
        if (schema >= 2) {
            if (!readCount()) return reject();
            for (std::size_t i = 0; i < count; ++i) {
                std::string left, right; int allied = 0;
                if (!(in >> std::quoted(left) >> std::quoted(right) >> allied)
                    || !validOwner(left) || !validOwner(right) || left == right
                    || (allied != 0 && allied != 1)
                    || !candidate.alliances.emplace(std::make_pair(left, right), allied != 0).second)
                    return reject();
            }
        }
        if (schema >= 3) {
            if (!readCount()) return reject();
            for (std::size_t i = 0; i < count; ++i) {
                int id = 0, kind = 0, guarding = 0;
                RuntimePlayerTravelOrder travel;
                if (!(in >> id >> std::quoted(travel.owner) >> travel.destination.x
                        >> travel.destination.y >> kind >> guarding >> travel.nextMoveAttemptTick)
                    || id <= 0 || !validOwner(travel.owner) || !validCell(travel.destination)
                    || (kind != static_cast<int>(RuntimeMultiplayerGameplayTargetOrderKind::AttackMove)
                        && kind != static_cast<int>(RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination))
                    || (guarding != 0 && guarding != 1) || travel.nextMoveAttemptTick < 0
                    || (guarding && kind != static_cast<int>(RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination))
                    || candidate.following.contains(id)) return reject();
                travel.kind = static_cast<RuntimeMultiplayerGameplayTargetOrderKind>(kind);
                travel.guarding = guarding != 0;
                if (!candidate.travelling.emplace(id, std::move(travel)).second) return reject();
            }
        }
        if (schema >= 4) {
            std::string savedWaypoints;
            if (!(in >> std::quoted(savedWaypoints)) || !candidate.waypoints.restore(savedWaypoints)) return reject();
        }
        in >> std::ws;
        if (!in.eof()) return reject();
        *this = std::move(candidate); error.clear(); return true;
    }
};
