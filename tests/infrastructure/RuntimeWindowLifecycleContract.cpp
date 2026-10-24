#include "runtime/window/RuntimeWindowLifecycle.h"
#include <iostream>
#include <string>
#include <vector>

struct Port final : RuntimeWindowLifecyclePort {
    bool attached = true;
    std::vector<std::string> trace;
    void resetFrameMeter() override { trace.push_back("meter"); }
    void reportDeparture() override { trace.push_back("departure"); }
    void destroyWindow() override {
        trace.push_back("destroy-enter");
        handleRuntimeWindowLifecycle(RuntimeWindowLifecycleEvent::Destroyed, attached, *this);
        trace.push_back("destroy-return");
    }
    void stopEva() override { trace.push_back("eva"); }
    void stopMusic() override { trace.push_back("music"); }
    void releaseGraphics() override { trace.push_back("graphics"); }
    void postQuit() override { trace.push_back("quit"); }
};
int main() {
    int checks = 0;
    auto check = [&](bool ok) { ++checks; if (!ok) std::cerr << "failed " << checks << '\n'; return ok; };
    bool ok = true;
    for (const bool attached : {false, true}) {
        Port p; p.attached = attached;
        for (auto e : {RuntimeWindowLifecycleEvent::Hidden, RuntimeWindowLifecycleEvent::Minimized}) {
            p.trace.clear();
            ok &= check(!handleRuntimeWindowLifecycle(e, attached, p));
            ok &= check(p.trace == (attached ? std::vector<std::string>{"meter"} : std::vector<std::string>{}));
        }
        p.trace.clear();
        ok &= check(handleRuntimeWindowLifecycle(RuntimeWindowLifecycleEvent::Close, attached, p));
        const std::vector<std::string> expected = attached
            ? std::vector<std::string>{"departure", "destroy-enter", "eva", "music", "graphics", "quit", "destroy-return"}
            : std::vector<std::string>{"destroy-enter", "quit", "destroy-return"};
        ok &= check(p.trace == expected);
        p.trace.clear();
        ok &= check(handleRuntimeWindowLifecycle(RuntimeWindowLifecycleEvent::Destroyed, attached, p));
        ok &= check(p.trace == (attached ? std::vector<std::string>{"eva", "music", "graphics", "quit"}
            : std::vector<std::string>{"quit"}));
    }
    std::cout << "Window lifetime: " << checks << " checks; null-session, meter, nested close and teardown order\n";
    return ok ? 0 : 1;
}
