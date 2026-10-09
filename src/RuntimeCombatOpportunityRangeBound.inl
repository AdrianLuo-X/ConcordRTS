// One read-only target selection owns this bound. It is deliberately not a
// world/type cache: firing can change ownership, rank, rules and weapon modes.
struct RuntimeCombatOpportunityRangeBound {
    bool enabled = false;
    double maximumBaseRange = 0.0;
    double elevationCap = 0.0;
    double elevationBonus = 0.0;
    int elevationIncrement = 1;

    RuntimeCombatOpportunityRangeBound(const RuntimeWindowState& state,
        const WorldObjectInstance& pose, const WeaponType* fixedWeapon,
        double rangeBonus, bool passengerFire) {
        // Specialized firing contexts retain their complete scalar selection.
        if (fixedWeapon != nullptr || passengerFire || rangeBonus != 0.0
            || pose.airborne || pose.tankBunkerHostId >= 0
            || (pose.kind != WorldObjectKind::Infantry && pose.kind != WorldObjectKind::Unit)
            || pose.activeGunnerWeaponIndex >= 0) return;
        const auto& cached = runtimeCachedCapabilityProfileForObject(state, pose);
        if (!cached.rulesResolved || cached.gattling || cached.airRangeBonus != 0.0
            || !cached.indexedWeaponCandidates.empty() || !cached.eliteIndexedWeaponCandidates.empty()
            || cached.openTransportWeapon.has_value()) return;
        const auto& elevation = state.rules.getElevationModelRules();
        if (!std::isfinite(elevation.bonusCap) || !std::isfinite(elevation.incrementBonus)) return;
        elevationCap = std::max(0.0, elevation.bonusCap);
        elevationBonus = std::max(0.0, elevation.incrementBonus);
        elevationIncrement = std::max(1, elevation.increment);
        bool foundWeapon = false;
        const auto include = [&](const auto& candidates) {
            for (const auto& candidate : candidates) {
                const WeaponType* weapon = findRuntimeWeaponType(state.rules, candidate.second);
                if (weapon == nullptr) continue;
                // Infinite spawners and unusual authored numbers cannot be
                // represented by this finite upper bound. Never truncate them.
                if (weapon->spawner || !std::isfinite(weapon->range)) return false;
                maximumBaseRange = std::max(maximumBaseRange, std::max(0.0, weapon->range));
                foundWeapon = true;
            }
            return true;
        };
        // With indexed/gunner/open-transport paths excluded, these are the
        // complete candidate lists consulted by runtimeWeaponProfileForObject.
        // Include both ranks and inactive deploy/occupy slots conservatively.
        enabled = include(cached.baseWeaponCandidates)
            && include(cached.deployWeaponCandidates)
            && include(cached.occupyWeaponCandidates) && foundWeapon;
    }

    bool excludes(const RuntimeWindowState& state, const WorldObjectInstance& pose,
        const WorldObjectInstance& candidate) const {
        if (!enabled) return false;
        double upper = maximumBaseRange;
        if (!candidate.airborne && pose.level > candidate.level) {
            const double height = static_cast<double>(static_cast<std::int64_t>(pose.level) - candidate.level);
            upper += std::min(elevationCap, std::floor(height / elevationIncrement)) * elevationBonus;
        }
        if (!std::isfinite(upper)) return false;
        // A footprint's nearest cell is no farther than its origin, so this
        // lower bound also covers weapons using CellRangefinding=yes. Keep a
        // relative margin; the original exact range check still decides hits.
        const auto footprint = runtimeStructureFootprintForObject(state, candidate);
        const std::int64_t lastX = static_cast<std::int64_t>(candidate.x) + std::max(1, footprint.width) - 1;
        const std::int64_t lastY = static_cast<std::int64_t>(candidate.y) + std::max(1, footprint.height) - 1;
        if (lastX > std::numeric_limits<int>::max() || lastY > std::numeric_limits<int>::max()) return false;
        const RuntimeCellPoint nearest{
            std::clamp(pose.x, candidate.x, static_cast<int>(lastX)),
            std::clamp(pose.y, candidate.y, static_cast<int>(lastY))
        };
        const std::int64_t dx = static_cast<std::int64_t>(pose.x) - nearest.x;
        const std::int64_t dy = static_cast<std::int64_t>(pose.y) - nearest.y;
        if (dx < std::numeric_limits<int>::min() || dx > std::numeric_limits<int>::max()
            || dy < std::numeric_limits<int>::min() || dy > std::numeric_limits<int>::max()) return false;
        const double distance = runtimeCellDistance(runtimeCellPointForObject(pose), nearest);
        return distance > upper + 1.0e-9 * std::max(1.0, upper);
    }
};
