bool runtimeUnitUsesFlyAuxSounds(const UnitType& type) {
    const auto locomotor = toLowerAsciiLocal(trimAsciiLocal(type.locomotor));
    // Jumpjet and Rocket have different code paths. In particular, Rocket
    // already uses AuxSound1 through its existing launchSound profile.
    return locomotor.find("4a582746-9839-11d1-b709-00a024ddafd1") != std::string::npos
        || locomotor == "fly";
}

void queueRuntimeAircraftAuxSound(const RuntimeWindowState& state,
    WorldObjectInstance& object, bool takingOff, int relativeHeightLeptons = 0) {
    if (!object.alive || object.health <= 0 || (!takingOff && relativeHeightLeptons >= 300)) return;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!type || !runtimeUnitUsesFlyAuxSounds(*type)) return;
    const auto event = trimAsciiLocal(takingOff ? type->auxSound1 : type->auxSound2);
    if (event.empty()) return;
    auto& events = object.pendingAircraftAuxSounds;
    if (events.size() >= 32) events.erase(events.begin());
    events.push_back({event, state.tick, object.x, object.y, object.level, takingOff});
}
