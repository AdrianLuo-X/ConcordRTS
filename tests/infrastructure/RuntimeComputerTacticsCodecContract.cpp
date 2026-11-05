#include "simulation/ai/RuntimeComputerTactics.h"
#include "core/io/IniFile.h"
#ifndef CONCORD_TACTICS_ORIGINAL_BASELINE
#include "simulation/ai/RuntimeComputerTacticsCodec.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "RuntimeComputerTacticsOldOracle.inl"

using Tactics = std::map<std::string, RuntimeComputerTacticsState>;
namespace {
int checks = 0;
bool require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) std::cerr << "Tactics codec: " << message << '\n';
    return condition;
}
std::string oldBytes(const Tactics& map) {
    original_tactics_codec::RuntimeWindowState state;
    state.triggers.computerTacticsByOwner = map;
    std::ostringstream output;
    original_tactics_codec::writeRuntimeComputerTactics(output, state);
    return output.str();
}
std::string bytes(const Tactics& map) {
#ifdef CONCORD_TACTICS_ORIGINAL_BASELINE
    return oldBytes(map);
#else
    std::ostringstream output;
    writeRuntimeComputerTacticsState(output, map);
    return output.str();
#endif
}
bool read(const IniFile& ini, Tactics& output, std::string& error) {
#ifdef CONCORD_TACTICS_ORIGINAL_BASELINE
    return original_tactics_codec::readRuntimeComputerTactics(ini, output, error);
#else
    return readRuntimeComputerTactics(ini, output, error);
#endif
}
Tactics fixture(int flags = 63) {
    Tactics result;
    auto& house = result["alpha"];
    house.nextTriggerTick = 12;
    house.selectionSerial = 9;
    house.nextTeamInstanceId = 8;
    auto& team = house.teams[7];
    team.instanceId = 7;
    team.triggerId = "trigger-a";
    team.teamTypeId = "team-a";
    team.targetOwner = "enemy";
    team.createdTick = 10;
    team.lastProgressTick = 11;
    team.missionInstanceId = 42;
    team.secondary = (flags & 1) != 0;
    team.baseDefense = (flags & 2) != 0;
    team.launched = (flags & 4) != 0;
    team.outcomeReported = (flags & 8) != 0;
    team.missionFinished = (flags & 16) != 0;
    team.achievedGreatSuccess = (flags & 32) != 0;
    team.objectIds = {4, 8};
    if (team.launched) team.joiningObjectIds = {8};
    team.missingMembers = {"HTNK"};
    house.production[0] = {7, "HTNK", 7, 300, 9};
    house.history["trigger-a"] = {1.25, 5, 3, 2};
    house.captureDecisions[99] = {31, 10, 3};
    result["zeta"] = {};
    return result;
}
std::string replace(std::string text, const std::string& from, const std::string& to) {
    const auto position = text.find(from);
    if (position == std::string::npos) throw std::runtime_error("test replacement not found: " + from);
    text.replace(position, from.size(), to);
    return text;
}
bool decodeCase(const std::string& text, bool expected, const std::string& label) {
    IniFile ini;
    if (!require(ini.loadFromString(text), label + " fixture INI")) return false;
    auto current = fixture();
    auto original = current;
    const auto unchanged = bytes(current);
    std::string error = "sentinel", oldError = error;
    const bool result = read(ini, current, error);
    const bool old = original_tactics_codec::readRuntimeComputerTactics(ini, original, oldError);
    return require(result == expected, label + " independent expected outcome")
        && require(result == old && error == oldError && bytes(current) == oldBytes(original), label + " original result/error/all serialized fields")
        && require(expected || bytes(current) == unchanged, label + " failed read atomicity");
}
}

int main() try {
    const auto full = fixture();
    const std::string expected = "\n[ComputerTactics]\nSchema=2\nCount=10\n"
        "0=O|alpha|12|9|8\n1=T|alpha|7|trigger-a|team-a|enemy|10|11|42|63\n"
        "2=B|alpha|7|4\n3=B|alpha|7|8\n4=J|alpha|7|8\n5=M|alpha|7|HTNK\n"
        "6=P|alpha|0|7|HTNK|7|300|9\n7=H|alpha|trigger-a|1.25|5|3|2\n"
        "8=C|alpha|99|31|10|3\n9=O|zeta|0|0|1\n";
    if (!require(bytes(full) == expected, "independent exact schema2 record order/bytes")) return 1;
    if (!require(bytes(Tactics{}) == "\n[ComputerTactics]\nSchema=2\nCount=0\n", "empty writer bytes")) return 1;
    if (!decodeCase("[Unrelated]\nX=1\n", true, "missing section clears old state")) return 1;
    if (!decodeCase("[ComputerTactics]\nSchema=2\nCount=0\n", true, "empty state")) return 1;
    for (int flags = 0; flags < 64; ++flags) {
        const auto map = fixture(flags);
        if (!require(bytes(map) == oldBytes(map), "all six flag bits writer " + std::to_string(flags))
            || !decodeCase(bytes(map), true, "all six flag bits reader " + std::to_string(flags))) return 1;
    }
    auto schema1 = full;
    schema1.at("alpha").captureDecisions.clear();
    if (!decodeCase(replace(bytes(schema1), "Schema=2", "Schema=1"), true, "schema1 compatibility")) return 1;
    if (!decodeCase(replace(expected, "Schema=2", "Schema=1"), false, "schema1 capture rejected")) return 1;
    for (const auto& item : std::vector<std::pair<std::string,std::string>>{
        {"schema0",replace(expected,"Schema=2","Schema=0")},
        {"schema3",replace(expected,"Schema=2","Schema=3")},
        {"negative count",replace(expected,"Count=10","Count=-1")},
        {"oversize count",replace(expected,"Count=10","Count=250001")},
        {"truncated record",replace(expected,"Count=10","Count=11")},
        {"bad owner integer",replace(expected,"O|alpha|12|9|8","O|alpha|12x|9|8")},
        {"overflow integer",replace(expected,"O|alpha|12|9|8","O|alpha|2147483648|9|8")},
        {"duplicate owner",replace(expected,"O|zeta|0|0|1","O|ALPHA|0|0|1")},
        {"unowned record",replace(expected,"T|alpha|7|","T|unknown|7|")},
        {"unknown record",replace(expected,"T|alpha|7|","X|alpha|7|")},
        {"flag64",replace(expected,"|11|42|63","|11|42|64")},
        {"team id limit",replace(expected,"O|alpha|12|9|8","O|alpha|12|9|7")},
        {"reversed tick",replace(expected,"|10|11|42|63","|10|9|42|63")},
        {"launched without mission",replace(expected,"|11|42|63","|11|-1|63")},
        {"duplicate body",replace(expected,"3=B|alpha|7|8","3=B|alpha|7|4")},
        {"joining not member",replace(expected,"J|alpha|7|8","J|alpha|7|99")},
        {"invalid lane",replace(expected,"P|alpha|0|7","P|alpha|3|7")},
        {"invalid production clock",replace(expected,"|HTNK|7|300|9","|HTNK|10|300|9")},
        {"production without missing",replace(expected,"M|alpha|7|HTNK","M|alpha|7|OTHER")},
        {"invalid capture",replace(expected,"C|alpha|99|31|10|3","C|alpha|99|31|10|6")},
        {"negative weight",replace(expected,"|trigger-a|1.25|5|3|2","|trigger-a|-1|5|3|2")},
        {"nonfinite weight",replace(expected,"|trigger-a|1.25|5|3|2","|trigger-a|nan|5|3|2")},
        {"history overflow",replace(expected,"|trigger-a|1.25|5|3|2","|trigger-a|1.25|4|3|2")},
    }) if (!decodeCase(item.second, false, item.first)) return 1;
    // Preserve the original permissive stod-prefix rule; no policy repair here.
    if (!decodeCase(replace(expected,"|trigger-a|1.25|5|3|2","|trigger-a|1.25suffix|5|3|2"), true, "original double prefix acceptance")) return 1;
    auto escaped = full;
    escaped.at("alpha").teams.at(7).triggerId = "trigger|line\nreturn\r";
    if (!require(bytes(escaped) == oldBytes(escaped), "original pipe/newline escaping")) return 1;
    std::cout << "Computer tactics codec passed: " << checks << " checks, schema1/2, all flags, exact records, atomic rejects\n";
    return 0;
} catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
}
