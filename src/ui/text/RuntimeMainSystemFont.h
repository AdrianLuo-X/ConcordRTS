#pragma once
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/values/RuntimeColorValues.h"
#include <cstdint>
#include <string_view>
namespace concord::ui {
enum class MainSystemFontRole : std::uint8_t { Button, Title, Small };
enum class MainSystemFontCapability : std::uint8_t { Unsupported, NativeText };
// RGB data reuses the existing value. Legacy high control bits are transported
// unchanged to the private adapter; the standard request never interprets them.
struct MainSystemFontColors {
 RuntimeRgbColorValue dark, middle;
 std::uint32_t darkControls=0, middleControls=0;
 static constexpr MainSystemFontColors fromPacked(std::uint32_t dark,std::uint32_t middle) noexcept {
  return {RuntimeRgbColorValue::fromPackedRgb(dark),RuntimeRgbColorValue::fromPackedRgb(middle),
      dark&0xff000000U,middle&0xff000000U};
 }
 constexpr std::uint32_t packedDark() const noexcept {return dark.packedRgb()|darkControls;}
 constexpr std::uint32_t packedMiddle() const noexcept {return middle.packedRgb()|middleControls;}
};
// Synchronous text borrow, including explicit counts/embedded nulls. The UTF16
// view is portable; wide view preserves the actual Windows producer without a
// per-draw allocation. A provider reports its native text capability explicitly.
template<class Character> struct BasicMainSystemTextRequest {
 std::basic_string_view<Character> text;
 RuntimeInputPixelRect rect;
 std::uint32_t format=0;
 MainSystemFontColors colors;
};
using MainSystemTextRequest=BasicMainSystemTextRequest<char16_t>;
using MainSystemWideTextRequest=BasicMainSystemTextRequest<wchar_t>;
// A synchronous, NUL-terminated text borrow. This preserves the video's
// original DrawText count=-1 policy; the caller keeps the terminator alive.
template<class Character> struct BasicMainStockFooterTextRequest {
 const Character* nulTerminatedText=nullptr;
 RuntimeInputPixelRect rect;
};
using MainStockFooterTextRequest=BasicMainStockFooterTextRequest<char16_t>;
using MainStockFooterWideTextRequest=BasicMainStockFooterTextRequest<wchar_t>;
}
