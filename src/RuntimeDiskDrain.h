#pragma once

#include "GameWorld.h"
#include "IniFile.h"
#include "RulesDatabase.h"

#include <charconv>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>

inline int runtimeDiskDrainNextMoneyTick(int tick, int delayFrames, int fixedTickHz) {
    const std::int64_t hz = std::max(1, fixedTickHz);
    const std::int64_t delay = std::max(1, delayFrames);
    const auto frame = std::max<std::int64_t>(0, tick) * 15 / hz;
    const auto nextFrame = (frame / delay + 1) * delay;
    return static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(),
        (nextFrame * hz + 14) / 15));
}

// One reciprocal relationship per source and target, matching the retail
// TechnoClass DrainTarget / DrainingMe pair. Presentation is derived from this
// clock; sound devices and renderer caches never become gameplay authority.
struct RuntimeDiskDrain {
    int targetId = -1;
    int startedTick = 0;
    int lastAdvancedTick = 0;
    int nextMoneyTick = 0;
    std::string owner;
    std::string sourceType;
    std::string targetOwner;
    std::string targetType;
    std::string weaponId;
};

struct RuntimeDiskDrainState {
    std::map<int, RuntimeDiskDrain> sources;

    int sourceForTarget(int targetId) const {
        for (const auto& [id, drain] : sources)
            if (drain.targetId == targetId) return id;
        return -1;
    }

    bool removeObject(int id) {
        bool removed = sources.erase(id) != 0;
        for (auto it = sources.begin(); it != sources.end();) {
            if (it->second.targetId == id) {
                it = sources.erase(it);
                removed = true;
            } else ++it;
        }
        return removed;
    }

    void save(std::ostream& out) const {
        out << "\n[DiskDrains]\nSchema=1\nCount=" << sources.size() << '\n';
        int index = 0;
        for (const auto& [id, drain] : sources) {
            out << "Drain" << index++ << '=' << id << ' ' << drain.targetId
                << ' ' << drain.startedTick << ' ' << drain.lastAdvancedTick
                << ' ' << drain.nextMoneyTick << ' ' << iniHexEncode(drain.owner)
                << ' ' << iniHexEncode(drain.sourceType) << ' ' << iniHexEncode(drain.targetOwner)
                << ' ' << iniHexEncode(drain.targetType) << ' ' << iniHexEncode(drain.weaponId) << '\n';
        }
    }

    bool load(const IniFile& ini, std::string& error) {
        RuntimeDiskDrainState staged;
        if (!ini.getSection("DiskDrains")) { sources.clear(); return true; }
        const auto integer = [](const std::string& text, int& value) {
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        };
        int schema = 0, count = 0;
        if (!integer(ini.getValue("DiskDrains", "Schema"), schema) || schema != 1
            || !integer(ini.getValue("DiskDrains", "Count"), count) || count < 0 || count > 100000) {
            error = "invalid DiskDrains schema/count";
            return false;
        }
        std::set<int> targets;
        for (int index = 0; index < count; ++index) {
            RuntimeDiskDrain drain;
            int id = -1;
            std::string owner, source, targetOwner, target, weapon;
            std::istringstream in(ini.getValue("DiskDrains", "Drain" + std::to_string(index)));
            if (!(in >> id >> drain.targetId >> drain.startedTick >> drain.lastAdvancedTick
                    >> drain.nextMoneyTick >> owner >> source >> targetOwner >> target >> weapon)
                || (in >> std::ws, !in.eof()) || id < 0 || drain.targetId < 0 || id == drain.targetId
                || drain.startedTick < 0 || drain.lastAdvancedTick < drain.startedTick
                || drain.nextMoneyTick < drain.startedTick
                || !iniHexDecode(owner, drain.owner, 1024) || drain.owner.empty()
                || !iniHexDecode(source, drain.sourceType, 1024) || drain.sourceType.empty()
                || !iniHexDecode(targetOwner, drain.targetOwner, 1024) || drain.targetOwner.empty()
                || !iniHexDecode(target, drain.targetType, 1024) || drain.targetType.empty()
                || !iniHexDecode(weapon, drain.weaponId, 1024) || drain.weaponId.empty()
                || !targets.insert(drain.targetId).second
                || !staged.sources.emplace(id, std::move(drain)).second) {
                error = "invalid or duplicate DiskDrains relationship";
                return false;
            }
        }
        sources.swap(staged.sources);
        return true;
    }

    bool validateWorld(const RulesDatabase& rules, const std::vector<WorldObjectInstance>& objects,
        int tick, std::string& error) const {
        std::map<int, const WorldObjectInstance*> byId;
        for (const auto& object : objects) byId.emplace(object.instanceId, &object);
        for (const auto& [id, drain] : sources) {
            const auto s = byId.find(id), t = byId.find(drain.targetId);
            const auto* source = s == byId.end() ? nullptr : s->second;
            const auto* target = t == byId.end() ? nullptr : t->second;
            const auto* weapon = rules.findWeaponType(drain.weaponId);
            const auto* building = target ? rules.findBuildingType(target->type) : nullptr;
            if (!source || !source->alive || source->health <= 0 || !target || !target->alive
                || target->health <= 0 || target->kind != WorldObjectKind::Structure
                || !building || !building->drainable || !weapon || !weapon->drainWeapon
                || source->owner != drain.owner || source->type != drain.sourceType
                || target->owner != drain.targetOwner || target->type != drain.targetType
                || drain.lastAdvancedTick > tick || drain.startedTick > tick
                || static_cast<std::int64_t>(drain.nextMoneyTick) > static_cast<std::int64_t>(tick) + 2400000) {
                error = "DiskDrains relationship does not match live source/target, rules or clock";
                return false;
            }
        }
        return true;
    }
};
