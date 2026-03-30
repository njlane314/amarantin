set(AMARANTIN_SYST_SOURCES
  Systematics.cc
)

set(AMARANTIN_SYST_PUBLIC_HEADERS
  Systematics.hh
  SystematicsCacheBuilder.hh
)

add_library(Syst SHARED
  ${AMARANTIN_SYST_SOURCES}
  ${AMARANTIN_SYST_PUBLIC_HEADERS}
)

find_package(Eigen3 QUIET)

target_compile_features(Syst PUBLIC cxx_std_17)

target_include_directories(Syst
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/amarantin>
)

target_link_libraries(Syst
  PUBLIC
    IO
    amarantin_root_deps
  PRIVATE
    Ana
    amarantin_build_options
)

if(Eigen3_FOUND)
  target_link_libraries(Syst PRIVATE Eigen3::Eigen)
  target_compile_definitions(Syst PRIVATE AMARANTIN_HAVE_EIGEN=1)
endif()

set_target_properties(Syst PROPERTIES
  OUTPUT_NAME Syst
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS Syst
  EXPORT amarantinTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/amarantin"
)

install(FILES ${AMARANTIN_SYST_PUBLIC_HEADERS}
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/amarantin"
)
