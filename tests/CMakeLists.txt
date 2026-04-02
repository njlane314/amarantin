if(TARGET Plot)
  add_executable(plot_rigorous_check plot_rigorous_check.cc)

  target_link_libraries(plot_rigorous_check
    PRIVATE
      IO
      Plot
      amarantin_build_options
  )

  set_target_properties(plot_rigorous_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_test(NAME plot_rigorous_check
    COMMAND "${CMAKE_BINARY_DIR}/bin/plot_rigorous_check"
  )

  if(TARGET check)
    add_dependencies(check
      Plot
      plot_rigorous_check
    )
  endif()
endif()

if(TARGET IO)
  add_executable(io_rigorous_check io_rigorous_check.cc)

  target_link_libraries(io_rigorous_check
    PRIVATE
      IO
      SQLite::SQLite3
      amarantin_build_options
  )

  set_target_properties(io_rigorous_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_test(NAME io_rigorous_check
    COMMAND "${CMAKE_BINARY_DIR}/bin/io_rigorous_check"
  )

  if(TARGET check)
    add_dependencies(check
      IO
      io_rigorous_check
    )
  endif()
endif()

if(TARGET Syst)
  add_executable(systematics_rigorous_check systematics_rigorous_check.cc)

  target_link_libraries(systematics_rigorous_check
    PRIVATE
      IO
      Syst
      amarantin_build_options
  )

  set_target_properties(systematics_rigorous_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_test(NAME systematics_rigorous_check
    COMMAND "${CMAKE_BINARY_DIR}/bin/systematics_rigorous_check"
  )

  if(TARGET check)
    add_dependencies(check
      Syst
      systematics_rigorous_check
    )
  endif()
endif()
