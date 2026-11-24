#include "RuntimeChronoTerrainView.h"
namespace concord::simulation::abilities {
RuntimeChronoTerrainView::RuntimeChronoTerrainView(
    const RuntimeChronoTerrainLookup& cached, bool reference)
    : terrain_(&cached), reference_(reference) {}
RuntimeChronoTerrainView::RuntimeChronoTerrainView(
    const RuntimeChronoTerrainLookup& cached, bool reference,
    const RuntimeChronoTerrainSource& source)
    : RuntimeChronoTerrainView(cached, reference) {
    selectTerrain(source);
}
void RuntimeChronoTerrainView::selectTerrain(const RuntimeChronoTerrainSource& source) {
    if (terrain_->empty()) {
        owned_ = source.buildLookup();
        terrain_ = &owned_;
        generated_ = true;
    } else if (reference_) {
        owned_ = *terrain_;
        terrain_ = &owned_;
    }
}
}
