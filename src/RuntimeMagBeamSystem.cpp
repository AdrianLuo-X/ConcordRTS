#include "RuntimeMagBeamSystem.h"

#include "IniFile.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

constexpr std::size_t kHardMaxBindings = 4096;
constexpr std::size_t kHardMaxActions = 16384;
constexpr std::size_t kHardMaxInputRecords = 16384;
constexpr std::size_t kHardMaxTextLength = 256;
constexpr int kHardCoordinate = 1000000;
constexpr double kHardWeight = 1000000000.0;
constexpr double kHardKinematic = 1000000000000.0;
constexpr double kHardDamageMultiplier = 1000000.0;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

RuntimeMagBeamLimits sanitizeLimits(RuntimeMagBeamLimits limits) {
    limits.maxBindings = std::clamp<std::size_t>(
        limits.maxBindings,
        1,
        kHardMaxBindings
    );
    limits.maxActionsPerAdvance = std::clamp<std::size_t>(
        limits.maxActionsPerAdvance,
        3,
        kHardMaxActions
    );
    limits.maxInputRecords = std::clamp<std::size_t>(
        limits.maxInputRecords,
        1,
        kHardMaxInputRecords
    );
    limits.maxTextLength = std::clamp<std::size_t>(
        limits.maxTextLength,
        1,
        kHardMaxTextLength
    );
    limits.maxRecursionDepth = std::clamp(limits.maxRecursionDepth, 0, 1);
    return limits;
}

char lowerAscii(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
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

bool textValid(
    const std::string& text,
    std::size_t maximumLength,
    bool emptyAllowed = false
) {
    return (emptyAllowed || !text.empty())
        && text.size() <= maximumLength
        && text.find('\0') == std::string::npos;
}

bool validLocation(const RuntimeMagBeamLocation& location) {
    return location.x >= 0
        && location.x <= kHardCoordinate
        && location.y >= 0
        && location.y <= kHardCoordinate
        && location.cell >= 0
        && location.cell < std::numeric_limits<int>::max()
        && location.level >= 0
        && location.level <= 32767;
}

bool finiteBounded(double value) {
    return std::isfinite(value)
        && value >= -kHardKinematic
        && value <= kHardKinematic;
}

bool validKinematics(const RuntimeMagBeamKinematics& value) {
    return validLocation(value.location)
        && finiteBounded(value.worldX)
        && finiteBounded(value.worldY)
        && finiteBounded(value.worldZ)
        && finiteBounded(value.velocityX)
        && finiteBounded(value.velocityY)
        && finiteBounded(value.velocityZ);
}

bool kindValueValid(int value) {
    return value >= static_cast<int>(RuntimeMagBeamTargetKind::Vehicle)
        && value <= static_cast<int>(RuntimeMagBeamTargetKind::Other);
}

bool phaseValueValid(int value) {
    return value >= static_cast<int>(RuntimeMagBeamPhase::Acquired)
        && value <= static_cast<int>(RuntimeMagBeamPhase::Falling);
}

bool reasonValueValid(int value) {
    return value >= static_cast<int>(RuntimeMagBeamReleaseReason::None)
        && value <= static_cast<int>(RuntimeMagBeamReleaseReason::Impact);
}

bool updateKindValueValid(int value) {
    return value >= static_cast<int>(RuntimeMagBeamHostUpdateKind::LiftStarted)
        && value <= static_cast<int>(
            RuntimeMagBeamHostUpdateKind::ImpactResolved
        );
}

bool bindableKind(RuntimeMagBeamTargetKind kind) {
    return kind == RuntimeMagBeamTargetKind::Vehicle
        || kind == RuntimeMagBeamTargetKind::Aircraft;
}

bool validSourceSnapshot(
    const RuntimeMagBeamSourceSnapshot& source,
    const RuntimeMagBeamLimits& limits,
    bool requirePresent
) {
    if (source.objectId <= 0) {
        return false;
    }
    if (!source.present) {
        return !requirePresent
            && textValid(source.owner, limits.maxTextLength, true)
            && textValid(source.typeId, limits.maxTextLength, true);
    }
    return textValid(source.owner, limits.maxTextLength)
        && textValid(source.typeId, limits.maxTextLength);
}

bool validTargetSnapshot(
    const RuntimeMagBeamTargetSnapshot& target,
    const RuntimeMagBeamLimits& limits,
    bool requirePresent
) {
    if (target.objectId <= 0) {
        return false;
    }
    if (!target.present) {
        return !requirePresent
            && textValid(target.owner, limits.maxTextLength, true)
            && textValid(target.typeId, limits.maxTextLength, true)
            && textValid(
                target.originalLocomotorId,
                limits.maxTextLength,
                true
            );
    }
    return textValid(target.owner, limits.maxTextLength)
        && textValid(target.typeId, limits.maxTextLength)
        && textValid(target.originalLocomotorId, limits.maxTextLength)
        && std::isfinite(target.weight)
        && target.weight >= 0.0
        && target.weight <= kHardWeight
        && target.currentStrength >= 0
        && target.maximumStrength >= 0
        && target.suppressionThreshold >= 0
        && validKinematics(target.kinematics);
}

bool validWeapon(
    const RuntimeMagBeamWeaponSnapshot& weapon,
    const RuntimeMagBeamLimits& limits
) {
    return weapon.weaponExists
        && weapon.warheadExists
        && weapon.isLocomotor
        && weapon.rawDamage > 0
        && textValid(weapon.weaponId, limits.maxTextLength)
        && textValid(weapon.warheadId, limits.maxTextLength)
        && textValid(weapon.locomotorId, limits.maxTextLength);
}

bool validRules(const RuntimeMagBeamRulesSnapshot& rules) {
    return std::isfinite(rules.fallingDamageMultiplier)
        && rules.fallingDamageMultiplier >= 0.0
        && rules.fallingDamageMultiplier <= kHardDamageMultiplier;
}

bool validOccupancy(
    const RuntimeMagBeamOccupancySnapshot& occupancy,
    const RuntimeMagBeamLimits& limits
) {
    if (!occupancy.occupied) {
        return occupancy.objectId == -1
            && occupancy.kind == RuntimeMagBeamTargetKind::Other
            && occupancy.owner.empty()
            && occupancy.typeId.empty();
    }
    return occupancy.objectId > 0
        && kindValueValid(static_cast<int>(occupancy.kind))
        && textValid(occupancy.owner, limits.maxTextLength, true)
        && textValid(occupancy.typeId, limits.maxTextLength);
}

bool validImpact(
    const RuntimeMagBeamImpactSnapshot& impact,
    const RuntimeMagBeamLimits& limits
) {
    return validKinematics(impact.kinematics)
        && impact.targetCurrentStrength >= 0
        && impact.targetMaximumStrength >= 0
        && validOccupancy(impact.occupancy, limits);
}

bool validHostUpdate(
    const RuntimeMagBeamHostUpdate& update,
    const RuntimeMagBeamLimits& limits
) {
    if (update.bindingId <= 0
        || !updateKindValueValid(static_cast<int>(update.kind))) {
        return false;
    }
    if (update.kind == RuntimeMagBeamHostUpdateKind::ImpactResolved) {
        return update.releaseReason == RuntimeMagBeamReleaseReason::None
            && validImpact(update.impact, limits);
    }
    if (!validKinematics(update.kinematics)) {
        return false;
    }
    if (update.kind == RuntimeMagBeamHostUpdateKind::ReleasedFalling) {
        return update.releaseReason != RuntimeMagBeamReleaseReason::None
            && update.releaseReason
                != RuntimeMagBeamReleaseReason::GentleLanding
            && update.releaseReason != RuntimeMagBeamReleaseReason::Impact
            && update.releaseReason
                != RuntimeMagBeamReleaseReason::TargetMissing
            && update.releaseReason != RuntimeMagBeamReleaseReason::TargetDead
            && update.releaseReason
                != RuntimeMagBeamReleaseReason::TargetIdentityChanged;
    }
    return update.releaseReason == RuntimeMagBeamReleaseReason::None;
}

bool validRelease(const RuntimeMagBeamReleaseRequest& release) {
    return release.bindingId > 0
        && (release.reason == RuntimeMagBeamReleaseReason::Explicit
            || release.reason == RuntimeMagBeamReleaseReason::Reconciled)
        && validKinematics(release.kinematics);
}

bool sameSourceIdentity(
    const RuntimeMagBeamBindingState& binding,
    const RuntimeMagBeamSourceSnapshot& source
) {
    return binding.sourceObjectId == source.objectId
        && equalsIgnoreCase(binding.sourceOwner, source.owner)
        && equalsIgnoreCase(binding.sourceTypeId, source.typeId);
}

bool sameTargetIdentity(
    const RuntimeMagBeamBindingState& binding,
    const RuntimeMagBeamTargetSnapshot& target
) {
    return binding.targetObjectId == target.objectId
        && equalsIgnoreCase(binding.targetOwner, target.owner)
        && equalsIgnoreCase(binding.targetTypeId, target.typeId)
        && binding.targetKind == target.kind;
}

template <typename Record>
const Record* findByObjectId(const std::vector<Record>& records, int objectId) {
    const auto iterator = std::lower_bound(
        records.begin(),
        records.end(),
        objectId,
        [](const Record& record, int id) {
            return record.objectId < id;
        }
    );
    return iterator != records.end() && iterator->objectId == objectId
        ? &*iterator
        : nullptr;
}

template <typename Record>
const Record* findByBindingId(
    const std::vector<Record>& records,
    int bindingId
) {
    const auto iterator = std::lower_bound(
        records.begin(),
        records.end(),
        bindingId,
        [](const Record& record, int id) {
            return record.bindingId < id;
        }
    );
    return iterator != records.end() && iterator->bindingId == bindingId
        ? &*iterator
        : nullptr;
}

bool prepareSnapshots(
    std::vector<RuntimeMagBeamSourceSnapshot>& sources,
    std::vector<RuntimeMagBeamTargetSnapshot>& targets,
    const RuntimeMagBeamLimits& limits
) {
    if (sources.size() > limits.maxInputRecords
        || targets.size() > limits.maxInputRecords) {
        return false;
    }
    std::sort(
        sources.begin(),
        sources.end(),
        [](const auto& left, const auto& right) {
            return left.objectId < right.objectId;
        }
    );
    std::sort(
        targets.begin(),
        targets.end(),
        [](const auto& left, const auto& right) {
            return left.objectId < right.objectId;
        }
    );
    int previous = -1;
    for (const RuntimeMagBeamSourceSnapshot& source : sources) {
        if (!validSourceSnapshot(source, limits, false)
            || source.objectId == previous) {
            return false;
        }
        previous = source.objectId;
    }
    previous = -1;
    for (const RuntimeMagBeamTargetSnapshot& target : targets) {
        if (!validTargetSnapshot(target, limits, false)
            || target.objectId == previous) {
            return false;
        }
        previous = target.objectId;
    }
    return true;
}

RuntimeMagBeamAction actionFor(
    const RuntimeMagBeamBindingState& binding,
    RuntimeMagBeamActionKind kind,
    int tick
) {
    RuntimeMagBeamAction action;
    action.kind = kind;
    action.tick = tick;
    action.bindingId = binding.bindingId;
    action.sourceObjectId = binding.sourceObjectId;
    action.targetObjectId = binding.targetObjectId;
    action.sourceOwner = binding.sourceOwner;
    action.targetOwner = binding.targetOwner;
    action.weaponId = binding.weaponId;
    action.warheadId = binding.warheadId;
    action.locomotorId = binding.locomotorId;
    action.originalLocomotorId = binding.originalLocomotorId;
    action.originalLocomotorKnown = binding.originalLocomotorKnown;
    action.magBeamVisual = binding.magBeamVisual;
    action.rawDamage = binding.rawDamage;
    action.targetWeight = binding.targetWeight;
    action.phase = binding.phase;
    action.releaseReason = binding.releaseReason;
    action.kinematics = binding.kinematics;
    action.fallingDamageMultiplier = binding.fallingDamageMultiplier;
    action.currentStrengthDamage = binding.currentStrengthDamage;
    return action;
}

std::string hexEncode(std::string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        output.push_back(digits[(byte >> 4U) & 0x0FU]);
        output.push_back(digits[byte & 0x0FU]);
    }
    return output;
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

bool hexDecode(
    const std::string& value,
    std::size_t maximumLength,
    std::string& output
) {
    if ((value.size() & 1U) != 0U || value.size() / 2U > maximumLength) {
        return false;
    }
    std::string decoded;
    decoded.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = hexDigit(value[index]);
        const int low = hexDigit(value[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    if (decoded.find('\0') != std::string::npos) {
        return false;
    }
    output = std::move(decoded);
    return true;
}

bool parseInt(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    int parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseSize(const std::string& text, std::size_t& value) {
    if (text.empty()) {
        return false;
    }
    unsigned long long parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end
        || parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parseDouble(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double parsed = 0.0;
    input >> std::noskipws >> parsed;
    if (!input || !input.eof() || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseBool(const std::string& text, bool& value) {
    if (text == "0") {
        value = false;
        return true;
    }
    if (text == "1") {
        value = true;
        return true;
    }
    return false;
}

std::string locationText(const RuntimeMagBeamLocation& location) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << location.x << ',' << location.y << ',' << location.cell << ','
           << location.level << ',' << (location.onBridge ? 1 : 0) << ','
           << (location.water ? 1 : 0);
    return output.str();
}

bool parseLocation(const std::string& text, RuntimeMagBeamLocation& location) {
    std::array<std::string, 6> parts;
    std::size_t begin = 0;
    for (std::size_t part = 0; part < parts.size(); ++part) {
        const std::size_t comma = text.find(',', begin);
        if (part + 1U == parts.size()) {
            if (comma != std::string::npos) {
                return false;
            }
            parts[part] = text.substr(begin);
        } else {
            if (comma == std::string::npos) {
                return false;
            }
            parts[part] = text.substr(begin, comma - begin);
            begin = comma + 1U;
        }
    }
    RuntimeMagBeamLocation parsed;
    int bridge = 0;
    int water = 0;
    if (!parseInt(parts[0], parsed.x)
        || !parseInt(parts[1], parsed.y)
        || !parseInt(parts[2], parsed.cell)
        || !parseInt(parts[3], parsed.level)
        || !parseInt(parts[4], bridge)
        || !parseInt(parts[5], water)
        || (bridge != 0 && bridge != 1)
        || (water != 0 && water != 1)) {
        return false;
    }
    parsed.onBridge = bridge != 0;
    parsed.water = water != 0;
    if (!validLocation(parsed)) {
        return false;
    }
    location = parsed;
    return true;
}

bool parseKinematics(
    const IniFile& ini,
    const std::string& section,
    RuntimeMagBeamKinematics& value
) {
    return parseLocation(
            ini.getValue(section, "TargetLocation", ""),
            value.location
        )
        && parseDouble(ini.getValue(section, "WorldX", ""), value.worldX)
        && parseDouble(ini.getValue(section, "WorldY", ""), value.worldY)
        && parseDouble(ini.getValue(section, "WorldZ", ""), value.worldZ)
        && parseDouble(
            ini.getValue(section, "VelocityX", ""),
            value.velocityX
        )
        && parseDouble(
            ini.getValue(section, "VelocityY", ""),
            value.velocityY
        )
        && parseDouble(
            ini.getValue(section, "VelocityZ", ""),
            value.velocityZ
        )
        && validKinematics(value);
}

void hashByte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void hashUint64(std::uint64_t& hash, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        hashByte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hashInt(std::uint64_t& hash, int value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) {
        hashByte(hash, static_cast<std::uint8_t>(bits >> shift));
    }
}

void hashBool(std::uint64_t& hash, bool value) {
    hashByte(hash, value ? 1U : 0U);
}

void hashDouble(std::uint64_t& hash, double value) {
    hashUint64(hash, std::bit_cast<std::uint64_t>(value));
}

void hashText(std::uint64_t& hash, const std::string& text) {
    hashUint64(hash, text.size());
    for (const unsigned char byte : text) {
        hashByte(hash, byte);
    }
}

void hashLocation(
    std::uint64_t& hash,
    const RuntimeMagBeamLocation& location
) {
    hashInt(hash, location.x);
    hashInt(hash, location.y);
    hashInt(hash, location.cell);
    hashInt(hash, location.level);
    hashBool(hash, location.onBridge);
    hashBool(hash, location.water);
}

void hashKinematics(
    std::uint64_t& hash,
    const RuntimeMagBeamKinematics& value
) {
    hashLocation(hash, value.location);
    hashDouble(hash, value.worldX);
    hashDouble(hash, value.worldY);
    hashDouble(hash, value.worldZ);
    hashDouble(hash, value.velocityX);
    hashDouble(hash, value.velocityY);
    hashDouble(hash, value.velocityZ);
}

bool loadedBindingValid(
    const RuntimeMagBeamBindingState& binding,
    const RuntimeMagBeamLimits& limits,
    int lastAdvancedTick,
    bool legacy
) {
    const bool originalValid = binding.originalLocomotorKnown
        ? textValid(binding.originalLocomotorId, limits.maxTextLength)
        : binding.originalLocomotorId.empty();
    const bool weightValid = legacy
        ? binding.rawDamage >= 0
        : binding.rawDamage > 0
            && static_cast<double>(binding.rawDamage) > binding.targetWeight;
    return binding.bindingId > 0
        && binding.sourceObjectId > 0
        && binding.targetObjectId > 0
        && binding.sourceObjectId != binding.targetObjectId
        && textValid(binding.sourceOwner, limits.maxTextLength)
        && textValid(binding.sourceTypeId, limits.maxTextLength)
        && textValid(binding.targetOwner, limits.maxTextLength)
        && textValid(binding.targetTypeId, limits.maxTextLength)
        && bindableKind(binding.targetKind)
        && std::isfinite(binding.targetWeight)
        && binding.targetWeight >= 0.0
        && binding.targetWeight <= kHardWeight
        && binding.targetCurrentStrength >= 0
        && binding.targetMaximumStrength >= 0
        && binding.targetSuppressionThreshold >= 0
        && validKinematics(binding.kinematics)
        && textValid(binding.weaponId, limits.maxTextLength)
        && textValid(binding.warheadId, limits.maxTextLength)
        && textValid(binding.locomotorId, limits.maxTextLength)
        && originalValid
        && weightValid
        && std::isfinite(binding.fallingDamageMultiplier)
        && binding.fallingDamageMultiplier >= 0.0
        && binding.fallingDamageMultiplier <= kHardDamageMultiplier
        && binding.acquiredTick >= 0
        && binding.acquiredTick <= lastAdvancedTick
        && binding.phaseChangedTick >= binding.acquiredTick
        && binding.phaseChangedTick <= lastAdvancedTick
        && (binding.phase == RuntimeMagBeamPhase::Falling
            ? binding.releaseReason != RuntimeMagBeamReleaseReason::None
            : binding.releaseReason == RuntimeMagBeamReleaseReason::None);
}

} // namespace

RuntimeMagBeamSystemState::RuntimeMagBeamSystemState()
    : RuntimeMagBeamSystemState(RuntimeMagBeamLimits{}) {}

RuntimeMagBeamSystemState::RuntimeMagBeamSystemState(
    RuntimeMagBeamLimits limits
) {
    reset(limits);
}

void RuntimeMagBeamSystemState::reset(RuntimeMagBeamLimits limits) {
    limits_ = sanitizeLimits(limits);
    lastAdvancedTick_ = -1;
    nextBindingId_ = 1;
    bindings_.clear();
}

RuntimeMagBeamAcquireResult RuntimeMagBeamSystemState::acquire(
    const RuntimeMagBeamAcquireRequest& request
) {
    RuntimeMagBeamAcquireResult result;
    if (request.currentTick < 0
        || (lastAdvancedTick_ >= 0
            && request.currentTick != lastAdvancedTick_)) {
        result.veto = RuntimeMagBeamVeto::InvalidTick;
        return result;
    }
    if (request.recursionDepth < 0
        || request.recursionDepth > limits_.maxRecursionDepth) {
        result.veto = RuntimeMagBeamVeto::RecursiveRequest;
        return result;
    }
    if (!validSourceSnapshot(request.source, limits_, true)
        || !request.source.alive) {
        result.veto = RuntimeMagBeamVeto::InvalidSource;
        return result;
    }
    if (!textValid(request.source.owner, limits_.maxTextLength)) {
        result.veto = RuntimeMagBeamVeto::InvalidOwner;
        return result;
    }
    if (!validTargetSnapshot(request.target, limits_, true)
        || !request.target.alive
        || request.target.objectId == request.source.objectId) {
        result.veto = RuntimeMagBeamVeto::InvalidTarget;
        return result;
    }
    if (!request.forceFire && (!request.target.enemyToSource
        || equalsIgnoreCase(request.source.owner, request.target.owner))) {
        result.veto = RuntimeMagBeamVeto::FriendlyTarget;
        return result;
    }
    if (!bindableKind(request.target.kind)) {
        result.veto = RuntimeMagBeamVeto::IllegalTargetKind;
        return result;
    }
    if (!request.target.onGround || request.target.airborne) {
        result.veto = RuntimeMagBeamVeto::TargetAirborne;
        return result;
    }
    if (!request.target.liftable
        || request.target.magBeamImmune
        || request.target.locomotorImmune) {
        result.veto = RuntimeMagBeamVeto::TargetImmune;
        return result;
    }
    if (!validWeapon(request.weapon, limits_)) {
        result.veto = RuntimeMagBeamVeto::InvalidWeapon;
        return result;
    }
    if (!(static_cast<double>(request.weapon.rawDamage)
            > request.target.weight)) {
        result.veto = RuntimeMagBeamVeto::TargetTooHeavy;
        return result;
    }
    if (!validRules(request.rules)) {
        result.veto = RuntimeMagBeamVeto::InvalidRules;
        return result;
    }

    for (const RuntimeMagBeamBindingState& binding : bindings_) {
        if (binding.sourceObjectId == request.source.objectId) {
            if (binding.targetObjectId == request.target.objectId
                && binding.acquiredTick == request.currentTick
                && sameSourceIdentity(binding, request.source)
                && sameTargetIdentity(binding, request.target)) {
                result.accepted = true;
                result.idempotent = true;
                result.bindingId = binding.bindingId;
                return result;
            }
            result.veto = RuntimeMagBeamVeto::SourceBusy;
            return result;
        }
        if (binding.targetObjectId == request.target.objectId) {
            result.veto = RuntimeMagBeamVeto::TargetBusy;
            return result;
        }
    }
    if (bindings_.size() >= limits_.maxBindings) {
        result.veto = RuntimeMagBeamVeto::BindingLimitReached;
        return result;
    }
    if (nextBindingId_ <= 0
        || nextBindingId_ == std::numeric_limits<int>::max()) {
        result.veto = RuntimeMagBeamVeto::IdExhausted;
        return result;
    }

    RuntimeMagBeamBindingState binding;
    binding.bindingId = nextBindingId_++;
    binding.sourceObjectId = request.source.objectId;
    binding.sourceOwner = request.source.owner;
    binding.sourceTypeId = request.source.typeId;
    binding.targetObjectId = request.target.objectId;
    binding.targetOwner = request.target.owner;
    binding.targetTypeId = request.target.typeId;
    binding.targetKind = request.target.kind;
    binding.targetWeight = request.target.weight;
    binding.targetCurrentStrength = request.target.currentStrength;
    binding.targetMaximumStrength = request.target.maximumStrength;
    binding.targetSuppressionThreshold = request.target.suppressionThreshold;
    binding.kinematics = request.target.kinematics;
    binding.weaponId = request.weapon.weaponId;
    binding.warheadId = request.weapon.warheadId;
    binding.locomotorId = request.weapon.locomotorId;
    binding.originalLocomotorId = request.target.originalLocomotorId;
    binding.originalLocomotorKnown = true;
    binding.magBeamVisual = request.weapon.isMagBeam;
    binding.rawDamage = request.weapon.rawDamage;
    binding.fallingDamageMultiplier = request.rules.fallingDamageMultiplier;
    binding.currentStrengthDamage = request.rules.currentStrengthDamage;
    binding.phase = RuntimeMagBeamPhase::Acquired;
    binding.acquiredTick = request.currentTick;
    binding.phaseChangedTick = request.currentTick;
    bindings_.push_back(binding);
    sortState();
    if (lastAdvancedTick_ < 0) {
        lastAdvancedTick_ = request.currentTick;
    }

    result.accepted = true;
    result.bindingId = binding.bindingId;
    result.actions.push_back(actionFor(
        binding,
        RuntimeMagBeamActionKind::Acquired,
        request.currentTick
    ));
    result.actions.push_back(actionFor(
        binding,
        RuntimeMagBeamActionKind::ApplyLocomotor,
        request.currentTick
    ));
    return result;
}

RuntimeMagBeamAdvanceResult RuntimeMagBeamSystemState::advance(
    const RuntimeMagBeamAdvanceInput& input
) {
    RuntimeMagBeamAdvanceResult result;
    if (input.currentTick < 0) {
        result.inputRejected = true;
        return result;
    }
    if (input.currentTick == lastAdvancedTick_) {
        return result;
    }
    if (lastAdvancedTick_ >= 0
        && input.currentTick != lastAdvancedTick_ + 1) {
        result.inputRejected = true;
        return result;
    }

    RuntimeMagBeamAdvanceInput prepared = input;
    if (!prepareSnapshots(prepared.sources, prepared.targets, limits_)
        || prepared.releases.size() > limits_.maxInputRecords
        || prepared.hostUpdates.size() > limits_.maxInputRecords) {
        result.inputRejected = true;
        return result;
    }
    std::sort(
        prepared.releases.begin(),
        prepared.releases.end(),
        [](const auto& left, const auto& right) {
            return left.bindingId < right.bindingId;
        }
    );
    std::sort(
        prepared.hostUpdates.begin(),
        prepared.hostUpdates.end(),
        [](const auto& left, const auto& right) {
            return left.bindingId < right.bindingId;
        }
    );
    int previous = -1;
    for (const RuntimeMagBeamReleaseRequest& release : prepared.releases) {
        if (!validRelease(release)
            || release.bindingId == previous
            || findBinding(release.bindingId) == nullptr) {
            result.inputRejected = true;
            return result;
        }
        previous = release.bindingId;
    }
    previous = -1;
    for (const RuntimeMagBeamHostUpdate& update : prepared.hostUpdates) {
        if (!validHostUpdate(update, limits_)
            || update.bindingId == previous
            || findBinding(update.bindingId) == nullptr) {
            result.inputRejected = true;
            return result;
        }
        previous = update.bindingId;
    }

    // With no binding authority there is no tick-relative state to advance.
    // Keep the empty representation canonical so unrelated host fast paths
    // may advance the world clock without poisoning the next acquisition.
    if (bindings_.empty()) {
        lastAdvancedTick_ = -1;
        return result;
    }

    RuntimeMagBeamSystemState candidate = *this;
    std::vector<RuntimeMagBeamAction> actions;
    std::set<int> removeIds;
    bool invalidTransition = false;
    auto emit = [&](RuntimeMagBeamAction action) {
        if (actions.size() >= limits_.maxActionsPerAdvance) {
            result.actionLimitReached = true;
            return false;
        }
        actions.push_back(std::move(action));
        return true;
    };

    for (RuntimeMagBeamBindingState& binding : candidate.bindings_) {
        const RuntimeMagBeamSourceSnapshot* source = input.validateSources
            ? findByObjectId(prepared.sources, binding.sourceObjectId)
            : nullptr;
        const RuntimeMagBeamTargetSnapshot* target = input.validateTargets
            ? findByObjectId(prepared.targets, binding.targetObjectId)
            : nullptr;
        const RuntimeMagBeamReleaseRequest* release = findByBindingId(
            prepared.releases,
            binding.bindingId
        );
        const RuntimeMagBeamHostUpdate* update = findByBindingId(
            prepared.hostUpdates,
            binding.bindingId
        );

        RuntimeMagBeamReleaseReason cancelReason =
            RuntimeMagBeamReleaseReason::None;
        RuntimeMagBeamReleaseReason forcedFallReason =
            RuntimeMagBeamReleaseReason::None;
        if (input.validateTargets) {
            if (target == nullptr || !target->present) {
                cancelReason = RuntimeMagBeamReleaseReason::TargetMissing;
            } else if (!sameTargetIdentity(binding, *target)) {
                cancelReason =
                    RuntimeMagBeamReleaseReason::TargetIdentityChanged;
            } else if (!target->alive) {
                cancelReason = RuntimeMagBeamReleaseReason::TargetDead;
            } else {
                binding.targetCurrentStrength = target->currentStrength;
                binding.targetMaximumStrength = target->maximumStrength;
                binding.targetSuppressionThreshold =
                    target->suppressionThreshold;
                binding.kinematics = target->kinematics;
                if (!binding.originalLocomotorKnown) {
                    binding.originalLocomotorId =
                        target->originalLocomotorId;
                    binding.originalLocomotorKnown = true;
                }
            }
        }
        if (cancelReason == RuntimeMagBeamReleaseReason::None
            && binding.phase != RuntimeMagBeamPhase::Falling
            && input.validateSources) {
            if (source == nullptr || !source->present) {
                forcedFallReason =
                    RuntimeMagBeamReleaseReason::SourceMissing;
            } else if (!sameSourceIdentity(binding, *source)) {
                forcedFallReason =
                    RuntimeMagBeamReleaseReason::SourceIdentityChanged;
            } else if (!source->alive) {
                forcedFallReason = RuntimeMagBeamReleaseReason::SourceDead;
            }
        }
        if (cancelReason == RuntimeMagBeamReleaseReason::None
            && binding.phase != RuntimeMagBeamPhase::Falling
            && release != nullptr) {
            forcedFallReason = release->reason;
            binding.kinematics = release->kinematics;
        }

        if (cancelReason != RuntimeMagBeamReleaseReason::None) {
            binding.releaseReason = cancelReason;
            if (!emit(actionFor(
                    binding,
                    RuntimeMagBeamActionKind::Cancelled,
                    input.currentTick
                ))) {
                break;
            }
            removeIds.insert(binding.bindingId);
            continue;
        }

        if (forcedFallReason != RuntimeMagBeamReleaseReason::None
            && binding.phase != RuntimeMagBeamPhase::Falling) {
            binding.phase = RuntimeMagBeamPhase::Falling;
            binding.releaseReason = forcedFallReason;
            binding.phaseChangedTick = input.currentTick;
            if (!emit(actionFor(
                    binding,
                    RuntimeMagBeamActionKind::BeginFall,
                    input.currentTick
                ))) {
                break;
            }
        }

        if (update == nullptr) {
            continue;
        }
        switch (update->kind) {
        case RuntimeMagBeamHostUpdateKind::LiftStarted:
            binding.kinematics = update->kinematics;
            if (binding.phase == RuntimeMagBeamPhase::Falling) {
                break;
            }
            if (binding.phase != RuntimeMagBeamPhase::Acquired
                && binding.phase != RuntimeMagBeamPhase::Lifting) {
                invalidTransition = true;
                break;
            }
            if (binding.phase != RuntimeMagBeamPhase::Lifting) {
                binding.phase = RuntimeMagBeamPhase::Lifting;
                binding.phaseChangedTick = input.currentTick;
                if (!emit(actionFor(
                        binding,
                        RuntimeMagBeamActionKind::LiftStarted,
                        input.currentTick
                    ))) {
                    break;
                }
            }
            break;

        case RuntimeMagBeamHostUpdateKind::Carried:
            binding.kinematics = update->kinematics;
            if (binding.phase == RuntimeMagBeamPhase::Falling) {
                break;
            }
            if (binding.phase != RuntimeMagBeamPhase::Lifting
                && binding.phase != RuntimeMagBeamPhase::Carried) {
                invalidTransition = true;
                break;
            }
            if (binding.phase != RuntimeMagBeamPhase::Carried) {
                binding.phase = RuntimeMagBeamPhase::Carried;
                binding.phaseChangedTick = input.currentTick;
                if (!emit(actionFor(
                        binding,
                        RuntimeMagBeamActionKind::Carried,
                        input.currentTick
                    ))) {
                    break;
                }
            }
            break;

        case RuntimeMagBeamHostUpdateKind::ReleasedFalling:
            binding.kinematics = update->kinematics;
            if (binding.phase != RuntimeMagBeamPhase::Falling) {
                binding.phase = RuntimeMagBeamPhase::Falling;
                binding.releaseReason = update->releaseReason;
                binding.phaseChangedTick = input.currentTick;
                if (!emit(actionFor(
                        binding,
                        RuntimeMagBeamActionKind::BeginFall,
                        input.currentTick
                    ))) {
                    break;
                }
            }
            break;

        case RuntimeMagBeamHostUpdateKind::GentleLanded:
            binding.kinematics = update->kinematics;
            if (!binding.originalLocomotorKnown) {
                invalidTransition = true;
                break;
            }
            binding.releaseReason =
                RuntimeMagBeamReleaseReason::GentleLanding;
            if (!emit(actionFor(
                    binding,
                    RuntimeMagBeamActionKind::RestoreLocomotor,
                    input.currentTick
                ))
                || !emit(actionFor(
                    binding,
                    RuntimeMagBeamActionKind::Released,
                    input.currentTick
                ))) {
                break;
            }
            removeIds.insert(binding.bindingId);
            break;

        case RuntimeMagBeamHostUpdateKind::ImpactResolved: {
            if (binding.phase != RuntimeMagBeamPhase::Falling
                || !binding.originalLocomotorKnown) {
                invalidTransition = true;
                break;
            }
            binding.kinematics = update->impact.kinematics;
            binding.targetCurrentStrength =
                update->impact.targetCurrentStrength;
            binding.targetMaximumStrength =
                update->impact.targetMaximumStrength;
            RuntimeMagBeamAction impact = actionFor(
                binding,
                RuntimeMagBeamActionKind::ResolveLanding,
                input.currentTick
            );
            impact.hasImpact = true;
            impact.impact = update->impact;
            if (!emit(std::move(impact))
                || !emit(actionFor(
                    binding,
                    RuntimeMagBeamActionKind::RestoreLocomotor,
                    input.currentTick
                ))
                || !emit(actionFor(
                    binding,
                    RuntimeMagBeamActionKind::Released,
                    input.currentTick
                ))) {
                break;
            }
            removeIds.insert(binding.bindingId);
            break;
        }
        }
        if (invalidTransition || result.actionLimitReached) {
            break;
        }
    }

    if (invalidTransition) {
        result.inputRejected = true;
        return result;
    }
    if (result.actionLimitReached) {
        result.actions.clear();
        return result;
    }
    candidate.bindings_.erase(
        std::remove_if(
            candidate.bindings_.begin(),
            candidate.bindings_.end(),
            [&](const RuntimeMagBeamBindingState& binding) {
                return removeIds.contains(binding.bindingId);
            }
        ),
        candidate.bindings_.end()
    );
    candidate.lastAdvancedTick_ = candidate.bindings_.empty()
        ? -1
        : input.currentTick;
    candidate.sortState();
    *this = std::move(candidate);
    result.advanced = true;
    result.actions = std::move(actions);
    return result;
}

RuntimeMagBeamReconcileReport RuntimeMagBeamSystemState::reconcile(
    const RuntimeMagBeamReconcileInput& input
) {
    RuntimeMagBeamReconcileReport report;
    if (input.currentTick < 0
        || (lastAdvancedTick_ >= 0
            && input.currentTick < lastAdvancedTick_)) {
        report.inputRejected = true;
        return report;
    }
    RuntimeMagBeamReconcileInput prepared = input;
    if (!prepareSnapshots(prepared.sources, prepared.targets, limits_)) {
        report.inputRejected = true;
        return report;
    }

    if (bindings_.empty()) {
        lastAdvancedTick_ = -1;
        return report;
    }

    RuntimeMagBeamSystemState candidate = *this;
    std::vector<RuntimeMagBeamAction> actions;
    std::set<int> removeIds;
    auto emit = [&](RuntimeMagBeamAction action) {
        if (actions.size() >= limits_.maxActionsPerAdvance) {
            report.actionLimitReached = true;
            return false;
        }
        actions.push_back(std::move(action));
        return true;
    };

    for (RuntimeMagBeamBindingState& binding : candidate.bindings_) {
        const RuntimeMagBeamSourceSnapshot* source = input.validateSources
            ? findByObjectId(prepared.sources, binding.sourceObjectId)
            : nullptr;
        const RuntimeMagBeamTargetSnapshot* target = input.validateTargets
            ? findByObjectId(prepared.targets, binding.targetObjectId)
            : nullptr;
        RuntimeMagBeamReleaseReason cancelReason =
            RuntimeMagBeamReleaseReason::None;
        RuntimeMagBeamReleaseReason fallReason =
            RuntimeMagBeamReleaseReason::None;

        if (input.validateTargets) {
            if (target == nullptr || !target->present) {
                cancelReason = RuntimeMagBeamReleaseReason::TargetMissing;
            } else if (!sameTargetIdentity(binding, *target)) {
                cancelReason =
                    RuntimeMagBeamReleaseReason::TargetIdentityChanged;
            } else if (!target->alive) {
                cancelReason = RuntimeMagBeamReleaseReason::TargetDead;
            } else {
                binding.targetCurrentStrength = target->currentStrength;
                binding.targetMaximumStrength = target->maximumStrength;
                binding.targetSuppressionThreshold =
                    target->suppressionThreshold;
                binding.kinematics = target->kinematics;
                if (!binding.originalLocomotorKnown) {
                    binding.originalLocomotorId =
                        target->originalLocomotorId;
                    binding.originalLocomotorKnown = true;
                }
            }
        }
        if (cancelReason == RuntimeMagBeamReleaseReason::None
            && binding.phase != RuntimeMagBeamPhase::Falling
            && input.validateSources) {
            if (source == nullptr || !source->present) {
                fallReason = RuntimeMagBeamReleaseReason::SourceMissing;
            } else if (!sameSourceIdentity(binding, *source)) {
                fallReason =
                    RuntimeMagBeamReleaseReason::SourceIdentityChanged;
            } else if (!source->alive) {
                fallReason = RuntimeMagBeamReleaseReason::SourceDead;
            }
        }

        if (cancelReason != RuntimeMagBeamReleaseReason::None) {
            binding.releaseReason = cancelReason;
            if (!emit(actionFor(
                    binding,
                    RuntimeMagBeamActionKind::Cancelled,
                    input.currentTick
                ))) {
                break;
            }
            removeIds.insert(binding.bindingId);
            ++report.cancelled;
            continue;
        }
        if (fallReason != RuntimeMagBeamReleaseReason::None) {
            binding.phase = RuntimeMagBeamPhase::Falling;
            binding.releaseReason = fallReason;
            binding.phaseChangedTick = input.currentTick;
            if (!emit(actionFor(
                    binding,
                    RuntimeMagBeamActionKind::BeginFall,
                    input.currentTick
                ))) {
                break;
            }
            ++report.released;
        }
        ++report.retained;
    }

    if (report.actionLimitReached) {
        report.retained = 0;
        report.released = 0;
        report.cancelled = 0;
        report.actions.clear();
        return report;
    }
    candidate.bindings_.erase(
        std::remove_if(
            candidate.bindings_.begin(),
            candidate.bindings_.end(),
            [&](const RuntimeMagBeamBindingState& binding) {
                return removeIds.contains(binding.bindingId);
            }
        ),
        candidate.bindings_.end()
    );
    candidate.lastAdvancedTick_ = candidate.bindings_.empty()
        ? -1
        : std::max(candidate.lastAdvancedTick_, input.currentTick);
    candidate.sortState();
    *this = std::move(candidate);
    report.actions = std::move(actions);
    return report;
}

void RuntimeMagBeamSystemState::save(std::ostream& output) const {
    output.imbue(std::locale::classic());
    output << "[RuntimeMagBeam]\n"
           << "Schema=2\n"
           << "LastAdvancedTick=" << lastAdvancedTick_ << "\n"
           << "NextBindingId=" << nextBindingId_ << "\n"
           << "Count=" << bindings_.size() << "\n\n";
    for (std::size_t index = 0; index < bindings_.size(); ++index) {
        const RuntimeMagBeamBindingState& binding = bindings_[index];
        output << "[RuntimeMagBeam.Binding." << index << "]\n"
               << "BindingId=" << binding.bindingId << "\n"
               << "SourceObjectId=" << binding.sourceObjectId << "\n"
               << "SourceOwner=" << hexEncode(binding.sourceOwner) << "\n"
               << "SourceType=" << hexEncode(binding.sourceTypeId) << "\n"
               << "TargetObjectId=" << binding.targetObjectId << "\n"
               << "TargetOwner=" << hexEncode(binding.targetOwner) << "\n"
               << "TargetType=" << hexEncode(binding.targetTypeId) << "\n"
               << "TargetKind=" << static_cast<int>(binding.targetKind)
               << "\n"
               << "TargetWeight=" << std::setprecision(17)
               << binding.targetWeight << "\n"
               << "TargetCurrentStrength="
               << binding.targetCurrentStrength << "\n"
               << "TargetMaximumStrength="
               << binding.targetMaximumStrength << "\n"
               << "TargetSuppressionThreshold="
               << binding.targetSuppressionThreshold << "\n"
               << "TargetLocation="
               << locationText(binding.kinematics.location) << "\n"
               << "WorldX=" << std::setprecision(17)
               << binding.kinematics.worldX << "\n"
               << "WorldY=" << binding.kinematics.worldY << "\n"
               << "WorldZ=" << binding.kinematics.worldZ << "\n"
               << "VelocityX=" << binding.kinematics.velocityX << "\n"
               << "VelocityY=" << binding.kinematics.velocityY << "\n"
               << "VelocityZ=" << binding.kinematics.velocityZ << "\n"
               << "Weapon=" << hexEncode(binding.weaponId) << "\n"
               << "Warhead=" << hexEncode(binding.warheadId) << "\n"
               << "Locomotor=" << hexEncode(binding.locomotorId) << "\n"
               << "OriginalLocomotor="
               << hexEncode(binding.originalLocomotorId) << "\n"
               << "OriginalLocomotorKnown="
               << (binding.originalLocomotorKnown ? 1 : 0) << "\n"
               << "MagBeamVisual=" << (binding.magBeamVisual ? 1 : 0)
               << "\n"
               << "RawDamage=" << binding.rawDamage << "\n"
               << "FallingDamageMultiplier=" << std::setprecision(17)
               << binding.fallingDamageMultiplier << "\n"
               << "CurrentStrengthDamage="
               << (binding.currentStrengthDamage ? 1 : 0) << "\n"
               << "Phase=" << static_cast<int>(binding.phase) << "\n"
               << "ReleaseReason="
               << static_cast<int>(binding.releaseReason) << "\n"
               << "AcquiredTick=" << binding.acquiredTick << "\n"
               << "PhaseChangedTick=" << binding.phaseChangedTick
               << "\n\n";
    }
}

bool RuntimeMagBeamSystemState::load(
    const IniFile& ini,
    std::string* errorMessage
) {
    auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    const std::string schemaText = ini.getValue(
        "RuntimeMagBeam",
        "Schema",
        ""
    );
    const bool legacy = schemaText == "1";
    if (!legacy && schemaText != "2") {
        return fail("unsupported RuntimeMagBeam schema");
    }

    RuntimeMagBeamSystemState loaded(limits_);
    std::size_t count = 0;
    if (!parseInt(
            ini.getValue("RuntimeMagBeam", "LastAdvancedTick", ""),
            loaded.lastAdvancedTick_
        )
        || loaded.lastAdvancedTick_ < -1
        || !parseInt(
            ini.getValue("RuntimeMagBeam", "NextBindingId", ""),
            loaded.nextBindingId_
        )
        || loaded.nextBindingId_ <= 0
        || !parseSize(
            ini.getValue("RuntimeMagBeam", "Count", ""),
            count
        )
        || count > limits_.maxBindings) {
        return fail("invalid RuntimeMagBeam header");
    }

    loaded.bindings_.reserve(count);
    std::set<int> bindingIds;
    std::set<int> sourceIds;
    std::set<int> targetIds;
    int maximumBindingId = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const std::string section =
            "RuntimeMagBeam.Binding." + std::to_string(index);
        RuntimeMagBeamBindingState binding;
        int kind = -1;
        int phase = -1;
        int reason = -1;
        if (!parseInt(
                ini.getValue(section, "BindingId", ""),
                binding.bindingId
            )
            || !parseInt(
                ini.getValue(section, "SourceObjectId", ""),
                binding.sourceObjectId
            )
            || !hexDecode(
                ini.getValue(section, "SourceOwner", ""),
                limits_.maxTextLength,
                binding.sourceOwner
            )
            || !hexDecode(
                ini.getValue(section, "SourceType", ""),
                limits_.maxTextLength,
                binding.sourceTypeId
            )
            || !parseInt(
                ini.getValue(section, "TargetObjectId", ""),
                binding.targetObjectId
            )
            || !hexDecode(
                ini.getValue(section, "TargetOwner", ""),
                limits_.maxTextLength,
                binding.targetOwner
            )
            || !hexDecode(
                ini.getValue(section, "TargetType", ""),
                limits_.maxTextLength,
                binding.targetTypeId
            )
            || !parseInt(ini.getValue(section, "TargetKind", ""), kind)
            || !kindValueValid(kind)
            || !parseDouble(
                ini.getValue(section, "TargetWeight", ""),
                binding.targetWeight
            )
            || !parseInt(
                ini.getValue(section, "TargetCurrentStrength", ""),
                binding.targetCurrentStrength
            )
            || !parseInt(
                ini.getValue(section, "TargetMaximumStrength", ""),
                binding.targetMaximumStrength
            )
            || !parseInt(
                ini.getValue(section, "TargetSuppressionThreshold", ""),
                binding.targetSuppressionThreshold
            )
            || !hexDecode(
                ini.getValue(section, "Weapon", ""),
                limits_.maxTextLength,
                binding.weaponId
            )
            || !hexDecode(
                ini.getValue(section, "Warhead", ""),
                limits_.maxTextLength,
                binding.warheadId
            )
            || !hexDecode(
                ini.getValue(section, "Locomotor", ""),
                limits_.maxTextLength,
                binding.locomotorId
            )) {
            return fail("invalid RuntimeMagBeam binding "
                + std::to_string(index));
        }
        binding.targetKind = static_cast<RuntimeMagBeamTargetKind>(kind);

        if (!legacy) {
            if (!parseKinematics(ini, section, binding.kinematics)
                || !hexDecode(
                    ini.getValue(section, "OriginalLocomotor", ""),
                    limits_.maxTextLength,
                    binding.originalLocomotorId
                )
                || !parseBool(
                    ini.getValue(section, "OriginalLocomotorKnown", ""),
                    binding.originalLocomotorKnown
                )
                || !parseBool(
                    ini.getValue(section, "MagBeamVisual", ""),
                    binding.magBeamVisual
                )
                || !parseInt(
                    ini.getValue(section, "RawDamage", ""),
                    binding.rawDamage
                )
                || !parseDouble(
                    ini.getValue(
                        section,
                        "FallingDamageMultiplier",
                        ""
                    ),
                    binding.fallingDamageMultiplier
                )
                || !parseBool(
                    ini.getValue(section, "CurrentStrengthDamage", ""),
                    binding.currentStrengthDamage
                )
                || !parseInt(ini.getValue(section, "Phase", ""), phase)
                || !phaseValueValid(phase)
                || !parseInt(
                    ini.getValue(section, "ReleaseReason", ""),
                    reason
                )
                || !reasonValueValid(reason)
                || !parseInt(
                    ini.getValue(section, "AcquiredTick", ""),
                    binding.acquiredTick
                )
                || !parseInt(
                    ini.getValue(section, "PhaseChangedTick", ""),
                    binding.phaseChangedTick
                )) {
                return fail("invalid RuntimeMagBeam binding "
                    + std::to_string(index));
            }
        } else {
            RuntimeMagBeamLocation legacyLocation;
            RuntimeMagBeamLocation ignoredLanding;
            int ignoredInterval = 0;
            int ignoredLiftTicks = 0;
            int ignoredFallTicks = 0;
            int ignoredNextDamageTick = 0;
            if (!parseLocation(
                    ini.getValue(section, "TargetLocation", ""),
                    legacyLocation
                )
                || !parseInt(
                    ini.getValue(section, "ContinuousDamage", ""),
                    binding.rawDamage
                )
                || !parseInt(
                    ini.getValue(section, "DamageInterval", ""),
                    ignoredInterval
                )
                || ignoredInterval < 1
                || !parseInt(
                    ini.getValue(section, "LiftTicks", ""),
                    ignoredLiftTicks
                )
                || ignoredLiftTicks < 1
                || !parseInt(
                    ini.getValue(section, "FallTicks", ""),
                    ignoredFallTicks
                )
                || ignoredFallTicks < 1
                || !parseDouble(
                    ini.getValue(
                        section,
                        "FallingDamageMultiplier",
                        ""
                    ),
                    binding.fallingDamageMultiplier
                )
                || !parseBool(
                    ini.getValue(section, "CurrentStrengthDamage", ""),
                    binding.currentStrengthDamage
                )
                || !parseInt(ini.getValue(section, "Phase", ""), phase)
                || !phaseValueValid(phase)
                || !parseInt(
                    ini.getValue(section, "ReleaseReason", ""),
                    reason
                )
                || reason < 0 || reason > 8
                || !parseInt(
                    ini.getValue(section, "AcquiredTick", ""),
                    binding.acquiredTick
                )
                || !parseInt(
                    ini.getValue(section, "PhaseStartedTick", ""),
                    binding.phaseChangedTick
                )
                || !parseInt(
                    ini.getValue(section, "NextDamageTick", ""),
                    ignoredNextDamageTick
                )
                || ignoredNextDamageTick < binding.acquiredTick
                || !parseLocation(
                    ini.getValue(section, "Landing", ""),
                    ignoredLanding
                )) {
                return fail("invalid legacy RuntimeMagBeam binding "
                    + std::to_string(index));
            }
            binding.kinematics.location = legacyLocation;
            binding.kinematics.worldX = legacyLocation.x;
            binding.kinematics.worldY = legacyLocation.y;
            binding.kinematics.worldZ = legacyLocation.level;
            binding.originalLocomotorId.clear();
            binding.originalLocomotorKnown = false;
            binding.magBeamVisual = true;
            // Schema 1 never persisted the actual bullet Damage or its
            // synthetic maximumTargetWeight.  The binding had already passed
            // acquisition, so retain a safe diagnostic ceiling that remains
            // strictly above the persisted target Weight.  It never drives
            // HP damage or an automatic phase transition.
            if (!(static_cast<double>(binding.rawDamage)
                    > binding.targetWeight)) {
                binding.rawDamage = static_cast<int>(
                    std::floor(binding.targetWeight)
                ) + 1;
            }
        }

        binding.phase = static_cast<RuntimeMagBeamPhase>(phase);
        binding.releaseReason =
            static_cast<RuntimeMagBeamReleaseReason>(reason);
        if (!loadedBindingValid(
                binding,
                limits_,
                loaded.lastAdvancedTick_,
                legacy
            )
            || !bindingIds.insert(binding.bindingId).second
            || !sourceIds.insert(binding.sourceObjectId).second
            || !targetIds.insert(binding.targetObjectId).second) {
            return fail("unsafe RuntimeMagBeam binding "
                + std::to_string(index));
        }
        maximumBindingId = std::max(maximumBindingId, binding.bindingId);
        loaded.bindings_.push_back(std::move(binding));
    }
    if (loaded.nextBindingId_ <= maximumBindingId) {
        return fail("RuntimeMagBeam next binding id is not monotonic");
    }
    if (loaded.bindings_.empty()) {
        loaded.lastAdvancedTick_ = -1;
    }
    loaded.sortState();
    *this = std::move(loaded);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

std::uint64_t RuntimeMagBeamSystemState::stateHash() const {
    std::uint64_t hash = kFnvOffset;
    hashInt(hash, lastAdvancedTick_);
    hashInt(hash, nextBindingId_);
    hashUint64(hash, limits_.maxBindings);
    hashUint64(hash, limits_.maxActionsPerAdvance);
    hashUint64(hash, limits_.maxInputRecords);
    hashUint64(hash, limits_.maxTextLength);
    hashInt(hash, limits_.maxRecursionDepth);
    hashUint64(hash, bindings_.size());
    for (const RuntimeMagBeamBindingState& binding : bindings_) {
        hashInt(hash, binding.bindingId);
        hashInt(hash, binding.sourceObjectId);
        hashText(hash, binding.sourceOwner);
        hashText(hash, binding.sourceTypeId);
        hashInt(hash, binding.targetObjectId);
        hashText(hash, binding.targetOwner);
        hashText(hash, binding.targetTypeId);
        hashByte(hash, static_cast<std::uint8_t>(binding.targetKind));
        hashDouble(hash, binding.targetWeight);
        hashInt(hash, binding.targetCurrentStrength);
        hashInt(hash, binding.targetMaximumStrength);
        hashInt(hash, binding.targetSuppressionThreshold);
        hashKinematics(hash, binding.kinematics);
        hashText(hash, binding.weaponId);
        hashText(hash, binding.warheadId);
        hashText(hash, binding.locomotorId);
        hashText(hash, binding.originalLocomotorId);
        hashBool(hash, binding.originalLocomotorKnown);
        hashBool(hash, binding.magBeamVisual);
        hashInt(hash, binding.rawDamage);
        hashDouble(hash, binding.fallingDamageMultiplier);
        hashBool(hash, binding.currentStrengthDamage);
        hashByte(hash, static_cast<std::uint8_t>(binding.phase));
        hashByte(hash, static_cast<std::uint8_t>(binding.releaseReason));
        hashInt(hash, binding.acquiredTick);
        hashInt(hash, binding.phaseChangedTick);
    }
    return hash;
}

const RuntimeMagBeamBindingState* RuntimeMagBeamSystemState::findBinding(
    int bindingId
) const {
    const auto iterator = std::lower_bound(
        bindings_.begin(),
        bindings_.end(),
        bindingId,
        [](const RuntimeMagBeamBindingState& binding, int id) {
            return binding.bindingId < id;
        }
    );
    return iterator != bindings_.end() && iterator->bindingId == bindingId
        ? &*iterator
        : nullptr;
}

const RuntimeMagBeamLimits& RuntimeMagBeamSystemState::limits() const {
    return limits_;
}

const std::vector<RuntimeMagBeamBindingState>&
RuntimeMagBeamSystemState::bindings() const {
    return bindings_;
}

int RuntimeMagBeamSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

int RuntimeMagBeamSystemState::nextBindingId() const {
    return nextBindingId_;
}

void RuntimeMagBeamSystemState::sortState() {
    std::sort(
        bindings_.begin(),
        bindings_.end(),
        [](const auto& left, const auto& right) {
            return left.bindingId < right.bindingId;
        }
    );
}

const char* runtimeMagBeamVetoName(RuntimeMagBeamVeto veto) {
    switch (veto) {
    case RuntimeMagBeamVeto::None: return "none";
    case RuntimeMagBeamVeto::InvalidTick: return "invalid-tick";
    case RuntimeMagBeamVeto::InvalidSource: return "invalid-source";
    case RuntimeMagBeamVeto::InvalidOwner: return "invalid-owner";
    case RuntimeMagBeamVeto::InvalidTarget: return "invalid-target";
    case RuntimeMagBeamVeto::FriendlyTarget: return "friendly-target";
    case RuntimeMagBeamVeto::IllegalTargetKind: return "illegal-target-kind";
    case RuntimeMagBeamVeto::TargetAirborne: return "target-airborne";
    case RuntimeMagBeamVeto::TargetImmune: return "target-immune";
    case RuntimeMagBeamVeto::TargetTooHeavy: return "target-too-heavy";
    case RuntimeMagBeamVeto::InvalidWeapon: return "invalid-weapon";
    case RuntimeMagBeamVeto::InvalidRules: return "invalid-rules";
    case RuntimeMagBeamVeto::RecursiveRequest: return "recursive-request";
    case RuntimeMagBeamVeto::SourceBusy: return "source-busy";
    case RuntimeMagBeamVeto::TargetBusy: return "target-busy";
    case RuntimeMagBeamVeto::BindingLimitReached:
        return "binding-limit-reached";
    case RuntimeMagBeamVeto::IdExhausted: return "id-exhausted";
    }
    return "unknown";
}

const char* runtimeMagBeamPhaseName(RuntimeMagBeamPhase phase) {
    switch (phase) {
    case RuntimeMagBeamPhase::Acquired: return "acquired";
    case RuntimeMagBeamPhase::Lifting: return "lifting";
    case RuntimeMagBeamPhase::Carried: return "carried";
    case RuntimeMagBeamPhase::Falling: return "falling";
    }
    return "unknown";
}

const char* runtimeMagBeamActionName(RuntimeMagBeamActionKind kind) {
    switch (kind) {
    case RuntimeMagBeamActionKind::Acquired: return "acquired";
    case RuntimeMagBeamActionKind::ApplyLocomotor: return "apply-locomotor";
    case RuntimeMagBeamActionKind::LiftStarted: return "lift-started";
    case RuntimeMagBeamActionKind::Carried: return "carried";
    case RuntimeMagBeamActionKind::BeginFall: return "begin-fall";
    case RuntimeMagBeamActionKind::ResolveLanding:
        return "resolve-landing";
    case RuntimeMagBeamActionKind::RestoreLocomotor:
        return "restore-locomotor";
    case RuntimeMagBeamActionKind::Released: return "released";
    case RuntimeMagBeamActionKind::Cancelled: return "cancelled";
    }
    return "unknown";
}

const char* runtimeMagBeamReleaseReasonName(
    RuntimeMagBeamReleaseReason reason
) {
    switch (reason) {
    case RuntimeMagBeamReleaseReason::None: return "none";
    case RuntimeMagBeamReleaseReason::Explicit: return "explicit";
    case RuntimeMagBeamReleaseReason::SourceMissing: return "source-missing";
    case RuntimeMagBeamReleaseReason::SourceDead: return "source-dead";
    case RuntimeMagBeamReleaseReason::SourceIdentityChanged:
        return "source-identity-changed";
    case RuntimeMagBeamReleaseReason::TargetMissing: return "target-missing";
    case RuntimeMagBeamReleaseReason::TargetDead: return "target-dead";
    case RuntimeMagBeamReleaseReason::TargetIdentityChanged:
        return "target-identity-changed";
    case RuntimeMagBeamReleaseReason::Reconciled: return "reconciled";
    case RuntimeMagBeamReleaseReason::GentleLanding:
        return "gentle-landing";
    case RuntimeMagBeamReleaseReason::Impact: return "impact";
    }
    return "unknown";
}
