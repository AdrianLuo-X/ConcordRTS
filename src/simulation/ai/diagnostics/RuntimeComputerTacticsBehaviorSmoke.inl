// Real movement and damage through the same flat stock-rule world.
const auto freezeHouseOrders = [&](RuntimeWindowState& s) {
    s.computerAiByOwner.at(ownerKey).nextThinkTick = 10000000;
    s.computerAiByOwner.at(ownerKey).nextAttackTick = 10000000;
    s.visionEnabled = false;
    for (auto& object : s.world.accessObjects()) object.weaponCooldownTicksRemaining = 100000;
};
const auto makeTeam = [&](RuntimeWindowState& s, const std::vector<int>& ids, bool guard, bool suicide,
    bool avoid, int targetCell) {
    RuntimeTeamType type; type.id = "BehaviorProbe"; type.name = type.id; type.house = owner;
    type.scriptId = "BehaviorScript"; type.taskForceId = "BehaviorForce";
    type.aggressive = true; type.guardSlower = guard; type.suicide = suicide; type.avoidThreats = avoid;
    RuntimeTaskForce force; force.id = type.taskForceId;
    for (int id : ids) force.members.push_back({1, findObjectById(s, id)->type});
    RuntimeScriptType script; script.id = type.scriptId; script.steps = {{4, targetCell}, {5, 100000}};
    s.triggers.taskForces[force.id] = force; s.triggers.teams[type.id] = type; s.triggers.scripts[script.id] = script;
    const int missionId = s.triggers.nextTeamMissionInstanceId;
    issueRuntimeTriggerTeamOrder(s, type, ids);
    return missionId;
};
std::array<int, 2> threatExposure{};
for (bool avoid : {false, true}) {
    auto storage = makeWorld("Americans", "Easy", {}); auto& s = *storage;
    RuntimeSnapshotActiveStateScope scope(s);
    const auto* gun = addRuntimeObjectInstanceForOwner(s, human, RuntimeBuildKind::Building, "TESLA", {20, 28});
    if (!gun) return fail("threat-route defense unavailable");
    const int gunId = gun->instanceId;
    if (!addRuntimeObjectInstanceForOwner(s, human, RuntimeBuildKind::Building, "NANRCT", {20, 3}))
        return fail("threat-route power unavailable");
    const auto* unit = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Unit, "MTNK", {8, 32});
    if (!unit) return fail("threat-route tank unavailable");
    const int unitId = unit->instanceId;
    initializeRuntimeNavigation(s); freezeHouseOrders(s);
    for (int frame = 0; frame < 180; ++frame) tick(s);
    if (!runtimeWeaponProfileForObject(s, *findObjectById(s, gunId), findObjectById(s, unitId)).weapon)
        return fail("threat-route defense has no operational weapon");
    (void)makeTeam(s, {unitId}, false, false, avoid, 32030);
    bool arrived = false;
    for (int frame = 0; frame < 4000 && !arrived; ++frame) {
        tick(s);
        const auto* actor = findObjectById(s, unitId); const auto* enemy = findObjectById(s, gunId);
        if (!actor || !enemy) return fail("threat-route actor lost");
        const auto profile = runtimeWeaponProfileForObject(s, *enemy, actor);
        if (profile.weapon && runtimeCellDistanceToObjectFootprint(s, runtimeCellPointForObject(*actor), *enemy)
            <= runtimeEffectiveWeaponRange(s, *enemy, *actor, *profile.weapon)) ++threatExposure[avoid];
        arrived = actor->x >= 29 && std::abs(actor->y - 32) <= 1;
    }
    if (!arrived) return fail("tank did not finish threat-aware route");
}
if (threatExposure[0] <= 0 || threatExposure[1] >= threatExposure[0])
    return fail("AvoidThreats did not reduce actual travel through the powered enemy gun's range: "
        + std::to_string(threatExposure[0]) + ":" + std::to_string(threatExposure[1]));
std::cout << "Computer threat avoidance: completed routes=yes enemy-range exposure="
    << threatExposure[0] << ":" << threatExposure[1] << '\n';

for (bool suicide : {false, true}) {
    auto storage = makeWorld("Americans", "Easy", {}); auto& s = *storage;
    RuntimeSnapshotActiveStateScope scope(s);
    const auto* first = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Unit, "MTNK", {18, 33});
    if (!first) return fail("damage reaction tank unavailable"); const int firstId = first->instanceId;
    const auto* second = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Unit, "MTNK", {17, 33});
    if (!second) return fail("damage reaction escort unavailable"); const int secondId = second->instanceId;
    const auto* attacker = addRuntimeObjectInstanceForOwner(s, human, RuntimeBuildKind::Unit, "MTNK", {20, 33});
    if (!attacker) return fail("damage reaction attacker unavailable"); const int attackerId = attacker->instanceId;
    initializeRuntimeNavigation(s); freezeHouseOrders(s);
    findMutableObjectById(s, attackerId)->weaponCooldownTicksRemaining = 0;
    const int missionId = makeTeam(s, {firstId, secondId}, false, suicide, false, 33033);
    const int health = findObjectById(s, firstId)->health;
    if (!issueRuntimeTeamAttack(s, {attackerId}, *findObjectById(s, firstId))) return fail("real damage shot unavailable");
    bool hurt = false, regrouped = false;
    for (int frame = 0; frame < 160 && (!hurt || (!suicide && !regrouped)); ++frame) {
        tick(s);
        const auto* member = findObjectById(s, firstId);
        hurt |= !member || member->health < health;
        const auto mission = s.triggers.teamMissions.find(missionId);
        regrouped |= mission != s.triggers.teamMissions.end() && mission->second.regrouping;
    }
    if (!hurt || regrouped == suicide) return fail("actual enemy shot did not distinguish ordinary and Suicide team reactions");
    std::cout << "Computer damage reaction: Suicide=" << suicide << " real hit=yes regroup=" << regrouped << '\n';
}

{
    auto storage = makeWorld("Americans", "Easy", {}); auto& s = *storage;
    RuntimeSnapshotActiveStateScope scope(s);
    const auto* support = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Unit, "SREF", {8, 35});
    if (!support) return fail("slow support unavailable"); const int supportId = support->instanceId;
    const auto* escort = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Unit, "MTNK", {9, 35});
    if (!escort) return fail("fast escort unavailable"); const int escortId = escort->instanceId;
    initializeRuntimeNavigation(s); freezeHouseOrders(s);
    const int missionId = makeTeam(s, {supportId, escortId}, true, false, false, 35033);
    bool regrouping = false;
    for (int frame = 0; frame < 1600 && !regrouping; ++frame) {
        tick(s);
        const auto mission = s.triggers.teamMissions.find(missionId);
        regrouping = mission != s.triggers.teamMissions.end() && mission->second.regrouping
            && !mission->second.regroupDestinations.empty();
    }
    if (!regrouping) return fail("fast escort never regrouped with its slower prism tank");
    std::ostringstream saved; if (!writeRuntimeStateToStream(s, saved, error)) return false;
    IniFile ini; ini.loadFromString(saved.str()); auto restored = std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*restored, s, s.visionEnabled);
    if (!materializeRuntimeLoadStateFromIni(*restored, ini, error)) return false;
    for (auto* world : {&s, restored.get()}) {
        RuntimeSnapshotActiveStateScope worldScope(*world);
        bool arrived = false;
        for (int frame = 0; frame < 6000 && !arrived; ++frame) {
            tick(*world);
            const auto* slow = findObjectById(*world, supportId); const auto* fast = findObjectById(*world, escortId);
            arrived = slow && fast && slow->x >= 30 && fast->x >= 30;
        }
        if (!arrived) {
            std::ostringstream snapshot; std::string detail;
            if (writeRuntimeStateToStream(*world, snapshot, detail)) {
                std::ofstream out("build/smoke/gameplay/guard-column-failed.ini"); out << snapshot.str();
            }
            const auto* slow = findObjectById(*world, supportId); const auto* fast = findObjectById(*world, escortId);
            return fail("guarded column did not resume its route after physical regrouping: " + detail
                + (slow ? " support=" + std::to_string(slow->x) + "," + std::to_string(slow->y) + ":" + slow->mission : " support absent")
                + (fast ? " escort=" + std::to_string(fast->x) + "," + std::to_string(fast->y) + ":" + fast->mission : " escort absent"));
        }
    }
    std::cout << "Computer GuardSlower: escort rejoins prism tank=yes full route resumes=yes regroup save/resume=yes\n";
}
