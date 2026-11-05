// Exact complete original consumers; no statement rewritten. Per-instance time only.
struct CampaignPresentationOriginal {
    const RuntimeUiClock* source;
    ULONGLONG GetTickCount64() const noexcept { return source->nowMs(); }

void enterCampaignMapOverlayFade(CampaignMenuState& state) {
    CampaignMapPresentationState& presentation = state.mapPresentation;
    presentation.phase = CampaignMapPresentationPhase::OverlayFade;
    if (!presentation.overlaySoundPlayed) {
        const CampaignDefinition* campaign = selectedCampaign(state);
        if (campaign != nullptr) {
            playCampaignMapSound(state, campaign->mapSelectionSounds.overlay);
        }
        presentation.overlaySoundPlayed = true;
    }
    presentation.phaseStartedAt = GetTickCount64();
}

void refreshCampaignMapPresentation(CampaignMenuState& state) {
    if (state.page != CampaignPage::Missions) {
        if (!state.mapPresentation.key.empty()) {
            releaseCampaignMapMouseCapture(state);
            stopCampaignVoice();
            stopCampaignMapEffect();
            state.mapPresentation = CampaignMapPresentationState{};
        }
        return;
    }
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    const CampaignMapSelectionStage* stage = selectedMapSelectionStage(state);
    std::string key;
    if (campaign != nullptr && mission != nullptr) {
        key = lowerAscii(campaign->id + "|" + mission->id + "|");
        key += stage == nullptr ? "(fallback)" : lowerAscii(stage->id);
    }
    if (key == state.mapPresentation.key) return;

    releaseCampaignMapMouseCapture(state);
    stopCampaignVoice();
    stopCampaignMapEffect();
    state.mapPresentation = CampaignMapPresentationState{};
    CampaignMapPresentationState& presentation = state.mapPresentation;
    presentation.key = key;
    if (campaign == nullptr || mission == nullptr || stage == nullptr) {
        presentation.phase = CampaignMapPresentationPhase::FallbackList;
        presentation.phaseStartedAt = GetTickCount64();
        presentation.diagnostic =
            "no incoming authored map-selection stage for this mission";
        return;
    }
    presentation.stageSection = stage->id;
    loadCampaignMapPalette(state, *campaign);

    if (!stage->clickMap.empty()) {
        const CampaignResourceRecord& clickResource =
            state.presentationResources.load(
                stage->clickMap,
                campaign->sourceId
            );
        std::vector<RuntimePaletteColor> ignoredPalette;
        std::string error;
        if (clickResource.found
            && decodeRuntimePcxImageBytes(
                clickResource.bytes,
                presentation.clickMap,
                ignoredPalette,
                error
            )) {
            presentation.clickMapSource = clickResource.source;
        } else {
            appendMapPresentationDiagnostic(
                presentation,
                stage->clickMap + " click map: "
                    + (error.empty() ? "resource not found" : error)
            );
        }
    } else {
        appendMapPresentationDiagnostic(
            presentation,
            "stage has no ClickMap"
        );
    }

    loadCampaignMapOverlays(state, *stage);
    loadCampaignMapAnimations(state, *campaign);

    bool vqaReady = false;
    if (!stage->mapVq.empty()) {
        const CampaignResourceRecord& vqaResource =
            state.presentationResources.load(
                stage->mapVq,
                campaign->sourceId
            );
        if (vqaResource.found
            && presentation.vqa.loadFromMemory(vqaResource.bytes)) {
            updateCampaignVqaImage(presentation, vqaResource.source);
            vqaReady = true;
        } else {
            appendMapPresentationDiagnostic(
                presentation,
                stage->mapVq + " VQA: "
                    + (vqaResource.found
                        ? presentation.vqa.errorMessage()
                        : "resource not found")
            );
        }
    }

    if (!presentation.hasClickMap()) {
        reconstructCampaignClickMapFromAuthoredTargets(state, *stage);
    }

    // Resource resolution can touch archive and audio indexes.  Start the
    // shared presentation clock only after frame zero and the voice channel
    // are ready, keeping the opening image and narration aligned.
    startCampaignMapVoice(state, *stage);
    presentation.phaseStartedAt = GetTickCount64();
    presentation.lastFrameAt = presentation.phaseStartedAt;
    if (vqaReady) {
        presentation.phase = CampaignMapPresentationPhase::VqaIntro;
        return;
    }

    if (!presentation.overlays.empty() || !presentation.animations.empty()) {
        enterCampaignMapOverlayFade(state);
    } else if (presentation.hasClickMap()) {
        presentation.keyboardColor = firstUsableMapChoiceColor(state, *stage);
        if (presentation.keyboardColor != 0) {
            presentation.phase = CampaignMapPresentationPhase::Interactive;
            if (!presentation.targetSoundPlayed) {
                playCampaignMapSound(
                    state,
                    campaign->mapSelectionSounds.targetFlyIn
                );
                presentation.targetSoundPlayed = true;
            }
            presentation.phaseStartedAt = GetTickCount64();
        } else {
            presentation.phase = CampaignMapPresentationPhase::FallbackList;
            appendMapPresentationDiagnostic(
                presentation,
                "stage has no available map choice"
            );
        }
    } else {
        presentation.phase = CampaignMapPresentationPhase::FallbackList;
    }
}

void updateCampaignMapPresentation(CampaignMenuState& state) {
    refreshCampaignMapPresentation(state);
    CampaignMapPresentationState& presentation = state.mapPresentation;
    const ULONGLONG now = GetTickCount64();
    if (presentation.phase == CampaignMapPresentationPhase::VqaIntro) {
        const ULONGLONG interval = 1000ULL
            / std::max<std::uint32_t>(1U, presentation.vqa.frameRate());
        int advancedFrames = 0;
        while (presentation.vqa.hasNextFrame()
            && now >= presentation.lastFrameAt + std::max<ULONGLONG>(1, interval)
            && advancedFrames < 4) {
            presentation.lastFrameAt += std::max<ULONGLONG>(1, interval);
            ++advancedFrames;
            if (!presentation.vqa.advance()) {
                if (!presentation.vqa.errorMessage().empty()) {
                    appendMapPresentationDiagnostic(
                        presentation,
                        "VQA frame: " + presentation.vqa.errorMessage()
                    );
                }
                presentation.vqa.clear();
                break;
            }
            updateCampaignVqaImage(
                presentation,
                presentation.vqaFrame.source
            );
        }
        if (!presentation.vqa.hasNextFrame()) {
            enterCampaignMapOverlayFade(state);
        }
    }
    if (presentation.phase == CampaignMapPresentationPhase::OverlayFade
        && now >= presentation.phaseStartedAt + 750ULL) {
        const CampaignDefinition* campaign = selectedCampaign(state);
        const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
        if (presentation.hasClickMap() && stage != nullptr) {
            presentation.keyboardColor = firstUsableMapChoiceColor(
                state,
                *stage
            );
            if (presentation.keyboardColor == 0) {
                presentation.phase =
                    CampaignMapPresentationPhase::FallbackList;
                appendMapPresentationDiagnostic(
                    presentation,
                    "stage has no available map choice"
                );
                return;
            }
            presentation.phase = CampaignMapPresentationPhase::Interactive;
            if (!presentation.targetSoundPlayed && campaign != nullptr) {
                playCampaignMapSound(
                    state,
                    campaign->mapSelectionSounds.targetFlyIn
                );
                presentation.targetSoundPlayed = true;
            }
            presentation.phaseStartedAt = GetTickCount64();
        } else {
            presentation.phase = CampaignMapPresentationPhase::FallbackList;
            presentation.phaseStartedAt = now;
        }
    }
}

std::size_t campaignMapAnimationFrameIndex(
    const CampaignMapAnimationVisual& animation,
    ULONGLONG timestamp
) {
    if (animation.frames.empty()) return 0U;
    const ULONGLONG frameDuration = std::max<ULONGLONG>(
        1ULL,
        static_cast<ULONGLONG>(animation.frameRate) * 1000ULL / 60ULL
    );
    return static_cast<std::size_t>(
        (timestamp / frameDuration) % animation.frames.size()
    );
}

std::vector<unsigned char> composeCampaignSurface(const CampaignMenuState& state) {
    std::vector<unsigned char> canvas(kBaseWidth * kBaseHeight * 4, 0);
    const auto& presentation = state.mapPresentation;
    const RECT media = campaignMapMediaRect(presentation);
    blitCampaignImageScaled(canvas, presentation.vqaFrame, media, false);
    const ULONGLONG elapsed = GetTickCount64() - presentation.phaseStartedAt;
    const auto opacity = static_cast<unsigned char>(
        presentation.phase == CampaignMapPresentationPhase::VqaIntro ? 0
        : presentation.phase == CampaignMapPresentationPhase::OverlayFade
            ? std::min<ULONGLONG>(255, elapsed * 255 / 750) : 255);
    if (!presentation.paletteFallback) {
        for (const auto& overlay : presentation.overlays) {
            blitCampaignImageScaled(canvas, overlay, media, false, opacity);
        }
        for (const auto& animation : presentation.animations) {
            if (animation.frames.empty()) continue;
            const auto& image = animation.frames[
                campaignMapAnimationFrameIndex(animation, GetTickCount64())];
            blitCampaignImageScaled(canvas, image,
                RECT{animation.x, animation.y, animation.x + image.width,
                     animation.y + image.height}, false);
        }
    }
    return canvas;
}

void drawCampaignMapTargets(
    const CampaignMenuState& state,
    HDC dc,
    const RECT& client
) {
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    if (stage == nullptr
        || state.mapPresentation.phase
            != CampaignMapPresentationPhase::Interactive) {
        return;
    }
    std::size_t count = stage->targets.size();
    if (stage->declaredTargetCount >= 0) {
        count = std::min(
            count,
            static_cast<std::size_t>(stage->declaredTargetCount)
        );
    }
    const int clientWidth = std::max(1L, client.right - client.left);
    const int clientHeight = std::max(1L, client.bottom - client.top);
    const RECT media = campaignMapMediaRect(state.mapPresentation);
    const int authoredWidth = state.mapPresentation.clickMap.width > 0
        ? state.mapPresentation.clickMap.width
        : std::max(1, state.mapPresentation.vqaFrame.width);
    const int authoredHeight = state.mapPresentation.clickMap.height > 0
        ? state.mapPresentation.clickMap.height
        : std::max(1, state.mapPresentation.vqaFrame.height);
    const ULONGLONG now = GetTickCount64();
    const int pulse = static_cast<int>((now / 80ULL) % 8ULL);
    const double linearFly = std::clamp(
        static_cast<double>(now - state.mapPresentation.phaseStartedAt) / 600.0,
        0.0,
        1.0
    );
    const double fly = 1.0 - (1.0 - linearFly) * (1.0 - linearFly);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    for (std::size_t index = 0; index < count; ++index) {
        const CampaignMapSelectionPoint& target = stage->targets[index];
        const int associatedColor = index < stage->choices.size()
            ? stage->choices[index].colorIndex
            : 0;
        const bool focused = associatedColor > 0
            && (associatedColor == state.mapPresentation.hoverColor
                || associatedColor == state.mapPresentation.keyboardColor);
        const COLORREF color = focused ? kBrightGold : RGB(210, 45, 35);
        HPEN pen = CreatePen(PS_SOLID, focused ? 3 : 2, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const int baseCenterX = static_cast<int>(media.left)
            + target.x * static_cast<int>(media.right - media.left)
                / authoredWidth;
        const int baseCenterY = static_cast<int>(media.top)
            + target.y * static_cast<int>(media.bottom - media.top)
                / authoredHeight;
        const int startBaseX = (index & 1U) == 0U ? -40 : kBaseWidth + 40;
        const int animatedBaseX = static_cast<int>(std::lround(
            startBaseX + (baseCenterX - startBaseX) * fly
        ));
        const int centerX = client.left
            + animatedBaseX * clientWidth / kBaseWidth;
        const int centerY = client.top
            + baseCenterY * clientHeight / kBaseHeight;
        const int radius = std::max(
            7,
            (focused ? 13 : 10) + pulse / 2
        );
        Ellipse(
            dc,
            centerX - radius,
            centerY - radius,
            centerX + radius,
            centerY + radius
        );
        MoveToEx(dc, centerX - radius - 5, centerY, nullptr);
        LineTo(dc, centerX + radius + 5, centerY);
        MoveToEx(dc, centerX, centerY - radius - 5, nullptr);
        LineTo(dc, centerX, centerY + radius + 5);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    SelectObject(dc, oldBrush);
}

void skipCampaignMapIntro(CampaignMenuState& state) {
    CampaignMapPresentationState& presentation = state.mapPresentation;
    if (presentation.phase != CampaignMapPresentationPhase::VqaIntro
        && presentation.phase != CampaignMapPresentationPhase::OverlayFade) {
        return;
    }
    const CampaignMapSelectionStage* stage = currentMapSelectionStage(state);
    const CampaignDefinition* campaign = selectedCampaign(state);
    presentation.vqa.clear();
    if (presentation.hasClickMap() && stage != nullptr) {
        presentation.keyboardColor = firstUsableMapChoiceColor(state, *stage);
        if (presentation.keyboardColor == 0) {
            presentation.phase = CampaignMapPresentationPhase::FallbackList;
            presentation.phaseStartedAt = GetTickCount64();
            appendMapPresentationDiagnostic(
                presentation,
                "stage has no available map choice"
            );
            return;
        }
        presentation.phase = CampaignMapPresentationPhase::Interactive;
        if (!presentation.targetSoundPlayed && campaign != nullptr) {
            playCampaignMapSound(
                state,
                campaign->mapSelectionSounds.targetFlyIn
            );
            presentation.targetSoundPlayed = true;
        }
        presentation.phaseStartedAt = GetTickCount64();
    } else {
        presentation.phase = CampaignMapPresentationPhase::FallbackList;
        presentation.phaseStartedAt = GetTickCount64();
    }
}
};
