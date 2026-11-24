
    if (imageId.empty()) {
        return nullptr;
    }

    std::vector<std::string> imageCandidates =
        resolveAuxiliarySpriteImageIdCandidates(imageId, context, fallbackArtType);
    if (imageCandidates.empty()) {
        imageCandidates.push_back(imageId);
    }
    const ArtType* auxiliaryArtType = context.art == nullptr
        ? nullptr
        : context.art->findArtType(imageId);
    if (auxiliaryArtType == nullptr) {
        auxiliaryArtType = fallbackArtType;
    }
    std::string candidateKey;
    for (const std::string& candidate : imageCandidates) {
        candidateKey += "|";
        candidateKey += toLowerAscii(candidate);
    }
    const std::string& mixExportsRoot =
        cachedRuntimeMixExportsRootForContext(context, cache);

    const std::string bytesCacheKey =
        toLowerAscii(imageId)
        + candidateKey
        + "|bytes|"
        + context.assetRootPath
        + "|"
        + context.structureRootPath
        + "|mix="
        + mixExportsRoot
        + "|theater="
        + (auxiliaryArtType != nullptr && auxiliaryArtType->theater
            ? "1"
            : "0");
    auto [bytesIt, bytesInserted] =
        cache.auxiliaryShpBytesByImage.try_emplace(bytesCacheKey);
    RuntimeAuxiliaryShpBytesCacheEntry& bytesEntry = bytesIt->second;
    if (bytesInserted) {
        ++cache.auxiliaryShpBytesCacheMisses;
    } else {
        ++cache.auxiliaryShpBytesCacheHits;
    }
    if (!bytesEntry.loadAttempted) {
        bytesEntry.loadAttempted = true;
        const bool detailTiming =
            runtimeRenderDetailDiagnosticsEnabled(context);
        const auto bytesMissStart =
            runtimeMaybeTimingStart(detailTiming);

        std::string sourceName;
        std::string errorMessage;

        for (const std::string& candidate : imageCandidates) {
            const std::string fileName = fileNameForShpImageId(candidate);
            if (!runtimeAuxiliaryResourceUsesShpBytes(
                    fileName,
                    auxiliaryArtType
                )) {
                continue;
            }
            const bool sidebarResource =
                isRuntimeSidebarResourceFileName(fileName);
            bool loaded = false;
            loaded = loadRuntimeResourceBytesFromMixArchives(
                context.assetRootPath,
                fileName,
                cache,
                bytesEntry.bytes,
                sourceName,
                errorMessage
            );
            if (!loaded
                && !mixExportsRoot.empty()
                && normalizedLowerPath(mixExportsRoot)
                    != normalizedLowerPath(context.assetRootPath)) {
                loaded = loadRuntimeResourceBytesFromMixArchives(
                    mixExportsRoot,
                    fileName,
                    cache,
                    bytesEntry.bytes,
                    sourceName,
                    errorMessage
                );
            }
            if (!sidebarResource) {
                if (!loaded) {
                    loaded = loadRuntimeResourceBytesByName(
                        {context.structureRootPath},
                        context.assetRootPath,
                        fileName,
                        cache,
                        bytesEntry.bytes,
                        sourceName,
                        errorMessage
                    );
                }
            }
            if (loaded) {
                bytesEntry.resolvedImageId = candidate;
                break;
            }
        }

        if (bytesEntry.bytes.empty()) {
            bytesEntry.loaded = false;
            bytesEntry.errorMessage =
                "auxiliary SHP was not found: "
                + imageId
                + " candidates="
                + candidateKey
                + " "
                + errorMessage;
        } else {
            bytesEntry.loaded = true;
            bytesEntry.sourceFingerprint = 14695981039346656037ULL;
            for (unsigned char value : bytesEntry.bytes)
                bytesEntry.sourceFingerprint = (bytesEntry.sourceFingerprint ^ value) * 1099511628211ULL;
            bytesEntry.sourcePath = sourceName;
            bytesEntry.frameCount = runtimeShpFrameCount(bytesEntry.bytes);
            bytesEntry.errorMessage = errorMessage;
        }
        cache.auxiliaryShpBytesCacheMissMs +=
            runtimeMaybeElapsedMs(detailTiming, bytesMissStart);
    }
    if (!bytesEntry.loaded || bytesEntry.frameCount <= 0) {
        return nullptr;
    }

