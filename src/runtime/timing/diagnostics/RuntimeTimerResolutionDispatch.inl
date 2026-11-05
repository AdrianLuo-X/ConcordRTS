if (const char* only = std::getenv("CONCORD_COMMAND_SMOKE_TIMER_VIDEO_SCOPE_ONLY"); only != nullptr) {
    if (std::string(only) != "1") { std::cerr << "Timer video ONLY must equal1\n"; return 1; }
    return runRuntimeTimerVideoScopeSmoke(config, std::cout) ? 0 : 1;
}
