#pragma once
#include <string>
struct RuntimeMapRenderContext;
namespace concord::grand_cannon::testing {
// Explicit diagnostic-only owner state, never read by the raster worker.
inline thread_local bool verifying = false;
inline thread_local int failure = 0;
}
bool verifyRuntimeGrandCannonPrewarm(const RuntimeMapRenderContext&, std::string&);
