#include "audio/world/RuntimeWorldAudioSystem.h"

#include "core/io/IniFile.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace {

constexpr std::size_t maximumCatalogSounds = 16384;
constexpr std::size_t maximumSavedHandles = 4096;
constexpr std::size_t maximumSavedCommands = 8192;
constexpr std::size_t maximumSamplesPerSound = 256;
constexpr int maximumDelayMilliseconds = 86400000;
constexpr int maximumCoordinateMagnitude = 1048576;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](char character) { return lowerAsciiCharacter(character); }
    );
    return value;
}

bool equalsAsciiIgnoreCase(
    const std::string& left,
    const std::string& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAsciiCharacter(left[index])
            != lowerAsciiCharacter(right[index])) {
            return false;
        }
    }
    return true;
}

bool lessAsciiIgnoreCase(std::string_view left, std::string_view right) {
    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const unsigned char a = static_cast<unsigned char>(
            lowerAsciiCharacter(left[index])
        );
        const unsigned char b = static_cast<unsigned char>(
            lowerAsciiCharacter(right[index])
        );
        if (a != b) {
            return a < b;
        }
    }
    return left.size() < right.size();
}

std::string trimAscii(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && (value[first] == ' ' || value[first] == '\t'
            || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
        && (value[last - 1U] == ' ' || value[last - 1U] == '\t'
            || value[last - 1U] == '\r' || value[last - 1U] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string stripQuotes(std::string value) {
    value = trimAscii(value);
    while (value.size() >= 2U
        && ((value.front() == '"' && value.back() == '"')
            || (value.front() == '\'' && value.back() == '\''))) {
        value = trimAscii(value.substr(1U, value.size() - 2U));
    }
    return value;
}

std::vector<std::string> splitTokens(const std::string& text) {
    std::string normalized = text;
    for (char& character : normalized) {
        if (character == ',' || character == '|') {
            character = ' ';
        }
    }
    std::istringstream stream(normalized);
    std::vector<std::string> result;
    std::string token;
    while (stream >> token) {
        token = stripQuotes(token);
        if (!token.empty()) {
            result.push_back(std::move(token));
        }
    }
    return result;
}

bool containsTokenIgnoreCase(
    const std::vector<std::string>& tokens,
    const std::string& wanted
) {
    return std::any_of(
        tokens.begin(),
        tokens.end(),
        [&wanted](const std::string& token) {
            return equalsAsciiIgnoreCase(token, wanted);
        }
    );
}

bool parseIntStrict(const std::string& text, int& output) {
    const std::string value = trimAscii(text);
    if (value.empty()) {
        return false;
    }
    int parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    output = parsed;
    return true;
}

bool parseUint64Strict(const std::string& text, std::uint64_t& output) {
    const std::string value = trimAscii(text);
    if (value.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    output = parsed;
    return true;
}

bool parseDoubleStrict(const std::string& text, double& output) {
    const std::string value = trimAscii(text);
    if (value.empty()) {
        return false;
    }
    std::istringstream stream(value);
    double parsed = 0.0;
    if (!(stream >> parsed) || !std::isfinite(parsed)) {
        return false;
    }
    std::string trailing;
    if (stream >> trailing) {
        return false;
    }
    output = parsed;
    return true;
}

int readClampedInt(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    int fallback,
    int minimum,
    int maximum,
    std::vector<std::string>* warnings
) {
    const std::string text = ini.getValue(section, key, "");
    if (text.empty()) {
        return fallback;
    }
    int parsed = 0;
    if (!parseIntStrict(text, parsed)) {
        if (warnings != nullptr) {
            warnings->push_back(
                "[" + section + "] " + key
                + " is invalid; inherited/default value was used"
            );
        }
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

double readClampedDouble(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    double fallback,
    double minimum,
    double maximum,
    std::vector<std::string>* warnings
) {
    const std::string text = ini.getValue(section, key, "");
    if (text.empty()) {
        return fallback;
    }
    double parsed = 0.0;
    if (!parseDoubleStrict(text, parsed)) {
        if (warnings != nullptr) {
            warnings->push_back(
                "[" + section + "] " + key
                + " is invalid; inherited/default value was used"
            );
        }
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

std::pair<int, int> readDelay(
    const IniFile& ini,
    const std::string& section,
    std::pair<int, int> fallback,
    std::vector<std::string>* warnings
) {
    const std::string text = ini.getValue(section, "Delay", "");
    if (text.empty()) {
        return fallback;
    }
    std::istringstream stream(text);
    long long first = 0;
    long long second = 0;
    if (!(stream >> first)) {
        if (warnings != nullptr) {
            warnings->push_back(
                "[" + section + "] Delay is invalid; inherited/default value was used"
            );
        }
        return fallback;
    }
    if (!(stream >> second)) {
        stream.clear();
        second = first;
    }
    std::string trailing;
    if (stream >> trailing) {
        if (warnings != nullptr) {
            warnings->push_back(
                "[" + section + "] Delay has extra data; inherited/default value was used"
            );
        }
        return fallback;
    }
    first = std::clamp<long long>(first, 0, maximumDelayMilliseconds);
    second = std::clamp<long long>(second, 0, maximumDelayMilliseconds);
    if (second < first) {
        std::swap(first, second);
    }
    return {
        static_cast<int>(first),
        static_cast<int>(second)
    };
}

std::uint64_t hashText(std::string_view text) {
    std::uint64_t hash = fnvOffset;
    for (const unsigned char character : text) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= fnvPrime;
    }
    return hash;
}

void hashMix(std::uint64_t& hash, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        hash ^= (value >> shift) & 0xffULL;
        hash *= fnvPrime;
    }
}

void hashMixText(std::uint64_t& hash, std::string_view text) {
    for (const unsigned char character : text) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= fnvPrime;
    }
    hash ^= 0xffULL;
    hash *= fnvPrime;
}

int millisecondsToTicks(int milliseconds, int fixedTickHz) {
    if (milliseconds <= 0) {
        return 0;
    }
    const long double ticks =
        static_cast<long double>(milliseconds)
        * static_cast<long double>(std::max(1, fixedTickHz))
        / 1000.0L;
    if (ticks >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return std::max(1, static_cast<int>(std::ceil(ticks - 1.0e-12L)));
}

int saturatedAdd(int left, int right) {
    const long long sum = static_cast<long long>(left)
        + static_cast<long long>(right);
    return sum >= static_cast<long long>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(sum);
}

int deterministicDelayTicks(
    const RuntimeWorldAudioSoundSpec& spec,
    std::uint64_t handleId,
    int playbackOrdinal,
    int fixedTickHz
) {
    const int minimum = millisecondsToTicks(
        spec.delayMinMilliseconds,
        fixedTickHz
    );
    const int maximum = millisecondsToTicks(
        spec.delayMaxMilliseconds,
        fixedTickHz
    );
    if (maximum <= minimum) {
        return minimum;
    }
    std::uint64_t hash = fnvOffset;
    hashMix(hash, handleId);
    hashMix(hash, static_cast<std::uint64_t>(playbackOrdinal));
    hashMixText(hash, lowerAscii(spec.eventId));
    const std::uint64_t width = static_cast<std::uint64_t>(maximum - minimum)
        + 1ULL;
    return minimum + static_cast<int>(hash % width);
}

std::string selectedSample(
    const RuntimeWorldAudioSoundSpec& spec,
    std::uint64_t handleId,
    int playbackOrdinal
) {
    if (spec.samples.empty()) {
        return "";
    }
    if (!spec.randomSample || spec.samples.size() == 1U) {
        return spec.samples[static_cast<std::size_t>(playbackOrdinal)
            % spec.samples.size()];
    }
    std::uint64_t hash = fnvOffset;
    hashMix(hash, handleId);
    hashMix(hash, static_cast<std::uint64_t>(playbackOrdinal));
    hashMixText(hash, lowerAscii(spec.eventId));
    return spec.samples[hash % spec.samples.size()];
}

bool cellsEqual(
    const RuntimeWorldAudioCell& left,
    const RuntimeWorldAudioCell& right
) {
    return left.x == right.x
        && left.y == right.y
        && left.level == right.level;
}

bool validCell(const RuntimeWorldAudioCell& cell) {
    return std::abs(static_cast<long long>(cell.x))
            <= maximumCoordinateMagnitude
        && std::abs(static_cast<long long>(cell.y))
            <= maximumCoordinateMagnitude
        && std::abs(static_cast<long long>(cell.level))
            <= maximumCoordinateMagnitude;
}

int effectiveGainPermille(
    const RuntimeWorldAudioSoundSpec& spec,
    const RuntimeWorldAudioContext& context,
    const RuntimeWorldAudioCell& source,
    int volumeDeltaPermille = 0
) {
    // LastGain is serialized. Listener-dependent gain here would put a
    // local camera into authoritative multiplayer and save state.
    (void)context; (void)source;
    return std::clamp(spec.volumePercent * 10 + volumeDeltaPermille, 0, 1000);
}

RuntimeWorldAudioAction makeAction(
    RuntimeWorldAudioActionKind kind,
    const RuntimeWorldAudioHandleState& handle,
    const RuntimeWorldAudioSoundSpec& spec,
    int currentTick,
    int gainPermille,
    const std::string& reason
) {
    RuntimeWorldAudioAction action;
    action.kind = kind;
    action.handleId = handle.handleId;
    action.eventId = handle.eventId;
    action.waypointId = handle.waypointId;
    action.cell = handle.cell;
    action.eventTick = currentTick;
    action.playbackOrdinal = handle.playbackOrdinal;
    action.gainPermille = gainPermille;
    action.authoredGainPermille = std::clamp(spec.volumePercent * 10 + runtimeAudioVariationFor(spec.variation,
        runtimeAudioVariationSeed(spec.eventId, handle.handleId, handle.playbackOrdinal)).volumeDeltaPermille, 0, 1000);
    action.frequencyPermille = runtimeAudioVariationFor(spec.variation,
        runtimeAudioVariationSeed(spec.eventId, handle.handleId, handle.playbackOrdinal)).frequencyPermille;
    action.local = spec.local;
    action.rangeCells = spec.rangeCells;
    action.reason = reason;
    return action;
}

std::string derivedPlayCommandKey(
    const RuntimeWorldAudioPlayRequest& request
) {
    if (!request.requestToken.empty()) {
        return "play-token:" + request.requestToken;
    }
    std::ostringstream key;
    key << "play:" << lowerAscii(request.eventId)
        << ':' << request.soundListIndex
        << ':' << request.waypointId
        << ':' << request.cell.x
        << ':' << request.cell.y
        << ':' << request.cell.level;
    return key.str();
}

std::string derivedStopCommandKey(
    const RuntimeWorldAudioStopRequest& request
) {
    if (!request.requestToken.empty()) {
        return "stop-token:" + request.requestToken;
    }
    std::ostringstream key;
    key << "stop:" << request.handleId
        << ':' << request.waypointId
        << ':' << (request.matchCell ? 1 : 0)
        << ':' << request.cell.x
        << ':' << request.cell.y
        << ':' << request.cell.level;
    return key.str();
}

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char character : value) {
        encoded.push_back(digits[character >> 4]);
        encoded.push_back(digits[character & 0x0f]);
    }
    return encoded;
}

int hexNibble(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

bool hexDecode(const std::string& value, std::string& output) {
    if ((value.size() % 2U) != 0U || value.size() > 32768U) {
        return false;
    }
    output.clear();
    output.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = hexNibble(value[index]);
        const int low = hexNibble(value[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        output.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

bool parseBool01(const std::string& text, bool& output) {
    if (text == "0") {
        output = false;
        return true;
    }
    if (text == "1") {
        output = true;
        return true;
    }
    return false;
}

} // namespace

bool RuntimeWorldAudioCatalog::loadFromIni(
    const IniFile& ini,
    std::string* errorMessage,
    std::vector<std::string>* warnings
) {
    auto fail = [errorMessage](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (warnings != nullptr) {
        warnings->clear();
    }
    if (ini.getSection("Defaults") == nullptr) {
        return fail("soundmd [Defaults] section was not found");
    }
    const IniFile::Section* soundList = ini.getSection("SoundList");
    if (soundList == nullptr) {
        return fail("soundmd [SoundList] section was not found");
    }

    RuntimeWorldAudioSoundSpec defaults;
    defaults.spatial = runtimeSpatialAudioRules(ini, "Defaults");
    defaults.volumePercent = readClampedInt(
        ini, "Defaults", "Volume", 80, 0, 100, warnings
    );
    defaults.rangeCells = readClampedDouble(
        ini, "Defaults", "Range", 10.0, 0.0, 4096.0, warnings
    );
    defaults.simultaneousLimit = readClampedInt(
        ini, "Defaults", "Limit", 5, 0,
        static_cast<int>(maximumSavedHandles), warnings
    );
    const auto defaultDelay = readDelay(
        ini, "Defaults", {0, 0}, warnings
    );
    defaults.delayMinMilliseconds = defaultDelay.first;
    defaults.delayMaxMilliseconds = defaultDelay.second;
    defaults.local = containsTokenIgnoreCase(
        splitTokens(ini.getValue("Defaults", "Type", "")),
        "Local"
    );

    std::vector<std::pair<int, std::string>> listed;
    listed.reserve(soundList->size());
    for (const auto& [key, rawEventId] : *soundList) {
        int index = -1;
        const std::string eventId = stripQuotes(rawEventId);
        if (!parseIntStrict(key, index) || index < 0 || eventId.empty()) {
            continue;
        }
        listed.emplace_back(index, eventId);
    }
    std::sort(
        listed.begin(),
        listed.end(),
        [](const auto& left, const auto& right) {
            return left.first != right.first
                ? left.first < right.first
                : lowerAscii(left.second) < lowerAscii(right.second);
        }
    );
    if (listed.size() > maximumCatalogSounds) {
        return fail("soundmd [SoundList] exceeds the runtime catalog limit");
    }

    RuntimeWorldAudioCatalog loaded;
    std::set<int> usedIndexes;
    std::set<std::string> usedEvents;
    for (const auto& [soundListIndex, listedEventId] : listed) {
        const std::string eventKey = lowerAscii(listedEventId);
        if (!usedIndexes.insert(soundListIndex).second
            || !usedEvents.insert(eventKey).second) {
            if (warnings != nullptr) {
                warnings->push_back(
                    "duplicate [SoundList] entry was ignored: "
                    + listedEventId
                );
            }
            continue;
        }
        if (ini.getSection(listedEventId) == nullptr) {
            if (warnings != nullptr) {
                warnings->push_back(
                    "sound section was not found: [" + listedEventId + "]"
                );
            }
            continue;
        }

        RuntimeWorldAudioSoundSpec spec = defaults;
        spec.soundListIndex = soundListIndex;
        spec.eventId = listedEventId;
        spec.variation = runtimeAudioVariationRules(ini, listedEventId);
        spec.spatial = runtimeSpatialAudioRules(ini, listedEventId);
        spec.volumePercent = readClampedInt(
            ini, listedEventId, "Volume", defaults.volumePercent,
            0, 100, warnings
        );
        spec.rangeCells = readClampedDouble(
            ini, listedEventId, "Range", defaults.rangeCells,
            0.0, 4096.0, warnings
        );
        spec.simultaneousLimit = readClampedInt(
            ini, listedEventId, "Limit", defaults.simultaneousLimit,
            0, static_cast<int>(maximumSavedHandles), warnings
        );
        const auto delay = readDelay(
            ini,
            listedEventId,
            {defaults.delayMinMilliseconds, defaults.delayMaxMilliseconds},
            warnings
        );
        spec.delayMinMilliseconds = delay.first;
        spec.delayMaxMilliseconds = delay.second;
        spec.loopIterations = readClampedInt(
            ini, listedEventId, "Loop", 0, 0,
            static_cast<int>(maximumSavedHandles), warnings
        );

        const std::vector<std::string> controls = splitTokens(
            ini.getValue(listedEventId, "Control", "")
        );
        spec.ambient = containsTokenIgnoreCase(controls, "Ambient");
        spec.loop = spec.ambient
            || containsTokenIgnoreCase(controls, "Loop");
        spec.randomSample = containsTokenIgnoreCase(controls, "Random");
        spec.predelay = containsTokenIgnoreCase(controls, "Predelay");
        const std::string typeText = ini.getValue(listedEventId, "Type", "");
        spec.local = typeText.empty()
            ? defaults.local
            : containsTokenIgnoreCase(splitTokens(typeText), "Local");

        std::string samplesText = ini.getValue(listedEventId, "Sounds", "");
        if (samplesText.empty()) {
            samplesText = ini.getValue(listedEventId, "Sound", "");
        }
        spec.samples = splitTokens(samplesText);
        if (spec.samples.size() > maximumSamplesPerSound) {
            if (warnings != nullptr) {
                warnings->push_back(
                    "[" + listedEventId
                    + "] sample list was truncated to the runtime limit"
                );
            }
            spec.samples.resize(maximumSamplesPerSound);
        }
        loaded.soundsByEventId_.push_back(loaded.sounds_.size());
        loaded.sounds_.push_back(std::move(spec));
    }
    std::sort(
        loaded.soundsByEventId_.begin(), loaded.soundsByEventId_.end(),
        [&loaded](std::size_t left, std::size_t right) {
            return lessAsciiIgnoreCase(
                loaded.sounds_[left].eventId, loaded.sounds_[right].eventId
            );
        }
    );
    *this = std::move(loaded);
    return true;
}

const RuntimeWorldAudioSoundSpec* RuntimeWorldAudioCatalog::find(
    const std::string& eventId
) const {
    const auto found = std::lower_bound(
        soundsByEventId_.begin(), soundsByEventId_.end(), eventId,
        [this](std::size_t index, const std::string& wanted) {
            return lessAsciiIgnoreCase(sounds_[index].eventId, wanted);
        }
    );
    return found != soundsByEventId_.end()
            && equalsAsciiIgnoreCase(sounds_[*found].eventId, eventId)
        ? &sounds_[*found]
        : nullptr;
}

const RuntimeWorldAudioSoundSpec*
RuntimeWorldAudioCatalog::findBySoundListIndex(int index) const {
    const auto found = std::lower_bound(
        sounds_.begin(), sounds_.end(), index,
        [](const RuntimeWorldAudioSoundSpec& spec, int wanted) {
            return spec.soundListIndex < wanted;
        }
    );
    return found != sounds_.end() && found->soundListIndex == index
        ? &*found
        : nullptr;
}

const std::vector<RuntimeWorldAudioSoundSpec>&
RuntimeWorldAudioCatalog::sounds() const {
    return sounds_;
}

std::uint64_t RuntimeWorldAudioCatalog::stateHash(bool includeVariation) const {
    std::uint64_t hash = fnvOffset;
    for (const RuntimeWorldAudioSoundSpec& spec : sounds_) {
        hashMix(hash, static_cast<std::uint64_t>(spec.soundListIndex));
        hashMixText(hash, lowerAscii(spec.eventId));
        hashMix(hash, static_cast<std::uint64_t>(spec.volumePercent));
        if (includeVariation) {
            hashMix(hash, static_cast<std::uint64_t>(spec.variation.minimumFrequencyPercent));
            hashMix(hash, static_cast<std::uint64_t>(spec.variation.maximumFrequencyPercent));
            hashMix(hash, static_cast<std::uint64_t>(spec.variation.maximumVolumeDropPercent));
        }
        std::ostringstream range;
        range << std::setprecision(17) << spec.rangeCells;
        hashMixText(hash, range.str());
        hashMix(hash, static_cast<std::uint64_t>(spec.delayMinMilliseconds));
        hashMix(hash, static_cast<std::uint64_t>(spec.delayMaxMilliseconds));
        hashMix(hash, static_cast<std::uint64_t>(spec.simultaneousLimit));
        hashMix(hash, static_cast<std::uint64_t>(spec.loopIterations));
        hashMix(hash, spec.loop ? 1ULL : 0ULL);
        hashMix(hash, spec.ambient ? 1ULL : 0ULL);
        hashMix(hash, spec.randomSample ? 1ULL : 0ULL);
        hashMix(hash, spec.predelay ? 1ULL : 0ULL);
        hashMix(hash, spec.local ? 1ULL : 0ULL);
        for (const std::string& sample : spec.samples) {
            hashMixText(hash, sample);
        }
    }
    return hash;
}

const char* runtimeWorldAudioActionName(RuntimeWorldAudioActionKind kind) {
    switch (kind) {
    case RuntimeWorldAudioActionKind::Play: return "play";
    case RuntimeWorldAudioActionKind::Replay: return "replay";
    case RuntimeWorldAudioActionKind::SetGain: return "set-gain";
    case RuntimeWorldAudioActionKind::Stop: return "stop";
    }
    return "unknown-world-audio-action";
}

RuntimeWorldAudioSystemState::RuntimeWorldAudioSystemState() {
    reset();
}

RuntimeWorldAudioSystemState::RuntimeWorldAudioSystemState(
    RuntimeWorldAudioConfig config
) {
    reset(config);
}

void RuntimeWorldAudioSystemState::reset(RuntimeWorldAudioConfig config) {
    config.fixedTickHz = std::clamp(config.fixedTickHz, 1, 1000);
    config.maximumHandles = std::clamp<std::size_t>(
        config.maximumHandles,
        1,
        maximumSavedHandles
    );
    config_ = config;
    lastAdvancedTick_ = -1;
    lastCommandTick_ = -1;
    nextHandleId_ = 1;
    commandKeysThisTick_.clear();
    handles_.clear();
}

const RuntimeWorldAudioConfig& RuntimeWorldAudioSystemState::config() const {
    return config_;
}

bool RuntimeWorldAudioSystemState::registerCommand(
    const std::string& key,
    int currentTick
) {
    if (currentTick < 0 || currentTick < lastCommandTick_) {
        return false;
    }
    if (currentTick != lastCommandTick_) {
        lastCommandTick_ = currentTick;
        commandKeysThisTick_.clear();
    }
    const auto insertion = std::lower_bound(
        commandKeysThisTick_.begin(), commandKeysThisTick_.end(), key
    );
    if (insertion != commandKeysThisTick_.end() && *insertion == key) {
        return false;
    }
    if (commandKeysThisTick_.size() >= maximumSavedCommands) {
        return false;
    }
    commandKeysThisTick_.insert(insertion, key);
    return true;
}

RuntimeWorldAudioCommandResult RuntimeWorldAudioSystemState::playAt(
    const RuntimeWorldAudioContext& context,
    const RuntimeWorldAudioPlayRequest& request,
    int currentTick
) {
    RuntimeWorldAudioCommandResult result;
    if (context.catalog == nullptr) {
        result.reason = "world audio catalog is unavailable";
        return result;
    }
    if (currentTick < 0 || currentTick < lastCommandTick_) {
        result.reason = "world audio play command has a stale tick";
        return result;
    }
    if (!validCell(request.cell)) {
        result.reason = "world audio play command has an invalid cell";
        return result;
    }
    const std::string commandKey = derivedPlayCommandKey(request);
    if (!registerCommand(commandKey, currentTick)) {
        result.duplicate = std::find(
            commandKeysThisTick_.begin(),
            commandKeysThisTick_.end(),
            commandKey
        ) != commandKeysThisTick_.end();
        result.reason = result.duplicate
            ? "duplicate world audio play command"
            : "world audio command limit was reached";
        return result;
    }

    const RuntimeWorldAudioSoundSpec* spec = request.eventId.empty()
        ? context.catalog->findBySoundListIndex(request.soundListIndex)
        : context.catalog->find(request.eventId);
    if (spec == nullptr) {
        result.reason = "world audio event is not present in soundmd";
        return result;
    }
    if (handles_.size() >= config_.maximumHandles) {
        result.reason = "world audio handle limit was reached";
        return result;
    }
    if (spec->simultaneousLimit > 0) {
        const std::size_t activeForEvent = static_cast<std::size_t>(
            std::count_if(
                handles_.begin(),
                handles_.end(),
                [spec](const RuntimeWorldAudioHandleState& handle) {
                    return equalsAsciiIgnoreCase(handle.eventId, spec->eventId);
                }
            )
        );
        if (activeForEvent
            >= static_cast<std::size_t>(spec->simultaneousLimit)) {
            result.reason = "soundmd simultaneous Limit was reached";
            return result;
        }
    }
    if (nextHandleId_ == 0
        || nextHandleId_ == std::numeric_limits<std::uint64_t>::max()) {
        result.reason = "world audio handle id space was exhausted";
        return result;
    }

    RuntimeWorldAudioHandleState handle;
    handle.handleId = nextHandleId_++;
    handle.eventId = spec->eventId;
    handle.requestToken = request.requestToken;
    handle.waypointId = request.waypointId;
    handle.cell = request.cell;
    handle.createdTick = currentTick;
    const int initialDelay = spec->predelay
        ? deterministicDelayTicks(
            *spec,
            handle.handleId,
            0,
            config_.fixedTickHz
        )
        : 0;
    handle.nextPlaybackTick = saturatedAdd(currentTick, initialDelay);
    handles_.push_back(handle);

    result.accepted = true;
    result.handleId = handle.handleId;
    result.reason = "world audio handle created";
    return result;
}

RuntimeWorldAudioCommandResult RuntimeWorldAudioSystemState::stopAt(
    const RuntimeWorldAudioContext& context,
    const RuntimeWorldAudioStopRequest& request,
    int currentTick
) {
    RuntimeWorldAudioCommandResult result;
    if (context.catalog == nullptr) {
        result.reason = "world audio catalog is unavailable";
        return result;
    }
    if (currentTick < 0 || currentTick < lastCommandTick_) {
        result.reason = "world audio stop command has a stale tick";
        return result;
    }
    if (request.handleId == 0
        && request.waypointId < 0
        && !request.matchCell) {
        result.reason = "world audio stop command has no target";
        return result;
    }
    if (request.matchCell && !validCell(request.cell)) {
        result.reason = "world audio stop command has an invalid cell";
        return result;
    }
    const std::string commandKey = derivedStopCommandKey(request);
    if (!registerCommand(commandKey, currentTick)) {
        result.duplicate = std::find(
            commandKeysThisTick_.begin(),
            commandKeysThisTick_.end(),
            commandKey
        ) != commandKeysThisTick_.end();
        result.reason = result.duplicate
            ? "duplicate world audio stop command"
            : "world audio command limit was reached";
        return result;
    }

    auto matches = [&request](const RuntimeWorldAudioHandleState& handle) {
        if (request.handleId != 0) {
            return handle.handleId == request.handleId;
        }
        // Action 116 supplies a waypoint.  Prefer that authored identity so
        // stacked waypoints do not accidentally stop neighboring ambience.
        if (request.waypointId >= 0) {
            return handle.waypointId == request.waypointId;
        }
        return request.matchCell && cellsEqual(handle.cell, request.cell);
    };

    std::size_t retainedCount = 0;
    for (RuntimeWorldAudioHandleState& handle : handles_) {
        if (!matches(handle)) {
            if (&handle != &handles_[retainedCount]) {
                handles_[retainedCount] = std::move(handle);
            }
            ++retainedCount;
            continue;
        }
        const RuntimeWorldAudioSoundSpec* spec =
            context.catalog->find(handle.eventId);
        if (spec != nullptr) {
            RuntimeWorldAudioAction action = makeAction(
                RuntimeWorldAudioActionKind::Stop,
                handle,
                *spec,
                currentTick,
                std::max(0, handle.lastGainPermille),
                "targeted scenario world sound stop"
            );
            result.actions.push_back(std::move(action));
        }
        if (result.handleId == 0) {
            result.handleId = handle.handleId;
        }
    }
    handles_.resize(retainedCount);
    result.accepted = !result.actions.empty();
    result.reason = result.accepted
        ? "targeted world audio handles stopped"
        : "no matching scenario world audio handle";
    return result;
}

std::vector<RuntimeWorldAudioAction> RuntimeWorldAudioSystemState::tick(
    const RuntimeWorldAudioContext& context,
    int currentTick
) {
    std::vector<RuntimeWorldAudioAction> actions;
    if (context.catalog == nullptr
        || currentTick < 0
        || currentTick <= lastAdvancedTick_) {
        return actions;
    }
    (void)reconcile(context);
    lastAdvancedTick_ = currentTick;

    std::size_t retainedCount = 0;
    const auto retain = [this, &retainedCount](
        RuntimeWorldAudioHandleState& handle
    ) {
        if (&handle != &handles_[retainedCount]) {
            handles_[retainedCount] = std::move(handle);
        }
        ++retainedCount;
    };
    for (RuntimeWorldAudioHandleState& handle : handles_) {
        const RuntimeWorldAudioSoundSpec* spec =
            context.catalog->find(handle.eventId);
        if (spec == nullptr) {
            continue;
        }
        const bool due = handle.nextPlaybackTick >= 0
            && currentTick >= handle.nextPlaybackTick;
        const int activeOrdinal = due ? handle.playbackOrdinal : std::max(0, handle.playbackOrdinal - 1);
        const auto variation = runtimeAudioVariationFor(spec->variation,
            runtimeAudioVariationSeed(spec->eventId, handle.handleId, activeOrdinal));
        const int gain = effectiveGainPermille(*spec, context, handle.cell, variation.volumeDeltaPermille);
        if (due) {
            const RuntimeWorldAudioActionKind kind = handle.playbackOrdinal == 0
                ? RuntimeWorldAudioActionKind::Play
                : RuntimeWorldAudioActionKind::Replay;
            RuntimeWorldAudioAction action = makeAction(
                kind,
                handle,
                *spec,
                currentTick,
                gain,
                kind == RuntimeWorldAudioActionKind::Play
                    ? "fixed-tick world sound start"
                    : "fixed-tick world sound replay"
            );
            action.sampleName = selectedSample(
                *spec,
                handle.handleId,
                handle.playbackOrdinal
            );

            handle.lastPlaybackTick = currentTick;
            ++handle.playbackOrdinal;
            handle.lastGainPermille = gain;
            const bool finiteComplete = spec->loopIterations > 0
                && handle.playbackOrdinal >= spec->loopIterations;
            if (!spec->loop || finiteComplete) {
                handle.nextPlaybackTick = -1;
            } else if (spec->loopIterations > 0
                || spec->delayMaxMilliseconds > 0) {
                const int delay = std::max(
                    1,
                    deterministicDelayTicks(
                        *spec,
                        handle.handleId,
                        handle.playbackOrdinal,
                        config_.fixedTickHz
                    )
                );
                handle.nextPlaybackTick = saturatedAdd(currentTick, delay);
            } else {
                handle.nextPlaybackTick = -1;
                handle.continuousLoopIssued = true;
                action.backendLoopIterations = -1;
            }
            actions.push_back(std::move(action));
            if (spec->loop && !finiteComplete) {
                retain(handle);
            }
            continue;
        }

        if (handle.lastPlaybackTick >= 0
            && gain != handle.lastGainPermille) {
            RuntimeWorldAudioAction action = makeAction(
                RuntimeWorldAudioActionKind::SetGain,
                handle,
                *spec,
                currentTick,
                gain,
                "listener-distance attenuation changed"
            );
            handle.lastGainPermille = gain;
            actions.push_back(std::move(action));
        }
        retain(handle);
    }
    handles_.resize(retainedCount);
    return actions;
}

std::vector<RuntimeWorldAudioAction>
RuntimeWorldAudioSystemState::rehydrateActions(
    const RuntimeWorldAudioContext& context,
    int currentTick
) const {
    std::vector<RuntimeWorldAudioAction> actions;
    if (context.catalog == nullptr || currentTick < 0) {
        return actions;
    }
    for (const RuntimeWorldAudioHandleState& handle : handles_) {
        const RuntimeWorldAudioSoundSpec* spec =
            context.catalog->find(handle.eventId);
        if (spec == nullptr
            || !spec->loop
            || spec->loopIterations != 0
            || spec->delayMaxMilliseconds > 0
            || !handle.continuousLoopIssued
            || handle.lastPlaybackTick < 0
            || handle.playbackOrdinal <= 0) {
            continue;
        }
        const int activeOrdinal = handle.playbackOrdinal - 1;
        const auto variation = runtimeAudioVariationFor(spec->variation,
            runtimeAudioVariationSeed(spec->eventId, handle.handleId, activeOrdinal));
        RuntimeWorldAudioAction action = makeAction(
            RuntimeWorldAudioActionKind::Play,
            handle,
            *spec,
            currentTick,
            effectiveGainPermille(*spec, context, handle.cell, variation.volumeDeltaPermille),
            "rehydrated continuous world sound loop"
        );
        action.playbackOrdinal = activeOrdinal;
        action.frequencyPermille = variation.frequencyPermille;
        action.sampleName = selectedSample(
            *spec,
            handle.handleId,
            activeOrdinal
        );
        action.backendLoopIterations = -1;
        actions.push_back(std::move(action));
    }
    return actions;
}

RuntimeWorldAudioReconcileReport RuntimeWorldAudioSystemState::reconcile(
    const RuntimeWorldAudioContext& context
) {
    RuntimeWorldAudioReconcileReport report;
    if (context.catalog == nullptr) {
        return report;
    }
    report.contextValid = true;
    // Allocation and load establish increasing handle IDs. Stable compaction
    // preserves that order without rebuilding and sorting two vectors each
    // fixed tick. Pointer keys are lookup-only; traversal remains ID-ordered.
    std::unordered_map<const RuntimeWorldAudioSoundSpec*, std::size_t>
        countByEvent;
    std::uint64_t lastSeenId = 0;
    bool hasSeenId = false;
    std::size_t retainedCount = 0;
    for (RuntimeWorldAudioHandleState& handle : handles_) {
        const RuntimeWorldAudioSoundSpec* spec =
            context.catalog->find(handle.eventId);
        if (spec == nullptr || (hasSeenId && handle.handleId == lastSeenId)) {
            ++report.removedMissingSounds;
            report.removedHandleIds.push_back(handle.handleId);
            continue;
        }
        lastSeenId = handle.handleId;
        hasSeenId = true;
        const bool completedOneShot = !spec->loop
            && handle.playbackOrdinal > 0
            && handle.nextPlaybackTick < 0;
        const bool completedFinite = spec->loopIterations > 0
            && handle.playbackOrdinal >= spec->loopIterations;
        if (completedOneShot || completedFinite) {
            ++report.removedCompletedHandles;
            report.removedHandleIds.push_back(handle.handleId);
            continue;
        }
        // Version-1 saves produced before finite loops were core-scheduled
        // delegated zero-delay Loop=N to the audio device.  A freshly loaded
        // backend has no such loop, so deterministically resume the remaining
        // Replay actions on the next fixed tick.
        if (spec->loopIterations > 0
            && handle.playbackOrdinal > 0
            && handle.playbackOrdinal < spec->loopIterations
            && handle.nextPlaybackTick < 0) {
            const int resumeAfter = std::max(
                handle.lastPlaybackTick,
                lastAdvancedTick_
            );
            handle.nextPlaybackTick = saturatedAdd(resumeAfter, 1);
            handle.continuousLoopIssued = false;
            ++report.rescheduledLegacyFiniteHandles;
        }
        std::size_t& eventCount = countByEvent[spec];
        if (spec->simultaneousLimit > 0
            && eventCount
                >= static_cast<std::size_t>(spec->simultaneousLimit)) {
            ++report.removedByEventLimit;
            report.removedHandleIds.push_back(handle.handleId);
            continue;
        }
        if (retainedCount >= config_.maximumHandles) {
            ++report.removedByGlobalLimit;
            report.removedHandleIds.push_back(handle.handleId);
            continue;
        }
        ++eventCount;
        if (&handle != &handles_[retainedCount]) {
            handles_[retainedCount] = std::move(handle);
        }
        ++retainedCount;
        ++report.retainedHandles;
    }
    handles_.resize(retainedCount);
    return report;
}

const RuntimeWorldAudioHandleState*
RuntimeWorldAudioSystemState::findHandle(std::uint64_t handleId) const {
    const auto found = std::lower_bound(
        handles_.begin(), handles_.end(), handleId,
        [](const RuntimeWorldAudioHandleState& handle, std::uint64_t wanted) {
            return handle.handleId < wanted;
        }
    );
    return found != handles_.end() && found->handleId == handleId
        ? &*found
        : nullptr;
}

const std::vector<RuntimeWorldAudioHandleState>&
RuntimeWorldAudioSystemState::handles() const {
    return handles_;
}

int RuntimeWorldAudioSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

void RuntimeWorldAudioSystemState::save(std::ostream& output) const {
    output << "[WorldAudioRuntime]\n"
        << "Version=1\n"
        << "FixedTickHz=" << config_.fixedTickHz << "\n"
        << "MaximumHandles=" << config_.maximumHandles << "\n"
        << "LastAdvancedTick=" << lastAdvancedTick_ << "\n"
        << "LastCommandTick=" << lastCommandTick_ << "\n"
        << "NextHandleId=" << nextHandleId_ << "\n"
        << "HandleCount=" << handles_.size() << "\n"
        << "CommandCount=" << commandKeysThisTick_.size() << "\n";

    for (std::size_t index = 0; index < handles_.size(); ++index) {
        const RuntimeWorldAudioHandleState& handle = handles_[index];
        output << "\n[WorldAudioRuntime.Handle." << index << "]\n"
            << "HandleId=" << handle.handleId << "\n"
            << "EventHex=" << hexEncode(handle.eventId) << "\n"
            << "RequestTokenHex=" << hexEncode(handle.requestToken) << "\n"
            << "WaypointId=" << handle.waypointId << "\n"
            << "CellX=" << handle.cell.x << "\n"
            << "CellY=" << handle.cell.y << "\n"
            << "CellLevel=" << handle.cell.level << "\n"
            << "CreatedTick=" << handle.createdTick << "\n"
            << "LastPlaybackTick=" << handle.lastPlaybackTick << "\n"
            << "NextPlaybackTick=" << handle.nextPlaybackTick << "\n"
            << "PlaybackOrdinal=" << handle.playbackOrdinal << "\n"
            << "LastGainPermille=" << handle.lastGainPermille << "\n"
            << "ContinuousLoopIssued="
            << (handle.continuousLoopIssued ? 1 : 0) << "\n";
    }
    for (std::size_t index = 0;
        index < commandKeysThisTick_.size();
        ++index) {
        output << "\n[WorldAudioRuntime.Command." << index << "]\n"
            << "KeyHex=" << hexEncode(commandKeysThisTick_[index]) << "\n";
    }
}

bool RuntimeWorldAudioSystemState::load(
    const IniFile& ini,
    const RuntimeWorldAudioContext& context,
    std::string* errorMessage,
    RuntimeWorldAudioReconcileReport* reconcileReport
) {
    auto fail = [errorMessage](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (context.catalog == nullptr) {
        return fail("world audio load requires a catalog");
    }
    if (ini.getSection("WorldAudioRuntime") == nullptr) {
        return fail("world audio runtime save section was not found");
    }
    auto value = [&ini](const std::string& section, const std::string& key) {
        return ini.getValue(section, key, "");
    };

    int version = 0;
    int fixedTickHz = 0;
    int maximumHandles = 0;
    int lastAdvancedTick = -1;
    int lastCommandTick = -1;
    int handleCount = 0;
    int commandCount = 0;
    std::uint64_t nextHandleId = 0;
    if (!parseIntStrict(value("WorldAudioRuntime", "Version"), version)
        || version != 1
        || !parseIntStrict(
            value("WorldAudioRuntime", "FixedTickHz"),
            fixedTickHz
        )
        || fixedTickHz < 1 || fixedTickHz > 1000
        || !parseIntStrict(
            value("WorldAudioRuntime", "MaximumHandles"),
            maximumHandles
        )
        || maximumHandles < 1
        || maximumHandles > static_cast<int>(maximumSavedHandles)
        || !parseIntStrict(
            value("WorldAudioRuntime", "LastAdvancedTick"),
            lastAdvancedTick
        )
        || lastAdvancedTick < -1
        || !parseIntStrict(
            value("WorldAudioRuntime", "LastCommandTick"),
            lastCommandTick
        )
        || lastCommandTick < -1
        || !parseUint64Strict(
            value("WorldAudioRuntime", "NextHandleId"),
            nextHandleId
        )
        || nextHandleId == 0
        || !parseIntStrict(
            value("WorldAudioRuntime", "HandleCount"),
            handleCount
        )
        || handleCount < 0 || handleCount > maximumHandles
        || !parseIntStrict(
            value("WorldAudioRuntime", "CommandCount"),
            commandCount
        )
        || commandCount < 0
        || commandCount > static_cast<int>(maximumSavedCommands)
        || (lastCommandTick < 0 && commandCount != 0)) {
        return fail("world audio runtime save header is invalid");
    }

    RuntimeWorldAudioSystemState loaded({
        fixedTickHz,
        static_cast<std::size_t>(maximumHandles)
    });
    loaded.lastAdvancedTick_ = lastAdvancedTick;
    loaded.lastCommandTick_ = lastCommandTick;
    loaded.nextHandleId_ = nextHandleId;
    std::set<std::uint64_t> ids;
    std::uint64_t maximumHandleId = 0;
    const int latestKnownTick = std::max(lastAdvancedTick, lastCommandTick);
    for (int index = 0; index < handleCount; ++index) {
        const std::string section = "WorldAudioRuntime.Handle."
            + std::to_string(index);
        RuntimeWorldAudioHandleState handle;
        std::string continuousText;
        if (ini.getSection(section) == nullptr
            || !parseUint64Strict(value(section, "HandleId"), handle.handleId)
            || handle.handleId == 0
            || !ids.insert(handle.handleId).second
            || !hexDecode(value(section, "EventHex"), handle.eventId)
            || handle.eventId.empty()
            || !hexDecode(
                value(section, "RequestTokenHex"),
                handle.requestToken
            )
            || !parseIntStrict(value(section, "WaypointId"), handle.waypointId)
            || !parseIntStrict(value(section, "CellX"), handle.cell.x)
            || !parseIntStrict(value(section, "CellY"), handle.cell.y)
            || !parseIntStrict(value(section, "CellLevel"), handle.cell.level)
            || !validCell(handle.cell)
            || !parseIntStrict(
                value(section, "CreatedTick"), handle.createdTick
            )
            || handle.createdTick < 0
            || handle.createdTick > latestKnownTick
            || !parseIntStrict(
                value(section, "LastPlaybackTick"),
                handle.lastPlaybackTick
            )
            || handle.lastPlaybackTick < -1
            || (handle.lastPlaybackTick >= 0
                && (handle.lastPlaybackTick < handle.createdTick
                    || handle.lastPlaybackTick > lastAdvancedTick))
            || !parseIntStrict(
                value(section, "NextPlaybackTick"),
                handle.nextPlaybackTick
            )
            || handle.nextPlaybackTick < -1
            || (handle.nextPlaybackTick >= 0
                && handle.nextPlaybackTick < handle.createdTick)
            || !parseIntStrict(
                value(section, "PlaybackOrdinal"),
                handle.playbackOrdinal
            )
            || handle.playbackOrdinal < 0
            || handle.playbackOrdinal
                == std::numeric_limits<int>::max()
            || static_cast<long long>(handle.playbackOrdinal)
                > std::max<long long>(
                    0,
                    static_cast<long long>(lastAdvancedTick)
                        - static_cast<long long>(handle.createdTick)
                        + 1
                )
            || !parseIntStrict(
                value(section, "LastGainPermille"),
                handle.lastGainPermille
            )
            || handle.lastGainPermille < -1
            || handle.lastGainPermille > 1000
            || !parseBool01(
                value(section, "ContinuousLoopIssued"),
                handle.continuousLoopIssued
            )
            || (handle.lastPlaybackTick < 0
                && (handle.playbackOrdinal != 0
                    || handle.lastGainPermille != -1
                    || handle.continuousLoopIssued))
            || (handle.lastPlaybackTick >= 0
                && (handle.playbackOrdinal == 0
                    || handle.lastGainPermille < 0))) {
            return fail(
                "world audio runtime handle is invalid: [" + section + "]"
            );
        }
        maximumHandleId = std::max(maximumHandleId, handle.handleId);
        loaded.handles_.push_back(std::move(handle));
    }
    if (nextHandleId <= maximumHandleId) {
        return fail("world audio runtime next handle id is not monotonic");
    }

    std::set<std::string> commandKeys;
    for (int index = 0; index < commandCount; ++index) {
        const std::string section = "WorldAudioRuntime.Command."
            + std::to_string(index);
        std::string key;
        if (ini.getSection(section) == nullptr
            || !hexDecode(value(section, "KeyHex"), key)
            || key.empty()
            || !commandKeys.insert(key).second) {
            return fail(
                "world audio runtime command is invalid: [" + section + "]"
            );
        }
        loaded.commandKeysThisTick_.push_back(std::move(key));
    }
    loaded.sortState();
    RuntimeWorldAudioReconcileReport report = loaded.reconcile(context);
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = std::move(report);
    }
    return true;
}

std::uint64_t RuntimeWorldAudioSystemState::stateHash() const {
    std::ostringstream output;
    save(output);
    return hashText(output.str());
}

void RuntimeWorldAudioSystemState::sortState() {
    std::sort(
        handles_.begin(),
        handles_.end(),
        [](const RuntimeWorldAudioHandleState& left,
           const RuntimeWorldAudioHandleState& right) {
            return left.handleId < right.handleId;
        }
    );
    std::sort(commandKeysThisTick_.begin(), commandKeysThisTick_.end());
}
