// Include after buildRuntimeTerrainLookup, before the player move command.
// The chrono legality predicates are invoked synchronously and never retained;
// they cannot change world terrain or the lookup during this local lifetime.
inline thread_local int runtimeChronoMoveTerrainReferenceOverride = -1;
bool runtimeChronoMoveTerrainReference() {
    if (runtimeChronoMoveTerrainReferenceOverride >= 0)
        return runtimeChronoMoveTerrainReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_CHRONO_MOVE_TERRAIN_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeChronoMoveTerrainReferenceScope {
public:
    explicit RuntimeChronoMoveTerrainReferenceScope(bool reference)
        : previous_(runtimeChronoMoveTerrainReferenceOverride) {
        runtimeChronoMoveTerrainReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeChronoMoveTerrainReferenceScope() {
        runtimeChronoMoveTerrainReferenceOverride = previous_;
    }
    RuntimeChronoMoveTerrainReferenceScope(const RuntimeChronoMoveTerrainReferenceScope&) = delete;
    RuntimeChronoMoveTerrainReferenceScope& operator=(const RuntimeChronoMoveTerrainReferenceScope&) = delete;
private:
    int previous_;
};
class RuntimeChronoMoveTerrainView {
public:
    explicit RuntimeChronoMoveTerrainView(const RuntimeWindowState& state,
        bool reference = runtimeChronoMoveTerrainReference())
        : terrain_(&state.terrainByCell), reference_(reference) {
        RuntimePerfTraceScope trace("moveCommand.chronoTerrain", state.tick);
        if (terrain_->empty()) {
            owned_ = buildRuntimeTerrainLookup(state.world);
            terrain_ = &owned_;
            generated_ = true;
        } else if (reference_) {
            owned_ = state.terrainByCell;
            terrain_ = &owned_;
        }
    }
    const std::unordered_map<int, const TerrainCell*>& get() const { return *terrain_; }
    bool borrowed() const { return terrain_ != &owned_; }
    bool generated() const { return generated_; }
    bool reference() const { return reference_; }
    RuntimeChronoMoveTerrainView(const RuntimeChronoMoveTerrainView&) = delete;
    RuntimeChronoMoveTerrainView& operator=(const RuntimeChronoMoveTerrainView&) = delete;
private:
    std::unordered_map<int, const TerrainCell*> owned_;
    const std::unordered_map<int, const TerrainCell*>* terrain_;
    bool reference_, generated_ = false;
};
