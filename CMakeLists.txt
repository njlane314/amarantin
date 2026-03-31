cmake_minimum_required(VERSION 3.16)

project(amarantin VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckIncludeFileCXX)

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

option(AMARANTIN_BUILD_APP "Build the CLI applications" ON)

find_package(SQLite3 REQUIRED)

add_library(amarantin_root_deps INTERFACE)

find_program(ROOT_CONFIG_EXECUTABLE NAMES root-config)
if(ROOT_DIR)
  find_package(ROOT REQUIRED CONFIG COMPONENTS Core RIO Tree Hist MathCore Minuit2)
  target_link_libraries(amarantin_root_deps INTERFACE
    ROOT::Core
    ROOT::RIO
    ROOT::Tree
    ROOT::Hist
    ROOT::MathCore
    ROOT::Minuit2
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
  list(APPEND ROOT_LIBS MathCore Minuit2)

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
if(NOT nlohmann_json_FOUND AND NOT NLOHMANN_JSON_INC)
  set(AMARANTIN_NLOHMANN_JSON_HINTS "")
  if(DEFINED ENV{NLOHMANN_JSON_INC} AND NOT "$ENV{NLOHMANN_JSON_INC}" STREQUAL "")
    list(APPEND AMARANTIN_NLOHMANN_JSON_HINTS "$ENV{NLOHMANN_JSON_INC}")
  endif()
  if(DEFINED ENV{NLOHMANN_JSON_FQ_DIR} AND NOT "$ENV{NLOHMANN_JSON_FQ_DIR}" STREQUAL "")
    list(APPEND AMARANTIN_NLOHMANN_JSON_HINTS "$ENV{NLOHMANN_JSON_FQ_DIR}/include")
  endif()
  if(DEFINED ENV{NLOHMANN_JSON_DIR} AND NOT "$ENV{NLOHMANN_JSON_DIR}" STREQUAL "")
    list(APPEND AMARANTIN_NLOHMANN_JSON_HINTS "$ENV{NLOHMANN_JSON_DIR}/include")
  endif()
  find_path(AMARANTIN_NLOHMANN_JSON_INC
    NAMES nlohmann/json.hpp
    HINTS
      ${AMARANTIN_NLOHMANN_JSON_HINTS}
  )
  if(AMARANTIN_NLOHMANN_JSON_INC)
    set(NLOHMANN_JSON_INC "${AMARANTIN_NLOHMANN_JSON_INC}" CACHE PATH
      "Optional path to nlohmann_json headers when no CMake package is available" FORCE)
  endif()
endif()
if(NOT nlohmann_json_FOUND AND NOT NLOHMANN_JSON_INC)
  check_include_file_cxx("nlohmann/json.hpp" AMARANTIN_HAS_NLOHMANN_JSON_HEADER)
  if(NOT AMARANTIN_HAS_NLOHMANN_JSON_HEADER)
    message(FATAL_ERROR
      "nlohmann/json.hpp not found. Source ./.setup.sh, run setup nlohmann_json, "
      "or configure with -DNLOHMANN_JSON_INC=/path/to/include."
    )
  endif()
endif()

add_library(amarantin_build_options INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(amarantin_build_options INTERFACE -Wall -Wextra)
elseif(MSVC)
  target_compile_options(amarantin_build_options INTERFACE /W4)
endif()

add_subdirectory(io)
add_subdirectory(ana)
add_subdirectory(syst)
add_subdirectory(plot)
add_subdirectory(fit)
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
