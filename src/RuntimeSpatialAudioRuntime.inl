RuntimeProjectedPoint runtimeProjectCellXY(double x, double y, double level, const RuntimeMapCamera& camera);

RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForObject(const WorldObjectInstance& object) {
    return {object.hasRenderPosition ? object.renderX : double(object.x),
        object.hasRenderPosition ? object.renderY : double(object.y),
        object.renderMoveActive ? object.renderLevel : double(object.level)};
}

RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForImpact(const RuntimeImpactEffect& impact) {
    return {impact.hasWorldPosition ? impact.worldX : double(impact.cell.x),
        impact.hasWorldPosition ? impact.worldY : double(impact.cell.y),
        impact.hasWorldPosition ? double(impact.worldLevel) : double(impact.level)};
}

RuntimeSpatialAudioResult runtimeSpatialAudioAt(const RuntimeWindowState& state,
    const RuntimeSpatialAudioRules& rules, const RuntimeSpatialAudioPosition& source) {
    const bool valid = std::isfinite(source.x) && std::isfinite(source.y) && std::isfinite(source.level)
        && source.x >= -1048576 && source.x <= 1048576 && source.y >= -1048576 && source.y <= 1048576
        && source.level >= -1048576 && source.level <= 1048576;
    if (!valid) return {};
    const auto projected = runtimeProjectCellXY(source.x, source.y, source.level, state.camera);
    double width = state.cameraClampViewport.right - state.cameraClampViewport.left;
    double height = state.cameraClampViewport.bottom - state.cameraClampViewport.top;
    double x = double(projected.x) - state.camera.worldOffsetX;
    double y = double(projected.y) - state.camera.worldOffsetY;
    if (width <= 0 || height <= 0) {
        // No window during command smoke/loading: preserve the established
        // waypoint listener, with an explicit 640x400 reference viewport.
        const auto context = runtimeWorldAudioContext(state);
        if (!context.listenerCellValid) return {};
        const auto center = runtimeProjectCellXY(context.listenerCell.x,
            context.listenerCell.y, context.listenerCell.level, state.camera);
        width = 640; height = 400;
        x = projected.x - center.x + width / 2; y = projected.y - center.y + height / 2;
    }
    std::string observer = state.economy.owner;
    for (const auto& house : state.humanHouseAuthorities) if (house.localFacade) {
        observer = house.canonicalOwner; break;
    }
    const bool mapped = !state.visionEnabled || state.vision.visibilityFor(observer,
        static_cast<int>(source.x), static_cast<int>(source.y)) != RuntimeCellVisibility::Shrouded;
    return runtimeSpatialAudioAtProjection(rules, x, y, width, height, mapped,
        std::max(1, state.camera.tileWidth) / 60.0);
}

RuntimeSpatialAudioResult runtimeSpatialAudioAt(const RuntimeWindowState& state,
    const std::string& event, const RuntimeSpatialAudioPosition& source) {
    const auto* sound = state.worldAudioCatalogLoaded ? state.worldAudioCatalog.find(event) : nullptr;
    return runtimeSpatialAudioAt(state, sound ? sound->spatial : runtimeSpatialAudioRules(state.soundIni, event), source);
}

int runtimeSpatialAudioCombinedGain(const RuntimeWindowState& state, int authoredPermille, int spatial16384) {
    const auto authored = std::clamp(authoredPermille, 0, 1000) * 16384 / 1000;
    const auto user = std::clamp(state.runtimeSoundVolume, 0, 100) * 16384 / 100;
    return int((std::int64_t(authored) * std::clamp(spatial16384, 0, 16384) / 16384) * user / 16384);
}

int runtimeSpatialAudioGainPermille(const RuntimeWindowState& state,
    const RuntimeWorldAudioSoundSpec& sound, const RuntimeSpatialAudioPosition& source) {
    if (!state.runtimeSoundEffectsEnabled || state.runtimeSoundVolume <= 0) return 0;
    return std::clamp(sound.volumePercent, 0, 100) * 10
        * runtimeSpatialAudioAt(state, sound.spatial, source).gain16384 / 16384;
}

RuntimeVoiceAudio& runtimeVoiceAudioFor(RuntimeWindowState& state) {
    if (!state.runtimeVoiceAudio) state.runtimeVoiceAudio = std::make_unique<RuntimeVoiceAudio>();
    return *state.runtimeVoiceAudio;
}
