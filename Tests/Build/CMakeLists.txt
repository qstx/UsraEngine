add_test(NAME UrsaBuildResourcePathsTests COMMAND "${CMAKE_COMMAND}"
  "-DENGINE_ROOT=${URSA_SOURCE_ROOT}" "-DSCRATCH=${URSA_BINARY_ROOT}/Temp/ResourcePaths"
  -P "${URSA_SOURCE_ROOT}/Tests/Build/BuildResourcePathsTests.cmake")

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
  find_program(URSA_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
  add_test(NAME UrsaProjectStructureTests COMMAND "${URSA_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
    -File "${URSA_SOURCE_ROOT}/Tools/CheckProjectStructure.ps1" -BuildDir "${URSA_BINARY_ROOT}")
  add_test(NAME UrsaProjectDiscoveryTests COMMAND "${URSA_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/ProjectDiscoveryTests.ps1"
    -Scratch "${URSA_BINARY_ROOT}/Temp/ProjectDiscovery/$<CONFIG>" -EngineRoot "${URSA_SOURCE_ROOT}")
  add_test(NAME UrsaProjectStructureNegativeTests COMMAND "${URSA_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/ProjectStructureNegativeTests.ps1"
    -BuildDir "${URSA_BINARY_ROOT}" -EngineRoot "${URSA_SOURCE_ROOT}"
    -Scratch "${URSA_BINARY_ROOT}/Temp/ProjectStructureNegative/$<CONFIG>")
endif()
ursa_finalize_test_directory()
