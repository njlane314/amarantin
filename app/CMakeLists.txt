add_executable(mk_sample mk_sample.cc)
add_executable(mk_dataset mk_dataset.cc)
add_executable(mk_eventlist mk_eventlist.cc)

foreach(target_name mk_sample mk_dataset mk_eventlist)
  target_link_libraries(${target_name}
    PRIVATE
      IO
      amarantin_build_options
  )

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

install(TARGETS mk_sample mk_dataset mk_eventlist
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
