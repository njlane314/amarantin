set(AMARANTIN_BOOK_SOURCES
  src/SampleBook.cc
)

set(AMARANTIN_BOOK_PUBLIC_HEADERS
  include/SampleBook.hh
  include/SampleCard.hh
)

add_library(Book SHARED
  ${AMARANTIN_BOOK_SOURCES}
  ${AMARANTIN_BOOK_PUBLIC_HEADERS}
)

target_compile_features(Book PUBLIC cxx_std_17)

target_include_directories(Book
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/amarantin>
)

target_link_libraries(Book
  PUBLIC
    IO
    amarantin_root_deps
  PRIVATE
    amarantin_build_options
)

if(nlohmann_json_FOUND)
  target_link_libraries(Book PRIVATE nlohmann_json::nlohmann_json)
elseif(NLOHMANN_JSON_INC)
  target_include_directories(Book PRIVATE "${NLOHMANN_JSON_INC}")
endif()

set_target_properties(Book PROPERTIES
  OUTPUT_NAME Book
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS Book
  EXPORT amarantinTargets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/amarantin"
)

install(FILES ${AMARANTIN_BOOK_PUBLIC_HEADERS}
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/amarantin"
)
