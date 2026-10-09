// Issue 16: real absorption arrivals, occupancy display and per-mutation replay.
// Canonical images are read with their physical ordinary v14 base. Legacy
// field removal is an explicitly in-memory compatibility case, never a disk claim.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeBioReactorOccupancyTransitionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string label = "startup", stage = "assets";
    const auto fail = [&](const std::string& why) {
        error = "Bio reactor transition " + label + " [" + stage + "]: " + why; return false;
    };
    auto live = std::make_unique<RuntimeWindowState>(); live->config = config;
    live->report = buildRuntimeStartupReport(config, &live->world, &live->theater, &live->art, &live->rules, &live->mapFacts);
    if (!live->report.success) return fail("original startup assets unavailable");
    live->rulesLoaded = live->artLoaded = live->theaterLoaded = true;
    const auto* stock = live->rules.findBuildingType("YAPOWR");
    const auto* footRule = live->rules.findUnitType("infantry", "E1");
    if (!stock || !footRule || !stock->infantryAbsorb || stock->passengers != 5 || stock->power != 150
        || stock->extraPower != 100 || stock->sizeLimit != 15 || footRule->strength <= 0)
        return fail("stock YAPOWR capacity/power/absorption and E1 declarations required");
    std::optional<TerrainCell> flat;
    auto metadataCache = std::make_unique<RuntimeMapRenderCache>();
    const auto metadata = runtimeTerrainMetadataContext(*live);
    for (const auto& cell : live->world.getTerrainCells()) {
        const auto info = runtimeTerrainInfoForCell(*live, cell);
        if (info.passable && !info.water && !info.bridge && !info.cliff && !info.shore && !cell.hasOverlay
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, *metadataCache) == 0) { flat = cell; break; }
    }
    if (!flat) return fail("original flat land TMP required");
    const char* out = std::getenv("CONCORD_BIOREACTOR_TRANSITION_OUTPUT");
    const std::filesystem::path root = out && *out ? out : "build/issue23_16_bioreactor_transition";
    std::filesystem::create_directories(root);
    std::ofstream cases(root / "cases.csv"), events(root / "transitions.csv"), images(root / "frames.csv"), saves(root / "checkpoints.csv");
    if (!cases || !events || !images || !saves) return fail("output unavailable");
    cases << "case,hz,arrivals,queued_unchanged,script_removed_decrements,normal_levels,damaged_levels,replay_checks,ordinary14,canonical_images,paired_live_ticks,legacy_entries,actual_unloaded,result\n";
    events << "case,stage,tick,actor,count,power,stamp,powered_stamp,alive,mission,x,y,pending_host\n";
    images << "case,stage,tick,count,health,stamp,relative_age,image,frame,roi_x,roi_y,roi_width,roi_height,roi_fnv64,powered_stamp,actualglobal_missing,simulation_tick,simulation_stamp,simulation_powered_stamp,controlled_phase\n";
    saves << "case,count,format,tick,file,base_file,disk_read,version,paired_live_ticks,authority_subset_fnv64\n";
    const auto hash = [](const auto& values) { std::uint64_t h = 14695981039346656037ULL;
        for (const auto value : values) { const auto v = std::uint64_t(value); for (int n = 0; n < int(sizeof(value)); ++n) { h ^= (v >> (n * 8)) & 255U; h *= 1099511628211ULL; } } return h; };
    const auto textHash = [](const std::string& text) { std::uint64_t h = 14695981039346656037ULL;
        for (unsigned char c : text) { h ^= c; h *= 1099511628211ULL; } return h; };
    const auto make = [&](int hz) {
        auto p = std::make_unique<RuntimeWindowState>(); auto& s = *p; seedRuntimeLoadScratch(s, *live, false);
        s.config.fixedTickHz = hz; s.config.gameMode = "Battle"; s.config.playerSlots.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.gameModeRulesOverride.clear(); s.config.multiplayerActive = s.config.computerAiEnabled = false;
        s.config.superweapons = s.config.crates = s.config.shortGame = false; s.campaignMode = false;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = s.config.playerCountry = "YuriCountry";
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.mapFacts = {}; s.mapFactsLoaded = false;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = s.report.map.height = -1;
        for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 28; ++x) {
            auto cell = *flat; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.level = 0; cell.hasOverlay = false; cell.overlayIndex = -1; cell.overlayFrame = 0; s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance host; host.instanceId = 1; host.kind = WorldObjectKind::Structure; host.type = "YAPOWR";
        host.owner = s.economy.owner; host.alive = true; host.health = stock->strength; host.x = host.y = 12;
        host.cell = runtimeCellKey(12,12); host.mission = "Guard"; clearRuntimeRenderPosition(host); s.world.accessObjects().push_back(host);
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s); return p;
    };
    const auto step = [](RuntimeWindowState& s) { RuntimeSnapshotActiveStateScope active(s); ++s.tick;
        advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); };
    const auto authority = [](const RuntimeWindowState& s) { std::ostringstream a; a << std::setprecision(17)
        << s.tick << '|' << s.economy.powerProduced << '|' << s.economy.powerUsed;
        for (const auto& o : s.world.getObjects()) { a << '|' << o.instanceId << ':' << o.type << ':' << o.owner << ':' << o.alive << ':' << o.health
            << ':' << o.mission << ':' << o.x << ':' << o.y << ':' << o.level << ':' << o.onBridge << ':' << o.subCell
            << ':' << o.airborne << ':' << o.hasRenderPosition << ':' << o.renderMoveActive << ':' << o.renderX << ':' << o.renderY << ':' << o.renderLevel
            << ':' << o.bioReactorAnimationStartTick << ':' << o.bioReactorAnimationPoweredStartTick << ':' << o.powerAnimationPausedAtTick << ':' << o.powerAnimationPausedTicks;
            for (int id : o.transportPassengerIds) a << ',' << id;
        }
        for (const auto& [id,host] : s.pendingTransportTargets) a << "|pending:" << id << ':' << host;
        return a.str(); };
    struct Picture { std::vector<std::uint32_t> roi; std::string image; int frame = -1; };
    int totalArrivals = 0, totalReplay = 0, totalPairs = 0, totalSaves = 0, totalLegacy = 0, totalUnloaded = 0;
    for (int hz : {30,60}) {
        label = "stock-YAPOWR-" + std::to_string(hz); stage = "empty native fixture";
        auto storage = make(hz); auto& s = *storage; step(s);
        const auto host = [&]() { return findObjectById(s,1); };
        const auto power = [&](RuntimeWindowState& q, int count) { const auto* h = findObjectById(q,1); if (!h) return false;
            const auto pips = runtimeCargoPipDisplayForObject(q,*h); const int expected = 150 + count * 100;
            return int(h->transportPassengerIds.size()) == count && q.economy.powerProduced == expected
                && runtimeBuildingPowerOutput(*stock,*h) == expected && pips.available && pips.totalUnits == 5 && pips.filledUnits == count
                && runtimeCampaignPowerNetForHouse(q,q.economy.owner) == expected - q.economy.powerUsed
                && runtimeSuperWeaponContext(q).ownerPowered(q.economy.owner) == (expected >= q.economy.powerUsed); };
        const auto record = [&](const RuntimeWindowState& q, const std::string& name, int id) {
            const auto* h = findObjectById(q,1); const auto* o = findRuntimeObjectByIdIncludingInactive(q,id);
            if (!h || !o) return false; const auto pending = q.pendingTransportTargets.find(id);
            events << label << ',' << name << ',' << q.tick << ',' << id << ',' << h->transportPassengerIds.size() << ',' << q.economy.powerProduced
                << ',' << h->bioReactorAnimationStartTick << ',' << h->bioReactorAnimationPoweredStartTick << ',' << o->alive << ',' << o->mission << ',' << o->x << ',' << o->y << ','
                << (pending == q.pendingTransportTargets.end() ? -1 : pending->second) << '\n'; return bool(events); };
        const auto capture = [&](RuntimeWindowState& q, const std::string& name, bool damaged, int age, Picture& picture) {
            auto* h = findMutableObjectById(q,1); if (!h) return fail("render host missing");
            const int savedTick = q.tick, savedHealth = h->health;
            const int savedStamp = h->bioReactorAnimationStartTick, savedPoweredStamp = h->bioReactorAnimationPoweredStartTick;
            const auto savedSelection = q.selectedObjectIds;
            // Fixed presentation phase aligns original damage fires and the A/B replay.
            // No simulation, save or authority comparison runs until all fields are restored.
            if (age >= 0) { q.tick = age;
                if (savedStamp >= 0) h->bioReactorAnimationStartTick = 0;
                if (savedPoweredStamp >= 0) h->bioReactorAnimationPoweredStartTick = 0;
            }
            h->health = damaged ? std::max(1,stock->strength / 4) : stock->strength; q.selectedObjectIds.clear();
            const int count = int(h->transportPassengerIds.size()), stamp = h->bioReactorAnimationStartTick, renderTick = q.tick;
            const int poweredStamp = h->bioReactorAnimationPoweredStartTick;
            std::vector<std::uint32_t> pixels;
            const auto stats = drawRuntimeStateOffscreenAtCell(q,{12,12},640,420,error,"",false,false,-1,&pixels);
            q.tick = savedTick; h = findMutableObjectById(q,1); h->health = savedHealth;
            h->bioReactorAnimationStartTick = savedStamp; h->bioReactorAnimationPoweredStartTick = savedPoweredStamp;
            q.selectedObjectIds = savedSelection;
            if (!stats || pixels.size() != 640U * 420U) return fail("actual offscreen draw: " + error);
            const std::string expected = damaged ? (count == 5 ? "YAPOWR_BD" : "YAPOWR_AD") : (count == 5 ? "YAPOWR_B" : "YAPOWR_A");
            if (stats->firstBuildingArtAnimationImage != expected || stats->buildingArtAnimationLayers <= 0)
                return fail("original active image missing: actual=" + stats->firstBuildingArtAnimationImage + " expected=" + expected);
            picture.image = stats->firstBuildingArtAnimationImage; picture.frame = stats->firstBuildingArtAnimationFrame; picture.roi.clear();
            // Original A/B source x53..116,y61..88. This lower chamber excludes every original C/CD lamp.
            for (int y = 200; y < 228; ++y) for (int x = 286; x < 350; ++x) picture.roi.push_back(pixels[std::size_t(y) * 640 + x] & 0x00ffffffU);
            images << label << ',' << name << ',' << renderTick << ',' << count << ',' << (damaged ? std::max(1,stock->strength / 4) : stock->strength)
                << ',' << stamp << ',' << (stamp < 0 ? -1 : renderTick - stamp) << ',' << picture.image << ',' << picture.frame
                << ",286,200,64,28," << hash(picture.roi) << ',' << poweredStamp << ',' << stats->buildingArtAnimationMissing
                << ',' << savedTick << ',' << savedStamp << ',' << savedPoweredStamp << ',' << (age >= 0) << '\n'; return bool(images); };
        const auto addFoot = [&](RuntimeWindowState& q, int id) { WorldObjectInstance o; o.instanceId = id; o.kind = WorldObjectKind::Infantry;
            o.type = "E1"; o.owner = q.economy.owner; o.alive = true; o.health = footRule->strength; o.x = 8; o.y = 10;
            o.cell = runtimeCellKey(o.x,o.y); o.mission = "Guard"; q.world.accessObjects().push_back(o); initializeRuntimeNavigation(q); };
        const auto enter = [&](RuntimeWindowState& q, int id, int before) {
            auto* h = findMutableObjectById(q,1); if (!h || !power(q,before)) return fail("pre-entry occupancy/power mismatch");
            const int oldStamp = h->bioReactorAnimationStartTick, oldPoweredStamp = h->bioReactorAnimationPoweredStartTick; q.selectedObjectIds = {id};
            if (runtimeTargetCursorIntent(q,*h) != RuntimeTargetCursorIntent::InfantryAbsorb || !queueRuntimeTargetAbility(q,*h)
                || !q.pendingTransportTargets.contains(id) || q.pendingTransportTargets.at(id) != 1)
                return fail("real absorption cursor/queue rejected actor=" + std::to_string(id));
            if (!power(q,before) || findObjectById(q,1)->bioReactorAnimationStartTick != oldStamp
                || findObjectById(q,1)->bioReactorAnimationPoweredStartTick != oldPoweredStamp || completeRuntimeTransportEntry(q,id)) return fail("queued/proximity intent changed actual count/stamp");
            if (!record(q,"queued-no-replay",id)) return fail("event output failed");
            bool arrived = false;
            for (int n = 0; n < hz * 20; ++n) {
                step(q); h = findMutableObjectById(q,1); if (!h) return fail("host disappeared while entrant walked");
                if (int(h->transportPassengerIds.size()) == before + 1) { arrived = true; break; }
                if (!power(q,before) || h->bioReactorAnimationStartTick != oldStamp || h->bioReactorAnimationPoweredStartTick != oldPoweredStamp) return fail("pre-arrival count/power/stamps changed");
            }
            const auto* o = findRuntimeObjectByIdIncludingInactive(q,id); h = findMutableObjectById(q,1);
            if (!arrived || !o || !h || !power(q,before + 1) || h->bioReactorAnimationStartTick != q.tick || h->bioReactorAnimationPoweredStartTick != q.tick
                || o->alive || o->health != footRule->strength || o->mission != "InsideTransport" || q.pendingTransportTargets.contains(id)
                || std::find(h->transportPassengerIds.begin(),h->transportPassengerIds.end(),id) == h->transportPassengerIds.end())
                return fail("actual arrival/stamp/containment failed actor=" + std::to_string(id) + " tick=" + std::to_string(q.tick));
            return record(q,"actual-arrival",id); };
        const auto checkpoint = [&](int count) {
            stage = "physical count-" + std::to_string(count) + " checkpoint";
            std::ostringstream baseText, combined, canonicalText; IniFile extension;
            if (!writeRuntimeStateToStream(s,baseText,error)) return fail("ordinary writer: " + error);
            combined << baseText.str();
            if (!writeRuntimeMultiplayerSnapshotExtension(s,combined,error) || !extension.loadFromString(combined.str())
                || !writeRuntimeMultiplayerSnapshotCanonicalIni(extension,canonicalText,error)) return fail("canonical authority writer: " + error);
            const auto basePath = root / (label + "-count-" + std::to_string(count) + "-ordinary14.ini");
            const auto exactPath = root / (label + "-count-" + std::to_string(count) + "-canonical-image.ini");
            const auto write = [](const std::filesystem::path& path,const std::string& text) { std::ofstream f(path,std::ios::binary); f << text; f.close(); return bool(f); };
            if (!write(basePath,baseText.str()) || !write(exactPath,canonicalText.str())) return fail("actual checkpoint write failed");
            IniFile base, exact;
            if (!base.loadFromFile(basePath.generic_string()) || !exact.loadFromFile(exactPath.generic_string())
                || base.getValue("RuntimeSave","Version","") != "14") return fail("actual checkpoint disk parse failed");
            const auto restore = [&](const IniFile& ini, bool precise) {
                auto q = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*q,s,false); RuntimeSnapshotActiveStateScope active(*q);
                if (!materializeRuntimeLoadStateFromIni(*q,ini,error) || (precise && !materializeRuntimeMultiplayerSnapshotExtension(*q,exact,error))) return std::unique_ptr<RuntimeWindowState>{};
                q->runtimeSoundsEnabled = q->runtimePreferencePersistenceEnabled = q->visionEnabled = false; q->runtimeSuppressPlayerAudioFeedback = true; return q; };
            auto ordinary = restore(base,false), precise = restore(base,true);
            if (!ordinary || !precise) return fail("physical ordinary/authority load: " + error);
            if (authority(*ordinary) != authority(s) || authority(*precise) != authority(s)) return fail("loaded stamp/manifest/body/power authority subset mismatch: live=" + authority(s) + " ordinary=" + authority(*ordinary) + " precise=" + authority(*precise));
            const auto framesEqual = [&]() { Picture a,b,c;
                if (!capture(s,"paired-live",false,-1,a) || !capture(*ordinary,"paired-ordinary",false,-1,b)
                    || !capture(*precise,"paired-canonical",false,-1,c)) return false;
                return a.image == b.image && a.image == c.image && a.frame == b.frame && a.frame == c.frame && a.roi == b.roi && a.roi == c.roi; };
            if (!framesEqual()) return fail("restored actual frame/ROI mismatch");
            const int savedTick = s.tick;
            for (int n = 0; n < 12; ++n) { step(s); step(*ordinary); step(*precise);
                if (authority(*ordinary) != authority(s) || authority(*precise) != authority(s)) return fail("paired live stamp/manifest/body/power continuation mismatch tick=" + std::to_string(s.tick)); ++totalPairs; }
            if (!framesEqual()) return fail("paired endpoint actual frame/ROI mismatch");
            saves << label << ',' << count << ",ordinary14," << savedTick << ',' << basePath.generic_string() << ',' << basePath.generic_string() << ",1,14,12," << textHash(authority(s)) << '\n';
            saves << label << ',' << count << ",ordinary14+canonical-image," << savedTick << ',' << exactPath.generic_string() << ',' << basePath.generic_string() << ",1,14,12," << textHash(authority(s)) << '\n';
            totalSaves += 2;
            if (count == 3) {
                stage = "legacy optional field memory compatibility"; std::istringstream input(baseText.str()); std::ostringstream legacyText;
                std::string line; bool objects = false; int stripped = 0;
                while (std::getline(input,line)) { if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty() && line.front() == '[') objects = line == "[Objects]";
                    const auto equal = line.find('=');
                    if (objects && equal != std::string::npos && line.substr(0,equal) != "Count") {
                        const auto fields = splitRuntimeSaveFields(line.substr(equal + 1));
                        if (fields.size() != 64) return fail("new ordinary object record must carry exactly optional fields63/64");
                        line = line.substr(0,equal + 1); for (std::size_t i = 0; i < 62; ++i) { if (i) line += '|'; line += fields[i]; } ++stripped;
                    } legacyText << line << '\n'; }
                IniFile legacy; if (!stripped || !legacy.loadFromString(legacyText.str())) return fail("legacy memory image creation failed");
                auto old = restore(legacy,false); if (!old) return fail("legacy optional-tail load: " + error);
                for (const auto& o : old->world.getObjects()) if (o.bioReactorAnimationStartTick != -1 || o.bioReactorAnimationPoweredStartTick != -1) return fail("missing optional epochs did not retain legacy -1");
                const auto* oh = findObjectById(*old,1);
                if (!power(*old,3) || runtimeBioReactorAnimationTick(stock,*oh,"activeanim",old->tick) != old->tick
                    || runtimeBioReactorAnimationTick(stock,*oh,"activeanim",old->tick,true,false,old->tick) != old->tick)
                    return fail("legacy original global animation clock changed");
                addFoot(*old,100); if (!enter(*old,100,3)) return false;
                Picture restarted; if (!capture(*old,"legacy-next-real-arrival",false,0,restarted) || restarted.frame != 2)
                    return fail("next real arrival did not restart legacy animation"); ++totalLegacy;
            }
            if (count == 5) {
                stage = "real absorber unload"; const int oldCount = int(ordinary->world.getObjects().size()); step(*ordinary);
                const int at = ordinary->tick;
                auto* unloadHost = findMutableObjectById(*ordinary,1);
                if (!unloadHost || unloadRuntimeTransport(*ordinary,*unloadHost) != 5 || !power(*ordinary,0) || findObjectById(*ordinary,1)->bioReactorAnimationStartTick != at || findObjectById(*ordinary,1)->bioReactorAnimationPoweredStartTick != at)
                    return fail("actual five-passenger unload count/power/replay mismatch");
                int released = 0; for (const auto& o : ordinary->world.getObjects()) if (o.instanceId >= 2 && o.instanceId <= 6
                    && o.alive && o.health == footRule->strength && o.mission != "InsideTransport") ++released;
                if (released != 5 || int(ordinary->world.getObjects().size()) != oldCount) return fail("unload did not preserve original five living occupants");
                totalUnloaded += released; record(*ordinary,"actual-unload-five",1);
            }
            return bool(saves);
        };
        std::array<std::vector<std::uint32_t>,6> normal, damaged;
        const auto captureLevel = [&](int count) { Picture n,d,w;
            if (!capture(s,"occupancy-normal",false,0,n) || !capture(s,"occupancy-damaged",true,0,d) || !capture(s,"occupancy-warm",false,0,w)) return false;
            if (n.frame != 2 || d.frame != 10 || n.roi != w.roi || n.roi == d.roi) return fail("authored start frame/warm/damaged image contract failed count=" + std::to_string(count));
            if (count && (n.roi == normal[count - 1] || d.roi == damaged[count - 1])) return fail("actual lower furnace ROI did not change for next admitted count=" + std::to_string(count));
            normal[count] = n.roi; damaged[count] = d.roi; return true; };
        if (!power(s,0) || host()->bioReactorAnimationStartTick != -1 || host()->bioReactorAnimationPoweredStartTick != -1 || !captureLevel(0)) return fail("empty original reactor contract failed");
        // Pure copied-field clock contracts; no live occupant, phase or manifest mutation.
        auto clock = *host(); clock.bioReactorAnimationStartTick = 100; clock.bioReactorAnimationPoweredStartTick = 80;
        clock.poweredOperational = true; clock.powerAnimationPausedAtTick = -1; clock.powerAnimationPausedTicks = 20;
        if (runtimeBioReactorAnimationTick(stock,clock,"activeanim",130,false,false,130) != 30
            || runtimeBioReactorAnimationTick(stock,clock,"activeanim",130,true,false,130) != 30)
            return fail("copied raw/effective clock elapsed contract");
        clock.poweredOperational = false; clock.powerAnimationPausedAtTick = 130;
        if (runtimeBioReactorAnimationTick(stock,clock,"activeanim",200,true,false,200) != 30)
            return fail("copied powered clock outage freeze contract");
        clock.poweredOperational = true; clock.powerAnimationPausedAtTick = -1; clock.powerAnimationPausedTicks = 90;
        if (runtimeBioReactorAnimationTick(stock,clock,"activeanim",200,true,false,200) != 30)
            return fail("copied powered clock resume continuity contract");
        if (runtimeBioReactorAnimationTick(stock,clock,"activeanim",207,true,false,207) != 37)
            return fail("copied powered clock resumed progression contract");
        if (hz == 30 && !checkpoint(0)) return false;
        for (int count = 1; count <= 5; ++count) {
            stage = "real arrival count-" + std::to_string(count); addFoot(s,count + 1);
            if (!enter(s,count + 1,count - 1) || !captureLevel(count)) return false; ++totalArrivals;
            const auto* h = host(); const int stamp = h->bioReactorAnimationStartTick, poweredStamp = h->bioReactorAnimationPoweredStartTick;
            if (runtimeBioReactorAnimationTick(stock,*h,"activeanim",stamp + 7) != 7
                || runtimeBioReactorAnimationTick(stock,*h,"activeanimtwo",stamp + 7) != 7
                || runtimeBioReactorAnimationTick(stock,*h,"idleanim",stamp + 7) != stamp + 7
                || runtimeBioReactorAnimationTick(nullptr,*h,"activeanim",stamp + 7) != stamp + 7
                || runtimeBioReactorAnimationTick(stock,*h,"activeanim",stamp + 7,true,false,stamp + 7) != 7
                || runtimeBioReactorAnimationTick(stock,*h,"activeanim",stamp + 7,false,true,stamp + 7) != 7)
                return fail("relative dual clocks widened beyond absorber A/B or failed powered replay");
            stage = "same count natural loop advance"; bool advanced = false; Picture start,next;
            if (!capture(s,"real-start",false,-1,start) || start.frame != 2) return fail("arrival instant is not original replay first frame");
            for (int n = 0; n < hz * 2; ++n) { step(s);
                if (!power(s,count) || host()->bioReactorAnimationStartTick != stamp || host()->bioReactorAnimationPoweredStartTick != poweredStamp) return fail("unchanged occupancy restamped or lost power");
                if (!capture(s,"natural-loop",false,-1,next)) return false;
                if (next.frame != start.frame) { advanced = true; break; }
            }
            if (!advanced || next.roi == start.roi) return fail("original A/B same-count animation did not naturally advance"); ++totalReplay;
            if (hz == 30 && (count == 3 || count == 5) && !checkpoint(count)) return false;
        }
        stage = "individual authoritative script-removal decrements";
        for (int count = 4; count >= 0; --count) { step(s); const int id = host()->transportPassengerIds.back(), at = s.tick;
            RuntimeObjectExitRequest request; request.objectId = id; request.kind = RuntimeObjectExitKind::ScriptRemoved;
            request.tick = at; request.mission = "ScriptRemoved"; request.reason = "bioreactor individual occupancy transition smoke";
            if (queueRuntimeObjectExit(s,request) != RuntimeObjectExitQueueResult::Queued) return fail("real individual exit queue rejected");
            if (!drainRuntimeObjectExitQueue(s)) return fail("real individual exit drain rejected"); rebuildRuntimeEconomyPower(s);
            const auto* o = findRuntimeObjectByIdIncludingInactive(s,id); Picture n,d;
            if (!o || o->alive || !power(s,count) || host()->bioReactorAnimationStartTick != at || host()->bioReactorAnimationPoweredStartTick != at
                || !capture(s,"script-removal-normal",false,0,n) || !capture(s,"script-removal-damaged",true,0,d)
                || n.roi != normal[count] || d.roi != damaged[count]) return fail("actual decrement count/power/stamp/fixed-phase image mismatch count=" + std::to_string(count));
            if (!record(s,"script-removal-not-live-unload",id)) return fail("event output failed");
        }
        const auto sheet = [&](const std::string& name,const auto& levels) { std::vector<std::uint32_t> pixels(384U * 28U);
            for (int count = 0; count <= 5; ++count) for (int y = 0; y < 28; ++y) for (int x = 0; x < 64; ++x)
                pixels[std::size_t(y) * 384 + count * 64 + x] = levels[count][std::size_t(y) * 64 + x];
            return writeRuntimeSmokeBmp((root / (label + "-" + name + ".bmp")).generic_string(),pixels.data(),384,28,error); };
        if (!sheet("normal-count0-to5",normal) || !sheet("damaged-count0-to5",damaged)) return fail("actual contact sheet write failed: " + error);
        cases << label << ',' << hz << ",5,5,5,6,6,5," << (hz == 30 ? 3 : 0) << ',' << (hz == 30 ? 3 : 0)
            << ',' << (hz == 30 ? 36 : 0) << ',' << (hz == 30 ? 1 : 0) << ',' << (hz == 30 ? 5 : 0) << ",PASS\n";
        std::cout << "Bio reactor transition case passed: " << label << " actual arrivals5, queued unchanged5, decrements5, visual levels6+6, replay5\n";
    }
    cases.close(); events.close(); images.close(); saves.close();
    if (!cases || !events || !images || !saves || totalArrivals != 10 || totalReplay != 10 || totalPairs != 36
        || totalSaves != 6 || totalLegacy != 1 || totalUnloaded != 5) return fail("finite evidence totals/output mismatch");
    std::cout << "Bio reactor occupancy transition smoke passed: 2 clocks,10 real arrivals,10 per-arrival replays,10 authoritative individual decrements,12 normal+12 damaged levels,6 physical files,36 live paired ticks,1 legacy memory entry,5 actual live unloaded,4 BMP\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
