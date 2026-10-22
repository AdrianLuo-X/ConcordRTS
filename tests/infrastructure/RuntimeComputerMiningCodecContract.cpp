#include "simulation/ai/RuntimeComputerMining.h"
#include "core/io/IniFile.h"
#ifndef S18_FREEZE_OLD_ORACLE
#include "simulation/ai/RuntimeComputerMiningCodec.h"
#endif
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "RuntimeComputerMiningOldOracle.inl"

namespace {
using Mining = std::map<std::string, RuntimeComputerMiningState>;
struct Fixture { std::string name; std::string text; bool accepted; };
constexpr std::array<std::uint64_t, 4> seeds = {
    0, 1469598103934665603ull, 0xffffffffffffffffull, 0xdecafbad12345678ull
};
int checks = 0;
void require(bool value, const std::string& why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
Mining sentinel() { return {{"preexisting", {7, 13, 3, 17, 19, 23}}}; }
std::string oldWrite(const Mining& mining) {
    std::ostringstream out;
    s18_old::writeRuntimeComputerMining(out, s18_old::RuntimeWindowState{mining});
    return out.str();
}
void oldMix(std::uint64_t& hash, const Mining& mining) {
    s18_old::mixHash(hash, mining);
}
#ifndef S18_FREEZE_OLD_ORACLE
std::string newWrite(const Mining& mining) {
    std::ostringstream out;
    writeRuntimeComputerMining(out, mining);
    return out.str();
}
#endif
std::string describe(bool accepted, const std::string& error, const Mining& mining,
    bool production) {
    std::string saved;
#ifdef S18_FREEZE_OLD_ORACLE
    (void)production;
    saved = oldWrite(mining);
#else
    saved = production ? newWrite(mining) : oldWrite(mining);
#endif
    std::ostringstream out;
    out << "accepted=" << accepted << "\nerror=" << error
        << "\nstateBytes=" << saved.size() << "\n" << saved;
    for (const auto seed : seeds) {
        auto hash = seed;
#ifdef S18_FREEZE_OLD_ORACLE
        oldMix(hash, mining);
#else
        if (production) mixRuntimeComputerMiningHash(hash, mining);
        else oldMix(hash, mining);
#endif
        out << "seed=" << seed << " hash=" << hash << "\n";
    }
    return out.str();
}
std::vector<Fixture> fixtures() {
    const std::string header = "[ComputerMining]\nSchema=1\nCount=1\n";
    const std::string record = "0=Alpha|7|80|3|9|11|13\n";
    std::vector<Fixture> result = {
        {"missing-section", "[Other]\nCount=2\n", true},
        {"empty-input", "", true},
        {"empty-state", "[ComputerMining]\nSchema=1\nCount=0\n", true},
        {"normal", header + record, true},
        {"multiple-owner", "[ComputerMining]\nSchema=1\nCount=3\n"
            "0= Zulu |10|300|2|4|5|6\n1= alpha |7|80|3|9|11|13\n"
            "2= BRAVO |0|0|0|0|0|0\n", true},
        {"truncated", "[ComputerMining]\nSchema=1\nCount=2\n" + record, false},
        {"empty-section", "[ComputerMining]\n", false},
        {"missing-schema", "[ComputerMining]\nCount=0\n", false},
        {"schema-two", "[ComputerMining]\nSchema=2\nCount=0\n", false},
        {"negative-count", "[ComputerMining]\nSchema=1\nCount=-1\n", false},
        {"too-many", "[ComputerMining]\nSchema=1\nCount=10001\n", false},
        {"overflow-count", "[ComputerMining]\nSchema=1\nCount=2147483648\n", false},
        {"count-suffix", "[ComputerMining]\nSchema=1\nCount=1x\n" + record, false},
        {"missing-count", "[ComputerMining]\nSchema=1\n", false},
        {"extra-fields", header + "0=alpha|7|80|3|9|11|13|17\n", false},
        {"trailing-delimiter", header + "0=alpha|7|80|3|9|11|13|\n", false},
        {"short-record", header + "0=alpha|7|80|3|9|11\n", false},
        {"empty-owner", header + "0= \t |7|80|3|9|11|13\n", false},
        {"duplicate-owner", "[ComputerMining]\nSchema=1\nCount=2\n"
            "0= ALPHA |7|80|3|9|11|13\n1=alpha|1|1|0|1|1|1\n", false},
        {"gem-exceeds-units", header + "0=alpha|1|80|2|9|11|13\n", false},
        {"zero-cargo-positive-value", header + "0=alpha|0|80|0|9|11|13\n", true},
        {"int-max", header + "0=alpha|2147483647|2147483647|2147483647|"
            "2147483647|2147483647|2147483647\n", true},
        {"duplicate-headers-last-wins", "[ComputerMining]\nSchema=99\nCount=99999\n"
            "schema=1\nCOUNT=1\n" + record, true},
        {"duplicate-record-last-wins", header + "0=malformed\n" + record, true},
        {"duplicate-record-last-fails", header + record + "0=malformed\n", false},
        {"unknown-fields", header + record + "Unknown=anything\n999=ignored\n", true},
        {"repeated-section", "[ComputerMining]\nSchema=1\nCount=2\n0=bad\n"
            "[computermining]\nCount=1\n" + record, true},
        {"case-insensitive-keys", "[computermining]\nsChEmA=1\ncOuNt=1\n" + record, true},
        {"comments-and-whitespace", " [ ComputerMining ] ; hi\nSchema=+01 // hi\n"
            "Count=01\n0= Alpha | +07 | 080 | 03 | 09 | 011 | 013 ; hi\n", true},
        {"negative-zero", header + "0=alpha|-0|+0|-0|0|00|+00\n", true},
        {"owner-punctuation", header + "0=House:01@!|7|80|3|9|11|13\n", true},
        {"empty-count-nonempty-ignored", "[ComputerMining]\nSchema=1\nCount=0\n"
            "0=bad\n", true}
    };
    const std::array<std::string, 6> normal = {"7", "80", "3", "9", "11", "13"};
    const std::array<std::string, 7> invalid = {"", "-1", "nan", "1x",
        "2147483648", "-2147483649", "0x10"};
    for (std::size_t field = 0; field < normal.size(); ++field) {
        for (std::size_t kind = 0; kind < invalid.size(); ++kind) {
            auto values = normal; values[field] = invalid[kind];
            std::string text = header + "0=alpha";
            for (const auto& value : values) text += "|" + value;
            text += "\n";
            result.push_back({"invalid-" + std::to_string(field) + "-"
                + std::to_string(kind), text, false});
        }
    }
    std::string limit = "[ComputerMining]\nSchema=1\nCount=10000\n";
    for (int i = 0; i < 10000; ++i)
        limit += std::to_string(i) + "=owner" + std::to_string(i) + "|1|2|0|3|4|5\n";
    result.push_back({"limit-10000", limit, true});
    return result;
}
std::vector<Mining> writerStates() {
    return {{}, sentinel(), {{"z|line\r\n", {1, 2, 0, 3, 4, 5}},
        {" A B ", {2, 3, 1, 4, 5, 6}}, {"", {0, 0, 0, 0, 0, 0}}},
        {{"owner", {-1, -2, -3, -4, -5, -6}}}};
}
std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), "missing frozen fixture " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}
void newFile(const std::filesystem::path& path, const std::string& data) {
    require(!std::filesystem::exists(path), "cannot rewrite frozen fixture");
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    require(static_cast<bool>(out), "fixture write failed");
}
void runFixtures(const std::filesystem::path& root, bool freeze) {
    std::string index;
    for (const auto& test : fixtures()) {
        index += test.name + "\n";
        IniFile ini; require(ini.loadFromString(test.text), "INI parse");
        auto old = sentinel(); std::string oldError = "retained-error";
        const bool oldOk = s18_old::readRuntimeComputerMining(ini, old, oldError);
        require(oldOk == test.accepted, "literal old acceptance " + test.name);
        if (!oldOk) require(oldWrite(old) == oldWrite(sentinel()),
            "literal old failure atomicity " + test.name);
        const auto expected = describe(oldOk, oldError, old, false);
        if (freeze) {
            newFile(root / (test.name + ".ini"), test.text);
            newFile(root / (test.name + ".expected"), expected);
        } else {
            require(readFile(root / (test.name + ".ini")) == test.text,
                "complete frozen input " + test.name);
            require(readFile(root / (test.name + ".expected")) == expected,
                "literal old oracle drift " + test.name);
#ifndef S18_FREEZE_OLD_ORACLE
            auto actual = sentinel(); std::string error = "retained-error";
            const bool ok = readRuntimeComputerMining(ini, actual, error);
            require(describe(ok, error, actual, true) == expected,
                "production byte/error/hash/atomicity " + test.name);
            if (!ok) require(newWrite(actual) == oldWrite(sentinel()),
                "production failed state unchanged " + test.name);
#endif
        }
    }
    const auto writers = writerStates();
    for (std::size_t i = 0; i < writers.size(); ++i) {
        const auto expected = describe(true, "writer", writers[i], false);
        const auto path = root / ("writer-" + std::to_string(i) + ".expected");
        if (freeze) newFile(path, expected);
        else {
            require(readFile(path) == expected, "literal writer fixture");
#ifndef S18_FREEZE_OLD_ORACLE
            require(describe(true, "writer", writers[i], true) == expected,
                "production direct writer and seeded hash");
#endif
        }
    }
    if (freeze) newFile(root / "index.txt", index);
    else require(readFile(root / "index.txt") == index, "complete fixture index");
}
#ifndef S18_FREEZE_OLD_ORACLE
void generatedRoundTrips() {
    std::uint64_t random = 0x123456789abcdefull;
    for (int sample = 0; sample < 1024; ++sample) {
        Mining mining;
        for (int owner = 0; owner < sample % 9; ++owner) {
            auto next = [&random]() {
                random = random * 6364136223846793005ull + 1442695040888963407ull;
                return static_cast<int>((random >> 32) & 0x7fffffffull);
            };
            RuntimeComputerMiningState record;
            record.storedResourceUnits = next(); record.storedResourceValue = next();
            record.storedResourceGemUnits = next() % (record.storedResourceUnits / 2 + 1);
            record.resourceTrips = next(); record.harvesterTeleports = next();
            record.slaveMinerLoads = next();
            mining.emplace("house-" + std::to_string(owner), record);
        }
        require(newWrite(mining) == oldWrite(mining), "generated writer bytes");
        IniFile ini; require(ini.loadFromString(newWrite(mining)), "generated INI");
        Mining restored = sentinel(); std::string error = "retained-error";
        require(readRuntimeComputerMining(ini, restored, error), "generated read");
        require(error == "retained-error", "success preserves existing error");
        require(newWrite(restored) == newWrite(mining), "complete roundtrip state");
        for (const auto seed : seeds) {
            auto expected = seed, actual = seed, restoredHash = seed;
            oldMix(expected, mining); mixRuntimeComputerMiningHash(actual, mining);
            mixRuntimeComputerMiningHash(restoredHash, restored);
            require(expected == actual && actual == restoredHash,
                "generated exact seeded roundtrip hash");
            if (mining.empty()) require(actual == seed, "empty hash contributes nothing");
        }
        auto changed = mining;
        if (!changed.empty()) {
            for (int field = 0; field < 6; ++field) {
                auto mutated = changed;
                auto& value = mutated.begin()->second;
                int* fields[] = {&value.storedResourceUnits, &value.storedResourceValue,
                    &value.storedResourceGemUnits, &value.resourceTrips,
                    &value.harvesterTeleports, &value.slaveMinerLoads};
                *fields[field] ^= 1;
                std::uint64_t expected = seeds[1], actual = seeds[1];
                oldMix(expected, mutated); mixRuntimeComputerMiningHash(actual, mutated);
                require(expected == actual, "each field contributes in old order");
            }
        }
    }
}
#endif
}
int main(int argc, char** argv) {
    try {
        const bool freeze = argc == 3 && std::string(argv[1]) == "--freeze";
        require(freeze || argc == 2, "usage: contract [--freeze] fixture-directory");
        const std::filesystem::path root(argv[freeze ? 2 : 1]);
        if (freeze) {
            require(!std::filesystem::exists(root), "fresh frozen fixture directory");
            std::filesystem::create_directories(root);
        }
#ifndef S18_FREEZE_OLD_ORACLE
        require(!freeze, "production cannot generate expected fixtures");
#endif
        runFixtures(root, freeze);
#ifndef S18_FREEZE_OLD_ORACLE
        require(!freeze, "production cannot generate expected fixtures");
        generatedRoundTrips();
#endif
        std::cout << "ComputerMining codec contract: fixtures=" << fixtures().size()
            << " directWriters=" << writerStates().size() << " seeds=" << seeds.size()
            << " checks=" << checks << " failures=0 literalOldOracle=1";
#ifdef S18_FREEZE_OLD_ORACLE
        std::cout << " frozenBeforeProduction=1\n";
#else
        std::cout << " sameProductionCpp=1 generatedRoundTrips=1024\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ComputerMining codec contract failed: " << error.what() << "\n";
        return 1;
    }
}
