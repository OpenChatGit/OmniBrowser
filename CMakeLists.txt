cmake_minimum_required(VERSION 3.21)

set(CMAKE_CONFIGURATION_TYPES Debug Release)
project(OmniBrowser LANGUAGES C CXX)

# CEF binary flags pin MSVC to /std:c++17 for targets.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# CEF sandbox complicates packaging (bootstrap DLL). v1 builds a plain EXE.
set(USE_SANDBOX OFF CACHE BOOL "Enable CEF sandbox" FORCE)

set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef")
if(NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
  message(FATAL_ERROR
    "CEF not found at ${CEF_ROOT}.\n"
    "Run: powershell -ExecutionPolicy Bypass -File scripts/download_cef.ps1")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
add_subdirectory(native)

PRINT_CEF_CONFIG()
