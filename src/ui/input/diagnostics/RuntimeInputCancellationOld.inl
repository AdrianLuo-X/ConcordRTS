LRESULT CALLBACK inputCancellationOldWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    RuntimeWindowState* campaignState =
        reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );

    // A synchronous window callback can enter during a local simulation
    // batch. Close its shared terrain signature before any handler mutates
    // state, including load/input paths that are ordinarily outside the batch.
    if (campaignState != nullptr)
        runtimeInvalidateVisionTerrainBatchForMessage(campaignState->world);

    if (campaignState != nullptr) {
        RuntimeForceAttackHeldUiHost forceHost(*campaignState, window);
        handleRuntimeForceAttackHeld(runtimeForceAttackInputForMessage(message, wParam), forceHost);
    }
    if (campaignState != nullptr) {
        RuntimeMapRightPointerGestureUiHost rightHost(*campaignState, window);
        const auto event = message == WM_KILLFOCUS ? RuntimeMapRightPointerCancelEvent::FocusLost
            : message == WM_CANCELMODE ? RuntimeMapRightPointerCancelEvent::CancelMode
            : message == WM_ACTIVATEAPP && wParam == FALSE
                ? RuntimeMapRightPointerCancelEvent::ApplicationDeactivated
                : RuntimeMapRightPointerCancelEvent::Other;
        cancelRuntimeMapRightPointer(event, rightHost);
    }
    if (campaignState != nullptr) {
        RuntimeCampaignInputAdmissionUiHost admissionHost(*campaignState);
        if (rejectRuntimeCampaignInput(runtimeCampaignInputEventForMessage(message), admissionHost))
            return 0;
    }
    if (campaignState != nullptr) {
        RuntimePointerIngressUiHost pointerHost(*campaignState, window);
        if (handleRuntimePointerIngress(
                runtimePointerIngressInputForMessage(message, wParam, lParam), pointerHost))
            return 0;
    }
    switch (message) {
    case WM_SETCURSOR:
    {
        RuntimeCursorUiHost host(window);
        return handleRuntimeCursor(LOWORD(lParam) == HTCLIENT, host)
            ? TRUE : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_LBUTTONDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerDown({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_MOUSEMOVE:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerMove({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_LBUTTONUP:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerUp({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_CAPTURECHANGED:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            RuntimeMapRightPointerGestureUiHost rightHost(*state, window);
            captureChangedRuntimeMapRightPointer(rightHost);
        }
        if (state != nullptr
            && (runtimeObserveCancellationSlider(*state) != RuntimeMenuAction::None
                || runtimeObserveCancellationPressed(*state) != RuntimeMenuAction::None)) {
            const RuntimeMenuAction interruptedSlider =
                runtimeObserveCancellationSlider(*state);
            runtimeObserveInputCancellation("setSlider");
            state->runtimeMenuSliderDrag = RuntimeMenuAction::None;
            runtimeObserveInputCancellation("setPressed");
            state->runtimeMenuPressed = RuntimeMenuAction::None;
            if (interruptedSlider != RuntimeMenuAction::None) {
                runtimeObserveInputCancellation("effects", interruptedSlider);
                applyRuntimeMenuSliderRuntimeEffects(
                    *state,
                    interruptedSlider
                );
                runtimeObserveInputCancellation("preferences");
                (void)persistRuntimeMenuPreferences(*state);
            }
            runtimeObserveInputCancellation("repaint");
            state->repaintRequested = true;
            runtimeObserveInputCancellation("redraw");
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_RBUTTONDOWN:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        RuntimeMapRightPointerGestureUiHost host(*state, window);
        const RuntimeMapRightPointerInput input{runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam))};
        return handleRuntimeMapRightPointerDown(input, host) ? LRESULT{0}
            : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_RBUTTONUP:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        RuntimeMapRightPointerGestureUiHost host(*state, window);
        RuntimeMapRightPointerClickUiHost clickHost(*state);
        const RuntimeMapRightPointerInput input{runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam))};
        return handleRuntimeMapRightPointerUp(input, host, clickHost) ? LRESULT{0}
            : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_CHAR:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state) {
            RuntimeChatTextUiHost host(*state, window);
            if (handleRuntimeChatTextCharacter({static_cast<std::uint64_t>(wParam)},
                state->playerCommunication, host)) return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        RuntimeKeyboardIngressUiHost host(*state, window, lParam);
        const RuntimeKeyboardIngressInput input =
            runtimeKeyboardIngressInputForMessage(message, wParam, lParam);
        if (::handleRuntimeKeyboardIngressKey(input, host)) return 0;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_MOUSEWHEEL:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            RuntimeSidebarPageUiHost host(*state, window, wParam);
            handleRuntimeSidebarWheel(host);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_SHOWWINDOW:
    case WM_SIZE:
        if ((message == WM_SHOWWINDOW && wParam == FALSE)
            || (message == WM_SIZE && wParam == SIZE_MINIMIZED)) {
            if (auto* state = reinterpret_cast<RuntimeWindowState*>(
                    GetWindowLongPtrW(window, GWLP_USERDATA))) {
                state->fpsDisplayMeter.reset();
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        if (auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA))) {
            reportRuntimeRankedDeparture(*state);
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            stopRuntimeEvaPlayback(*state);
            stopRuntimeMusic(*state);
            releaseRuntimeBackBuffer(state->backBuffer);
            releaseRuntimeBackBuffer(state->sidebarStaticBuffer);
            releaseRuntimeBackBuffer(state->radarTerrainBuffer);
            state->sidebarStaticKey.clear();
        }
        PostQuitMessage(0);
        return 0;
    }
    case WM_PAINT:
    {
        RuntimePaintCameraMemoScope cameraMemo;
        RuntimePaintTechTreeMemoScope techTreeMemo;
        RuntimePalettePathMemoScope paletteMemo;
        const auto paintStart = std::chrono::steady_clock::now();
        PAINTSTRUCT paint;
        HDC paintDc = BeginPaint(window, &paint);

        RECT clientRect;
        GetClientRect(window, &clientRect);

        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            const std::uint64_t presentationMs=runtimeUiClockNow(*state);
            advanceRuntimeResultPresentation(*state,presentationMs);
            expireRuntimePlayerDefeatNotices(*state,presentationMs);
        }
        const bool reuseCachedFrame =
            state != nullptr
            && !state->repaintRequested
            // Invalidation alone still reuses the cached bitmap. The score
            // SHP animation must redraw even though the terminal world is paused.
            && !runtimeResultScoreIsAnimated(*state)
            && state->backBuffer.memoryDc != nullptr
            && state->backBuffer.bitmap != nullptr
            && state->backBuffer.pixels != nullptr
            && state->backBuffer.width == clientWidth
            && state->backBuffer.height == clientHeight;

        HDC dc = paintDc;
        bool backBufferReady = false;
        void* surfacePixels = nullptr;
        if (state != nullptr
            && ensureRuntimeBackBuffer(
                state->backBuffer,
                paintDc,
                clientWidth,
                clientHeight
            )) {
            dc = state->backBuffer.memoryDc;
            surfacePixels = state->backBuffer.pixels;
            backBufferReady = true;
        }

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220, 230, 240));

        if (state != nullptr && !reuseCachedFrame) {
            if (state->world.getSummary().loaded) {
                RECT mapViewport = runtimeMapViewport(clientRect);
                if (state->playerLocalCommands.cameraFollow && !state->runtimeMenuOpen)
                    runtimePlayerCenterSelection(*state, clientRect);
                // Resize/load can change the usable map viewport before any
                // input path gets a chance to clamp it.  Establish the exact
                // camera used by both this map frame and the radar overlay.
                ensureRuntimeCameraForViewport(*state, mapViewport);

                RuntimeMapRenderContext renderContext;
                renderContext.theater =
                    state->theaterLoaded ? &state->theater : nullptr;
                renderContext.art =
                    state->artLoaded ? &state->art : nullptr;
                renderContext.rules =
                    state->rulesLoaded ? &state->rules : nullptr;
                renderContext.mapFacts =
                    state->mapFactsLoaded ? &state->mapFacts : nullptr;
                renderContext.ownerRemapColors =
                    state->ownerRemapColors.empty()
                        ? nullptr
                        : &state->ownerRemapColors;
                renderContext.neutralRemapOwners =
                    state->neutralRemapOwners.empty()
                        ? nullptr
                        : &state->neutralRemapOwners;
                if (runtimeLocalVisionRestricted(*state)) {
                    renderContext.vision = &state->vision;
                    renderContext.visionObserverOwner = state->economy.owner;
                }
                const MapLightingProfile* lightingProfile = &state->lighting.currentProfile();
                RuntimeLightingTint lightingTint = state->lighting.renderTint();
                if (state->campaignMode
                    && state->triggers.campaignIonStormUntilTick
                        > state->tick
                    && state->mapFactsLoaded) {
                    lightingProfile = &state->mapFacts.lighting.ion;
                    lightingTint = runtimeLightingTintForProfile(*lightingProfile);
                }
                renderContext.lightingTint = &lightingTint;
                renderContext.lightingProfile = lightingProfile;
                refreshRuntimeParticleRenderSnapshots(
                    *state,
                    renderContext
                );
                renderContext.projectileActors =
                    runtimeProjectileRenderActorsForRender(*state);
                renderContext.impactActors =
                    runtimeImpactRenderActorsForRender(*state);
                renderContext.wrecks = &state->wrecks;
                renderContext.buildingCollapses = &state->buildingCollapses;
                refreshRuntimeSpecialMechanismRenderSnapshots(*state);
                renderContext.specialMechanismSnapshots =
                    &state->specialMechanismRenderSnapshots;
                renderContext.selectedObjectIds = &state->selectedObjectIds;
                renderContext.playerCheerUntilTick = &state->playerCommunication.cheerUntilTick;
                refreshRuntimeGateRenderSnapshots(*state);
                renderContext.gateSnapshots =
                    &state->gateRenderSnapshots;
                renderContext.tileRootPath =
                    runtimeTileRootPath(state->config, state->report.map);
                renderContext.palettePath =
                    runtimePalettePath(state->config, state->report.map);
                renderContext.assetRootPath =
                    runtimeAssetRootPath(state->config);
                renderContext.structureRootPath =
                    runtimeStructureRootPath(state->config, state->report.map);
                renderContext.structurePalettePath =
                    runtimeStructurePalettePath(state->config, state->report.map);
                renderContext.stableRenderConfiguration = true;
                renderContext.coarseStageTimingEnabled = state->frameDiagnostics != nullptr;
                renderContext.surfacePixels =
                    static_cast<std::uint32_t*>(surfacePixels);
                renderContext.surfaceWidth =
                    surfacePixels == nullptr ? 0 : clientWidth;
                renderContext.surfaceHeight =
                    surfacePixels == nullptr ? 0 : clientHeight;
                renderContext.surfaceTopDownOwned = backBufferReady
                    && surfacePixels == state->backBuffer.pixels
                    && state->backBuffer.topDownDib;
                renderContext.animationTick = state->tick;
                renderContext.fixedTickHz = state->config.fixedTickHz;
                renderContext.visualDetail = state->runtimeVisualDetail;
                renderContext.seeHiddenObjects = state->config.seeHiddenObjects;
                renderContext.shadowsEnabled = state->runtimeVisualDetail >= 1;
                if (!state->uiResourceRenderSummaryValid) {
                    state->uiResourceRenderSummary =
                        buildRuntimeUiResourceRenderSummary(*state);
                    state->uiResourceRenderSummaryValid = true;
                }
                renderContext.uiResources = &state->uiResourceRenderSummary;
                // Explicit immutable map -> first interaction projection lease.
                // Only the capture call primes; later UI calls cannot publish.
                std::optional<RuntimePaintProjectionSignatureScope> projectionSignatureScope;
                projectionSignatureScope.emplace(*state, true);
                renderContext.captureBuildingDepthHitSpans =
                    runtimePaintBuildingDepthCaptureAllowed(state->world)
                    && runtimeInteractionNeedsProjectedObjects(*state, mapViewport)
                    && (!state->projectedObjectsCacheValid
                        || !runtimeRectEquals(state->projectedObjectsCacheViewport, mapViewport)
                        || state->projectedObjectsCacheCameraX != state->camera.worldOffsetX
                        || state->projectedObjectsCacheCameraY != state->camera.worldOffsetY
                        || state->projectedObjectsCacheTileWidth != state->camera.tileWidth
                        || state->projectedObjectsCacheTileHeight != state->camera.tileHeight
                        || state->projectedObjectsCacheSignature
                            != runtimeProjectedObjectsSignatureForPaintCapture(*state));

                // The world and these refreshed snapshots are immutable until
                // both map and interaction drawing have finished. Share exact
                // body geometry within this paint only; never across ticks.
                RuntimeGeometryQueryScope paintGeometryScope(state->world, mapViewport,
                    renderContext, state->camera, state->renderCache, false);
                const auto mapStart = std::chrono::steady_clock::now();
                state->renderStats = drawRuntimeMap(
                    dc,
                    mapViewport,
                    state->world,
                    renderContext,
                    state->camera,
                    state->renderCache
                );
                const auto mapEnd = std::chrono::steady_clock::now();
                state->perf.lastMapDrawMs =
                    std::chrono::duration<double, std::milli>(
                        mapEnd - mapStart
                    ).count();
                updateRuntimeRenderPerfPeaks(
                    state->perf,
                    state->renderStats
                );

                const auto overlayStart = std::chrono::steady_clock::now();
                drawRuntimeInteractionOverlay(
                    dc,
                    clientRect,
                    *state,
                    renderContext
                );
                applyRuntimeScreenShake(dc, mapViewport, renderContext, state->renderCache);
                const auto overlayEnd = std::chrono::steady_clock::now();
                state->perf.lastOverlayDrawMs =
                    std::chrono::duration<double, std::milli>(
                        overlayEnd - overlayStart
                    ).count();
                projectionSignatureScope.reset();

                const auto sidebarStart = std::chrono::steady_clock::now();
                {
                    RuntimePerfTraceScope sidebarTrace("paint.sidebarCore", state->tick);
                    drawRuntimeSidebar(dc, clientRect, *state);
                }
                {
                    RuntimePerfTraceScope otherUiTrace("paint.otherUiOverlays", state->tick);
                    drawRuntimeGameplayTooltip(dc, clientRect, *state);
                    drawRuntimePlayerHotkeyOverlay(dc, clientRect, *state);
                    drawRuntimeMenuOverlay(dc, clientRect, *state);
                    drawRuntimeSkirmishResultOverlay(dc, clientRect, *state);
                    drawRuntimePlayerDefeatNotices(dc,*state,clientRect);
                }
                {
                    RuntimePerfTraceScope cursorTrace("paint.mouseCursor", state->tick);
                    drawRuntimeMouseCursorOverlay(dc, clientRect, *state);
                }
                const auto sidebarEnd = std::chrono::steady_clock::now();
                state->perf.lastSidebarDrawMs =
                    std::chrono::duration<double, std::milli>(
                        sidebarEnd - sidebarStart
                    ).count();
                state->repaintRequested = false;
            } else {
                HBRUSH background = CreateSolidBrush(RGB(13, 18, 24));
                FillRect(dc, &clientRect, background);
                DeleteObject(background);

                const std::vector<std::string> lines = buildRuntimeTextLines(
                    state->config,
                    state->report,
                    state->tick,
                    state->fps
                );

                int y = 24;
                for (const std::string& line : lines) {
                    const std::wstring wideLine = widenUtf8(line);
                    TextOutW(
                        dc,
                        24,
                        y,
                        wideLine.c_str(),
                        static_cast<int>(wideLine.size())
                    );
                    y += 22;
                }
                state->repaintRequested = false;
            }
        }

        bool submissionSucceeded = false;
        if (backBufferReady) {
            submissionSucceeded = BitBlt(
                paintDc,
                0,
                0,
                clientWidth,
                clientHeight,
                state->backBuffer.memoryDc,
                0,
                0,
                SRCCOPY
            ) != FALSE;
        } else if (state != nullptr && !reuseCachedFrame && paintDc != nullptr) {
            // The allocation-failure fallback renders directly to paintDc.
            // Flush its GDI batch so a failed submission is not counted.
            submissionSucceeded = GdiFlush() != FALSE;
        }

        if (state != nullptr) {
            const bool visible = IsWindowVisible(window) != FALSE;
            const bool minimized = IsIconic(window) != FALSE;
            const bool hasClientArea = clientWidth > 0 && clientHeight > 0;
            RECT paintClip{};
            const int clipKind = paintDc == nullptr
                ? ERROR : GetClipBox(paintDc, &paintClip);
            const bool hasPaintArea = !IsRectEmpty(&paint.rcPaint)
                && clipKind != ERROR && clipKind != NULLREGION
                && !IsRectEmpty(&paintClip);
            if (!state->world.getSummary().loaded || !visible || minimized
                || !hasClientArea) {
                state->fpsDisplayMeter.reset();
            } else if (runtimeFpsDisplayShouldCountFrame(
                    visible, minimized, hasClientArea,
                    hasPaintArea, submissionSucceeded)) {
                state->fpsDisplayMeter.recordPresentedFrame(
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now().time_since_epoch()
                    ).count()
                );
                drawRuntimeFpsDisplay(paintDc, clientRect, state->fpsDisplayMeter);
                // Keep the software cursor above the HUD, without drawing it
                // twice over any pixels outside the newly replaced panel.
                const RECT fpsBounds = runtimeFpsDisplayRect(clientRect);
                if (!IsRectEmpty(&fpsBounds)) {
                    drawRuntimeMouseCursorOverlay(paintDc, clientRect, *state, &fpsBounds);
                }
            }
        }

        if (state != nullptr) {
            ++state->paintedFramesThisSecond;
            if (!reuseCachedFrame) {
                ++state->renderedFramesThisSecond;
            }
            state->perf.lastPaintMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - paintStart
                ).count();
        }
        EndPaint(window, &paint);
        if (state != nullptr) {
            const bool diagnosticSurfaceActive = state->liveBenchmark != nullptr
                || (IsWindowVisible(window) && !IsIconic(window));
            // The hidden benchmark measures completed CPU back-buffer work;
            // its paint DC may reject BitBlt because it has no visible region.
            const bool diagnosticCpuBenchmarkFrame = state->liveBenchmark != nullptr
                && backBufferReady && state->backBuffer.pixels != nullptr;
            recordRuntimeCompletedFrameDiagnostics(*state, clientWidth, clientHeight,
                reuseCachedFrame, diagnosticCpuBenchmarkFrame
                    || (submissionSucceeded && diagnosticSurfaceActive));
        }
        if (state != nullptr && state->liveBenchmark != nullptr) {
            state->liveBenchmark->recordPaint(
                state->tick, state->perf.lastPaintMs,
                reuseCachedFrame ? 0.0 : state->perf.lastMapDrawMs,
                reuseCachedFrame ? 0.0 : state->renderStats.visionMs,
                !reuseCachedFrame, state->renderStats.visibleWorldObjects
            );
            if (!reuseCachedFrame && state->perf.lastPaintMs >= 50.0) {
                std::cout << "Live slow frame: tick=" << state->tick
                    << " paintMs=" << state->perf.lastPaintMs
                    << " mapMs=" << state->perf.lastMapDrawMs
                    << " terrainMs=" << state->renderStats.terrainMs
                    << " terrainObjectsMs=" << state->renderStats.terrainObjectsMs
                    << " worldObjectsMs=" << state->renderStats.worldObjectsMs
                    << " fogMs=" << state->renderStats.visionMs
                    << " overlayMs=" << state->perf.lastOverlayDrawMs
                    << " sidebarMs=" << state->perf.lastSidebarDrawMs
                    << " shpBytesMissMs=" << state->renderStats.auxiliaryShpBytesCacheMissMs
                    << " shpFrameMissMs=" << state->renderStats.auxiliaryShpFrameCacheMissMs
                    << " voxelMisses=" << state->renderStats.voxelCacheMisses
                    << '\n';
            }
        }
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
