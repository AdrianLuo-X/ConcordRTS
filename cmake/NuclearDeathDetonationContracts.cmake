# H012: original synchronous nuclear-death policy with typed owner borrows.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")
concord_add_profiled_leaf(concord_nuclear_death_contract_leaf
    src/simulation/superweapons/RuntimeNuclearDeathDetonation.cpp concord_radar_cell_values_value_contract Contract)
add_subdirectory(tests/simulation/superweapons/nuclear_death)
add_test(NAME nuclear_death_runtime_contract_test COMMAND concord_nuclear_death_runtime_contract)
add_test(NAME nuclear_death_contract_contract_test COMMAND concord_nuclear_death_contract_contract)
set_tests_properties(nuclear_death_runtime_contract_test nuclear_death_contract_contract_test
    PROPERTIES TIMEOUT 120 LABELS "nuclear-death;standard-contract;no-assets")
