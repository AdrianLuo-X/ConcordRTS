#pragma once

namespace superweapon_detail {
inline int superWeaponObjectCellKey(int x, int y) {
    // World objects and runtime saves use y*1000+x; IsoMapPack terrain
    // indices use 512 and must not be substituted here.
    return x < 0 || y < 0 ? -1 : y * 1000 + x;
}
}
