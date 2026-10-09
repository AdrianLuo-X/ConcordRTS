#include "RuntimeLightingSystem.h"

#include "IniFile.h"

#include <algorithm>
#include <bit>
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

constexpr std::size_t maximumThresholdWatchers = 4096;
constexpr double maximumLightingValue = 16.0;
constexpr double maximumAmbientRate = 1000000.0;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

bool finiteInRange(double value, double minimum, double maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

double sanitizeValue(
    double value,
    double fallback,
    double minimum,
    double maximum,
    bool& changed
) {
    if (!std::isfinite(value)) {
        changed = true;
        return fallback;
    }
    const double sanitized = std::clamp(value, minimum, maximum);
    changed = changed || sanitized != value;
    return sanitized;
}

MapLightingProfile sanitizeProfile(
    const MapLightingProfile& source,
    const MapLightingProfile& fallback,
    bool& changed
) {
    MapLightingProfile result;
    result.ambient = sanitizeValue(
        source.ambient,
        fallback.ambient,
        -maximumLightingValue,
        maximumLightingValue,
        changed
    );
    result.red = sanitizeValue(
        source.red,
        fallback.red,
        0.0,
        maximumLightingValue,
        changed
    );
    result.green = sanitizeValue(
        source.green,
        fallback.green,
        0.0,
        maximumLightingValue,
        changed
    );
    result.blue = sanitizeValue(
        source.blue,
        fallback.blue,
        0.0,
        maximumLightingValue,
        changed
    );
    result.ground = sanitizeValue(
        source.ground,
        fallback.ground,
        -maximumLightingValue,
        maximumLightingValue,
        changed
    );
    result.level = sanitizeValue(
        source.level,
        fallback.level,
        -maximumLightingValue,
        maximumLightingValue,
        changed
    );
    return result;
}

MapLighting sanitizeLighting(const MapLighting& source, bool& changed) {
    const MapLighting fallback;
    MapLighting result = source;
    result.base = sanitizeProfile(source.base, fallback.base, changed);
    result.ion = sanitizeProfile(source.ion, fallback.ion, changed);
    result.dominator = sanitizeProfile(
        source.dominator,
        fallback.dominator,
        changed
    );
    result.dominatorAmbientChangeRate = sanitizeValue(
        source.dominatorAmbientChangeRate,
        fallback.dominatorAmbientChangeRate,
        0.0,
        maximumAmbientRate,
        changed
    );
    return result;
}

bool profilesEqual(
    const MapLightingProfile& left,
    const MapLightingProfile& right
) {
    return left.ambient == right.ambient
        && left.red == right.red
        && left.green == right.green
        && left.blue == right.blue
        && left.ground == right.ground
        && left.level == right.level;
}

bool authoredLightingEqual(
    const MapLighting& left,
    const MapLighting& right
) {
    return profilesEqual(left.base, right.base)
        && profilesEqual(left.ion, right.ion)
        && profilesEqual(left.dominator, right.dominator)
        && left.dominatorAmbientChangeRate
            == right.dominatorAmbientChangeRate;
}

int ambientDelayTicks(
    int fixedTickHz,
    double rate
) {
    if (!std::isfinite(rate) || rate <= 0.0) {
        return 0;
    }
    const long double ticks = 60.0L
        * static_cast<long double>(std::max(1, fixedTickHz))
        * static_cast<long double>(rate);
    if (ticks >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return std::max(0, static_cast<int>(std::floor(ticks)));
}

bool parseIntStrict(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    const char* first = text.data();
    const char* last = first + text.size();
    const auto parsed = std::from_chars(first, last, value);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

bool parseDoubleStrict(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::noskipws >> value;
    return input && input.eof() && std::isfinite(value);
}

void saveProfile(
    std::ostream& output,
    const std::string& prefix,
    const MapLightingProfile& profile
) {
    output << prefix << "Ambient=" << profile.ambient << "\n";
    output << prefix << "Red=" << profile.red << "\n";
    output << prefix << "Green=" << profile.green << "\n";
    output << prefix << "Blue=" << profile.blue << "\n";
    output << prefix << "Ground=" << profile.ground << "\n";
    output << prefix << "Level=" << profile.level << "\n";
}

bool loadProfile(
    const IniFile& ini,
    const std::string& section,
    const std::string& prefix,
    MapLightingProfile& profile
) {
    return parseDoubleStrict(
            ini.getValue(section, prefix + "Ambient", ""),
            profile.ambient
        )
        && parseDoubleStrict(
            ini.getValue(section, prefix + "Red", ""),
            profile.red
        )
        && parseDoubleStrict(
            ini.getValue(section, prefix + "Green", ""),
            profile.green
        )
        && parseDoubleStrict(
            ini.getValue(section, prefix + "Blue", ""),
            profile.blue
        )
        && parseDoubleStrict(
            ini.getValue(section, prefix + "Ground", ""),
            profile.ground
        )
        && parseDoubleStrict(
            ini.getValue(section, prefix + "Level", ""),
            profile.level
        )
        && finiteInRange(
            profile.ambient,
            -maximumLightingValue,
            maximumLightingValue
        )
        && finiteInRange(profile.red, 0.0, maximumLightingValue)
        && finiteInRange(profile.green, 0.0, maximumLightingValue)
        && finiteInRange(profile.blue, 0.0, maximumLightingValue)
        && finiteInRange(
            profile.ground,
            -maximumLightingValue,
            maximumLightingValue
        )
        && finiteInRange(
            profile.level,
            -maximumLightingValue,
            maximumLightingValue
        );
}

std::uint64_t hashText(std::string_view text) {
    std::uint64_t hash = fnvOffset;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= fnvPrime;
    }
    return hash;
}

int channelWithMultiplier(unsigned int channel, double multiplier) {
    if (!std::isfinite(multiplier)) {
        multiplier = 1.0;
    }
    return std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(channel) * std::max(0.0, multiplier)
        )),
        0,
        255
    );
}

} // namespace

RuntimeLightingSystemState::RuntimeLightingSystemState() {
    reset();
}

RuntimeLightingSystemState::RuntimeLightingSystemState(
    const MapLighting& lighting,
    RuntimeLightingConfig config
) {
    reset(lighting, config);
}

void RuntimeLightingSystemState::reset(
    const MapLighting& lighting,
    RuntimeLightingConfig config
) {
    bool ignored = false;
    authoredLighting_ = sanitizeLighting(lighting, ignored);
    config.fixedTickHz = std::clamp(config.fixedTickHz, 1, 1000);
    config.ambientChangeRate = std::isfinite(config.ambientChangeRate)
        ? std::clamp(config.ambientChangeRate, 0.0, maximumAmbientRate)
        : 0.0;
    config.ambientChangeStep = std::isfinite(config.ambientChangeStep)
        ? std::clamp(config.ambientChangeStep, 0.0, maximumLightingValue)
        : 0.0;
    config_ = config;
    currentProfile_ = authoredLighting_.base;
    mode_ = RuntimeLightingMode::Base;
    lastAdvancedTick_ = -1;
    targetAmbient_.reset();
    ticksUntilAmbientStep_ = -1;
    transitionRateOverride_.reset();
    thresholdWatchers_.clear();
}

const RuntimeLightingConfig& RuntimeLightingSystemState::config() const {
    return config_;
}

const MapLighting& RuntimeLightingSystemState::authoredLighting() const {
    return authoredLighting_;
}

const MapLightingProfile& RuntimeLightingSystemState::currentProfile() const {
    return currentProfile_;
}

const MapLightingProfile& RuntimeLightingSystemState::profile(
    RuntimeLightingMode mode
) const {
    if (mode == RuntimeLightingMode::Ion) {
        return authoredLighting_.ion;
    }
    if (mode == RuntimeLightingMode::Dominator) {
        return authoredLighting_.dominator;
    }
    return authoredLighting_.base;
}

RuntimeLightingMode RuntimeLightingSystemState::mode() const {
    return mode_;
}

int RuntimeLightingSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

std::vector<int> RuntimeLightingSystemState::thresholdWatcherEventIds() const {
    std::vector<int> result;
    result.reserve(thresholdWatchers_.size());
    for (const ThresholdWatcher& watcher : thresholdWatchers_) {
        result.push_back(watcher.eventId);
    }
    return result;
}

bool RuntimeLightingSystemState::hasAmbientTarget() const {
    return targetAmbient_.has_value();
}

double RuntimeLightingSystemState::targetAmbient() const {
    return targetAmbient_.value_or(currentProfile_.ambient);
}

int RuntimeLightingSystemState::ticksUntilAmbientStep() const {
    return ticksUntilAmbientStep_;
}

RuntimeLightingChange RuntimeLightingSystemState::activateProfile(
    RuntimeLightingMode mode,
    int currentTick,
    bool transitionAmbient
) {
    const double previousAmbient = currentProfile_.ambient;
    const MapLightingProfile target = profile(mode);
    currentProfile_.red = target.red;
    currentProfile_.green = target.green;
    currentProfile_.blue = target.blue;
    currentProfile_.ground = target.ground;
    currentProfile_.level = target.level;
    mode_ = mode;
    ticksUntilAmbientStep_ = -1;
    transitionRateOverride_.reset();
    if (transitionAmbient && currentProfile_.ambient != target.ambient) {
        targetAmbient_ = target.ambient;
        if (mode == RuntimeLightingMode::Dominator) {
            transitionRateOverride_ =
                authoredLighting_.dominatorAmbientChangeRate;
        }
    } else {
        currentProfile_.ambient = target.ambient;
        targetAmbient_.reset();
    }
    return {
        RuntimeLightingChangeKind::ProfileActivated,
        currentTick,
        mode_,
        previousAmbient,
        currentProfile_.ambient
    };
}

RuntimeLightingChange RuntimeLightingSystemState::activateIon(int currentTick) {
    return activateProfile(RuntimeLightingMode::Ion, currentTick, false);
}

RuntimeLightingChange RuntimeLightingSystemState::activateDominator(
    int currentTick
) {
    return activateProfile(RuntimeLightingMode::Dominator, currentTick, true);
}

RuntimeLightingChange RuntimeLightingSystemState::restoreBase(
    int currentTick,
    bool transitionAmbient
) {
    return activateProfile(
        RuntimeLightingMode::Base,
        currentTick,
        transitionAmbient
    );
}

void RuntimeLightingSystemState::setAmbientChangeRate(double rate) {
    if (std::isfinite(rate)) {
        config_.ambientChangeRate = std::clamp(
            rate,
            0.0,
            maximumAmbientRate
        );
    }
}

void RuntimeLightingSystemState::setAmbientChangeStep(double step) {
    if (std::isfinite(step)) {
        config_.ambientChangeStep = std::clamp(
            step,
            0.0,
            maximumLightingValue
        );
    }
}

void RuntimeLightingSystemState::setTargetAmbient(double ambient) {
    if (!std::isfinite(ambient)) {
        return;
    }
    targetAmbient_ = std::clamp(
        ambient,
        -maximumLightingValue,
        maximumLightingValue
    );
    transitionRateOverride_.reset();
}

void RuntimeLightingSystemState::setTargetAmbientPercent(int percent) {
    setTargetAmbient(static_cast<double>(percent) / 100.0);
}

RuntimeLightingTriggerActionResult
RuntimeLightingSystemState::applyTriggerAction(
    const RuntimeLightingTriggerAction& action
) {
    RuntimeLightingTriggerActionResult result;
    result.actionType = action.actionType;
    if (action.actionType == 71 || action.actionType == 72) {
        result.supported = true;
        result.decodedValue = runtimeLightingDecodeTriggerFloat(
            action.rawValue
        );
        if (!finiteInRange(
                result.decodedValue,
                0.0,
                action.actionType == 71
                    ? maximumLightingValue
                    : maximumAmbientRate
            )) {
            result.reason = "encoded ambient value is invalid";
            return result;
        }
        if (action.actionType == 71) {
            setAmbientChangeStep(result.decodedValue);
        } else {
            setAmbientChangeRate(result.decodedValue);
        }
        result.accepted = true;
        return result;
    }
    if (action.actionType == 73) {
        result.supported = true;
        if (action.rawValue < -1600 || action.rawValue > 1600) {
            result.reason = "ambient target percentage is out of range";
            return result;
        }
        result.decodedValue = static_cast<double>(action.rawValue) / 100.0;
        setTargetAmbient(result.decodedValue);
        result.accepted = true;
        return result;
    }
    result.reason = "unsupported lighting trigger action";
    return result;
}

RuntimeLightingThresholdResult RuntimeLightingSystemState::evaluateThreshold(
    const RuntimeLightingThresholdRequest& request
) {
    RuntimeLightingThresholdResult result;
    result.currentAmbient = currentProfile_.ambient;
    if (request.eventType == 45) {
        result.supported = true;
        result.direction = RuntimeLightingThresholdDirection::Below;
    } else if (request.eventType == 46) {
        result.supported = true;
        result.direction = RuntimeLightingThresholdDirection::Above;
    } else {
        return result;
    }
    if (request.eventId < 0
        || request.thresholdPercent < -1600
        || request.thresholdPercent > 1600) {
        return result;
    }
    result.valid = true;
    result.threshold = static_cast<double>(request.thresholdPercent) / 100.0;

    sortThresholdWatchers();
    auto found = std::lower_bound(
        thresholdWatchers_.begin(),
        thresholdWatchers_.end(),
        request.eventId,
        [](const ThresholdWatcher& watcher, int eventId) {
            return watcher.eventId < eventId;
        }
    );
    if (found == thresholdWatchers_.end() || found->eventId != request.eventId) {
        if (thresholdWatchers_.size() < maximumThresholdWatchers) {
            thresholdWatchers_.insert(
                found,
                ThresholdWatcher{
                    request.eventId,
                    request.eventType,
                    request.thresholdPercent,
                    currentProfile_.ambient
                }
            );
        } else {
            result.valid = false;
        }
        return result;
    }
    if (found->eventType != request.eventType
        || found->thresholdPercent != request.thresholdPercent) {
        found->eventType = request.eventType;
        found->thresholdPercent = request.thresholdPercent;
        found->previousAmbient = currentProfile_.ambient;
        return result;
    }

    result.hadPreviousSample = true;
    result.previousAmbient = found->previousAmbient;
    if (found->previousAmbient != currentProfile_.ambient) {
        if (request.eventType == 46) {
            result.triggered = currentProfile_.ambient >= result.threshold
                && found->previousAmbient < result.threshold;
        } else {
            result.triggered = currentProfile_.ambient <= result.threshold
                && found->previousAmbient > result.threshold;
        }
    }
    found->previousAmbient = currentProfile_.ambient;
    return result;
}

std::vector<RuntimeLightingChange> RuntimeLightingSystemState::tick(
    int currentTick
) {
    std::vector<RuntimeLightingChange> changes;
    if (currentTick < 0 || currentTick <= lastAdvancedTick_) {
        return changes;
    }
    long long remainingTicks = lastAdvancedTick_ < 0
        ? 1LL
        : static_cast<long long>(currentTick)
            - static_cast<long long>(lastAdvancedTick_);
    long long logicalTick = static_cast<long long>(currentTick)
        - remainingTicks;
    lastAdvancedTick_ = currentTick;
    if (!targetAmbient_.has_value()) {
        return changes;
    }

    const double step = config_.ambientChangeStep;
    if (!std::isfinite(step) || step <= 0.0) {
        const double rate = transitionRateOverride_.value_or(
            config_.ambientChangeRate
        );
        ticksUntilAmbientStep_ = ambientDelayTicks(
            config_.fixedTickHz,
            rate
        );
        return changes;
    }

    while (remainingTicks > 0 && targetAmbient_.has_value()) {
        const double rate = transitionRateOverride_.value_or(
            config_.ambientChangeRate
        );
        if (ticksUntilAmbientStep_ < 0) {
            ticksUntilAmbientStep_ = ambientDelayTicks(
                config_.fixedTickHz,
                rate
            );
        }
        if (ticksUntilAmbientStep_ > 0) {
            const long long consumed = std::min<long long>(
                remainingTicks,
                ticksUntilAmbientStep_
            );
            ticksUntilAmbientStep_ -= static_cast<int>(consumed);
            remainingTicks -= consumed;
            logicalTick += consumed;
            continue;
        }

        --remainingTicks;
        ++logicalTick;
        const double before = currentProfile_.ambient;
        const double difference = *targetAmbient_ - before;
        if (difference == 0.0) {
            const double reached = *targetAmbient_;
            targetAmbient_.reset();
            transitionRateOverride_.reset();
            ticksUntilAmbientStep_ = -1;
            changes.push_back({
                RuntimeLightingChangeKind::AmbientTargetReached,
                static_cast<int>(std::min<long long>(
                    logicalTick,
                    std::numeric_limits<int>::max()
                )),
                mode_,
                reached,
                reached
            });
            continue;
        }
        currentProfile_.ambient = std::abs(difference) <= step
            ? *targetAmbient_
            : before + std::copysign(step, difference);
        changes.push_back({
            RuntimeLightingChangeKind::AmbientChanged,
            static_cast<int>(std::min<long long>(
                logicalTick,
                std::numeric_limits<int>::max()
            )),
            mode_,
            before,
            currentProfile_.ambient
        });
        ticksUntilAmbientStep_ = -1;
        if (currentProfile_.ambient == *targetAmbient_) {
            const double reached = *targetAmbient_;
            targetAmbient_.reset();
            transitionRateOverride_.reset();
            changes.push_back({
                RuntimeLightingChangeKind::AmbientTargetReached,
                static_cast<int>(std::min<long long>(
                    logicalTick,
                    std::numeric_limits<int>::max()
                )),
                mode_,
                reached,
                reached
            });
        }
    }
    return changes;
}

RuntimeLightingTint RuntimeLightingSystemState::renderTint(
    double elevation
) const {
    return runtimeLightingTintForProfile(currentProfile_, elevation);
}

RuntimeLightingReconcileReport RuntimeLightingSystemState::reconcile(
    const MapLighting& lighting
) {
    RuntimeLightingReconcileReport report;
    report.contextValid = true;
    bool incomingChanged = false;
    const MapLighting sanitized = sanitizeLighting(lighting, incomingChanged);
    report.valuesSanitized = incomingChanged;
    report.profilesMatched = authoredLightingEqual(
        authoredLighting_,
        sanitized
    );
    if (!report.profilesMatched) {
        const RuntimeLightingConfig retainedConfig = config_;
        reset(sanitized, retainedConfig);
        report.stateReset = true;
        return report;
    }
    authoredLighting_ = sanitized;

    bool currentChanged = false;
    const MapLightingProfile target = profile(mode_);
    currentProfile_ = sanitizeProfile(
        currentProfile_,
        target,
        currentChanged
    );
    if (currentProfile_.red != target.red
        || currentProfile_.green != target.green
        || currentProfile_.blue != target.blue
        || currentProfile_.ground != target.ground
        || currentProfile_.level != target.level) {
        currentProfile_.red = target.red;
        currentProfile_.green = target.green;
        currentProfile_.blue = target.blue;
        currentProfile_.ground = target.ground;
        currentProfile_.level = target.level;
        currentChanged = true;
    }
    if (targetAmbient_.has_value()) {
        const double sanitizedTarget = std::clamp(
            *targetAmbient_,
            -maximumLightingValue,
            maximumLightingValue
        );
        currentChanged = currentChanged || sanitizedTarget != *targetAmbient_;
        targetAmbient_ = sanitizedTarget;
    } else {
        ticksUntilAmbientStep_ = -1;
        transitionRateOverride_.reset();
    }
    report.valuesSanitized = report.valuesSanitized || currentChanged;

    sortThresholdWatchers();
    std::vector<ThresholdWatcher> retained;
    retained.reserve(thresholdWatchers_.size());
    std::set<int> ids;
    for (const ThresholdWatcher& watcher : thresholdWatchers_) {
        if (watcher.eventId < 0
            || (watcher.eventType != 45 && watcher.eventType != 46)
            || watcher.thresholdPercent < -1600
            || watcher.thresholdPercent > 1600
            || !finiteInRange(
                watcher.previousAmbient,
                -maximumLightingValue,
                maximumLightingValue
            )
            || !ids.insert(watcher.eventId).second
            || retained.size() >= maximumThresholdWatchers) {
            ++report.removedThresholdWatchers;
            continue;
        }
        retained.push_back(watcher);
    }
    thresholdWatchers_ = std::move(retained);
    report.retainedThresholdWatchers = thresholdWatchers_.size();
    return report;
}

void RuntimeLightingSystemState::save(std::ostream& output) const {
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "[LightingRuntime]\n";
    output << "Version=1\n";
    output << "FixedTickHz=" << config_.fixedTickHz << "\n";
    output << "AmbientChangeRate=" << config_.ambientChangeRate << "\n";
    output << "AmbientChangeStep=" << config_.ambientChangeStep << "\n";
    output << "Mode=" << runtimeLightingModeName(mode_) << "\n";
    output << "LastAdvancedTick=" << lastAdvancedTick_ << "\n";
    output << "TargetAmbientPresent=" << (targetAmbient_.has_value() ? 1 : 0)
           << "\n";
    output << "TargetAmbient=" << targetAmbient_.value_or(0.0) << "\n";
    output << "TicksUntilAmbientStep=" << ticksUntilAmbientStep_ << "\n";
    output << "TransitionRateOverridePresent="
           << (transitionRateOverride_.has_value() ? 1 : 0) << "\n";
    output << "TransitionRateOverride="
           << transitionRateOverride_.value_or(0.0) << "\n";
    saveProfile(output, "AuthoredBase", authoredLighting_.base);
    saveProfile(output, "AuthoredIon", authoredLighting_.ion);
    saveProfile(output, "AuthoredDominator", authoredLighting_.dominator);
    output << "DominatorAmbientChangeRate="
           << authoredLighting_.dominatorAmbientChangeRate << "\n";
    saveProfile(output, "Current", currentProfile_);

    std::vector<ThresholdWatcher> watchers = thresholdWatchers_;
    std::sort(
        watchers.begin(),
        watchers.end(),
        [](const ThresholdWatcher& left, const ThresholdWatcher& right) {
            return left.eventId < right.eventId;
        }
    );
    output << "WatcherCount=" << watchers.size() << "\n\n";
    for (std::size_t index = 0; index < watchers.size(); ++index) {
        const ThresholdWatcher& watcher = watchers[index];
        output << "[LightingRuntime.Watcher." << index << "]\n";
        output << "EventId=" << watcher.eventId << "\n";
        output << "EventType=" << watcher.eventType << "\n";
        output << "ThresholdPercent=" << watcher.thresholdPercent << "\n";
        output << "PreviousAmbient=" << watcher.previousAmbient << "\n\n";
    }
}

bool RuntimeLightingSystemState::load(
    const IniFile& ini,
    const MapLighting& lighting,
    std::string* errorMessage,
    RuntimeLightingReconcileReport* reconcileReport
) {
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (ini.getSection("LightingRuntime") == nullptr) {
        reset(lighting, config_);
        RuntimeLightingReconcileReport report;
        report.contextValid = true;
        report.profilesMatched = true;
        report.stateReset = true;
        if (reconcileReport != nullptr) {
            *reconcileReport = report;
        }
        return true;
    }

    const std::string section = "LightingRuntime";
    int version = 0;
    int fixedTickHz = 0;
    int lastAdvancedTick = -1;
    int targetPresent = 0;
    int ticksUntilStep = -1;
    int overridePresent = 0;
    int watcherCount = 0;
    double ambientRate = 0.0;
    double ambientStep = 0.0;
    double targetAmbient = 0.0;
    double transitionOverride = 0.0;
    RuntimeLightingMode mode = RuntimeLightingMode::Base;
    MapLighting savedLighting;
    MapLightingProfile current;

    if (!parseIntStrict(ini.getValue(section, "Version", ""), version)
        || version != 1
        || !parseIntStrict(
            ini.getValue(section, "FixedTickHz", ""),
            fixedTickHz
        )
        || fixedTickHz < 1 || fixedTickHz > 1000
        || !parseDoubleStrict(
            ini.getValue(section, "AmbientChangeRate", ""),
            ambientRate
        )
        || !finiteInRange(ambientRate, 0.0, maximumAmbientRate)
        || !parseDoubleStrict(
            ini.getValue(section, "AmbientChangeStep", ""),
            ambientStep
        )
        || !finiteInRange(ambientStep, 0.0, maximumLightingValue)
        || !runtimeLightingModeFromName(
            ini.getValue(section, "Mode", ""),
            mode
        )
        || !parseIntStrict(
            ini.getValue(section, "LastAdvancedTick", ""),
            lastAdvancedTick
        )
        || lastAdvancedTick < -1
        || !parseIntStrict(
            ini.getValue(section, "TargetAmbientPresent", ""),
            targetPresent
        )
        || (targetPresent != 0 && targetPresent != 1)
        || !parseDoubleStrict(
            ini.getValue(section, "TargetAmbient", ""),
            targetAmbient
        )
        || !finiteInRange(
            targetAmbient,
            -maximumLightingValue,
            maximumLightingValue
        )
        || !parseIntStrict(
            ini.getValue(section, "TicksUntilAmbientStep", ""),
            ticksUntilStep
        )
        || ticksUntilStep < -1
        || !parseIntStrict(
            ini.getValue(section, "TransitionRateOverridePresent", ""),
            overridePresent
        )
        || (overridePresent != 0 && overridePresent != 1)
        || !parseDoubleStrict(
            ini.getValue(section, "TransitionRateOverride", ""),
            transitionOverride
        )
        || !finiteInRange(transitionOverride, 0.0, maximumAmbientRate)
        || !loadProfile(ini, section, "AuthoredBase", savedLighting.base)
        || !loadProfile(ini, section, "AuthoredIon", savedLighting.ion)
        || !loadProfile(
            ini,
            section,
            "AuthoredDominator",
            savedLighting.dominator
        )
        || !parseDoubleStrict(
            ini.getValue(section, "DominatorAmbientChangeRate", ""),
            savedLighting.dominatorAmbientChangeRate
        )
        || !finiteInRange(
            savedLighting.dominatorAmbientChangeRate,
            0.0,
            maximumAmbientRate
        )
        || !loadProfile(ini, section, "Current", current)
        || !parseIntStrict(
            ini.getValue(section, "WatcherCount", ""),
            watcherCount
        )
        || watcherCount < 0
        || static_cast<std::size_t>(watcherCount)
            > maximumThresholdWatchers
        || (targetPresent == 0 && ticksUntilStep != -1)
        || (targetPresent == 0 && overridePresent != 0)
        || (lastAdvancedTick < 0 && ticksUntilStep != -1)) {
        return fail("lighting runtime save has invalid header values");
    }

    RuntimeLightingSystemState loaded(
        savedLighting,
        {fixedTickHz, ambientRate, ambientStep}
    );
    loaded.mode_ = mode;
    loaded.currentProfile_ = current;
    loaded.lastAdvancedTick_ = lastAdvancedTick;
    loaded.ticksUntilAmbientStep_ = ticksUntilStep;
    if (targetPresent != 0) {
        loaded.targetAmbient_ = targetAmbient;
    }
    if (overridePresent != 0) {
        loaded.transitionRateOverride_ = transitionOverride;
    }

    std::set<int> watcherIds;
    for (int index = 0; index < watcherCount; ++index) {
        const std::string watcherSection = "LightingRuntime.Watcher."
            + std::to_string(index);
        ThresholdWatcher watcher;
        if (ini.getSection(watcherSection) == nullptr
            || !parseIntStrict(
                ini.getValue(watcherSection, "EventId", ""),
                watcher.eventId
            )
            || watcher.eventId < 0
            || !parseIntStrict(
                ini.getValue(watcherSection, "EventType", ""),
                watcher.eventType
            )
            || (watcher.eventType != 45 && watcher.eventType != 46)
            || !parseIntStrict(
                ini.getValue(watcherSection, "ThresholdPercent", ""),
                watcher.thresholdPercent
            )
            || watcher.thresholdPercent < -1600
            || watcher.thresholdPercent > 1600
            || !parseDoubleStrict(
                ini.getValue(watcherSection, "PreviousAmbient", ""),
                watcher.previousAmbient
            )
            || !finiteInRange(
                watcher.previousAmbient,
                -maximumLightingValue,
                maximumLightingValue
            )
            || !watcherIds.insert(watcher.eventId).second) {
            return fail("lighting runtime save has invalid threshold watcher ["
                + watcherSection + "]");
        }
        loaded.thresholdWatchers_.push_back(watcher);
    }
    loaded.sortThresholdWatchers();

    RuntimeLightingReconcileReport report = loaded.reconcile(lighting);
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = report;
    }
    return true;
}

std::uint64_t RuntimeLightingSystemState::stateHash() const {
    std::ostringstream output;
    save(output);
    return hashText(output.str());
}

void RuntimeLightingSystemState::sortThresholdWatchers() {
    std::sort(
        thresholdWatchers_.begin(),
        thresholdWatchers_.end(),
        [](const ThresholdWatcher& left, const ThresholdWatcher& right) {
            return left.eventId < right.eventId;
        }
    );
}

const char* runtimeLightingModeName(RuntimeLightingMode mode) {
    switch (mode) {
    case RuntimeLightingMode::Ion:
        return "Ion";
    case RuntimeLightingMode::Dominator:
        return "Dominator";
    case RuntimeLightingMode::Base:
    default:
        return "Base";
    }
}

const char* runtimeLightingChangeName(RuntimeLightingChangeKind kind) {
    switch (kind) {
    case RuntimeLightingChangeKind::ProfileActivated:
        return "ProfileActivated";
    case RuntimeLightingChangeKind::AmbientTargetReached:
        return "AmbientTargetReached";
    case RuntimeLightingChangeKind::AmbientChanged:
    default:
        return "AmbientChanged";
    }
}

bool runtimeLightingModeFromName(
    const std::string& value,
    RuntimeLightingMode& mode
) {
    if (value == "Base") {
        mode = RuntimeLightingMode::Base;
        return true;
    }
    if (value == "Ion") {
        mode = RuntimeLightingMode::Ion;
        return true;
    }
    if (value == "Dominator") {
        mode = RuntimeLightingMode::Dominator;
        return true;
    }
    return false;
}

double runtimeLightingDecodeTriggerFloat(std::int32_t rawValue) {
    static_assert(sizeof(float) == sizeof(std::int32_t));
    return static_cast<double>(std::bit_cast<float>(rawValue));
}

RuntimeLightingTint runtimeLightingTintForProfile(
    const MapLightingProfile& profile,
    double elevation
) {
    if (!std::isfinite(elevation)) {
        elevation = 1.0;
    }
    double intensity = profile.ambient
        + profile.ground
        + (profile.level * (elevation - 1.0));
    if (!std::isfinite(intensity)) {
        intensity = 1.0;
    }
    intensity = std::max(0.0, intensity);
    return {
        std::clamp(profile.red * intensity, 0.0, maximumLightingValue),
        std::clamp(profile.green * intensity, 0.0, maximumLightingValue),
        std::clamp(profile.blue * intensity, 0.0, maximumLightingValue)
    };
}

bool runtimeLightingTintIsIdentity(const RuntimeLightingTint& tint) {
    constexpr double epsilon = 1.0e-12;
    return std::isfinite(tint.red)
        && std::isfinite(tint.green)
        && std::isfinite(tint.blue)
        && std::abs(tint.red - 1.0) <= epsilon
        && std::abs(tint.green - 1.0) <= epsilon
        && std::abs(tint.blue - 1.0) <= epsilon;
}

std::uint32_t runtimeApplyLightingTintToPackedDibColor(
    std::uint32_t packedColor,
    const RuntimeLightingTint& tint
) {
    const unsigned int red = (packedColor >> 16U) & 0xFFU;
    const unsigned int green = (packedColor >> 8U) & 0xFFU;
    const unsigned int blue = packedColor & 0xFFU;
    return (packedColor & 0xFF000000U)
        | (static_cast<std::uint32_t>(
                channelWithMultiplier(red, tint.red)
            ) << 16U)
        | (static_cast<std::uint32_t>(
                channelWithMultiplier(green, tint.green)
            ) << 8U)
        | static_cast<std::uint32_t>(
            channelWithMultiplier(blue, tint.blue)
        );
}
