# Native .NET hosting is optional and deliberately isolated from the general
# runtime target until the host implementation is enabled by CMake.
set(URSA_MANAGED_WARNING_FLAGS)
if(URSA_WARNINGS_AS_ERRORS)
  list(APPEND URSA_MANAGED_WARNING_FLAGS -warnaserror)
endif()

add_library(UrsaDotNetHostSupport INTERFACE)
add_library(Ursa::DotNetHostSupport ALIAS UrsaDotNetHostSupport)
target_include_directories(UrsaDotNetHostSupport INTERFACE "${URSA_DOTNET_HOST_PACK}")
target_link_libraries(UrsaDotNetHostSupport INTERFACE "${URSA_DOTNET_HOST_PACK}/nethost.lib")
target_compile_definitions(UrsaDotNetHostSupport INTERFACE URSA_ENABLE_DOTNET=1)
set_target_properties(UrsaDotNetHostSupport PROPERTIES FOLDER "Runtime/Scripting"
  URSA_NETHOST_DLL "${URSA_DOTNET_HOST_PACK}/nethost.dll")

add_library(UrsaDotNetHost STATIC DotNetHost.cpp ManagedScriptRuntime.cpp)
add_library(Ursa::DotNetHost ALIAS UrsaDotNetHost)
target_compile_features(UrsaDotNetHost PUBLIC cxx_std_17)
target_include_directories(UrsaDotNetHost PUBLIC "${PROJECT_SOURCE_DIR}/Runtime")
target_link_libraries(UrsaDotNetHost PUBLIC Ursa::Runtime UrsaDotNetHostSupport)
set_target_properties(UrsaDotNetHost PROPERTIES FOLDER "Runtime/Scripting")

file(GLOB_RECURSE URSA_MANAGED_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/Managed/*.cs" "${PROJECT_SOURCE_DIR}/Managed/*.csproj")
list(FILTER URSA_MANAGED_SOURCES EXCLUDE REGEX "/(bin|obj)/")

set(URSA_MANAGED_REQUIRED_OUTPUTS
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.ManagedHost.dll"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.ManagedHost.deps.json"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.ManagedHost.runtimeconfig.json"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll"
  "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Editor.dll")
add_custom_command(OUTPUT "${URSA_MANAGED_OUTPUT_DIR}/Built.stamp" ${URSA_MANAGED_REQUIRED_OUTPUTS}
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}/Managed"
    "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Managed/Source" -P "${PROJECT_SOURCE_DIR}/Tools/StageManaged.cmake"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.ManagedHost/Ursa.ManagedHost.csproj"
    --configuration "$<CONFIG>" --output "${URSA_MANAGED_OUTPUT_DIR}" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.Editor/Ursa.Editor.csproj"
    --configuration "$<CONFIG>" --output "${URSA_MANAGED_OUTPUT_DIR}" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.ManagedHost.ProtocolTests/Ursa.ManagedHost.ProtocolTests.csproj"
    --configuration "$<CONFIG>" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${URSA_MANAGED_OUTPUT_DIR}/TestScripts"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.ManagedHost.ProtocolTests/bin/$<CONFIG>/net10.0/Ursa.ManagedHost.ProtocolTests.dll"
    "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/Ursa.ManagedHost.ProtocolTests.dll"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.SessionFixtures/Ursa.SessionFixtures.csproj"
    --configuration "$<CONFIG>" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.SessionFixtures/bin/$<CONFIG>/net10.0/Ursa.SessionFixtures.dll"
    "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/Ursa.SessionFixtures.dll"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.SessionFixtures/bin/$<CONFIG>/net10.0/Ursa.SessionDependency.dll"
    "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/Ursa.SessionDependency.dll"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.EditorFixtures/Ursa.EditorFixtures.csproj"
    --configuration "$<CONFIG>" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.EditorFixtures/bin/$<CONFIG>/net10.0/Ursa.EditorFixtures.dll"
    "${URSA_MANAGED_OUTPUT_DIR}/TestScripts/Ursa.EditorFixtures.dll"
  COMMAND "${CMAKE_COMMAND}" -E touch "${URSA_MANAGED_OUTPUT_DIR}/Built.stamp"
  DEPENDS ${URSA_MANAGED_SOURCES} "${PROJECT_SOURCE_DIR}/Tools/StageManaged.cmake"
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  VERBATIM)
add_custom_target(UrsaManagedHostAssemblies DEPENDS "${URSA_MANAGED_OUTPUT_DIR}/Built.stamp" ${URSA_MANAGED_REQUIRED_OUTPUTS})
add_dependencies(UrsaDotNetHost UrsaManagedHostAssemblies)
set_target_properties(UrsaManagedHostAssemblies PROPERTIES FOLDER "Runtime/Scripting")

add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/Managed/Tools/$<CONFIG>/Built.stamp"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_BINARY_DIR}/Managed/Source/Ursa.ScriptCatalogGenerator/Ursa.ScriptCatalogGenerator.csproj"
    --configuration "$<CONFIG>" --output "${CMAKE_BINARY_DIR}/Managed/Tools" --nologo -m:1 ${URSA_MANAGED_WARNING_FLAGS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/Managed/Tools/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/Managed/Tools/$<CONFIG>/Built.stamp"
  DEPENDS UrsaManagedHostAssemblies ${URSA_MANAGED_SOURCES}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  VERBATIM)
add_custom_target(UrsaManagedScriptCatalog DEPENDS "${CMAKE_BINARY_DIR}/Managed/Tools/$<CONFIG>/Built.stamp")
set_target_properties(UrsaManagedScriptCatalog PROPERTIES FOLDER "Runtime/Scripting")
