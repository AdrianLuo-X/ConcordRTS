// Included after runtimeMultiplayerOutcomeOwners and before cooperative outcome.
// Names and colors are resolved by the local HUD, not by simulation authority.



void resetRuntimePlayerDefeatNoticesAfterLoad(RuntimeWindowState& state) noexcept {
    resetRuntimePlayerDefeatNotices(state);
    // The next qualified observation sees the fully committed world and roster.
    state.playerDefeatNotices.suppressExisting = true;
}

// Prepare only on a fully materialized detached load state. Allocation may fail
// before the noexcept commit; a later death must never become part of this baseline.




bool runtimePlayerDefeatNoticeHasParticipant(const RuntimeWindowState& state,
    const std::string& ownerKey) {
    for (std::size_t index = 0; index < state.config.playerSlots.size(); ++index) {
        const auto& player = state.config.playerSlots[index];
        if (player.enabled && !runtimeObserverSlot(player)
            && runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(index)) == ownerKey)
            return true;
    }
    return false;
}



// Direct accepted events, such as resignation, need not wait for an outcome tick.
bool enqueueRuntimePlayerDefeatNotice(RuntimeWindowState& state,
    const std::string& owner, std::uint64_t nowMs) {
    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode)) == "CAMPAIGNMISSION")
        return false;
    const std::string ownerKey = runtimeCanonicalHumanHouseOwner(owner);
    if (ownerKey.empty() || !runtimePlayerDefeatNoticeHasParticipant(state, ownerKey))
        return false;
    return appendRuntimePlayerDefeatNotice(state, ownerKey, nowMs);
}

bool enqueueRuntimePlayerDefeatNotice(RuntimeWindowState& state, const std::string& owner) {
    const std::uint64_t nowMs = runtimeUiClockNow(state);
    return enqueueRuntimePlayerDefeatNotice(state, owner, nowMs);
}



