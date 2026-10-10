// The mining state machines operate on a house economy facade. Computers use
// the same machines, but their actual credit account remains in AI/campaign
// state. Keep local production, selection feedback and technology out of this
// short-lived facade, including when a slave miner deploys or repacks.
class RuntimeComputerMiningScope {
public:
    RuntimeComputerMiningScope(RuntimeWindowState& state, const std::string& owner)
        : state_(state), owner_(runtimeRepairDepotCanonicalOwnerId(state, owner)),
          startingCredits_(runtimeRepairDepotCreditBalance(state, owner)),
          ledger_(state.computerMiningByOwner[owner_]),
          interaction_(state.interactionStatus), simulation_(state.simulationStatus),
          lastCommand_(state.lastCommand),
          economy_(std::move(state.economy)), techTree_(std::move(state.techTree)),
          techTreeLoaded_(state.techTreeLoaded),
          suppressAudio_(state.runtimeSuppressPlayerAudioFeedback) {
        state.economy = RuntimeEconomyState{};
        state.economy.owner = owner;
        state.economy.credits = startingCredits_;
        state.economy.storedResourceUnits = ledger_.storedResourceUnits;
        state.economy.storedResourceValue = ledger_.storedResourceValue;
        state.economy.storedResourceGemUnits = ledger_.storedResourceGemUnits;
        state.economy.resourceTrips = ledger_.resourceTrips;
        state.economy.harvesterTeleports = ledger_.harvesterTeleports;
        state.economy.slaveMinerLoads = ledger_.slaveMinerLoads;
        state.economy.skirmishStartApplied = economy_.skirmishStartApplied;
        state.techTree = TechTreeReport{};
        state.techTreeLoaded = false;
        state.humanHouseScopeActive = true;
        state.remoteHumanHouseScopeActive = true;
        state.computerMiningScopeActive = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
        refreshRuntimeEconomyMetricsForCurrentOwner(state);
    }
    RuntimeComputerMiningScope(const RuntimeComputerMiningScope&) = delete;
    RuntimeComputerMiningScope& operator=(const RuntimeComputerMiningScope&) = delete;
    ~RuntimeComputerMiningScope() {
        const int income = std::max(0, state_.economy.credits - startingCredits_);
        ledger_.storedResourceUnits = state_.economy.storedResourceUnits;
        ledger_.storedResourceValue = state_.economy.storedResourceValue;
        ledger_.storedResourceGemUnits = state_.economy.storedResourceGemUnits;
        ledger_.resourceTrips = state_.economy.resourceTrips;
        ledger_.harvesterTeleports = state_.economy.harvesterTeleports;
        ledger_.slaveMinerLoads = state_.economy.slaveMinerLoads;
        state_.economy = std::move(economy_);
        state_.techTree = std::move(techTree_);
        state_.techTreeLoaded = techTreeLoaded_;
        state_.interactionStatus = std::move(interaction_);
        state_.simulationStatus = std::move(simulation_);
        state_.lastCommand = std::move(lastCommand_);
        state_.humanHouseScopeActive = false;
        state_.remoteHumanHouseScopeActive = false;
        state_.computerMiningScopeActive = false;
        state_.runtimeSuppressPlayerAudioFeedback = suppressAudio_;
        // Restore the facade first: this routes the deposit to its sole real
        // account, rather than back into the temporary harvesting balance.
        depositRuntimeRepairDepotCredits(state_, owner_, income);
        const auto ai = state_.computerAiByOwner.find(owner_);
        if (!state_.campaignMode && ai != state_.computerAiByOwner.end()) {
            ai->second.incomeEarned = static_cast<int>(std::min<long long>(
                std::numeric_limits<int>::max(),
                static_cast<long long>(ai->second.incomeEarned) + income));
        }
    }
private:
    RuntimeWindowState& state_;
    std::string owner_;
    int startingCredits_;
    RuntimeComputerMiningState& ledger_;
    std::string interaction_, simulation_;
    decltype(RuntimeWindowState::lastCommand) lastCommand_;
    RuntimeEconomyState economy_;
    TechTreeReport techTree_;
    bool techTreeLoaded_, suppressAudio_;
};

bool advanceRuntimeComputerMining(RuntimeWindowState& state) {
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
        RuntimeComputerMiningScope scope(state, owner);
        changed = advanceRuntimeHarvesterEconomy(state) || changed;
        changed = advanceRuntimeMobileSlaveMinerEconomy(state) || changed;
        changed = advanceRuntimeSlaveMinerEconomy(state) || changed;
    }
    return changed;
}
