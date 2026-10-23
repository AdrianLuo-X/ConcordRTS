// Real host recovery entries over the same nonempty, deterministic fixture.
// No socket, window, device, wall clock or alternate simulation is involved.
#include "runtime/replay/RuntimeMatchReplayState.inl"

bool runRuntimeRecoveryConsistencySmoke(std::string& error) try {
    const char* configured = std::getenv("CONCORD_RECOVERY_CONTRACT_OUTPUT");
    if (!configured || !*configured) { error = "recovery contract requires private output"; return false; }
    const std::filesystem::path parent(configured);
    std::filesystem::create_directories(parent);
    std::filesystem::path output;
    for (int suffix=0;suffix<10000;++suffix) {
        auto candidate=parent/("run-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(suffix));
        if (std::filesystem::create_directory(candidate)) { output=std::move(candidate); break; }
    }
    if (output.empty()) { error="recovery evidence directory exhausted"; return false; }
    int checks = 0, pairedTicks = 0, rejections = 0, legacyLoads = 0;
    const auto need = [&](bool value, const std::string& why) {
        ++checks; if (!value) error = "recovery consistency: " + why; return value;
    };
    const auto write = [&](const std::string& name, const std::string& bytes) {
        std::ofstream file(output / name, std::ios::binary); file << bytes; file.close();
        return need(static_cast<bool>(file), "evidence write " + name);
    };
    const auto make = [&]() {
        auto result = std::make_unique<RuntimeWindowState>();
        auto& s = *result;
        if (!configureRuntimeDetailSnapshotSmokeWorld(s, false, error)) return std::unique_ptr<RuntimeWindowState>{};
        IniFile rules;
        if (!rules.loadFromString(R"RECOVERY(
[General]
BuildSpeed=0.1
[CombatDamage]
C4Delay=0.01
C4Warhead=RECOVERY_C4_WH
[VehicleTypes]
0=SNAPSHOT_TANK
[InfantryTypes]
0=RECOVERY_C4
[BuildingTypes]
0=DETAIL_FACTORY
1=SNAPSHOT_LANDMARK
[SNAPSHOT_LANDMARK]
Strength=1000
DestroyAnim=SNAPSHOT_COLLAPSE
LeaveRubble=yes
[SNAPSHOT_TANK]
Strength=200
Cost=100
TechLevel=1
Speed=6
SpeedType=Track
MovementZone=Normal
[DETAIL_FACTORY]
Strength=1000
Cost=100
TechLevel=1
Factory=UnitType
WeaponsFactory=yes
Foundation=1x1
[RECOVERY_C4]
Strength=100
Cost=100
C4=yes
Speed=4
[RECOVERY_C4_WH]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
)RECOVERY") || !s.rules.loadFromIni(rules)) return std::unique_ptr<RuntimeWindowState>{};
        s.config.defaultMapPath = "recovery-contract.map";
        s.config.shortGame = false; s.config.crates = false; s.config.superweapons = false;
        s.config.computerAiEnabled = false;
        // One human is also the original legacy snapshot compatibility boundary.
        s.config.playerSlots[1].human = false;
        s.humanHouseAuthorities.resize(1);
        s.economy=makeRuntimeMultiplayerSnapshotSmokeEconomy(s,"SkirmishPlayer1",9000);
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.wrecks.clear(); s.buildingCollapses.clear();
        s.world.accessObjects().erase(std::remove_if(s.world.accessObjects().begin(),
            s.world.accessObjects().end(), [](const auto& o) { return !o.alive; }), s.world.accessObjects().end());
        s.computerMiningByOwner["skirmishplayer3"] = {12, 450, 3, 7, 2, 4};
        s.economy.storedResourceUnits = 8; s.economy.storedResourceValue = 300;
        s.economy.storedResourceGemUnits = 2; s.economy.resourceTrips = 5;
        s.economy.skirmishStartApplied = true;
        auto* source = addRuntimeObjectInstanceForOwner(s, "SkirmishPlayer1",
            RuntimeBuildKind::Infantry, "RECOVERY_C4", {7, 12});
        if (!source) return std::unique_ptr<RuntimeWindowState>{};
        // Production/load use the tile centre for idle continuous coordinates.
        clearRuntimeRenderPosition(*source);
        // Local saves initialize this empty subsystem when materializing it.
        ensureRuntimeRepairDepotsInitialized(s);
        const int sourceId = source->instanceId;
        ensureRuntimeSpecialMechanismsInitialized(s);
        if (!s.specialMechanisms.tryPlantC4(s.world.getObjects(), s.rules, sourceId, 47,
                s.tick, s.config.fixedTickHz, SpecialEventSink{}, error)) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeNavigation(s); completeRuntimeWorldRefresh(s);
        // Enter production through the actual stage; never invent partially
        // paid counters that the original load validator correctly rejects.
        ++s.tick; advanceRuntimeSimulation(s,false); completeRuntimeWorldRefresh(s);
        // The stock snapshot fixture supplies empty subsystem clocks. Advance
        // those to this checkpoint after warming the real production stage.
        // Keep the separately recorded original SuperWeapon restore failure.
        if (!s.superWeapons.alignEmptyStateTick(s.tick)
            || !s.particles.alignEmptyStateTick(s.tick)) return std::unique_ptr<RuntimeWindowState>{};
        return result;
    };
    const auto authority = [&](const RuntimeWindowState& s) {
        std::string why; std::vector<std::pair<std::string,std::uint64_t>> domains;
        const auto full = runtimeStandaloneSimulationStateHash(s, why, &domains);
        if (!full || !why.empty()) throw std::runtime_error("full authority: " + why);
        std::ostringstream out; out << "tick=" << s.tick << "\nfull=" << full << '\n';
        for (const auto& [name, hash] : domains) out << name << '=' << hash << '\n';
        out << "match-finished=" << s.skirmishMatchFinished << '\n';
        writeRuntimeMatchScore(s, out); writeRuntimeComputerMining(out, s);
        out << "credits=" << s.economy.credits << "\nproduction="
            << runtimeSerializeProductionLanes(s.economy.localProductionLanes) << '\n';
        s.factoryDeliveries.save(out); s.specialMechanisms.save(out);
        std::vector<std::string> aiOwners;
        for (const auto& [owner, ai] : s.computerAiByOwner) {
            (void)ai; aiOwners.push_back(owner);
        }
        std::sort(aiOwners.begin(), aiOwners.end());
        for (const auto& owner : aiOwners) {
            const auto& ai = s.computerAiByOwner.at(owner);
            out << "ai=" << owner << '|' << ai.credits << '|' << ai.nextThinkTick << '|'
                << ai.nextIncomeTick << '|' << ai.nextBuildTick << '|' << ai.nextAttackTick << '\n';
        }
        // The wire canonicalizes object records. Locate every object by ID
        // without imposing its storage order on authority or simulation.
        std::vector<const WorldObjectInstance*> objects;
        for (const auto& object : s.world.getObjects()) objects.push_back(&object);
        std::sort(objects.begin(), objects.end(), [](const auto* left, const auto* right) {
            return left->instanceId < right->instanceId;
        });
        for (const auto* object : objects) {
            const auto& o = *object;
            out << "object=" << o.instanceId << '|' << o.owner << '|' << o.type << '|'
                << o.alive << '|' << o.health << '|' << o.x << '|' << o.y << '\n';
        }
        return out.str();
    };
    const auto whole = [&](const RuntimeWindowState& s) {
        std::ostringstream out; std::string why;
        if (!writeRuntimeStateToStream(s, out, why) || !writeRuntimeMultiplayerSnapshotExtension(s, out, why))
            throw std::runtime_error("whole live state: " + why);
        out << "\nLOCAL\n" << s.camera.worldOffsetX << '|' << s.camera.worldOffsetY << '|'
            << s.runtimeSoundEvents << '|' << s.runtimeEvaEvents << '|' << s.nextReplayEventSequence
            << '|' << s.recentReplayEvents.size() << '|' << s.interactionStatus << '|' << s.simulationStatus << '\n';
        for (int id : s.selectedObjectIds) out << "selected=" << id << '\n';
        return authority(s) + out.str();
    };
    const auto replace = [&](std::string bytes, const std::string& section,
                             const std::string& key, const std::string& value) {
        std::vector<std::uint8_t> changed;
        if (!runtimeMultiplayerSnapshotSmokeReplaceIniValue({bytes.begin(),bytes.end()},section,key,value,changed))
            throw std::runtime_error("missing mutation field " + section + "/" + key);
        return std::string(changed.begin(),changed.end());
    };
    const auto remove = [](std::string bytes, const std::string& section) {
        const auto begin = bytes.find("[" + section + "]\n");
        if (begin == std::string::npos) throw std::runtime_error("missing removal section " + section);
        const auto end = bytes.find("\n[",begin+1);
        bytes.erase(begin,end == std::string::npos ? bytes.size()-begin : end+1-begin);
        return bytes;
    };
    auto source = make(); if (!need(static_cast<bool>(source), "nonempty fixture: " + error)) return false;
    if (!need(!source->matchScores.empty() && !source->computerMiningByOwner.empty()
        && !source->specialMechanisms.timedBombs().empty(), "nonempty migrated domains")) return false;
    const auto expected = authority(*source);
    if (!write("initial-reference.txt",expected)) return false;
    std::ostringstream ordinary; if (!writeRuntimeStateToStream(*source,ordinary,error)) return false;
    const std::string save = ordinary.str();
    std::string replayState; if (!writeRuntimeMatchReplayWorld(*source,replayState,error)) return false;
    const auto configText = serializeRuntimeMatchReplayConfig(source->config);
    const auto initialHash = runtimeMatchReplayStateHash(*source,error);
    RuntimeMultiplayerSnapshotMemoryImage image;
    auto identity = runtimeMultiplayerSnapshotSmokeIdentity(*source,27,"s27-recovery",RuntimeMultiplayerSnapshotKind::Load);
    if (!exportRuntimeMultiplayerSnapshotV15(*source,identity,image,error)) return false;
    if (!write("seed-save-v14.ini",save) || !write("seed-replay-state.ini",replayState)
        || !write("seed-network-v15.ini",{image.bytes.begin(),image.bytes.end()})) return false;
    std::ostringstream recording(std::ios::binary); RuntimeMatchReplayWriter writer;
    RuntimeMatchReplayHeader header; header.initialTick = source->tick;
    header.fixedTickHz = source->config.fixedTickHz; header.compatibility = 27;
    header.initialHash = initialHash; header.config = configText; header.initialState = replayState;
    if (!writer.begin(recording,header,error)) return false;
    RuntimeMatchReplayRecord reset; reset.kind = RuntimeMatchReplayRecordKind::Reset;
    reset.tick = source->tick; reset.stateHash = initialHash; reset.config = configText; reset.state = replayState;
    if (!writer.append(reset,error)) return false;
    auto finish = reset; finish.kind = RuntimeMatchReplayRecordKind::Finish; finish.config.clear(); finish.state.clear();
    if (!writer.append(finish,error) || !write("seed-format-v1.creplay",recording.str())) return false;
    std::istringstream replay(recording.str(),std::ios::binary); RuntimeMatchReplayReader reader;
    RuntimeMatchReplayHeader decoded; RuntimeMatchReplayRecord decodedReset;
    if (!reader.begin(replay,decoded,error) || !reader.next(decodedReset,error)
        || !need(decodedReset == reset, "real replay Reset envelope")) return false;
    std::array<std::unique_ptr<RuntimeWindowState>,3> restored;
    for (auto& state : restored) { state=make(); if (!state) return false; state->matchScores.clear(); state->computerMiningByOwner.clear(); }
    if (!loadRuntimeState(*restored[0],pathString(output/"seed-save-v14.ini"),error)) {
        error="local initial recovery: "+error; return false;
    }
    if (!restoreRuntimeMatchReplayWorld(*restored[1],decodedReset.config,decodedReset.state,
            decodedReset.tick,decodedReset.stateHash,error)) return false;
    RuntimeMultiplayerSnapshotStagedState staged;
    if (!stageRuntimeMultiplayerSnapshot(*restored[2],image.bytes,image.descriptor,staged,error)) return false;
    restored[2]->camera.worldOffsetX=727; restored[2]->selectedObjectIds={41};
    commitRuntimeMultiplayerSnapshotAuthority(*restored[2],std::move(staged));
    if (!need(restored[2]->camera.worldOffsetX==727 && restored[2]->selectedObjectIds==std::vector<int>{41},
            "network authority commit retains local presentation")) return false;
    for (int route=0;route<3;++route) {
        const auto actual=authority(*restored[route]); write("initial-route-"+std::to_string(route)+".txt",actual);
        std::string stateBytes, diagnosticError;
        if (!writeRuntimeMatchReplayWorld(*restored[route],stateBytes,diagnosticError)
            || !write("initial-route-"+std::to_string(route)+"-state.ini",stateBytes)) return false;
        std::ostringstream difference;
        difference << runtimeMultiplayerSnapshotAuthorityDifferenceSummary(*source,*restored[route])
            << "\nworld-reference=" << source->world.deterministicSimulationStateHash()
            << "\nworld-actual=" << restored[route]->world.deterministicSimulationStateHash() << '\n';
        if (!write("initial-route-"+std::to_string(route)+"-difference.txt",difference.str())) return false;
        if (!need(actual==expected,"initial route "+std::to_string(route))) return false;
    }
    // Same commands and fixed-tick calls, including an active production lane
    // and a real C4 deadline. All full hashes and locating fields are retained.
    std::array<std::ostringstream,4> traces;
    for (int tick=0;tick<96;++tick) {
        std::array<RuntimeWindowState*,4> states{source.get(),restored[0].get(),restored[1].get(),restored[2].get()};
        std::string reference;
        for (std::size_t route=0;route<states.size();++route) {
            auto& s=*states[route];
            if (tick==0 || tick==48) { s.selectedObjectIds={41}; issueRuntimeStopCommand(s); }
            ++s.tick; advanceRuntimeSimulation(s,tick%13==0); completeRuntimeWorldRefresh(s);
            const auto current=authority(s); traces[route]<<current<<'\n';
            if (route==0) reference=current;
            else if (!need(current==reference,"continued authority route "+std::to_string(route)+" tick "+std::to_string(s.tick))) {
                write("failed-continuation-reference.txt",reference);
                write("failed-continuation-actual.txt",current); return false;
            }
            ++pairedTicks;
        }
    }
    for (int route=0;route<4;++route) if (!write("continuation-"+std::to_string(route)+".txt",traces[route].str())) return false;
    const auto* bombTarget=findObjectById(*source,47);
    if (!need(source->specialMechanisms.timedBombs().empty() && (!bombTarget || !bombTarget->alive),
            "C4 deadline actually executed")) return false;
    auto live=make(); if (!live) return false;
    live->camera.worldOffsetX=827; live->selectedObjectIds={41}; live->runtimeSoundEvents=17;
    const auto untouched=whole(*live); const auto* storage=live->world.getObjects().data();
    const auto unchanged = [&]() { return whole(*live)==untouched && live->world.getObjects().data()==storage; };
    std::vector<std::pair<std::string,std::string>> invalid;
    invalid.emplace_back("truncated",save.substr(0,save.size()/2));
    invalid.emplace_back("outer-version",replace(save,"RuntimeSave","Version","999"));
    invalid.emplace_back("score-version",replace(save,"RuntimeMatchScore","Schema","999"));
    invalid.emplace_back("mining-field",replace(save,"ComputerMining","Count","-1"));
    invalid.emplace_back("production-checksum",replace(save,"ProductionLanes","StateHash","0"));
    invalid.emplace_back("timed-bomb-field",replace(save,"SpecialTimedBombs","Count","-1"));
    invalid.emplace_back("missing-modern-section",remove(save,"SpecialTimedBombs"));
    for (const auto& [name,bytes] : invalid) {
        const auto path=output/("invalid-"+name+".ini"); if (!write(path.filename().string(),bytes)) return false;
        std::string why;
        if (!need(!loadRuntimeState(*live,pathString(path),why) && !why.empty() && unchanged(),"local rejection "+name)) return false;
        ++rejections;
        // Preserve replay-specific exact sections while corrupting the same domain.
        std::string replayBad=bytes;
        const auto extra=replayState.find("[MultiplayerObjectPoses]");
        if (extra!=std::string::npos) replayBad+='\n'+replayState.substr(extra);
        if (!need(!restoreRuntimeMatchReplayWorld(*live,configText,replayBad,reset.tick,initialHash,why)
                && !why.empty() && unchanged(),"replay rejection "+name)) return false;
        ++rejections;
    }
    if (!need(!restoreRuntimeMatchReplayWorld(*live,configText,replayState,reset.tick,initialHash^1ULL,error)
            && unchanged(),"replay authoritative checksum")) return false;
    ++rejections;
    if (!need(!restoreRuntimeMatchReplayWorld(*live,configText,replayState,reset.tick,0,error)
            && unchanged(),"replay zero checkpoint")) return false;
    ++rejections;
    if (!need(!restoreRuntimeMatchReplayWorld(*live,configText,replayState,-1,initialHash,error)
            && unchanged(),"replay negative checkpoint tick")) return false;
    ++rejections;
    auto wrongConfig=source->config; wrongConfig.defaultMapPath="other.map";
    if (!need(!restoreRuntimeMatchReplayWorld(*live,serializeRuntimeMatchReplayConfig(wrongConfig),replayState,
            reset.tick,initialHash,error) && unchanged(),"replay map context")) return false;
    ++rejections;
    const std::string network(image.bytes.begin(),image.bytes.end());
    std::vector<std::pair<std::string,std::string>> networkInvalid;
    networkInvalid.emplace_back("version",replace(network,"RuntimeSave","Version","999"));
    networkInvalid.emplace_back("score",replace(network,"RuntimeMatchScore","Schema","999"));
    networkInvalid.emplace_back("mining",replace(network,"ComputerMining","Count","-1"));
    networkInvalid.emplace_back("production",replace(network,"MultiplayerHouse0","ProductionDataHex","zz"));
    networkInvalid.emplace_back("bomb",replace(network,"SpecialTimedBombs","Count","-1"));
    networkInvalid.emplace_back("missing-modern",remove(network,"SpecialTimedBombs"));
    networkInvalid.emplace_back("truncated",network.substr(0,network.size()/2));
    RuntimeMultiplayerSnapshotStagedState sentinel;
    sentinel.scratch=make(); const auto* sentinelPointer=sentinel.scratch.get();
    for (const auto& [name,bytes] : networkInvalid) {
        std::vector<std::uint8_t> candidate(bytes.begin(),bytes.end()); RuntimeMultiplayerSnapshotDescriptor descriptor;
        if (!makeRuntimeMultiplayerSnapshotDescriptor(identity,candidate,descriptor,error)) return false;
        std::string why;
        if (!need(!stageRuntimeMultiplayerSnapshot(*live,candidate,descriptor,sentinel,why)
                && !why.empty() && unchanged() && sentinel.scratch.get()==sentinelPointer,"network inner rejection "+name)) return false;
        ++rejections;
    }
    auto damaged=image.bytes; damaged[damaged.size()/2]^=1;
    if (!need(!stageRuntimeMultiplayerSnapshot(*live,damaged,image.descriptor,sentinel,error)
            && unchanged() && sentinel.scratch.get()==sentinelPointer,"network digest rejection")) return false;
    ++rejections;
    // Missing optional old domain sections are accepted with their original
    // clear-to-default semantics. Current replay/network hashes still bind them.
    const auto legacy=remove(remove(save,"RuntimeMatchScore"),"ComputerMining");
    if (!write("legacy-missing-optional.ini",legacy)) return false;
    auto legacyState=make(); if (!legacyState) return false;
    if (!need(loadRuntimeState(*legacyState,pathString(output/"legacy-missing-optional.ini"),error)
            && legacyState->matchScores.empty() && legacyState->computerMiningByOwner.empty(),"legacy optional defaults")) return false;
    ++legacyLoads;
    auto legacyExpected=make(); auto legacyReplay=make(); auto legacyNetwork=make();
    if (!legacyExpected || !legacyReplay || !legacyNetwork) return false;
    legacyExpected->matchScores.clear(); legacyExpected->computerMiningByOwner.clear();
    const auto legacyHash=runtimeMatchReplayStateHash(*legacyExpected,error);
    const auto legacyReplayBytes=remove(remove(replayState,"RuntimeMatchScore"),"ComputerMining");
    if (!need(restoreRuntimeMatchReplayWorld(*legacyReplay,configText,legacyReplayBytes,reset.tick,legacyHash,error)
            && authority(*legacyReplay)==authority(*legacyExpected),"legacy replay optional defaults")) return false;
    ++legacyLoads;
    auto legacyNetworkText=remove(remove(network,"RuntimeMatchScore"),"ComputerMining");
    legacyNetworkText=replace(legacyNetworkText,"MultiplayerSnapshot","AuthoritativeStateHash",std::to_string(legacyHash));
    const std::vector<std::uint8_t> legacyNetworkBytes(legacyNetworkText.begin(),legacyNetworkText.end());
    auto legacyIdentity=identity; legacyIdentity.authoritativeStateHash=legacyHash;
    RuntimeMultiplayerSnapshotDescriptor legacyDescriptor;
    if (!makeRuntimeMultiplayerSnapshotDescriptor(legacyIdentity,legacyNetworkBytes,legacyDescriptor,error)
        || !stageRuntimeMultiplayerSnapshot(*legacyNetwork,legacyNetworkBytes,legacyDescriptor,staged,error)) return false;
    commitRuntimeMultiplayerSnapshotAuthority(*legacyNetwork,std::move(staged));
    if (!need(authority(*legacyNetwork)==authority(*legacyExpected)
            && authority(*legacyState)==authority(*legacyExpected),"all three old-domain default authorities")) return false;
    ++legacyLoads;
    if (!need(!restoreRuntimeMatchReplayWorld(*live,configText,legacyReplayBytes,reset.tick,initialHash,error)
            && unchanged(),"missing optional replay domain cannot bypass current checksum")) return false;
    ++rejections;
    if (!need(unchanged(),"all rejects preserve whole live state and storage")) return false;
    std::ostringstream summary;
    summary<<"checks="<<checks<<"\nfixed-tick-observations="<<pairedTicks<<"\ntransactional-rejections="<<rejections
        <<"\nlegacy-default-loads="<<legacyLoads<<"\nouter-save=14\nouter-snapshot=15\nreplay-format=1\n"
        <<"replay-recorder-viewer=not-integrated\nfull-application-L1-L4=not-run\n";
    if (!write("result.txt",summary.str())) return false;
    error.clear(); std::cout<<"Recovery consistency passed: "<<checks<<" checks, "<<pairedTicks
        <<" fixed-tick observations, "<<rejections<<" unchanged-live rejections, "<<legacyLoads<<" old-domain default load\n";
    return true;
} catch (const std::exception& exception) { error="recovery consistency: "+std::string(exception.what()); return false; }
