std::string serializeRuntimeWreck(const RuntimeWreckState& wreck, bool canonicalOwner = false) {
    std::ostringstream out;
    out.imbue(std::locale::classic()); out << std::setprecision(17);
    const auto& object = wreck.object;
    out << object.instanceId << '|' << runtimeSaveField(object.type) << '|'
        << runtimeSaveField(canonicalOwner ? runtimeCanonicalHumanHouseOwner(object.owner) : object.owner)
        << '|' << static_cast<int>(object.kind) << '|'
        << object.facing << '|' << object.turretFacing << '|' << object.authoredAircraft << '|'
        << wreck.startTick << '|' << wreck.sinking << '|' << wreck.jumpjet << '|' << wreck.tilt << '|'
        << wreck.x << '|' << wreck.y << '|' << wreck.startLevel << '|' << wreck.groundLevel << '|'
        << wreck.fallSpeed << '|' << wreck.gravity << '|' << wreck.driftX << '|' << wreck.driftY << '|'
        << runtimeSaveField(wreck.impactAnim) << '|' << runtimeSaveField(wreck.warhead) << '|' << object.subCell;
    return out.str();
}

void writeRuntimeWrecks(const RuntimeWindowState& state, std::ostream& output) {
    output << "\n[Wrecks]\nSchema=1\nCount=" << state.wrecks.size() << '\n';
    for (std::size_t i = 0; i < state.wrecks.size(); ++i)
        output << i << '=' << serializeRuntimeWreck(state.wrecks[i]) << '\n';
}

bool restoreRuntimeWrecks(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    std::vector<RuntimeWreckState> restored;
    if (!ini.getSection("Wrecks")) { state.wrecks.clear(); return true; }
    int schema = 0, count = 0;
    const auto fail = [&] { error = "invalid retained wreck state"; return false; };
    if (!parseRuntimeInt(ini.getValue("Wrecks", "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue("Wrecks", "Count", ""), count) || count < 0 || count > 100000) return fail();
    std::unordered_set<int> ids;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("Wrecks", std::to_string(i), ""));
        if (fields.size() != 22) return fail();
        RuntimeWreckState wreck; auto& object = wreck.object;
        int kind = -1, authored = 0, sinking = 0, jumpjet = 0, tilt = 0;
        if (!parseRuntimeInt(fields[0], object.instanceId) || object.instanceId < 0
            || fields[1].empty() || fields[2].empty() || !ids.insert(object.instanceId).second
            || !parseRuntimeInt(fields[3], kind)
            || (kind != static_cast<int>(WorldObjectKind::Unit) && kind != static_cast<int>(WorldObjectKind::Infantry))
            || !parseRuntimeInt(fields[4], object.facing) || !parseRuntimeInt(fields[5], object.turretFacing)
            || !parseRuntimeInt(fields[6], authored) || !parseRuntimeInt(fields[7], wreck.startTick)
            || wreck.startTick < 0 || wreck.startTick > state.tick
            || !parseRuntimeInt(fields[8], sinking) || !parseRuntimeInt(fields[9], jumpjet)
            || !parseRuntimeInt(fields[10], tilt) || !parseRuntimeInt(fields[21], object.subCell)) return fail();
        const auto number = [&](std::size_t index, double& value) {
            std::istringstream input(fields[index]); input.imbue(std::locale::classic());
            return static_cast<bool>(input >> value) && input.peek() == std::char_traits<char>::eof() && std::isfinite(value);
        };
        if (!number(11, wreck.x) || !number(12, wreck.y) || !number(13, wreck.startLevel)
            || !number(14, wreck.groundLevel) || !number(15, wreck.fallSpeed)
            || !number(16, wreck.gravity) || !number(17, wreck.driftX) || !number(18, wreck.driftY)
            || wreck.x < 0 || wreck.y < 0 || wreck.x >= 512 || wreck.y >= 512
            || wreck.fallSpeed <= 0 || wreck.gravity <= 0
            || std::abs(wreck.driftX) > 1 || std::abs(wreck.driftY) > 1
            || authored < 0 || authored > 1 || sinking < 0 || sinking > 1
            || jumpjet < 0 || jumpjet > 1 || tilt < 0 || tilt > 1) return fail();
        object.kind = static_cast<WorldObjectKind>(kind); object.type = fields[1]; object.owner = fields[2];
        object.authoredAircraft = authored != 0; object.alive = false; object.health = 0;
        wreck.sinking = sinking != 0; wreck.jumpjet = jumpjet != 0; wreck.tilt = tilt != 0;
        wreck.impactAnim = fields[19]; wreck.warhead = fields[20];
        const auto* source = findRuntimeObjectByIdIncludingInactive(state, object.instanceId);
        if (!source || source->alive || source->type != object.type || source->owner != object.owner) return fail();
        restored.push_back(std::move(wreck));
    }
    state.wrecks = std::move(restored); return true;
}

void hashRuntimeWrecks(std::uint64_t& hash, const RuntimeWindowState& state) {
    runtimeStableHashMixString(hash, "Wrecks/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.wrecks.size()));
    for (const auto& wreck : state.wrecks) runtimeStableHashMixString(hash, serializeRuntimeWreck(wreck, true));
}
