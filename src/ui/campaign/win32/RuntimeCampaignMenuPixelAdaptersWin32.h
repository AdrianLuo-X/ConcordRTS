#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "runtime/menu/RuntimeCampaignMenuController.h"

namespace concord::campaign_menu::win32 {
// Shape conversion only; layout and hit decisions live in the controller.
inline RuntimeInputPixelRect campaignPixelRect(const RECT& r) { return {r.left,r.top,r.right,r.bottom}; }
inline RECT campaignNativeRect(RuntimeInputPixelRect r) { return {r.left,r.top,r.right,r.bottom}; }
inline RECT campaignShellClientRect(const RECT& r) { return campaignNativeRect(concord::campaign_menu::campaignShellClientRect(campaignPixelRect(r))); }
inline POINT campaignShellPoint(const RECT& r,int x,int y) { const auto p=concord::campaign_menu::campaignShellPoint(campaignPixelRect(r),x,y);return {p.x,p.y}; }
inline void campaignFrameControl(HDC dc,RuntimeInputPixelRect r,HBRUSH brush) { const RECT rect=campaignNativeRect(r);FrameRect(dc,&rect,brush); }
inline RuntimeInputPixelRect campaignEventClientRect(HWND window) { RECT r{};GetClientRect(window,&r);return {r.left,r.top,r.right,r.bottom}; }
} // namespace concord::campaign_menu::win32
