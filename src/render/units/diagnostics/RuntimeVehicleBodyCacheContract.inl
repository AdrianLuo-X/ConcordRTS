bool runRuntimeVehicleBodyCacheContractCheck(std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "vehicle pose cache: " + message;
        return false;
    };
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    constexpr int normalCount = 1024;
    constexpr int hotCount = 128;
    constexpr int coldCount = 8192;
    for (int index = 0; index < normalCount; ++index) {
        const std::string key = "normal-" + std::to_string(index);
        cache->voxelObjectsByType[key].image.pixels = {11, 22, 33};
        cache->stableVoxelCacheKeyByLookup[key + "|lookup"] = key;
    }
    int hotMisses = 0;
    std::unordered_set<std::string> oldPolicy;
    int oldPolicyHotMisses = 0;
    const auto visit = [&](const std::string& key, bool hot) {
        if (oldPolicy.size() >= 768U) oldPolicy.clear();
        if (oldPolicy.insert(key).second && hot) ++oldPolicyHotMisses;
        RuntimeVehicleBodyVoxelCacheDrawScope draw{*cache};
        auto [it, inserted] = cache->voxelObjectsByType.try_emplace(key);
        if (inserted) {
            it->second.image.pixels = {44, 55, 66};
            if (hot) ++hotMisses;
        }
        const std::string lookup = key + "|lookup";
        cache->stableVoxelCacheKeyByLookup.insert_or_assign(lookup, key);
        touchRuntimeVehicleBodyVoxelCache(*cache, it->second, key, lookup);
        return it->second.image.pixels == std::vector<unsigned char>{44, 55, 66};
    };
    for (int index = 0; index < coldCount; ++index) {
        if (!visit("hot-" + std::to_string(index % hotCount) + "|bodytilt=1,2", true)
            || !visit("cold-" + std::to_string(index) + "|bodytilt=1,2", false))
            return fail("cached raster data changed");
        if (cache->vehicleBodyPoseCacheEntries > 768
            || cache->voxelObjectsByType.size() > normalCount + 768U
            || cache->stableVoxelCacheKeyByLookup.size() > normalCount + 768U)
            return fail("raster or secondary cache exceeded its bound");
    }
    if (hotMisses != hotCount || oldPolicyHotMisses <= hotMisses
        || cache->vehicleBodyPoseUses.size() != 768U
        || cache->vehicleBodyPoseCacheEvictions != coldCount + hotCount - 768U)
        return fail("continuous cold poses displaced frequently used poses");
    for (int index = 0; index < normalCount; ++index) {
        const std::string key = "normal-" + std::to_string(index);
        const auto normal = cache->voxelObjectsByType.find(key);
        if (normal == cache->voxelObjectsByType.end()
            || normal->second.image.pixels != std::vector<unsigned char>{11, 22, 33}
            || !cache->stableVoxelCacheKeyByLookup.contains(key + "|lookup"))
            return fail("ordinary facing or its secondary lookup was evicted");
    }

    const std::string hotKey = "hot-0|bodytilt=1,2";
    const auto oldestBeforeCopy = cache->vehicleBodyPoseUses.begin()->first;
    auto copied = std::make_unique<RuntimeMapRenderCache>(*cache);
    auto& copiedEntry = copied->voxelObjectsByType.at(hotKey);
    for (int alias = 0; alias < 2048; ++alias) {
        const std::string lookup = hotKey + "|exact-scale=" + std::to_string(alias);
        copied->stableVoxelCacheKeyByLookup.insert_or_assign(lookup, hotKey);
        touchRuntimeVehicleBodyVoxelCache(*copied, copiedEntry, hotKey, lookup);
        if (copied->stableVoxelCacheKeyByLookup.size() != normalCount + 768U)
            return fail("alternate precise-scale aliases accumulated");
    }
    if (!cache->stableVoxelCacheKeyByLookup.contains(hotKey + "|lookup")
        || cache->vehicleBodyPoseUses.begin()->first != oldestBeforeCopy
        || copiedEntry.vehicleBodyPoseUse == cache->voxelObjectsByType.at(hotKey).vehicleBodyPoseUse)
        return fail("copy reused bookkeeping from the original cache");

    // Diagnostic cache bypass reconstructs the image while retaining the
    // value handle; repeated bypasses must not leave stale recency records.
    for (int attempt = 0; attempt < 1024; ++attempt) {
        const auto handle = copiedEntry.vehicleBodyPoseUse;
        copiedEntry = RuntimeVoxelCacheEntry{};
        copiedEntry.vehicleBodyPoseUse = handle;
        touchRuntimeVehicleBodyVoxelCache(*copied, copiedEntry, hotKey, {});
    }
    if (copied->vehicleBodyPoseUses.size() != 768U)
        return fail("cache bypass leaked recency records");

    // Eviction belongs to the end of the complete unit draw. All body/part
    // references, including the oldest pose, remain readable inside it.
    const std::string borrowedKey = copied->vehicleBodyPoseUses.begin()->second.rasterKey;
    const auto* borrowed = &copied->voxelObjectsByType.at(borrowedKey);
    {
        RuntimeVehicleBodyVoxelCacheDrawScope draw{*copied};
        for (int part = 0; part < 3; ++part) {
            const std::string key = "new-part-" + std::to_string(part) + "|bodytilt=2,3";
            auto& entry = copied->voxelObjectsByType[key];
            touchRuntimeVehicleBodyVoxelCache(*copied, entry, key, {});
        }
        if (copied->vehicleBodyPoseUses.size() != 771U
            || borrowed->image.pixels != std::vector<unsigned char>{44, 55, 66})
            return fail("an active body/part reference was invalidated");
    }
    if (copied->vehicleBodyPoseUses.size() != 768U
        || copied->voxelObjectsByType.contains(borrowedKey)
        || !cache->voxelObjectsByType.contains(borrowedKey))
        return fail("draw completion did not release only the oldest copied poses");

    std::cout << "[runtime-vehicle-body-cache] coldPoses=" << coldCount
        << " hotPoses=" << hotCount << " oldHotMisses=" << oldPolicyHotMisses
        << " hotMisses=" << hotMisses
        << " retained=" << cache->vehicleBodyPoseCacheEntries
        << " evictions=" << cache->vehicleBodyPoseCacheEvictions
        << " aliasesBounded=yes copyIndependent=yes referencesStable=yes\n";
    return true;
}
