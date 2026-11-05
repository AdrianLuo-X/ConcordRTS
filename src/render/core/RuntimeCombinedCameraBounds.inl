// This result exists only for the current clamp. Both envelopes read the same
// terrain/projection; no terrain identity or revision is cached across calls.




struct RuntimeCameraBoundsPassReferenceScope {
    int previous = runtimeCameraBoundsPassReferenceOverride;
    explicit RuntimeCameraBoundsPassReferenceScope(bool reference) {
        runtimeCameraBoundsPassReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeCameraBoundsPassReferenceScope() {
        runtimeCameraBoundsPassReferenceOverride = previous;
    }
    RuntimeCameraBoundsPassReferenceScope(const RuntimeCameraBoundsPassReferenceScope&) = delete;
    RuntimeCameraBoundsPassReferenceScope& operator=(const RuntimeCameraBoundsPassReferenceScope&) = delete;
};




