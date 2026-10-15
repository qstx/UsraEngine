file(GLOB_RECURSE testSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/*.csproj")
list(FILTER testSources EXCLUDE REGEX "/(bin|obj)/")
file(GLOB_RECURSE frameworkSources CONFIGURE_DEPENDS "${URSA_SOURCE_ROOT}/Managed/*.cs" "${URSA_SOURCE_ROOT}/Managed/*.csproj")
list(FILTER frameworkSources EXCLUDE REGEX "/(bin|obj)/")
set(testOutputs)
file(GLOB exampleSources CONFIGURE_DEPENDS "${URSA_SOURCE_ROOT}/Docs/Examples/*.cs")
set(testCommands)
foreach(project IN ITEMS Ursa.ManagedHost.ProtocolTests Ursa.SessionFixtures Ursa.EditorFixtures)
  list(APPEND testOutputs "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/${project}.dll")
  list(APPEND testCommands
    COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${URSA_MANAGED_TEST_STAGE_ROOT}/Tests/Managed/${project}/${project}.csproj"
    --configuration "$<CONFIG>" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${URSA_MANAGED_TEST_STAGE_ROOT}/Tests/Managed/${project}/bin/$<CONFIG>/net10.0/${project}.dll"
    "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/${project}.dll")
endforeach()
list(APPEND testOutputs "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/Ursa.SessionDependency.dll")
add_custom_command(OUTPUT "${URSA_BINARY_ROOT}/Tests/Managed/$<CONFIG>/Built.stamp" ${testOutputs}
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${URSA_SOURCE_ROOT}/Managed"
  "-DOUTPUT_ROOT=${URSA_MANAGED_TEST_STAGE_ROOT}/Managed" -P "${URSA_SOURCE_ROOT}/Tools/StageManaged.cmake"
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
  "-DOUTPUT_ROOT=${URSA_MANAGED_TEST_STAGE_ROOT}/Tests/Managed" -P "${URSA_SOURCE_ROOT}/Tools/StageManaged.cmake"
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${URSA_SOURCE_ROOT}/Docs/Examples"
  "-DOUTPUT_ROOT=${URSA_MANAGED_TEST_STAGE_ROOT}/Docs/Examples" -P "${URSA_SOURCE_ROOT}/Tools/StageManaged.cmake"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${URSA_MANAGED_OUTPUT_DIR}/TestScripts"
  ${testCommands}
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "${URSA_MANAGED_TEST_STAGE_ROOT}/Tests/Managed/Ursa.SessionFixtures/bin/$<CONFIG>/net10.0/Ursa.SessionDependency.dll"
  "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/Ursa.SessionDependency.dll"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${URSA_BINARY_ROOT}/Tests/Managed/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_BINARY_ROOT}/Tests/Managed/$<CONFIG>/Built.stamp"
  DEPENDS UrsaManagedHostAssemblies ${testSources} ${frameworkSources} ${exampleSources} "${URSA_SOURCE_ROOT}/Tools/StageManaged.cmake"
  VERBATIM)
add_custom_target(UrsaManagedTestAssemblies DEPENDS "${URSA_BINARY_ROOT}/Tests/Managed/$<CONFIG>/Built.stamp" ${testOutputs}
  SOURCES ${testSources})
ursa_source_groups(UrsaManagedTestAssemblies "Tests/Managed")
