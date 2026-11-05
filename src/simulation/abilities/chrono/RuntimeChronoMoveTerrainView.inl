// Include after buildRuntimeTerrainLookup, before the player move command.
// The chrono legality predicates are invoked synchronously and never retained;
// they cannot change world terrain or the lookup during this local lifetime.


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

