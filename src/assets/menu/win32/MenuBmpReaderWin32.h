#pragma once
#include "assets/menu/MenuBmpReader.h"
namespace concord::assets {
// Private native provider. No device identity crosses the standard interface.
const MenuBmpReader& menuWin32BmpReader() noexcept;
}
