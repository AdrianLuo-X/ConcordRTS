#include "ui/results/win32/RuntimeResultBitmapTextWin32.h"
#include <utility>
namespace concord::ui {
namespace {
RECT nativeRect(const RuntimeInputPixelRect& r) { return {r.left,r.top,r.right,r.bottom}; }
class ResultBitmapSinkWin32 final : public ResultBitmapTextSink {
public:
    ResultBitmapSinkWin32(HDC dc,const RECT& client,COLORREF color,ResultBitmapScaleRectWin32 scale)
        : dc_(dc),client_(client),color_(color),scale_(scale) {}
    void beginText(const RuntimeInputPixelRect& base) noexcept override {
        const RECT clip = scale_(client_,nativeRect(base));
        saved_ = SaveDC(dc_);
        IntersectClipRect(dc_,clip.left,clip.top,clip.right,clip.bottom);
    }
    void beginPass(bool shadow) noexcept override { brush_ = CreateSolidBrush(shadow ? RGB(18,10,0) : color_); }
    void fillRun(const RuntimeInputPixelRect& run) noexcept override {
        const RECT scaled = scale_(client_,nativeRect(run)); FillRect(dc_,&scaled,brush_);
    }
    void endPass() noexcept override { DeleteObject(brush_); }
    void endText() noexcept override { RestoreDC(dc_,saved_); }
private:
    HDC dc_; const RECT& client_; COLORREF color_; ResultBitmapScaleRectWin32 scale_;
    int saved_ = 0; HBRUSH brush_ = nullptr;
};
}
void drawResultBitmapTextWin32(const ResultBitmapFontView& font,HDC dc,const RECT& client,
    RECT base,std::wstring text,COLORREF color,bool centered,ResultBitmapScaleRectWin32 scale) {
    ResultBitmapSinkWin32 sink(dc,client,color,scale);
    drawResultBitmapText(font,{base.left,base.top,base.right,base.bottom},std::move(text),centered,sink);
}
} // namespace concord::ui
