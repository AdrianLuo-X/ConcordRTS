// Diagnostic-only use of the real production state, commands, fixed step,
// complete authority hash and complete state writers. No alternate simulation.
int runRuntimePathCandidateSmoke() try {
    const char* configured = std::getenv("CONCORD_PATH_CANDIDATE_TEST_OUTPUT");
    if (!configured || !*configured) throw std::runtime_error("private path candidate output required");
    const std::filesystem::path output(configured);
    if (!std::filesystem::create_directory(output))
        throw std::runtime_error("path candidate output must be new");
    std::string error;
    const bool formal = std::getenv("CONCORD_PATH_CANDIDATE_TEST_PARALLEL") != nullptr;
    const auto make = [&](int workers) {
        auto s = std::make_unique<RuntimeWindowState>();
        if (!configureRuntimeDetailSnapshotSmokeWorld(*s, false, error))
            throw std::runtime_error("path candidate live fixture: " + error);
        // The shared fixture was authored for serialization: its default -1
        // tiles are intentionally not a traversable movement map. This test
        // supplies valid ordinary ground tiles before exercising commands.
        for (auto& cell : s->world.accessTerrainCells()) {
            cell.tileIndex = 0; cell.subTileIndex = 0;
        }
        // Keep the three original hostile houses alive and the match active,
        // with an explicit movement-only rules input (no weapons). This is
        // fixture data, never a change to shipped rules or gameplay gates.
        IniFile movementRules;
        if (!movementRules.loadFromString(
            "[VehicleTypes]\n0=SNAPSHOT_TANK\n[BuildingTypes]\n0=DETAIL_FACTORY\n1=SNAPSHOT_LANDMARK\n"
            "[SNAPSHOT_TANK]\nStrength=200\nCost=100\nTechLevel=1\nSpeed=6\nSpeedType=Track\nMovementZone=Normal\n"
            "[DETAIL_FACTORY]\nStrength=1000\nCost=100\nTechLevel=1\nFactory=UnitType\nWeaponsFactory=yes\nFoundation=1x1\n"
            "[SNAPSHOT_LANDMARK]\nStrength=1000\n")
            || !s->rules.loadFromIni(movementRules))
            throw std::runtime_error("path candidate movement rules input invalid");
        s->capabilityCache = RuntimeCapabilityProfileCache{};
        initializeRuntimeNavigation(*s);
        // Real foundations force an owner queue search instead of admitting
        // only the separate short greedy path, which is outside this task.
        for (int y = 1; y < 15; ++y)
            if (!addRuntimeObjectInstanceForOwner(*s, "SkirmishPlayer1",
                    RuntimeBuildKind::Building, "DETAIL_FACTORY", {7, y}))
                throw std::runtime_error("path candidate foundation setup failed");
        s->config.defaultMapPath = "candidate-authority-fixture.map";
        // Internal alliance tables contain normalized teams, as in production setup.
        for (auto& [owner, team] : s->skirmishTeamByOwner) {
            (void)owner; team = runtimeNormalizeSkirmishTeam(team);
        }
        s->config.shortGame = false;
        s->config.crates = false;
        s->config.superweapons = false;
        s->config.computerAiEnabled = false;
        s->runtimeSoundsEnabled = false;
        s->runtimeSuppressPlayerAudioFeedback = true;
        s->runtimePreferencePersistenceEnabled = false;
        s->wrecks.clear(); s->buildingCollapses.clear();
        s->pathCandidateRunnerChecked = true;
        if (workers >= 0) s->pathCandidateRunner = std::make_unique<PathGoalCandidateRunner>(
            static_cast<std::size_t>(workers), true, concord::tasks::ExecutorConfig{},
            formal ? PathGoalCandidateMode::Parallel : PathGoalCandidateMode::Shadow);
        completeRuntimeWorldRefresh(*s);
        return s;
    };
    const auto whole = [&](const RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope scope(s);
        std::ostringstream bytes;
        if (!writeRuntimeStateToStream(s, bytes, error)
            || !writeRuntimeMultiplayerSnapshotExtension(s, bytes, error))
            throw std::runtime_error("path candidate whole-state writer: " + error);
        return bytes.str();
    };
    const auto authority = [&](const RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope scope(s);
        error.clear();
        const auto hash = runtimeMultiplayerAuthoritativeStateHash(s, error);
        if (!hash || !error.empty()) throw std::runtime_error("path candidate authority: " + error);
        return hash;
    };
    std::array<std::unique_ptr<RuntimeWindowState>, 5> states;
    for (std::size_t i = 0; i < states.size(); ++i)
        states[i] = make(i == 0 ? -1 : (i == 4 ? 4 : static_cast<int>(i) - 1));
    // Modes are reference, true-inline 0, worker 1, worker 2, worker 4.
    std::ofstream complete(output / "reference-full-state.txt", std::ios::binary);
    std::ofstream hashes(output / "per-tick-authority.txt", std::ios::binary);
    std::size_t pairs = 0;
    const auto initialCell = runtimeCellPointForObject(*findObjectById(*states[0], 41));
    bool everMoved = false;
    for (int offset = 0; offset <= 120; ++offset) {
        for (auto& owned : states) {
            auto& s = *owned;
            RuntimeSnapshotActiveStateScope scope(s);
            if (offset % 20 == 0) {
                s.selectedObjectIds = {41, 43};
                std::map<int, RuntimeCellPoint> accepted;
                issueRuntimeMoveCommand(s, offset % 40 == 0
                    ? RuntimeCellPoint{12, 11} : RuntimeCellPoint{2, 3}, &accepted, true);
                if (accepted.size() != 2) throw std::runtime_error(
                    "path candidate real move not accepted offset=" + std::to_string(offset)
                    + " accepted=" + std::to_string(accepted.size())
                    + " selected=" + std::to_string(s.selectedObjectIds.size())
                    + " rejected=" + std::to_string(s.lastCommand.rejectedCount)
                    + " status=" + s.simulationStatus);
            }
            if (offset) { ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); }
            if (s.skirmishMatchFinished) throw std::runtime_error("path candidate fixture ended before sample boundary");
        }
        everMoved = everMoved || runtimeCellPointForObject(*findObjectById(*states[0], 41)) != initialCell;
        const auto expectedHash = authority(*states[0]);
        const auto expectedBytes = whole(*states[0]);
        complete << "CHECKPOINT " << offset << " BYTES " << expectedBytes.size() << '\n' << expectedBytes;
        hashes << "tick=" << states[0]->tick << " full=" << expectedHash;
        for (std::size_t i = 1; i < states.size(); ++i) {
            const auto actualHash = authority(*states[i]);
            const auto actualBytes = whole(*states[i]);
            if (actualHash != expectedHash || actualBytes != expectedBytes) {
                std::ofstream failed(output / ("failure-mode-" + std::to_string(i) + ".ini"), std::ios::binary);
                failed << actualBytes;
                throw std::runtime_error("path candidate full authority/state mismatch tick="
                    + std::to_string(states[0]->tick) + " mode=" + std::to_string(i)
                    + " domains=" + runtimeMultiplayerSnapshotAuthorityDifferenceSummary(*states[0], *states[i]));
            }
            hashes << " mode" << i << '=' << actualHash;
            ++pairs;
        }
        hashes << '\n';
    }
    std::size_t compared = 0;
    for (std::size_t i = 1; i < states.size(); ++i) {
        const auto& stats = states[i]->pathCandidateRunner->stats();
        if (!(formal ? stats.adopted : stats.compared) || stats.mismatches || stats.unavailable)
            throw std::runtime_error("path candidate mode did not exercise all admitted jobs");
        compared += static_cast<std::size_t>(formal ? stats.adopted : stats.compared);
    }
    if (!everMoved)
        throw std::runtime_error("path candidate fixture never moved");
    complete.close(); hashes.close();
    if (!complete || !hashes) throw std::runtime_error("path candidate evidence write failed");
    std::cout << "Path candidate full authority passed ticks=121 pairedStates=" << pairs
        << " fullStateBytes=exact commands=7x2 perModeResults=original candidateComparisons=" << compared
        << " workers=reference,0,1,2,4\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "Path candidate authority failed: " << e.what() << '\n';
    return 1;
}