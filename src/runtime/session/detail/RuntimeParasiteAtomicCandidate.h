#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteHostDeathPolicy.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include <utility>
namespace concord::runtime::session_detail {
class ParasiteAtomicCandidateFactory;
class ParasiteAtomicOwnedCandidate final : public concord::simulation::abilities::RuntimeParasiteHostDeathCandidate {
    RuntimeSpecialMechanismState value_;
    friend class ParasiteAtomicCandidateFactory;
public:
    explicit ParasiteAtomicOwnedCandidate(const RuntimeSpecialMechanismState& source) : value_(source) {}
    ParasiteAtomicOwnedCandidate(const ParasiteAtomicOwnedCandidate&) = delete;
    ParasiteAtomicOwnedCandidate(ParasiteAtomicOwnedCandidate&&) = delete;
    ParasiteAtomicOwnedCandidate& operator=(const ParasiteAtomicOwnedCandidate&) = delete;
    ParasiteAtomicOwnedCandidate& operator=(ParasiteAtomicOwnedCandidate&&) = delete;
    ParasiteOperationResult beginParasiteLaunch(const ParasiteLaunchRequest& request, const SpecialEventSink& emit) { return value_.beginParasiteLaunch(request, emit); }
    ParasiteOperationResult resolveParasiteImpact(const ParasiteImpactRequest& request, const SpecialEventSink& emit) { return value_.resolveParasiteImpact(request, emit); }
    const std::map<int, ParasiteLinkState>& links() const override { return value_.parasiteLinks(); }
    ParasiteOperationResult handle(const ParasiteHostEventRequest& request) override { return value_.handleParasiteHostEvent(request, SpecialEventSink{}); }
};
class ParasiteAtomicCandidateFactory {
    RuntimeSpecialMechanismState& canonical_;
public:
    explicit ParasiteAtomicCandidateFactory(RuntimeSpecialMechanismState& canonical) noexcept : canonical_(canonical) {}
    ParasiteAtomicOwnedCandidate copyCandidate() const { return ParasiteAtomicOwnedCandidate(canonical_); }
    void commitCandidate(ParasiteAtomicOwnedCandidate& candidate) { canonical_ = std::move(candidate.value_); }
};
}
