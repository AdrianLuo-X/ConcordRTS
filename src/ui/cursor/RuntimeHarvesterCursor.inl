// The ore command uses the native attack reticle, including unarmed CMIN.
// Keep object actions, tool modes and force fire ahead of this cell preview.
bool runtimeSelectionOffersHarvestCursor(const RuntimeWindowState& state, RuntimeCellPoint cell) {
    if (!runtimeCellHasHarvestableResource(state, cell)
        || (runtimeLocalVisionRestricted(state)
            && !state.vision.isCellExploredBy(state.economy.owner, cell.x, cell.y))) return false;
    return runtimeSelectedUnitsCanHarvestAt(state, state.selectedObjectIds, cell);
}
