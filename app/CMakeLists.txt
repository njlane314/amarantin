add_executable(mk_sample mk_sample.cc)
add_executable(mk_dataset mk_dataset.cc)
add_executable(mk_eventlist mk_eventlist.cc)
add_executable(mk_dist mk_dist.cc)
add_executable(mk_xsec_fit mk_xsec_fit.cc)

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

target_link_libraries(mk_xsec_fit
  PRIVATE
    Fit
    amarantin_build_options
)

foreach(target_name mk_sample mk_dataset mk_eventlist mk_dist mk_xsec_fit)

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

install(TARGETS mk_sample mk_dataset mk_eventlist mk_dist mk_xsec_fit
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
