cmake_minimum_required(VERSION 3.20)

project(ConcordRTS LANGUAGES CXX)

option(CONCORD_PORTABLE_CORE "Build only real production core fixtures, without window/device backends" OFF)

if(CONCORD_PORTABLE_CORE)
    set(_concord_profile_identity portable-core)
else()
    set(_concord_profile_identity product)
endif()
if(DEFINED CONCORD_CONFIGURED_BUILD_PROFILE AND
        NOT CONCORD_CONFIGURED_BUILD_PROFILE STREQUAL _concord_profile_identity)
    message(FATAL_ERROR "Product/core caches must be separate; select a new build directory")
endif()
set(CONCORD_CONFIGURED_BUILD_PROFILE "${_concord_profile_identity}" CACHE INTERNAL "Owned build capability" FORCE)


# Keep this include order: these files share the root directory scope.
# include() preserves existing src/tests paths and target enumeration.
include(cmake/RuntimeSmokeTargets.cmake)
include(cmake/GameTargets.cmake)
include(cmake/DevelopmentTargets.cmake)
include(cmake/VideoBridge.cmake)
include(cmake/CompilerAndPlatform.cmake)
include(cmake/TestsRuntimeRegression.cmake)
include(cmake/TestsUnitAndNetwork.cmake)
include(cmake/TestsAssets.cmake)
include(cmake/MultiplayerIntegration.cmake)
include(cmake/TestsAdditionalRegression.cmake)
