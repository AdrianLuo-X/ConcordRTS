if (const char* only = std::getenv("CONCORD_COMMAND_SMOKE_WORLD_AUDIO_CLOCK_ONLY"); only != nullptr) {
    if (std::string(only) != "1") { std::cerr<<"World audio clock: ONLY must be exactly 1\n"; return 1; }
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->runtimePreferencePersistenceEnabled=false;
    stock->report=buildRuntimeStartupReport(config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    stock->rulesLoaded=stock->report.rulesLoaded;stock->artLoaded=stock->report.artLoaded;
    stock->theaterLoaded=stock->report.theaterLoaded;stock->mapFactsLoaded=stock->report.map.loaded;
    if (!stock->report.success) { std::cerr<<"World audio clock: original asset startup failed\n"; return 1; }
    return runRuntimeWorldAudioClockSmoke(*stock,std::cout)?0:1;
}