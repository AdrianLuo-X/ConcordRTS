int rankedPersistenceProcess(int argc, char** argv) {
    if (argc != 3) throw std::runtime_error("expected --persistence-export/restore/reject and a state path");
    const std::string mode = argv[1];
    const std::filesystem::path path = argv[2];
    if (mode == "--persistence-reject") {
        const std::string previous = contents(path);
        RuntimeInternetCommunityServiceConfig config;
        config.serviceId = "ranked-service"; config.statePath = path;
        config.nowMilliseconds = [] { return 1ULL; }; config.utcMilliseconds = [] { return 1700000001000ULL; };
        RuntimeInternetCommunityService service; std::string error;
        require(!service.open(config, error) && !error.empty() && contents(path) == previous,
            "damaged state must be rejected without replacing the original file");
        std::cout << "Ranked damaged state rejected: " << error << '\n'; return 0;
    }
    if (mode == "--persistence-export") {
        require(!std::filesystem::exists(path), "export fixture must be new");
        Fixture f(path);
        f.authorize("finished"); f.report(0, "finished", Result::Win); f.report(1, "finished", Result::Loss);
        f.authorize("assigned", false);
        f.authorize("missing"); f.report(0, "missing", Result::Win);
        f.authorize("conflict"); f.report(0, "conflict", Result::Win); f.report(1, "conflict", Result::Win);
        require(f.record("conflict").status == Status::ConflictingReports && f.service.stats().unresolvedQuickMatches == 3,
            "export must persist actual unresolved authority and reports");
        std::cout << "Ranked persistence export: confirmed, assigned, single report and conflict durably saved before abrupt process exit.\n" << std::flush;
        std::_Exit(0); // Deliberately skip service/client destruction.
    }
    if (mode == "--persistence-legacy") {
        Fixture f(path); f.refresh();
        require(f.service.stats().registeredAccounts == 3 && f.service.stats().authorizedQuickMatches == 0
            && f.clients[0].snapshot().profile.wins == 1 && f.clients[0].snapshot().profile.points == 1032
            && contents(path).starts_with("CONCORD_WOL_COMMUNITY_V4\n"), "legacy account migration preserves original score and writes complete state");
        std::cout << "Ranked legacy migration: original accounts and points retained without inventing match history.\n"; return 0;
    }
    if (mode == "--persistence-restore") {
        Fixture f(path);
        require(f.record("finished").status == Status::Confirmed && f.record("finished").winnerPeerId == "alice", "final state recovered");
        for (const auto& id : {"assigned", "missing", "conflict"})
            require(f.record(id).status == Status::VoidServiceRestart, "unresolved match must recover with explicit restart conclusion");
        require(f.record("missing").reports[0] == RuntimeInternetRankedResult::Win
            && f.record("missing").reports[1] == RuntimeInternetRankedResult::None, "single report evidence recovered");
        require(f.record("conflict").firstReports[0] == RuntimeInternetRankedResult::Win
            && f.record("conflict").firstReports[1] == RuntimeInternetRankedResult::Win
            && f.record("conflict").conflictObserved, "conflict evidence recovered");
        for (int attempt = 0; attempt < 3; ++attempt) {
            f.report(0, "finished", Result::Win); f.report(1, "finished", Result::Loss);
            f.report(1, "missing", Result::Loss); f.request(0, Operation::AbandonQuickMatch, "conflict");
        }
        f.refresh();
        require(f.clients[0].snapshot().profile.wins == 1 && f.clients[0].snapshot().profile.points == 1032
            && f.clients[1].snapshot().profile.losses == 1 && f.clients[1].snapshot().profile.points == 984
            && f.service.stats().unresolvedQuickMatches == 0 && f.service.stats().finalizedQuickMatches == 4,
            "fresh process must preserve ledger and scores without double settlement");
        std::cout << "Ranked persistence restore: all four records, original reports and final points retained; late/repeated submissions unchanged.\n";
        return 0;
    }
    throw std::runtime_error("unrecognized persistence process mode");
}
