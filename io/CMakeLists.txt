set(AMARANTIN_IO_SOURCES
  src/ArtProvenanceIO.cc
  src/DatasetIO.cc
  src/EventListIO.cc
  src/RunDatabaseService.cc
  src/SampleIO.cc
)

set(AMARANTIN_IO_HEADERS
  include/ArtProvenanceIO.hh
  include/DatasetIO.hh
  include/EventListIO.hh
  include/RootUtils.hh
  include/RunDatabaseService.hh
  include/SampleIO.hh
)

add_library(IO SHARED
  ${AMARANTIN_IO_SOURCES}
  ${AMARANTIN_IO_HEADERS}
)

target_compile_features(IO PUBLIC cxx_std_17)

target_include_directories(IO
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/amarantin>
)

target_link_libraries(IO
  PUBLIC
    amarantin_root_deps
  PRIVATE
    SQLite::SQLite3
    amarantin_build_options
)

if(nlohmann_json_FOUND)
  target_link_libraries(IO PRIVATE nlohmann_json::nlohmann_json)
elseif(NLOHMANN_JSON_INC)
  target_include_directories(IO PRIVATE "${NLOHMANN_JSON_INC}")
endif()

set_target_properties(IO PROPERTIES
  OUTPUT_NAME IO
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS IO
  EXPORT amarantinTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/amarantin"
)

install(TARGETS amarantin_root_deps
  EXPORT amarantinTargets
)

install(FILES ${AMARANTIN_IO_HEADERS}
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/amarantin"
)
