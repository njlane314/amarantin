add_executable(mk_sample mk_sample.cc)

target_link_libraries(mk_sample
  PRIVATE
    IO
    amarantin_build_options
)

set_target_properties(mk_sample PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(APPLE)
  set_property(TARGET mk_sample PROPERTY
    INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
  set_property(TARGET mk_sample PROPERTY
    INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

install(TARGETS mk_sample
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
