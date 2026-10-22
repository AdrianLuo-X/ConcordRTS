# S14-01: compile the identical production codec and producer in ordinary contracts.
include_guard(GLOBAL)
if(NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "TMP contracts require their existing ordinary compiler owner")
endif()
add_executable(concord_tmp_codec_contract
    tests/infrastructure/RuntimeTmpCodecContract.cpp
    src/assets/codecs/RuntimeTmpCodec.cpp)
add_executable(concord_terrain_tmp_query_contract
    tests/infrastructure/RuntimeTerrainTmpQueryContract.cpp
    src/assets/codecs/RuntimeTmpCodec.cpp
    src/assets/terrain/RuntimeTerrainTmpQuery.cpp
    src/data/art/TheaterDatabase.cpp
    src/data/maps/MapDatabase.cpp
    src/core/io/IniFile.cpp)
foreach(tmp_contract IN ITEMS concord_tmp_codec_contract concord_terrain_tmp_query_contract)
    target_include_directories(${tmp_contract} PRIVATE src)
    target_compile_features(${tmp_contract} PRIVATE cxx_std_20)
    target_compile_options(${tmp_contract} PRIVATE
        "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(${tmp_contract} PRIVATE
        "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
    if(CONCORD_MSVC_ABI)
        set_property(TARGET ${tmp_contract} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endforeach()