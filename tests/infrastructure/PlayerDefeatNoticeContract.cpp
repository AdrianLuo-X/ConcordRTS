#include "ui/input/RuntimePlayerDefeatNotice.h"

#if !defined(W001_LEGACY_PROBE) && (defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI))
#error Player defeat notice values must not transitively include windows.h
#endif

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#if !defined(W001_LEGACY_PROBE)
static_assert(std::is_same_v<decltype(RuntimePlayerDefeatNotice::untilMs), std::uint64_t>);
static_assert(std::is_same_v<std::remove_cv_t<decltype(runtimePlayerDefeatNoticeDurationMs)>, std::uint64_t>);
static_assert(std::is_unsigned_v<decltype(RuntimePlayerDefeatNotice::untilMs)>);
static_assert(std::numeric_limits<decltype(RuntimePlayerDefeatNotice::untilMs)>::digits == 64);
static_assert(std::is_same_v<decltype(runtimePlayerDefeatNoticeDeadline(std::uint64_t{})), std::uint64_t>);
static_assert(noexcept(runtimePlayerDefeatNoticeDeadline(std::uint64_t{})));
static_assert(runtimePlayerDefeatNoticeDeadline(0) == 15000);
static_assert(noexcept(swap(std::declval<RuntimePlayerDefeatNoticeState&>(),
    std::declval<RuntimePlayerDefeatNoticeState&>())));
#endif

namespace {
std::size_t checks = 0;
bool expect(bool condition, const char* message) {
    ++checks;
    if (!condition) std::cerr << "Player defeat notice contract failed: " << message << '\n';
    return condition;
}

// Measure actual member addresses: these owning types need not be standard-layout.
// The same source and profile can inspect the original header without offsetof.
template<class Object, class Member>
void emitMember(const char* name, const Object& object, const Member& member) {
    const auto start = reinterpret_cast<std::uintptr_t>(std::addressof(object));
    const auto field = reinterpret_cast<std::uintptr_t>(std::addressof(member));
    std::cout << "ABI " << name << ".offset=" << field - start
        << " size=" << sizeof(Member) << " align=" << alignof(Member) << '\n';
}

void emitAbi(const RuntimePlayerDefeatNotice& notice, const RuntimePlayerDefeatNoticeState& state) {
    std::cout << "ABI RuntimePlayerDefeatNotice.size=" << sizeof(notice)
        << " align=" << alignof(RuntimePlayerDefeatNotice) << '\n';
    emitMember("notice.owner", notice, notice.owner);
    emitMember("notice.untilMs", notice, notice.untilMs);
    std::cout << "ABI RuntimePlayerDefeatNoticeState.size=" << sizeof(state)
        << " align=" << alignof(RuntimePlayerDefeatNoticeState) << '\n';
    emitMember("state.lines", state, state.lines);
    emitMember("state.announcedOwners", state, state.announcedOwners);
    emitMember("state.suppressExisting", state, state.suppressExisting);
#if defined(_ITERATOR_DEBUG_LEVEL)
    std::cout << "ABI iterator=" << _ITERATOR_DEBUG_LEVEL << '\n';
#else
    std::cout << "ABI iterator=not-defined\n";
#endif
#if defined(_DEBUG)
    std::cout << "ABI debugCRTMacro=1\n";
#else
    std::cout << "ABI debugCRTMacro=0\n";
#endif
}

#if !defined(W001_LEGACY_PROBE)
bool testDeadlines() {
    bool ok = true;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    // Each expectation calls the production helper. No substitute deadline algorithm.
    ok &= expect(runtimePlayerDefeatNoticeDeadline(0) == 15000, "zero must expire at 15000ms");
    ok &= expect(runtimePlayerDefeatNoticeDeadline(1) == 15001, "one millisecond origin changed");
    ok &= expect(runtimePlayerDefeatNoticeDeadline(123456789) == 123471789,
        "ordinary millisecond duration changed");
    ok &= expect(runtimePlayerDefeatNoticeDeadline(4294967296ULL) == 4294982296ULL,
        "clock value above 32 bits was truncated");
    ok &= expect(runtimePlayerDefeatNoticeDeadline(maximum - 15001) == maximum - 1,
        "last nonsaturated deadline below maximum changed");
    ok &= expect(runtimePlayerDefeatNoticeDeadline(maximum - 15000) == maximum,
        "exact addition boundary must reach maximum");
    ok &= expect(runtimePlayerDefeatNoticeDeadline(maximum - 14999) == maximum,
        "first overflow origin must saturate");
    ok &= expect(runtimePlayerDefeatNoticeDeadline(maximum - 1) == maximum,
        "near-maximum origin must saturate");
    ok &= expect(runtimePlayerDefeatNoticeDeadline(maximum) == maximum,
        "maximum origin must retain maximum deadline");
    return ok;
}

bool testStorageSwap() {
    bool ok = true;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    RuntimePlayerDefeatNoticeState live;
    live.lines = {{"owner1", 15000}, {"owner2", 16000}};
    live.announcedOwners = {"owner1", "owner2", "expired-only"};
    RuntimePlayerDefeatNoticeState loaded;
    loaded.lines = {{"owner3", maximum}};
    loaded.announcedOwners = {"owner3"};
    loaded.suppressExisting = true;
    const auto* liveLines = live.lines.data();
    const auto* loadedLines = loaded.lines.data();
    const auto* liveOwnerNode = std::addressof(*live.announcedOwners.find("owner1"));
    const auto* loadedOwnerNode = std::addressof(*loaded.announcedOwners.find("owner3"));
    const auto expectedLiveOwners = live.announcedOwners;
    const auto expectedLoadedOwners = loaded.announcedOwners;
    swap(live, loaded);
    ok &= expect(live.lines.size() == 1 && live.lines[0].owner == "owner3"
        && live.lines[0].untilMs == maximum, "committed notice owner/deadline changed");
    ok &= expect(loaded.lines.size() == 2 && loaded.lines[0].owner == "owner1"
        && loaded.lines[0].untilMs == 15000 && loaded.lines[1].owner == "owner2"
        && loaded.lines[1].untilMs == 16000, "outgoing multiple-owner notice values changed");
    ok &= expect(live.lines.data() == loadedLines && loaded.lines.data() == liveLines,
        "swap must exchange vector storage directly");
    ok &= expect(live.announcedOwners == expectedLoadedOwners
        && loaded.announcedOwners == expectedLiveOwners,
        "swap must preserve all announced owners, including expired-only owners");
    const auto committedOwner = live.announcedOwners.find("owner3");
    const auto outgoingOwner = loaded.announcedOwners.find("owner1");
    ok &= expect(committedOwner != live.announcedOwners.end()
        && std::addressof(*committedOwner) == loadedOwnerNode
        && outgoingOwner != loaded.announcedOwners.end()
        && std::addressof(*outgoingOwner) == liveOwnerNode,
        "swap must exchange set nodes without rebuilding their storage");
    ok &= expect(live.suppressExisting && !loaded.suppressExisting,
        "load-suppression flag must follow its state");

    RuntimePlayerDefeatNoticeState scratch;
    scratch.announcedOwners = {"existing-defeat"};
    swap(live, scratch);
    ok &= expect(live.lines.empty() && live.announcedOwners.size() == 1
        && live.announcedOwners.contains("existing-defeat") && !live.suppressExisting,
        "empty loaded notice state must retain its prepared owner baseline");
    ok &= expect(scratch.lines.size() == 1 && scratch.lines.data() == loadedLines
        && scratch.lines[0].owner == "owner3" && scratch.suppressExisting,
        "empty-state commit must retain outgoing notice storage and suppression state");
    return ok;
}
#endif
} // namespace

int main() {
    bool ok = true;
    const RuntimePlayerDefeatNotice notice;
    const RuntimePlayerDefeatNoticeState state;
    ok &= expect(notice.owner.empty(), "default owner must be empty");
    ok &= expect(notice.untilMs == 0, "default deadline must be zero");
    ok &= expect(runtimePlayerDefeatNoticeDurationMs == 15000, "duration must remain 15000ms");
    ok &= expect(state.lines.empty(), "default notice list must be empty");
    ok &= expect(state.announcedOwners.empty(), "default announced-owner set must be empty");
    ok &= expect(!state.suppressExisting, "default load suppression must be disabled");
    emitAbi(notice, state);
    if (!ok) return 1;
    std::cout << "ABI defaults=preserved\n";
#if !defined(W001_LEGACY_PROBE)
    ok &= testDeadlines();
    ok &= testStorageSwap();
#endif
    if (!ok) return 1;
    std::cout << "Player defeat notice contract passed: checks=" << checks;
#if defined(W001_LEGACY_PROBE)
    std::cout << " legacyLayoutProbe=1\n";
#else
    std::cout << " deadlineCases=9 storageSwap=1 windowsHeader=absent\n";
#endif
    return 0;
}
