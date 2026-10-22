// Local input routing is gameplay infrastructure. It must not depend on an
// optional recording/viewing session being present.
bool runtimeUsesGameplayIntentRouter(const RuntimeWindowState& state) {
    return state.multiplayerBridge != nullptr
        || (!state.campaignMode && runtimeLocalObserver(state.config))
        || state.offlineGameplayIntentRouting;
}

bool dispatchRuntimeOfflineGameplayCommand(RuntimeWindowState& state,
    const RuntimeMultiplayerGameplayCommand& command, std::string& error) {
    const std::string owner=state.economy.owner;
    if(state.config.multiplayerActive
        && !prevalidateRuntimeMultiplayerGameplayCommand(state,owner,command,error)) return false;
    const bool applied=applyRuntimeMultiplayerGameplayCommand(state,owner,command);
    if(applied)error.clear();else error="当前状态无法执行此操作";
    RuntimeKeyboardParentDiagnostics::command("offline-apply", state.tick, owner, command, applied, error);
    return applied;
}

void initializeRuntimeOfflineGameplayRouting(RuntimeWindowState& state) {
    state.offlineGameplayIntentRouting=!state.config.multiplayerActive && !state.multiplayerBridge;
    if(!state.offlineGameplayIntentRouting)return;
    state.multiplayerIntentRouter.setCaptureCallback([&state](const auto& command,std::string& error) {
        return dispatchRuntimeOfflineGameplayCommand(state,command,error);
    });
}
