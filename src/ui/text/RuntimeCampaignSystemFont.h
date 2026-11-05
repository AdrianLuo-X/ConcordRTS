#pragma once
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/values/RuntimeColorValues.h"
#include <cstdint>
#include <string_view>
namespace concord::ui {
enum class CampaignSystemFontRole : std::uint8_t { Body, Small };
enum class CampaignSystemFontCapability : std::uint8_t { Unsupported, NativeText };
struct CampaignSystemFontColor {
 RuntimeRgbColorValue value; std::uint32_t controls=0;
 static constexpr CampaignSystemFontColor fromPacked(std::uint32_t color) noexcept {
  return {RuntimeRgbColorValue::fromPackedRgb(color),color&0xff000000U};
 }
 constexpr std::uint32_t packed() const noexcept {return value.packedRgb()|controls;}
};
template<class Character> struct BasicCampaignSystemTextRequest {
 std::basic_string_view<Character> text;
 RuntimeInputPixelRect rect;
 std::uint32_t format=0;
 CampaignSystemFontColor color;
};
using CampaignSystemTextRequest=BasicCampaignSystemTextRequest<char16_t>;
using CampaignSystemWideTextRequest=BasicCampaignSystemTextRequest<wchar_t>;
}
