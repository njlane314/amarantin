cmake_minimum_required(VERSION 3.16)

project(amarantin VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not supported. "
    "Use: cmake -S . -B build"
  )
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

option(AMARANTIN_BUILD_APP "Build the mk_sample application" ON)

find_package(SQLite3 REQUIRED)

add_library(amarantin_root_deps INTERFACE)

find_program(ROOT_CONFIG_EXECUTABLE NAMES root-config)
if(ROOT_DIR)
  find_package(ROOT REQUIRED CONFIG COMPONENTS Core RIO Tree Hist)
  target_link_libraries(amarantin_root_deps INTERFACE
    ROOT::Core
    ROOT::RIO
    ROOT::Tree
    ROOT::Hist
  )
elseif(ROOT_CONFIG_EXECUTABLE)
  execute_process(
    COMMAND "${ROOT_CONFIG_EXECUTABLE}" --prefix
    OUTPUT_VARIABLE ROOT_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(
    COMMAND "${ROOT_CONFIG_EXECUTABLE}" --incdir
    OUTPUT_VARIABLE ROOT_INCLUDEDIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(
    COMMAND "${ROOT_CONFIG_EXECUTABLE}" --cflags
    OUTPUT_VARIABLE ROOT_CFLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(
    COMMAND "${ROOT_CONFIG_EXECUTABLE}" --libs
    OUTPUT_VARIABLE ROOT_LIBS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  separate_arguments(ROOT_CFLAGS NATIVE_COMMAND "${ROOT_CFLAGS_RAW}")
  separate_arguments(ROOT_LIBS NATIVE_COMMAND "${ROOT_LIBS_RAW}")

  target_include_directories(amarantin_root_deps INTERFACE "${ROOT_INCLUDEDIR}")
  target_compile_options(amarantin_root_deps INTERFACE ${ROOT_CFLAGS})
  target_link_libraries(amarantin_root_deps INTERFACE ${ROOT_LIBS})
else()
  message(FATAL_ERROR
    "ROOT not found. Source your ROOT environment, set ROOT_DIR, "
    "or put root-config on PATH."
  )
endif()

find_package(nlohmann_json QUIET)
set(NLOHMANN_JSON_INC "" CACHE PATH
  "Optional path to nlohmann_json headers when no CMake package is available")

add_library(amarantin_build_options INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(amarantin_build_options INTERFACE -Wall -Wextra)
elseif(MSVC)
  target_compile_options(amarantin_build_options INTERFACE /W4)
endif()

add_subdirectory(io)
add_subdirectory(plot)
if(AMARANTIN_BUILD_APP)
  add_subdirectory(app)
endif()

configure_package_config_file(
  cmake/amarantinConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/amarantinConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/amarantin"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/amarantinConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(EXPORT amarantinTargets
  FILE amarantinTargets.cmake
  NAMESPACE amarantin::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/amarantin"
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/amarantinConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/amarantinConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/amarantin"
)
