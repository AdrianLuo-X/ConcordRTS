// Included by RuntimeVision.cpp. Separate from permanent exploration so an
// artillery shot cannot permanently uncover an enemy base or a Gap Generator.
bool RuntimeVisionSystem::applyFireReveals(ObserverGrid& grid, int tick) {
    bool changed = false;
    auto it = fireReveals_.lower_bound(FireRevealKey{grid.owner, -1, -1});
    for (; it != fireReveals_.end() && std::get<0>(it->first) == grid.owner; ++it) {
        if (it->second <= tick) continue;
        const int cx = std::get<1>(it->first), cy = std::get<2>(it->first);
        for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
            if (dx * dx + dy * dy > 9) continue;
            const int index = cellIndex(cx + dx, cy + dy);
            if (index < 0 || validCells_.empty() || !validCells_[index]) continue;
            if (!grid.visible[index]) {
                grid.visible[index] = 1;
                grid.visibleIndices.push_back(index);
                changed = true;
            }
            if (grid.gapShrouded[index]) {
                grid.gapShrouded[index] = 0;
                changed = true;
            }
        }
    }
    if (changed) {
        std::erase_if(grid.gapShroudedIndices, [&grid](int index) { return !grid.gapShrouded[index]; });
        grid.stats.visibleCells = static_cast<int>(grid.visibleIndices.size());
        grid.stats.gapShroudedCells = static_cast<int>(grid.gapShroudedIndices.size());
    }
    return changed;
}

bool RuntimeVisionSystem::notifyWeaponFired(const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon, int tick, int fixedTickHz) {
    if (!weapon.revealOnFire || !attacker.alive || !target.alive
        || attacker.instanceId < 0 || target.instanceId < 0 || tick < 0) return false;
    const auto observer = observers_.find(canonicalOwner(target.owner));
    if (observer == observers_.end()) return false;
    auto& grid = observer->second;
    const double px = attacker.hasRenderPosition ? attacker.renderX : attacker.x;
    const double py = attacker.hasRenderPosition ? attacker.renderY : attacker.y;
    if (!std::isfinite(px) || !std::isfinite(py) || px < 0 || py < 0 || px > 511 || py > 511)
        return false;
    const int x = static_cast<int>(std::lround(px)), y = static_cast<int>(std::lround(py));
    const int index = cellIndex(x, y);
    if (index < 0 || validCells_.empty() || !validCells_[index]) return false;
    const FireRevealKey key{grid.owner, x, y};
    const auto active = fireReveals_.find(key);
    if (visibilityForGrid(grid, x, y) != RuntimeCellVisibility::Shrouded
        && (active == fireReveals_.end() || active->second <= tick)) return false;
    const int expiry = static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(),
        static_cast<std::int64_t>(tick) + 5LL * std::clamp(fixedTickHz, 1, 1000)));
    if (expiry <= tick) return false;
    fireReveals_[key] = expiry;
    if (applyFireReveals(grid, tick)) ++grid.stats.revision;
    return true;
}

std::string RuntimeVisionSystem::serializeFireReveals(int tick) const {
    std::ostringstream output;
    const auto count = std::count_if(fireReveals_.begin(), fireReveals_.end(),
        [tick](const auto& entry) { return entry.second > tick; });
    output << "1 " << count << '\n';
    for (const auto& [key, expiry] : fireReveals_) if (expiry > tick)
        output << std::quoted(std::get<0>(key)) << ' ' << std::get<1>(key) << ' '
            << std::get<2>(key) << ' ' << expiry << '\n';
    return output.str();
}

bool RuntimeVisionSystem::restoreFireReveals(const std::string& text, int tick, int fixedTickHz) {
    if (text.size() > 4U * 1024U * 1024U || tick < 0) return false;
    std::istringstream input(text);
    int version = 0, count = -1;
    if (!(input >> version >> count) || version != 1 || count < 0 || count > 65536) return false;
    decltype(fireReveals_) staged;
    for (int i = 0; i < count; ++i) {
        std::string owner; int x = -1, y = -1, expiry = 0;
        if (!(input >> std::quoted(owner) >> x >> y >> expiry)
            || owner.empty() || owner.size() > 256 || owner != canonicalOwner(owner)
            || !hasObserver(owner) || expiry <= tick
            || static_cast<std::int64_t>(expiry) - tick > 5LL * std::clamp(fixedTickHz, 1, 1000)) return false;
        const int index = cellIndex(x, y);
        if (index < 0 || validCells_.empty() || !validCells_[index]
            || !staged.emplace(FireRevealKey{owner, x, y}, expiry).second) return false;
    }
    input >> std::ws;
    if (!input.eof()) return false;
    fireReveals_.swap(staged);
    for (auto& [owner, grid] : observers_)
        if (applyFireReveals(grid, tick)) ++grid.stats.revision;
    return true;
}
