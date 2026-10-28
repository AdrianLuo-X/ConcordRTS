#pragma once
#include "ui/surface/win32/RuntimeMainBackBufferWin32.h"
#include "render/core/MapDrawCommands.h"
namespace concord::ui::win32 {
// Separate private diagnostic extension; original CPU probe ABI is unchanged.
struct MapFrameProbeWin32 : MainFrameProbeWin32 {
    virtual void observeMap(HDC,int,int,bool,bool,const concord::render::MapDrawFrame&)=0;
};
} // namespace concord::ui::win32
