void initializeRuntimeKirovClearanceMotion(RuntimeWindowState&, const WorldObjectInstance&,
    RuntimeJumpjetFlight&);

std::string serializeRuntimeActiveJumpjetFlights(const RuntimeWindowState& state,
    bool includePresentation = true) {
    RuntimeJumpjetFlights active;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || !object.airborne || !runtimeJumpjetProfile(state, object)) continue;
        const auto found = state.jumpjetFlights.find(object.instanceId);
        if (found != state.jumpjetFlights.end()) active.emplace(found->first, found->second);
    }
    return serializeRuntimeJumpjetFlights(active, includePresentation);
}

void writeRuntimeJumpjetFlights(const RuntimeWindowState& state, std::ostream& output) {
    const auto data = serializeRuntimeActiveJumpjetFlights(state);
    output << "[JumpjetFlights]\nSchema=5"
        << "\nDataHex=" << iniHexEncode(data) << "\n\n";
}

bool readRuntimeJumpjetFlights(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    RuntimeJumpjetFlights staged;
    if (ini.getSection("JumpjetFlights")) {
        std::string payload;
        const auto schema=ini.getValue("JumpjetFlights", "Schema", "");
        if ((schema != "1" && schema != "2" && schema != "3" && schema != "4" && schema != "5")
            || !iniHexDecode(ini.getValue("JumpjetFlights", "DataHex", ""), payload, 16U*1024U*1024U)
            || !deserializeRuntimeJumpjetFlights(payload, staged)) {
            error = "invalid Jumpjet flight save section"; return false;
        }
        if (!payload.starts_with("JumpjetFlights " + schema + " ")) {
            error = "Jumpjet flight schema does not match payload"; return false;
        }
        for (const auto& [id, flight] : staged) {
            const auto* object = findObjectById(state, id);
            const auto* unit = object ? findRuntimeUnitType(state.rules, *object) : nullptr;
            if (!object || !object->alive || !unit || !runtimeUnitUsesJumpjetMovement(*unit)
                || flight.lastTick > state.tick || flight.verticalTick > state.tick
                || flight.clearanceTick > state.tick
                || ((flight.active || flight.stopped)
                    && (!object->airborne || object->x != flight.toX || object->y != flight.toY))) {
                error = "Jumpjet flight does not match saved world"; return false;
            }
        }
    }
    state.jumpjetFlights = std::move(staged);
    for (auto& object : state.world.accessObjects()) {
        const auto flight = state.jumpjetFlights.find(object.instanceId);
        // Restore the physical fractional pose before resolving a legacy
        // roof lift; the core object can still be at its committed cell.
        applyRuntimeJumpjetPose(state, object);
        if (flight != state.jumpjetFlights.end() && flight->second.clearanceTick < 0) {
            initializeRuntimeKirovClearanceMotion(state, object, flight->second);
            applyRuntimeJumpjetPose(state, object);
        }
    }
    return true;
}
