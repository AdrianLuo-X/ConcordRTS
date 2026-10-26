#include "render/buildings/RuntimeAuxiliaryAnimation.h"

#include <initializer_list>
#include <iostream>
#include <limits>

namespace {

struct Sample { int tick; int frame; };
int checks = 0;

bool trace(const char* name, const ArtType* art, int frameCount, bool mainOnly,
    int tickHz, std::initializer_list<Sample> samples) {
    bool passed = true;
    for (const auto sample : samples) {
        ++checks;
        const int frame = runtimeAuxiliaryAnimationFrame(art, frameCount, mainOnly, sample.tick, tickHz);
        if (frame != sample.frame) {
            std::cerr << name << " tick=" << sample.tick << " expected=" << sample.frame
                << " actual=" << frame << '\n';
            passed = false;
        }
    }
    return passed;
}

bool frameRanges() {
    bool passed = true;
    passed &= trace("empty", nullptr, 0, true, 15, {{0, 0}, {100, 0}});
    passed &= trace("negative-count", nullptr, -1, false, 15, {{0, 0}, {100, 0}});
    passed &= trace("singleton", nullptr, 1, true, 15, {{0, 0}, {100, 0}});
    passed &= trace("whole-file", nullptr, 6, false, 15, {{0, 1}, {5, 5}, {6, 0}, {7, 1}});
    passed &= trace("main-shadow-halves", nullptr, 6, true, 15, {{-3, 1}, {0, 1}, {2, 2}, {3, 0}});
    passed &= trace("odd-file-no-half", nullptr, 5, true, 15, {{4, 4}, {5, 0}, {6, 1}});

    ArtType art;
    art.hasAnimationStart = true; art.animationStart = 2;
    art.hasAnimationEnd = true; art.animationEnd = 3;
    passed &= trace("start-plus-count", &art, 10, false, 15, {{0, 3}, {2, 4}, {3, 2}, {4, 3}});
    art.hasAnimationLoopStart = true; art.animationLoopStart = 3;
    passed &= trace("end-relative-to-start", &art, 10, false, 15, {{0, 4}, {2, 3}, {3, 4}});
    art.hasAnimationLoopEnd = true; art.animationLoopEnd = 7;
    passed &= trace("loop-overrides-start-end", &art, 10, true, 15,
        {{0, 4}, {2, 5}, {3, 6}, {4, 3}, {5, 4}});
    art.hasAnimationStart = false; art.hasAnimationLoopEnd = false;
    passed &= trace("end-relative-to-loop-start", &art, 10, false, 15, {{0, 4}, {2, 5}, {3, 3}});
    art.animationEnd = -10;
    passed &= trace("nonpositive-end-count", &art, 10, false, 15, {{0, 3}, {999, 3}});
    art.hasAnimationLoopEnd = true; art.animationLoopEnd = -1;
    passed &= trace("backward-loop-collapses", &art, 10, false, 15, {{0, 3}, {999, 3}});
    art.animationLoopEnd = 300;
    passed &= trace("loop-end-clipped", &art, 10, true, 15, {{6, 9}, {7, 3}, {8, 4}});
    art.animationLoopStart = 300;
    passed &= trace("start-clipped", &art, 10, true, 15, {{0, 9}, {999, 9}});
    art.animationLoopStart = -300; art.animationLoopEnd = 3;
    passed &= trace("negative-start-clipped", &art, 10, false, 15, {{0, 1}, {2, 2}, {3, 0}});
    art.hasAnimationLoopEnd = false; art.hasAnimationEnd = false; art.animationLoopStart = 7;
    passed &= trace("start-after-main-half", &art, 10, true, 15, {{0, 7}, {999, 7}});
    return passed;
}

bool reflectedCycles() {
    ArtType art;
    art.hasAnimationLoopStart = true; art.animationLoopStart = 2;
    art.hasAnimationLoopEnd = true; art.animationLoopEnd = 6;
    art.pingPong = true;
    bool passed = trace("reflected-four", &art, 10, false, 15,
        {{0, 3}, {1, 3}, {2, 4}, {3, 5}, {4, 4}, {5, 3}, {6, 2}, {7, 3}, {12, 2}});
    art.animationLoopEnd = 4;
    passed &= trace("reflected-two", &art, 10, false, 15, {{0, 3}, {1, 3}, {2, 2}, {3, 3}, {4, 2}});
    art.animationLoopEnd = 3;
    passed &= trace("reflected-one", &art, 10, false, 15, {{0, 2}, {1, 2}, {100, 2}});
    return passed;
}

bool rateCadence() {
    ArtType art;
    art.rate = 120;
    bool passed = trace("rate120-hz15", &art, 10, false, 15,
        {{0, 1}, {6, 1}, {7, 1}, {13, 1}, {14, 2}, {21, 3}, {70, 0}});
    passed &= trace("rate120-hz30", &art, 10, false, 30,
        {{0, 1}, {13, 1}, {14, 1}, {27, 1}, {28, 2}, {42, 3}, {140, 0}});
    art.rate = 0;
    passed &= trace("default-rate", &art, 10, false, 60, {{0, 1}, {7, 1}, {8, 2}, {12, 3}});
    art.rate = -1;
    passed &= trace("negative-rate", &art, 10, false, 15, {{-100, 1}, {0, 1}, {2, 2}});
    art.rate = 901;
    passed &= trace("fast-rate", &art, 10, false, 15, {{0, 1}, {2, 2}, {10, 0}});
    passed &= trace("low-hz-clamp", &art, 10, false, 0, {{0, 1}, {2, 2}});
    passed &= trace("high-hz-clamp", &art, 10, false, 2000, {{133, 1}, {134, 2}, {201, 3}});
    return passed;
}

bool integerBoundaries() {
    constexpr int largest = std::numeric_limits<int>::max();
    constexpr int smallest = std::numeric_limits<int>::min();
    ArtType art;
    art.hasAnimationStart = true; art.animationStart = largest - 4;
    art.hasAnimationEnd = true; art.animationEnd = largest;
    bool passed = trace("end-addition-wide", &art, largest, false, 15,
        {{0, largest - 3}, {2, largest - 2}, {3, largest - 1}, {4, largest - 4}});
    art.hasAnimationStart = false; art.hasAnimationEnd = false; art.pingPong = true;
    passed &= trace("reflected-period-wide", &art, largest, false, 15,
        {{smallest, 1}, {largest - 1, largest - 1}, {largest, largest - 2}});

    const int bounds[] = {smallest, -1, 0, 1, 2, 17, largest};
    const int ticks[] = {smallest, 0, 1, 2, 31, largest};
    // Malformed authored bounds must always map to an existing frame, including
    // overflowing Start + End inputs. Expected traces above define the behavior.
    for (const int count : {2, 3, 18, largest}) {
        for (const int start : bounds) {
            for (const int end : bounds) {
                for (const bool loop : {false, true}) {
                    art.hasAnimationStart = true; art.animationStart = start;
                    art.hasAnimationEnd = true; art.animationEnd = end;
                    art.hasAnimationLoopStart = loop; art.animationLoopStart = start;
                    art.hasAnimationLoopEnd = loop; art.animationLoopEnd = end;
                    for (const bool reflected : {false, true}) {
                        art.pingPong = reflected;
                        for (const int tick : ticks) {
                            ++checks;
                            const int frame = runtimeAuxiliaryAnimationFrame(&art, count, true, tick, 15);
                            if (frame < 0 || frame >= count) {
                                std::cerr << "invalid frame for authored bounds: " << frame << '\n';
                                passed = false;
                            }
                        }
                    }
                }
            }
        }
    }
    return passed;
}

} // namespace

int main() {
    const bool ranges = frameRanges();
    const bool cycles = reflectedCycles();
    const bool cadence = rateCadence();
    const bool boundaries = integerBoundaries();
    if (!ranges || !cycles || !cadence || !boundaries) return 1;
    std::cout << "Auxiliary animation contract: checks=" << checks
        << " ranges=exact loopEnd=exclusive pingPong=exact rate=preserved legalFrames=yes\n";
    return 0;
}
