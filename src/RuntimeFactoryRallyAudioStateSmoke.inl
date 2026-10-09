// Issue25.14: synthetic player-command codec controls. These do not create
// units, dispatch gameplay orders, or stand in for genuine factory production.
// Literal schema rows deliberately avoid the new queue member so the old
// business baseline can compile this function without executing it.
#pragma optimize("", off)
bool runRuntimeFactoryRallyAudioStateSmoke(std::string& error) {
    const char* requested = std::getenv("CONCORD_FACTORY_RALLY_AUDIO_OUTPUT");
    const std::filesystem::path root = requested && *requested
        ? requested : "build/smoke/feature-set-25/14-factory-rally-audio";
    std::string current;
    int controls = 0, rejected = 0;
    try {
        std::filesystem::create_directories(root);
        std::ofstream rows(root / "state-codec.csv");
        if (!rows) { error = "Issue25 factory rally state codec evidence stream"; return false; }
        rows << "control,kind,expected,actual,passed,scope\n";
        const auto check = [&](const char* name, const char* kind, bool ok,
            const std::string& expected, const std::string& actual) {
            current = name;
            rows << std::quoted(name) << ',' << kind << ',' << std::quoted(expected)
                << ',' << std::quoted(actual) << ',' << ok << ",synthetic_codec\n";
            rows.flush();
            if (!ok) { error = "Issue25 factory rally state codec " + current + ": " + actual; return false; }
            ++controls;
            return true;
        };
        using Kind = RuntimeMultiplayerGameplayTargetOrderKind;
        const auto order = [](Kind kind, int id, RuntimeMultiplayerGameplayTarget target) {
            RuntimeMultiplayerGameplayTargetOrderCommand result;
            result.order = kind; result.objectIds = {id}; result.target = std::move(target);
            result.append = false; return result;
        };
        const auto wireHex = [&](const RuntimeMultiplayerGameplayCommand& command) {
            std::vector<std::uint8_t> bytes; std::string why;
            if (!encodeRuntimeMultiplayerGameplayCommand(command, bytes, why))
                throw std::runtime_error("synthetic command encoding: " + why);
            constexpr char hex[] = "0123456789abcdef";
            std::string result; result.reserve(bytes.size() * 2);
            for (std::uint8_t byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
            return result;
        };
        const auto hexOrder = [&](const RuntimeMultiplayerGameplayTargetOrderCommand& value) {
            RuntimeMultiplayerGameplayCommand command; command.body = value; return wireHex(command);
        };
        const auto move = order(Kind::Move, 7, RuntimeMultiplayerGameplayCellTarget{{4, 5}});
        const auto manualObject = order(Kind::Primary, 8, RuntimeMultiplayerGameplayObjectTarget{31});
        const std::string moveHex = hexOrder(move), manualHex = hexOrder(manualObject);
        // Fixed, independently authored old tails: following, primary, rally;
        // then alliance(v2), travelling(v3), and empty waypoint literal(v4).
        const auto tail = [](int schema) {
            std::string result = " 0 0 0";
            if (schema >= 2) result += " 0";
            if (schema >= 3) result += " 0";
            if (schema >= 4) result += " \"1 0 0\"";
            return result;
        };
        const auto literal = [&](int schema, const std::string& originIndex,
            const std::string& commandHex, int rowId = 7, const std::string& owner = "player") {
            std::ostringstream out;
            out << schema << " 1 " << rowId << ' ' << std::quoted(owner) << " 1";
            if (schema >= 5 && !originIndex.empty()) out << ' ' << originIndex;
            out << ' ' << commandHex << tail(schema); return out.str();
        };
        struct ObservedQueue { std::string owner; std::size_t length = 0; int index = -2; };
        const auto observe = [](const RuntimePlayerCommandState& state) {
            std::map<int, ObservedQueue> observed;
            std::istringstream in(state.serialize()); int schema = 0; std::size_t count = 0;
            if (!(in >> schema >> count) || schema != 5)
                throw std::runtime_error("current serializer must write schema5");
            for (std::size_t index = 0; index < count; ++index) {
                int id = 0; ObservedQueue row;
                if (!(in >> id >> std::quoted(row.owner) >> row.length >> row.index)
                    || (row.index < -1 || row.index >= static_cast<int>(row.length)))
                    throw std::runtime_error("invalid observed schema5 queue");
                for (std::size_t j = 0; j < row.length; ++j) {
                    std::string ignored; if (!(in >> ignored)) throw std::runtime_error("missing observed command");
                }
                if (!observed.emplace(id, std::move(row)).second)
                    throw std::runtime_error("duplicate observed queue");
            }
            return observed;
        };
        for (int schema = 1; schema <= 4; ++schema) {
            RuntimePlayerCommandState state, reloaded; std::string why;
            const bool restored = state.restore(literal(schema, "", moveHex), why);
            const auto observed = restored ? observe(state) : std::map<int, ObservedQueue>{};
            const bool ok = restored && observed.size() == 1 && observed.at(7).index == -1
                && state.queues.at(7).owner == "player" && state.queues.at(7).orders.size() == 1
                && state.queues.at(7).orders.front() == move
                && reloaded.restore(state.serialize(), why)
                && reloaded.serialize() == state.serialize() && reloaded.stateHash() == state.stateHash();
            const std::string name = "legacy-v" + std::to_string(schema) + "-literal";
            if (!check(name.c_str(), "migration", ok, "old queue defaults index-1; schema5 roundtrip exact",
                restored ? state.serialize() : why)) return false;
        }
        RuntimePlayerCommandState marked, unmarked; std::string why;
        const bool markedRestored = marked.restore(literal(5, "0", moveHex), why);
        if (!check("schema5-index-zero", "restore", markedRestored
            && observe(marked).at(7).index == 0 && marked.queues.at(7).orders.front() == move,
            "first Move/cell queue marked", why.empty() ? marked.serialize() : why)) return false;
        const bool unmarkedRestored = unmarked.restore(literal(5, "-1", moveHex), why);
        if (!check("schema5-index-minus-one", "restore", unmarkedRestored
            && observe(unmarked).at(7).index == -1 && unmarked.queues.at(7).orders.front() == move,
            "manual Move/cell queue unmarked", why.empty() ? unmarked.serialize() : why)) return false;
        RuntimePlayerCommandState roundtrip;
        if (!check("schema5-roundtrip-and-hash", "hash", roundtrip.restore(marked.serialize(), why)
            && roundtrip.serialize() == marked.serialize() && roundtrip.stateHash() == marked.stateHash()
            && marked.serialize() != unmarked.serialize() && marked.stateHash() != unmarked.stateHash(),
            "exact restored bytes/hash; origin contributes to authoritative hash",
            std::to_string(marked.stateHash()) + "/" + std::to_string(unmarked.stateHash()))) return false;
        RuntimePlayerCommandState manual;
        const bool manualRestored = manual.restore(literal(5, "-1", manualHex, 8), why);
        if (!check("unmarked-object-primary-retained", "restore", manualRestored
            && observe(manual).at(8).index == -1 && manual.queues.at(8).orders.front() == manualObject,
            "index-1 preserves ordinary contextual object order", why.empty() ? manual.serialize() : why)) return false;
        // Perform an actual local deque prepend, then author its indexed save
        // row independently. This is a synthetic codec state, not a runtime
        // factory-exit yield or proof of the production index-update call site.
        auto prependedOrders = marked.queues.at(7).orders;
        const auto resume = order(Kind::Move, 7, RuntimeMultiplayerGameplayCellTarget{{9, 10}});
        prependedOrders.push_front(resume);
        const std::string indexedSave = "5 1 7 \"player\" 2 1 " + hexOrder(resume) + " " + moveHex + tail(5);
        RuntimePlayerCommandState indexed, indexedReload;
        const bool indexedRestored = indexed.restore(indexedSave, why);
        if (!check("prepend-second-Move-index-one", "indexed_restore", indexedRestored
            && observe(indexed).at(7).index == 1 && observe(indexed).at(7).length == 2
            && indexed.queues.at(7).orders == prependedOrders
            && indexedReload.restore(indexed.serialize(), why)
            && indexedReload.serialize() == indexedSave
            && indexedReload.stateHash() == indexed.stateHash(),
            "actual local deque prepend; index1 marks original second Move; codec exact",
            indexedRestored ? indexed.serialize() : why)) return false;
        const auto manualPrefix = order(Kind::Primary, 7, RuntimeMultiplayerGameplayObjectTarget{31});
        const std::string prefixedSave = "5 1 7 \"player\" 2 1 " + hexOrder(manualPrefix) + " " + moveHex + tail(5);
        RuntimePlayerCommandState prefixed;
        const bool prefixRestored = prefixed.restore(prefixedSave, why);
        if (!check("index-one-validates-designated-order", "indexed_restore", prefixRestored
            && observe(prefixed).at(7).index == 1 && prefixed.queues.at(7).orders.front() == manualPrefix
            && prefixed.queues.at(7).orders[1] == move && prefixed.serialize() == prefixedSave,
            "only designated second order requires Move/cell; ordinary prefix preserved",
            prefixRestored ? prefixed.serialize() : why)) return false;
        // Enrich the existing target before every malformed restore. Candidate
        // rejection must preserve every authoritative collection, not just origin index.
        RuntimePlayerCommandState atomic = marked;
        atomic.following[9] = {"player", 10, {-1, -1}};
        atomic.primaryFactories[{"player", "UNITTYPE:LAND"}] = 21;
        atomic.rallyPoints[21] = {"player", {8, 9}};
        atomic.alliances[{"player", "other"}] = true;
        atomic.travelling[11] = {"player", {12, 13}, Kind::AttackMove, false, 17};
        const std::string before = atomic.serialize(); const auto beforeHash = atomic.stateHash();
        const auto bad = [&](const char* name, const std::string& encoded) {
            std::string rejection;
            const bool accepted = atomic.restore(encoded, rejection);
            const bool ok = !accepted && !rejection.empty() && atomic.serialize() == before
                && atomic.stateHash() == beforeHash && observe(atomic).at(7).index == 0;
            if (ok) ++rejected;
            return check(name, "atomic_reject", ok, "reject without any authoritative mutation",
                std::string("accepted=") + (accepted ? "1" : "0") + ";error=" + rejection);
        };
        if (!bad("index-greater-than-length", literal(5, "2", moveHex))
            || !bad("index-equal-to-length", literal(5, "1", moveHex))
            || !bad("index-less-than-minus-one", literal(5, "-2", moveHex))
            || !bad("index-missing", literal(5, "", moveHex))
            || !bad("index-text", literal(5, "not-an-index", moveHex))
            || !bad("index-fractional", literal(5, "0.0", moveHex))) return false;
        const auto primaryCell = order(Kind::Primary, 7, RuntimeMultiplayerGameplayCellTarget{{4, 5}});
        const auto attackObject = order(Kind::Attack, 7, RuntimeMultiplayerGameplayObjectTarget{31});
        const auto primaryObject = order(Kind::Primary, 7, RuntimeMultiplayerGameplayObjectTarget{31});
        const auto wrongActor = order(Kind::Move, 8, RuntimeMultiplayerGameplayCellTarget{{4, 5}});
        auto appended = move; appended.append = true;
        RuntimeMultiplayerGameplayCommand stop; stop.body = RuntimeMultiplayerGameplayStopCommand{{7}};
        if (!bad("marked-non-Move-first", literal(5, "0", hexOrder(primaryCell)))
            || !bad("marked-attack-object", literal(5, "0", hexOrder(attackObject)))
            || !bad("marked-contextual-object", literal(5, "0", hexOrder(primaryObject)))
            || !bad("marked-wrong-actor-id", literal(5, "0", hexOrder(wrongActor)))
            || !bad("marked-wire-append", literal(5, "0", hexOrder(appended)))
            || !bad("marked-non-target-command", literal(5, "0", wireHex(stop)))
            || !bad("indexed-second-non-Move", "5 1 7 \"player\" 2 1 " + moveHex + " " + hexOrder(primaryCell) + tail(5))
            || !bad("schema5-trailing-token", literal(5, "0", moveHex) + " trailing")
            || !bad("schema5-truncated-waypoints", "5 1 7 \"player\" 1 0 " + moveHex + " 0 0 0 0 0")
            || !bad("future-schema-six", literal(6, "0", moveHex))) return false;
        auto queued = marked;
        auto next = order(Kind::Move, 7, RuntimeMultiplayerGameplayCellTarget{{6, 7}}); next.append = true;
        const bool sameOwnerAppended = queued.append("player", next);
        if (!check("same-owner-manual-append", "cleanup", sameOwnerAppended
            && observe(queued).at(7).index == 0 && observe(queued).at(7).length == 2
            && queued.queues.at(7).orders[1].target == next.target && !queued.queues.at(7).orders[1].append,
            "later manual order does not erase first-order origin", queued.serialize())) return false;
        auto ownerChanged = marked;
        const bool ownerReplaced = ownerChanged.append("other", next);
        if (!check("owner-replacement-clears-origin", "cleanup", ownerReplaced
            && observe(ownerChanged).at(7).index == -1 && observe(ownerChanged).at(7).length == 1
            && observe(ownerChanged).at(7).owner == "other"
            && ownerChanged.queues.at(7).orders.front().target == next.target,
            "replacement owner gets only its own unmarked order", ownerChanged.serialize())) return false;
        auto emptyQueue = marked; emptyQueue.queues.at(7).orders.clear();
        const bool emptyReused = emptyQueue.append("player", next);
        if (!check("empty-queue-reuse-clears-origin", "cleanup", emptyReused
            && observe(emptyQueue).at(7).index == -1 && observe(emptyQueue).at(7).length == 1,
            "empty same-owner queue cannot leak a stale origin", emptyQueue.serialize())) return false;
        auto cancelled = atomic;
        auto other = order(Kind::Move, 8, RuntimeMultiplayerGameplayCellTarget{{14, 15}});
        if (!cancelled.append("player", other)) return check("cancel-setup", "cleanup", false, "append", "rejected");
        cancelled.cancel({7, 9, 11});
        if (!check("cancel-removes-origin-and-selected-state", "cleanup", !cancelled.queues.contains(7)
            && !cancelled.following.contains(9) && !cancelled.travelling.contains(11)
            && observe(cancelled).size() == 1 && observe(cancelled).at(8).index == -1
            && cancelled.primaryFactories == atomic.primaryFactories
            && cancelled.rallyPoints.at(21).owner == "player" && cancelled.alliances == atomic.alliances,
            "selected queues/follow/travel removed; other state retained", cancelled.serialize())) return false;
        const bool recreated = cancelled.append("player", next);
        if (!check("cancel-then-manual-recreate", "cleanup", recreated
            && observe(cancelled).at(7).index == -1 && observe(cancelled).at(7).length == 1,
            "cancelled actor recreated with index-1 origin", cancelled.serialize())) return false;
        const bool emptyRestored = cancelled.restore("", why);
        if (!check("empty-restore-clears-origin", "cleanup", emptyRestored
            && cancelled.queues.empty() && cancelled.following.empty() && cancelled.travelling.empty()
            && cancelled.primaryFactories.empty() && cancelled.rallyPoints.empty() && cancelled.alliances.empty()
            && cancelled.serialize() == "5 0 0 0 0 0 0 \"1 0 0\"",
            "empty input retains legacy full reset semantics", cancelled.serialize())) return false;
        rows.flush(); if (!rows) { error = "Issue25 factory rally state codec evidence write"; return false; }
        std::ofstream result(root / "result-state.txt");
        if (!result) { error = "Issue25 factory rally state codec result stream"; return false; }
        result << "PASS syntheticCodecControls=" << controls << " legacyLiterals=4 schema5Indices=3 indexedControls=2 atomicRejects="
            << rejected << " cleanupControls=6 gameProductionScenes=0 networkWireVersion="
            << runtimeMultiplayerGameplayCommandVersion << '\n';
        result << "Scope: synthetic codec literals and local authoritative-state methods; no world production, gameplay dispatch, audio playback or physical save reopen.\n";
        result.flush(); if (!result) { error = "Issue25 factory rally state codec result write"; return false; }
        std::cout << "Issue25 factory rally state codec PASS synthetic=" << controls << " oldLiterals=4 atomicRejects=" << rejected << '\n';
        error.clear(); return true;
    } catch (const std::exception& exception) {
        error = "Issue25 factory rally state codec " + current + ": " + exception.what(); return false;
    }
}
#pragma optimize("", on)
