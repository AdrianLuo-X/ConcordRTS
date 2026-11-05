#pragma once
#include <cstdlib>
#include <string_view>

// Test-owned materials only; default legacy cleanup remains available, while
// this continuous development explicitly supplies RETAIN=1 for every run.
inline bool runtimeMultiplayerSmokeRetainArtifacts() {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t count = 0;
    const int result = _dupenv_s(&value, &count, "CONCORD_MULTIPLAYER_MENU_SMOKE_RETAIN");
    const bool retain = result != 0 || (value && std::string_view(value) == "1");
    std::free(value);
    return retain;
#else
    const char* value = std::getenv("CONCORD_MULTIPLAYER_MENU_SMOKE_RETAIN");
    return value && std::string_view(value) == "1";
#endif
}
