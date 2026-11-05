// Host-private presentation phase. Each old consumer retains its own clock
// read at its original call point; no shared timestamp or simulation tick.
void advanceRuntimeWorldAudioPresentationClocks(RuntimeWindowState& state) {
    advanceRuntimeCreditAudio(state, runtimeWorldAudioClockNow(state));
    advanceRuntimeSuperWeaponBuildingAudio(state, runtimeWorldAudioClockNow(state));
    advanceRuntimeChronosphereSelectionAudio(state, runtimeWorldAudioClockNow(state));
    advanceRuntimeDiskDrainAudio(state, runtimeWorldAudioClockNow(state));
    advanceRuntimeDiskMoveAudio(state, runtimeWorldAudioClockNow(state));
    advanceRuntimeGattlingWeaponAudio(state, runtimeWorldAudioClockNow(state));
    advanceRuntimeObjectAmbientAudio(state, runtimeWorldAudioClockNow(state));
    advanceRuntimeAircraftAuxAudio(state, runtimeWorldAudioClockNow(state));
    advanceRuntimeTurretSoundAudio(state, runtimeWorldAudioClockNow(state));
}
