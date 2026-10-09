#include "RuntimeGameSpeedTiming.h"
#include "RuntimeBuildingDepthOracle.h"

#include <array>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using RuntimeBuildingDepthOracle::Bytes;
using RuntimeBuildingDepthOracle::little;

std::size_t peOffset(const Bytes& bytes, std::uint32_t virtualAddress) {
    const auto pe = little(bytes, 0x3c, 4);
    if (little(bytes, pe, 4) != 0x4550 || little(bytes, pe + 24, 2) != 0x10b)
        throw std::runtime_error("retail PE32 header differs");
    const auto base = little(bytes, pe + 24 + 28, 4);
    if (virtualAddress < base) throw std::runtime_error("retail address precedes image");
    const auto rva = virtualAddress - base;
    const auto sectionStart = pe + 24 + little(bytes, pe + 20, 2);
    for (std::uint32_t index = 0; index < little(bytes, pe + 6, 2); ++index) {
        const auto section = sectionStart + index * 40;
        const auto start = little(bytes, section + 12, 4);
        const auto size = little(bytes, section + 16, 4);
        if (rva >= start && rva - start < size)
            return little(bytes, section + 20, 4) + rva - start;
    }
    throw std::runtime_error("retail address is outside file-backed sections");
}

void require(bool value, const std::string& description, int& checks) {
    if (!value) throw std::runtime_error(description);
    ++checks;
}

void requireCode(const Bytes& bytes, std::uint32_t address,
    std::initializer_list<unsigned char> expected, int& checks) {
    const auto start = peOffset(bytes, address);
    require(start + expected.size() <= bytes.size()
        && std::equal(expected.begin(), expected.end(), bytes.begin() + start),
        "retail game-speed instruction bytes differ", checks);
}

// Independent reconstruction of the audited original timer instructions.
// This oracle uses integer milliseconds and never calls the production timing
// helper. Native frames at 960 ms are exactly 10/12/15/20/30/60 for delays 6..1.
int originalFrameCount(int delay, int untilMilliseconds) {
    int began = 0;
    int frames = 0;
    for (int milliseconds = 1; milliseconds <= untilMilliseconds; ++milliseconds) {
        const int timer = milliseconds >> 4;
        if (timer - began >= delay) {
            ++frames;
            began = timer;
        }
    }
    return frames;
}

// The user selected 50% faster finite speeds on top of the audited retail
// cadence. Keep this expectation in integer time, independent of the runtime
// helper and its floating-point accumulation (some rates yield partial ticks).
int acceleratedFrameCount(int delay, int tickHz, int untilMilliseconds) {
    return untilMilliseconds * tickHz * 3 / (16 * delay * 15 * 2);
}
}

int main(int argc, char** argv) {
    try {
        int checks = 0;
        const auto root = argc > 1 ? std::filesystem::path(argv[1])
            : std::filesystem::current_path();
        const auto retail = RuntimeBuildingDepthOracle::readPinned(
            root / "RedAlert2/GameMD/gamemd.exe",
            "7b8a068535d6af06845edf95ae829b113d00c02909330e16f197426cd7db94b6");
        // timeGetTime import followed by shr eax,4, ret.
        requireCode(retail, 0x6c8c40,
            {0xff,0x15,0x30,0x15,0x7e,0x00,0xc1,0xe8,0x04,0xc3}, checks);
        // UI slider is reversed; options value is copied into the frame timer.
        requireCode(retail, 0x4f160f,
            {0xbe,0x06,0x00,0x00,0x00,0x2b,0xf0,0xa1,0x60,0xeb,0xa8,0x00}, checks);
        requireCode(retail, 0x55d79e,
            {0x8b,0x35,0x60,0xeb,0xa8,0x00}, checks);
        requireCode(retail, 0x55d7bc,
            {0x89,0x35,0x50,0x73,0x88,0x00}, checks);
        // Native loop exits both for elapsed >= duration and remaining == 0.
        requireCode(retail, 0x55e2c8,
            {0x2b,0x05,0x48,0x73,0x88,0x00,0x3b,0xc6,0x7d,0x69,
             0x2b,0xf0,0x3b,0xf5,0x74,0x63}, checks);
        const auto importNameRva = little(retail, peOffset(retail, 0x7e1530), 4);
        const auto importName = peOffset(retail, 0x400000 + importNameRva) + 2;
        const std::string expectedImport = "timeGetTime";
        require(importName + expectedImport.size() < retail.size()
            && std::equal(expectedImport.begin(), expectedImport.end(),
                retail.begin() + importName)
            && retail[importName + expectedImport.size()] == 0,
            "retail clock import is not timeGetTime", checks);

        constexpr std::array<int, 6> referenceFrames{{10,12,15,20,30,60}};
        constexpr std::array<int, 3> tickRates{{15,30,60}};
        constexpr std::array<int, 2> batchBudgets{{8,16}};
        constexpr double expectedMilliseconds[3][6] = {
            {64,160.0/3.0,128.0/3.0,32,64.0/3.0,32.0/3.0},
            {32,80.0/3.0,64.0/3.0,16,32.0/3.0,16.0/3.0},
            {16,40.0/3.0,32.0/3.0,8,16.0/3.0,8.0/3.0}
        };
        constexpr int expectedRoundedTickRates[3][6] = {
            {16,19,23,31,47,94}, {31,38,47,63,94,188}, {63,75,94,125,188,375}
        };
        for (int speed = 0; speed < 6; ++speed) {
            require(originalFrameCount(6 - speed, 960) == referenceFrames[speed],
                "original integer-clock reference count differs", checks);
            for (std::size_t rate = 0; rate < tickRates.size(); ++rate) {
                const auto timing = runtimeGameSpeedTiming(speed, tickRates[rate]);
                require(!timing.unlimited()
                    && std::abs(timing.secondsPerTick * 1000.0
                        - expectedMilliseconds[rate][speed]) < 1e-12,
                    "finite slider interval does not apply the requested 50% speedup", checks);
                require(timing.roundedTickRate() == expectedRoundedTickRates[rate][speed],
                    "diagnostic rounded tick rate differs", checks);
                require(acceleratedFrameCount(6 - speed, tickRates[rate], 960)
                    == referenceFrames[speed] * tickRates[rate] * 3 / (15 * 2),
                    "accelerated integer-clock reference count differs", checks);
                for (const int budget : batchBudgets) {
                    double accumulated = 0.0;
                    int advanced = 0;
                    // One extra millisecond avoids choosing a floating-point
                    // boundary, and is smaller than every tested tick interval.
                    for (int ms = 0; ms < 961; ++ms) {
                        accumulated = timing.accumulate(accumulated, 0.001, budget);
                        int inBatch = 0;
                        while (timing.ready(accumulated, inBatch, budget)) {
                            ++advanced;
                            ++inBatch;
                            accumulated = timing.consume(accumulated);
                        }
                    }
                    require(advanced == acceleratedFrameCount(6 - speed, tickRates[rate], 961),
                        "wall-clock schedule does not preserve the requested speedup", checks);
                    accumulated = timing.accumulate(0.0, 3600.0, budget);
                    int caughtUp = 0;
                    while (timing.ready(accumulated, caughtUp, budget)) {
                        ++caughtUp;
                        accumulated = timing.consume(accumulated);
                    }
                    require(caughtUp == budget,
                        "finite catch-up is unbounded or drops a large backlog", checks);
                    require(!timing.ready(accumulated, 0, budget),
                        "finite catch-up leaves an extra tick after exhausting its credit", checks);
                    require(!timing.ready(0.0, 0, budget)
                        && !timing.ready(3600.0, budget, budget),
                        "finite wait or per-loop cap is bypassed", checks);
                    require(timing.accumulate(0.0, -1.0, budget) == 0.0
                        && timing.consume(0.0, 10) == 0.0,
                        "clock underflow creates simulation credit", checks);
                }
            }
        }
        for (const int hz : tickRates) {
            const auto fastest = runtimeGameSpeedTiming(6, hz);
            require(fastest.unlimited() && fastest.secondsPerTick == 0.0
                && fastest.roundedTickRate() == 0,
                "fastest has an artificial fixed rate", checks);
            for (const int budget : batchBudgets) {
                double accumulated = fastest.accumulate(0.0, 0.0, budget);
                int advanced = 0;
                while (fastest.ready(accumulated, advanced, budget)) {
                    ++advanced;
                    accumulated = fastest.consume(accumulated);
                }
                require(advanced == budget && accumulated == 0.0,
                    "unlimited zero-wall-time batch does not preserve input budget", checks);
            }
        }
        require(runtimeGameSpeedTiming(-1, 30).nativeWaitQuanta == 6
            && runtimeGameSpeedTiming(7, 30).unlimited()
            && std::isfinite(runtimeGameSpeedTiming(0, 0).secondsPerTick),
            "invalid configuration is not bounded", checks);
        std::cout << "Game speed timing smoke: passed=yes checks=" << checks
            << " finiteSchedules=36 unlimitedSchedules=6 retailClockQuantumMs=16"
            << " finiteSpeedMultiplier=1.5 batchBudgets=8,16"
            << " displayFpsMeasured=no\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Game speed timing smoke: passed=no " << error.what() << '\n';
        return 1;
    }
}
