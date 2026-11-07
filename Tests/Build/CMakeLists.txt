add_test(NAME UrsaBuildResourcePathsTests COMMAND "${CMAKE_COMMAND}"
  "-DENGINE_ROOT=${URSA_SOURCE_ROOT}" "-DSCRATCH=${URSA_BINARY_ROOT}/Temp/ResourcePaths"
  -P "${URSA_SOURCE_ROOT}/Tests/Build/BuildResourcePathsTests.cmake")
add_test(NAME UrsaDevelopmentPathsTests COMMAND "${CMAKE_COMMAND}"
  "-DENGINE_ROOT=${URSA_SOURCE_ROOT}" "-DSCRATCH=${URSA_BINARY_ROOT}/Temp/DevelopmentPaths/$<CONFIG>"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/DevelopmentPathsTests.cmake")

add_test(NAME UrsaShaderPackageBuild
  COMMAND ${CMAKE_COMMAND}
  "-DURSA_PAK_EXECUTABLE=$<TARGET_FILE:UrsaPak>"
  "-DURSA_ENGINE_ASSET_SOURCE=${URSA_SOURCE_ROOT}/Assets"
  "-DURSA_TEST_PROJECT=${URSA_BINARY_ROOT}/TestProjects/Runtime/$<CONFIG>"
  "-DURSA_CONFIGURATION=$<CONFIG>"
  "-DURSA_SHADER_PAK_DIRECTORY=${URSA_BINARY_ROOT}/Package/$<CONFIG>"
  "-DURSA_SHADER_PAK_PATH=${URSA_SHADER_TEST_PAK}"
  -P "${URSA_SOURCE_ROOT}/Tools/Pak/BuildShaderTestPak.cmake")
set_tests_properties(UrsaShaderPackageBuild PROPERTIES FIXTURES_SETUP UrsaShaderContentPak)
ursa_require_test_content(UrsaShaderPackageBuild UrsaShaderPackageTests)

if(WIN32 AND CMAKE_GENERATOR MATCHES "^Visual Studio")
  add_test(NAME UrsaDotNetConfigurationTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/DotNetConfigurationTests.py" --cmake "${CMAKE_COMMAND}")
  add_test(NAME UrsaProfilingConfigurationsTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/ProfilingConfigurationsTests.py" --build-root "${URSA_BINARY_ROOT}"
    --dotnet "${URSA_DOTNET_EXECUTABLE}")
  add_test(NAME UrsaBuildOutputEncodingTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/BuildOutputEncodingTests.py" --cmake "${CMAKE_COMMAND}"
    --scratch "${URSA_BINARY_ROOT}/Temp/BuildOutputEncoding/$<CONFIG>")
  set_tests_properties(UrsaBuildOutputEncodingTests PROPERTIES TIMEOUT 120)
  add_test(NAME UrsaPythonWorkspaceToolsTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/PythonWorkspaceToolsTests.py")
  add_test(NAME UrsaPythonAutomationTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/PythonAutomationTests.py")

  add_test(NAME UrsaAcceptanceProcessTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${URSA_SOURCE_ROOT}/Tests/Editor/Scripting/AcceptanceProcessTests.py")
  add_test(NAME UrsaManagedWorkspaceGenerationTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/ManagedWorkspaceGenerationTests.py"
    --engine-root "${URSA_SOURCE_ROOT}" --scratch "${URSA_BINARY_ROOT}/Temp/ManagedWorkspaceGeneration/$<CONFIG>")
  if(TARGET UrsaScriptCompile AND NOT URSA_PROJECT_DIR STREQUAL "")
    add_test(NAME UrsaUnifiedIdeFixtureTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
      "${CMAKE_CURRENT_SOURCE_DIR}/UnifiedIdeFixtureTests.py"
      --build-directory "${URSA_BINARY_ROOT}" --compiler "$<TARGET_FILE:UrsaScriptCompile>")
    add_test(NAME UrsaUnifiedWorkspaceRefreshTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
      "${CMAKE_CURRENT_SOURCE_DIR}/UnifiedWorkspaceRefreshTests.py"
      --build-directory "${URSA_BINARY_ROOT}" --compiler "$<TARGET_FILE:UrsaScriptCompile>")
  endif()
  add_test(NAME UrsaProjectStructureTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat"
    "${URSA_SOURCE_ROOT}/Tools/CheckProjectStructure.py" --build-dir "${URSA_BINARY_ROOT}")
  add_test(NAME UrsaProjectDiscoveryTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat" "${CMAKE_CURRENT_SOURCE_DIR}/ProjectDiscoveryTests.py"
    --scratch "${URSA_BINARY_ROOT}/Temp/ProjectDiscovery/$<CONFIG>" --engine-root "${URSA_SOURCE_ROOT}"
    --cmake "${CMAKE_COMMAND}" --generator "${CMAKE_GENERATOR}")
  add_test(NAME UrsaProjectStructureNegativeTests COMMAND "${URSA_SOURCE_ROOT}/Tools/RunPython.bat" "${CMAKE_CURRENT_SOURCE_DIR}/ProjectStructureNegativeTests.py"
    --build-dir "${URSA_BINARY_ROOT}" --engine-root "${URSA_SOURCE_ROOT}"
    --scratch "${URSA_BINARY_ROOT}/Temp/ProjectStructureNegative/$<CONFIG>")
endif()
ursa_finalize_test_directory()
