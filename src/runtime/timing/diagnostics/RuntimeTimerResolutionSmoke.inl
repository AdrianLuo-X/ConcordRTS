// Native observations only. The standard lifetime/failure matrix is separate.
void writeRuntimeTimerResolutionDiagnostics(std::ostream& output, const char* context,
    const RuntimeWin32TimerResolutionScope& scope) {
    const auto& d = scope.diagnostics();
    output << "Timer resolution native scope: context=" << context
        << " period=" << d.periodMilliseconds << " beginCalls=" << d.beginCalls
        << " endCalls=" << d.endCalls << " beginResult=" << d.beginResult
        << " endResult=" << d.endResult << " began=" << scope.beginSucceeded()
        << " endPending=" << scope.endPending() << " globalFakeContext=0\n";
}

bool runRuntimeTimerVideoScopeSmoke(const RuntimeConfig& config, std::ostream& output) {
    // Same policy-before-timer order as the actual game session.
    RuntimeWin32ProcessPerformanceScope performance;
    RuntimeWin32TimerResolutionScope timer;
    std::string error;
    const bool video = runRuntimeCampaignVideoSkipSmoke(config, error);
    const bool ended = timer.close();
    writeRuntimeTimerResolutionDiagnostics(output, "original-four-real-Bink-playbacks", timer);
    const auto& d = timer.diagnostics();
    const bool success = video && ended && timer.beginSucceeded() && !timer.endPending()
        && d.periodMilliseconds == 1 && d.beginCalls == 1 && d.endCalls == 1
        && d.beginResult == TIMERR_NOERROR && d.endResult == TIMERR_NOERROR;
    output << "Timer video scope contract: passed=" << success << " failures=" << (success ? 0 : 1)
        << " skipped=0 originalVideoFixture=unchanged realPlaybacks=4 actualPolicyMask="
        << performance.appliedMask() << " error=" << error << '\n';
    return success;
}
