// Asset-backed S35 oracle. Enabled only by the explicit loading-smoke opt-in;
// the normal product path never generates a second atlas or these diagnostics.
namespace {
// Owner-only test state is declared by the private consumer implementation.
std::uint64_t grandCannonCacheFingerprint(const RuntimeMapRenderCache& cache) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto bytes = [&](const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) { hash ^= p[i]; hash *= 1099511628211ULL; }
    };
    const auto value = [&](const auto& scalar) { bytes(&scalar, sizeof(scalar)); };
    const auto text = [&](const std::string& s) { value(s.size()); bytes(s.data(), s.size()); };
    const auto plane = [&](const auto& v) { value(v.size()); bytes(v.data(), v.size() * sizeof(v[0])); };
    std::vector<std::string> keys;
    for (const auto& [key, entry] : cache.voxelObjectsByType) { (void)entry; keys.push_back(key); }
    std::sort(keys.begin(), keys.end());
    value(keys.size());
    for (const auto& key : keys) {
        const auto& entry = cache.voxelObjectsByType.at(key);
        const auto& image = entry.image;
        text(key); text(entry.metadata.imageId); text(entry.metadata.vxlPath); text(entry.metadata.hvaPath);
        value(entry.loaded); value(entry.hasSprite); value(entry.facingBucket);
        value(entry.animationFrameIndex); value(entry.shadowSectionRequested); value(entry.shadowSectionApplied);
        value(entry.rasterFitScale); value(entry.projectedRangeX); value(entry.projectedRangeY);
        value(image.offsetX); value(image.offsetY); value(image.width); value(image.height);
        value(image.globalWidth); value(image.globalHeight);
        value(image.sceneDepthBoundsValid); value(image.sceneDepthBoundsTop); value(image.sceneDepthBoundsBottom);
        plane(image.pixels); plane(image.rawColorIndices); plane(image.ownerRemapMask);
        plane(image.lightLevels); plane(image.normalIndices); plane(image.normalBrightnessLevels);
        plane(image.vplSections); plane(image.depthLevels); plane(image.pixelMaterials); plane(entry.shadowPixels);
        value(image.paletteRemapStart); value(image.paletteRemapEnd); value(image.paletteRemapRangeFromHeader);
        value(image.fallbackOwnerRemapCount); value(image.zeroColorVoxelCount);
        value(image.vxlAlreadyVplResolved); value(image.lightLevelsApplied); value(image.remapLightApplied);
        value(image.opaqueBoundsWidth); value(image.opaqueBoundsHeight);
        value(image.topFacePixelCount); value(image.sideFacePixelCount); value(image.lowerFacePixelCount);
        value(image.pointSampleCount); value(image.faceSampleCount); value(image.exposedFaceCount);
        value(image.rasterPathStats.pointSamplesWritten); value(image.rasterPathStats.cuboidFacesRasterized);
        value(image.rasterPathStats.indexedCoveragePixelsResolved); value(image.rasterPathStats.supersampledPixelsResolved);
        value(image.rasterPathStats.triangleFacesRasterized); value(image.rasterPathStats.coverageZOverwrites);
        text(image.rasterFunctionActuallyCalled); text(image.rasterCacheKey); text(image.sourcePath);
    }
    const auto mapping = [&](const auto& map) {
        std::vector<std::string> names;
        for (const auto& [key, item] : map) { (void)item; names.push_back(key); }
        std::sort(names.begin(), names.end());
        value(names.size());
        for (const auto& name : names) { text(name); text(map.at(name)); }
    };
    mapping(cache.stableVoxelCacheKeyByLookup); mapping(cache.structureTurretBarrelImageByKey);
    value(cache.voxelFixedScaleByKey.size());
    return hash;
}

}
bool verifyRuntimeGrandCannonPrewarm(const RuntimeMapRenderContext& context, std::string& error) {
    struct VerifyScope {
        VerifyScope() { concord::grand_cannon::testing::verifying = true; }
        ~VerifyScope() { concord::grand_cannon::testing::verifying = false; concord::grand_cannon::testing::failure = 0; }
    } scope;
    try {
        auto reference = std::make_unique<RuntimeMapRenderCache>();
        const auto start = std::chrono::steady_clock::now();
        for (int bucket = 0; bucket < 32; ++bucket) {
            int count = 0;
            if (!prewarmRuntimeStructureTurretVoxelFacingForRender("GTGCAN", bucket, context, *reference, count, error) || count != 2)
                throw std::runtime_error("S35 original serial oracle could not produce every facing");
        }
        std::size_t expectedCuboidJobs = 0, expectedOwnerParts = 0;
        for (const auto& [key, entry] : reference->voxelObjectsByType) {
            (void)key;
            if (entry.facingBucket == 30) continue;
            if (entry.image.rasterFunctionActuallyCalled == "cuboid") ++expectedCuboidJobs;
            else ++expectedOwnerParts;
        }
        if (expectedCuboidJobs != 31 || expectedOwnerParts != 31)
            throw std::runtime_error("S35 fixture source is not the selected original cuboid-turret/point-barrel profile");
        const auto expected = grandCannonCacheFingerprint(*reference);
        const auto referenceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::clog << "S35_ORACLE images=" << reference->voxelObjectsByType.size()
            << " hash=" << expected << " elapsedMs=" << referenceMs << "\n";
        const auto fallback = [&] {
            auto target = std::make_unique<RuntimeMapRenderCache>();
            int count = 0;
            if (!prewarmRuntimeStructureTurretVoxelFacingForRender("GTGCAN", 30, context, *target, count, error) || count != 2)
                throw std::runtime_error("S35 default-facing fallback failed");
            target->grandCannonVoxelReadyMask = std::uint32_t{1} << 30;
            target->grandCannonVoxelPrewarmActive = true;
            return target;
        };
        std::uint64_t generation = 100;
        for (int repetition = 0; repetition < 3; ++repetition) for (int mode : {0, 1, 2, 4, -1}) {
            auto target = fallback();
            RuntimeGrandCannonPrewarmJob job(mode, ++generation);
            if (!job.prepare(context, *target, error)) throw std::runtime_error(error);
            const auto before = grandCannonCacheFingerprint(*target);
            {
                std::thread wrongOwner([&] { job.merge(*target, generation); });
                wrongOwner.join();
                if (grandCannonCacheFingerprint(*target) != before)
                    throw std::runtime_error("S35 foreign owner installed a result");
            }
            // Prepared tasks remain valid after the producer's source/VPL
            // copies disappear. Only byte payloads are used by the workers.
            target->voxelSourcesByImage.clear();
            target->voxelPaletteLookup.clear();
            job.merge(*target, generation + 1);
            if (grandCannonCacheFingerprint(*target) != before)
                throw std::runtime_error("S35 wrong-generation merge installed a result");
            job.finish(*target, generation);
            if (!job.error().empty() || !job.done() || target->grandCannonVoxelReadyMask != 0xffffffffU)
                throw std::runtime_error("S35 real atlas did not finish");
            const auto actual = grandCannonCacheFingerprint(*target);
            if (actual != expected) {
                std::clog << "S35_MISMATCH expected=" << expected << " actual=" << actual << " mode=" << mode << "\n";
                for (const auto& [key, entry] : reference->voxelObjectsByType) {
                    auto found = target->voxelObjectsByType.find(key);
                    if (found == target->voxelObjectsByType.end()) { std::clog << "missing key " << key << "\n"; continue; }
                    const auto& image = found->second.image;
                    if (image.pixels != entry.image.pixels || image.pixelMaterials != entry.image.pixelMaterials
                        || found->second.shadowPixels != entry.shadowPixels)
                        std::clog << "pixel mismatch " << key << "\n";
                }
                throw std::runtime_error("S35 original serial/candidate image or cache mismatch");
            }
            job.merge(*target, generation); job.finish(*target, generation);
            if (grandCannonCacheFingerprint(*target) != expected || job.metrics().installed != 62)
                throw std::runtime_error("S35 duplicate merge changed the cache");
            const auto m = job.metrics();
            if (m.workers != concord::grand_cannon::workerBudget(mode, std::thread::hardware_concurrency()))
                throw std::runtime_error("S35 configured worker measurement mismatch");
            if (m.submitted != expectedCuboidJobs || m.synchronousFallbacks != expectedOwnerParts || m.peakJobs > 4 || m.peakExecutorBytes > RuntimeGrandCannonPrewarmJob::ExecutorBytes)
                throw std::runtime_error("S35 real consumer did not use its bounded cuboid path");
            std::clog << "S35_COMPARE repeat=" << repetition << " requested=" << mode << " workers=" << m.workers
                << " hash=" << actual << " prepareMs=" << m.prepareMs << " waitMs=" << m.ownerWaitMs
                << " totalMs=" << m.elapsedMs << " peakJobs=" << m.peakJobs << " peakRunning=" << m.peakRunning
                << " peakBytes=" << m.peakExecutorBytes << " perWorkerPayloadMax=" << m.largestJobBytes
                << " atlasBudgetCharged=" << m.cacheBytes << "\n";
        }
        for (int scenario = 0; scenario < 5; ++scenario) {
            auto target = fallback();
            const auto original = grandCannonCacheFingerprint(*target);
            RuntimeGrandCannonPrewarmJob job(2, ++generation);
            if (scenario == 0) concord::grand_cannon::testing::failure = 1; // allocation after admission
            if (scenario == 1) concord::grand_cannon::testing::failure = 2; // worker exception
            if (scenario == 3) concord::grand_cannon::testing::failure = 3;
            if (scenario == 4) concord::grand_cannon::testing::failure = 4;
            const bool ready = job.prepare(context, *target, error);
            concord::grand_cannon::testing::failure = 0;
            if (scenario == 0 && ready) throw std::runtime_error("S35 allocation injection did not fail");
            if (scenario >= 2 && !ready) throw std::runtime_error(error);
            if (scenario == 2) job.stop(); // close with unclaimed results
            job.finish(*target, generation);
            job.merge(*target, generation);
            if (grandCannonCacheFingerprint(*target) != original || target->grandCannonVoxelReadyMask != (std::uint32_t{1} << 30))
                throw std::runtime_error("S35 failed/cancelled/closed/old-map result escaped to the live cache");
            std::clog << "S35_FAILURE scenario=" << scenario << " defaultFallbackPreserved=yes\n";
        }
        {
            auto unchanged = fallback();
            const auto hash = grandCannonCacheFingerprint(*unchanged);
            bool failed = false;
            concord::grand_cannon::testing::failure = 5;
            try { RuntimeGrandCannonPrewarmJob startup(4, ++generation); }
            catch (const std::bad_alloc&) { failed = true; }
            concord::grand_cannon::testing::failure = 0;
            if (!failed || grandCannonCacheFingerprint(*unchanged) != hash)
                throw std::runtime_error("S35 startup unwind did not preserve the fallback");
            std::clog << "S35_FAILURE startupAfterThreads defaultFallbackPreserved=yes joined=yes\n";
        }
        // A fresh session after stop/generation failure must still complete.
        auto target = fallback();
        RuntimeGrandCannonPrewarmJob replacement(1, ++generation);
        if (!replacement.prepare(context, *target, error)) throw std::runtime_error(error);
        replacement.finish(*target, generation);
        if (grandCannonCacheFingerprint(*target) != expected) throw std::runtime_error("S35 replacement map failed");
        std::clog << "S35_CONTRACT passed realImageCacheModes=5 repetitions=3 failureCases=6 replacementMap=yes\n";
        error.clear();
        return true;
    } catch (const std::exception& failure) {
        error = failure.what(); std::cerr << "S35_CONTRACT failed: " << error << "\n"; return false;
    }
}
