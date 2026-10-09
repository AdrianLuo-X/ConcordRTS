#include <array>
#include <cstring>
#include <limits>

bool runRuntimeInfantryWaterSoundSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& text) { error = "Water sound: " + text; return false; };
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    stock->theaterLoaded = stock->rulesLoaded = true;
    stock->runtimeSoundsEnabled = false;
    if (!loadRuntimeSoundDatabase(*stock)) return fail("original sound catalog");
    for (const char* id : {"GHOST", "TANY"}) {
        const auto* unit = stock->rules.findUnitType("InfantryType", id);
        if (!unit || unit->enterWaterSound != "TanyaEntersWater" || unit->leaveWaterSound != "TanyaLeavesWater")
            return fail(std::string(id) + " original fields");
    }
    const auto* gi = stock->rules.findUnitType("InfantryType", "E1");
    if (!gi || !gi->enterWaterSound.empty() || !gi->leaveWaterSound.empty()) return fail("empty defaults");
    for (const char* section : {"TanyaEntersWater", "TanyaLeavesWater"}) {
        emitRuntimeSound(*stock, std::string("sound:") + section);
        if (stock->runtimeSoundPlaybackHistory.empty()) return fail("sound route omitted");
        const auto& played = stock->runtimeSoundPlaybackHistory.back();
        if (played.section != section || played.sample.empty() || !played.playable)
            return fail(std::string(section) + " did not resolve original audio: " + played.status);
    }
    auto context = runtimeTerrainMetadataContext(*stock);
    context.rules = &stock->rules; context.art = &stock->art;
    context.palettePath = runtimePalettePath(config, stock->report.map);
    context.structurePalettePath = runtimeStructurePalettePath(config, stock->report.map);
    if (!runRuntimeInfantrySwimRenderContractCheck(context,error)) return false;
    std::cout << "Water sound: original fields, audio sample resolution and swimming animation passed\n";
    TerrainCell wet, dry;
    bool foundWet = false, foundDry = false;
    for (const auto& set : stock->theater.getTileSets()) {
        const auto name = toLowerAsciiLocal(set.fileName + " " + set.setName);
        if (name.find("shore") == std::string::npos && name.find("water") == std::string::npos) continue;
        for (int tile = set.startTileIndex; tile < set.startTileIndex + set.tilesInSet; ++tile) {
            for (int sub = 0; sub < 16; ++sub) {
                TerrainCell probe; probe.tileIndex = tile; probe.subTileIndex = sub;
                const auto land = resolveRuntimeTerrainSubTileLandType(probe, context, stock->renderCache);
                if (land == RuntimeTerrainLandType::Water && !foundWet) { wet = probe; foundWet = true; }
                if ((land == RuntimeTerrainLandType::Clear || land == RuntimeTerrainLandType::Rough)
                    && name.find("shore") != std::string::npos && !foundDry) { dry = probe; foundDry = true; }
                if (foundWet && foundDry) break;
            }
            if (foundWet && foundDry) break;
        }
        if (foundWet && foundDry) break;
    }
    if (!foundWet || !foundDry) return fail("authored water and dry shore TMPs missing");
    const auto fixture = [&](int hz) {
        auto s = std::make_unique<RuntimeWindowState>();
        s->config = config; s->config.fixedTickHz = hz; s->config.computerAiEnabled = false;
        s->config.defaultMapPath = "water-sound-smoke.map"; s->config.playerSlots.clear(); s->config.shortGame = false;
        s->report = stock->report; s->rules = stock->rules; s->rulesLoaded = true;
        s->theater = stock->theater; s->theaterLoaded = true; s->mapFactsLoaded = true;
        s->economy.owner = "Alpha"; s->runtimeSoundsEnabled = false;
        s->runtimeSuppressPlayerAudioFeedback = true; s->runtimePreferencePersistenceEnabled = false;
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x) {
            auto cell = x >= 6 && x <= 8 ? wet : dry;
            cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            s->world.accessTerrainCells().push_back(cell);
            MapCellFacts fact; fact.x = x; fact.y = y; fact.cellIndex = cell.cellIndex512;
            fact.hasTerrain = true;
            fact.terrain.x = x; fact.terrain.y = y;
            fact.terrain.tileIndex = cell.tileIndex; fact.terrain.subTileIndex = cell.subTileIndex;
            fact.terrain.level = cell.level;
            // Deliberately ambiguous coarse shoreline flags: TMP decides.
            fact.isWater = fact.isShore = true;
            s->mapFacts.cellIndexLookup.emplace(fact.cellIndex,s->mapFacts.cells.size());
            s->mapFacts.cells.push_back(fact);
        }
        WorldObjectInstance object; object.instanceId = 1; object.kind = WorldObjectKind::Infantry;
        object.type = "GHOST"; object.owner = "Alpha"; object.alive = true; object.health = 125;
        object.x = 4; object.y = 10; object.cell = runtimeCellKey(4,10); object.mission = "Guard";
        s->world.accessObjects().push_back(object); initializeRuntimeNavigation(*s);
        return s;
    };
    const auto collect = [](RuntimeWindowState& s, std::vector<std::string>& events) {
        for (const auto& event : s.runtimeSoundIntentHistory)
            if (event == "sound:TanyaEntersWater" || event == "sound:TanyaLeavesWater") events.push_back(event);
        s.runtimeSoundIntentHistory.clear();
    };
    const std::vector<std::string> pair = {"sound:TanyaEntersWater", "sound:TanyaLeavesWater"};
    const auto tick = [](RuntimeWindowState& s) { ++s.tick; (void)advanceRuntimeSimulation(s); };
    const auto equalPose = [](const RuntimeInfantryWaterPose& a, const RuntimeInfantryWaterPose& b) {
        return a.valid == b.valid && a.swimming == b.swimming
            && std::memcmp(&a.x,&b.x,sizeof(double)) == 0
            && std::memcmp(&a.y,&b.y,sizeof(double)) == 0;
    };
    const auto equalAuthority = [&](RuntimeWindowState& a, RuntimeWindowState& b,
        const std::string& phase) {
        completeRuntimeWorldRefresh(a); completeRuntimeWorldRefresh(b);
        std::string oldError, newError;
        const auto oldHash = runtimeWorldSimulationHash(a,oldError,false);
        const auto newHash = runtimeWorldSimulationHash(b,newError,false);
        if (!oldError.empty() || !newError.empty() || oldHash != newHash
            || a.world.deterministicSimulationStateHash() != b.world.deterministicSimulationStateHash())
            return fail("context reuse full authority " + phase + " old=" + oldError + " new=" + newError);
        return true;
    };
    // These are actual original TMP/MapFacts queries, including objects which
    // must return before lazily creating any terrain context.
    for (int sample = 0; sample < 17; ++sample) {
        auto oldState = fixture(30), newState = fixture(30);
        auto beforeObject = oldState->world.getObjects().front();
        beforeObject.x = 5; beforeObject.y = 10;
        beforeObject.hasRenderPosition = true;
        beforeObject.renderX = 5; beforeObject.renderY = 10;
        auto afterObject = beforeObject; afterObject.renderX = 6;
        switch (sample) {
        case 1: beforeObject.renderX = 8; afterObject.renderX = 9; break;
        case 2: beforeObject.renderOnBridge = true; break;
        case 3: afterObject.renderOnBridge = true; break;
        case 4: afterObject.renderX = 5; break;
        case 5: afterObject.renderX = 8; break;
        case 6: beforeObject.type = afterObject.type = "E1"; break;
        case 7: beforeObject.kind = afterObject.kind = WorldObjectKind::Unit;
            beforeObject.type = afterObject.type = "MTNK"; break;
        case 8: beforeObject.airborne = afterObject.airborne = true; break;
        case 9: beforeObject.alive = afterObject.alive = false; break;
        case 10: beforeObject.health = afterObject.health = 0; break;
        case 11: beforeObject.type = afterObject.type = "MISSING_WATER_SOUND_TYPE"; break;
        case 12: beforeObject.renderX = afterObject.renderX = -1; break;
        case 13: beforeObject.renderX = afterObject.renderX = std::numeric_limits<double>::quiet_NaN(); break;
        case 14: beforeObject.renderX = afterObject.renderX = 511.5; break;
        case 15: beforeObject.renderX = afterObject.renderX = 20; break;
        case 16: beforeObject.hasRenderPosition = false; break;
        }
        RuntimeInfantryWaterPoseQuery oldQuery(*oldState,true), newQuery(*newState,false);
        const auto oldBefore = oldQuery.pose(beforeObject), newBefore = newQuery.pose(beforeObject);
        const auto oldAfter = oldQuery.pose(afterObject), newAfter = newQuery.pose(afterObject);
        if (!equalPose(oldBefore,newBefore) || !equalPose(oldAfter,newAfter)
            || oldQuery.hasContext() || newQuery.hasContext() != (newBefore.valid || newAfter.valid))
            return fail("context reuse complete pose/lazy construction sample=" + std::to_string(sample));
        emitRuntimeInfantryWaterTransition(*oldState,afterObject,oldBefore,&oldQuery);
        emitRuntimeInfantryWaterTransition(*newState,afterObject,newBefore,&newQuery);
        if (oldState->runtimeSoundIntentHistory != newState->runtimeSoundIntentHistory
            || !equalAuthority(*oldState,*newState,"pose " + std::to_string(sample)))
            return fail("context reuse sound/authority sample=" + std::to_string(sample) + " " + error);
        // A query belonging to another state must use this state's scalar path.
        const auto history = oldState->runtimeSoundIntentHistory;
        emitRuntimeInfantryWaterTransition(*oldState,afterObject,oldBefore,&newQuery);
        auto foreignReference = fixture(30);
        foreignReference->runtimeSoundIntentHistory = history;
        emitRuntimeInfantryWaterTransition(*foreignReference,afterObject,oldBefore);
        if (oldState->runtimeSoundIntentHistory != foreignReference->runtimeSoundIntentHistory)
            return fail("foreign context query borrowed another state's metadata");
    }
    // Scalar, C16 per-actor metadata and one lexical-loop metadata context.
    // Every branch executes the same original pose and sound decisions. Four
    // actors make actual construction savings observable without timing them.
    for (int sample = 0; sample < 17; ++sample) {
        std::array<std::unique_ptr<RuntimeWindowState>,3> states;
        std::array<std::vector<RuntimeInfantryWaterPose>,3> poses;
        std::array<std::uint64_t,3> poseConstructions{};
        for (int mode = 0; mode < 3; ++mode) {
            states[mode] = fixture(30);
            std::uint64_t constructions = 0;
            RuntimeInfantryWaterContextReferenceScope scalar(mode == 0);
            RuntimeInfantryWaterBatchContextReferenceScope c16(mode != 2);
            RuntimeInfantryWaterContextConstructionScope witness(constructions);
            RuntimeInfantryWaterBehaviorContextScope batch(*states[mode], mode == 2);
            for (int actor = 0; actor < 4; ++actor) {
                auto before = states[mode]->world.getObjects().front();
                before.instanceId = actor + 1; before.x = 5; before.y = 7 + actor;
                before.hasRenderPosition = true; before.renderX = 5; before.renderY = before.y;
                auto after = before; after.renderX = 6;
                switch (sample) {
                case 1: before.renderX = 8; after.renderX = 9; break;
                case 2: before.renderOnBridge = true; break;
                case 3: after.renderOnBridge = true; break;
                case 4: after.renderX = 5; break;
                case 5: after.renderX = 8; break;
                case 6: before.type = after.type = "E1"; break;
                case 7: before.kind = after.kind = WorldObjectKind::Unit;
                    before.type = after.type = "MTNK"; break;
                case 8: before.airborne = after.airborne = true; break;
                case 9: before.alive = after.alive = false; break;
                case 10: before.health = after.health = 0; break;
                case 11: before.type = after.type = "MISSING_WATER_SOUND_TYPE"; break;
                case 12: before.renderX = after.renderX = -1; break;
                case 13: before.renderX = after.renderX = std::numeric_limits<double>::quiet_NaN(); break;
                case 14: before.renderX = after.renderX = 511.5; break;
                case 15: before.renderX = after.renderX = 20; break;
                case 16: before.hasRenderPosition = false; break;
                }
                RuntimeInfantryWaterPoseQuery query(*states[mode]);
                const auto countBefore = constructions;
                const auto first = query.pose(before), last = query.pose(after);
                poseConstructions[mode] += constructions - countBefore;
                poses[mode].push_back(first); poses[mode].push_back(last);
                emitRuntimeInfantryWaterTransition(*states[mode], after, first, &query);
            }
            const bool eligible = poses[mode].front().valid || poses[mode][1].valid;
            const std::uint64_t expected = eligible ? (mode == 0 ? 8 : mode == 1 ? 4 : 1) : 0;
            if (poseConstructions[mode] != expected || batch.hasContext() != (eligible && mode == 2))
                return fail("batch actual construction witness sample=" + std::to_string(sample)
                    + " mode=" + std::to_string(mode) + " count=" + std::to_string(poseConstructions[mode]));
        }
        for (int mode = 1; mode < 3; ++mode) {
            for (std::size_t pose = 0; pose < poses[0].size(); ++pose)
                if (!equalPose(poses[0][pose],poses[mode][pose]))
                    return fail("batch three-way complete pose sample=" + std::to_string(sample));
            if (states[0]->runtimeSoundIntentHistory != states[mode]->runtimeSoundIntentHistory
                || !equalAuthority(*states[0],*states[mode],"batch pose " + std::to_string(sample)))
                return fail("batch three-way ordered sound/authority sample=" + std::to_string(sample) + " " + error);
        }
    }
    {
        RuntimeInfantryWaterContextReferenceScope optimized(false);
        RuntimeInfantryWaterBatchContextReferenceScope batchEnabled(false);
        auto state = fixture(30), foreign = fixture(30);
        auto actor = state->world.getObjects().front(); actor.x = 6;
        RuntimeInfantryWaterBehaviorContextScope outer(*state);
        RuntimeInfantryWaterPoseQuery first(*state);
        if (!equalPose(first.pose(actor),runtimeInfantryWaterPose(*state,actor)) || first.hasContext())
            return fail("batch outer did not loan exact metadata");
        const auto* original = runtimeBorrowInfantryWaterMetadataContext(*state);
        if (!original || !outer.hasContext()) return fail("batch outer lazy construction");
        {
            RuntimeInfantryWaterBehaviorContextScope disabled(*state,false);
            RuntimeInfantryWaterPoseQuery query(*state);
            if (!equalPose(query.pose(actor),runtimeInfantryWaterPose(*state,actor))
                || !query.hasContext() || disabled.hasContext()
                || runtimeBorrowInfantryWaterMetadataContext(*state))
                return fail("batch disabled inner borrowed outer metadata");
        }
        if (runtimeBorrowInfantryWaterMetadataContext(*state) != original)
            return fail("batch disabled inner failed to restore outer");
        {
            RuntimeInfantryWaterBehaviorContextScope inner(*foreign);
            RuntimeInfantryWaterPoseQuery other(*state), own(*foreign);
            if (!equalPose(other.pose(actor),runtimeInfantryWaterPose(*state,actor)) || !other.hasContext()
                || !equalPose(own.pose(actor),runtimeInfantryWaterPose(*foreign,actor)) || own.hasContext()
                || !inner.hasContext()) return fail("batch foreign/nested state identity");
        }
        if (runtimeBorrowInfantryWaterMetadataContext(*state) != original)
            return fail("batch foreign inner failed to restore outer");
        {
            RuntimeInfantryWaterBatchContextReferenceScope c16(true);
            RuntimeInfantryWaterPoseQuery query(*state);
            if (!equalPose(query.pose(actor),runtimeInfantryWaterPose(*state,actor)) || !query.hasContext())
                return fail("batch dedicated reference did not fall back to C16");
        }
        if (runtimeBorrowInfantryWaterMetadataContext(*state) != original)
            return fail("batch reference restoration");
        ++state->tick;
        RuntimeInfantryWaterPoseQuery nextTick(*state);
        if (!equalPose(nextTick.pose(actor),runtimeInfantryWaterPose(*state,actor)) || !nextTick.hasContext()
            || runtimeBorrowInfantryWaterMetadataContext(*state)) return fail("batch tick mismatch fallback");
        --state->tick;
        if (runtimeBorrowInfantryWaterMetadataContext(*state)) return fail("batch invalidated tick resurrected");
    }
    // Source metadata is immutable only within the declared loan. Test both
    // automatic identity rejection and explicit invalidation before same-
    // address edits, followed by a fresh scope on the changed source.
    for (int change = 0; change < 7; ++change) {
        auto oldState = fixture(30), newState = fixture(30);
        RuntimeInfantryWaterContextReferenceScope optimized(false);
        RuntimeInfantryWaterBatchContextReferenceScope batchEnabled(false);
        auto actor = newState->world.getObjects().front(); actor.x = 6;
        RuntimeInfantryWaterBehaviorContextScope scope(*newState);
        RuntimeInfantryWaterPoseQuery before(*newState);
        const auto wetPose = before.pose(actor);
        if (!wetPose.valid || !wetPose.swimming || before.hasContext())
            return fail("batch source fixture did not use original wet TMP");
        if (change >= 4) scope.invalidate();
        const auto mutate = [&](RuntimeWindowState& state) {
            switch (change) {
            case 0: state.config.localDataRootPath.clear(); break;
            case 1: state.report.map.path.clear(); break;
            case 2: state.theaterLoaded = false; break;
            case 3: ++state.mapFacts.contentHash; break;
            case 4: {
                const auto key = 10 * 512 + 6;
                const auto position = state.mapFacts.cellIndexLookup.find(key);
                if (position == state.mapFacts.cellIndexLookup.end()) return false;
                auto& cell = state.mapFacts.cells[position->second];
                cell.terrain.tileIndex = dry.tileIndex; cell.terrain.subTileIndex = dry.subTileIndex;
                break;
            }
            case 5: state.theater = stock->theater; break;
            case 6: { auto replacement = state.mapFacts; state.mapFacts = std::move(replacement); break; }
            }
            return true;
        };
        if (!mutate(*oldState) || !mutate(*newState)) return fail("batch source mutation fixture");
        RuntimeInfantryWaterPoseQuery after(*newState);
        const auto oldAfter = runtimeInfantryWaterPose(*oldState,actor), newAfter = after.pose(actor);
        if (!equalPose(oldAfter,newAfter) || !after.hasContext()
            || runtimeBorrowInfantryWaterMetadataContext(*newState))
            return fail("batch source invalidation exact fallback change=" + std::to_string(change));
        if (change == 4 && (newAfter.swimming || !newAfter.valid))
            return fail("batch in-place TMP edit did not change real wet pose to dry");
        if (!equalAuthority(*oldState,*newState,"batch source " + std::to_string(change))) return false;
        {
            RuntimeInfantryWaterBehaviorContextScope refreshed(*newState);
            RuntimeInfantryWaterPoseQuery query(*newState);
            if (!equalPose(query.pose(actor),oldAfter) || query.hasContext() || !refreshed.hasContext())
                return fail("batch fresh source scope reload change=" + std::to_string(change));
        }
        if (runtimeBorrowInfantryWaterMetadataContext(*newState))
            return fail("batch reload reactivated invalidated outer");
    }
    std::cout << "Water sound: batch 17 three-way four-actor poses/sounds and 8/4/1 construction witness, foreign/nested/disabled/reference/tick and seven source invalidation cases passed\n";
    {
        const int originalOverride = runtimeInfantryWaterContextReferenceOverride;
        auto state = fixture(30);
        {
            RuntimeInfantryWaterContextReferenceScope outer(true);
            RuntimeInfantryWaterPoseQuery oldQuery(*state);
            if (!oldQuery.reference()) return fail("context reference override");
            {
                RuntimeInfantryWaterContextReferenceScope inner(false);
                RuntimeInfantryWaterPoseQuery newQuery(*state);
                if (newQuery.reference()) return fail("context nested false override");
            }
            if (!runtimeInfantryWaterContextReference()) return fail("context nested restoration");
        }
        if (runtimeInfantryWaterContextReferenceOverride != originalOverride)
            return fail("context outer restoration");
    }
    // Drive both production advanceRuntimeRenderMove branches through real
    // commands and simulation, then compare all authoritative state each tick.
    for (int hz : {15,30,60}) {
        auto oldState = fixture(hz), newState = fixture(hz);
        oldState->selectedObjectIds = newState->selectedObjectIds = {1};
        issueRuntimeMoveCommand(*oldState,{10,10}); issueRuntimeMoveCommand(*newState,{10,10});
        if (oldState->lastCommand.acceptedCount != 1 || newState->lastCommand.acceptedCount != 1
            || !equalAuthority(*oldState,*newState,"command")) return fail("context comparison command fixture");
        std::vector<std::string> oldEvents, newEvents;
        for (int n = 0; n < 1200 && (oldState->behaviors.contains(1) || newState->behaviors.contains(1)); ++n) {
            { RuntimeInfantryWaterContextReferenceScope reference(true); tick(*oldState); }
            { RuntimeInfantryWaterContextReferenceScope optimized(false); tick(*newState); }
            collect(*oldState,oldEvents); collect(*newState,newEvents);
            const auto* a = findObjectById(*oldState,1); const auto* b = findObjectById(*newState,1);
            if (!a || !b || a->x != b->x || a->y != b->y || a->renderX != b->renderX
                || a->renderY != b->renderY || a->renderLevel != b->renderLevel
                || a->hasRenderPosition != b->hasRenderPosition || a->renderMoveActive != b->renderMoveActive
                || a->renderOnBridge != b->renderOnBridge || a->facing != b->facing
                || a->movementAnimationDistance != b->movementAnimationDistance || oldEvents != newEvents
                || !equalAuthority(*oldState,*newState,"tick " + std::to_string(n)))
                return fail("context comparison actual movement Hz=" + std::to_string(hz) + " " + error);
        }
        if (oldEvents != pair || newEvents != pair || oldState->world.getObjects().front().x != 10
            || newState->world.getObjects().front().x != 10) return fail("context comparison actual crossing lost");
        std::ostringstream oldSave, newSave;
        std::string oldError, newError;
        if (!writeRuntimeStateToStream(*oldState,oldSave,oldError)
            || !writeRuntimeStateToStream(*newState,newSave,newError)
            || oldSave.str() != newSave.str()) return fail("context comparison full save " + oldError + " " + newError);
    }
    std::cout << "Water sound: context reuse 17 exact poses, foreign/default paths, nested reference and full authority/save actual 15/30/60 Hz commands passed\n";
    // Exercise the installed behavior-loop scope through actual multi-actor
    // commands. Complete poses, interpolation fields, sound order and full
    // authority agree every tick; stable saves also agree at every tick.
    for (int hz : {15,30,60}) {
        std::array<std::unique_ptr<RuntimeWindowState>,3> states;
        std::array<std::vector<std::string>,3> events;
        std::array<std::uint64_t,3> constructions{};
        for (int mode = 0; mode < 3; ++mode) {
            states[mode] = fixture(hz);
            auto actor = states[mode]->world.getObjects().front();
            states[mode]->world.accessObjects().clear();
            for (int id = 1; id <= 4; ++id) {
                actor.instanceId = id; actor.y = 6 + id; actor.cell = runtimeCellKey(actor.x,actor.y);
                states[mode]->world.accessObjects().push_back(actor);
            }
            initializeRuntimeNavigation(*states[mode]);
            RuntimeInfantryWaterContextReferenceScope scalar(mode == 0);
            RuntimeInfantryWaterBatchContextReferenceScope c16(mode != 2);
            for (int id = 1; id <= 4; ++id) {
                states[mode]->selectedObjectIds = {id};
                issueRuntimeMoveCommand(*states[mode],{10,6 + id});
                if (states[mode]->lastCommand.acceptedCount != 1)
                    return fail("batch real command fixture mode=" + std::to_string(mode) + " id=" + std::to_string(id));
            }
        }
        for (int mode = 1; mode < 3; ++mode)
            if (!equalAuthority(*states[0],*states[mode],"batch multi-actor command")) return false;
        for (int n = 0; n < 1200; ++n) {
            bool pending = false;
            for (const auto& state : states)
                for (int id = 1; id <= 4; ++id) pending = pending || state->behaviors.contains(id);
            if (!pending) break;
            for (int mode = 0; mode < 3; ++mode) {
                RuntimeInfantryWaterContextReferenceScope scalar(mode == 0);
                RuntimeInfantryWaterBatchContextReferenceScope c16(mode != 2);
                RuntimeInfantryWaterContextConstructionScope witness(constructions[mode]);
                tick(*states[mode]); collect(*states[mode],events[mode]);
            }
            for (int mode = 1; mode < 3; ++mode) {
                for (int id = 1; id <= 4; ++id) {
                    const auto* a = findObjectById(*states[0],id);
                    const auto* b = findObjectById(*states[mode],id);
                    if (!a || !b || a->x != b->x || a->y != b->y || a->renderX != b->renderX
                        || a->renderY != b->renderY || a->renderLevel != b->renderLevel
                        || a->hasRenderPosition != b->hasRenderPosition
                        || a->renderMoveActive != b->renderMoveActive
                        || a->renderOnBridge != b->renderOnBridge || a->facing != b->facing
                        || a->movementAnimationDistance != b->movementAnimationDistance
                        || !equalPose(runtimeInfantryWaterPose(*states[0],*a),runtimeInfantryWaterPose(*states[mode],*b)))
                        return fail("batch real complete pose Hz=" + std::to_string(hz)
                            + " tick=" + std::to_string(n) + " id=" + std::to_string(id));
                }
                if (events[0] != events[mode]
                    || !equalAuthority(*states[0],*states[mode],"batch actual tick " + std::to_string(n)))
                    return fail("batch real ordered audio/full hash Hz=" + std::to_string(hz) + " " + error);
            }
            std::array<std::string,3> saves;
            for (int mode = 0; mode < 3; ++mode) {
                std::ostringstream saved;
                std::string saveError;
                if (!writeRuntimeStateToStream(*states[mode],saved,saveError))
                    return fail("batch full per-tick save Hz=" + std::to_string(hz) + " " + saveError);
                saves[mode] = saved.str();
            }
            if (saves[0] != saves[1] || saves[0] != saves[2])
                return fail("batch real full per-tick save differs Hz=" + std::to_string(hz) + " tick=" + std::to_string(n));
        }
        for (int mode = 0; mode < 3; ++mode) {
            if (std::count(events[mode].begin(),events[mode].end(),pair[0]) != 4
                || std::count(events[mode].begin(),events[mode].end(),pair[1]) != 4
                || events[mode].size() != 8)
                return fail("batch real crossing sound counts Hz=" + std::to_string(hz) + " mode=" + std::to_string(mode));
            for (int id = 1; id <= 4; ++id) {
                const auto* actor = findObjectById(*states[mode],id);
                if (!actor || actor->x != 10 || actor->renderMoveActive)
                    return fail("batch real crossing did not complete Hz=" + std::to_string(hz));
            }
        }
        if (!(constructions[0] > constructions[1] && constructions[1] > constructions[2]
                && constructions[2] > 0))
            return fail("batch production construction savings absent Hz=" + std::to_string(hz)
                + " scalar=" + std::to_string(constructions[0]) + " C16=" + std::to_string(constructions[1])
                + " batch=" + std::to_string(constructions[2]));
        std::cout << "Water sound: actual four-actor three-way Hz=" << hz << " contexts="
            << constructions[0] << '/' << constructions[1] << '/' << constructions[2]
            << " exact per-tick poses/sounds/full authority/full saves passed\n";
    }
    for (int hz : {15,30,60}) {
        auto s = fixture(hz); s->selectedObjectIds = {1};
        auto probe = *findObjectById(*s,1);
        const auto landPose = runtimeInfantryWaterPose(*s,probe);
        probe.x = 6;
        const auto waterPose = runtimeInfantryWaterPose(*s,probe);
        if (!landPose.valid || landPose.swimming || !waterPose.valid || !waterPose.swimming)
            return fail("fixture map lookup or authored shoreline classification");
        issueRuntimeMoveCommand(*s,{10,10});
        if (s->lastCommand.acceptedCount != 1) return fail("swimmer move command rejected");
        std::vector<std::string> events;
        for (int n = 0; n < 1200 && s->behaviors.contains(1); ++n) { tick(*s); collect(*s,events); }
        const auto* object = findObjectById(*s,1);
        if (object->x != 10 || object->renderMoveActive || events != pair)
            return fail("actual movement Hz=" + std::to_string(hz) + " x=" + std::to_string(object->x)
                + " events=" + std::to_string(events.size()) + " " + s->simulationStatus);
        for (int n = 0; n < 60; ++n) { tick(*s); collect(*s,events); }
        if (events != pair) return fail("stationary shore repeated audio");
    }
    {
        auto s = fixture(30); auto* object = findMutableObjectById(*s,1);
        const auto segment = [&](int from, int to, bool onBridge, const std::string& type, int frames = 12) {
            object->type = type; object->x = to; object->cell = runtimeCellKey(to,10);
            object->onBridge = onBridge; object->mission = "Move";
            RuntimeObjectBehavior behavior;
            beginRuntimeRenderMove(*object,behavior,{from,10},{to,10},0,onBridge,frames);
            std::vector<std::string> events;
            // Reserving a water destination alone cannot emit an entry sound.
            collect(*s,events);
            if (!events.empty()) return false;
            for (int n = 0; n < frames; ++n) {
                ++s->tick; advanceRuntimeRenderMove(*s,*object,behavior); collect(*s,events);
                if (from == 5 && to == 6 && !onBridge && type == "GHOST" && frames == 12
                    && events.size() != (n < 5 ? 0U : 1U)) return false;
            }
            const int expected = !onBridge && type == "GHOST" && (from == 5 || to == 9) ? 1 : 0;
            return events.size() == expected;
        };
        if (!segment(5,6,false,"GHOST") || !segment(8,9,false,"GHOST",1)
            || !segment(5,6,true,"GHOST") || !segment(8,9,true,"GHOST")
            || !segment(6,7,false,"GHOST") || !segment(5,6,false,"E1"))
            return fail("interpolated crossing, one-tick move, bridge, water or missing field");
    }
    {
        auto s = fixture(30);
        auto target = *findObjectById(*s,1); target.instanceId = 2; target.owner = "Beta";
        target.x = 12; target.cell = runtimeCellKey(12,10); target.health = 100000;
        s->world.accessObjects().push_back(target); initializeRuntimeNavigation(*s);
        auto* unit = const_cast<UnitType*>(s->rules.findUnitType("InfantryType","GHOST"));
        unit->secondaryWeapon.clear();
        auto* weapon = const_cast<WeaponType*>(s->rules.findWeaponType(unit->primaryWeapon));
        if (!weapon) return fail("attack fixture weapon");
        weapon->range = 1; s->selectedObjectIds = {1}; issueRuntimeAttackCommand(*s,*findObjectById(*s,2));
        std::vector<std::string> events;
        for (int n = 0; n < 1200 && events.size() < 2; ++n) { tick(*s); collect(*s,events); }
        if (events != pair) return fail("attack chase omitted shoreline audio: " + s->simulationStatus);
    }
    std::cout << "Water sound: actual movement, 15/30/60 Hz, interpolation, bridge and attack chase passed\n";
    // Save on either side of the interpolation boundary; loading itself is
    // silent, and continuing cannot repeat an entry already heard.
    for (bool entered : {false,true}) {
        auto s = fixture(30); s->selectedObjectIds = {1}; issueRuntimeMoveCommand(*s,{7,10});
        std::vector<std::string> heard;
        for (int n = 0; n < 1200; ++n) {
            tick(*s); collect(*s,heard);
            const auto* object = findObjectById(*s,1);
            if (object->renderMoveActive && object->renderX >= (entered ? 5.5 : 5.0)
                && object->renderX < (entered ? 6.0 : 5.5)) break;
        }
        const auto* object = findObjectById(*s,1);
        if (!object->renderMoveActive || heard.size() != (entered ? 1U : 0U)) return fail("save boundary fixture");
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(*s,saved,error)) return fail("full save: " + error);
        IniFile savedIni; if (!savedIni.loadFromString(saved.str())) return fail("saved INI");
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored,*s,false);
        if (!materializeRuntimeLoadStateFromIni(*restored,savedIni,error)) return fail("full restore: " + error);
        initializeRuntimeNavigation(*restored);
        std::vector<std::string> restoredEvents; collect(*restored,restoredEvents);
        if (!restoredEvents.empty()) return fail("loading emitted entry");
        std::vector<std::string> originalEvents;
        for (int n = 0; n < 240; ++n) {
            tick(*s); tick(*restored); collect(*s,originalEvents); collect(*restored,restoredEvents);
            const auto* a = findObjectById(*s,1); const auto* b = findObjectById(*restored,1);
            if (a->x != b->x || a->renderX != b->renderX || a->renderMoveActive != b->renderMoveActive)
            {
                // Failure-only, read-only diagnostics. Preserve the exact
                // original equality assertion and all continuation ticks.
                const auto describe = [](const RuntimeWindowState& state,
                    const WorldObjectInstance& actor) {
                    std::ostringstream output;
                    output << std::setprecision(17)
                        << "tick=" << state.tick << " logical=" << actor.x << ',' << actor.y
                        << " renderX=" << std::hexfloat << actor.renderX << std::defaultfloat
                        << " renderY=" << std::hexfloat << actor.renderY << std::defaultfloat
                        << " hasRender=" << actor.hasRenderPosition
                        << " renderActive=" << actor.renderMoveActive
                        << " bridge=" << actor.onBridge << '/' << actor.renderOnBridge
                        << " level=" << actor.level << '/' << actor.renderLevel
                        << " subcell=" << actor.subCell << " facing=" << actor.facing
                        << " hp=" << actor.health << " airborne=" << actor.airborne
                        << " pendingPathIds=" << state.pendingPathObjectIds.size();
                    const auto behavior = state.behaviors.find(actor.instanceId);
                    if (behavior == state.behaviors.end()) output << " no-behavior";
                    else {
                        const auto& move = behavior->second;
                        output << " kind=" << runtimeBehaviorKindName(move.kind)
                            << " pathCount=" << move.path.size()
                            << " cd=" << move.moveCooldownTicks
                            << " renderRemaining=" << move.renderMoveTicksRemaining
                            << " renderTotal=" << move.renderMoveTicksTotal
                            << " renderFrom=" << move.renderFrom.x << ',' << move.renderFrom.y
                            << " renderTo=" << move.renderTo.x << ',' << move.renderTo.y
                            << " segmentFrom=" << move.moveSegmentFrom.x << ',' << move.moveSegmentFrom.y
                            << " goal=" << move.moveTarget.x << ',' << move.moveTarget.y
                            << " pathPending=" << move.pathRequestPending
                            << " pendingStart=" << move.pendingPathStart.x << ',' << move.pendingPathStart.y
                            << " pendingGoal=" << move.pendingPathGoal.x << ',' << move.pendingPathGoal.y
                            << " nextRepath=" << move.nextRepathTick
                            << " remainderQ16=" << move.moveIntervalRemainderQ16;
                        if (!move.path.empty()) output << " next=" << move.path.front().x << ',' << move.path.front().y;
                    }
                    const auto reservation = state.subcells.reservations().find(actor.instanceId);
                    if (reservation == state.subcells.reservations().end()) output << " no-reservation";
                    else output << " reservation=" << reservation->second.destination.x << ','
                        << reservation->second.destination.y << ':' << reservation->second.subCell;
                    return output.str();
                };
                return fail("loaded movement diverged entered=" + std::to_string(entered)
                    + " continuation=" + std::to_string(n)
                    + " saveVersion=" + savedIni.getValue("RuntimeSave","Version","")
                    + " savedWalkCount=" + savedIni.getValue("InfantryWalkPresentation","Count","")
                    + " savedWalk0=" + savedIni.getValue("InfantryWalkPresentation","0","")
                    + " live:{" + describe(*s,*a) + "} loaded:{" + describe(*restored,*b) + "}");
            }
        }
        if (originalEvents != restoredEvents || restoredEvents.size() != (entered ? 0U : 1U))
            return fail("save replayed or lost shoreline crossing");
    }
    std::cout << "Water sound: passed; native fields/audio, authored shore TMP, wet animation, 15/30/60 Hz movement, body crossing, bridge, chase and full save continuity\n";
    return true;
}
