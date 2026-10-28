// Private failing-direct-scene diagnostic only. No probe/public API changes.
const bool traceScenePaintEnabled = state && state->frameProbeWin32
    && state->frameProbeWin32->failSceneAllocation
    && std::getenv("CONCORD_S41_SCENE_PAINT_TRACE");
const auto traceScenePaint = [&](const char* phase, bool eligible=false, bool submitted=false) {
    if (!traceScenePaintEnabled) return;
    RECT clip{};
    const auto kind=paintDc?GetClipBox(paintDc,&clip):ERROR;
    std::cout << "S41 scene-paint phase=" << phase << " clipKind=" << kind
        << " clip=" << clip.left << ',' << clip.top << ',' << clip.right << ',' << clip.bottom
        << " paint=" << paint.rcPaint.left << ',' << paint.rcPaint.top << ','
        << paint.rcPaint.right << ',' << paint.rcPaint.bottom
        << " visible=" << (IsWindowVisible(window)!=FALSE) << " iconic=" << (IsIconic(window)!=FALSE)
        << " client=" << clientWidth << 'x' << clientHeight << " loaded=" << state->world.getSummary().loaded
        << " eligible=" << eligible << " submitted=" << submitted
        << " sampled=" << state->fpsDisplayMeter.hasSample()
        << " fps=" << state->fpsDisplayMeter.framesPerSecond() << std::endl;
};
traceScenePaint("begin");
