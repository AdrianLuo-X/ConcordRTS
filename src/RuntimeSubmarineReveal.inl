bool runtimeObjectUsesSubmarineReveal(const RuntimeWindowState& state,const WorldObjectInstance& object) {
    const auto* unit=findRuntimeUnitType(state.rules,object);
    return unit && unit->naval && unit->underwater && !unit->organic && runtimeObjectCanCloak(state,object);
}

bool runtimeRevealSubmarineForActivity(RuntimeWindowState& state,WorldObjectInstance& object,const std::string& reason) {
    if(!runtimeObjectUsesSubmarineReveal(state,object))return false;
    bool changed=runtimeDecloakObject(state,object,reason);
    object.submergeCooldownTicks=std::max(object.submergeCooldownTicks,runtimeObjectCloakTransitionTicks(state,object));
    changed=runtimeSetObjectSubmerged(state,object,false,reason)||changed;
    return changed;
}
