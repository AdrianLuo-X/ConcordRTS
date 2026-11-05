#include "ui/network/RuntimeInternetMenuSkin.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <limits>
static_assert(sizeof(RuntimeInputPixelRect)==16);
static_assert(alignof(RuntimeInputPixelRect)==4);
static_assert(offsetof(RuntimeInputPixelRect,left)==0);
static_assert(offsetof(RuntimeInputPixelRect,top)==4);
static_assert(offsetof(RuntimeInputPixelRect,right)==8);
static_assert(offsetof(RuntimeInputPixelRect,bottom)==12);
static_assert(std::is_abstract_v<RuntimeInternetSkinDrawTarget>);
static_assert(!std::is_copy_constructible_v<RuntimeInternetSkinDrawTarget>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelRect::left),std::int32_t>);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::right)>::digits==31);
static_assert(sizeof(RuntimeInternetSkinDrawingCapability)==4);
