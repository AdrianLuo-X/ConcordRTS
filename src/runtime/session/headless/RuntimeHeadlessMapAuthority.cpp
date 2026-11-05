#include "runtime/menu/detail/RuntimeSkirmishMapResources.h"

int resolveRuntimeMapStartingPointLimit(const RuntimeConfig& config) {
    return concord::skirmish::map_detail::resolveMapStartingPointLimit(config, nullptr);
}
