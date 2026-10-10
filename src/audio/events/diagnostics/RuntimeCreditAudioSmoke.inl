bool runRuntimeCreditAudioSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Credit audio: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
    s.config = config; s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
    s.config.computerAiEnabled = false; s.config.multiplayerActive = false;
    s.config.noCooldown = false; s.config.startingCredits = 100000; s.config.startingUnits = 0;
    s.config.playerCountry = "Americans"; s.config.playerSlots.clear(); s.config.playerSlots.resize(2);
    for (std::size_t i = 0; i < 2; ++i) {
        auto& slot = s.config.playerSlots[i]; slot.enabled = true; slot.human = i == 0;
        slot.local = i == 0; slot.country = i == 0 ? "Americans" : "Russians";
        slot.team = "-"; slot.startPosition = static_cast<int>(i) + 1;
    }
    s.runtimePreferencePersistenceEnabled = false; s.runtimeSoundsEnabled = false;
    s.runtimeSoundVolume = 100; s.runtimeSoundEffectsEnabled = true;
    s.report = buildRuntimeStartupReport(s.config, &s.world, &s.theater, &s.art, &s.rules, &s.mapFacts);
    s.rulesLoaded = s.report.rulesLoaded; s.artLoaded = s.report.artLoaded; s.theaterLoaded = s.report.theaterLoaded;
    if (!s.report.success || !loadRuntimeSoundDatabase(s)) return fail("stock resources missing");
    initializeRuntimeNavigation(s); initializeRuntimeEconomy(s);
    if (!initializeRuntimeSkirmishStart(s)) return fail(s.economy.status);
    if (!deployRuntimeMcv(s,s.economy.skirmishStartObjectId,false)) return fail("manual opening deployment");
    s.triggers.bootstrapComplete = true;
    prepareRuntimeCreditAudio(s); snapRuntimeCreditAudio(s);
    const auto frame = [&](std::uint64_t ms) { advanceRuntimeCreditAudio(s, ms); };
    const auto creditRecord = [&](const std::string& section) {
        if (s.runtimeSoundPlaybackHistory.empty()) return false;
        const auto& r = s.runtimeSoundPlaybackHistory.back();
        return r.section == section && r.playable && !r.eva && r.status == "resolved"
            && r.sample == (section == "CreditUp" ? "ucredup" : "ucreddn")
            && r.volumePercent == (section == "CreditUp" ? 20 : 30)
            && runtimeWavDurationTicks(r.path, 1000) > 1;
    };
    int before = s.runtimeSoundEvents; frame(1000);
    if (s.runtimeSoundEvents != before) return fail("starting money made a sound");

    const auto* catalog = findRuntimeGameplayCatalogItem(s, s.economy.owner, "GAPOWR");
    if (!catalog) return fail("stock production catalog missing");
    const auto power = *catalog;
    const int initialCredits = s.economy.credits;
    if (!dispatchRuntimeQueueProductionFromUi(s, power)) return fail("building order failed");
    for (int i = 0; i < 60 && s.economy.credits == initialCredits; ++i) {
        ++s.tick; advanceRuntimeProductionEconomy(s);
    }
    frame(2000);
    if (s.economy.credits >= initialCredits || !creditRecord("CreditDown")) return fail("real construction spending had no stock tick");
    const int spentCredits = s.economy.credits;
    if (!dispatchRuntimeBuildButtonSecondaryActionFromUi(s, power)) return fail("production hold failed");
    if (s.economy.credits == spentCredits && !dispatchRuntimeBuildButtonSecondaryActionFromUi(s, power))
        return fail("production refund failed");
    frame(4000);
    if (s.economy.credits <= spentCredits || !creditRecord("CreditUp")) return fail("real cancellation refund had no stock tick");
    std::cout << "Credit audio production: actual build debit and cancel refund use native down/up samples\n";

    const int originalTickHz = s.config.fixedTickHz;
    for (int hz : {15, 30, 60}) {
        s.config.fixedTickHz = hz; s.economy.credits = 1000; snapRuntimeCreditAudio(s); frame(1000);
        if (!spendRuntimeCredits(s, 10)) return fail("spending fixture");
        frame(2000); if (!creditRecord("CreditDown")) return fail("expense direction");
        depositRuntimeCredits(s, 25); frame(4000);
        if (!creditRecord("CreditUp")) return fail("deposit direction");
        s.economy.credits -= 10; frame(6000);
        if (!creditRecord("CreditDown")) return fail("direct balance adjustment was missed");
        before = s.runtimeSoundEvents;
        spendRuntimeCredits(s, 0); depositRuntimeCredits(s, 0); frame(8000);
        if (spendRuntimeCredits(s, 1000000)) return fail("unaffordable spending accepted");
        frame(9000); if (s.runtimeSoundEvents != before) return fail("unchanged balance made a sound");
        s.economy.credits = runtimeMaximumCreditBalance; snapRuntimeCreditAudio(s);
        depositRuntimeCredits(s, 9999); frame(10000);
        if (s.runtimeSoundEvents != before) return fail("capped deposit made a sound");

        s.economy.credits = 1000; snapRuntimeCreditAudio(s);
        s.runtimeSoundEffectsEnabled = false; depositRuntimeCredits(s, 10); frame(11000);
        s.runtimeSoundEffectsEnabled = true; frame(12000);
        s.runtimeSoundVolume = 0; spendRuntimeCredits(s, 10); frame(13000);
        s.runtimeSoundVolume = 100; frame(14000);
        if (s.runtimeSoundEvents != before) return fail("muted changes replayed after unmuting");

        const auto owner = s.economy.owner; const int balance = s.economy.credits;
        s.remoteHumanHouseScopeActive = true; s.economy.owner = "Remote"; s.economy.credits = 25;
        frame(15000); s.economy.credits = 2; frame(16000);
        s.remoteHumanHouseScopeActive = false; s.economy.owner = owner; s.economy.credits = balance; frame(17000);
        if (s.runtimeSoundEvents != before || s.creditAudio.balance != balance) return fail("another house leaked money feedback");
        s.economy.owner = "OtherView"; s.economy.credits = 10000; frame(18000);
        s.economy.owner = owner; s.economy.credits = balance; frame(19000);
        if (s.runtimeSoundEvents != before) return fail("owner switch sounded like income");

        snapRuntimeCreditAudio(s); const int rapidBefore = s.runtimeSoundEvents;
        for (int ms = 0; ms < 1000; ms += 5) {
            spendRuntimeCredits(s, 1); frame(20000 + ms);
        }
        const int rapidCount = s.runtimeSoundEvents - rapidBefore;
        if (rapidCount < 2 || rapidCount > 1 + 1000 / s.creditAudio.intervalMs[1])
            return fail("rapid spending stacked or lost continuous feedback");
        depositRuntimeCredits(s, 20); frame(20999); frame(23000);
        if (!creditRecord("CreditUp")) return fail("rapid direction reversal lost the final income cue");
        before = s.runtimeSoundEvents; frame(24000);
        if (s.runtimeSoundEvents != before) return fail("money stopped but its sound kept repeating");
        spendRuntimeCredits(s, 1); frame(24001);
        depositRuntimeCredits(s, 1); frame(24002);
        s.runtimeMenuOpen = true; frame(24003); s.runtimeMenuOpen = false;
        before = s.runtimeSoundEvents; frame(26000);
        if (s.runtimeSoundEvents != before) return fail("paused pending cue replayed on resume");
        std::cout << "Credit audio hz=" << hz << " increase/decrease/zero/cap/mute/local-owner/rapid-stop=ok rate=" << rapidCount << "/sec\n";

        for (int direction : {1,-1}) {
            s.economy.credits=10000; snapRuntimeCreditAudio(s); frame(30000);
            before=s.runtimeSoundEvents;
            if(direction>0)depositRuntimeCredits(s,1000); else if(!spendRuntimeCredits(s,1000))return fail("lump-sum spending");
            const int committed=s.economy.credits;
            const auto hash=s.world.deterministicSimulationStateHash();
            int previousCount=before;
            std::uint64_t lastSound=0;
            const int interval=s.creditAudio.intervalMs[direction>0 ? 0 : 1];
            // No further economic transactions occur while this burst plays.
            for(std::uint64_t ms=31000;ms<=35000;ms+=10) {
                frame(ms);
                if(s.runtimeSoundEvents!=previousCount) {
                    if(s.runtimeSoundEvents!=previousCount+1 || (lastSound && ms-lastSound<static_cast<std::uint64_t>(interval))
                        || !creditRecord(direction>0 ? "CreditUp" : "CreditDown"))return fail("lump-sum tick spacing/sample");
                    previousCount=s.runtimeSoundEvents;lastSound=ms;
                }
            }
            const int ticks=s.runtimeSoundEvents-before;
            if(ticks<10 || ticks>24 || s.creditAudio.pendingDirection!=0)return fail("one transaction did not produce a bounded continuous burst");
            frame(50000);
            if(s.runtimeSoundEvents!=previousCount || s.economy.credits!=committed
                || s.world.deterministicSimulationStateHash()!=hash)return fail("burst changed economy or continued after settling");
            std::cout << "Credit audio lump sum: Hz=" << hz << " amount=" << direction*1000 << " ticks=" << ticks << " stop/spacing/authority=ok\n";
        }
        // Muting and menus also cut off an already-playing burst.
        s.economy.credits=10000; snapRuntimeCreditAudio(s); depositRuntimeCredits(s,1000);frame(60000);
        before=s.runtimeSoundEvents; s.runtimeMenuOpen=true;frame(60010);s.runtimeMenuOpen=false;frame(60100);
        if(s.runtimeSoundEvents!=before || s.creditAudio.pendingDirection!=0)return fail("menu did not cancel a money burst");
        depositRuntimeCredits(s,1000);frame(62000);before=s.runtimeSoundEvents;
        s.runtimeSoundEffectsEnabled=false;frame(62010);s.runtimeSoundEffectsEnabled=true;frame(62100);
        if(s.runtimeSoundEvents!=before || s.creditAudio.pendingDirection!=0)return fail("mute did not cancel a money burst");
    }
    // The rate probe changes presentation settings without rebuilding world
    // subsystems; return to their original clock before exercising a full save.
    s.config.fixedTickHz = originalTickHz;
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(s, saved, error)) return fail("save: " + error);
    IniFile ini; if (!ini.loadFromString(saved.str())) return fail("saved INI");
    auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, s.visionEnabled);
    restored->creditAudio = s.creditAudio; restored->creditAudio.balance += 1000;
    restored->creditAudio.pendingDirection = 1;
    if (!materializeRuntimeLoadStateFromIni(*restored, ini, error)) return fail("load: " + error);
    restored->runtimeSuppressPlayerAudioFeedback = false;
    restored->runtimeSoundEffectsEnabled = true; restored->runtimeSoundVolume = 100;
    before = restored->runtimeSoundEvents;
    advanceRuntimeCreditAudio(*restored, 50000);
    if (restored->runtimeSoundEvents != before || restored->creditAudio.balance != restored->economy.credits
        || restored->creditAudio.pendingDirection != 0) return fail("loading sounded like a money change");
    const auto worldHash = restored->world.deterministicSimulationStateHash();
    const int credits = restored->economy.credits;
    advanceRuntimeCreditAudio(*restored, 51000);
    if (worldHash != restored->world.deterministicSimulationStateHash() || restored->economy.credits != credits)
        return fail("UI audio changed gameplay");
    std::cout << "Credit audio passed: stock CreditTicks samples and volumes, real build/refund, lump-sum bursts, both balance directions, 15/30/60 Hz, rate control, mute, owner isolation and full save/load; playback spacing="
        << s.creditAudio.intervalMs[0] << '/' << s.creditAudio.intervalMs[1] << "ms\n";
    return true;
}
