# H039: the original synchronous load transaction policy has one producer per ABI.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")
concord_add_profiled_leaf(concord_load_transaction_runtime_leaf
    src/runtime/persistence/RuntimeLoadTransaction.cpp concord_runtime Runtime)
concord_add_profiled_leaf(concord_load_transaction_contract_leaf
    src/runtime/persistence/RuntimeLoadTransaction.cpp concord_superweapon_smoke Contract)
target_link_libraries(concord_runtime PRIVATE concord_load_transaction_runtime_leaf)
add_subdirectory(tests/infrastructure/load_transaction)
