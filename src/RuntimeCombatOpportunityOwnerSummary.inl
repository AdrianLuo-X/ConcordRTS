// Local to one independent-fire pass. No pointers into the population and no
// relation result survive a fire attempt, which may run immediate world actions.
struct RuntimeCombatOpportunityOwnerSummary {
    bool valid = false;
    bool disabledForPass = false;
    const void* objectData = nullptr;
    std::size_t objectCount = 0;
    std::vector<std::string> livingOwners;
    std::unordered_map<std::string, bool> hasHostileOwner;
    int populationScans = 0;
    int emptyProofs = 0;

    void invalidate() { valid = false; }

    bool provesNoCandidate(const RuntimeWindowState& state,
        const WorldObjectInstance& pose, bool forceFire) {
        // A pass containing hostile owners already needs ordinary targeting.
        // Keep this conservative decision across fire invalidations so combat
        // never rebuilds the population census for every subsequent shooter.
        if (disabledForPass) return false;
        // Force fire may admit an allied/neutral preferred target. Any active
        // psychic record takes the scalar gate, including both berserk policies.
        if (forceFire) return false;
        const auto* psychic = state.psychicStatuses.findByTarget(pose.instanceId);
        if (psychic != nullptr && state.tick < psychic->expiresTick) return false;
        const auto& objects = state.world.getObjects();
        if (!valid || objectData != objects.data() || objectCount != objects.size()) {
            livingOwners.clear();
            hasHostileOwner.clear();
            std::unordered_set<std::string> seen;
            for (const auto& object : objects) {
                if (object.alive && object.health > 0 && seen.insert(object.owner).second)
                    livingOwners.push_back(object.owner);
            }
            objectData = objects.data(); objectCount = objects.size();
            valid = true;
            ++populationScans;
        }
        auto found = hasHostileOwner.find(pose.owner);
        if (found == hasHostileOwner.end()) {
            bool hostile = false;
            for (const auto& owner : livingOwners) {
                if (runtimeOwnersAreHostileForAutoTarget(state, pose.owner, owner)) {
                    hostile = true;
                    break;
                }
            }
            found = hasHostileOwner.emplace(pose.owner, hostile).first;
        }
        if (found->second) {
            disabledForPass = true;
            return false;
        }
        ++emptyProofs;
        return true;
    }
};
