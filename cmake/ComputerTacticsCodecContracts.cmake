# H004: one source per formal S11 Runtime/Contract ABI profile.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")

concord_add_profiled_leaf(concord_computer_tactics_runtime_leaf
    src/simulation/ai/RuntimeComputerTacticsCodec.cpp concord_runtime Runtime)
concord_add_profiled_leaf(concord_computer_tactics_contract_leaf
    src/simulation/ai/RuntimeComputerTacticsCodec.cpp concord_computer_mining_codec_contract Contract)
target_link_libraries(concord_computer_tactics_runtime_leaf PRIVATE concord_ini_runtime_leaf)
target_link_libraries(concord_computer_tactics_contract_leaf PRIVATE concord_ini_contract_leaf)
target_link_libraries(concord_runtime PRIVATE concord_computer_tactics_runtime_leaf)

# Local directory scope keeps Windows GUI defaults out of these pure fixtures.
add_subdirectory(tests/infrastructure/computer_tactics_codec)
