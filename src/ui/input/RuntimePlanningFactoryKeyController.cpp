#include "ui/input/RuntimePlanningFactoryKeyController.h"
#include "ui/input/RuntimePlayerCommands.h"

bool handleRuntimePlanningFactoryKey(const RuntimePlanningFactoryKeyInput& input,
    RuntimePlayerLocalCommands& local, const std::vector<int>& selection,
    RuntimePlanningFactoryKeyHost& host
) {
    const auto matches = [&](const std::string& action, int fallback) {
        return input.code == host.binding(action, fallback);
    };
    if (matches("PlanningMode", 'Z')) {
        if (!input.repeated) host.dispatchWaypoint(
            local.planning ? RuntimePlayerWaypointOperation::Commit : RuntimePlayerWaypointOperation::Begin, 0);
        return true;
    }
    if (matches("SetPrimaryFactory", 'U' | runtimePlayerControlKey)) {
        if (!input.repeated && selection.size() == 1)
            host.dispatchFactory({selection.front(), std::nullopt});
        return true;
    }
    if (matches("SetRallyPoint", 'J')) {
        local.rallyTargeting = selection.size() == 1;
        local.followTargeting = false;
        host.setStatus("choose a factory rally point");
        return true;
    }
    return false;
}

bool dispatchRuntimePlayerFactory(RuntimePlayerFactoryRequest request,
    RuntimePlayerFactoryHost& host
) {
    bool accepted = false;
    std::string error;
    if (host.usesIntentRouter()) {
        host.refreshAuthority();
        accepted = request.cell ? host.submitRally(request.objectId, *request.cell, error)
            : host.submitPrimary(request.objectId, error);
    } else accepted = request.cell ? host.applyRally(request.objectId, *request.cell)
        : host.applyPrimary(request.objectId);
    host.setStatus(accepted ? (request.cell ? "factory rally point set" : "primary factory selected")
        : "select an owned production factory");
    if (accepted && !request.cell) host.primarySelectedSound();
    host.requestRepaint();
    return accepted;
}