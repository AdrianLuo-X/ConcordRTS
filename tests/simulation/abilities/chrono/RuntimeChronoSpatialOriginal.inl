// Frozen original H023-04 production bodies; byte-checked before installation.
bool runtimeChronoCellIsOccupied(
    const RuntimeGameSessionState& state, const std::unordered_map<int,int>& occupied,
    int x, int y, int objectId) {
    const auto* object=findObjectById(state,objectId);
    if(object && runtimeInfantryUsesGroundSubcells(*object))
        return runtimeAvailableInfantrySubcells(state,*object,{x,y}).empty();
    const auto found=occupied.find(runtimeCellKey(x,y));
    return found!=occupied.end() && found->second!=objectId;
}

class RuntimeChronoMoveTerrainView {
public:
    explicit RuntimeChronoMoveTerrainView(const RuntimeGameSessionState& state,
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
