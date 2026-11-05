#include "ui/text/win32/RuntimeSkirmishSystemFontWin32.h"
#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
namespace concord::ui::win32 {
void SkirmishSystemFontsWin32::initialize() {
    titleFont_ = CreateFontW(
        -26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    captionFont_ = CreateFontW(
        -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    normalFont_ = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    smallFont_ = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
}
void SkirmishSystemFontsWin32::bindNativeControl(HWND window,SkirmishSystemFontRole role) const noexcept {
 SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(nativeFont(role)),FALSE);
}
SkirmishSystemFontsWin32::~SkirmishSystemFontsWin32(){reset();}
void SkirmishSystemFontsWin32::reset(SkirmishGdiReleaseTrace* trace) noexcept {
        if (titleFont_ != nullptr) {
            const BOOL disposed = DeleteObject(titleFont_);
            if (trace) trace->record(3, disposed != FALSE);
        }
        if (captionFont_ != nullptr) {
            const BOOL disposed = DeleteObject(captionFont_);
            if (trace) trace->record(4, disposed != FALSE);
        }
        if (normalFont_ != nullptr) {
            const BOOL disposed = DeleteObject(normalFont_);
            if (trace) trace->record(5, disposed != FALSE);
        }
        if (smallFont_ != nullptr) {
            const BOOL disposed = DeleteObject(smallFont_);
            if (trace) trace->record(6, disposed != FALSE);
        }
 titleFont_=nullptr;captionFont_=nullptr;normalFont_=nullptr;smallFont_=nullptr;
}
HFONT SkirmishSystemFontsWin32::nativeFont(SkirmishSystemFontRole role) const noexcept {
 switch(role){case SkirmishSystemFontRole::Title:return titleFont_;
 case SkirmishSystemFontRole::Caption:return captionFont_;
 case SkirmishSystemFontRole::Normal:return normalFont_;
 case SkirmishSystemFontRole::Small:return smallFont_;}
 return nullptr;
}
}
