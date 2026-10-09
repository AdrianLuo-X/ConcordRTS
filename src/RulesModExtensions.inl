// Member implementations included by RulesDatabase.cpp.
const TechnoModExtensionRule* RulesDatabase::findTechnoModExtension(const std::string& id) const {
    const auto found = technoModExtensions_.find(canonicalIdKey(id));
    return found == technoModExtensions_.end() ? nullptr : &found->second;
}

const ShieldTypeRule* RulesDatabase::findShieldType(const std::string& id) const {
    const auto found = shieldTypes_.find(canonicalIdKey(id));
    return found == shieldTypes_.end() ? nullptr : &found->second;
}

AttachEffectRule RulesDatabase::loadAttachEffect(const IniFile& ini, const std::string& id, bool warhead) {
    AttachEffectRule rule;
    rule.duration = std::max(-1, readInt(ini, id, "AttachEffect.Duration", 0));
    rule.initialDelay = warhead ? 0 : std::max(0, readInt(ini, id, "AttachEffect.InitialDelay", 0));
    rule.delay = warhead ? -1 : std::max(-1, readInt(ini, id, "AttachEffect.Delay", 0));
    const auto multiplier = [&](const char* key) {
        const double value = readRatio(ini, id, key, 1.0);
        if (value < 0.01 || value > 100.0) {
            addWarning("[" + id + "] " + key + " must be in [0.01,100]; using 1");
            return 1.0;
        }
        return value;
    };
    rule.speed = multiplier("AttachEffect.SpeedMultiplier");
    rule.armor = multiplier("AttachEffect.ArmorMultiplier");
    rule.firepower = multiplier("AttachEffect.FirepowerMultiplier");
    rule.rof = multiplier("AttachEffect.ROFMultiplier");
    rule.animation = trim(ini.getValue(id, "AttachEffect.Animation", ""));
    if (isNoneValue(rule.animation)) rule.animation.clear();
    rule.cumulative = warhead && readBool(ini, id, "AttachEffect.Cumulative", false);
    rule.animResetOnReapply = warhead && readBool(ini, id, "AttachEffect.AnimResetOnReapply", false);
    rule.temporalHidesAnim = readBool(ini, id, "AttachEffect.TemporalHidesAnim", false);
    rule.cloakable = readBool(ini, id, "AttachEffect.Cloakable", false);
    rule.forceDecloak = readBool(ini, id, "AttachEffect.ForceDecloak", false);
    rule.discardOnEntry = readBool(ini, id, "AttachEffect.DiscardOnEntry", false);
    rule.penetratesIronCurtain = readBool(ini, id, "AttachEffect.PenetratesIronCurtain", false);
    return rule;
}

void RulesDatabase::loadModExtensions(const IniFile& ini) {
    technoModExtensions_.clear();
    shieldTypes_.clear();
    const auto* registry = ini.getSection("ShieldTypes");
    if (registry) for (const auto& [index, entry] : *registry) {
        (void)index;
        const std::string id = trim(entry);
        if (id.empty() || isNoneValue(id)) continue;
        ShieldTypeRule rule;
        rule.id = id;
        rule.strength = std::clamp(readInt(ini, id, "Strength", 0), 0, 100000000);
        rule.initialStrength = std::clamp(readInt(ini, id, "InitialStrength", rule.strength), 0, rule.strength);
        rule.armor = trim(ini.getValue(id, "Armor", "none"));
        rule.inheritArmor = readBool(ini, id, "InheritArmorFromTechno", false);
        rule.applyArmorMult = readBool(ini, id, "ApplyArmorMult",
            readBool(ini, "CombatDamage", "ShieldApplyArmorMult", false));
        rule.powered = readBool(ini, id, "Powered", false);
        rule.absorbOverDamage = readBool(ini, id, "AbsorbOverDamage", false);
        rule.selfHealing = std::clamp(readRatio(ini, id, "SelfHealing", 0), -100000000.0, 100000000.0);
        rule.selfHealingRate = std::clamp(readDouble(ini, id, "SelfHealing.Rate", 0), 0.0, 1440.0);
        rule.selfHealingRestartInCombat = readBool(ini, id, "SelfHealing.RestartInCombat", true);
        rule.selfHealingRestartDelay = std::max(0, readInt(ini, id, "SelfHealing.RestartInCombatDelay", 0));
        rule.respawn = std::clamp(readRatio(ini, id, "Respawn", 0), 0.0, 100000000.0);
        rule.respawnRate = std::clamp(readDouble(ini, id, "Respawn.Rate", 0), 0.0, 1440.0);
        rule.respawnRestartInCombat = readBool(ini, id, "Respawn.RestartInCombat", true);
        rule.respawnRestartDelay = std::max(0, readInt(ini, id, "Respawn.RestartInCombatDelay", 0));
        shieldTypes_.emplace(canonicalIdKey(id), std::move(rule));
    }
    const auto loadTechnos = [&](const auto& values) {
        for (const auto& type : values) {
            TechnoModExtensionRule rule;
            rule.shieldType = trim(ini.getValue(type.id, "Shield.Type", ""));
            if (isNoneValue(rule.shieldType)) rule.shieldType.clear();
            if (!rule.shieldType.empty() && !findShieldType(rule.shieldType))
                addWarning("[" + type.id + "] Shield.Type references an unregistered ShieldType: " + rule.shieldType);
            rule.attachEffect = loadAttachEffect(ini, type.id, false);
            technoModExtensions_.emplace(canonicalIdKey(type.id), std::move(rule));
        }
    };
    loadTechnos(infantryTypes_); loadTechnos(vehicleTypes_); loadTechnos(aircraftTypes_); loadTechnos(buildingTypes_);
    for (const auto& building : buildingTypes_) for (const auto& id : building.extraSuperWeapons)
        if (!isNoneValue(id) && !findSuperWeaponType(id))
            addWarning("[" + building.id + "] SuperWeapons references missing SuperWeaponType: " + id);

    // Include every authored extension dependency in the rules fingerprint,
    // including unsupported options reported by the scanner.
    std::vector<std::string> entries;
    for (const auto& section : ini.getSectionNames()) {
        const auto* values = ini.getSection(section);
        if (!values) continue;
        const bool shield = findShieldType(section) != nullptr;
        for (const auto& [key, value] : *values) {
            const auto normalized = toLower(key);
            if (shield || normalized.rfind("attacheffect.", 0) == 0
                || normalized.rfind("shield.", 0) == 0 || normalized == "superweapons"
                || normalized == "shieldapplyarmormult")
                entries.push_back(canonicalIdKey(section) + "|" + normalized + "=" + value);
        }
    }
    std::sort(entries.begin(), entries.end());
    modExtensionRulesHash_ = 14695981039346656037ULL;
    for (const auto& entry : entries) for (unsigned char c : entry + "\n")
        modExtensionRulesHash_ = (modExtensionRulesHash_ ^ c) * 1099511628211ULL;
}
