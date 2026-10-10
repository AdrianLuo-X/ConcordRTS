#include "analysis/reports/ObjectReferenceValidator.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

std::string trim(const std::string& text) {
    const char* whitespace = " \t\r\n";

    const auto start = text.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }

    const auto end = text.find_last_not_of(whitespace);
    return text.substr(start, end - start + 1);
}

std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );

    return value;
}

std::string normalizedLookupKey(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(whitespace);
    std::string key;
    key.reserve(last - first + 1);
    for (std::size_t index = first; index <= last; ++index) {
        key.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[index]))
        ));
    }
    return key;
}

bool isEmptyReference(const std::string& value) {
    const std::string normalized = normalizedLookupKey(value);
    return normalized.empty()
        || normalized == "none"
        || normalized == "<none>";
}

bool isIntegerToken(const std::string& value) {
    const std::string normalized = trim(value);
    if (normalized.empty()) {
        return false;
    }

    std::size_t index = 0;
    if (normalized[index] == '-' || normalized[index] == '+') {
        ++index;
    }

    if (index >= normalized.size()) {
        return false;
    }

    for (; index < normalized.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(normalized[index]))) {
            return false;
        }
    }

    return true;
}

std::vector<std::string> splitCommaList(const std::string& text) {
    std::vector<std::string> values;
    std::size_t start = 0;

    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;

        const std::string value = trim(text.substr(start, end - start));
        if (!value.empty()) {
            values.push_back(value);
        }

        if (comma == std::string::npos) {
            break;
        }

        start = comma + 1;
    }

    return values;
}

std::unordered_set<std::string> normalizedSectionNames(const IniFile& ini) {
    std::vector<std::string> names = ini.getSectionNames();
    std::unordered_set<std::string> sections;
    sections.reserve(names.size());
    for (const std::string& sectionName : names) {
        const std::string key = normalizedLookupKey(sectionName);
        if (!key.empty()) {
            sections.insert(key);
        }
    }
    return sections;
}

bool hasSection(
    const std::unordered_set<std::string>& sections,
    const std::string& sectionName
) {
    const std::string normalized = normalizedLookupKey(sectionName);
    return !normalized.empty()
        && sections.find(normalized) != sections.end();
}

void addDiagnostic(
    DiagnosticReport& report,
    DiagnosticSeverity severity,
    const std::string& source,
    const std::string& field,
    const std::string& target,
    const std::string& message
) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.source = source;
    diagnostic.field = field;
    diagnostic.target = target;
    diagnostic.message = message;

    report.diagnostics.push_back(diagnostic);
}

std::unordered_set<std::string> valuesFromListSection(
    const IniFile& ini,
    const std::string& sectionName
) {
    std::unordered_set<std::string> values;
    const IniFile::Section* section = ini.getSection(sectionName);

    if (section == nullptr) {
        return values;
    }

    for (const auto& [key, value] : *section) {
        if (!value.empty()) {
            values.insert(value);
            values.insert(toLower(value));
        }
    }

    return values;
}

std::unordered_set<std::string> sideAndCountryNames(const IniFile& ini) {
    std::unordered_set<std::string> names = valuesFromListSection(ini, "Countries");

    const IniFile::Section* sides = ini.getSection("Sides");
    if (sides == nullptr) {
        return names;
    }

    for (const auto& [sideName, members] : *sides) {
        if (!sideName.empty()) {
            names.insert(sideName);
            names.insert(toLower(sideName));
        }

        for (const std::string& member : splitCommaList(members)) {
            names.insert(member);
            names.insert(toLower(member));
        }
    }

    return names;
}

bool isKnownPrerequisiteAlias(
    const std::unordered_set<std::string>& aliases,
    const std::string& value
) {
    const std::string normalized = normalizedLookupKey(value);
    return !normalized.empty()
        && aliases.find(normalized) != aliases.end();
}

std::unordered_set<std::string> knownPrerequisiteAliases(
    const RulesDatabase& rules
) {
    std::unordered_set<std::string> aliases = {
        "power",
        "barracks",
        "radar",
        "tech",
        "proc",
        "factory",
        "weapons",
        "weapon",
        "construction yard",
        "constructionyard",
        "buildingtype",
        "infantrytype",
        "unittype",
        "aircrafttype",
        "vesseltype"
    };

    aliases.reserve(aliases.size() + rules.getPrerequisiteAliases().size());
    for (const PrerequisiteAlias& alias : rules.getPrerequisiteAliases()) {
        const std::string key = normalizedLookupKey(alias.name);
        if (!key.empty()) {
            aliases.insert(key);
        }
    }

    return aliases;
}

bool isKnownFactoryType(const std::string& value) {
    static const std::unordered_set<std::string> factoryTypes = {
        "buildingtype",
        "infantrytype",
        "unittype",
        "aircrafttype",
        "vesseltype"
    };

    return factoryTypes.find(toLower(trim(value))) != factoryTypes.end();
}

bool parserWarningAlreadyCovered(const std::string& warning) {
    return warning.find("Missing type list section:") != std::string::npos
        || warning.find("-> missing unit section") != std::string::npos
        || warning.find("-> missing building section") != std::string::npos
        || warning.find("-> missing overlay section") != std::string::npos
        || warning.find("-> missing warhead section") != std::string::npos
        || warning.find("Weapon referenced by ") != std::string::npos
        || warning.find("Projectile referenced by ") != std::string::npos
        || warning.find("Warhead referenced by ") != std::string::npos;
}

void validateTypeListSections(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::string& listSection,
    bool required = true
) {
    const IniFile::Section* section = ini.getSection(listSection);

    if (section == nullptr) {
        if (!required) {
            return;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            listSection,
            "section",
            listSection,
            "Missing type list section."
        );
        return;
    }

    for (const auto& [index, objectId] : *section) {
        if (hasSection(sections, objectId)) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            listSection,
            "section",
            objectId,
            "List entry has no matching object section."
        );
    }
}

void validateHouseRestrictionField(
    const IniFile& ini,
    DiagnosticReport& report,
    const std::string& objectId,
    const std::string& field,
    const std::unordered_set<std::string>& validOwners
) {
    const std::string fieldValue = ini.getValue(objectId, field, "");
    if (fieldValue.empty()) {
        return;
    }

    for (const std::string& house : splitCommaList(fieldValue)) {
        if (isEmptyReference(house)
            || validOwners.find(house) != validOwners.end()
            || validOwners.find(toLower(house)) != validOwners.end()) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            "object [" + objectId + "]",
            field,
            house,
            field + " value was not found in [Countries] or [Sides]."
        );
    }
}

void validateWeaponReference(
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::string& source,
    const std::string& field,
    const std::string& weaponId,
    DiagnosticSeverity severity
) {
    if (isEmptyReference(weaponId) || hasSection(sections, weaponId)) {
        return;
    }

    addDiagnostic(
        report,
        severity,
        source,
        field,
        weaponId,
        "Weapon reference has no matching section."
    );
}

void validateUnitWeaponReferences(
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::vector<UnitType>& units,
    const std::string& objectKind
) {
    for (const UnitType& unit : units) {
        const std::string source = objectKind + " [" + unit.id + "]";

        validateWeaponReference(
            sections,
            report,
            source,
            "Primary",
            unit.primaryWeapon,
            DiagnosticSeverity::Error
        );
        validateWeaponReference(
            sections,
            report,
            source,
            "Secondary",
            unit.secondaryWeapon,
            DiagnosticSeverity::Warning
        );
        validateWeaponReference(
            sections,
            report,
            source,
            "ElitePrimary",
            unit.elitePrimaryWeapon,
            DiagnosticSeverity::Warning
        );
        validateWeaponReference(
            sections,
            report,
            source,
            "EliteSecondary",
            unit.eliteSecondaryWeapon,
            DiagnosticSeverity::Warning
        );
        if (unit.deployWeaponSource == "DeployWeapon") {
            validateWeaponReference(
                sections,
                report,
                source,
                "DeployWeapon",
                unit.deployWeapon,
                DiagnosticSeverity::Warning
            );
        }
        if (unit.eliteDeployWeaponSource == "EliteDeployWeapon") {
            validateWeaponReference(
                sections,
                report,
                source,
                "EliteDeployWeapon",
                unit.eliteDeployWeapon,
                DiagnosticSeverity::Warning
            );
        }
        if (!isIntegerToken(unit.occupyWeapon)) {
            validateWeaponReference(
                sections,
                report,
                source,
                "OccupyWeapon",
                unit.occupyWeapon,
                DiagnosticSeverity::Warning
            );
        }
        if (!isIntegerToken(unit.eliteOccupyWeapon)) {
            validateWeaponReference(
                sections,
                report,
                source,
                "EliteOccupyWeapon",
                unit.eliteOccupyWeapon,
                DiagnosticSeverity::Warning
            );
        }
    }
}

void validateBuildingWeaponReferences(
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::vector<BuildingType>& buildings
) {
    for (const BuildingType& building : buildings) {
        const std::string source = "building [" + building.id + "]";

        validateWeaponReference(
            sections,
            report,
            source,
            "Primary",
            building.primaryWeapon,
            DiagnosticSeverity::Error
        );
        validateWeaponReference(
            sections,
            report,
            source,
            "Secondary",
            building.secondaryWeapon,
            DiagnosticSeverity::Warning
        );
        validateWeaponReference(
            sections,
            report,
            source,
            "ElitePrimary",
            building.elitePrimaryWeapon,
            DiagnosticSeverity::Warning
        );
        validateWeaponReference(
            sections,
            report,
            source,
            "EliteSecondary",
            building.eliteSecondaryWeapon,
            DiagnosticSeverity::Warning
        );
        if (building.deployWeaponSource == "DeployWeapon") {
            validateWeaponReference(
                sections,
                report,
                source,
                "DeployWeapon",
                building.deployWeapon,
                DiagnosticSeverity::Warning
            );
        }
        if (building.eliteDeployWeaponSource == "EliteDeployWeapon") {
            validateWeaponReference(
                sections,
                report,
                source,
                "EliteDeployWeapon",
                building.eliteDeployWeapon,
                DiagnosticSeverity::Warning
            );
        }
    }
}

void validateWeaponProjectiles(
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::vector<WeaponType>& weapons
) {
    for (const WeaponType& weapon : weapons) {
        if (isEmptyReference(weapon.projectile)
            || hasSection(sections, weapon.projectile)) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            "weapon [" + weapon.id + "]",
            "Projectile",
            weapon.projectile,
            "Projectile reference has no matching section."
        );
    }
}

void validateWeaponWarheads(
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::vector<WeaponType>& weapons
) {
    for (const WeaponType& weapon : weapons) {
        const std::string source = "weapon [" + weapon.id + "]";

        if (isEmptyReference(weapon.warhead)) {
            addDiagnostic(
                report,
                DiagnosticSeverity::Warning,
                source,
                "Warhead",
                "",
                "Weapon has no Warhead value."
            );
            continue;
        }

        if (!hasSection(sections, weapon.warhead)) {
            addDiagnostic(
                report,
                DiagnosticSeverity::Error,
                source,
                "Warhead",
                weapon.warhead,
                "Warhead reference has no matching section."
            );
        }
    }
}

void validateProjectileWeaponReferences(
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::vector<ProjectileType>& projectiles
) {
    for (const ProjectileType& projectile : projectiles) {
        validateWeaponReference(
            sections,
            report,
            "projectile [" + projectile.id + "]",
            "AirburstWeapon",
            projectile.airburstWeapon,
            DiagnosticSeverity::Warning
        );
    }
}

void validateWarheadVerses(
    DiagnosticReport& report,
    const std::vector<WarheadType>& warheads
) {
    for (const WarheadType& warhead : warheads) {
        if (!warhead.verses.empty()) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            "warhead [" + warhead.id + "]",
            "Verses",
            "",
            "Warhead has no Verses value."
        );
    }
}

void validateOwnerField(
    const IniFile& ini,
    DiagnosticReport& report,
    const std::string& objectId,
    const std::unordered_set<std::string>& validOwners
) {
    const std::string ownerValue = ini.getValue(objectId, "Owner", "");
    if (ownerValue.empty()) {
        return;
    }

    for (const std::string& owner : splitCommaList(ownerValue)) {
        if (validOwners.find(owner) != validOwners.end()
            || validOwners.find(toLower(owner)) != validOwners.end()) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            "object [" + objectId + "]",
            "Owner",
            owner,
            "Owner value was not found in [Countries] or [Sides]."
        );
    }
}

void validatePrerequisiteField(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    const std::unordered_set<std::string>& prerequisiteAliases,
    DiagnosticReport& report,
    const std::string& objectId,
    const std::string& field
) {
    const std::string prerequisiteValue = ini.getValue(objectId, field, "");
    if (prerequisiteValue.empty()) {
        return;
    }

    for (const std::string& prerequisite : splitCommaList(prerequisiteValue)) {
        if (isKnownPrerequisiteAlias(prerequisiteAliases, prerequisite)
            || hasSection(sections, prerequisite)) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            "object [" + objectId + "]",
            field,
            prerequisite,
            "Prerequisite value was not found as an object section or known abstract prerequisite."
        );
    }
}

void validatePrerequisiteFields(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    const std::unordered_set<std::string>& prerequisiteAliases,
    DiagnosticReport& report,
    const std::string& objectId
) {
    validatePrerequisiteField(
        ini,
        sections,
        prerequisiteAliases,
        report,
        objectId,
        "Prerequisite"
    );
    validatePrerequisiteField(
        ini,
        sections,
        prerequisiteAliases,
        report,
        objectId,
        "PrerequisiteOverride"
    );
    validatePrerequisiteField(
        ini,
        sections,
        prerequisiteAliases,
        report,
        objectId,
        "Prerequisite.Override"
    );
}

void validateSingleSectionReference(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::string& objectId,
    const std::string& field
) {
    const std::string target = ini.getValue(objectId, field, "");
    if (isEmptyReference(target) || hasSection(sections, target)) {
        return;
    }

    addDiagnostic(
        report,
        DiagnosticSeverity::Warning,
        "object [" + objectId + "]",
        field,
        target,
        "Referenced section was not found."
    );
}

void validateSectionReferenceList(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::string& objectId,
    const std::string& field
) {
    const std::string targets = ini.getValue(objectId, field, "");
    if (targets.empty()) {
        return;
    }

    for (const std::string& target : splitCommaList(targets)) {
        if (isEmptyReference(target) || hasSection(sections, target)) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            "object [" + objectId + "]",
            field,
            target,
            "Referenced section was not found."
        );
    }
}

void validateFactoryField(
    DiagnosticReport& report,
    const std::string& objectId,
    const std::string& factory
) {
    if (isEmptyReference(factory) || isKnownFactoryType(factory)) {
        return;
    }

    addDiagnostic(
        report,
        DiagnosticSeverity::Warning,
        "object [" + objectId + "]",
        "Factory",
        factory,
        "Factory value is not a known production type."
    );
}

void validateGeneralPrerequisiteAliases(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    const std::unordered_set<std::string>& prerequisiteAliases,
    DiagnosticReport& report
) {
    const char* keys[] = {
        "PrerequisitePower",
        "PrerequisiteFactory",
        "PrerequisiteBarracks",
        "PrerequisiteRadar",
        "PrerequisiteTech",
        "PrerequisiteProc",
        "PrerequisiteProcAlternate"
    };

    for (const char* key : keys) {
        const std::string value = ini.getValue("General", key, "");
        if (value.empty()) {
            continue;
        }

        for (const std::string& prerequisite : splitCommaList(value)) {
            if (isKnownPrerequisiteAlias(prerequisiteAliases, prerequisite)
                || hasSection(sections, prerequisite)) {
                continue;
            }

            addDiagnostic(
                report,
                DiagnosticSeverity::Warning,
                "General",
                key,
                prerequisite,
                "Prerequisite alias value was not found as an object section."
            );
        }
    }
}

void validateObjectFields(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    const std::unordered_set<std::string>& prerequisiteAliases,
    DiagnosticReport& report,
    const std::string& objectId,
    const std::unordered_set<std::string>& validOwners
) {
    validateOwnerField(ini, report, objectId, validOwners);
    validateHouseRestrictionField(
        ini,
        report,
        objectId,
        "RequiredHouses",
        validOwners
    );
    validateHouseRestrictionField(
        ini,
        report,
        objectId,
        "ForbiddenHouses",
        validOwners
    );
    validatePrerequisiteFields(
        ini,
        sections,
        prerequisiteAliases,
        report,
        objectId
    );
    validateSingleSectionReference(
        ini,
        sections,
        report,
        objectId,
        "DeploysInto"
    );
    validateSingleSectionReference(
        ini,
        sections,
        report,
        objectId,
        "UndeploysInto"
    );
    validateSingleSectionReference(
        ini,
        sections,
        report,
        objectId,
        "UnloadingClass"
    );
    validateSingleSectionReference(
        ini,
        sections,
        report,
        objectId,
        "SuperWeapon"
    );
    validateSingleSectionReference(
        ini,
        sections,
        report,
        objectId,
        "SuperWeapon2"
    );
}

void validateUnitObjectFields(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    const std::unordered_set<std::string>& prerequisiteAliases,
    DiagnosticReport& report,
    const std::vector<UnitType>& units,
    const std::unordered_set<std::string>& validOwners
) {
    for (const UnitType& unit : units) {
        validateObjectFields(
            ini,
            sections,
            prerequisiteAliases,
            report,
            unit.id,
            validOwners
        );
        validateSectionReferenceList(ini, sections, report, unit.id, "Dock");
    }
}

void validateBuildingObjectFields(
    const IniFile& ini,
    const std::unordered_set<std::string>& sections,
    const std::unordered_set<std::string>& prerequisiteAliases,
    DiagnosticReport& report,
    const std::vector<BuildingType>& buildings,
    const std::unordered_set<std::string>& validOwners
) {
    for (const BuildingType& building : buildings) {
        validateObjectFields(
            ini,
            sections,
            prerequisiteAliases,
            report,
            building.id,
            validOwners
        );
        validateSingleSectionReference(
            ini,
            sections,
            report,
            building.id,
            "FreeUnit"
        );
        validateFactoryField(report, building.id, building.factory);
    }
}

void validateOverlayObjectFields(
    const std::unordered_set<std::string>& sections,
    DiagnosticReport& report,
    const std::vector<OverlayType>& overlays
) {
    std::unordered_set<std::string> overlayIds;
    for (const OverlayType& overlay : overlays) {
        overlayIds.insert(toLower(overlay.id));
    }

    for (const OverlayType& overlay : overlays) {
        if (isEmptyReference(overlay.toOverlay)
            || hasSection(sections, overlay.toOverlay)
            || overlayIds.find(toLower(overlay.toOverlay)) != overlayIds.end()) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            "overlay [" + overlay.id + "]",
            "ToOverlay",
            overlay.toOverlay,
            "Referenced overlay was not found in [OverlayTypes] or as a section."
        );
    }
}

void addParserWarnings(
    const RulesDatabase& rules,
    DiagnosticReport& report
) {
    for (const std::string& warning : rules.getWarnings()) {
        if (parserWarningAlreadyCovered(warning)) {
            continue;
        }

        addDiagnostic(
            report,
            DiagnosticSeverity::Warning,
            "RulesDatabase",
            "parse",
            "",
            warning
        );
    }
}

} // namespace

int DiagnosticReport::errorCount() const {
    return static_cast<int>(std::count_if(
        diagnostics.begin(),
        diagnostics.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        }
    ));
}

int DiagnosticReport::warningCount() const {
    return static_cast<int>(std::count_if(
        diagnostics.begin(),
        diagnostics.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Warning;
        }
    ));
}

int DiagnosticReport::infoCount() const {
    return static_cast<int>(std::count_if(
        diagnostics.begin(),
        diagnostics.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Info;
        }
    ));
}

DiagnosticReport ObjectReferenceValidator::validate(
    const IniFile& ini,
    const RulesDatabase& rules
) const {
    DiagnosticReport report;
    const std::unordered_set<std::string> sections =
        normalizedSectionNames(ini);
    const std::unordered_set<std::string> prerequisiteAliases =
        knownPrerequisiteAliases(rules);
    const std::unordered_set<std::string> validOwners = sideAndCountryNames(ini);

    validateTypeListSections(ini, sections, report, "InfantryTypes");
    validateTypeListSections(ini, sections, report, "VehicleTypes");
    validateTypeListSections(ini, sections, report, "AircraftTypes");
    validateTypeListSections(ini, sections, report, "BuildingTypes");
    validateTypeListSections(ini, sections, report, "Warheads");

    validateUnitWeaponReferences(
        sections,
        report,
        rules.getInfantryTypes(),
        "infantry"
    );
    validateUnitWeaponReferences(
        sections,
        report,
        rules.getVehicleTypes(),
        "vehicle"
    );
    validateUnitWeaponReferences(
        sections,
        report,
        rules.getAircraftTypes(),
        "aircraft"
    );
    validateBuildingWeaponReferences(sections, report, rules.getBuildingTypes());

    validateWeaponProjectiles(sections, report, rules.getWeaponTypes());
    validateWeaponWarheads(sections, report, rules.getWeaponTypes());
    validateProjectileWeaponReferences(
        sections,
        report,
        rules.getProjectileTypes()
    );
    validateWarheadVerses(report, rules.getWarheadTypes());
    validateGeneralPrerequisiteAliases(
        ini,
        sections,
        prerequisiteAliases,
        report
    );

    validateUnitObjectFields(
        ini,
        sections,
        prerequisiteAliases,
        report,
        rules.getInfantryTypes(),
        validOwners
    );
    validateUnitObjectFields(
        ini,
        sections,
        prerequisiteAliases,
        report,
        rules.getVehicleTypes(),
        validOwners
    );
    validateUnitObjectFields(
        ini,
        sections,
        prerequisiteAliases,
        report,
        rules.getAircraftTypes(),
        validOwners
    );
    validateBuildingObjectFields(
        ini,
        sections,
        prerequisiteAliases,
        report,
        rules.getBuildingTypes(),
        validOwners
    );
    validateOverlayObjectFields(sections, report, rules.getOverlayTypes());

    addParserWarnings(rules, report);

    return report;
}

std::string toString(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Info:
        return "info";
    }

    return "unknown";
}
