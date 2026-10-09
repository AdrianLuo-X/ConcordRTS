#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <locale>
#include <map>
#include <sstream>
#include <string>

// Distances and speed are leptons (256/cell), in Q16. Speed/acceleration use
// the original 15-frame clock; no render clock or wall time enters authority.
struct RuntimeJumpjetFlight {
    std::int64_t height = 0;
    std::int64_t speed = 0;
    std::int64_t progress = 0;
    std::int64_t length = 0;
    int fromX = 0, fromY = 0, toX = 0, toY = 0;
    int lastTick = -1;
    int verticalTick = -1;
    int wobblePhase = 0;
    bool active = false;
    int goalX = -1, goalY = -1, targetX = -1, targetY = -1;
    int goalRadius = 8;
    // Unused distance from the tick that just reached a cell boundary. Only
    // an immediately approved continuation in that same tick may consume it.
    std::int64_t segmentCarry = 0;
    // Stop freezes the current fractional segment until a new travel order.
    bool stopped = false;
    // Local roof presentation uses the same lepton Q16 scale, but never
    // changes height or the physical segment. Excluded from authority saves.
    std::int64_t clearanceLift = 0;
    int clearanceTick = -1;
};
using RuntimeJumpjetFlights = std::map<int, RuntimeJumpjetFlight>;
constexpr std::int64_t runtimeJumpjetQ16 = 65536;
constexpr std::int64_t runtimeJumpjetCellQ16 = 256 * runtimeJumpjetQ16;

inline std::string serializeRuntimeJumpjetFlights(const RuntimeJumpjetFlights& flights,
    bool includePresentation = true) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const bool extended = std::any_of(flights.begin(), flights.end(), [](const auto& entry) {
        const auto& f = entry.second;
        return f.stopped;
    });
    const int schema = includePresentation ? 5 : extended ? 4 : 3;
    out << "JumpjetFlights " << schema << ' ' << flights.size() << '\n';
    for (const auto& [id, f] : flights)
    {
        out << id << ' ' << f.height << ' ' << f.speed << ' ' << f.progress << ' ' << f.length << ' '
            << f.fromX << ' ' << f.fromY << ' ' << f.toX << ' ' << f.toY << ' '
            << f.lastTick << ' ' << f.verticalTick << ' ' << f.wobblePhase << ' ' << f.active << ' '
            << f.goalX << ' ' << f.goalY << ' ' << f.targetX << ' ' << f.targetY << ' ' << f.goalRadius
            << ' ' << f.segmentCarry;
        if (schema >= 4) out << ' ' << f.stopped;
        if (includePresentation) out << ' ' << f.clearanceLift << ' ' << f.clearanceTick;
        out << '\n';
    }
    return out.str();
}

inline bool deserializeRuntimeJumpjetFlights(const std::string& text, RuntimeJumpjetFlights& result) {
    if (text.size() > 16U * 1024U * 1024U) return false;
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::string magic;
    int schema = 0, count = 0;
    if (!(in >> magic >> schema >> count) || magic != "JumpjetFlights" || (schema < 1 || schema > 5)
        || count < 0 || count > 100000) return false;
    RuntimeJumpjetFlights staged;
    for (int n = 0; n < count; ++n) {
        int id = -1, active = -1;
        RuntimeJumpjetFlight f;
        if (!(in >> id >> f.height >> f.speed >> f.progress >> f.length
                >> f.fromX >> f.fromY >> f.toX >> f.toY >> f.lastTick >> f.verticalTick >> f.wobblePhase >> active)
            || id < 0 || f.height < -128 * runtimeJumpjetCellQ16 || f.height > 1024 * runtimeJumpjetCellQ16
            || f.speed < 0 || f.speed > 100000 * runtimeJumpjetQ16
            || f.length < 0 || f.length > 2 * runtimeJumpjetCellQ16
            || f.progress < 0 || f.progress > f.length || f.lastTick < -1 || f.verticalTick < -1
            || f.wobblePhase < 0 || f.wobblePhase >= 65536 || active < 0 || active > 1
            || f.fromX < 0 || f.fromY < 0 || f.toX < 0 || f.toY < 0
            || f.fromX >= 512 || f.fromY >= 512 || f.toX >= 512 || f.toY >= 512
            || std::abs(f.fromX-f.toX) > 1 || std::abs(f.fromY-f.toY) > 1
            || (active && (f.length == 0 || f.progress >= f.length))) return false;
        f.active = active != 0;
        if (schema >= 2 && (!(in >> f.goalX >> f.goalY >> f.targetX >> f.targetY >> f.goalRadius)
            || f.goalX < -1 || f.goalY < -1 || f.targetX < -1 || f.targetY < -1
            || f.goalX >= 512 || f.goalY >= 512 || f.targetX >= 512 || f.targetY >= 512
            || ((f.goalX == -1) != (f.goalY == -1)) || ((f.targetX == -1) != (f.targetY == -1))
            || ((f.goalX == -1) != (f.targetX == -1)) || f.goalRadius < 1 || f.goalRadius > 512)) return false;
        if (schema >= 3 && (!(in >> f.segmentCarry) || f.segmentCarry < 0
            || f.segmentCarry > 1500000 * runtimeJumpjetQ16
            || (f.segmentCarry > 0 && (f.active || f.lastTick < 0 || f.progress != f.length)))) return false;
        if (schema >= 4) {
            int stopped = -1;
            if (!(in >> stopped)
                || stopped < 0 || stopped > 1
                || (stopped && (f.active || f.speed != 0 || f.segmentCarry != 0
                    || f.length == 0 || f.progress >= f.length))) return false;
            f.stopped = stopped != 0;
        }
        if (schema >= 5 && (!(in >> f.clearanceLift >> f.clearanceTick)
            || f.clearanceLift < 0 || f.clearanceLift > 64 * runtimeJumpjetCellQ16
            || f.clearanceTick < -1 || (f.clearanceLift > 0 && f.clearanceTick < 0))) return false;
        if (f.length > 0) {
            const auto expected = std::llround(std::hypot(f.toX-f.fromX, f.toY-f.fromY) * runtimeJumpjetCellQ16);
            if (f.length != expected) return false;
        }
        if (!staged.emplace(id, f).second) return false;
    }
    in >> std::ws;
    if (!in.eof()) return false;
    result = std::move(staged);
    return true;
}
