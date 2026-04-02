set(AMARANTIN_TEST_ROOT_FIXTURE "${CMAKE_SOURCE_DIR}/test.root" CACHE FILEPATH
  "Optional path to the test.root fixture used by the end-to-end smoke")

if(NOT TARGET mk_sample OR
   NOT TARGET mk_dataset OR
   NOT TARGET mk_eventlist OR
   NOT TARGET mk_dist OR
   NOT TARGET mk_fit OR
   NOT TARGET mk_cov OR
   NOT TARGET IO OR
   NOT TARGET Ana OR
   NOT TARGET Plot OR
   NOT TARGET Syst OR
   NOT TARGET Fit)
  message(STATUS
    "amarantin: skipping test.root fixture smoke; required targets are disabled")
elseif(NOT EXISTS "${AMARANTIN_TEST_ROOT_FIXTURE}")
  message(STATUS
    "amarantin: skipping test.root fixture smoke; missing ${AMARANTIN_TEST_ROOT_FIXTURE}")
else()
  add_executable(testroot_pipeline_check testroot_pipeline_check.cc)

  target_link_libraries(testroot_pipeline_check
    PRIVATE
      Ana
      IO
      Plot
      amarantin_build_options
  )

  set_target_properties(testroot_pipeline_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_test(NAME testroot_pipeline_smoke
    COMMAND "${CMAKE_COMMAND}" -E env
      "AMARANTIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
      bash "${CMAKE_SOURCE_DIR}/tools/test-root-smoke.sh"
      "${CMAKE_BINARY_DIR}"
      "${AMARANTIN_TEST_ROOT_FIXTURE}"
  )

  set_tests_properties(testroot_pipeline_smoke PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )

  if(TARGET check)
    add_dependencies(check
      mk_sample
      mk_dataset
      mk_eventlist
      mk_dist
      mk_fit
      mk_cov
      IO
      Ana
      Plot
      Syst
      Fit
      testroot_pipeline_check
    )
  endif()
endif()

if(TARGET Fit)
  add_executable(fit_rigorous_check fit_rigorous_check.cc)

  target_link_libraries(fit_rigorous_check
    PRIVATE
      Fit
      amarantin_build_options
  )

  set_target_properties(fit_rigorous_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_test(NAME fit_rigorous_check
    COMMAND "${CMAKE_BINARY_DIR}/bin/fit_rigorous_check"
  )

  if(TARGET check)
    add_dependencies(check
      Fit
      fit_rigorous_check
    )
  endif()
endif()

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

if(TARGET IO AND TARGET Ana AND TARGET Plot AND TARGET Syst AND TARGET Fit AND TARGET mk_cov)
  add_test(NAME macro_analysis_smoke
    COMMAND "${CMAKE_COMMAND}" -E env
      "AMARANTIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
      bash "${CMAKE_SOURCE_DIR}/tools/macro-analysis-smoke.sh"
  )

  set_tests_properties(macro_analysis_smoke PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )

  if(TARGET check)
    add_dependencies(check
      IO
      Ana
      Plot
      Syst
      Fit
      mk_cov
    )
  endif()
endif()
