file(GLOB_RECURSE managedSources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/*.csproj"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.props" "${CMAKE_CURRENT_SOURCE_DIR}/*.targets")
list(FILTER managedSources EXCLUDE REGEX "/(bin|obj)/")

# This tool precedes the framework build: it only references SDK Roslyn and binding support.
file(GLOB bindingGeneratorSources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Ursa.Bindings/*.cs"
  "${CMAKE_CURRENT_SOURCE_DIR}/Ursa.Bindings/*.csproj"
  "${CMAKE_CURRENT_SOURCE_DIR}/Ursa.BindingsGenerator/*.cs"
  "${CMAKE_CURRENT_SOURCE_DIR}/Ursa.BindingsGenerator/*.csproj")
list(APPEND bindingGeneratorSources "${CMAKE_CURRENT_SOURCE_DIR}/Directory.Build.targets")
set(bindingGeneratorSources "${bindingGeneratorSources}" PARENT_SCOPE)
set(URSA_BINDINGS_GENERATOR_DIR "${URSA_BINARY_ROOT}/Managed/BindingTools/$<CONFIG>")
set(URSA_BINDINGS_GENERATOR "${URSA_BINDINGS_GENERATOR_DIR}/Ursa.BindingsGenerator.dll")
set(URSA_BINDINGS_GENERATOR "${URSA_BINDINGS_GENERATOR}" PARENT_SCOPE)
set(URSA_BINDINGS_GENERATOR_STAMP "${URSA_BINDINGS_GENERATOR_DIR}/Built.stamp")
set(URSA_BINDINGS_GENERATOR_STAMP "${URSA_BINDINGS_GENERATOR_STAMP}" PARENT_SCOPE)
add_custom_command(OUTPUT "${URSA_BINDINGS_GENERATOR_STAMP}"
  BYPRODUCTS "${URSA_BINDINGS_GENERATOR}" "${URSA_BINDINGS_GENERATOR_DIR}/Ursa.Bindings.dll"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build
    "${CMAKE_CURRENT_SOURCE_DIR}/Ursa.BindingsGenerator/Ursa.BindingsGenerator.csproj"
    --configuration "$<CONFIG>" --nologo -m:1
    --artifacts-path "${URSA_BINARY_ROOT}/Managed/BindingToolArtifacts"
    --output "${URSA_BINDINGS_GENERATOR_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_BINDINGS_GENERATOR_STAMP}"
  DEPENDS ${bindingGeneratorSources}
  VERBATIM)
add_custom_target(UrsaBindingsGenerator DEPENDS "${URSA_BINDINGS_GENERATOR_STAMP}" SOURCES ${bindingGeneratorSources})
ursa_source_groups(UrsaBindingsGenerator "Managed/Tools")

set(URSA_BINDINGS_GENERATED_DIR "${URSA_BINARY_ROOT}/Bindings/$<CONFIG>")
add_custom_command(OUTPUT "${URSA_BINDINGS_GENERATED_DIR}/Generated.stamp"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" "${URSA_BINDINGS_GENERATOR}"
    --input "${CMAKE_CURRENT_SOURCE_DIR}" --output "${URSA_BINDINGS_GENERATED_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_BINDINGS_GENERATED_DIR}/Generated.stamp"
  DEPENDS UrsaBindingsGenerator "${URSA_BINDINGS_GENERATOR_STAMP}" ${managedSources}
  VERBATIM)
add_custom_target(UrsaBindingsGenerated DEPENDS "${URSA_BINDINGS_GENERATED_DIR}/Generated.stamp")
set_target_properties(UrsaBindingsGenerated PROPERTIES FOLDER "Managed/Bindings")
add_dependencies(UrsaEngine UrsaBindingsGenerated)
target_include_directories(UrsaEngine PUBLIC "${URSA_BINDINGS_GENERATED_DIR}/Native")

set(bindingsSnapshot "${URSA_SOURCE_ROOT}/Engine/Scripting/Bindings/Generated")
add_custom_target(UrsaUpdateBindingsSnapshot
  COMMAND "${URSA_DOTNET_EXECUTABLE}" "${URSA_BINDINGS_GENERATOR}"
    --input "${CMAKE_CURRENT_SOURCE_DIR}" --output "${bindingsSnapshot}" --native-only
  DEPENDS UrsaBindingsGenerator "${URSA_BINDINGS_GENERATOR_STAMP}" VERBATIM)
add_custom_target(UrsaCheckBindingsSnapshot
  COMMAND "${URSA_DOTNET_EXECUTABLE}" "${URSA_BINDINGS_GENERATOR}"
    --input "${CMAKE_CURRENT_SOURCE_DIR}" --output "${bindingsSnapshot}" --native-only --check
  DEPENDS UrsaBindingsGenerated VERBATIM)
ursa_source_groups(UrsaCheckBindingsSnapshot "Managed/Bindings")
set_target_properties(UrsaUpdateBindingsSnapshot PROPERTIES FOLDER "Managed/Bindings")

set(managedOutputs
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Bindings.dll"
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
    "-p:UrsaBindingsGenerator=${URSA_BINDINGS_GENERATOR}"
    "-p:UrsaBindingsGeneratedDir=${URSA_BINARY_ROOT}/Bindings/$<CONFIG>"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${URSA_MANAGED_STAGE_ROOT}/Ursa.Editor/Ursa.Editor.csproj"
    --configuration "$<CONFIG>" --output "${URSA_MANAGED_OUTPUT_DIR}" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
    "-p:UrsaBindingsGenerator=${URSA_BINDINGS_GENERATOR}"
    "-p:UrsaBindingsGeneratedDir=${URSA_BINARY_ROOT}/Bindings/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_MANAGED_OUTPUT_DIR}/Built.stamp"
  DEPENDS UrsaBindingsGenerated UrsaCheckBindingsSnapshot ${managedSources} "${URSA_SOURCE_ROOT}/Tools/StageManaged.cmake"
  VERBATIM)
add_custom_target(UrsaManagedHostAssemblies DEPENDS "${URSA_MANAGED_OUTPUT_DIR}/Built.stamp" ${managedOutputs}
  SOURCES ${managedSources})
ursa_source_groups(UrsaManagedHostAssemblies "Managed")

add_custom_command(OUTPUT "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>/Built.stamp"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${URSA_MANAGED_STAGE_ROOT}/Ursa.ScriptCatalogGenerator/Ursa.ScriptCatalogGenerator.csproj"
    --configuration "$<CONFIG>" --output "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>/Built.stamp"
  DEPENDS UrsaManagedHostAssemblies ${managedSources}
  VERBATIM)
add_custom_target(UrsaManagedScriptCatalog DEPENDS "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>/Built.stamp")
set_target_properties(UrsaManagedScriptCatalog PROPERTIES FOLDER "Managed/Tools")
