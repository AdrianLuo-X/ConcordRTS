#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Only the current synchronous native callback borrows this message target.
// Nested callbacks restore the previous target; no window ownership is taken.
struct RuntimeMenuEventWindowWin32 {
    HWND value = nullptr;
};
static_assert(sizeof(RuntimeMenuEventWindowWin32) == sizeof(HWND));
static_assert(alignof(RuntimeMenuEventWindowWin32) == alignof(HWND));

struct RuntimeMenuEventWindowBorrowWin32 {
    RuntimeMenuEventWindowWin32& slot;
    HWND previous;
    RuntimeMenuEventWindowBorrowWin32(RuntimeMenuEventWindowWin32& target, HWND current)
        : slot(target), previous(target.value) { slot.value = current; }
    ~RuntimeMenuEventWindowBorrowWin32() { slot.value = previous; }
    RuntimeMenuEventWindowBorrowWin32(const RuntimeMenuEventWindowBorrowWin32&) = delete;
    RuntimeMenuEventWindowBorrowWin32& operator=(const RuntimeMenuEventWindowBorrowWin32&) = delete;
};
