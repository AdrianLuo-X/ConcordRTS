if(CONCORD_MSVC_FRONTEND)
    # /RTC1 cannot be combined with the optimized playable Debug runtime.
    # cl.exe adds it back only to the non-optimized command-line tool below.
    # clang-cl has no /RTC1 implementation; its tool target reports that omission.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
