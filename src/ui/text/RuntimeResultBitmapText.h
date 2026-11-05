#pragma once
#include "assets/text/WestwoodFontCodec.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include <span>
#include <string>
#include <string_view>
namespace concord::ui {
// Synchronous borrow of the original result font's bytes and decoded metrics.
// Result text deliberately visits each UTF16 code unit, including surrogates.
struct ResultBitmapFontView {
    std::span<const unsigned char> bytes;
    concord::assets::WestwoodFontMetrics metrics;
};
// Logical800x600 rectangles, half-open. A consumer applies its existing scale,
// clip and shadow/face color; no device or native color semantics enter layout.
class ResultBitmapTextSink {
public:
    virtual ~ResultBitmapTextSink() = default;
    virtual void beginText(const RuntimeInputPixelRect& clip) noexcept = 0;
    virtual void beginPass(bool shadow) noexcept = 0;
    virtual void fillRun(const RuntimeInputPixelRect&) noexcept = 0;
    virtual void endPass() noexcept = 0;
    virtual void endText() noexcept = 0;
};
std::int32_t measureResultBitmapText(const ResultBitmapFontView&, std::wstring_view);
std::int32_t measureResultBitmapText(const ResultBitmapFontView&, std::u16string_view);
// Text is the original owned working copy for suffix truncation. Preserve the
// representable signed32 layout domain and original even-too-narrow ellipsis.
void drawResultBitmapText(const ResultBitmapFontView&, const RuntimeInputPixelRect&,
    std::wstring text, bool centered, ResultBitmapTextSink&);
void drawResultBitmapText(const ResultBitmapFontView&, const RuntimeInputPixelRect&,
    std::u16string text, bool centered, ResultBitmapTextSink&);
} // namespace concord::ui
