#pragma once

enum class RuntimeMenuPage {
    Main,
    Settings,
    Display,
    Sound,
    Keyboard,
    ExitConfirm
};

enum class RuntimeMenuAction {
    None,
    Options,
    Save,
    Load,
    Exit,
    Resume,
    OpenDisplay,
    OpenSound,
    OpenKeyboard,
    CycleGameSpeed,
    CycleScrollRate,
    CycleVisualDetail,
    ToggleTooltips,
    ToggleTargetLines,
    ToggleScrollCoasting,
    ToggleHiddenObjects,
    CycleResolution,
    ApplyResolution,
    AdjustMusicVolume,
    AdjustSoundVolume,
    AdjustVoiceVolume,
    ToggleMusic,
    ToggleSound,
    RebindOptions,
    RebindStopObject,
    RebindGuardObject,
    RebindScatterObject,
    RebindDeployObject,
    RebindCenterView,
    Back,
    ConfirmExit,
    CancelExit,
    KeyboardCategory,
    KeyboardPreviousPage,
    KeyboardNextPage,
    KeyboardResetAll
};
