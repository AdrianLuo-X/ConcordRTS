int testRandomMapPeerRecreation(const fs::path& root) {
    RuntimeConfig host;
    host.localDataRootPath="local_data"; host.resourceRootPath="RedAlert2";
    RuntimeConfig peer=host; peer.localDataRootPath=(root/"client").string();
    fs::create_directories(peer.localDataRootPath);
    std::string error;
    const auto fail=[&](const std::string& why){std::cerr<<"Random map LAN: "<<why<<'\n';return 1;};
    const RuntimeCampaignDataSourceOptions source{host.resourceRootPath,host.localDataRootPath,"","yr"};
    for (const auto& climate:{std::string("TEMPERATE"),std::string("SNOW")}) {
        std::string text;
        if (!resolveRuntimeCampaignTheaterSource(source,climate).readText(text,&error)
            || !writeFile(fs::path(peer.localDataRootPath)/(climate=="TEMPERATE"?"temperatmd.ini":"snowmd.ini"),text)) return fail(error);
    }
    std::string text;
    if (!resolveRuntimeCampaignIniSource(source,RuntimeCampaignIniKind::Rules).readText(text,&error)
        || !writeFile(fs::path(peer.localDataRootPath)/"rulesmd.ini",text)) return fail(error);
    fs::copy_file(fs::path(host.localDataRootPath)/"resources/mix_exports/expandmd01/rmgmd.ini",
        fs::path(peer.localDataRootPath)/"rmgmd.ini",fs::copy_options::overwrite_existing);
    for (int shape:{0,3,4}) {
        RuntimeRandomMapOptions options;
        options.environment=shape; options.theater=shape%2; options.players=8;
        options.resources=2; options.seed=0x914150U+shape; options.name="联机地图 "+std::to_string(shape);
        RuntimeRandomMapResult map; fs::path hostPath;
        if (!runtimeGenerateRandomMap(host,options,map,error)
            || !runtimeStoreRandomMap(map,root/"host",hostPath,error)) return fail(error);
        RuntimeMultiplayerMatchMap roomMap; roomMap.canonicalId="multimd/"+map.fileName;
        RuntimeMultiplayerContentComponent component;
        if (!loadRuntimeMultiplayerContentComponentFile(hostPath,roomMap.canonicalId,component,error)
            || !runtimeMultiplayerContentFingerprint({component},roomMap.contentFingerprint,error)) return fail(error);
        fs::path resolved;
        if (fs::exists(runtimeRandomMapCacheDirectory(peer)/map.fileName)) return fail("peer fixture already has this map");
        if (!resolveRuntimeMultiplayerMenuMap(peer,roomMap,resolved,error)) return fail(error);
        std::ifstream input(resolved,std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(input)),{});
        if (bytes!=map.iniText || resolved.parent_path()!=runtimeRandomMapCacheDirectory(peer)) return fail("peer did not create the same battlefield");
        roomMap.contentFingerprint^=1U; const auto original=resolved;
        if (resolveRuntimeMultiplayerMenuMap(peer,roomMap,resolved,error) || resolved!=original) return fail("different host map accepted");
        std::cout<<"PASS missing peer map recreated: shape="<<shape<<" players=8\n";
    }
    std::cout<<"Random map LAN: named eight-player maps recreated from room identity; map mismatch rejected.\n";
    return 0;
}
