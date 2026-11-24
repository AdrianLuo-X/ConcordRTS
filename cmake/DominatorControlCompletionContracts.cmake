# H015-01: original permanent-control completion after the owner transaction.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")
concord_add_profiled_leaf(concord_dom_control_contract_leaf
    src/simulation/abilities/psionic/RuntimeDominatorControlCompletion.cpp concord_radar_cell_values_value_contract Contract)
add_subdirectory(tests/simulation/psionic/dominator_control)
add_test(NAME dom_control_runtime_contract_test COMMAND concord_dom_control_runtime_contract)
add_test(NAME dom_control_contract_contract_test COMMAND concord_dom_control_contract_contract)
set_tests_properties(dom_control_runtime_contract_test dom_control_contract_contract_test
    PROPERTIES TIMEOUT 120 LABELS "dominator-control;standard-contract;no-assets")
