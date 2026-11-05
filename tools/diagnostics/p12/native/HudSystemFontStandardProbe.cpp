#include "ui/text/RuntimeHudSystemFont.h"
#include <cstddef>
#include <type_traits>
using namespace concord::ui;
static_assert(sizeof(char16_t)==2);
static_assert(sizeof(HudSystemFontRole)==1&&sizeof(HudSystemFontCapability)==1);
static_assert(sizeof(RuntimeInputPixelRect)==16&&alignof(RuntimeInputPixelRect)==4);
static_assert(std::is_standard_layout_v<HudSystemTextRequest>);
static_assert(std::is_trivially_copyable_v<HudSystemTextRequest>);
static_assert(sizeof(HudSystemTextRequest)==32&&alignof(HudSystemTextRequest)==8);
static_assert(offsetof(HudSystemTextRequest,text)==0&&offsetof(HudSystemTextRequest,count)==8);
static_assert(offsetof(HudSystemTextRequest,rect)==12&&offsetof(HudSystemTextRequest,format)==28);
static_assert(sizeof(HudSystemTextExtent)==12&&alignof(HudSystemTextExtent)==4);
constexpr HudSystemTextRequest empty{};
static_assert(empty.text==nullptr&&empty.count==-1&&empty.format==0);
static_assert(empty.rect.left==0&&empty.rect.top==0&&empty.rect.right==0&&empty.rect.bottom==0);
