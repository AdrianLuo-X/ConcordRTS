// Included after runtimeMultiplayerOutcomeOwners and before cooperative outcome.
// Names and colors are resolved by the local HUD, not by simulation authority.

void resetRuntimePlayerDefeatNotices(RuntimeWindowState& state) noexcept {
    state.playerDefeatNotices.lines.clear();
    state.playerDefeatNotices.announcedOwners.clear();
    state.playerDefeatNotices.suppressExisting = false;
}

void resetRuntimePlayerDefeatNoticesAfterLoad(RuntimeWindowState& state) noexcept {
    resetRuntimePlayerDefeatNotices(state);
    // The next qualified observation sees the fully committed world and roster.
    state.playerDefeatNotices.suppressExisting = true;
}

// Prepare only on a fully materialized detached load state. Allocation may fail
// before the noexcept commit; a later death must never become part of this baseline.
void prepareRuntimePlayerDefeatNoticeLoadBaseline(RuntimeWindowState& state) {
    resetRuntimePlayerDefeatNotices(state);
    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode)) == "CAMPAIGNMISSION"
        || !state.economy.skirmishStartApplied) return;

    const bool authoritativeOutcome = state.config.multiplayerActive
        || runtimeLocalObserver(state.config) || state.cooperativeSession.active();
    if (!authoritativeOutcome && state.skirmishCountryByOwner.empty()) return;

    for (std::size_t index = 0; index < state.config.playerSlots.size(); ++index) {
        const auto& player = state.config.playerSlots[index];
        if (!player.enabled || runtimeObserverSlot(player)) continue;
        const std::string owner = runtimeSkirmishOwnerForSlot(index);
        const std::string ownerKey = runtimeCanonicalHumanHouseOwner(owner);
        const bool defeated = authoritativeOutcome
            ? state.multiplayerDefeatedOwners.contains(ownerKey)
            : runtimeSkirmishOwnerDefeated(state, owner);
        if (defeated) state.playerDefeatNotices.announcedOwners.insert(ownerKey);
    }
}

bool expireRuntimePlayerDefeatNotices(RuntimeWindowState& state, std::uint64_t nowMs) {
    auto& lines = state.playerDefeatNotices.lines;
    const auto previousSize = lines.size();
    std::erase_if(lines, [nowMs](const RuntimePlayerDefeatNotice& notice) {
        return nowMs >= notice.untilMs;
    });
    const bool changed = lines.size() != previousSize;
    if (changed) state.repaintRequested = true;
    return changed;
}

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

bool appendRuntimePlayerDefeatNotice(RuntimeWindowState& state,
    const std::string& ownerKey, std::uint64_t nowMs) {
    auto& notices = state.playerDefeatNotices;
    const auto inserted = notices.announcedOwners.insert(ownerKey);
    if (!inserted.second) return false;
    const std::uint64_t untilMs = runtimePlayerDefeatNoticeDeadline(nowMs);
    try {
        notices.lines.push_back({ownerKey, untilMs});
    } catch (...) {
        notices.announcedOwners.erase(inserted.first);
        throw;
    }
    state.repaintRequested = true;
    return true;
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
bool observeRuntimePlayerDefeatNotices(RuntimeWindowState& state,
    std::uint64_t nowMs) {
    bool changed = expireRuntimePlayerDefeatNotices(state, nowMs);
    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode)) == "CAMPAIGNMISSION"
        || !state.economy.skirmishStartApplied || state.tick < 5 || state.tick % 10 != 0)
        return changed;

    const bool authoritativeOutcome = state.config.multiplayerActive
        || runtimeLocalObserver(state.config) || state.cooperativeSession.active();
    if (!authoritativeOutcome && state.skirmishCountryByOwner.empty()) return changed;

    auto& notices = state.playerDefeatNotices;
    const bool suppressExisting = notices.suppressExisting;
    bool hasParticipant = false;
    // Roster order is stable in both builds and naturally bounds notices by the
    // actual combatants, without a hard-coded eight/twelve-player capacity.
    for (std::size_t index = 0; index < state.config.playerSlots.size(); ++index) {
        const auto& player = state.config.playerSlots[index];
        if (!player.enabled || runtimeObserverSlot(player)) continue;
        hasParticipant = true;
        const std::string owner = runtimeSkirmishOwnerForSlot(index);
        const std::string ownerKey = runtimeCanonicalHumanHouseOwner(owner);
        if (notices.announcedOwners.contains(ownerKey)) continue;
        const bool defeated = authoritativeOutcome
            ? state.multiplayerDefeatedOwners.contains(ownerKey)
            : runtimeSkirmishOwnerDefeated(state, owner);
        if (!defeated) continue;
        if (suppressExisting) notices.announcedOwners.insert(ownerKey);
        else changed = appendRuntimePlayerDefeatNotice(state, ownerKey, nowMs) || changed;
    }
    if (hasParticipant) notices.suppressExisting = false;
    // Terminal outcome ticks must still announce their newly defeated players.
    return changed;
}

bool observeRuntimePlayerDefeatNotices(RuntimeWindowState& state) {
    const std::uint64_t nowMs = runtimeUiClockNow(state);
    return observeRuntimePlayerDefeatNotices(state, nowMs);
}
