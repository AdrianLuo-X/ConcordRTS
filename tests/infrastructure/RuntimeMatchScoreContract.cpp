#include "core/results/RuntimeMatchScore.h"
#include "core/io/IniFile.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::uint64_t seed = 1469598103934665603ull;

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Runtime match score contract failure: " << message << '\n';
    }
    return condition;
}

std::string saved(const RuntimeMatchScoreState& state) {
    std::ostringstream output;
    writeRuntimeMatchScore(state, output);
    return output.str();
}

std::uint64_t digest(const RuntimeMatchScoreState& state) {
    std::uint64_t hash = seed;
    hashRuntimeMatchScore(state, hash);
    return hash;
}

bool loadText(RuntimeMatchScoreState& state, const std::string& text, std::string& error) {
    IniFile ini;
    if (!ini.loadFromString(text)) return false;
    return loadRuntimeMatchScore(state, ini, error);
}

const std::string schema1 =
    "\n[RuntimeMatchScore]\nSchema=1\nCount=2\n"
    "0=616c706861 1 2 3 4 500\n"
    "1=7a657461 6 7 8 9 1000\n";
const std::string schema2 =
    "\n[RuntimeMatchScore]\nSchema=2\nCount=2\n"
    "0=616c706861 1 2 3 4 500 11\n"
    "1=7a657461 6 7 8 9 1000 12\n";

RuntimeMatchScoreState fixture(bool builds) {
    RuntimeMatchScoreState state;
    state.emplace("zeta", RuntimeMatchScoreEntry{6, 7, 8, 9, 1000, builds ? 12 : 0});
    state.emplace("alpha", RuntimeMatchScoreEntry{1, 2, 3, 4, 500, builds ? 11 : 0});
    return state;
}

// Preserve the original state smoke's pure checks, fixture, seed and assertions.
// Real object exits and completed-build exclusions remain in the host smoke.
bool testMigratedStateSmokeCodec() {
    const auto fail = [&](const std::string& reason) {
        return expect(false, "migrated state smoke: " + reason);
    };
    const RuntimeMatchScoreState probe{
        {"skirmishplayer1", {1, 0, 1, 0, 100, 4}},
        {"skirmishplayer2", {0, 0, 2, 1, 0, 7}},
        {"skirmishplayer3", {0, 1, 0, 0, 500, 0}}
    };
    std::string error;
    std::ostringstream saved;
    writeRuntimeMatchScore(probe, saved);
    IniFile ini;
    ini.loadFromString(saved.str());
    RuntimeMatchScoreState restored;
    if (!loadRuntimeMatchScore(restored, ini, error) || restored != probe)
        return fail("cumulative score did not survive save/load");
    std::uint64_t before = 1, after = 1;
    hashRuntimeMatchScore(probe, before);
    hashRuntimeMatchScore(restored, after);
    if (before != after) return fail("restored score hash changed");
    restored.begin()->second.score++;
    after = 1;
    hashRuntimeMatchScore(restored, after);
    if (after == before) return fail("score was not part of authority");
    restored = probe;
    restored.begin()->second.built++;
    after = 1;
    hashRuntimeMatchScore(restored, after);
    if (after == before) return fail("build count was not part of authority");
    const auto valid = restored;
    IniFile invalid;
    invalid.loadFromString("[RuntimeMatchScore]\nSchema=1\nCount=1\n0=41 -1 0 0 0 0\n");
    if (loadRuntimeMatchScore(restored, invalid, error) || restored != valid)
        return fail("invalid score did not load atomically");
    invalid.loadFromString("[RuntimeMatchScore]\nSchema=2\nCount=1\n0=41 1 2 3 4 5 -1\n");
    if (loadRuntimeMatchScore(restored, invalid, error) || restored != valid)
        return fail("invalid build count did not load atomically");
    IniFile oldScore;
    oldScore.loadFromString("[RuntimeMatchScore]\nSchema=1\nCount=1\n0=41 1 2 3 4 5\n");
    if (!loadRuntimeMatchScore(restored, oldScore, error) || restored.at("a").built != 0
        || restored.at("a").score != 5) return fail("old score schema no longer loads");
    IniFile legacy;
    if (!loadRuntimeMatchScore(restored, legacy, error) || !restored.empty())
        return fail("old saves inherited another match's score");
    std::cout << "Migrated match score codec: saveLoad=ok hash=ok legacy=ok\n";
    return true;
}
bool testEmptyAndLegacyAbsence() {
    bool success = true;
    RuntimeMatchScoreState state;
    success &= expect(saved(state) == "\n[RuntimeMatchScore]\nSchema=1\nCount=0\n",
        "empty save bytes changed");
    for (const std::uint64_t initial : {std::uint64_t{0}, std::uint64_t{1}, seed, std::numeric_limits<std::uint64_t>::max()}) {
        auto hash = initial;
        hashRuntimeMatchScore(state, hash);
        success &= expect(hash == initial, "empty score hash must leave every input seed unchanged");
    }
    state = fixture(true);
    std::string error = "previous diagnostic";
    success &= expect(loadText(state, "[Other]\nValue=1\n", error) && state.empty(),
        "missing legacy section must clear existing score state");
    success &= expect(error == "previous diagnostic", "successful absent-section load cleared the caller's error");
    for (const int version : {1, 2}) {
        state = fixture(true);
        error = "previous diagnostic";
        success &= expect(loadText(state, "[RuntimeMatchScore]\nSchema=" + std::to_string(version)
            + "\nCount=0\n0=this unused record is ignored\n", error) && state.empty(),
            "zero-count section must clear state and ignore records outside Count");
        success &= expect(error == "previous diagnostic", "successful zero-count load changed the caller's error");
    }
    return success;
}

bool testFixedSaveHashAndRoundTrip() {
    bool success = true;
    for (const bool builds : {false, true}) {
        const auto state = fixture(builds);
        RuntimeMatchScoreState ascending;
        ascending.emplace("alpha", state.at("alpha"));
        ascending.emplace("zeta", state.at("zeta"));
        const std::string& text = builds ? schema2 : schema1;
        success &= expect(saved(state) == text && saved(ascending) == text,
            "fixed save bytes or map owner order changed");
        const auto golden = builds ? 2245471447362629957ull : 418795754624111929ull;
        success &= expect(digest(state) == golden && digest(ascending) == golden,
            "legacy score hash golden or insertion-order independence changed");
        RuntimeMatchScoreState restored{{"stale", {42, 43, 44, 45, 46, 47}}};
        std::string error = "previous diagnostic";
        success &= expect(loadText(restored, text, error) && restored == state,
            "fixed Schema 1/2 text did not restore the six score fields");
        success &= expect(error == "previous diagnostic", "successful load changed the caller's error");
        success &= expect(saved(restored) == text && digest(restored) == golden,
            "save/load roundtrip changed bytes or authoritative hash");
        auto changed = restored;
        ++changed.at("alpha").score;
        success &= expect(digest(changed) != golden, "score facts disappeared from the hash");
        changed = restored;
        ++changed.at("alpha").built;
        success &= expect(digest(changed) != golden, "build facts disappeared from the hash");
    }
    RuntimeMatchScoreState zeroBuilds;
    std::string error;
    success &= expect(loadText(zeroBuilds,
        "[RuntimeMatchScore]\nSchema=2\nCount=2\n"
        "0=616c706861 1 2 3 4 500 0\n1=7a657461 6 7 8 9 1000 0\n", error)
        && zeroBuilds == fixture(false) && saved(zeroBuilds) == schema1
        && digest(zeroBuilds) == 418795754624111929ull,
        "all-zero built facts must retain Schema 1 output and legacy hash");
    auto oneBuild = fixture(false);
    oneBuild.at("zeta").built = 12;
    success &= expect(saved(oneBuild).find("0=616c706861 1 2 3 4 500 0\n") != std::string::npos,
        "Schema 2 must emit built=0 for owners without build facts");
    return success;
}

bool testEventAccumulationAndSaturation() {
    bool success = true;
    RuntimeMatchScoreState state;
    recordRuntimeMatchScoreBuild(state, {" \t\r\n\f\vALpHa\v\f\n\r\t ", 3});
    recordRuntimeMatchScoreBuild(state, {"ALPHA"});
    recordRuntimeMatchScoreBuild(state, {"alpha", 3});
    recordRuntimeMatchScoreBuild(state, {"unused", 0});
    recordRuntimeMatchScoreBuild(state, {"unused", -1});
    recordRuntimeMatchScoreBuild(state, {" \t\r\n\f\v ", 1});
    success &= expect(state.size() == 1 && state.at("alpha").built == 7,
        "build canonicalization, default count, repetition or rejected-count behavior changed");
    const RuntimeMatchScoreDestructionEvent unit{" \tZETA\r\n", " ALPHA ", false, 125};
    recordRuntimeMatchScoreDestruction(state, unit);
    recordRuntimeMatchScoreDestruction(state, unit);
    recordRuntimeMatchScoreDestruction(state, {"zeta", "alpha", true, -40});
    recordRuntimeMatchScoreDestruction(state, {"zeta", "", false, 999});
    recordRuntimeMatchScoreDestruction(state, {"zeta", "", true, 999});
    success &= expect(state.size() == 2 && state.at("alpha") == RuntimeMatchScoreEntry{2, 1, 0, 0, 250, 7}
        && state.at("zeta") == RuntimeMatchScoreEntry{0, 0, 3, 2, 0, 0},
        "destruction repetition, unit/structure loss, credit or negative-cost behavior changed");

    constexpr int maximum = std::numeric_limits<int>::max();
    state = {{"alpha", {maximum - 1, maximum - 1, maximum - 1, maximum - 1, maximum - 1, maximum - 1}},
             {"zeta", {maximum - 1, maximum - 1, maximum - 1, maximum - 1, maximum - 1, maximum - 1}}};
    for (int repetition = 0; repetition < 3; ++repetition) {
        recordRuntimeMatchScoreBuild(state, {"alpha", maximum});
        recordRuntimeMatchScoreBuild(state, {"zeta", maximum});
        recordRuntimeMatchScoreDestruction(state, {"alpha", "zeta", false, maximum});
        recordRuntimeMatchScoreDestruction(state, {"zeta", "alpha", false, maximum});
        recordRuntimeMatchScoreDestruction(state, {"alpha", "zeta", true, maximum});
        recordRuntimeMatchScoreDestruction(state, {"zeta", "alpha", true, maximum});
    }
    const RuntimeMatchScoreEntry saturated{maximum, maximum, maximum, maximum, maximum, maximum};
    success &= expect(state.at("alpha") == saturated && state.at("zeta") == saturated,
        "build/kill/loss/score arithmetic must saturate at INT_MAX without wrapping");
    RuntimeMatchScoreState restored;
    std::string error;
    success &= expect(loadText(restored, saved(state), error) && restored == state
        && digest(restored) == digest(state), "saturated values failed save/load/hash roundtrip");
    return success;
}

bool testOwnerAndHeaderCompatibility() {
    bool success = true;
    RuntimeMatchScoreState state;
    std::string error = "previous diagnostic";
    success &= expect(loadText(state,
        "[runtimematchscore]\nsCheMa= +0002 \ncoUNT= +0001 \n"
        "0=20090a0d0b0c416C5068410c0b0d0a0920 +1 02 3 4 5 0\n", error)
        && state == RuntimeMatchScoreState{{"alpha", {1, 2, 3, 4, 5, 0}}},
        "old INI case, strict stoi decimal/sign, whitespace or mixed-case hex semantics changed");
    success &= expect(error == "previous diagnostic", "successful normalized load changed the caller's error");
    const std::string maximumOwner(1024, 'A');
    success &= expect(loadText(state, "[RuntimeMatchScore]\nSchema=1\nCount=1\n0="
        + iniHexEncode(maximumOwner) + " 1 2 3 4 5\n", error)
        && state.size() == 1 && state.begin()->first == std::string(1024, 'a'),
        "exactly 1024 decoded owner bytes must remain accepted");
    const std::string byteOwner("A\0B\xff", 4);
    const std::string canonicalByteOwner("a\0b\xff", 4);
    success &= expect(loadText(state, "[RuntimeMatchScore]\nSchema=1\nCount=1\n0="
        + iniHexEncode(byteOwner) + " 1 2 3 4 5\n", error)
        && state.size() == 1 && state.begin()->first == canonicalByteOwner,
        "embedded zero/high owner bytes were rejected or changed");
    RuntimeMatchScoreState bytesRestored;
    success &= expect(loadText(bytesRestored, saved(state), error) && bytesRestored == state
        && digest(bytesRestored) == digest(state), "byte owner save/load/hash roundtrip changed");

    // IniFile keeps the last repeated key; Count bounds the records consumed by this section.
    success &= expect(loadText(state,
        "[RuntimeMatchScore]\nSchema=9\nSchema=1\nCount=1\n"
        "0=not-a-valid-record\n0=41 1 2 3 4 5\n1=ignored\n", error)
        && state == RuntimeMatchScoreState{{"a", {1, 2, 3, 4, 5, 0}}},
        "last INI key wins or records beyond Count behavior changed");
    std::ostringstream maximumCount;
    maximumCount << "[RuntimeMatchScore]\nSchema=1\nCount=4096\n";
    for (int index = 0; index < 4096; ++index) {
        maximumCount << index << '=' << iniHexEncode("owner" + std::to_string(index)) << " 0 0 0 0 0\n";
    }
    success &= expect(loadText(state, maximumCount.str(), error) && state.size() == 4096,
        "the inclusive 4096 owner count boundary changed");
    return success;
}

bool testRejectedLoadsAreAtomic() {
    bool success = true;
    const auto original = fixture(true);
    const auto reject = [&](const std::string& text, const std::string& expectedError, const std::string& label) {
        auto state = original;
        std::string error = "previous diagnostic";
        const bool accepted = loadText(state, text, error);
        success &= expect(!accepted && state == original && error == expectedError,
            label + " must reject atomically with the original error text");
    };
    const std::string prefix = "[RuntimeMatchScore]\n";
    for (const char* header : {
        "Count=0\n", "Schema=1\n", "Schema=0\nCount=0\n", "Schema=3\nCount=0\n",
        "Schema=-1\nCount=0\n", "Schema=1junk\nCount=0\n", "Schema=1.0\nCount=0\n",
        "Schema=0x1\nCount=0\n", "Schema=2147483648\nCount=0\n", "Schema=\nCount=0\n",
        "Schema=1\nCount=-1\n", "Schema=1\nCount=4097\n", "Schema=1\nCount=0junk\n",
        "Schema=1\nCount=2147483648\n", "Schema=1\nCount=-2147483649\n", "Schema=1\nCount=\n"}) {
        reject(prefix + header, "invalid match score schema/count", "schema/count parsing");
    }
    const std::string recordPrefix = prefix + "Schema=1\nCount=1\n0=";
    for (const char* record : {
        "", "41 1 2 3 4", "41 1 2 3 4 5 extra", "41 1 2 3 4 5 0", "41 1 2 3 4 5junk",
        "4 1 2 3 4 5", "gg 1 2 3 4 5", "20 1 2 3 4 5", "090a0d0b0c 1 2 3 4 5",
        "41 1 2 nope 4 5", "41 1 2 3 4 -2147483649", "41 1 2 3 4 999999999999999999999999"}) {
        reject(recordPrefix + record + '\n', "invalid match score record 0", "malformed Schema 1 record");
    }
    reject(prefix + "Schema=1\nCount=1\n", "invalid match score record 0", "missing indexed record");
    reject(recordPrefix + iniHexEncode(std::string(1025, 'a')) + " 1 2 3 4 5\n",
        "invalid match score record 0", "oversized decoded owner");
    reject(recordPrefix + iniHexEncode(std::string(1024, ' ') + 'a') + " 1 2 3 4 5\n",
        "invalid match score record 0", "owner byte limit before canonical trimming");
    for (const int version : {1, 2}) {
        const int fields = version == 1 ? 5 : 6;
        for (int field = 0; field < fields; ++field) {
            for (const std::string_view invalid : {"-1", "2147483648"}) {
                std::string record = "41";
                for (int index = 0; index < fields; ++index) {
                    record += ' ';
                    record += index == field ? invalid : "0";
                }
                reject(prefix + "Schema=" + std::to_string(version) + "\nCount=1\n0=" + record + '\n',
                    "invalid match score record 0", "negative/overflow numeric field");
            }
        }
    }
    for (const char* record : {"41 1 2 3 4 5", "41 1 2 3 4 5 6 extra", "41 1 2 3 4 5 6junk"}) {
        reject(prefix + "Schema=2\nCount=1\n0=" + record + '\n',
            "invalid match score record 0", "malformed Schema 2 built field");
    }
    reject(prefix + "Schema=1\nCount=2\n0=41 1 2 3 4 5\n1=42 -1 0 0 0 0\n",
        "invalid match score record 1", "rejection after an earlier valid record");
    reject(prefix + "Schema=2\nCount=2\n0=414c504841 1 2 3 4 5 6\n"
        "1=2009616c7068610b0c 6 5 4 3 2 1\n", "invalid match score record 1",
        "duplicate canonical owners differing in case and whitespace");
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testMigratedStateSmokeCodec();
    success &= testEmptyAndLegacyAbsence();
    success &= testFixedSaveHashAndRoundTrip();
    success &= testEventAccumulationAndSaturation();
    success &= testOwnerAndHeaderCompatibility();
    success &= testRejectedLoadsAreAtomic();
    if (!success) return 1;
    std::cout << "Runtime match score contract: events, saturation, Schema 1/2, owner order/bytes, "
        "legacy hash goldens, atomic rejection and roundtrip passed.\n";
    return 0;
}