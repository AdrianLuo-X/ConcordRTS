# Original Main/EVA process-global asynchronous sound; no perowner cleanup.
include_guard(GLOBAL)
concord_add_profiled_leaf(concord_async_sound_unsupported src/audio/backend/RuntimeUnsupportedAsyncSoundDevice.cpp concord_runtime Runtime)
concord_add_profiled_leaf(concord_async_sound_unsupported_contract src/audio/backend/RuntimeUnsupportedAsyncSoundDevice.cpp concord_runtime_match_score_contract Contract)
if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
 concord_add_profiled_leaf(concord_async_sound_win32 src/audio/backend/win32/RuntimeAsyncSoundDeviceWin32.cpp concord_runtime Runtime)
 concord_add_profiled_leaf(concord_async_sound_win32_contract src/audio/backend/win32/RuntimeAsyncSoundDeviceWin32.cpp concord_runtime_match_score_contract Contract)
 target_link_libraries(concord_async_sound_win32 PRIVATE concord_async_sound_unsupported)
 target_link_libraries(concord_async_sound_win32_contract PRIVATE concord_async_sound_unsupported_contract winmm)
 target_link_libraries(concord_runtime PRIVATE concord_async_sound_win32)
endif()
add_subdirectory(tests/audio/async_sound_device)

if(WIN32 AND NOT CONCORD_PORTABLE_CORE)
 add_test(NAME runtime_eva_playsound_fallback_smoke_test
  COMMAND concord_runtime --command-smoke-test
  --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
  --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
  --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
  --config "${CMAKE_SOURCE_DIR}/tests/fixtures/ui_clock_runtime.ini")
 set_tests_properties(runtime_eva_playsound_fallback_smoke_test PROPERTIES
  TIMEOUT 120 RUN_SERIAL TRUE LABELS "windows;audio;real-production;explicit-input"
  ENVIRONMENT "CONCORD_COMMAND_SMOKE_EVA_PLAYSOUND_FALLBACK_ONLY=1")
endif()
