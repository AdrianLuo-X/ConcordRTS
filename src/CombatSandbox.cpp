#include "CombatSandbox.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

const UnitType* findUnitType(const RulesDatabase& rules, const std::string& id) {
    const auto findIn = [&id](const std::vector<UnitType>& units) -> const UnitType* {
        const auto it = std::find_if(
            units.begin(),
            units.end(),
            [&id](const UnitType& unit) {
                return unit.id == id;
            }
        );
        return it == units.end() ? nullptr : &*it;
    };

    if (const UnitType* unit = findIn(rules.getInfantryTypes())) {
        return unit;
    }
    if (const UnitType* unit = findIn(rules.getVehicleTypes())) {
        return unit;
    }
    return findIn(rules.getAircraftTypes());
}

const WeaponType* findWeaponType(const RulesDatabase& rules, const std::string& id) {
    const auto& weapons = rules.getWeaponTypes();
    const auto it = std::find_if(
        weapons.begin(),
        weapons.end(),
        [&id](const WeaponType& weapon) {
            return weapon.id == id;
        }
    );
    return it == weapons.end() ? nullptr : &*it;
}

const WarheadType* findWarheadType(const RulesDatabase& rules, const std::string& id) {
    const auto& warheads = rules.getWarheadTypes();
    const auto it = std::find_if(
        warheads.begin(),
        warheads.end(),
        [&id](const WarheadType& warhead) {
            return warhead.id == id;
        }
    );
    return it == warheads.end() ? nullptr : &*it;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<double> parseVerses(const std::string& verses) {
    std::vector<double> values;
    std::stringstream input(verses);
    std::string token;

    while (std::getline(input, token, ',')) {
        token = trim(token);
        if (!token.empty() && token.back() == '%') {
            token.pop_back();
        }

        try {
            values.push_back(std::stod(token) / 100.0);
        } catch (...) {
            values.push_back(1.0);
        }
    }

    return values;
}

int armorIndex(const std::string& armor) {
    static const std::vector<std::string> armorOrder = {
        "none",
        "flak",
        "plate",
        "light",
        "medium",
        "heavy",
        "wood",
        "steel",
        "concrete",
        "special_1",
        "special_2"
    };

    std::string lower = armor;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );

    const auto it = std::find(armorOrder.begin(), armorOrder.end(), lower);
    return it == armorOrder.end()
        ? -1
        : static_cast<int>(std::distance(armorOrder.begin(), it));
}

double damageMultiplier(const WarheadType* warhead, const std::string& armor) {
    if (warhead == nullptr || warhead->verses.empty()) {
        return 1.0;
    }

    const std::vector<double> verses = parseVerses(warhead->verses);
    const int index = armorIndex(armor);
    if (index < 0 || static_cast<std::size_t>(index) >= verses.size()) {
        return 1.0;
    }

    return verses[static_cast<std::size_t>(index)];
}

std::string formatPoint(const CombatGridPoint& point) {
    return "(" + std::to_string(point.x) + "," + std::to_string(point.y) + ")";
}

bool operator==(const CombatGridPoint& left, const CombatGridPoint& right) {
    return left.x == right.x && left.y == right.y;
}

double distanceCells(const CombatGridPoint& left, const CombatGridPoint& right) {
    const double dx = static_cast<double>(left.x - right.x);
    const double dy = static_cast<double>(left.y - right.y);
    return std::sqrt((dx * dx) + (dy * dy));
}

struct UnitCommand {
    enum class Kind {
        None,
        Move,
        Attack
    };

    Kind kind = Kind::None;
    CombatGridPoint moveTarget;
    int targetInstanceId = 0;
};

struct CombatGameObject {
    int instanceId = 0;
    CombatGridPoint position;
    bool selected = false;
    bool alive = true;
};

struct UnitInstance {
    CombatGameObject object;
    const UnitType* type = nullptr;
    const WeaponType* weapon = nullptr;
    const WarheadType* warhead = nullptr;
    UnitCommand command;
    int hp = 0;
    int weaponCooldownTicks = 0;
    int moveCooldownTicks = 0;
};

struct CombatGridPointLess {
    bool operator()(const CombatGridPoint& left, const CombatGridPoint& right) const {
        if (left.y != right.y) {
            return left.y < right.y;
        }
        return left.x < right.x;
    }
};

bool loadUnitInstance(
    const RulesDatabase& rules,
    int instanceId,
    const std::string& unitId,
    CombatGridPoint position,
    bool selected,
    UnitInstance& instance,
    std::string& errorMessage
) {
    instance = UnitInstance{};
    instance.object.instanceId = instanceId;
    instance.object.position = position;
    instance.object.selected = selected;
    instance.type = findUnitType(rules, unitId);
    if (instance.type == nullptr) {
        errorMessage = "Unknown unit type: " + unitId;
        return false;
    }

    std::string equippedWeaponId = instance.type->primaryWeapon;
    if (equippedWeaponId.empty()
        && instance.type->indexedWeapons.size() == 1U) {
        equippedWeaponId = instance.type->indexedWeapons.front();
    }
    if (equippedWeaponId.empty()) {
        errorMessage = "Unit " + unitId + " has no equipped weapon.";
        return false;
    }

    instance.weapon = findWeaponType(rules, equippedWeaponId);
    if (instance.weapon == nullptr) {
        errorMessage =
            "Unit " + unitId + " references missing equipped weapon ["
            + equippedWeaponId + "].";
        return false;
    }

    instance.warhead = findWarheadType(rules, instance.weapon->warhead);
    instance.hp = instance.type->strength;
    return true;
}

int movementIntervalTicks(const UnitInstance& unit) {
    if (unit.type == nullptr || unit.type->speed <= 0) {
        return std::numeric_limits<int>::max();
    }

    return std::max(1, static_cast<int>(std::round(60.0 / unit.type->speed)));
}

int calculateDamage(const UnitInstance& attacker, const UnitInstance& defender) {
    const int burst = attacker.weapon->burst <= 0 ? 1 : attacker.weapon->burst;
    const double multiplier =
        damageMultiplier(attacker.warhead, defender.type->armor);
    return std::max(
        1,
        static_cast<int>(std::round(
            static_cast<double>(attacker.weapon->damage * burst) * multiplier
        ))
    );
}

void appendEvent(
    CombatSandboxReport& report,
    int tick,
    const std::string& text
) {
    report.events.push_back({tick, text});
}

int clampGridSize(int startDistanceCells) {
    return std::max(12, startDistanceCells + 5);
}

bool isInsideGrid(const CombatGridPoint& point, int width, int height) {
    return point.x >= 0
        && point.y >= 0
        && point.x < width
        && point.y < height;
}

std::vector<CombatGridPoint> neighborsOf(const CombatGridPoint& point) {
    return {
        {point.x + 1, point.y},
        {point.x - 1, point.y},
        {point.x, point.y + 1},
        {point.x, point.y - 1}
    };
}

std::optional<CombatGridPoint> findNextStepTowardRange(
    const UnitInstance& unit,
    const UnitInstance& target,
    int width,
    int height
) {
    if (distanceCells(unit.object.position, target.object.position) <= unit.weapon->range) {
        return std::nullopt;
    }

    std::deque<CombatGridPoint> frontier;
    std::map<CombatGridPoint, CombatGridPoint, CombatGridPointLess> previous;
    frontier.push_back(unit.object.position);
    previous[unit.object.position] = unit.object.position;

    while (!frontier.empty()) {
        const CombatGridPoint current = frontier.front();
        frontier.pop_front();

        if (!(current == unit.object.position)
            && distanceCells(current, target.object.position) <= unit.weapon->range
        ) {
            CombatGridPoint step = current;
            while (!(previous[step] == unit.object.position)) {
                step = previous[step];
            }
            return step;
        }

        for (const CombatGridPoint& next : neighborsOf(current)) {
            if (!isInsideGrid(next, width, height)) {
                continue;
            }
            if (next == target.object.position) {
                continue;
            }
            if (previous.find(next) != previous.end()) {
                continue;
            }
            previous[next] = current;
            frontier.push_back(next);
        }
    }

    return std::nullopt;
}

std::optional<CombatGridPoint> findNextStepToPoint(
    const CombatGridPoint& start,
    const CombatGridPoint& goal,
    int width,
    int height
) {
    if (start == goal) {
        return std::nullopt;
    }

    std::deque<CombatGridPoint> frontier;
    std::map<CombatGridPoint, CombatGridPoint, CombatGridPointLess> previous;
    frontier.push_back(start);
    previous[start] = start;

    while (!frontier.empty()) {
        const CombatGridPoint current = frontier.front();
        frontier.pop_front();

        if (current == goal) {
            CombatGridPoint step = current;
            while (!(previous[step] == start)) {
                step = previous[step];
            }
            return step;
        }

        for (const CombatGridPoint& next : neighborsOf(current)) {
            if (!isInsideGrid(next, width, height)) {
                continue;
            }
            if (previous.find(next) != previous.end()) {
                continue;
            }
            previous[next] = current;
            frontier.push_back(next);
        }
    }

    return std::nullopt;
}

bool tryMoveUnit(
    CombatSandboxReport& report,
    int tick,
    UnitInstance& unit,
    CombatGridPoint next,
    const std::string& reason,
    int width,
    int height
) {
    (void)width;
    (void)height;

    if (unit.moveCooldownTicks > 0) {
        --unit.moveCooldownTicks;
        return false;
    }

    unit.object.position = next;
    unit.moveCooldownTicks = movementIntervalTicks(unit) - 1;

    std::ostringstream line;
    line << "#"
         << unit.object.instanceId
         << " "
         << unit.type->id
         << " moves to "
         << formatPoint(unit.object.position)
         << " "
         << reason;
    appendEvent(report, tick, line.str());
    return true;
}

bool fireWeapon(
    CombatSandboxReport& report,
    int tick,
    UnitInstance& attacker,
    UnitInstance& defender
) {
    if (attacker.weaponCooldownTicks > 0) {
        --attacker.weaponCooldownTicks;
        return false;
    }

    if (distanceCells(attacker.object.position, defender.object.position) > attacker.weapon->range) {
        return false;
    }

    const int damage = calculateDamage(attacker, defender);
    defender.hp -= damage;
    attacker.weaponCooldownTicks = std::max(0, attacker.weapon->rof - 1);

    std::ostringstream line;
    line << "#"
         << attacker.object.instanceId
         << " "
         << attacker.type->id
         << " fires "
         << attacker.weapon->id
         << " ("
         << attacker.weapon->warhead
         << ") at #"
         << defender.object.instanceId
         << " "
         << defender.type->id
         << " for "
         << damage
         << " damage; "
         << defender.type->id
         << " HP="
         << std::max(0, defender.hp);
    appendEvent(report, tick, line.str());

    if (defender.hp <= 0 && defender.object.alive) {
        defender.object.alive = false;
        std::ostringstream death;
        death << "#"
              << defender.object.instanceId
              << " "
              << defender.type->id
              << " destroyed and removed from combat";
        appendEvent(report, tick, death.str());
        return true;
    }

    return false;
}

bool processUnitTick(
    CombatSandboxReport& report,
    int tick,
    UnitInstance& unit,
    UnitInstance& target,
    int width,
    int height
) {
    if (!unit.object.alive || !target.object.alive) {
        return false;
    }

    if (unit.command.kind == UnitCommand::Kind::Move) {
        if (unit.object.position == unit.command.moveTarget) {
            unit.command.kind = UnitCommand::Kind::None;
            appendEvent(
                report,
                tick,
                "#" + std::to_string(unit.object.instanceId)
                    + " " + unit.type->id + " move command complete"
            );
            return false;
        }

        const std::optional<CombatGridPoint> next =
            findNextStepToPoint(
                unit.object.position,
                unit.command.moveTarget,
                width,
                height
            );
        if (next.has_value()) {
            tryMoveUnit(report, tick, unit, *next, "toward move target", width, height);
        }
        return false;
    }

    if (unit.command.kind != UnitCommand::Kind::Attack) {
        return false;
    }

    const std::optional<CombatGridPoint> next =
        findNextStepTowardRange(unit, target, width, height);
    if (next.has_value()) {
        tryMoveUnit(report, tick, unit, *next, "toward weapon range", width, height);
    }
    return fireWeapon(report, tick, unit, target);
}

void issueAttackCommand(
    CombatSandboxReport& report,
    int tick,
    UnitInstance& unit,
    const UnitInstance& target
) {
    unit.command.kind = UnitCommand::Kind::Attack;
    unit.command.targetInstanceId = target.object.instanceId;

    std::ostringstream line;
    line << "#"
         << unit.object.instanceId
         << " "
         << unit.type->id
         << " attack command -> #"
         << target.object.instanceId
         << " "
         << target.type->id;
    appendEvent(report, tick, line.str());
}

} // namespace

CombatSandboxReport runCombatSandbox(
    const RulesDatabase& rules,
    const CombatSandboxConfig& config
) {
    CombatSandboxReport report;
    UnitInstance attacker;
    UnitInstance defender;

    const int startDistance = std::max(1, config.startDistanceCells);
    const int gridWidth = clampGridSize(startDistance);
    constexpr int gridHeight = 5;
    const CombatGridPoint attackerStart{1, 2};
    const CombatGridPoint defenderStart{1 + startDistance, 2};

    if (!loadUnitInstance(
            rules,
            report.attackerInstanceId,
            config.attackerId,
            attackerStart,
            true,
            attacker,
            report.errorMessage
        )) {
        return report;
    }

    if (!loadUnitInstance(
            rules,
            report.defenderInstanceId,
            config.defenderId,
            defenderStart,
            false,
            defender,
            report.errorMessage
        )) {
        return report;
    }

    report.success = true;
    report.attackerId = attacker.type->id;
    report.defenderId = defender.type->id;
    report.attackerStartingHp = attacker.hp;
    report.defenderStartingHp = defender.hp;
    report.attackerStartPosition = attacker.object.position;
    report.defenderStartPosition = defender.object.position;
    report.selectedInstanceId = attacker.object.instanceId;

    appendEvent(
        report,
        0,
        "selected #" + std::to_string(attacker.object.instanceId)
            + " " + attacker.type->id + " at " + formatPoint(attacker.object.position)
    );
    issueAttackCommand(report, 0, attacker, defender);
    issueAttackCommand(report, 0, defender, attacker);

    const int maxTicks = std::max(1, config.maxTicks);
    for (int tick = 0; tick < maxTicks; ++tick) {
        report.ticksSimulated = tick + 1;

        if (processUnitTick(report, tick, attacker, defender, gridWidth, gridHeight)) {
            break;
        }

        if (processUnitTick(report, tick, defender, attacker, gridWidth, gridHeight)) {
            break;
        }

        if (!attacker.object.alive || !defender.object.alive) {
            break;
        }
    }

    report.attackerFinalHp = std::max(0, attacker.hp);
    report.defenderFinalHp = std::max(0, defender.hp);
    report.attackerFinalPosition = attacker.object.position;
    report.defenderFinalPosition = defender.object.position;
    report.attackerAlive = attacker.object.alive;
    report.defenderAlive = defender.object.alive;

    if (!attacker.object.alive && !defender.object.alive) {
        report.winner = "draw";
    } else if (!attacker.object.alive) {
        report.winner = defender.type->id;
    } else if (!defender.object.alive) {
        report.winner = attacker.type->id;
    } else {
        report.winner = "none";
    }

    return report;
}
