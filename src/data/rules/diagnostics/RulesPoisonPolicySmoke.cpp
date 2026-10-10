#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <iostream>
#include <string>

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) std::cerr << "poison rules: " << message << '\n';
    return condition;
}

const char* fixture = R"INI(
[InfantryTypes]
0=ProtectedInfantry
1=OrdinaryInfantry
[VehicleTypes]
0=ProtectedVehicle
[AircraftTypes]
0=OrdinaryAircraft
[BuildingTypes]
0=ProtectedBuilding
1=OrdinaryBuilding
[ProtectedInfantry]
Strength=100
ImmuneToPoison=yes
Primary=RenamedEmitter
[OrdinaryInfantry]
Strength=100
[ProtectedVehicle]
Strength=100
ImmuneToPoison=yes
[OrdinaryAircraft]
Strength=100
ImmuneToPoison=no
[ProtectedBuilding]
Strength=100
ImmuneToPoison=yes
[OrdinaryBuilding]
Strength=100
ImmuneToPoison=no
[RenamedEmitter]
Damage=5
Projectile=Invisible
Warhead=aRbItRaRyClOuD
[Invisible]
Inviso=yes
[ArbitraryCloud]
Poison=yes
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
[LimitedCloud]
Poison=yes
AffectsAllies=no
[ExplicitCloud]
Poison=yes
AffectsAllies=yes
[FingerprintCloud]
Poison=yes
AffectsAllies=yes
[Virus]
InfDeath=8
[GenericGas]
Particle=GenericCloudSystem
Poison=no
[ParticleSystems]
0=GenericCloudSystem
[GenericCloudSystem]
BehavesLike=Gas
HoldsWhat=GenericCloud
[Particles]
0=GenericCloud
[GenericCloud]
BehavesLike=Gas
Warhead=GenericGas
Damage=5
MaxDC=30
MaxEC=1000
)INI";

const char* warheadList = R"INI(
[Warheads]
0=ArbitraryCloud
1=LimitedCloud
2=ExplicitCloud
3=FingerprintCloud
4=Virus
5=GenericGas
)INI";

bool checkWarhead(const RulesDatabase& rules, const char* id,
    bool poison, bool affectsAllies) {
    const auto* type = rules.findWarheadType(id);
    return expect(type && type->poison == poison && type->affectsAllies == affectsAllies
        && type->profile.poison == poison && type->profile.affectsAllies == affectsAllies
        && type->profile.affectsEnemies == type->affectsEnemies,
        std::string(id) + " declaration, ally override or public profile differs");
}

bool syntheticContract() {
    bool ok = true;
    // A non-retail name is discovered through an actual weapon reference;
    // explicit registration must have exactly the same declared policy.
    for (bool listed : {false, true}) {
        IniFile ini;
        if (!expect(ini.loadFromString(std::string(fixture) + warheadList),
                "in-memory fixture did not parse")) return false;
        if (!listed) {
            // The unlisted path is a minimal independent weapon closure.
            if (!ini.loadFromString(R"INI(
[InfantryTypes]
0=Carrier
[Carrier]
Strength=100
Primary=RenamedEmitter
[RenamedEmitter]
Damage=5
Projectile=Invisible
Warhead=aRbItRaRyClOuD
[Invisible]
Inviso=yes
[ArbitraryCloud]
Poison=yes
)INI")) return false;
        }
        RulesDatabase rules;
        if (!expect(rules.loadFromIni(ini), "in-memory rules did not load")) return false;
        ok &= checkWarhead(rules, "ArbitraryCloud", true, true);
        if (!listed) continue;
        ok &= checkWarhead(rules, "LimitedCloud", true, false);
        ok &= checkWarhead(rules, "ExplicitCloud", true, true);
        ok &= checkWarhead(rules, "Virus", false, false);
        ok &= checkWarhead(rules, "GenericGas", false, false);
        const auto* infantry = rules.findUnitType("infantry", "ProtectedInfantry");
        const auto* ordinary = rules.findUnitType("infantry", "OrdinaryInfantry");
        const auto* vehicle = rules.findUnitType("vehicle", "ProtectedVehicle");
        const auto* aircraft = rules.findUnitType("aircraft", "OrdinaryAircraft");
        const auto* building = rules.findBuildingType("ProtectedBuilding");
        const auto* ordinaryBuilding = rules.findBuildingType("OrdinaryBuilding");
        ok &= expect(infantry && infantry->immuneToPoison && ordinary && !ordinary->immuneToPoison
            && vehicle && vehicle->immuneToPoison && aircraft && !aircraft->immuneToPoison
            && building && building->immuneToPoison && ordinaryBuilding && !ordinaryBuilding->immuneToPoison,
            "typed unit/building immunity declarations and defaults differ");

        // Keep AffectsAllies explicitly yes on both sides of the poison
        // comparison: fingerprint changes must cover poison itself.
        for (const std::string& patch : {
                "[FingerprintCloud]\nPoison=no\n",
                "[ProtectedInfantry]\nImmuneToPoison=no\n",
                "[ProtectedBuilding]\nImmuneToPoison=no\n"}) {
            IniFile changed = ini, overlay;
            RulesDatabase revised;
            if (!overlay.loadFromString(patch)) return false;
            changed.appendFrom(overlay);
            if (!expect(revised.loadFromIni(changed), "fingerprint variant did not load")) return false;
            const auto* before = rules.findWarheadType("FingerprintCloud");
            const auto* after = revised.findWarheadType("FingerprintCloud");
            ok &= expect(before && after && before->affectsAllies && after->affectsAllies,
                "fingerprint probe accidentally changed the explicit ally policy");
            if (patch.find("[FingerprintCloud]") == 0) {
                ok &= expect(after && !after->poison && !after->profile.poison,
                    "fingerprint poison variant did not change its declared property");
            } else if (patch.find("[ProtectedInfantry]") == 0) {
                const auto* changedUnit = revised.findUnitType("infantry", "ProtectedInfantry");
                ok &= expect(changedUnit && !changedUnit->immuneToPoison,
                    "fingerprint unit variant did not change its declared immunity");
            } else {
                const auto* changedBuilding = revised.findBuildingType("ProtectedBuilding");
                ok &= expect(changedBuilding && !changedBuilding->immuneToPoison,
                    "fingerprint building variant did not change its declared immunity");
            }
            ok &= expect(rules.getDetailRulesHash() != revised.getDetailRulesHash(),
                "functional fingerprint omitted a poison/immunity declaration: " + patch);
        }
    }
    return ok;
}

bool stockContract(const std::string& path) {
    IniFile ini;
    RulesDatabase rules;
    if (!expect(ini.loadFromFile(path) && rules.loadFromIni(ini), "original stock rules did not load")) return false;
    bool ok = checkWarhead(rules, "VirusGas", true, true)
        && checkWarhead(rules, "Virus", false, false)
        && checkWarhead(rules, "Gas", false, false);
    int poisonWarheads = 0, immuneUnits = 0, immuneBuildings = 0;
    for (const auto& type : rules.getWarheadTypes()) if (type.poison) {
        ++poisonWarheads;
        ok &= expect(type.id == "VirusGas", "unexpected original poison warhead: " + type.id);
    }
    const auto countUnits = [&](const auto& types) {
        for (const auto& type : types) if (type.immuneToPoison) {
            ++immuneUnits;
            ok &= expect(type.id == "VIRUS", "unexpected original poison-immune unit: " + type.id);
        }
    };
    countUnits(rules.getInfantryTypes());
    countUnits(rules.getVehicleTypes());
    countUnits(rules.getAircraftTypes());
    for (const auto& type : rules.getBuildingTypes()) if (type.immuneToPoison) ++immuneBuildings;
    ok &= expect(poisonWarheads == 1 && immuneUnits == 1 && immuneBuildings == 0,
        "stock poison/immunity inventory differs from original declarations");
    const auto* virus = rules.findUnitType("infantry", "VIRUS");
    const auto* weapon = rules.findWeaponType("Virusgun");
    const auto* cloud = rules.findParticleType("VirusCloud1");
    const auto* fade = rules.findParticleType("VirusCloudD1");
    ok &= expect(virus && virus->strength == 100 && virus->immuneToPoison
        && weapon && weapon->damage == 125 && weapon->warhead == "Virus"
        && cloud && cloud->damage == 5 && cloud->maxDamageDelay == 30 && cloud->maxLifetime == 1000
        && cloud->radius == 0 && cloud->warhead == "VirusGas" && cloud->nextParticle == "VirusCloudD1"
        && fade && fade->damage == 5 && fade->maxDamageDelay == 60 && fade->maxLifetime == 50,
        "original HP, rifle, poison cadence/lifetime or dissipation declarations changed");
    return ok;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: concord_rules_poison_policy_smoke <original rulesmd.ini>\n";
        return 2;
    }
    if (!syntheticContract() || !stockContract(argv[1])) return 1;
    std::cout << "Poison rules policy passed: renamed/list/discovered/default/explicit-no/yes, "
        "unit/building immunity, three independent fingerprints, stock declarations preserved\n";
    return 0;
}
