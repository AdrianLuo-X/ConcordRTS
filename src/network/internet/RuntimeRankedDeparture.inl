void reportRuntimeRankedDeparture(RuntimeWindowState& state) {
    if (state.multiplayerBridge == nullptr || state.multiplayerLocalSpectator) return;
    bool submitted = false;
    std::string error;
    const bool success = state.skirmishMatchFinished && !state.resultPresentation.resigned
        ? state.multiplayerBridge->reportRankedMatchResult(state.skirmishVictory, submitted, error)
        : state.multiplayerBridge->reportRankedMatchAbandon(submitted, error);
    if (!success) std::cerr << "Runtime ranked departure: " << error << '\n';
    else if (submitted) std::cout << "Runtime ranked departure finalized.\n";
}
