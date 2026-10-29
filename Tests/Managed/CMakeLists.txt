file(GLOB_RECURSE testSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/*.csproj")
list(FILTER testSources EXCLUDE REGEX "/(bin|obj)/")
file(GLOB_RECURSE frameworkSources CONFIGURE_DEPENDS "${URSA_SOURCE_ROOT}/Managed/*.cs" "${URSA_SOURCE_ROOT}/Managed/*.csproj")
list(FILTER frameworkSources EXCLUDE REGEX "/(bin|obj)/")
list(APPEND frameworkSources "${URSA_SOURCE_ROOT}/Directory.Build.props"
  "${URSA_SOURCE_ROOT}/Managed/Directory.Build.targets")
set(testOutputs)
file(GLOB_RECURSE exampleSources CONFIGURE_DEPENDS "${URSA_SOURCE_ROOT}/Docs/Examples/*.cs")
set(testSourceList "${URSA_BINARY_ROOT}/Tests/Managed/SourceInputs.txt")
string(JOIN "\n" testSourceListContent ${testSources} ${frameworkSources} ${exampleSources})
file(CONFIGURE OUTPUT "${testSourceList}" CONTENT "${testSourceListContent}\n" @ONLY)
set(testCommands)
foreach(project IN ITEMS Ursa.ManagedHost.ProtocolTests Ursa.SessionFixtures Ursa.EditorFixtures)
  list(APPEND testOutputs "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/${project}.dll"
    "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/${project}.pdb")
  list(APPEND testCommands
    COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/${project}/${project}.csproj"
    --artifacts-path "${URSA_MANAGED_TEST_ARTIFACTS_ROOT}" --output "${URSA_MANAGED_TEST_OUTPUT_DIR}"
    "-p:UrsaBindingsGenerator=${URSA_BINDINGS_GENERATOR}"
    "-p:UrsaBindingsGeneratedDir=${URSA_BINARY_ROOT}/Bindings/$<CONFIG>"
    --configuration "$<CONFIG>" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${URSA_MANAGED_TEST_OUTPUT_DIR}/${project}.dll"
    "${URSA_MANAGED_TEST_OUTPUT_DIR}/${project}.pdb"
    "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/")
endforeach()
list(APPEND testOutputs "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/Ursa.SessionDependency.dll"
  "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/Ursa.SessionDependency.pdb"
  "${URSA_MANAGED_TEST_OUTPUT_DIR}/Ursa.ManagedHost.ProtocolTests.dll"
  "${URSA_MANAGED_TEST_OUTPUT_DIR}/Ursa.ManagedHost.ProtocolTests.pdb"
  "${URSA_MANAGED_TEST_OUTPUT_DIR}/Ursa.ManagedHost.ProtocolTests.runtimeconfig.json")
add_custom_command(OUTPUT "${URSA_BINARY_ROOT}/Tests/Managed/$<CONFIG>/Built.stamp" ${testOutputs}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${URSA_MANAGED_OUTPUT_DIR}/TestScripts"
  ${testCommands}
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "${URSA_MANAGED_TEST_OUTPUT_DIR}/Ursa.SessionDependency.dll"
  "${URSA_MANAGED_TEST_OUTPUT_DIR}/Ursa.SessionDependency.pdb"
  "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${URSA_BINARY_ROOT}/Tests/Managed/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_BINARY_ROOT}/Tests/Managed/$<CONFIG>/Built.stamp"
  DEPENDS UrsaManagedHostAssemblies ${testSources} ${frameworkSources} ${exampleSources} "${testSourceList}"
  VERBATIM)
add_custom_target(UrsaManagedTestAssemblies DEPENDS "${URSA_BINARY_ROOT}/Tests/Managed/$<CONFIG>/Built.stamp" ${testOutputs}
  SOURCES ${testSources})
ursa_source_groups(UrsaManagedTestAssemblies "Tests/Managed")
