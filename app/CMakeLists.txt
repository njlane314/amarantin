add_executable(mk_sample mk_sample.cc)
add_executable(mk_dataset mk_dataset.cc)
add_executable(mk_eventlist mk_eventlist.cc)
add_executable(mk_dist mk_dist.cc)
add_executable(mk_cov mk_cov.cc)

set(AMARANTIN_APP_TARGETS
  mk_sample
  mk_dataset
  mk_eventlist
  mk_dist
  mk_cov
)

if(AMARANTIN_BUILD_COLLIE_EXPORT)
  add_executable(mk_collie mk_collie.cc)
  target_link_libraries(mk_collie
    PRIVATE
      IO
      Collie::IO
      Eigen3::Eigen
      amarantin_build_options
  )
  set_property(TARGET mk_collie PROPERTY INSTALL_RPATH_USE_LINK_PATH TRUE)
  list(APPEND AMARANTIN_APP_TARGETS mk_collie)
endif()

target_link_libraries(mk_sample
  PRIVATE
    IO
    amarantin_build_options
)

target_link_libraries(mk_dataset
  PRIVATE
    Ana
    IO
    amarantin_build_options
)

target_link_libraries(mk_eventlist
  PRIVATE
    IO
    Ana
    amarantin_build_options
)

target_link_libraries(mk_dist
  PRIVATE
    IO
    Syst
    amarantin_build_options
)

target_link_libraries(mk_cov
  PRIVATE
    IO
    amarantin_build_options
)

foreach(target_name IN LISTS AMARANTIN_APP_TARGETS)

  set_target_properties(${target_name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  if(APPLE)
    set_property(TARGET ${target_name} PROPERTY
      INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
  elseif(UNIX)
    set_property(TARGET ${target_name} PROPERTY
      INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  endif()
endforeach()

install(TARGETS ${AMARANTIN_APP_TARGETS}
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
