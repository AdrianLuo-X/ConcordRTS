// Test-only old call sequence; preserved from the baseline live loop.
void presentation(RuntimeWindowState& state) {
    advanceRuntimeCreditAudio(state, GetTickCount64());
    advanceRuntimeSuperWeaponBuildingAudio(state, GetTickCount64());
    advanceRuntimeChronosphereSelectionAudio(state, GetTickCount64());
    advanceRuntimeDiskDrainAudio(state, GetTickCount64());
    advanceRuntimeDiskMoveAudio(state, GetTickCount64());
    advanceRuntimeGattlingWeaponAudio(state, GetTickCount64());
    advanceRuntimeObjectAmbientAudio(state, GetTickCount64());
    advanceRuntimeAircraftAuxAudio(state, GetTickCount64());
    advanceRuntimeTurretSoundAudio(state, GetTickCount64());
}
