namespace {
class ParasiteHostDeathQueries final : public concord::simulation::abilities::RuntimeParasiteHostDeathQueries {
public:
    explicit ParasiteHostDeathQueries(const FixtureState& state) noexcept : state_(state) {}
    const WorldObjectInstance* objectIncludingInactive(int id) const override {
        return findRuntimeObjectByIdIncludingInactive(state_, id);
    }
    bool damageIsTypeImmune(const WorldObjectInstance& target, int sourceId, int damage) const override {
        return runtimeDamageIsTypeImmune(state_, target, runtimeTypeImmuneSource(state_, sourceId), damage);
    }
    int authoredDamage(const WorldObjectInstance& target, int damage, const std::string& warhead,
        double distance, double modifier) const override {
        return runtimeAuthoredDamageAgainst(state_, target, damage, warhead, distance, modifier);
    }
    RuntimeCellPoint objectCell(const WorldObjectInstance& object) const override {
        return runtimeCellPointForObject(object);
    }
    std::optional<RuntimeCellPoint> placementCell(const WorldObjectInstance& source,
        const SpecialCell& requested, const std::unordered_set<int>* reserved, int terminatingHostId) const override {
        return runtimeParasitePlacementCell(state_, source, requested, reserved, terminatingHostId);
    }
private:
    const FixtureState& state_;
};
class ParasiteHostDeathCandidate final : public concord::simulation::abilities::RuntimeParasiteHostDeathCandidate {
public:
    explicit ParasiteHostDeathCandidate(FixtureCompleteState& candidate) noexcept : candidate_(candidate) {}
    const std::map<int, ParasiteLinkState>& links() const override { return candidate_.parasiteLinks(); }
    ParasiteOperationResult handle(const ParasiteHostEventRequest& request) override {
        return candidate_.handleParasiteHostEvent(request, SpecialEventSink{});
    }
private:
    FixtureCompleteState& candidate_;
};
class ParasiteHostDeathRejection final : public concord::simulation::abilities::RuntimeParasiteHostDeathRejection {
public:
    ParasiteHostDeathRejection(FixtureState& state, RuntimeParasiteActionApplyResult& result) noexcept
        : state_(state), result_(result) {}
    void rejectAtomically() override {
        result_.accepted = false;
        state_.simulationStatus =
            "parasite-caused host death rejected atomically";
    }
private:
    FixtureState& state_;
    RuntimeParasiteActionApplyResult& result_;
};
}
