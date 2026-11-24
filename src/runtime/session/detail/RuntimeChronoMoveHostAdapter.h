#pragma once
#include "simulation/abilities/chrono/RuntimeChronoTerrainView.h"
namespace concord::runtime::session_detail {
struct RuntimeGameSessionState;
bool runtimeChronoMoveTerrainReference();
class RuntimeChronoMoveTerrainView : private concord::simulation::abilities::RuntimeChronoTerrainView {
    using View = concord::simulation::abilities::RuntimeChronoTerrainView;
public:
    explicit RuntimeChronoMoveTerrainView(const RuntimeGameSessionState& state,
        bool reference = runtimeChronoMoveTerrainReference());
    using View::get;
    using View::borrowed;
    using View::generated;
    using View::reference;
    RuntimeChronoMoveTerrainView(const RuntimeChronoMoveTerrainView&) = delete;
    RuntimeChronoMoveTerrainView& operator=(const RuntimeChronoMoveTerrainView&) = delete;
};
}
