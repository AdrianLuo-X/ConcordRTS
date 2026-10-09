bool issueRuntimeTeamLoad(RuntimeWindowState& state, RuntimeTeamMissionState& mission) {
    const auto members = runtimeLiveTeamObjectIds(state, mission);
    std::vector<int> transports;
    for (int id : members) {
        const auto* object = findObjectById(state, id);
        if (object && runtimeTransportProfileForObject(state, *object).canTransport) {
            transports.push_back(id);
            mission.transportReturnCells.try_emplace(id, runtimeCellPointForObject(*object));
        }
    }
    if (transports.empty()) return false;
    const auto savedSelection = state.selectedObjectIds;
    const bool savedAudio = state.runtimeSuppressPlayerAudioFeedback;
    state.runtimeSuppressPlayerAudioFeedback = true;
    bool queued = false;
    for (int passengerId : members) {
        if (std::find(transports.begin(), transports.end(), passengerId) != transports.end()) continue;
        if (state.pendingTransportTargets.contains(passengerId)) { queued = true; continue; }
        for (int transportId : transports) {
            const auto* transport = findObjectById(state, transportId);
            if (transport == nullptr) continue;
            applySelection(state, {passengerId}, false, false);
            (void)queueRuntimeTransportTarget(state, *transport);
            const auto pending = state.pendingTransportTargets.find(passengerId);
            if (pending != state.pendingTransportTargets.end() && pending->second == transportId) {
                queued = true; ++state.triggers.ordersIssued; break;
            }
        }
    }
    state.selectedObjectIds = savedSelection;
    state.runtimeSuppressPlayerAudioFeedback = savedAudio;
    return queued;
}

bool unloadRuntimeTeamTransports(RuntimeWindowState& state,
    RuntimeTeamMissionState& mission, int argument) {
    const auto members = runtimeLiveTeamObjectIds(state, mission);
    std::vector<int> transports;
    bool cargoRemaining = false;
    for (int id : members) {
        auto* object = findMutableObjectById(state, id);
        if (object == nullptr || !runtimeTransportProfileForObject(state, *object).canTransport) continue;
        transports.push_back(id);
        if (!object->transportPassengerIds.empty()) (void)unloadRuntimeTransport(state, *object);
        object = findMutableObjectById(state, id);
        cargoRemaining = cargoRemaining || (object && !object->transportPassengerIds.empty());
    }
    if (cargoRemaining) return false;

    const auto team = state.triggers.teams.find(mission.teamTypeId);
    const bool returnTransports = team != state.triggers.teams.end()
        && team->second.transportsReturnOnUnload;
    // Retail action8: 0 keeps both, 1 releases passengers, 2 releases
    // transports, 3 releases both. A released transport may be sent home.
    const auto allMembers = mission.objectIds;
    for (int id : allMembers) {
        const bool transport = std::find(transports.begin(), transports.end(), id) != transports.end();
        if (transport ? (returnTransports || argument == 2 || argument == 3)
                      : (argument == 1 || argument == 3)) {
            const auto returnCell = mission.transportReturnCells.find(id);
            const auto destination = returnCell == mission.transportReturnCells.end()
                ? std::optional<RuntimeCellPoint>{} : std::optional<RuntimeCellPoint>{returnCell->second};
            detachRuntimeObjectFromTeamMembership(state, id);
            if (auto* object = findMutableObjectById(state, id)) object->mission = "Guard";
            if (transport && returnTransports && destination)
                (void)issueRuntimeTeamMove(state, {id}, *destination);
        }
    }
    return true;
}
