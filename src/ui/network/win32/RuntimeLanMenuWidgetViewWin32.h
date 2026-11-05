#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Original non-owning UI-thread widget slots only. This private native value
// takes no window ownership; the actual existing public runner owns the page.
struct RuntimeLanMenuWidgetViewWin32 {
    HWND hostWindow = nullptr;
    HWND window = nullptr;
    HWND playerNameLabel = nullptr;
    HWND playerNameEdit = nullptr;
    HWND hostPortLabel = nullptr;
    HWND hostPortEdit = nullptr;
    HWND directAddressLabel = nullptr;
    HWND directAddressEdit = nullptr;
    HWND passwordLabel = nullptr;
    HWND passwordEdit = nullptr;
    HWND hostButton = nullptr;
    HWND resumeHostButton = nullptr;
    HWND resumeSwapButton = nullptr;
    HWND refreshButton = nullptr;
    HWND joinButton = nullptr;
    HWND spectatorCheck = nullptr;
    HWND backButton = nullptr;
    HWND roomList = nullptr;
    HWND rosterList = nullptr;
    HWND countryLabel = nullptr;
    HWND countryEdit = nullptr;
    HWND colorLabel = nullptr;
    HWND colorEdit = nullptr;
    HWND teamLabel = nullptr;
    HWND teamEdit = nullptr;
    HWND difficultyLabel = nullptr;
    HWND difficultyEdit = nullptr;
    HWND startPositionLabel = nullptr;
    HWND startPositionEdit = nullptr;
    HWND applySlotButton = nullptr;
    HWND kickButton = nullptr;
    HWND readyButton = nullptr;
    HWND startButton = nullptr;
    HWND leaveButton = nullptr;
    HWND statusText = nullptr;
    HWND networkHint = nullptr;
};
