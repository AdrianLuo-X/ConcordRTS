// A production completion creates a FIFO obligation even while the vat's
// current clone is still leaving. Repeated host/type records are distinct
// completions; they must not be deduplicated or sorted on save/load.
namespace {
constexpr std::size_t runtimeClonePendingDeliveriesMaximumCount = 100000U;

bool runtimeClonePendingDeliveryTextValid(const std::string& value) {
    return !value.empty() && value.size() <= 256U
        && std::none_of(value.begin(), value.end(), [](unsigned char c) {
            return c < 0x20U || c == 0x7fU;
        });
}

bool runtimeClonePendingDeliveriesCandidateValid(
    const RuntimeWindowState& state,
    const std::vector<RuntimeSpecialBuildingClone>& candidate,
    std::string& error
) {
    const auto fail = [&](const char* reason) {
        error = std::string("invalid ClonePendingDeliveries: ") + reason;
        return false;
    };
    if (candidate.size() > runtimeClonePendingDeliveriesMaximumCount)
        return fail("queue exceeds count limit");
    if (!candidate.empty() && !state.rulesLoaded)
        return fail("rules unavailable");
    std::unordered_map<int, std::string> hostOwners;
    for (const auto& host : state.world.getObjects()) {
        if (host.kind != WorldObjectKind::Structure || !host.alive
            || host.health <= 0 || host.rubble) continue;
        const auto* building = findRuntimeBuildingTypeById(state.rules, host.type);
        if (!building || !building->cloning) continue;
        hostOwners.emplace(host.instanceId,
            runtimeRepairDepotCanonicalOwnerId(state, host.owner));
    }
    for (const auto& request : candidate) {
        if (request.sourceObjectId < 0
            || !runtimeClonePendingDeliveryTextValid(request.owner)
            || !runtimeClonePendingDeliveryTextValid(request.infantryTypeId)
            || !state.rules.findUnitType("InfantryType", request.infantryTypeId))
            return fail("host ID, owner or infantry type");
        const auto host = hostOwners.find(request.sourceObjectId);
        const auto owner = runtimeRepairDepotCanonicalOwnerId(state, request.owner);
        if (host == hostOwners.end() || owner.empty() || host->second != owner
            || runtimeOwnerIsNeutralForAutoTarget(request.owner))
            return fail("missing/dead/non-cloning host or owner mismatch");
    }
    return true;
}
} // namespace

std::string runtimeClonePendingDeliveriesRecord(
    const RuntimeSpecialBuildingClone& request
) {
    return std::to_string(request.sourceObjectId) + '|'
        + iniHexEncode(request.owner) + '|'
        + iniHexEncode(request.infantryTypeId);
}

bool validateRuntimeClonePendingDeliveries(
    const RuntimeWindowState& state, std::string& error
) {
    return runtimeClonePendingDeliveriesCandidateValid(
        state, state.pendingCloneDeliveries, error);
}

void writeRuntimeClonePendingDeliveries(
    const RuntimeWindowState& state, std::ostream& output
) {
    std::string error;
    if (!validateRuntimeClonePendingDeliveries(state, error)) {
        output.setstate(std::ios::failbit);
        return;
    }
    output << "\n[ClonePendingDeliveries]\nSchema=1\nCount="
        << state.pendingCloneDeliveries.size() << '\n';
    for (std::size_t index = 0; index < state.pendingCloneDeliveries.size(); ++index)
        output << index << '='
            << runtimeClonePendingDeliveriesRecord(state.pendingCloneDeliveries[index])
            << '\n';
}

bool restoreRuntimeClonePendingDeliveries(
    RuntimeWindowState& state, const IniFile& ini, std::string& error
) {
    const auto fail = [&](const char* reason) {
        error = std::string("invalid ClonePendingDeliveries: ") + reason;
        return false;
    };
    const auto* section = ini.getSection("ClonePendingDeliveries");
    if (!section) {
        state.pendingCloneDeliveries.clear(); // Legacy ordinary saves.
        return true;
    }
    std::size_t matchingSections = 0;
    for (const auto& name : ini.getSectionNames())
        if (toLowerAsciiLocal(name) == "clonependingdeliveries") ++matchingSections;
    if (matchingSections != 1U) return fail("duplicate section names");
    int count = -1;
    if (ini.getValue("ClonePendingDeliveries", "Schema", "") != "1"
        || !parseRuntimeInt(ini.getValue("ClonePendingDeliveries", "Count", ""), count)
        || count < 0 || static_cast<std::size_t>(count) > runtimeClonePendingDeliveriesMaximumCount)
        return fail("schema or count");
    std::unordered_set<std::string> keys;
    for (const auto& [key, value] : *section) {
        (void)value;
        const auto normalized = toLowerAsciiLocal(trimAsciiLocal(key));
        if (!keys.insert(normalized).second) return fail("duplicate key");
        if (normalized == "schema" || normalized == "count") continue;
        int index = -1;
        if (!parseRuntimeInt(normalized, index) || index < 0 || index >= count
            || normalized != std::to_string(index)) return fail("record index");
    }
    if (keys.size() != static_cast<std::size_t>(count) + 2U)
        return fail("missing or extra records");
    std::vector<RuntimeSpecialBuildingClone> candidate;
    candidate.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(
            "ClonePendingDeliveries", std::to_string(index), ""));
        RuntimeSpecialBuildingClone request;
        if (fields.size() != 3U
            || !parseRuntimeInt(fields[0], request.sourceObjectId)
            || request.sourceObjectId < 0
            || fields[0] != std::to_string(request.sourceObjectId)
            || !iniHexDecode(fields[1], request.owner, 256U)
            || !iniHexDecode(fields[2], request.infantryTypeId, 256U))
            return fail("host ID, token count or encoded text");
        candidate.push_back(std::move(request));
    }
    if (!runtimeClonePendingDeliveriesCandidateValid(state, candidate, error))
        return false;
    // No saved record can alter live authority until all records are valid.
    state.pendingCloneDeliveries = std::move(candidate);
    return true;
}
