#pragma once
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/values/RuntimeColorValues.h"
#include <cstdint>
#include <string_view>
namespace concord::ui {
enum class MatchResultSystemFontCapability : std::uint8_t { Unsupported, NativeText };
template<class Character> struct BasicMatchResultSystemTextRequest {
 std::basic_string_view<Character> text;
 RuntimeInputPixelRect rect;
 std::uint32_t format=0;
 RuntimeRgbColorValue color;
 std::uint32_t colorControls=0;
 static constexpr BasicMatchResultSystemTextRequest fromPacked(
  std::basic_string_view<Character> text,RuntimeInputPixelRect rect,std::uint32_t format,std::uint32_t color) noexcept {
  return {text,rect,format,RuntimeRgbColorValue::fromPackedRgb(color),color&0xff000000U};
 }
 constexpr std::uint32_t packedColor() const noexcept {return color.packedRgb()|colorControls;}
};
using MatchResultSystemTextRequest=BasicMatchResultSystemTextRequest<char16_t>;
using MatchResultSystemWideTextRequest=BasicMatchResultSystemTextRequest<wchar_t>;
}
