cmake_minimum_required(VERSION 3.24)
project(ChaoticGamingAtlas VERSION 0.2.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The initial ATLAS plugin target is Windows-only.")
endif()

# Pin the ATLAS API source and import library together; do not track a moving branch.
include(FetchContent)
set(CMG_ATLAS_API_COMMIT "a757d0c256e6d221742fb131d46d8049c12815a6" CACHE STRING "Pinned cmg-atlas-api commit")
if(CMG_ATLAS_API_COMMIT STREQUAL "144ee5301177428894eda4c318276f52f051d3f5" OR
   CMG_ATLAS_API_COMMIT STREQUAL "1bc2be00b5841069d816ff6c9d85486b13cfc252")
  set(CMG_ATLAS_API_COMMIT "a757d0c256e6d221742fb131d46d8049c12815a6" CACHE STRING "Pinned cmg-atlas-api commit" FORCE)
endif()
FetchContent_Declare(cmg_atlas_api
  GIT_REPOSITORY https://github.com/Chaotic-Minds-Collective/cmg-atlas-api.git
  GIT_TAG ${CMG_ATLAS_API_COMMIT}
  GIT_PROGRESS TRUE
  SOURCE_SUBDIR _cmake_not_used
)
FetchContent_MakeAvailable(cmg_atlas_api)

if(NOT EXISTS "${cmg_atlas_api_SOURCE_DIR}/version/Core/Public/API/Atlas/Atlas.h")
  message(FATAL_ERROR "Pinned ATLAS SDK headers were not found.")
endif()
if(NOT EXISTS "${cmg_atlas_api_SOURCE_DIR}/out_lib/AtlasApi.lib")
  message(FATAL_ERROR "Pinned ATLAS SDK import library was not found.")
endif()

add_library(cmg_atlas_sdk_import UNKNOWN IMPORTED GLOBAL)
set_target_properties(cmg_atlas_sdk_import PROPERTIES
  IMPORTED_LOCATION "${cmg_atlas_api_SOURCE_DIR}/out_lib/AtlasApi.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${cmg_atlas_api_SOURCE_DIR}/version/Core/Public"
  INTERFACE_COMPILE_DEFINITIONS "UNICODE;_UNICODE"
)

add_library(ChaoticGamingCore SHARED
  plugins/ChaoticGamingCore/src/plugin.cpp
  plugins/ChaoticGamingCore/src/atlas_sdk_link_check.cpp
)
target_compile_features(ChaoticGamingCore PRIVATE cxx_std_17)
target_include_directories(ChaoticGamingCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ChaoticGamingCore PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  # Match the pinned SDK's Unicode character set for Windows and CRT headers.
  UNICODE
  _UNICODE
  CG_PLUGIN_VERSION="${PROJECT_VERSION}"
  CG_ATLAS_API_VERSION=1.71
)
target_link_libraries(ChaoticGamingCore PRIVATE cmg_atlas_sdk_import winhttp)

set_target_properties(ChaoticGamingCore PROPERTIES
  PREFIX ""
  OUTPUT_NAME "ChaoticGamingCore"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/ChaoticGamingCore"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/ChaoticGamingCore"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugins/ChaoticGamingCore"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugins/ChaoticGamingCore"
)

# Portable domain contracts are compiled and tested alongside the Windows plugin.
add_library(chaotic_atlas_contracts INTERFACE)
target_include_directories(chaotic_atlas_contracts INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(chaotic_atlas_contracts INTERFACE cxx_std_17)

include(CTest)
if(BUILD_TESTING)
  add_executable(atlas_contracts_test tests/contracts_test.cpp)
  target_link_libraries(atlas_contracts_test PRIVATE chaotic_atlas_contracts)
  add_test(NAME atlas_contracts_test COMMAND atlas_contracts_test)
endif()
