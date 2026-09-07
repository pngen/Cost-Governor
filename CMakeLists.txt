cmake_minimum_required(VERSION 3.20)
project(CostGovernor VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(CG_BUILD_TESTS "Build unit/property/adversarial/concurrency tests" ON)
option(CG_BUILD_EXAMPLES "Build runnable examples" ON)
option(CG_BUILD_TOOLS "Build the cg CLI" ON)
option(CG_BUILD_BENCH "Build benchmarks" ON)
option(CG_BUILD_DISTRIBUTED "Build the real multiprocess distributed proof" ON)
option(CG_BUILD_CUDA "Build the real CUDA proof (requires CUDA + NVIDIA device)" OFF)
option(CG_BUILD_ASAN "Enable AddressSanitizer across all built targets" OFF)

# Real AddressSanitizer (MSVC: /fsanitize=address with debug info so the
# C5072 "ASAN enabled without debug information" warning is satisfied under
# /WX; the per-target /W4 /WX warnings remain in force). Appending here (not
# replacing CMAKE_CXX_FLAGS) preserves the default /EHsc exception model.
if(CG_BUILD_ASAN AND MSVC)
  add_compile_options(/fsanitize=address /Zi)
  add_link_options(/fsanitize=address)
elseif(CG_BUILD_ASAN)
  add_compile_options(-fsanitize=address -g)
  add_link_options(-fsanitize=address)
endif()

find_package(Threads REQUIRED)

add_library(costgovernor
  src/clock.cpp
  src/cost.cpp
  src/persistence.cpp
  src/governor.cpp
)
add_library(CostGovernor::CostGovernor ALIAS costgovernor)
target_include_directories(costgovernor PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(costgovernor PUBLIC cxx_std_20)
target_link_libraries(costgovernor PRIVATE Threads::Threads)
set_target_properties(costgovernor PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON EXPORT_NAME CostGovernor)

if(MSVC)
  target_compile_options(costgovernor PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(costgovernor PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

if(CG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(CG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(CG_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(CG_BUILD_BENCH)
  add_subdirectory(bench)
endif()
if(CG_BUILD_DISTRIBUTED)
  add_subdirectory(distributed)
endif()
if(CG_BUILD_CUDA)
  add_subdirectory(cuda)
endif()

# --- installation / downstream package -----------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS costgovernor EXPORT CostGovernorTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/costgovernor DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT CostGovernorTargets
  FILE CostGovernorTargets.cmake
  NAMESPACE CostGovernor::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CostGovernor)

configure_package_config_file(cmake/CostGovernorConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CostGovernorConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CostGovernor)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/CostGovernorConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/CostGovernorConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/CostGovernorConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CostGovernor)

install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/CostGovernor)
