// Commit one net mining delta after all three paths and after restoring
// feedback guards. The same boundary commits partial work on stack unwinding.
void commitRuntimeComputerMiningWork(RuntimeWindowState& state,
    const std::string& owner, const RuntimeEconomyState& work, int startingCredits) {
    const std::string key = runtimeRepairDepotCanonicalOwnerId(state, owner);
    auto& ledger = state.computerMiningByOwner[key];
    ledger.storedResourceUnits = work.storedResourceUnits;
    ledger.storedResourceValue = work.storedResourceValue;
    ledger.storedResourceGemUnits = work.storedResourceGemUnits;
    ledger.resourceTrips = work.resourceTrips;
    ledger.harvesterTeleports = work.harvesterTeleports;
    ledger.slaveMinerLoads = work.slaveMinerLoads;
    const int income = std::max(0, work.credits - startingCredits);
    depositRuntimeRepairDepotCredits(state, key, income);
    const auto ai = state.computerAiByOwner.find(key);
    if (!state.campaignMode && ai != state.computerAiByOwner.end()) {
        ai->second.incomeEarned = static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),
            static_cast<long long>(ai->second.incomeEarned) + income));
    }
}

// Preserve the old synchronous feedback policy without moving either the
// player's economy or technology tree during explicit computer harvesting.
class RuntimeComputerMiningFeedbackScope {
public:
    explicit RuntimeComputerMiningFeedbackScope(RuntimeWindowState& state)
        : state_(state), interaction_(state.interactionStatus), simulation_(state.simulationStatus),
          command_(state.lastCommand), human_(state.humanHouseScopeActive),
          remote_(state.remoteHumanHouseScopeActive), computer_(state.computerMiningScopeActive),
          suppress_(state.runtimeSuppressPlayerAudioFeedback) {
        state.humanHouseScopeActive = state.remoteHumanHouseScopeActive = state.computerMiningScopeActive = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
    }
    ~RuntimeComputerMiningFeedbackScope() {
        state_.interactionStatus = std::move(interaction_); state_.simulationStatus = std::move(simulation_);
        state_.lastCommand = std::move(command_); state_.humanHouseScopeActive = human_;
        state_.remoteHumanHouseScopeActive = remote_; state_.computerMiningScopeActive = computer_;
        state_.runtimeSuppressPlayerAudioFeedback = suppress_;
    }
    RuntimeComputerMiningFeedbackScope(const RuntimeComputerMiningFeedbackScope&) = delete;
    RuntimeComputerMiningFeedbackScope& operator=(const RuntimeComputerMiningFeedbackScope&) = delete;
private:
    RuntimeWindowState& state_;
    std::string interaction_, simulation_;
    decltype(RuntimeWindowState::lastCommand) command_;
    bool human_, remote_, computer_, suppress_;
};
// Private synchronous fixture override; null in every product session.
static bool (*runtimeComputerMiningDiagnosticOverride)(RuntimeWindowState&) = nullptr;

bool advanceRuntimeComputerMining(RuntimeWindowState& state) {
    if (runtimeComputerMiningDiagnosticOverride) return runtimeComputerMiningDiagnosticOverride(state);
    if (!state.rulesLoaded || state.humanHouseScopeActive
        || state.skirmishMatchFinished) return false;
    std::vector<std::string> owners;
    if (state.campaignMode) {
        for (const auto& [key, house] : state.triggers.campaignHouseProduction) {
            if (!house.playerControlled && house.side >= 0
                && runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner) != key)
                owners.push_back(house.owner);
        }
    } else {
        if (!runtimeComputerAiEnabledForSession(state)
            || !state.economy.skirmishStartApplied) return false;
        ensureRuntimeComputerAiStates(state);
        for (const auto& [key, ai] : state.computerAiByOwner) {
            if (ai.initialized && !findRuntimeHumanHouseAuthority(state, ai.owner)
                && runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner) != key)
                owners.push_back(ai.owner);
        }
    }
    std::sort(owners.begin(), owners.end());
    bool changed = false;
    for (const std::string& owner : owners) {
        RuntimeEconomyState ordinaryWork;
        ordinaryWork.owner = owner;
        ordinaryWork.credits = runtimeRepairDepotCreditBalance(state, owner);
        const auto& ledger = state.computerMiningByOwner[runtimeRepairDepotCanonicalOwnerId(state, owner)];
        ordinaryWork.storedResourceUnits = ledger.storedResourceUnits;
        ordinaryWork.storedResourceValue = ledger.storedResourceValue;
        ordinaryWork.storedResourceGemUnits = ledger.storedResourceGemUnits;
        ordinaryWork.resourceTrips = ledger.resourceTrips;
        ordinaryWork.harvesterTeleports = ledger.harvesterTeleports;
        ordinaryWork.slaveMinerLoads = ledger.slaveMinerLoads;
        ordinaryWork.skirmishStartApplied = state.economy.skirmishStartApplied;
        refreshRuntimeEconomyMetricsForOwner(state, ordinaryWork);
        const int ordinaryStartingCredits = ordinaryWork.credits;
        try {
            RuntimeComputerMiningFeedbackScope feedback(state);
            auto context = makeRuntimeHouseEconomyContext(ordinaryWork, true);
            changed = advanceRuntimeHarvesterEconomy(state, context, ordinaryWork) || changed;
            changed = advanceRuntimeMobileSlaveMinerEconomy(state, context, ordinaryWork) || changed;
            changed = advanceRuntimeSlaveMinerEconomy(state, context, ordinaryWork) || changed;
        } catch (...) {
            commitRuntimeComputerMiningWork(state, owner, ordinaryWork, ordinaryStartingCredits);
            throw;
        }
        commitRuntimeComputerMiningWork(state, owner, ordinaryWork, ordinaryStartingCredits);
    }
    return changed;
}
