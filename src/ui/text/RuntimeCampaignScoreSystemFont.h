#pragma once
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/values/RuntimeColorValues.h"
#include <cstdint>
#include <string_view>
namespace concord::ui {
enum class CampaignScoreFontPage:std::uint8_t {Fallback,OriginalArt};
enum class CampaignScoreFontRole:std::uint8_t {Title,Heading,Body,Number,Message,Label,Value};
enum class CampaignScoreSystemFontCapability:std::uint8_t {Unsupported,NativeText};
template<class Character> struct BasicCampaignScoreSystemTextRequest {
 CampaignScoreFontRole role=CampaignScoreFontRole::Title;
 std::basic_string_view<Character> text;
 RuntimeInputPixelRect rect;
 std::uint32_t format=0;
 RuntimeRgbColorValue color;
 std::uint32_t colorControls=0;
 static constexpr BasicCampaignScoreSystemTextRequest fromPacked(CampaignScoreFontRole role,
  std::basic_string_view<Character> text,RuntimeInputPixelRect rect,std::uint32_t format,std::uint32_t color) noexcept {
  return {role,text,rect,format,RuntimeRgbColorValue::fromPackedRgb(color),color&0xff000000U};
 }
 constexpr std::uint32_t packedColor()const noexcept{return color.packedRgb()|colorControls;}
};
using CampaignScoreSystemTextRequest=BasicCampaignScoreSystemTextRequest<char16_t>;
using CampaignScoreSystemWideTextRequest=BasicCampaignScoreSystemTextRequest<wchar_t>;
}
