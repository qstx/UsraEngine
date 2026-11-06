ursa_collect_sources(sources "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(UrsaTestFixtures STATIC ${sources})
target_link_libraries(UrsaTestFixtures PUBLIC Ursa::Engine)
target_include_directories(UrsaTestFixtures PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
ursa_source_groups(UrsaTestFixtures "Tests/Fixtures")
file(MAKE_DIRECTORY "${URSA_BINARY_ROOT}/Temp/ShaderCompiler")
if(TARGET UrsaAssetImport)

  add_test(NAME UrsaTestContentPrepare COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT=${URSA_TEST_CONTENT_PROJECT}" "-DIMPORTER=$<TARGET_FILE:UrsaAssetImport>"
    "-DCONFIGURATION=$<CONFIG>" "-DGLSLC=${URSA_GLSLC_EXECUTABLE}"
    "-DSHADER_SOURCES=${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/PrepareTestContent.cmake")
  set_tests_properties(UrsaTestContentPrepare PROPERTIES FIXTURES_SETUP UrsaTestContent
    TIMEOUT 600
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/TestContentUser/$<CONFIG>;TEMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler;TMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler")
    add_test(NAME UrsaTestSamplePrepare COMMAND "${CMAKE_COMMAND}"
      "-DBUILD_ROOT=${URSA_BINARY_ROOT}" "-DPROJECT=${URSA_TEST_SAMPLE_PROJECT}"
      "-DIMPORTER=$<TARGET_FILE:UrsaAssetImport>" "-DCONFIGURATION=$<CONFIG>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/PrepareSampleProject.cmake")
    set_tests_properties(UrsaTestSamplePrepare PROPERTIES FIXTURES_SETUP UrsaTestSample
      TIMEOUT 600
      ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/TestSampleUser/$<CONFIG>;TEMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler;TMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler")
endif()
set(URSA_DEMO_SUITE_SOURCE "${URSA_SOURCE_ROOT}/../Demo")
set(URSA_TEST_DEMO_SUITE_PROJECT "${URSA_BINARY_ROOT}/TestProjects/DemoSuite/$<CONFIG>" CACHE INTERNAL "DemoSuite isolated test project")
if(WIN32 AND TARGET UrsaAssetImport AND EXISTS "${URSA_DEMO_SUITE_SOURCE}/Assets/Tests/Menu.ursascene")
  add_test(NAME UrsaDemoSuitePrepare COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/PrepareDemoSuite.py"
    --build-root "${URSA_BINARY_ROOT}" --project "${URSA_TEST_DEMO_SUITE_PROJECT}" --source "${URSA_DEMO_SUITE_SOURCE}"
    --importer "$<TARGET_FILE:UrsaAssetImport>" --configuration "$<CONFIG>")
  set_tests_properties(UrsaDemoSuitePrepare PROPERTIES FIXTURES_SETUP UrsaDemoSuite TIMEOUT 600
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/DemoSuiteImportUser/$<CONFIG>;TEMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler;TMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler")
  add_test(NAME UrsaDemoSuitePreparationTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/PrepareDemoSuiteTests.py")
endif()
if(WIN32)
  add_test(NAME UrsaSamplePreparationTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/PrepareSampleProjectTests.py" --cmake "${CMAKE_COMMAND}" --build-root "${URSA_BINARY_ROOT}")
endif()
ursa_finalize_test_directory()
