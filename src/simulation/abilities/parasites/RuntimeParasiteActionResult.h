#pragma once

// The existing result is a value contract; retain its C++ type identity.
namespace concord::runtime::session_detail {
struct RuntimeParasiteActionApplyResult {
    bool accepted = true;
    bool changed = false;
};
}
