// Included immediately before issueRuntimeMoveCommand, after the complete BFS.
// An owned, command-local SCC certificate, never a partial component map.
// The caller must keep the admitted graph inputs unchanged during assignment.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <new>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>



class RuntimeMixedLocalComponentReferenceScope {
    int previous_;
public:
    explicit RuntimeMixedLocalComponentReferenceScope(bool reference) noexcept
        : previous_(runtimeMixedLocalComponentReferenceOverride) {
        runtimeMixedLocalComponentReferenceOverride=reference?1:0;
    }
    ~RuntimeMixedLocalComponentReferenceScope() { runtimeMixedLocalComponentReferenceOverride=previous_; }
    RuntimeMixedLocalComponentReferenceScope(const RuntimeMixedLocalComponentReferenceScope&)=delete;
    RuntimeMixedLocalComponentReferenceScope& operator=(const RuntimeMixedLocalComponentReferenceScope&)=delete;
};


class RuntimeMixedKnownUnmappedReferenceScope {
    int previous_;
public:
    explicit RuntimeMixedKnownUnmappedReferenceScope(bool reference) noexcept
        :previous_(runtimeMixedKnownUnmappedReferenceOverride) {
        runtimeMixedKnownUnmappedReferenceOverride=reference?1:0;
    }
    ~RuntimeMixedKnownUnmappedReferenceScope(){runtimeMixedKnownUnmappedReferenceOverride=previous_;}
    RuntimeMixedKnownUnmappedReferenceScope(const RuntimeMixedKnownUnmappedReferenceScope&)=delete;
    RuntimeMixedKnownUnmappedReferenceScope& operator=(const RuntimeMixedKnownUnmappedReferenceScope&)=delete;
};

// Semantic control: recover C38's prelisted-centre-only coverage admission.


class RuntimeMixedBestFrontierClosureReferenceScope {
    int previous_;
public:
    explicit RuntimeMixedBestFrontierClosureReferenceScope(bool reference) noexcept
        :previous_(runtimeMixedBestFrontierClosureReferenceOverride) {
        runtimeMixedBestFrontierClosureReferenceOverride=reference?1:0;
    }
    ~RuntimeMixedBestFrontierClosureReferenceScope(){runtimeMixedBestFrontierClosureReferenceOverride=previous_;}
    RuntimeMixedBestFrontierClosureReferenceScope(const RuntimeMixedBestFrontierClosureReferenceScope&)=delete;
    RuntimeMixedBestFrontierClosureReferenceScope& operator=(const RuntimeMixedBestFrontierClosureReferenceScope&)=delete;
};


// Contract hooks supply owned expected output, never a callback or World writer.




class RuntimeMixedLocalComponentStatsScope {
    RuntimeMixedLocalComponentStats* previous_;
    const std::vector<std::pair<int,int>>* previousMap_;
    std::size_t previousLimit_;
public:
    explicit RuntimeMixedLocalComponentStatsScope(RuntimeMixedLocalComponentStats& stats,
        const std::vector<std::pair<int,int>>* expected=nullptr,std::size_t formationLimit=256) noexcept
        : previous_(runtimeMixedLocalComponentStats),previousMap_(runtimeMixedLocalComponentExpectedMap),
          previousLimit_(runtimeMixedLocalComponentFormationLimit) {
        runtimeMixedLocalComponentStats=&stats; runtimeMixedLocalComponentExpectedMap=expected;
        runtimeMixedLocalComponentFormationLimit=std::min<std::size_t>(formationLimit,256);
    }
    ~RuntimeMixedLocalComponentStatsScope() {
        runtimeMixedLocalComponentStats=previous_; runtimeMixedLocalComponentExpectedMap=previousMap_;
        runtimeMixedLocalComponentFormationLimit=previousLimit_;
    }
    RuntimeMixedLocalComponentStatsScope(const RuntimeMixedLocalComponentStatsScope&)=delete;
    RuntimeMixedLocalComponentStatsScope& operator=(const RuntimeMixedLocalComponentStatsScope&)=delete;
};


