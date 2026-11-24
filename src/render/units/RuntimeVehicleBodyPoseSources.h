#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

#include "render/core/RuntimeRenderSpatialIndex.h"
#include "render/units/RuntimeVehicleBodyBlastPresentation.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"

struct RuntimeImpactRenderActor;

namespace concord::render {

struct RuntimeVehicleBodyImpact {
    const RuntimeImpactRenderActor* actor = nullptr;
    RuntimeVehicleBlastPresentation presentation;
};

struct RuntimeVehicleBodyPoseSources {
    std::unordered_map<int, const SpecialMechanismRenderSnapshot*> parasitesByTarget;
    std::unordered_map<int, const SpecialMechanismRenderSnapshot*> squidByTarget;
    std::vector<RuntimeVehicleBodyImpact> impacts;
    RuntimeRenderSpatialIndex impactIndex;
    bool impactsIndexed = false;
    mutable std::vector<std::size_t> impactCandidates;

    void captureAttachments(
        std::span<const SpecialMechanismRenderSnapshot> snapshots, const RulesDatabase& rules
    ) {
        for (const auto& snapshot : snapshots) {
            if (snapshot.kind != "parasite" || snapshot.intensity <= 0) continue;
            if (snapshot.phase == "organic") {
                squidByTarget.try_emplace(snapshot.targetObjectId, &snapshot);
                continue;
            }
            const auto* warhead = rules.findWarheadType(snapshot.resourceId);
            if (warhead != nullptr && !warhead->rocker) continue;
            // Preserve the first eligible attachment for each target.
            parasitesByTarget.try_emplace(snapshot.targetObjectId, &snapshot);
        }
    }

    bool beginImpactIndex(bool enabled) {
        impactsIndexed = enabled && impacts.size() > 16;
        return impactsIndexed;
    }

    void indexImpact(double x, double y, double level, double blastRadius) {
        // The extra cell keeps bucket bounds conservative. Invalid heights
        // retain the original all-source fallback and insertion order.
        const double radius = std::isfinite(level)
            ? blastRadius + 1.0 : std::numeric_limits<double>::quiet_NaN();
        impactIndex.add(x - radius, y - radius, x + radius, y + radius);
    }

    const std::vector<std::size_t>* candidatesFor(double x, double y, double z) const {
        if (!impactsIndexed || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return nullptr;
        impactIndex.query(x, y, impactCandidates);
        return &impactCandidates;
    }
};

} // namespace concord::render
