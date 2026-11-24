# H008: original AI base plan policy with no host state.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")
concord_add_profiled_leaf(concord_computer_base_plan_contract_leaf
    src/simulation/ai/RuntimeComputerBasePlan.cpp concord_radar_cell_values_value_contract Contract)
add_subdirectory(tests/simulation/ai/base_plan)
add_test(NAME computer_base_plan_runtime_contract_test COMMAND concord_computer_base_plan_runtime_contract)
add_test(NAME computer_base_plan_contract_contract_test COMMAND concord_computer_base_plan_contract_contract)
set_tests_properties(computer_base_plan_runtime_contract_test computer_base_plan_contract_contract_test
    PROPERTIES TIMEOUT 120 LABELS "computer-base-plan;standard-contract;no-assets")
