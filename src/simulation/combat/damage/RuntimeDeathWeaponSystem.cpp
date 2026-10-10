#include "simulation/combat/damage/RuntimeDeathWeaponSystem.h"

#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>

namespace {

constexpr std::size_t maximumSavedTriggeredObjects = 262144;
constexpr std::size_t maximumSavedStringBytes = 16384;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

char lowerAscii(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool equalsAsciiIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAscii(left[index]) != lowerAscii(right[index])) {
            return false;
        }
    }
    return true;
}

const WorldObjectInstance* findObject(
    const RuntimeDeathWeaponContext& context,
    int objectId
) {
    if (context.objects == nullptr) {
        return nullptr;
    }
    for (const WorldObjectInstance& object : *context.objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

struct DeathWeaponProfile {
    bool found = false;
    bool explodes = false;
    std::string weaponId;
    double damageModifier = 1.0;
};

DeathWeaponProfile profileForObject(
    const RuntimeDeathWeaponContext& context,
    const WorldObjectInstance& object
) {
    const RulesDatabase& rules = *context.rules;
    if (object.kind == WorldObjectKind::Structure) {
        const BuildingType* type = rules.findBuildingType(object.type);
        if (type == nullptr) {
            return {};
        }
        return {
            true,
            type->explodes,
            type->deathWeapon,
            type->deathWeaponDamageModifier
        };
    }

    const UnitType* type = nullptr;
    if (object.kind == WorldObjectKind::Infantry) {
        type = rules.findUnitType("InfantryType", object.type);
    } else if (object.authoredAircraft) {
        type = rules.findUnitType("AircraftType", object.type);
        if (type == nullptr) {
            type = rules.findUnitType("UnitType", object.type);
        }
    } else {
        type = rules.findUnitType("UnitType", object.type);
        if (type == nullptr) {
            // Produced aircraft use WorldObjectKind::Unit as well.  The map's
            // authoredAircraft flag is only an initial-state hint, so this
            // fallback is required for runtime-created aircraft.
            type = rules.findUnitType("AircraftType", object.type);
        }
    }
    if (type == nullptr) {
        return {};
    }
    std::string deathWeapon = type->deathWeapon;
    if (type->gunner && equalsAsciiIgnoreCase(deathWeapon, "CRNuke")) {
        // Stock FV's CRNuke is only armed by its Ivan gunner mode. Use the
        // actual cargo manifest, not a cached weapon/turret index: an empty
        // vehicle (including one just unloaded) must not emit nuclear fallout.
        const WorldObjectInstance* passenger = object.transportPassengerIds.empty()
            ? nullptr : findObject(context, object.transportPassengerIds.front());
        const UnitType* passengerType = passenger == nullptr ? nullptr
            : rules.findUnitType(passenger->kind == WorldObjectKind::Infantry
                ? "InfantryType" : "UnitType", passenger->type);
        bool armed = false;
        if (passengerType != nullptr && passenger->health > 0) {
            const int mode = std::clamp(passengerType->ifvMode, 0,
                static_cast<int>(type->ifvWeaponIndices.size()) - 1);
            const int authoredIndex = type->ifvWeaponIndices[static_cast<std::size_t>(mode)];
            const int index = authoredIndex < 0 ? mode : authoredIndex;
            armed = index >= 0 && static_cast<std::size_t>(index) < type->indexedWeapons.size()
                && equalsAsciiIgnoreCase(type->indexedWeapons[static_cast<std::size_t>(index)], deathWeapon);
        }
        if (!armed) deathWeapon.clear();
    }
    return {
        true,
        type->explodes,
        std::move(deathWeapon),
        type->deathWeaponDamageModifier
    };
}

int modifiedDamage(int damage, double modifier) {
    const long double value = static_cast<long double>(damage)
        * static_cast<long double>(modifier);
    if (value >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (value <= static_cast<long double>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    // The original integer damage path truncates the multiplied value toward
    // zero.  Retaining that rule matters for small fractional death weapons.
    return static_cast<int>(value);
}

int kindValue(WorldObjectKind kind) {
    switch (kind) {
    case WorldObjectKind::Structure:
        return 0;
    case WorldObjectKind::Unit:
        return 1;
    case WorldObjectKind::Infantry:
        return 2;
    }
    return 0;
}

bool kindFromValue(int value, WorldObjectKind& kind) {
    switch (value) {
    case 0:
        kind = WorldObjectKind::Structure;
        return true;
    case 1:
        kind = WorldObjectKind::Unit;
        return true;
    case 2:
        kind = WorldObjectKind::Infantry;
        return true;
    default:
        return false;
    }
}

std::string hexEncode(std::string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        result.push_back(digits[(byte >> 4U) & 0x0FU]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool hexDecode(const std::string& text, std::string& result) {
    if ((text.size() & 1U) != 0U
        || text.size() / 2U > maximumSavedStringBytes) {
        return false;
    }
    std::string decoded;
    decoded.reserve(text.size() / 2U);
    for (std::size_t index = 0; index < text.size(); index += 2U) {
        const int high = hexDigit(text[index]);
        const int low = hexDigit(text[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    result = std::move(decoded);
    return true;
}

template <typename Integer>
bool parseIntegerStrict(const std::string& text, Integer& result) {
    if (text.empty()) {
        return false;
    }
    Integer parsed{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto conversion = std::from_chars(begin, end, parsed);
    if (conversion.ec != std::errc() || conversion.ptr != end) {
        return false;
    }
    result = parsed;
    return true;
}

bool parseBoolStrict(const std::string& text, bool& result) {
    if (equalsAsciiIgnoreCase(text, "yes")
        || equalsAsciiIgnoreCase(text, "true")
        || text == "1") {
        result = true;
        return true;
    }
    if (equalsAsciiIgnoreCase(text, "no")
        || equalsAsciiIgnoreCase(text, "false")
        || text == "0") {
        result = false;
        return true;
    }
    return false;
}

std::uint64_t hashText(std::uint64_t hash, std::string_view value) {
    for (const unsigned char byte : value) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= fnvPrime;
    }
    return hash;
}

} // namespace

void RuntimeDeathWeaponSystemState::reset() {
    nextRequestId_ = 1;
    triggeredObjects_.clear();
}

RuntimeDeathWeaponResult RuntimeDeathWeaponSystemState::notifyDeath(
    const RuntimeDeathWeaponContext& context,
    const RuntimeDeathWeaponDeathEvent& event
) {
    RuntimeDeathWeaponResult result;
    if (context.rules == nullptr
        || context.objects == nullptr
        || event.currentTick < 0) {
        result.veto = RuntimeDeathWeaponVeto::InvalidContext;
        return result;
    }

    const WorldObjectInstance* object = findObject(context, event.objectId);
    if (object == nullptr) {
        result.veto = RuntimeDeathWeaponVeto::ObjectMissing;
        return result;
    }
    const auto insertion = std::lower_bound(
        triggeredObjects_.begin(), triggeredObjects_.end(), event.objectId,
        [](const RuntimeDeathWeaponTriggeredObject& record, int objectId) {
            return record.objectId < objectId;
        }
    );
    if (insertion != triggeredObjects_.end()
        && insertion->objectId == event.objectId) {
        result.veto = RuntimeDeathWeaponVeto::ObjectAlreadyTriggered;
        return result;
    }

    RuntimeDeathWeaponTriggeredObject triggered;
    triggered.objectId = object->instanceId;
    triggered.kind = object->kind;
    triggered.authoredAircraft = object->authoredAircraft;
    triggered.type = object->type;
    triggered.deathTick = event.currentTick;
    triggeredObjects_.insert(insertion, std::move(triggered));
    result.accepted = true;

    if (event.suppressFire) {
        result.veto = RuntimeDeathWeaponVeto::SuppressedByMechanism;
        return result;
    }

    const bool directCause = event.cause.cascadeId == 0
        && event.cause.parentRequestId == 0
        && event.cause.parentDepth == 0;
    const bool chainedCause = event.cause.cascadeId != 0
        && event.cause.parentRequestId != 0
        && event.cause.parentDepth
            < std::numeric_limits<std::uint32_t>::max();
    if (!directCause && !chainedCause) {
        result.veto = RuntimeDeathWeaponVeto::InvalidCascadeCause;
        return result;
    }

    const std::uint32_t cascadeDepth = directCause
        ? 0U
        : event.cause.parentDepth + 1U;
    if (cascadeDepth >= context.maximumCascadeDepth) {
        result.veto = RuntimeDeathWeaponVeto::CascadeDepthExceeded;
        return result;
    }

    const DeathWeaponProfile profile = profileForObject(
        context,
        *object
    );
    if (!profile.found) {
        result.veto = RuntimeDeathWeaponVeto::TypeMissing;
        return result;
    }

    std::string weaponId = profile.weaponId;
    RuntimeDeathWeaponResolution resolution =
        RuntimeDeathWeaponResolution::Explicit;
    if (weaponId.empty()) {
        if (!profile.explodes) {
            result.veto = RuntimeDeathWeaponVeto::NotConfigured;
            return result;
        }
        weaponId = context.rules->getSpecialMechanismRules().defaultDeathWeapon;
        resolution = RuntimeDeathWeaponResolution::GlobalDefault;
    }
    if (weaponId.empty()) {
        result.veto = RuntimeDeathWeaponVeto::NotConfigured;
        return result;
    }

    const WeaponType* weapon = context.rules->findWeaponType(weaponId);
    if (weapon == nullptr) {
        result.veto = RuntimeDeathWeaponVeto::WeaponMissing;
        return result;
    }
    if (!std::isfinite(profile.damageModifier)
        || profile.damageModifier < 0.0) {
        result.veto = RuntimeDeathWeaponVeto::InvalidDamageModifier;
        return result;
    }
    if (nextRequestId_ == std::numeric_limits<std::uint64_t>::max()) {
        result.veto = RuntimeDeathWeaponVeto::RequestSequenceExhausted;
        return result;
    }

    RuntimeDeathWeaponFireRequest request;
    request.requestId = nextRequestId_++;
    request.cascadeId = directCause
        ? request.requestId
        : event.cause.cascadeId;
    request.parentRequestId = directCause
        ? 0
        : event.cause.parentRequestId;
    request.cascadeDepth = cascadeDepth;
    request.currentTick = event.currentTick;
    request.sourceObjectId = object->instanceId;
    request.sourceKind = object->kind;
    request.sourceAuthoredAircraft = object->authoredAircraft;
    request.sourceOwner = object->owner;
    request.sourceType = object->type;
    request.sourceMapId = object->mapId;
    request.sourceTag = object->tag;
    request.x = object->x;
    request.y = object->y;
    request.cell = object->cell;
    request.level = object->level;
    request.onBridge = object->onBridge;
    request.weaponId = weapon->id;
    request.resolution = resolution;
    request.authoredDamage = weapon->damage;
    request.damageModifier = profile.damageModifier;
    request.modifiedDamage = modifiedDamage(
        weapon->damage,
        profile.damageModifier
    );

    for (RuntimeDeathWeaponTriggeredObject& record : triggeredObjects_) {
        if (record.objectId == object->instanceId) {
            record.emittedRequestId = request.requestId;
            break;
        }
    }
    result.fireRequest = std::move(request);
    return result;
}

RuntimeDeathWeaponReconcileReport
RuntimeDeathWeaponSystemState::reconcile(
    const RuntimeDeathWeaponContext& context
) {
    RuntimeDeathWeaponReconcileReport report;
    if (context.objects == nullptr) {
        return report;
    }
    triggeredObjects_.erase(
        std::remove_if(
            triggeredObjects_.begin(),
            triggeredObjects_.end(),
            [&](const RuntimeDeathWeaponTriggeredObject& record) {
                const WorldObjectInstance* object = findObject(
                    context,
                    record.objectId
                );
                if (object == nullptr) {
                    ++report.removedMissingObjects;
                    return true;
                }
                if (object->kind != record.kind
                    || object->authoredAircraft != record.authoredAircraft
                    || !equalsAsciiIgnoreCase(object->type, record.type)) {
                    ++report.removedIdentityMismatches;
                    return true;
                }
                ++report.retained;
                return false;
            }
        ),
        triggeredObjects_.end()
    );
    sortState();
    return report;
}

void RuntimeDeathWeaponSystemState::save(std::ostream& output) const {
    std::ostringstream saved;
    saved.imbue(std::locale::classic());
    saved << "[DeathWeaponRuntime]\n";
    saved << "Version=1\n";
    saved << "NextRequestId=" << nextRequestId_ << "\n";
    saved << "TriggeredCount=" << triggeredObjects_.size() << "\n\n";

    std::vector<const RuntimeDeathWeaponTriggeredObject*> sorted;
    sorted.reserve(triggeredObjects_.size());
    for (const RuntimeDeathWeaponTriggeredObject& record : triggeredObjects_) {
        sorted.push_back(&record);
    }
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const auto* left, const auto* right) {
            return left->objectId < right->objectId;
        }
    );
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        const RuntimeDeathWeaponTriggeredObject& record = *sorted[index];
        saved << "[DeathWeaponRuntime.Triggered." << index << "]\n";
        saved << "ObjectId=" << record.objectId << "\n";
        saved << "Kind=" << kindValue(record.kind) << "\n";
        saved << "AuthoredAircraft="
              << (record.authoredAircraft ? "yes" : "no") << "\n";
        saved << "TypeHex=" << hexEncode(record.type) << "\n";
        saved << "DeathTick=" << record.deathTick << "\n";
        saved << "EmittedRequestId=" << record.emittedRequestId << "\n\n";
    }
    output << saved.str();
}

bool RuntimeDeathWeaponSystemState::load(
    const IniFile& ini,
    const RuntimeDeathWeaponContext& context,
    std::string* errorMessage,
    RuntimeDeathWeaponReconcileReport* reconcileReport
) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (ini.getSection("DeathWeaponRuntime") == nullptr) {
        return true;
    }
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (context.rules == nullptr || context.objects == nullptr) {
        return fail("death weapon load requires rules and world objects");
    }
    const auto value = [&ini](
        const std::string& section,
        const std::string& key,
        const std::string& fallback = std::string()
    ) {
        return ini.getValue(section, key, fallback);
    };

    int version = 0;
    int count = 0;
    std::uint64_t nextRequestId = 0;
    if (!parseIntegerStrict(
            value("DeathWeaponRuntime", "Version", "0"),
            version
        )
        || version != 1) {
        return fail("unsupported death weapon runtime save version");
    }
    if (!parseIntegerStrict(
            value("DeathWeaponRuntime", "NextRequestId", "0"),
            nextRequestId
        )
        || nextRequestId == 0
        || nextRequestId == std::numeric_limits<std::uint64_t>::max()) {
        return fail("invalid death weapon request sequence");
    }
    if (!parseIntegerStrict(
            value("DeathWeaponRuntime", "TriggeredCount", "-1"),
            count
        )
        || count < 0
        || static_cast<std::size_t>(count) > maximumSavedTriggeredObjects) {
        return fail("invalid death weapon triggered count");
    }

    RuntimeDeathWeaponSystemState loaded;
    loaded.nextRequestId_ = nextRequestId;
    loaded.triggeredObjects_.reserve(static_cast<std::size_t>(count));
    std::set<int> objectIds;
    std::set<std::uint64_t> requestIds;
    std::uint64_t maximumRequestId = 0;
    for (int index = 0; index < count; ++index) {
        const std::string section = "DeathWeaponRuntime.Triggered."
            + std::to_string(index);
        if (ini.getSection(section) == nullptr) {
            return fail("missing death weapon triggered record");
        }
        RuntimeDeathWeaponTriggeredObject record;
        int kind = -1;
        if (!parseIntegerStrict(value(section, "ObjectId", "-1"), record.objectId)
            || record.objectId < 0
            || !objectIds.insert(record.objectId).second
            || !parseIntegerStrict(value(section, "Kind", "-1"), kind)
            || !kindFromValue(kind, record.kind)
            || !parseBoolStrict(
                value(section, "AuthoredAircraft", ""),
                record.authoredAircraft
            )
            || !hexDecode(value(section, "TypeHex", ""), record.type)
            || record.type.empty()
            || !parseIntegerStrict(
                value(section, "DeathTick", "-1"),
                record.deathTick
            )
            || record.deathTick < 0
            || !parseIntegerStrict(
                value(section, "EmittedRequestId", ""),
                record.emittedRequestId
            )) {
            return fail("invalid death weapon triggered record");
        }
        if (record.emittedRequestId != 0) {
            if (!requestIds.insert(record.emittedRequestId).second) {
                return fail("duplicate death weapon request id");
            }
            maximumRequestId = std::max(
                maximumRequestId,
                record.emittedRequestId
            );
        }
        loaded.triggeredObjects_.push_back(std::move(record));
    }
    if (maximumRequestId >= loaded.nextRequestId_) {
        return fail("death weapon request sequence is not monotonic");
    }

    loaded.sortState();
    const RuntimeDeathWeaponReconcileReport report = loaded.reconcile(context);
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = report;
    }
    return true;
}

std::uint64_t RuntimeDeathWeaponSystemState::stateHash() const {
    std::ostringstream saved;
    save(saved);
    return hashText(fnvOffset, saved.str());
}

bool RuntimeDeathWeaponSystemState::hasTriggered(int objectId) const {
    const auto found = std::lower_bound(
        triggeredObjects_.begin(), triggeredObjects_.end(), objectId,
        [](const RuntimeDeathWeaponTriggeredObject& record, int wantedId) {
            return record.objectId < wantedId;
        }
    );
    return found != triggeredObjects_.end() && found->objectId == objectId;
}

std::uint64_t RuntimeDeathWeaponSystemState::nextRequestId() const {
    return nextRequestId_;
}

const std::vector<RuntimeDeathWeaponTriggeredObject>&
RuntimeDeathWeaponSystemState::triggeredObjects() const {
    return triggeredObjects_;
}

void RuntimeDeathWeaponSystemState::sortState() {
    std::sort(
        triggeredObjects_.begin(),
        triggeredObjects_.end(),
        [](const RuntimeDeathWeaponTriggeredObject& left,
           const RuntimeDeathWeaponTriggeredObject& right) {
            return left.objectId < right.objectId;
        }
    );
}

RuntimeDeathWeaponCause runtimeDeathWeaponCauseFrom(
    const RuntimeDeathWeaponFireRequest& request
) {
    return {
        request.cascadeId,
        request.requestId,
        request.cascadeDepth
    };
}

const char* runtimeDeathWeaponVetoName(RuntimeDeathWeaponVeto veto) {
    switch (veto) {
    case RuntimeDeathWeaponVeto::None:
        return "None";
    case RuntimeDeathWeaponVeto::InvalidContext:
        return "InvalidContext";
    case RuntimeDeathWeaponVeto::ObjectMissing:
        return "ObjectMissing";
    case RuntimeDeathWeaponVeto::ObjectAlreadyTriggered:
        return "ObjectAlreadyTriggered";
    case RuntimeDeathWeaponVeto::SuppressedByMechanism:
        return "SuppressedByMechanism";
    case RuntimeDeathWeaponVeto::TypeMissing:
        return "TypeMissing";
    case RuntimeDeathWeaponVeto::NotConfigured:
        return "NotConfigured";
    case RuntimeDeathWeaponVeto::WeaponMissing:
        return "WeaponMissing";
    case RuntimeDeathWeaponVeto::InvalidDamageModifier:
        return "InvalidDamageModifier";
    case RuntimeDeathWeaponVeto::InvalidCascadeCause:
        return "InvalidCascadeCause";
    case RuntimeDeathWeaponVeto::CascadeDepthExceeded:
        return "CascadeDepthExceeded";
    case RuntimeDeathWeaponVeto::RequestSequenceExhausted:
        return "RequestSequenceExhausted";
    }
    return "None";
}

const char* runtimeDeathWeaponResolutionName(
    RuntimeDeathWeaponResolution resolution
) {
    switch (resolution) {
    case RuntimeDeathWeaponResolution::Explicit:
        return "Explicit";
    case RuntimeDeathWeaponResolution::GlobalDefault:
        return "GlobalDefault";
    }
    return "Explicit";
}
