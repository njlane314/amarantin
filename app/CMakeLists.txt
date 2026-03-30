add_executable(mk_sample mk_sample.cc)
add_executable(mk_dataset mk_dataset.cc)
add_executable(mk_eventlist mk_eventlist.cc)

target_link_libraries(mk_sample
  PRIVATE
    IO
    amarantin_build_options
)

target_link_libraries(mk_dataset
  PRIVATE
    Book
    IO
    amarantin_build_options
)

target_link_libraries(mk_eventlist
  PRIVATE
    IO
    Ana
    Syst
    amarantin_build_options
)

foreach(target_name mk_sample mk_dataset mk_eventlist)

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
