set(AMARANTIN_TEST_ROOT_FIXTURE "${CMAKE_SOURCE_DIR}/test.root" CACHE FILEPATH
  "Optional path to the test.root fixture used by the end-to-end smoke")

add_test(NAME run_macro_wrapper_check
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/run_macro_wrapper_check.sh"
)

set_tests_properties(run_macro_wrapper_check PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(NAME render_sample_catalog_wrapper_check
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/render_sample_catalog_wrapper_check.sh"
)

set_tests_properties(render_sample_catalog_wrapper_check PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(NAME overnight_minimality_pass_wrapper_check
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/overnight_minimality_pass_wrapper_check.sh"
)

set_tests_properties(overnight_minimality_pass_wrapper_check PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(NAME mklist_argument_check
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/mklist_argument_check.sh"
)

set_tests_properties(mklist_argument_check PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

if(TARGET Ana)
  add_executable(signal_definition_contract_check signal_definition_contract_check.cc)

  target_link_libraries(signal_definition_contract_check
    PRIVATE
      Ana
      IO
      amarantin_build_options
  )

  if(nlohmann_json_FOUND)
    target_link_libraries(signal_definition_contract_check PRIVATE nlohmann_json::nlohmann_json)
  elseif(NLOHMANN_JSON_INC)
    target_include_directories(signal_definition_contract_check PRIVATE "${NLOHMANN_JSON_INC}")
  endif()

  set_target_properties(signal_definition_contract_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_test(NAME signal_definition_contract_check
    COMMAND "${CMAKE_BINARY_DIR}/bin/signal_definition_contract_check"
      "${CMAKE_SOURCE_DIR}/ana/ccnumu_hyperon_measurement_contract.json"
  )

  if(TARGET check)
    add_dependencies(check signal_definition_contract_check)
  endif()
endif()

if(NOT TARGET mk_sample OR
   NOT TARGET mk_dataset OR
   NOT TARGET mk_eventlist OR
   NOT TARGET mk_dist OR
   NOT TARGET mk_cov OR
   NOT TARGET IO OR
   NOT TARGET Ana OR
   NOT TARGET Plot OR
   NOT TARGET Syst)
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
      mk_cov
      IO
      Ana
      Plot
      Syst
      testroot_pipeline_check
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

if(TARGET IO AND TARGET Ana)
  add_executable(pipeline_normalization_check pipeline_normalization_check.cc)

  target_link_libraries(pipeline_normalization_check
    PRIVATE
      IO
      Ana
      SQLite::SQLite3
      amarantin_build_options
  )

  set_target_properties(pipeline_normalization_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )

  add_test(NAME pipeline_normalization_check
    COMMAND "${CMAKE_BINARY_DIR}/bin/pipeline_normalization_check"
  )

  if(TARGET check)
    add_dependencies(check
      IO
      Ana
      pipeline_normalization_check
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

if(TARGET IO AND TARGET Ana AND TARGET Plot AND TARGET Syst AND TARGET mk_cov)
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
      mk_cov
    )
  endif()
endif()
