#include "JsonExporter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <vector>

namespace {

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char character : value) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}

void writeIndent(std::ostream& output, int indent) {
    for (int i = 0; i < indent; ++i) {
        output << ' ';
    }
}

void writeStringProperty(
    std::ostream& output,
    int indent,
    const std::string& key,
    const std::string& value,
    bool hasComma
) {
    writeIndent(output, indent);
    output << "\"" << key << "\": \"" << jsonEscape(value) << "\"";
    if (hasComma) {
        output << ",";
    }
    output << "\n";
}

void writeIntProperty(
    std::ostream& output,
    int indent,
    const std::string& key,
    int value,
    bool hasComma
) {
    writeIndent(output, indent);
    output << "\"" << key << "\": " << value;
    if (hasComma) {
        output << ",";
    }
    output << "\n";
}

void writeDoubleProperty(
    std::ostream& output,
    int indent,
    const std::string& key,
    double value,
    bool hasComma
) {
    writeIndent(output, indent);
    output << "\"" << key << "\": " << value;
    if (hasComma) {
        output << ",";
    }
    output << "\n";
}

void writeBoolProperty(
    std::ostream& output,
    int indent,
    const std::string& key,
    bool value,
    bool hasComma
) {
    writeIndent(output, indent);
    output << "\"" << key << "\": " << (value ? "true" : "false");
    if (hasComma) {
        output << ",";
    }
    output << "\n";
}

void writeUnit(std::ostream& output, const UnitType& unit, int indent) {
    writeIndent(output, indent);
    output << "{\n";

    writeStringProperty(output, indent + 2, "id", unit.id, true);
    writeStringProperty(output, indent + 2, "uiName", unit.uiName, true);
    writeStringProperty(output, indent + 2, "name", unit.name, true);
    writeStringProperty(output, indent + 2, "image", unit.image, true);
    writeStringProperty(output, indent + 2, "owner", unit.owner, true);
    writeIntProperty(output, indent + 2, "techLevel", unit.techLevel, true);
    writeStringProperty(output, indent + 2, "prerequisite", unit.prerequisite, true);
    writeIntProperty(output, indent + 2, "strength", unit.strength, true);
    writeStringProperty(output, indent + 2, "armor", unit.armor, true);
    writeStringProperty(output, indent + 2, "primaryWeapon", unit.primaryWeapon, true);
    writeStringProperty(output, indent + 2, "secondaryWeapon", unit.secondaryWeapon, true);
    writeStringProperty(output, indent + 2, "elitePrimaryWeapon", unit.elitePrimaryWeapon, true);
    writeStringProperty(output, indent + 2, "eliteSecondaryWeapon", unit.eliteSecondaryWeapon, true);
    writeIntProperty(output, indent + 2, "speed", unit.speed, true);
    writeStringProperty(output, indent + 2, "speedType", unit.speedType, true);
    writeStringProperty(output, indent + 2, "movementZone", unit.movementZone, true);
    writeStringProperty(output, indent + 2, "locomotor", unit.locomotor, true);
    writeIntProperty(output, indent + 2, "sight", unit.sight, true);
    writeStringProperty(output, indent + 2, "category", unit.category, true);
    writeBoolProperty(output, indent + 2, "crusher", unit.crusher, true);
    writeBoolProperty(output, indent + 2, "turret", unit.turret, true);
    writeIntProperty(output, indent + 2, "passengers", unit.passengers, true);
    writeIntProperty(output, indent + 2, "size", unit.size, true);
    writeStringProperty(output, indent + 2, "deploysInto", unit.deploysInto, true);
    writeBoolProperty(output, indent + 2, "airportBound", unit.airportBound, true);
    writeStringProperty(output, indent + 2, "dock", unit.dock, true);
    writeIntProperty(output, indent + 2, "ammo", unit.ammo, true);
    writeIntProperty(output, indent + 2, "rot", unit.rot, true);
    writeBoolProperty(output, indent + 2, "noShadow", unit.noShadow, true);
    writeBoolProperty(output, indent + 2, "fearless", unit.fearless, true);
    writeBoolProperty(output, indent + 2, "selfHealing", unit.selfHealing, true);
    writeIntProperty(output, indent + 2, "cost", unit.cost, false);

    writeIndent(output, indent);
    output << "}";
}

void writeBuilding(std::ostream& output, const BuildingType& building, int indent) {
    writeIndent(output, indent);
    output << "{\n";

    writeStringProperty(output, indent + 2, "id", building.id, true);
    writeStringProperty(output, indent + 2, "uiName", building.uiName, true);
    writeStringProperty(output, indent + 2, "name", building.name, true);
    writeStringProperty(output, indent + 2, "image", building.image, true);
    writeStringProperty(output, indent + 2, "owner", building.owner, true);
    writeIntProperty(output, indent + 2, "techLevel", building.techLevel, true);
    writeStringProperty(output, indent + 2, "prerequisite", building.prerequisite, true);
    writeIntProperty(output, indent + 2, "strength", building.strength, true);
    writeStringProperty(output, indent + 2, "armor", building.armor, true);
    writeStringProperty(output, indent + 2, "primaryWeapon", building.primaryWeapon, true);
    writeStringProperty(output, indent + 2, "secondaryWeapon", building.secondaryWeapon, true);
    writeStringProperty(output, indent + 2, "elitePrimaryWeapon", building.elitePrimaryWeapon, true);
    writeStringProperty(output, indent + 2, "eliteSecondaryWeapon", building.eliteSecondaryWeapon, true);
    writeIntProperty(output, indent + 2, "power", building.power, true);
    writeBoolProperty(output, indent + 2, "powered", building.powered, true);
    writeStringProperty(output, indent + 2, "buildCat", building.buildCat, true);
    writeStringProperty(output, indent + 2, "factory", building.factory, true);
    writeStringProperty(output, indent + 2, "superWeapon", building.superWeapon, true);
    writeBoolProperty(
        output,
        indent + 2,
        "noShadow",
        building.noShadow,
        true
    );
    writeBoolProperty(
        output,
        indent + 2,
        "selfHealing",
        building.selfHealing,
        true
    );
    writeIntProperty(output, indent + 2, "cost", building.cost, false);

    writeIndent(output, indent);
    output << "}";
}

void writeWeapon(std::ostream& output, const WeaponType& weapon, int indent) {
    writeIndent(output, indent);
    output << "{\n";

    writeStringProperty(output, indent + 2, "id", weapon.id, true);
    writeIntProperty(output, indent + 2, "damage", weapon.damage, true);
    writeIntProperty(output, indent + 2, "rof", weapon.rof, true);
    writeDoubleProperty(output, indent + 2, "range", weapon.range, true);
    writeStringProperty(output, indent + 2, "projectile", weapon.projectile, true);
    writeIntProperty(output, indent + 2, "speed", weapon.speed, true);
    writeStringProperty(output, indent + 2, "warhead", weapon.warhead, true);
    writeIntProperty(output, indent + 2, "burst", weapon.burst, true);
    writeStringProperty(output, indent + 2, "report", weapon.report, true);
    writeStringProperty(output, indent + 2, "anim", weapon.anim, true);
    writeBoolProperty(output, indent + 2, "bright", weapon.bright, true);
    writeBoolProperty(output, indent + 2, "decloakToFire", weapon.decloakToFire, true);
    writeBoolProperty(output, indent + 2, "cellRangefinding", weapon.cellRangefinding, true);
    writeBoolProperty(output, indent + 2, "fireOnce", weapon.fireOnce, true);
    writeBoolProperty(output, indent + 2, "areaFire", weapon.areaFire, false);

    writeIndent(output, indent);
    output << "}";
}

void writeProjectile(std::ostream& output, const ProjectileType& projectile, int indent) {
    writeIndent(output, indent);
    output << "{\n";

    writeStringProperty(output, indent + 2, "id", projectile.id, true);
    writeStringProperty(output, indent + 2, "image", projectile.image, true);
    writeBoolProperty(output, indent + 2, "arcing", projectile.arcing, true);
    writeIntProperty(output, indent + 2, "rot", projectile.rot, true);
    writeBoolProperty(output, indent + 2, "aa", projectile.aa, true);
    writeBoolProperty(output, indent + 2, "ag", projectile.ag, true);
    writeBoolProperty(output, indent + 2, "animates", projectile.animates, true);
    writeBoolProperty(output, indent + 2, "inaccurate", projectile.inaccurate, true);
    writeBoolProperty(output, indent + 2, "subjectToCliffs", projectile.subjectToCliffs, true);
    writeBoolProperty(output, indent + 2, "subjectToElevation", projectile.subjectToElevation, true);
    writeBoolProperty(output, indent + 2, "subjectToWalls", projectile.subjectToWalls, true);
    writeBoolProperty(output, indent + 2, "dropping", projectile.dropping, true);
    writeBoolProperty(output, indent + 2, "airburst", projectile.airburst, true);
    writeStringProperty(
        output,
        indent + 2,
        "airburstWeapon",
        projectile.airburstWeapon,
        true
    );
    writeBoolProperty(output, indent + 2, "shadow", projectile.shadow, true);
    writeBoolProperty(output, indent + 2, "high", projectile.high, true);
    writeBoolProperty(output, indent + 2, "veryHigh", projectile.veryHigh, true);
    writeIntProperty(output, indent + 2, "frames", projectile.frames, true);
    writeIntProperty(output, indent + 2, "translucency", projectile.translucency, true);
    writeIntProperty(output, indent + 2, "cluster", projectile.cluster, true);
    writeIntProperty(output, indent + 2, "acceleration", projectile.acceleration, true);
    writeStringProperty(output, indent + 2, "trailer", projectile.trailer, false);

    writeIndent(output, indent);
    output << "}";
}

void writeElevationModel(
    std::ostream& output,
    const ElevationModelRules& model,
    int indent,
    bool hasComma
) {
    writeIndent(output, indent);
    output << "\"elevationModel\": {\n";
    writeIntProperty(output, indent + 2, "increment", model.increment, true);
    writeDoubleProperty(
        output,
        indent + 2,
        "incrementBonus",
        model.incrementBonus,
        true
    );
    writeDoubleProperty(
        output,
        indent + 2,
        "bonusCap",
        model.bonusCap,
        false
    );
    writeIndent(output, indent);
    output << "}";
    if (hasComma) {
        output << ",";
    }
    output << "\n";
}

void writeWarhead(std::ostream& output, const WarheadType& warhead, int indent) {
    writeIndent(output, indent);
    output << "{\n";

    writeStringProperty(output, indent + 2, "id", warhead.id, true);
    writeDoubleProperty(output, indent + 2, "cellSpread", warhead.cellSpread, true);
    writeDoubleProperty(output, indent + 2, "percentAtMax", warhead.percentAtMax, true);
    writeStringProperty(output, indent + 2, "verses", warhead.verses, true);
    writeStringProperty(output, indent + 2, "animList", warhead.animList, true);
    writeIntProperty(output, indent + 2, "infDeath", warhead.infDeath, true);
    writeStringProperty(output, indent + 2, "proneDamage", warhead.proneDamage, true);
    writeDoubleProperty(
        output,
        indent + 2,
        "proneDamageMultiplier",
        warhead.proneDamageMultiplier,
        true
    );
    writeBoolProperty(output, indent + 2, "wall", warhead.wall, true);
    writeBoolProperty(output, indent + 2, "wood", warhead.wood, true);
    writeBoolProperty(output, indent + 2, "affectsAllies", warhead.affectsAllies, false);

    writeIndent(output, indent);
    output << "}";
}

void writeDiagnostic(std::ostream& output, const Diagnostic& diagnostic, int indent) {
    writeIndent(output, indent);
    output << "{\n";

    writeStringProperty(output, indent + 2, "severity", toString(diagnostic.severity), true);
    writeStringProperty(output, indent + 2, "source", diagnostic.source, true);
    writeStringProperty(output, indent + 2, "field", diagnostic.field, true);
    writeStringProperty(output, indent + 2, "target", diagnostic.target, true);
    writeStringProperty(output, indent + 2, "message", diagnostic.message, false);

    writeIndent(output, indent);
    output << "}";
}

void writeArtType(std::ostream& output, const ArtType& art, int indent) {
    writeIndent(output, indent);
    output << "{\n";

    writeStringProperty(output, indent + 2, "id", art.id, true);
    writeStringProperty(output, indent + 2, "image", art.image, true);
    writeStringProperty(output, indent + 2, "cameo", art.cameo, true);
    writeStringProperty(output, indent + 2, "altCameo", art.altCameo, true);
    writeStringProperty(output, indent + 2, "sequence", art.sequence, true);
    writeStringProperty(output, indent + 2, "foundation", art.foundation, true);
    writeBoolProperty(output, indent + 2, "newTheater", art.newTheater, true);
    writeBoolProperty(output, indent + 2, "remapable", art.remapable, true);
    writeBoolProperty(output, indent + 2, "normalized", art.normalized, true);
    writeBoolProperty(output, indent + 2, "voxel", art.voxel, true);
    writeStringProperty(output, indent + 2, "turretOffset", art.turretOffset, true);
    writeStringProperty(output, indent + 2, "primaryFireFLH", art.primaryFireFLH, true);
    writeStringProperty(output, indent + 2, "secondaryFireFLH", art.secondaryFireFLH, true);
    writeIntProperty(output, indent + 2, "walkFrames", art.walkFrames, true);
    writeIntProperty(output, indent + 2, "firingFrames", art.firingFrames, true);
    writeIntProperty(output, indent + 2, "standingFrames", art.standingFrames, true);
    writeIntProperty(output, indent + 2, "startWalkFrame", art.startWalkFrame, true);
    writeIntProperty(output, indent + 2, "facings", art.facings, true);
    writeStringProperty(output, indent + 2, "trailerAnim", art.trailerAnim, true);
    writeIntProperty(output, indent + 2, "rate", art.rate, true);
    writeBoolProperty(
        output,
        indent + 2,
        "shadowSpecified",
        art.shadowSpecified,
        true
    );
    writeBoolProperty(output, indent + 2, "shadow", art.shadow, true);
    writeIntProperty(
        output,
        indent + 2,
        "shadowIndex",
        std::max(0, art.shadowIndex),
        true
    );
    writeBoolProperty(
        output,
        indent + 2,
        "useTurretShadow",
        art.useTurretShadow,
        false
    );

    writeIndent(output, indent);
    output << "}";
}

void writeArtObjectLink(std::ostream& output, const ArtObjectLink& link, int indent) {
    writeIndent(output, indent);
    output << "{\n";

    writeStringProperty(output, indent + 2, "objectKind", link.objectKind, true);
    writeStringProperty(output, indent + 2, "objectId", link.objectId, true);
    writeStringProperty(output, indent + 2, "artId", link.artId, true);
    writeBoolProperty(output, indent + 2, "hasArt", link.hasArt, true);
    writeStringProperty(output, indent + 2, "image", link.image, true);
    writeStringProperty(output, indent + 2, "cameo", link.cameo, true);
    writeStringProperty(output, indent + 2, "altCameo", link.altCameo, true);
    writeStringProperty(output, indent + 2, "sequence", link.sequence, true);
    writeStringProperty(output, indent + 2, "foundation", link.foundation, true);
    writeBoolProperty(output, indent + 2, "voxel", link.voxel, true);
    writeBoolProperty(
        output,
        indent + 2,
        "shadowSpecified",
        link.shadowSpecified,
        true
    );
    writeBoolProperty(output, indent + 2, "shadow", link.shadow, true);
    writeIntProperty(
        output,
        indent + 2,
        "shadowIndex",
        std::max(0, link.shadowIndex),
        true
    );
    writeBoolProperty(
        output,
        indent + 2,
        "useTurretShadow",
        link.useTurretShadow,
        false
    );

    writeIndent(output, indent);
    output << "}";
}

template <typename T, typename Writer>
void writeObjectArray(
    std::ostream& output,
    int indent,
    const std::string& key,
    const std::vector<T>& values,
    Writer writer,
    bool hasComma
) {
    writeIndent(output, indent);
    output << "\"" << key << "\": [\n";

    for (std::size_t i = 0; i < values.size(); ++i) {
        writer(output, values[i], indent + 2);
        if (i + 1 < values.size()) {
            output << ",";
        }
        output << "\n";
    }

    writeIndent(output, indent);
    output << "]";
    if (hasComma) {
        output << ",";
    }
    output << "\n";
}

void writeStringArray(
    std::ostream& output,
    int indent,
    const std::string& key,
    const std::vector<std::string>& values,
    bool hasComma
) {
    writeIndent(output, indent);
    output << "\"" << key << "\": [\n";

    for (std::size_t i = 0; i < values.size(); ++i) {
        writeIndent(output, indent + 2);
        output << "\"" << jsonEscape(values[i]) << "\"";
        if (i + 1 < values.size()) {
            output << ",";
        }
        output << "\n";
    }

    writeIndent(output, indent);
    output << "]";
    if (hasComma) {
        output << ",";
    }
    output << "\n";
}

bool ensureParentDirectoryExists(
    const std::string& outputPath,
    std::string& errorMessage
) {
    const std::filesystem::path path(outputPath);
    const std::filesystem::path parent = path.parent_path();

    if (parent.empty()) {
        return true;
    }

    try {
        std::filesystem::create_directories(parent);
    } catch (const std::exception& exception) {
        errorMessage = "Failed to create output directory: ";
        errorMessage += exception.what();
        return false;
    }

    return true;
}

} // namespace

bool exportRulesToJson(
    const std::string& outputPath,
    const RulesDatabase& rules,
    const DiagnosticReport& diagnostics,
    const ArtDatabase* artDatabase,
    std::string& errorMessage
) {
    if (outputPath.empty()) {
        errorMessage = "Output path is empty.";
        return false;
    }

    if (!ensureParentDirectoryExists(outputPath, errorMessage)) {
        return false;
    }

    std::ofstream output(outputPath);
    if (!output.is_open()) {
        errorMessage = "Failed to open JSON output file: " + outputPath;
        return false;
    }

    output << "{\n";
    writeObjectArray(output, 2, "infantry", rules.getInfantryTypes(), writeUnit, true);
    writeObjectArray(output, 2, "vehicles", rules.getVehicleTypes(), writeUnit, true);
    writeObjectArray(output, 2, "aircraft", rules.getAircraftTypes(), writeUnit, true);
    writeObjectArray(output, 2, "buildings", rules.getBuildingTypes(), writeBuilding, true);
    writeObjectArray(output, 2, "weapons", rules.getWeaponTypes(), writeWeapon, true);
    writeObjectArray(output, 2, "projectiles", rules.getProjectileTypes(), writeProjectile, true);
    writeObjectArray(output, 2, "warheads", rules.getWarheadTypes(), writeWarhead, true);
    writeElevationModel(output, rules.getElevationModelRules(), 2, true);
    writeStringArray(output, 2, "warnings", rules.getWarnings(), true);
    writeObjectArray(output, 2, "diagnostics", diagnostics.diagnostics, writeDiagnostic, artDatabase != nullptr);

    if (artDatabase != nullptr) {
        const std::vector<ArtObjectLink> artLinks = artDatabase->buildLinks(rules);
        writeObjectArray(output, 2, "art", artDatabase->getArtTypes(), writeArtType, true);
        writeObjectArray(output, 2, "artLinks", artLinks, writeArtObjectLink, false);
    }

    output << "}\n";

    if (!output.good()) {
        errorMessage = "Failed while writing JSON output file: " + outputPath;
        return false;
    }

    return true;
}
