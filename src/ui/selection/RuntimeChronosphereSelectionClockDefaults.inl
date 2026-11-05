// Host-private overload preserving the old default-argument read point.
// The former default argument was evaluated before entering the guarded body.
// Keep that read, including rejected/early-return calls; explicit times do not read.
void startRuntimeChronosphereSelectionAudio(RuntimeWindowState& state,
    const RuntimeImpactEffect& marker) {
    startRuntimeChronosphereSelectionAudio(state, marker, runtimeWorldAudioClockNow(state));
}

