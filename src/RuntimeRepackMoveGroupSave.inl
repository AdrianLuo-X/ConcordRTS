// A folding construction yard needs the same arrival intent as the vehicles
// that left immediately. This is deliberately separate from path work/state.
bool runtimeRepackMoveGroupActor(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    if (!object.alive || move.kind != RuntimeBehaviorKind::Move || move.autoAcquired
        || move.targetObjectId >= 0 || move.pendingAllowedOccupiedObjectId >= 0
        || move.forceFire || move.attackGround || move.attackBridge) return false;
    if (runtimeStructureIsRepacking(object)) {
        const auto* yard = findRuntimeBuildingTypeById(state.rules, object.type);
        if (!yard || !yard->constructionYard) return false;
        WorldObjectInstance mobile = object;
        mobile.kind = WorldObjectKind::Unit;
        mobile.type = yard->undeploysInto;
        const auto* unit = findRuntimeUnitType(state.rules, mobile);
        return unit && runtimeUnitTypeIsMcv(state, *unit);
    }
    const auto* unit = object.kind == WorldObjectKind::Unit
        ? findRuntimeUnitType(state.rules, object) : nullptr;
    return unit && runtimeUnitTypeIsMcv(state, *unit);
}

bool runtimeRepackMoveGroupMember(const RuntimeWindowState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& member) {
    if (!member.alive || member.owner != actor.owner) return false;
    if (member.kind == WorldObjectKind::Unit || member.kind == WorldObjectKind::Infantry)
        return true;
    if (!runtimeStructureIsRepacking(member)) return false;
    const auto* yard = findRuntimeBuildingTypeById(state.rules, member.type);
    return yard && yard->constructionYard;
}

std::optional<RuntimeRepackMoveGroupIntent> canonicalRuntimeRepackMoveGroup(
    const RuntimeWindowState& state, int actorId, const RuntimeObjectBehavior& move) {
    if (!move.repackMoveGroup) return std::nullopt;
    const auto* actor = findObjectById(state, actorId);
    if (!actor || !runtimeRepackMoveGroupActor(state, *actor, move))
        return std::nullopt;
    // Keep raw identities, including dead, missing or temporarily controlled
    // peers. Save/hash must retain the same future membership as the live
    // order if a controlled peer later returns to its original owner.
    RuntimeRepackMoveGroupIntent intent = *move.repackMoveGroup;
    std::sort(intent.members.begin(), intent.members.end());
    intent.members.erase(std::unique(intent.members.begin(), intent.members.end()), intent.members.end());
    return intent;
}

void refreshRuntimeRepackMoveGroup(RuntimeWindowState& state,
    const WorldObjectInstance& actor, RuntimeObjectBehavior& move) {
    if (!move.repackMoveGroup) return;
    const auto intent = canonicalRuntimeRepackMoveGroup(state, actor.instanceId, move);
    if (!intent) { move.repackMoveGroup.reset(); return; }
    move.pendingHasSharedGoal = true;
    move.pendingSharedGoal = intent->center;
    move.pendingIgnoredObjectIds.clear();
    for (int id : intent->members) {
        const auto* member = findObjectById(state, id);
        // Never ignore a still-folding foundation. Keep its ID in the intent
        // so the eventual conversion can substitute the real vehicle ID.
        if (member && runtimeRepackMoveGroupMember(state, actor, *member)
            && member->kind != WorldObjectKind::Structure
            && runtimeObjectCanMove(state, *member)) move.pendingIgnoredObjectIds.insert(id);
    }
}

void remapRuntimeRepackMoveGroupMember(RuntimeWindowState& state, int oldId, int newId) {
    for (auto& [actorId, move] : state.behaviors) {
        if (!move.repackMoveGroup) continue;
        for (int& id : move.repackMoveGroup->members) if (id == oldId) id = newId;
        auto& members = move.repackMoveGroup->members;
        std::sort(members.begin(), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        if (const auto* actor = findObjectById(state, actorId))
            refreshRuntimeRepackMoveGroup(state, *actor, move);
    }
}

void writeRuntimeRepackMoveGroups(const RuntimeWindowState& state, std::ostream& output) {
    std::map<int, RuntimeRepackMoveGroupIntent> records;
    for (const auto& [id, move] : state.behaviors)
        if (auto intent = canonicalRuntimeRepackMoveGroup(state, id, move)) records.emplace(id, std::move(*intent));
    output << "\n[RepackMoveGroups]\nSchema=1\nCount=" << records.size() << '\n';
    int index = 0;
    for (const auto& [id, intent] : records) {
        output << index++ << '=' << id << '|' << intent.center.x << '|' << intent.center.y
            << '|' << intent.members.size();
        for (int member : intent.members) output << '|' << member;
        output << '\n';
    }
}

bool loadRuntimeRepackMoveGroups(RuntimeWindowState& state, const IniFile& ini,
    std::string& error, bool preserveExactBehavior = false) {
    constexpr const char* section = "RepackMoveGroups";
    const auto* entries = ini.getSection(section);
    if (!entries) return true; // Legacy saves retain their target-only order.
    const auto fail = [&]() { error = "invalid repack move group intent"; return false; };
    int schema = 0, count = 0;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size()
        || entries->size() != static_cast<std::size_t>(count) + 2) return fail();
    std::unordered_set<std::string> keys;
    for (const auto& [key, value] : *entries)
        if (!keys.insert(toUpperAsciiLocal(trimAsciiLocal(key))).second) return fail();
    std::map<int, RuntimeRepackMoveGroupIntent> records;
    int previousActor = -1;
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(index), ""));
        int actorId = -1, memberCount = 0;
        RuntimeRepackMoveGroupIntent intent;
        if (fields.size() < 5 || !parseRuntimeInt(fields[0], actorId) || actorId <= previousActor
            || !parseRuntimeInt(fields[1], intent.center.x) || !parseRuntimeInt(fields[2], intent.center.y)
            || intent.center.x < 0 || intent.center.y < 0 || intent.center.x >= 512 || intent.center.y >= 512
            || !parseRuntimeInt(fields[3], memberCount) || memberCount < 1
            || memberCount > 100000
            || fields.size() != static_cast<std::size_t>(memberCount) + 4) return fail();
        previousActor = actorId;
        const auto* actor = findObjectById(state, actorId);
        const auto behavior = state.behaviors.find(actorId);
        if (!actor || behavior == state.behaviors.end()
            || !runtimeRepackMoveGroupActor(state, *actor, behavior->second)) return fail();
        int previousMember = -1;
        for (int index = 0; index < memberCount; ++index) {
            int id = -1;
            if (!parseRuntimeInt(fields[4 + index], id) || id <= 0 || id <= previousMember || id >= INT_MAX) return fail();
            previousMember = id;
            intent.members.push_back(id);
        }
        if (!std::binary_search(intent.members.begin(), intent.members.end(), actorId)) return fail();
        // Exact snapshots already own their pending request, path and timing.
        // Attach only the independent intent after that behavior is restored.
        if (preserveExactBehavior && (!behavior->second.pendingHasSharedGoal
            || behavior->second.pendingSharedGoal != intent.center)) return fail();
        records.emplace(actorId, std::move(intent));
    }
    // Entire section is checked before touching even one destination behavior.
    for (auto& [id, intent] : records) {
        auto& move = state.behaviors.at(id);
        move.repackMoveGroup = std::move(intent);
        if (!preserveExactBehavior) refreshRuntimeRepackMoveGroup(state, *findObjectById(state, id), move);
    }
    return true;
}
