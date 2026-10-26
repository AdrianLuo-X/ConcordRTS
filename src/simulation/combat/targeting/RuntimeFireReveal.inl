// Temporary fire visibility is owned by the attacked observer. It never
// contributes to the permanent exploration mask.
namespace RuntimeFireRevealDetail {
constexpr int radius = 3;
constexpr std::array<int, 7> rowHalfWidths{0, 2, 2, 3, 2, 2, 0};

std::int64_t lifetimeTicks(int fixedTickHz) {
    return 5LL * std::clamp(fixedTickHz, 1, 1000);
}

int deadline(int tick, int fixedTickHz) {
    const std::int64_t value = static_cast<std::int64_t>(tick) + lifetimeTicks(fixedTickHz);
    return static_cast<int>(std::min(value,
        static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

bool contains(int centerX, int centerY, int x, int y) {
    const int dx = x - centerX;
    const int dy = y - centerY;
    return dx * dx + dy * dy <= radius * radius;
}
} // namespace RuntimeFireRevealDetail

bool RuntimeVisionSystem::applyFireReveals(ObserverGrid& grid, int tick) {
    if (validCells_.empty()) return false;
    bool addedVisibility = false;
    bool removedGap = false;
    auto pulse = fireReveals_.lower_bound(FireRevealKey{grid.owner, 0, 0});
    while (pulse != fireReveals_.end() && std::get<0>(pulse->first) == grid.owner) {
        const auto& [key, expiresAt] = *pulse++;
        if (expiresAt <= tick) continue;
        const auto& [owner, centerX, centerY] = key;
        (void)owner;
        for (int row = 0; row < static_cast<int>(RuntimeFireRevealDetail::rowHalfWidths.size()); ++row) {
            const int y = centerY + row - RuntimeFireRevealDetail::radius;
            if (y < 0 || y >= mapCellStride_) continue;
            const int halfWidth = RuntimeFireRevealDetail::rowHalfWidths[row];
            const int firstX = std::max(0, centerX - halfWidth);
            const int lastX = std::min(mapCellStride_ - 1, centerX + halfWidth);
            for (int x = firstX; x <= lastX; ++x) {
                const int index = y * mapCellStride_ + x;
                if (!validCells_[index]) continue;
                if (grid.visible[index] == 0) {
                    grid.visible[index] = 1;
                    grid.visibleIndices.push_back(index);
                    addedVisibility = true;
                }
                removedGap |= grid.gapShrouded[index] != 0;
                grid.gapShrouded[index] = 0;
            }
        }
    }
    if (addedVisibility) grid.stats.visibleCells = static_cast<int>(grid.visibleIndices.size());
    if (removedGap) {
        std::erase_if(grid.gapShroudedIndices,
            [&grid](int index) { return grid.gapShrouded[index] == 0; });
        grid.stats.gapShroudedCells = static_cast<int>(grid.gapShroudedIndices.size());
    }
    return addedVisibility || removedGap;
}

bool RuntimeVisionSystem::notifyWeaponFired(const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon, int tick, int fixedTickHz) {
    if (tick < 0 || !weapon.revealOnFire || !attacker.alive || !target.alive
        || attacker.instanceId < 0 || target.instanceId < 0) return false;
    const auto recipient = observers_.find(canonicalOwner(target.owner));
    if (recipient == observers_.end() || validCells_.empty()) return false;
    const double sourceX = attacker.hasRenderPosition ? attacker.renderX : attacker.x;
    const double sourceY = attacker.hasRenderPosition ? attacker.renderY : attacker.y;
    if (!std::isfinite(sourceX) || !std::isfinite(sourceY)
        || sourceX < 0.0 || sourceY < 0.0
        || sourceX > mapCellStride_ - 1 || sourceY > mapCellStride_ - 1) return false;
    const int x = static_cast<int>(std::lround(sourceX));
    const int y = static_cast<int>(std::lround(sourceY));
    const int index = cellIndex(x, y);
    if (index < 0 || validCells_[index] == 0) return false;

    auto& grid = recipient->second;
    bool temporaryCoverage = false;
    // A prior pulse may still be present in the displayed mask until update().
    // Its visibility must not suppress a new shot from an overlapping center.
    auto pulse = fireReveals_.lower_bound(FireRevealKey{grid.owner, 0, 0});
    for (; pulse != fireReveals_.end() && std::get<0>(pulse->first) == grid.owner; ++pulse) {
        if (pulse->second > grid.stats.lastUpdateTick
            && RuntimeFireRevealDetail::contains(std::get<1>(pulse->first),
                std::get<2>(pulse->first), x, y)) {
            temporaryCoverage = true;
            break;
        }
    }
    if (grid.explored[index] != 0 && grid.gapShrouded[index] == 0 && !temporaryCoverage)
        return false;

    const int expiresAt = RuntimeFireRevealDetail::deadline(tick, fixedTickHz);
    if (expiresAt <= tick) return false;
    auto [entry, inserted] = fireReveals_.try_emplace(FireRevealKey{grid.owner, x, y}, expiresAt);
    if (!inserted) entry->second = std::max(entry->second, expiresAt);
    if (applyFireReveals(grid, tick)) ++grid.stats.revision;
    return true;
}

std::string RuntimeVisionSystem::serializeFireReveals(int tick) const {
    std::ostringstream records;
    std::size_t count = 0;
    for (const auto& [key, expiresAt] : fireReveals_) {
        if (expiresAt <= tick) continue;
        const auto& [owner, x, y] = key;
        records << std::quoted(owner) << ' ' << x << ' ' << y << ' ' << expiresAt << '\n';
        ++count;
    }
    return "1 " + std::to_string(count) + "\n" + records.str();
}

bool RuntimeVisionSystem::restoreFireReveals(const std::string& text, int tick, int fixedTickHz) {
    if (tick < 0 || text.size() > 4U * 1024U * 1024U) return false;
    std::istringstream input(text);
    int version = 0;
    int count = 0;
    if (!(input >> version >> count) || version != 1 || count < 0 || count > 65536)
        return false;

    decltype(fireReveals_) replacement;
    for (int record = 0; record < count; ++record) {
        std::string owner;
        int x = 0, y = 0, expiresAt = 0;
        if (!(input >> std::quoted(owner) >> x >> y >> expiresAt)) return false;
        if (owner.empty() || owner.size() > 256 || canonicalOwner(owner) != owner
            || !hasObserver(owner)) return false;
        const auto remaining = static_cast<std::int64_t>(expiresAt) - tick;
        if (remaining <= 0 || remaining > RuntimeFireRevealDetail::lifetimeTicks(fixedTickHz))
            return false;
        const int index = cellIndex(x, y);
        if (index < 0 || validCells_.empty() || validCells_[index] == 0) return false;
        if (!replacement.emplace(FireRevealKey{owner, x, y}, expiresAt).second) return false;
    }
    input >> std::ws;
    if (!input.eof()) return false;

    fireReveals_ = std::move(replacement);
    for (auto& [owner, grid] : observers_) {
        (void)owner;
        // The next full update also removes coverage from pulses replaced by
        // this restore, even when the ordinary vision sources are unchanged.
        grid.sourceMemoValid = false;
        if (applyFireReveals(grid, tick)) ++grid.stats.revision;
    }
    return true;
}
