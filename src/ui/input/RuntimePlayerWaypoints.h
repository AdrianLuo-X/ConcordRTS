#pragma once

#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

struct RuntimePlayerWaypoint {
    std::uint64_t id = 0;
    RuntimeMultiplayerGameplayTargetOrderCommand order;
};

struct RuntimePlayerWaypointRoute {
    std::string owner;
    std::vector<RuntimePlayerWaypoint> points;
    std::size_t cursor = 0;
    std::uint64_t loopStart = 0;
    bool loopCommitted = false;
    bool active = false;
    int nextAttemptTick = 0;

    std::size_t indexOf(std::uint64_t id) const {
        const auto found = std::find_if(points.begin(), points.end(),
            [id](const auto& p) { return p.id == id; });
        return static_cast<std::size_t>(found - points.begin());
    }
    void finishPoint() {
        active = false;
        if (points.empty()) { cursor = 0; return; }
        if (loopStart && cursor >= indexOf(loopStart)) {
            if (++cursor >= points.size()) cursor = indexOf(loopStart);
        } else {
            points.erase(points.begin() + cursor);
            if (cursor >= points.size()) cursor = 0;
        }
    }
    // Return true when deleting the current destination requires a new order.
    bool erasePoint(std::uint64_t id) {
        const auto index = indexOf(id);
        if (index == points.size()) return false;
        const bool interrupt = index == cursor && active;
        const bool loopOrigin = id == loopStart;
        points.erase(points.begin() + index);
        if (index < cursor) --cursor;
        if (cursor >= points.size()) cursor = 0;
        if (interrupt) active = false;
        if (loopOrigin) loopStart = loopCommitted && !points.empty()
            ? points[std::min(index, points.size() - 1)].id : 0;
        if (loopStart && points.size() - indexOf(loopStart) < 2) loopStart = 0;
        if (!loopStart) loopCommitted = false;
        nextAttemptTick = 0;
        return interrupt;
    }
};

// This state is saved and hashed. Local marker selection is deliberately separate.
struct RuntimePlayerWaypointState {
    static constexpr std::size_t maximumPoints = 64;
    std::uint64_t nextId = 1;
    std::set<std::string> planningOwners;
    std::map<int, RuntimePlayerWaypointRoute> routes;

    void cancel(const std::vector<std::int32_t>& ids) {
        for (int id : ids) routes.erase(id);
    }
    bool ownsPoint(const std::string& owner, std::uint64_t id) const {
        if (!id) return false;
        return std::any_of(routes.begin(), routes.end(), [&](const auto& entry) {
            return entry.second.owner == owner
                && entry.second.indexOf(id) < entry.second.points.size();
        });
    }
    bool canAppend(const std::string& owner, const std::vector<std::int32_t>& ids) const {
        if (ids.empty() || nextId == std::numeric_limits<std::uint64_t>::max()) return false;
        return std::none_of(ids.begin(), ids.end(), [&](int id) {
            const auto found = routes.find(id);
            return found != routes.end() && found->second.owner == owner
                && found->second.points.size() >= maximumPoints;
        });
    }
    bool append(const std::string& owner, const RuntimeMultiplayerGameplayTargetOrderCommand& order) {
        if (!canAppend(owner, order.objectIds)) return false;
        const auto pointId = nextId++;
        for (int id : order.objectIds) {
            auto& route = routes[id];
            if (route.owner != owner) { route = {}; route.owner = owner; }
            auto single = order; single.objectIds = {id}; single.append = false;
            route.points.push_back({pointId, std::move(single)});
        }
        return true;
    }
    std::string serialize() const {
        std::ostringstream out;
        out << nextId << ' ' << planningOwners.size();
        for (const auto& owner : planningOwners) out << ' ' << std::quoted(owner);
        out << ' ' << routes.size();
        for (const auto& [id, route] : routes) {
            out << ' ' << id << ' ' << std::quoted(route.owner) << ' ' << route.cursor
                << ' ' << route.loopStart << ' ' << route.loopCommitted << ' ' << route.active
                << ' ' << route.nextAttemptTick << ' ' << route.points.size();
            for (const auto& point : route.points) {
                RuntimeMultiplayerGameplayCommand command; command.body = point.order;
                std::vector<std::uint8_t> bytes; std::string error;
                if (!encodeRuntimeMultiplayerGameplayCommand(command, bytes, error)) return {};
                out << ' ' << point.id << ' ';
                constexpr char hex[] = "0123456789abcdef";
                for (const auto b : bytes) out << hex[b >> 4] << hex[b & 15];
            }
        }
        return out.str();
    }
    bool restore(const std::string& encoded) {
        RuntimePlayerWaypointState candidate;
        std::istringstream in(encoded);
        std::size_t count = 0;
        const auto validOwner = [](const auto& owner) { return !owner.empty() && owner.size() <= 256; };
        if (!(in >> candidate.nextId >> count) || candidate.nextId == 0 || count > 64) return false;
        for (std::size_t i = 0; i < count; ++i) {
            std::string owner;
            if (!(in >> std::quoted(owner)) || !validOwner(owner)
                || !candidate.planningOwners.insert(owner).second) return false;
        }
        if (!(in >> count) || count > 65536) return false;
        for (std::size_t i = 0; i < count; ++i) {
            int id = 0, committed = 0, active = 0;
            std::size_t length = 0;
            RuntimePlayerWaypointRoute route;
            if (!(in >> id >> std::quoted(route.owner) >> route.cursor >> route.loopStart
                    >> committed >> active >> route.nextAttemptTick >> length)
                || id <= 0 || !validOwner(route.owner) || length == 0 || length > maximumPoints
                || route.cursor >= length || route.nextAttemptTick < 0
                || (committed != 0 && committed != 1) || (active != 0 && active != 1)) return false;
            route.loopCommitted = committed != 0; route.active = active != 0;
            std::set<std::uint64_t> ids;
            for (std::size_t j = 0; j < length; ++j) {
                RuntimePlayerWaypoint point; std::string hex;
                if (!(in >> point.id >> hex) || point.id == 0 || point.id >= candidate.nextId
                    || !ids.insert(point.id).second || hex.empty() || hex.size() % 2
                    || hex.size() > 2 * runtimeMultiplayerGameplayMaximumPayloadBytes) return false;
                std::vector<std::uint8_t> bytes;
                const auto nibble = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    return -1;
                };
                for (std::size_t k = 0; k < hex.size(); k += 2) {
                    const int hi = nibble(hex[k]), lo = nibble(hex[k + 1]);
                    if (hi < 0 || lo < 0) return false;
                    bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
                }
                RuntimeMultiplayerGameplayCommand command; std::string error;
                if (!decodeRuntimeMultiplayerGameplayCommand(bytes, command, error)) return false;
                const auto* order = std::get_if<RuntimeMultiplayerGameplayTargetOrderCommand>(&command.body);
                if (!order || order->append || order->objectIds != std::vector<std::int32_t>{id}) return false;
                point.order = *order; route.points.push_back(std::move(point));
            }
            if (route.loopStart && (route.indexOf(route.loopStart) >= route.points.size()
                || route.points.size() - route.indexOf(route.loopStart) < 2)) return false;
            if (route.loopCommitted && !route.loopStart) return false;
            if (!candidate.routes.emplace(id, std::move(route)).second) return false;
        }
        in >> std::ws;
        if (!in.eof()) return false;
        *this = std::move(candidate);
        return true;
    }
};
