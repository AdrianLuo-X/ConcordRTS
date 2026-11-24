# H021-01: original overload response with synchronous typed owner ports.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")
concord_add_profiled_leaf(concord_mind_overload_contract_leaf
    src/simulation/abilities/psionic/RuntimeMindControlOverloadEffects.cpp concord_radar_cell_values_value_contract Contract)
add_subdirectory(tests/simulation/psionic/overload_effects)
add_test(NAME mind_overload_runtime_contract_test COMMAND concord_mind_overload_runtime_contract)
add_test(NAME mind_overload_contract_contract_test COMMAND concord_mind_overload_contract_contract)
set_tests_properties(mind_overload_runtime_contract_test mind_overload_contract_contract_test
    PROPERTIES TIMEOUT 120 LABELS "mind-overload;standard-contract;no-assets")
