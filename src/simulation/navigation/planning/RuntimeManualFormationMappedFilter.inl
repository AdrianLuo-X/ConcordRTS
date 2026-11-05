// Include before formation finders, after RuntimeWindowState and unit lookup.
// Opt-in ONLY from a manual command. Automatic finder callers pass nullptr.
#pragma once
#include <cstdlib>



class RuntimeManualFormationMappedReferenceScope {
    int previous_;
public:
    explicit RuntimeManualFormationMappedReferenceScope(bool reference) noexcept
        :previous_(runtimeManualFormationMappedReferenceOverride) {
        runtimeManualFormationMappedReferenceOverride=reference?1:0;
    }
    ~RuntimeManualFormationMappedReferenceScope() {
        runtimeManualFormationMappedReferenceOverride=previous_;
    }
    RuntimeManualFormationMappedReferenceScope(const RuntimeManualFormationMappedReferenceScope&)=delete;
    RuntimeManualFormationMappedReferenceScope& operator=(const RuntimeManualFormationMappedReferenceScope&)=delete;
};

// The finder is synchronously read-only. State/mover/owner/vision/rules must
// remain unchanged until this stack object and the finder return. Nothing is
// retained in world state, a generic path queue, or a cross-command cache.

