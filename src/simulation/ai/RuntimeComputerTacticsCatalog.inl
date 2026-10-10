// Global AI trigger availability is separate from scenario trigger execution.
// A skirmish assembles these teams using its actual factories and credits.
void loadRuntimeComputerTacticTriggers(RuntimeWindowState& state,
    const IniFile* globalAi, const IniFile& mapIni) {
    auto& catalog = state.triggers.computerAiTriggers;
    catalog.clear();
    if (state.campaignMode) return;
    const auto merge = [&](const IniFile& ini) {
        const auto* definitions = ini.getSection("AITriggerTypes");
        if (definitions == nullptr) return;
        for (const auto& [id, value] : *definitions) {
            auto definition = parseRuntimeCampaignAiTrigger(id, value);
            if (const auto* enabled = ini.getSection("AITriggerTypesEnable"))
                for (const auto& [enabledId, enabledValue] : *enabled)
                    if (toLowerAsciiLocal(enabledId) == toLowerAsciiLocal(id))
                        definition.enabled = parseRuntimeBool(enabledValue);
            // The twelfth CSV field is reserved. Base defense is field 14,
            // as written by the original AITriggerTypeClass save routine.
            const auto fields = splitRuntimeTokenList(value);
            definition.baseDefense = fields.size() >= 14 && parseRuntimeBool(fields[13]);
            const auto previous = std::find_if(catalog.begin(),
                catalog.end(), [&](const auto& entry) {
                    return toLowerAsciiLocal(entry.id) == toLowerAsciiLocal(id);
                });
            if (previous == catalog.end())
                catalog.push_back(std::move(definition));
            else *previous = std::move(definition);
        }
    };
    if (globalAi != nullptr && !parseRuntimeBool(
        mapIni.getValue("Basic", "IgnoreGlobalAITriggers", "no"))) merge(*globalAi);
    merge(mapIni);
    if (const auto* enabled = mapIni.getSection("AITriggerTypesEnable")) {
        for (auto& definition : catalog)
            for (const auto& [id, value] : *enabled)
                if (toLowerAsciiLocal(id) == toLowerAsciiLocal(definition.id))
                    definition.enabled = parseRuntimeBool(value);
    }
    std::sort(catalog.begin(), catalog.end(),
        [](const auto& left, const auto& right) { return left.id < right.id; });
}
