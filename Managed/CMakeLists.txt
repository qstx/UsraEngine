file(GLOB_RECURSE managedSources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/*.csproj")
list(FILTER managedSources EXCLUDE REGEX "/(bin|obj)/")
set(managedOutputs
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.ManagedHost.dll"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.ManagedHost.deps.json"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.ManagedHost.runtimeconfig.json"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Rendering.dll"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Editor.dll")
add_custom_command(OUTPUT "${URSA_MANAGED_OUTPUT_DIR}/Built.stamp" ${managedOutputs}
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DOUTPUT_ROOT=${URSA_MANAGED_STAGE_ROOT}" -P "${URSA_SOURCE_ROOT}/Tools/StageManaged.cmake"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${URSA_MANAGED_STAGE_ROOT}/Ursa.ManagedHost/Ursa.ManagedHost.csproj"
    --configuration "$<CONFIG>" --output "${URSA_MANAGED_OUTPUT_DIR}" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${URSA_MANAGED_STAGE_ROOT}/Ursa.Editor/Ursa.Editor.csproj"
    --configuration "$<CONFIG>" --output "${URSA_MANAGED_OUTPUT_DIR}" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_MANAGED_OUTPUT_DIR}/Built.stamp"
  DEPENDS ${managedSources} "${URSA_SOURCE_ROOT}/Tools/StageManaged.cmake"
  VERBATIM)
add_custom_target(UrsaManagedHostAssemblies DEPENDS "${URSA_MANAGED_OUTPUT_DIR}/Built.stamp" ${managedOutputs}
  SOURCES ${managedSources})
ursa_source_groups(UrsaManagedHostAssemblies "Managed")

add_custom_command(OUTPUT "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>/Built.stamp"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${URSA_MANAGED_STAGE_ROOT}/Ursa.ScriptCatalogGenerator/Ursa.ScriptCatalogGenerator.csproj"
    --configuration "$<CONFIG>" --output "${URSA_BINARY_ROOT}/Managed/Tools" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>/Built.stamp"
  DEPENDS UrsaManagedHostAssemblies ${managedSources}
  VERBATIM)
add_custom_target(UrsaManagedScriptCatalog DEPENDS "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>/Built.stamp")
set_target_properties(UrsaManagedScriptCatalog PROPERTIES FOLDER "Managed/Tools")
