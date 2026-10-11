ursa_collect_sources(sources "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(UrsaTestFixtures STATIC ${sources})
target_link_libraries(UrsaTestFixtures PUBLIC Ursa::Engine)
target_include_directories(UrsaTestFixtures PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
ursa_source_groups(UrsaTestFixtures "Tests/Fixtures")
if(TARGET UrsaAssetImport)

  add_test(NAME UrsaTestContentPrepare COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT=${URSA_TEST_CONTENT_PROJECT}" "-DIMPORTER=$<TARGET_FILE:UrsaAssetImport>"
    "-DCONFIGURATION=$<CONFIG>" "-DGLSLC=${URSA_GLSLC_EXECUTABLE}"
    "-DSHADER_SOURCES=${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/PrepareTestContent.cmake")
  set_tests_properties(UrsaTestContentPrepare PROPERTIES FIXTURES_SETUP UrsaTestContent)
  if(NOT URSA_PROJECT_DIR STREQUAL "")

    add_test(NAME UrsaTestSamplePrepare COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE=${URSA_PROJECT_DIR}/Assets" "-DPROJECT=${URSA_TEST_SAMPLE_PROJECT}"
      "-DIMPORTER=$<TARGET_FILE:UrsaAssetImport>" "-DCONFIGURATION=$<CONFIG>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/PrepareSampleProject.cmake")
    set_tests_properties(UrsaTestSamplePrepare PROPERTIES FIXTURES_SETUP UrsaTestSample)
  endif()
endif()
ursa_finalize_test_directory()
