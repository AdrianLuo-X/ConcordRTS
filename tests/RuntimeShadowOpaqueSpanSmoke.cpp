#include "RuntimeShadowOpaqueSpan.h"

#include <iostream>
#include <string>

#include "RuntimeShadowOpaqueSpanContract.inl"

int main() {
    std::string error;
    if (!runRuntimeShadowOpaqueSpanContractCheck(error)) {
        std::cerr << "Shadow opaque span contract failed: " << error << '\n';
        return 1;
    }
    std::cout << "Shadow opaque span contract passed: exhaustive 256x256 blend, "
        "signature invalidation, bounded fallback, clipped pixels, overlap, counts\n";
    return 0;
}
