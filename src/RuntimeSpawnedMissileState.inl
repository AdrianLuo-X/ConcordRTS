namespace {
std::string saveSpawnedMissileRow(const SpawnedMissileState& m, bool includePresentation = true) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << m.objectId << '|' << m.parentObjectId << '|' << m.slot << '|'
        << m.targetObjectId << '|' << m.startTick << '|' << m.releaseTick << '|'
        << m.lastTick << '|' << boolText(m.released) << '|'
        << saveField(m.typeId) << '|' << saveField(m.parentType) << '|'
        << saveField(m.owner) << '|' << saveField(m.weaponId) << '|'
        << saveField(m.warheadId) << '|' << m.damage << '|' << m.damageMultiplier << '|'
        << m.x << '|' << m.y << '|' << m.z << '|'
        << m.originX << '|' << m.originY << '|' << m.originZ << '|'
        << m.targetX << '|' << m.targetY << '|' << m.targetZ << '|'
        << m.pitch << '|' << m.heading << '|' << m.speed << '|' << m.flightStage;
    if (includePresentation)
        out << '|' << m.originScreenOffsetX << '|' << m.originScreenOffsetY
            << '|' << m.originNativeFixedX << '|' << m.originNativeFixedY;
    return out.str();
}

bool loadSpawnedMissiles(const IniFile& ini,
    std::map<int, SpawnedMissileState>& missiles, std::string& error) {
    int version = 0, count = 0;
    if (!parseIntStrict(ini.getValue("SpecialSpawnedMissiles", "Version", "0"), version)
        || !parseIntStrict(ini.getValue("SpecialSpawnedMissiles", "Count", "0"), count)
        || version < 0 || version > 4 || count < 0 || count > 4096
        || (version == 0 && count != 0)) {
        error = "invalid SpecialSpawnedMissiles header";
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const auto fields = split(ini.getValue("SpecialSpawnedMissiles", std::to_string(i), ""), '|');
        SpawnedMissileState m;
        if (fields.size() != (version == 4 ? 32U : version == 3 ? 30U : version == 2 ? 28U : 27U)
            || !parseIntStrict(fields[0], m.objectId)
            || !parseIntStrict(fields[1], m.parentObjectId)
            || !parseIntStrict(fields[2], m.slot)
            || !parseIntStrict(fields[3], m.targetObjectId)
            || !parseIntStrict(fields[4], m.startTick)
            || !parseIntStrict(fields[5], m.releaseTick)
            || !parseIntStrict(fields[6], m.lastTick)
            || !parseBoolStrict(fields[7], m.released)
            || !parseIntStrict(fields[13], m.damage)) {
            error = "invalid SpecialSpawnedMissiles entry";
            return false;
        }
        m.typeId = loadField(fields[8]); m.parentType = loadField(fields[9]);
        m.owner = loadField(fields[10]); m.weaponId = loadField(fields[11]);
        m.warheadId = loadField(fields[12]);
        double* values[] = {&m.damageMultiplier, &m.x, &m.y, &m.z,
            &m.originX, &m.originY, &m.originZ, &m.targetX, &m.targetY, &m.targetZ,
            &m.pitch, &m.heading, &m.speed};
        for (int field = 14; field < 27; ++field) {
            std::istringstream input(fields[field]);
            input.imbue(std::locale::classic());
            if (!(input >> *values[field - 14]) || input.peek() != std::char_traits<char>::eof()
                || !std::isfinite(*values[field - 14]) || std::abs(*values[field - 14]) > 65536.0) {
                error = "invalid spawned missile flight coordinate";
                return false;
            }
        }
        const auto validText = [](const std::string& s) { return !s.empty() && s.size() <= 256; };
        if (version >= 2) {
            if (!parseIntStrict(fields[27], m.flightStage) || m.flightStage < 0 || m.flightStage > 2) {
                error = "invalid spawned missile flight stage";
                return false;
            }
        } else if (m.released) {
            m.flightStage = m.pitch < 0.0 ? 2 : m.pitch == 0.0 ? 1 : 0;
        }
        if (version >= 3
            && (!parseIntStrict(fields[28], m.originScreenOffsetX)
                || !parseIntStrict(fields[29], m.originScreenOffsetY)
                || m.originScreenOffsetX < -65536 || m.originScreenOffsetX > 65536
                || m.originScreenOffsetY < -65536 || m.originScreenOffsetY > 65536)) {
            error = "invalid spawned missile presentation offset";
            return false;
        }
        if (version >= 4
            && (!parseIntStrict(fields[30], m.originNativeFixedX)
                || !parseIntStrict(fields[31], m.originNativeFixedY)
                || m.originNativeFixedX < -65536 || m.originNativeFixedX > 65536
                || m.originNativeFixedY < -65536 || m.originNativeFixedY > 65536)) {
            error = "invalid spawned missile native presentation offset";
            return false;
        }
        if (m.objectId <= 0 || m.parentObjectId <= 0 || m.objectId == m.parentObjectId
            || m.slot < 0 || m.slot >= 4096 || m.targetObjectId < -1 || m.targetObjectId == 0
            || m.startTick < 0 || m.releaseTick < m.startTick || m.lastTick < 0
            || (m.released && m.lastTick < m.releaseTick)
            || m.damage <= 0 || m.damageMultiplier <= 0.0 || m.damageMultiplier > 100.0
            || m.speed < 0.0 || m.speed > 4096.0 || std::abs(m.pitch) > 1.570797
            || std::abs(m.heading) > 3.141593 || !validText(m.typeId) || !validText(m.parentType)
            || !validText(m.owner) || !validText(m.weaponId) || !validText(m.warheadId)
            || !missiles.emplace(m.objectId, m).second) {
            error = "invalid or conflicting spawned missile";
            return false;
        }
    }
    return true;
}
}

const std::map<int, SpawnedMissileState>& RuntimeSpecialMechanismState::spawnedMissiles() const {
    return spawnedMissiles_;
}
std::map<int, SpawnedMissileState>& RuntimeSpecialMechanismState::accessSpawnedMissiles() {
    return spawnedMissiles_;
}

bool RuntimeSpecialMechanismState::releaseCarrierMissile(
    int objectId, int tick, const SpecialEventSink& emit) {
    const auto child = carrierChildren_.find(objectId);
    if (child == carrierChildren_.end() || child->second.phase != CarrierChildPhase::Docked)
        return false;
    const CarrierChildState released = child->second;
    // Detach without destroying the object. Reloading a slot must not give
    // the parent lifetime or targeting authority over a missile in flight.
    notifyCarrierChildDestroyed(objectId, tick, nullptr);
    if (emit) emit({"CarrierMissileReleased", tick, released.parentObjectId,
        objectId, released.slot, {}, "independent missile flight"});
    return true;
}
