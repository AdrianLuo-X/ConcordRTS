// Geometry only, scoped to one independent-fire pass. The caller invalidates
// this index before a fire attempt can run immediate world actions. Candidate
// pointers, owner relations and weapon profiles are never cached here.
struct RuntimeCombatOpportunityCandidates {
    static constexpr std::int64_t bucketSize = 8;
    const RuntimeWindowState* stateIdentity = nullptr;
    const void* objectData = nullptr;
    std::size_t objectCount = 0;
    bool valid = false;
    bool disabledForPass = false;
    bool geometryRepresentable = true;
    std::int64_t maximumWidth = 1;
    std::int64_t maximumHeight = 1;
    std::int64_t minimumX = 0, maximumX = 0;
    std::int64_t minimumY = 0, maximumY = 0;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> buckets;
    std::vector<std::size_t> orderedCandidates;
    std::uint64_t populationBuilds = 0;
    std::uint64_t indexedQueries = 0;
    std::uint64_t candidateVisits = 0;

    void invalidate() { valid = false; }

    void invalidateForWorldAction() {
        valid = false;
        // Once shots can alter the world, finish this pass with scalar reads.
        // Do not rebuild the whole population separately for every shooter.
        disabledForPass = true;
    }

    static std::int64_t coordinate(std::int64_t value) {
        const auto quotient = value / bucketSize;
        return quotient - (value % bucketSize < 0 ? 1 : 0);
    }

    static std::uint64_t key(std::int64_t x, std::int64_t y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U)
            | static_cast<std::uint32_t>(y);
    }

    void rebuild(const RuntimeWindowState& state) {
        const auto& objects = state.world.getObjects();
        buckets.clear();
        maximumWidth = maximumHeight = 1;
        geometryRepresentable = true;
        bool found = false;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            const auto& object = objects[index];
            if (!object.alive || object.health <= 0) continue;
            const auto footprint = runtimeStructureFootprintForObject(state, object);
            const auto width = static_cast<std::int64_t>(std::max(1, footprint.width));
            const auto height = static_cast<std::int64_t>(std::max(1, footprint.height));
            const auto lastX = static_cast<std::int64_t>(object.x) + width - 1;
            const auto lastY = static_cast<std::int64_t>(object.y) + height - 1;
            // The scalar bound deliberately retains unrepresentable geometry.
            // A shortlist must likewise leave every such candidate available.
            if (lastX > std::numeric_limits<int>::max()
                || lastY > std::numeric_limits<int>::max()) geometryRepresentable = false;
            maximumWidth = std::max(maximumWidth, width);
            maximumHeight = std::max(maximumHeight, height);
            if (!found) {
                minimumX = object.x; maximumX = lastX;
                minimumY = object.y; maximumY = lastY;
                found = true;
            } else {
                minimumX = std::min(minimumX, static_cast<std::int64_t>(object.x));
                maximumX = std::max(maximumX, lastX);
                minimumY = std::min(minimumY, static_cast<std::int64_t>(object.y));
                maximumY = std::max(maximumY, lastY);
            }
            buckets[key(coordinate(object.x), coordinate(object.y))].push_back(index);
        }
        stateIdentity = &state;
        objectData = objects.data(); objectCount = objects.size();
        valid = true;
        ++populationBuilds;
    }

    // nullptr means use the unchanged scalar walk. An empty vector proves that
    // no living origin/footprint can pass the existing conservative range bound.
    const std::vector<std::size_t>* query(const RuntimeWindowState& state,
        const WorldObjectInstance& pose, const RuntimeCombatOpportunityRangeBound& bound) {
        if (!bound.enabled || disabledForPass) return nullptr;
        const auto& objects = state.world.getObjects();
        // Building an index costs more than walking a small population once.
        if (objects.size() < 64) return nullptr;
        const double upper = bound.maximumBaseRange + bound.elevationCap * bound.elevationBonus;
        const double padded = upper + 1.0e-9 * std::max(1.0, upper);
        if (!std::isfinite(padded) || padded > std::numeric_limits<int>::max()) return nullptr;
        if (!valid || stateIdentity != &state || objectData != objects.data()
            || objectCount != objects.size()) rebuild(state);
        if (!geometryRepresentable) return nullptr;
        // Avoid pruning where the original int-coordinate distance helper or
        // conservative scalar bound has to retain extreme coordinates.
        const auto intMin = static_cast<std::int64_t>(std::numeric_limits<int>::min());
        const auto intMax = static_cast<std::int64_t>(std::numeric_limits<int>::max());
        if (!buckets.empty()
            && (static_cast<std::int64_t>(pose.x) - minimumX > intMax
                || static_cast<std::int64_t>(pose.x) - maximumX < intMin
                || static_cast<std::int64_t>(pose.y) - minimumY > intMax
                || static_cast<std::int64_t>(pose.y) - maximumY < intMin)) return nullptr;
        const auto radius = static_cast<std::int64_t>(std::ceil(padded));
        const auto firstX = coordinate(static_cast<std::int64_t>(pose.x) - radius - maximumWidth + 1);
        const auto lastX = coordinate(static_cast<std::int64_t>(pose.x) + radius);
        const auto firstY = coordinate(static_cast<std::int64_t>(pose.y) - radius - maximumHeight + 1);
        const auto lastY = coordinate(static_cast<std::int64_t>(pose.y) + radius);
        const auto columns = lastX - firstX + 1;
        const auto rows = lastY - firstY + 1;
        // Large foundations/mod ranges keep their scalar path instead of
        // turning the sparse bucket query into a large empty-grid traversal.
        if (columns > 256 || rows > 256 || columns * rows > 256) return nullptr;
        orderedCandidates.clear();
        for (auto y = firstY; y <= lastY; ++y) for (auto x = firstX; x <= lastX; ++x) {
            const auto bucket = buckets.find(key(x, y));
            if (bucket != buckets.end())
                orderedCandidates.insert(orderedCandidates.end(), bucket->second.begin(), bucket->second.end());
        }
        // Original world order decides preferred targets and exact floating
        // comparisons. Bucket/hash iteration must not decide gameplay order.
        std::sort(orderedCandidates.begin(), orderedCandidates.end());
        ++indexedQueries;
        candidateVisits += orderedCandidates.size();
        return &orderedCandidates;
    }
};

bool runtimeCombatOpportunityCandidatesReference() {
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_OPPORTUNITY_INDEX_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
