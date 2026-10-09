// Included before multiplayer prevalidation; issuing ownership comes from its frame.
std::string runtimePlayerCommunicationViewerOwner(const RuntimeWindowState& state) {
    // A remote house's simulation may temporarily borrow economy.owner. It
    // does not change the player viewing this process's private presentation.
    if (state.remoteHumanHouseScopeActive) {
        for (const auto& house : state.humanHouseAuthorities) {
            if (house.localFacade) {
                return runtimeCanonicalHumanHouseOwner(
                    house.canonicalOwner.empty() ? house.owner : house.canonicalOwner);
            }
        }
    }
    return runtimeCanonicalHumanHouseOwner(state.economy.owner);
}

bool runtimePlayerBeaconVisibleToLocal(const RuntimeWindowState& state,
    const RuntimePlayerBeacon& beacon) {
    if (beacon.untilTick >= 0 && beacon.untilTick <= state.tick) return false;
    const auto viewer = runtimePlayerCommunicationViewerOwner(state);
    const auto owner = runtimeCanonicalHumanHouseOwner(beacon.owner);
    return !viewer.empty() && !owner.empty()
        && (owner == viewer || runtimeOwnersAreAllied(state, owner, viewer));
}

std::optional<std::size_t> runtimePlayerChatSlot(const RuntimeWindowState& state,
    const std::string& owner) {
    const auto key = runtimeCanonicalHumanHouseOwner(owner);
    for (std::size_t slot = 0; slot < state.config.playerSlots.size() && slot < 8; ++slot)
        if (state.config.playerSlots[slot].enabled && state.config.playerSlots[slot].human
            && runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(slot)) == key) return slot;
    return std::nullopt;
}

std::uint8_t runtimePlayerChatSlotsMask(const RuntimeWindowState& state) {
    std::uint8_t mask = 0;
    for (std::size_t slot = 0; slot < state.config.playerSlots.size() && slot < 8; ++slot)
        if (state.config.playerSlots[slot].enabled && state.config.playerSlots[slot].human)
            mask |= static_cast<std::uint8_t>(1U << slot);
    return mask;
}

bool runtimePlayerChatRecipientsAvailable(const RuntimeWindowState& state) {
    return state.config.playerSlots.size() <= 8 && runtimePlayerChatSlot(state, state.economy.owner).has_value();
}

std::uint8_t runtimePlayerOutgoingChatMask(const RuntimeWindowState& state) {
    if (!state.playerCommunication.chatRecipientsExplicit || !runtimePlayerChatRecipientsAvailable(state)) return 0;
    const auto own = *runtimePlayerChatSlot(state, state.economy.owner);
    return static_cast<std::uint8_t>((state.playerCommunication.chatRecipients & runtimePlayerChatSlotsMask(state))
        | (1U << own));
}

std::vector<std::string> runtimePlayerDiplomacyOwners(const RuntimeWindowState& state) {
    std::set<std::string> owners;
    for (const auto& object : state.world.getObjects()) {
        const auto owner = runtimeCanonicalHumanHouseOwner(object.owner);
        if (object.alive && !owner.empty() && !runtimeOwnerIsNeutralForAutoTarget(owner))
            owners.insert(owner);
    }
    owners.erase(runtimeCanonicalHumanHouseOwner(state.economy.owner));
    return {owners.begin(), owners.end()};
}

bool runtimePlayerDiplomacyAllowed(const RuntimeWindowState& state) {
    return !state.campaignMode && state.skirmishModeAlliesAllowed
        && state.skirmishModeAllyChangeAllowed
        && toUpperAsciiLocal(state.config.gameMode) != "FREEFORALL"
        && toUpperAsciiLocal(state.config.gameMode) != "COOPERATIVE";
}

bool validateRuntimePlayerCommunication(const RuntimeWindowState& state,
    const std::string& sender, const RuntimeMultiplayerGameplayPlayerCommunicationCommand& command,
    std::string& error) {
    if ((command.kind == RuntimePlayerCommunicationKind::ChatAll
            || command.kind == RuntimePlayerCommunicationKind::ChatAllies) && command.value != 0) {
        const auto source = runtimePlayerChatSlot(state, sender);
        if (state.config.playerSlots.size() > 8 || !source
            || !(command.value & (1U << *source))
            || (command.value & ~runtimePlayerChatSlotsMask(state)) != 0) {
            error = "invalid authenticated chat recipient slots"; return false;
        }
    }
    if (command.kind == RuntimePlayerCommunicationKind::Alliance) {
        const auto target = runtimeCanonicalHumanHouseOwner(command.text);
        const bool exists = std::any_of(state.world.getObjects().begin(), state.world.getObjects().end(),
            [&](const auto& object) { return object.alive
                && runtimeCanonicalHumanHouseOwner(object.owner) == target; });
        if (!runtimePlayerDiplomacyAllowed(state)
            || (state.skirmishModeMustAlly && command.value == 0)
            || target != command.text || !exists
            || target == runtimeCanonicalHumanHouseOwner(sender)
            || runtimeOwnerIsNeutralForAutoTarget(target)) {
            error = "this match or target does not allow alliance changes"; return false;
        }
    }
    if ((command.kind == RuntimePlayerCommunicationKind::PlaceBeacon
            || command.kind == RuntimePlayerCommunicationKind::DeleteBeacon
            || command.kind == RuntimePlayerCommunicationKind::BeaconText)
        && !findRuntimeTerrainCellAt(state, {command.cell.x, command.cell.y})) {
        error = "beacon cell is outside the map"; return false;
    }
    error.clear(); return true;
}

void runtimePlayerAddChatLine(RuntimeWindowState& state, const std::string& owner,
    const std::string& text, bool allies = false) {
    auto& lines = state.playerCommunication.lines;
    lines.push_back({owner, text, allies, state.tick + 15 * std::max(1, state.config.fixedTickHz)});
    while (lines.size() > 8) lines.pop_front();
    state.repaintRequested = true;
}

bool applyRuntimePlayerCommunication(RuntimeWindowState& state, const std::string& sender,
    const RuntimeMultiplayerGameplayPlayerCommunicationCommand& command) {
    const auto owner = runtimeCanonicalHumanHouseOwner(sender);
    auto& presentation = state.playerCommunication;
    const int tickHz = std::max(1, state.config.fixedTickHz);
    if (command.kind == RuntimePlayerCommunicationKind::Alliance) {
        // Each house controls only its own offer. Two offers are required to ally.
        state.playerCommands.alliances[{owner, command.text}] = command.value != 0;
        // Human opponents accept by offering back; computer houses accept the offer.
        const bool humanTarget = std::any_of(state.humanHouseAuthorities.begin(), state.humanHouseAuthorities.end(),
            [&](const auto& house) { return house.canonicalOwner == command.text; });
        if (!humanTarget) state.playerCommands.alliances[{command.text, owner}] = command.value != 0;
        updateRuntimeVisionState(state);
        runtimePlayerAddChatLine(state, owner, command.value
            ? "向 " + command.text + " 提出结盟" : "取消与 " + command.text + " 的盟约");
        return true;
    }
    const auto viewer = runtimePlayerCommunicationViewerOwner(state);
    const bool visibleToLocal = !owner.empty() && !viewer.empty()
        && (owner == viewer || runtimeOwnersAreAllied(state, owner, viewer));
    switch (command.kind) {
    case RuntimePlayerCommunicationKind::ChatAll:
    case RuntimePlayerCommunicationKind::ChatAllies:
        if ((command.value == 0 || (!state.multiplayerLocalSpectator
                && runtimePlayerChatSlot(state, state.economy.owner)
                && (command.value & (1U << *runtimePlayerChatSlot(state, state.economy.owner)))))
            && (command.kind == RuntimePlayerCommunicationKind::ChatAll || visibleToLocal))
            runtimePlayerAddChatLine(state, owner, command.text,
                command.kind == RuntimePlayerCommunicationKind::ChatAllies);
        return true;
    case RuntimePlayerCommunicationKind::PlaceBeacon:
        if (visibleToLocal) {
            std::erase_if(presentation.beacons, [&](const auto& beacon) {
                return (beacon.untilTick >= 0 && beacon.untilTick <= state.tick)
                    || (beacon.owner == owner && beacon.cell == command.cell);
            });
            // Native has three slots and refuses overflow without evicting an
            // older marker. This presentation-only no-op still consumes the
            // authenticated command identically for all local viewers.
            if (std::count_if(presentation.beacons.begin(), presentation.beacons.end(),
                    [&](const auto& beacon) { return beacon.owner == owner; }) >= 3) return true;
            presentation.beacons.push_back({owner, command.cell, -1, {},
                terrainLevelAtRuntimeCell(state, command.cell.x, command.cell.y)});
            state.playerLastRadarCell = command.cell;
            runtimePlayerAddChatLine(state, owner, "信标 (" + std::to_string(command.cell.x)
                + ", " + std::to_string(command.cell.y) + ")", true);
        }
        return true;
    case RuntimePlayerCommunicationKind::BeaconText:
        if (visibleToLocal) for (auto& beacon:presentation.beacons) {
            // Owner comes from the authenticated frame, so teammates cannot
            // edit one another's markers. Expired/missing markers stay absent.
            if (beacon.owner==owner && beacon.cell==command.cell
                && runtimePlayerBeaconVisibleToLocal(state, beacon)) {
                beacon.text=command.text;
                runtimePlayerAddChatLine(state,owner,"信标："+command.text,true);
                break;
            }
        }
        return true;
    case RuntimePlayerCommunicationKind::DeleteBeacon:
        std::erase_if(presentation.beacons, [&](const auto& beacon) {
            return beacon.owner == owner && beacon.cell == command.cell;
        });
        if (owner==viewer
            && presentation.selectedBeacon==command.cell) presentation.selectedBeacon.reset();
        state.repaintRequested = true;
        return true;
    case RuntimePlayerCommunicationKind::Cheer:
    case RuntimePlayerCommunicationKind::Taunt: {
        const auto rateKey = std::make_pair(owner, command.kind);
        if (presentation.nextFeedbackTick[rateKey] > state.tick) return true;
        presentation.nextFeedbackTick[rateKey] = state.tick + 10 * tickHz;
        if (command.kind == RuntimePlayerCommunicationKind::Cheer) {
            for (const auto& object : state.world.getObjects())
                if (object.alive && object.kind == WorldObjectKind::Infantry
                    && runtimeCanonicalHumanHouseOwner(object.owner) == owner
                    && !runtimeGateObjectIsLimbo(object))
                    presentation.cheerUntilTick[object.instanceId] = state.tick + 2 * tickHz;
            if (visibleToLocal && !state.runtimeSuppressPlayerAudioFeedback) emitRuntimeSound(state, "Cheer");
        } else {
            if (!presentation.tauntsEnabled) return true;
            runtimePlayerAddChatLine(state, owner, "叫嚣 " + std::to_string(command.value));
            const auto country = toUpperAsciiLocal(runtimeCountryForOwner(state, sender));
            const std::map<std::string, std::string> prefixes{
                {"AMERICANS", "am"}, {"BRITISH", "br"}, {"FRENCH", "fr"},
                {"GERMANS", "ge"}, {"ALLIANCE", "ko"}, {"RUSSIANS", "ru"},
                {"ARABS", "ir"}, {"CONFEDERATION", "cu"}, {"AFRICANS", "li"},
                {"YURICOUNTRY", "yu"}, {"YURI", "yu"}};
            const auto prefix = prefixes.find(country);
            if (prefix != prefixes.end() && state.runtimeSoundsEnabled
                && !state.runtimeSuppressPlayerAudioFeedback) {
                const auto filename = "tau" + prefix->second + "0" + std::to_string(command.value) + ".wav";
                const std::filesystem::path root(state.config.resourceRootPath);
                for (const auto& path : {root / "GameMD" / "Taunts" / filename, root / "Taunts" / filename}) {
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(path, ec)) {
                        playRuntimeSfxOnDedicatedChannel(state, pathString(path), state.config.voiceVolume);
                        break;
                    }
                }
            }
        }
        state.repaintRequested = true;
        return true;
    }
    default: return false;
    }
}
