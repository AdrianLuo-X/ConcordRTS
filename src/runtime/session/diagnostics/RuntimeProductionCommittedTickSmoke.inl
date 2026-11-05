template<class Session>
bool runRuntimeProductionCommittedTickSmoke(
    std::string& error,
    bool (*draw)(Session&, std::ostream&, std::string&) = nullptr
) try {
    const char* configured = std::getenv("CONCORD_PRODUCTION_COMMITTED_TICK_OUTPUT");
    if (!configured || !*configured) {
        error = "committed tick requires an explicit retained output directory";
        return false;
    }
    const char* mapPath = std::getenv("CONCORD_PRODUCTION_COMMITTED_TICK_MAP");
    IniFile mapDocument;
    MapDatabase map;
    if (!mapPath || !*mapPath || !mapDocument.loadFromPath(std::filesystem::path(mapPath))
        || !map.loadFromIni(mapDocument)) {
        error = "committed tick requires the authored clear map fixture";
        return false;
    }
    const std::filesystem::path output(configured);
    std::filesystem::create_directories(output);
    if (!std::filesystem::is_empty(output)) {
        error = "committed tick output directory must be empty";
        return false;
    }
    int checks = 0;
    const auto require = [&](bool condition, const std::string& detail) {
        ++checks;
        if (!condition) {
            throw std::runtime_error("committed tick: " + detail);
        }
    };
    const auto make = [&]() {
        auto session = makeRuntimeRecoveryConsistencyFixture<Session>(error, true);
        require(static_cast<bool>(session), "real recovery fixture " + error);
        // Apply the real session's team normalization. The old fixture
        // stores "-" directly and would otherwise declare the seats allied.
        for (auto& [owner, team] : session->skirmishTeamByOwner) {
            (void)owner;
            team = runtimeNormalizeSkirmishTeam(team);
        }
        // Hydrate the clear terrain with the production loader so the same
        // world is valid for both navigation and the real map renderer.
        GameWorld world;
        const bool loaded = world.loadFromMap(map, &session->rules);
        require(loaded && world.getSummary().loaded && world.getTerrainCells().size() == 256,
                "production world loaded the authored clear terrain");
        world.accessObjects() = std::move(session->world.accessObjects());
        world.accessTerrainObjects() = std::move(session->world.accessTerrainObjects());
        world.accessSmudges() = std::move(session->world.accessSmudges());
        session->world = std::move(world);
        initializeRuntimeNavigation(*session);
        completeRuntimeWorldRefresh(*session);
        return session;
    };
    const auto write = [&](const char* name, const std::string& bytes) {
        std::ofstream stream(output / name, std::ios::binary);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        require(static_cast<bool>(stream), std::string("write ") + name);
    };
    const auto observe = [&](const Session& state) {
        std::string why;
        std::vector<std::pair<std::string, std::uint64_t>> domains;
        const auto hash = runtimeStandaloneSimulationStateHash(state, why, &domains);
        require(hash != 0U && !domains.empty() && why.empty(), "complete state hash " + why);
        const auto* actor = findObjectById(state, 41);
        require(actor != nullptr && actor->alive, "selected movement actor");
        std::ostringstream line;
        line << state.tick << '|' << hash << '|' << actor->x << '|' << actor->y << '|'
             << state.economy.credits << '|' << state.economy.localProductionLanes.nextSerial << '\n';
        for (const auto& [name, value] : domains) {
            line << name << '=' << value << '\n';
        }
        return line.str();
    };
    const auto compare = [&](const Session& state, const std::string& expected, const std::string& detail) {
        const std::string actual = observe(state);
        if (actual != expected) {
            std::cerr << "Expected authority\n" << expected << "Observed authority\n" << actual;
        }
        require(actual == expected, detail);
    };

    auto source = make();
    auto painted = draw ? make() : std::unique_ptr<Session>{};
    std::array<std::unique_ptr<Session>, 3> restored;
    const std::string peer = source->config.playerSlots.at(0).peerId;
    require(!peer.empty(), "authenticated existing human slot");
    const auto firstTick = source->tick;
    const auto initial = observe(*source);
    if (painted) {
        require(observe(*painted) == initial, "draw and no-draw enter the same state");
    }
    const auto* initialActor = findObjectById(*source, 41);
    const RuntimeCellPoint initialCell{initialActor->x, initialActor->y};

    const auto item = [&](int step, auto body) {
        RuntimeMultiplayerLockstepApplyCommand value;
        value.tick = firstTick + step + 1;
        value.peerId = peer;
        value.frameSequence = static_cast<std::uint64_t>(step + 1);
        value.commandIndex = 0;
        value.command.body = std::move(body);
        return value;
    };
    RuntimeMultiplayerGameplayStopCommand stale;
    stale.objectIds = {901};
    auto unknown = item(0, stale);
    unknown.peerId = "unknown-committed-tick-peer";
    const std::array wrongPeer{unknown};
    require(!prevalidateRuntimeMultiplayerGameplayBatch(*source, wrongPeer, error) && !error.empty(),
            "unknown peer rejected before application");
    require(observe(*source) == initial, "unknown peer left all authority intact");
    bool rejectedTick = false;
    try {
        advanceRuntimeCommittedTick(*source, firstTick + 2);
    } catch (const std::logic_error& ex) {
        rejectedTick = std::string_view(ex.what()) == "multiplayer committed tick is not the next runtime tick";
    }
    require(rejectedTick && observe(*source) == initial, "out-of-order tick leaves authority intact");

    RuntimeMultiplayerGameplayTargetOrderCommand move;
    move.objectIds = {41};
    move.target = RuntimeMultiplayerGameplayCellTarget{{7, 5}};
    RuntimeMultiplayerGameplayTargetOrderCommand futureMove = move;
    futureMove.target = RuntimeMultiplayerGameplayCellTarget{{11, 3}};
    RuntimeMultiplayerGameplayTargetOrderCommand afterLocalRestore = move;
    afterLocalRestore.target = RuntimeMultiplayerGameplayCellTarget{{8, 8}};
    constexpr int networkCheckpoint = 16;
    constexpr int restoreHorizon = 32;
    // Legacy local saves rebuild active paths. Exercise that unchanged format
    // after the first orders settle, and test active paths in replay and v15.
    constexpr int localCheckpoint = 768;
    constexpr int steps = localCheckpoint + restoreHorizon;
    const std::array commands{
        item(0, move),
        item(1, RuntimeMultiplayerGameplaySetPrimaryFactoryCommand{44}),
        item(2, RuntimeMultiplayerGameplaySetRallyPointCommand{44, {6, 9}}),
        item(3, RuntimeMultiplayerGameplayQueueProductionCommand{"SNAPSHOT_TANK"}),
        item(6, stale),
        item(21, futureMove),
        item(localCheckpoint + 4, afterLocalRestore),
    };
    const auto stepOnce = [&](Session& state, int step, const RuntimeMultiplayerLockstepApplyCommand* command) {
        const int previous = state.tick;
        require(previous == firstTick + step, "fixed tick continuity");
        const auto applied = state.multiplayerGameplayCommandsApplied;
        const auto noOps = state.multiplayerGameplayCommandsNoOp;
        const auto serial = state.economy.localProductionLanes.nextSerial;
        const std::span<const RuntimeMultiplayerLockstepApplyCommand> batch(command, command ? 1U : 0U);
        error.clear();
        const bool validated = prevalidateRuntimeMultiplayerGameplayBatch(state, batch, error);
        require(validated, "real command prevalidation at step " + std::to_string(step) + " " + error);
        RuntimeMassMovementRejectSink rejectedMovement;
        const bool appliedBatch = [&] {
            RuntimeMassMovementRejectScope rejectScope(
                &static_cast<RuntimeGameSessionState&>(state), previous,
                &RuntimeMassMovementRejectSink::receive, &rejectedMovement);
            return applyRuntimeMultiplayerGameplayBatch(state, batch, error);
        }();
        require(appliedBatch, "real command application at step " + std::to_string(step) + " " + error);
        if (command) {
            const bool countersMatch = state.multiplayerGameplayCommandsApplied == applied + (step == 6 ? 0 : 1)
                && state.multiplayerGameplayCommandsNoOp == noOps + (step == 6 ? 1 : 0);
            if (!countersMatch) {
                std::cerr << "Committed command trace: applied=" << state.multiplayerGameplayCommandsApplied - applied
                          << " no-op=" << state.multiplayerGameplayCommandsNoOp - noOps << '\n';
                for (std::size_t i = 0; i < rejectedMovement.count; ++i) {
                    const auto& rejection = rejectedMovement.records[i];
                    std::cerr << "Committed movement rejection: " << rejection.reason.bytes.data()
                              << ' ' << rejection.detail.bytes.data() << '\n';
                }
            }
            require(countersMatch, "actual applied/no-op accounting at step " + std::to_string(step));
        }
        if (step == 1) {
            require(std::any_of(state.playerCommands.primaryFactories.begin(),
                                state.playerCommands.primaryFactories.end(),
                                [](const auto& entry) { return entry.second == 44; }),
                    "real primary factory selection");
        } else if (step == 2) {
            const auto rally = state.playerCommands.rallyPoints.find(44);
            require(rally != state.playerCommands.rallyPoints.end()
                    && rally->second.cell == RuntimeMultiplayerGameplayCell{6, 9},
                    "real rally point");
        } else if (step == 3) {
            require(state.economy.localProductionLanes.nextSerial == serial + 1,
                    "real production order allocation");
        }
        require(state.tick == previous, "commands apply before the completed tick");
        advanceRuntimeCommittedTick(state, previous + 1);
        completeRuntimeWorldRefresh(state);
        require(state.tick == previous + 1, "same real committed simulation advances exactly once");
        require(!state.skirmishMatchFinished, "the command fixture remains a running match");
        require(state.superWeapons.lastTick() == state.tick, "support power time follows the production tick");
    };

    std::ostringstream tape;
    std::ostringstream drawWitness;
    int comparedRestoredTicks = 0;
    int paintedTicks = 0;
    bool moved = false;
    for (int step = 0; step < steps; ++step) {
        const auto next = std::find_if(commands.begin(), commands.end(), [&](const auto& value) {
            return value.tick == firstTick + step + 1;
        });
        const auto* command = next == commands.end() ? nullptr : &*next;
        stepOnce(*source, step, command);
        const auto expected = observe(*source);
        tape << expected;
        const auto* actor = findObjectById(*source, 41);
        moved = moved || actor->x != initialCell.x || actor->y != initialCell.y;
        if (painted) {
            stepOnce(*painted, step, command);
            if (step < networkCheckpoint + restoreHorizon && step % 4 == 0) {
                const bool drew = draw(*painted, drawWitness, error);
                require(drew, "real draw " + error);
                ++paintedTicks;
            }
            require(observe(*painted) == expected, "draw frequency preserves all authority domains");
        }
        if (step + 1 == networkCheckpoint) {
            require(source->behaviors.contains(41), "active movement at the replay and v15 checkpoint");
            std::string replay;
            const bool wroteReplay = writeRuntimeMatchReplayWorld(*source, replay, error);
            require(wroteReplay, "full replay checkpoint " + error);
            write("committed-replay.ini", replay);
            const auto replayConfig = serializeRuntimeMatchReplayConfig(source->config);
            const auto replayHash = runtimeMatchReplayStateHash(*source, error);
            auto identity = runtimeMultiplayerSnapshotSmokeIdentity(
                *source, 31, "committed-tick-recovery", RuntimeMultiplayerSnapshotKind::Load);
            RuntimeMultiplayerSnapshotMemoryImage image;
            const bool wroteV15 = exportRuntimeMultiplayerSnapshotV15(*source, identity, image, error);
            require(wroteV15, "full v15 checkpoint " + error);
            write("committed-v15.ini", {image.bytes.begin(), image.bytes.end()});
            restored[1] = make();
            restored[2] = make();
            const bool loadedReplay = restoreRuntimeMatchReplayWorld(
                *restored[1], replayConfig, replay, source->tick, replayHash, error);
            require(loadedReplay, "real replay restore " + error);
            compare(*restored[1], expected, "complete authority immediately after replay restore");
            auto& network = *restored[2];
            // A live peer reaches the v15 barrier on its own fixed ticks.
            // Without the source commands, it has different authority.
            for (int peerStep = 0; peerStep < networkCheckpoint; ++peerStep) {
                advanceRuntimeCommittedTick(network, firstTick + peerStep + 1);
                completeRuntimeWorldRefresh(network);
            }
            const std::string beforeStage = observe(network);
            require(network.tick == source->tick && beforeStage != expected,
                    "live peer reaches the same v15 barrier with different authority");
            RuntimeMultiplayerSnapshotStagedState staged;
            const bool stagedV15 = stageRuntimeMultiplayerSnapshot(
                network, image.bytes, image.descriptor, staged, error);
            require(stagedV15, "real staged v15 restore " + error);
            compare(network, beforeStage, "staging preserves live authority before commit");
            commitRuntimeMultiplayerSnapshotAuthority(network, std::move(staged));
            compare(network, expected, "complete authority immediately after v15 commit");
        } else if (step + 1 > networkCheckpoint && step + 1 <= networkCheckpoint + restoreHorizon) {
            for (std::size_t route = 1; route < restored.size(); ++route) {
                stepOnce(*restored[route], step, command);
                compare(*restored[route], expected,
                        "restored simulation continues with equal authority route " + std::to_string(route));
                ++comparedRestoredTicks;
            }
        }
        if (step + 1 == localCheckpoint) {
            require(source->pendingPathObjectIds.empty(), "legacy local checkpoint has no queued path work");
            require(std::none_of(source->behaviors.begin(), source->behaviors.end(), [](const auto& entry) {
                const auto& behavior = entry.second;
                return behavior.kind == RuntimeBehaviorKind::Move && behavior.state != RuntimeBehaviorState::Complete
                    && behavior.state != RuntimeBehaviorState::Idle;
            }), "legacy local checkpoint follows completed movement");
            std::ostringstream ordinary;
            const bool wroteLocal = writeRuntimeStateToStream(*source, ordinary, error);
            require(wroteLocal, "full local checkpoint " + error);
            write("committed-v14.ini", ordinary.str());
            restored[0] = make();
            const bool loadedLocal = loadRuntimeState(*restored[0], pathString(output / "committed-v14.ini"), error);
            require(loadedLocal, "real local restore " + error);
            if (observe(*restored[0]) != expected) {
                const auto domains = runtimeMultiplayerSnapshotAuthorityDifferenceSummary(*source, *restored[0]);
                std::cerr << "Local restore difference from source: " << domains << '\n';
                std::string restoredDocument;
                if (writeRuntimeMatchReplayWorld(*restored[0], restoredDocument, error)) {
                    write("local-restored-difference.ini", restoredDocument);
                }
            }
            compare(*restored[0], expected, "complete authority immediately after local restore");
        } else if (step + 1 > localCheckpoint) {
            stepOnce(*restored[0], step, command);
            compare(*restored[0], expected, "local restored simulation continues with equal authority");
            ++comparedRestoredTicks;
        }
    }
    require(moved && comparedRestoredTicks == 96, "actual world movement and 96 restored future observations");
    write("authority-tape.txt", tape.str());
    if (painted) {
        require(paintedTicks == 12, "12 actual software draws");
        write("draw-witness.txt", drawWitness.str());
    }
    std::ostringstream result;
    result << "Production committed tick: ticks=" << steps
           << " commandFrames=" << commands.size() << " restoredFuture=" << comparedRestoredTicks
           << " draws=" << paintedTicks << " checks=" << checks << '\n';
    write("result.txt", result.str());
    std::cout << result.str();
    error.clear();
    return true;
} catch (const std::exception& exception) {
    error = exception.what();
    return false;
}
