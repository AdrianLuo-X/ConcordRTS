#pragma once

struct RuntimeCellPoint {
    int x = -1;
    int y = -1;
};

inline bool operator==(const RuntimeCellPoint& left, const RuntimeCellPoint& right) {
    return left.x == right.x && left.y == right.y;
}
inline bool operator!=(const RuntimeCellPoint& left, const RuntimeCellPoint& right) {
    return !(left == right);
}

struct RuntimeCellPointLess {
    bool operator()(const RuntimeCellPoint& left, const RuntimeCellPoint& right) const {
        if (left.y != right.y) {
            return left.y < right.y;
        }
        return left.x < right.x;
    }
};
