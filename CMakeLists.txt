cmake_minimum_required(VERSION 3.20)

project(ConcordRTS LANGUAGES CXX)

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
