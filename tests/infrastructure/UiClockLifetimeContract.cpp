#include "runtime/host/RuntimeUiClockWin32.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace {
// Models an early-registered global cache whose owned borrower is populated
// later at run time. It consults the real provider during its late destruction.
struct LateBorrower {
    const RuntimeUiClock* provider=nullptr;
    std::uint64_t before=0;
    ~LateBorrower() {
        if(!provider) {std::fputs("late clock borrower not armed\n",stderr);std::fflush(stderr);std::_Exit(3);}
        const auto now=provider->nowMs();
        if(now<before) {std::fputs("late clock epoch changed\n",stderr);std::fflush(stderr);std::_Exit(4);}
        std::fputs("UI clock late lifetime passed: real immutable provider read after main, static cache order preserved, no global fake clock\n",stdout);
        std::fflush(stdout);
    }
} lateBorrower;
void rejectPureCall() {
    std::fputs("UI clock late lifetime failed: provider destroyed before borrower\n",stderr);
    std::fflush(stderr);std::_Exit(17);
}
}
int main() {
    (void)_set_purecall_handler(&rejectPureCall);
    lateBorrower.provider=&runtimeWin32UiClock();
    lateBorrower.before=lateBorrower.provider->nowMs();
    if(lateBorrower.provider!=&runtimeWin32UiClock()) return 2;
    std::fputs("UI clock lifetime: main complete; late borrower result follows during real teardown\n",stdout);
    return 0;
}
