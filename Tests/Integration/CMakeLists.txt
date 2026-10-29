if(URSA_ENABLE_DOTNET AND URSA_BUILD_EDITOR)
  ursa_add_test_executable(UrsaManagedAssetImportWorkflowTests ManagedAssetImportWorkflowTests.cpp)
  target_link_libraries(UrsaManagedAssetImportWorkflowTests PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaManagedAssetImportWorkflowTests PRIVATE
    URSA_WORKFLOW_ROOT="${URSA_BINARY_ROOT}/Temp/ManagedAssetWorkflow"
    URSA_WORKFLOW_EXAMPLES="${URSA_SOURCE_ROOT}/Docs/Examples"
    URSA_WORKFLOW_ENGINE_ASSETS="${URSA_SOURCE_ROOT}/Assets"
    URSA_WORKFLOW_ANIMATED_FBX="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/FBX/animation_with_skeleton.fbx"
    URSA_WORKFLOW_ANIMATED_GLTF="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/glTF2/simple_skin/simple_skin.gltf"
    URSA_WORKFLOW_CLI="$<TARGET_FILE:UrsaAssetImport>"
    URSA_WORKFLOW_EDITOR="$<TARGET_FILE:UrsaEditor>"
    URSA_WORKFLOW_CAPTURE="$<TARGET_FILE:UrsaCapture>"
    URSA_WORKFLOW_PLAYER="$<TARGET_FILE:UrsaScriptPlayer>"
    URSA_WORKFLOW_MANAGED_OUTPUT="${URSA_MANAGED_OUTPUT_DIR}"
    URSA_WORKFLOW_CONFIGURATION="$<CONFIG>")
  add_dependencies(UrsaManagedAssetImportWorkflowTests UrsaAssetImport UrsaScriptPlayer UrsaEditor UrsaCapture UrsaManagedTestAssemblies)
  ursa_attach_dotnet_host_runtime(UrsaManagedAssetImportWorkflowTests)
  add_test(NAME UrsaManagedAssetImportWorkflowTests COMMAND UrsaManagedAssetImportWorkflowTests)
  set_tests_properties(UrsaManagedAssetImportWorkflowTests PROPERTIES TIMEOUT 600
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/ManagedWorkflowUser;TEMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler;TMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler")
  add_test(NAME UrsaGeneratedAssetPlayerWorkflowTests COMMAND UrsaManagedAssetImportWorkflowTests --player)
  set_tests_properties(UrsaGeneratedAssetPlayerWorkflowTests PROPERTIES TIMEOUT 300
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/GeneratedPlayerUser")
  add_test(NAME UrsaScriptedImporterInspectorWorkflowTests COMMAND UrsaManagedAssetImportWorkflowTests --inspector)
  set_tests_properties(UrsaScriptedImporterInspectorWorkflowTests PROPERTIES TIMEOUT 300
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/ScriptedInspectorUser")
  if(NOT URSA_PROJECT_DIR STREQUAL "")
    ursa_add_test_executable(UrsaEditorSrpAcceptanceTests EditorSrpAcceptanceTests.cpp)
    target_sources(UrsaEditorSrpAcceptanceTests PRIVATE CaptureCliAcceptance.cpp)
    target_compile_definitions(UrsaEditorSrpAcceptanceTests PRIVATE URSA_CAPTURE_CLI="$<TARGET_FILE:UrsaCapture>")
    add_dependencies(UrsaEditorSrpAcceptanceTests UrsaCapture)
    target_link_libraries(UrsaEditorSrpAcceptanceTests PRIVATE Ursa::Editor glfw)
    ursa_attach_dotnet_host_runtime(UrsaEditorSrpAcceptanceTests)
    add_test(NAME UrsaEditorSrpAcceptanceTests COMMAND UrsaEditorSrpAcceptanceTests
      "${URSA_TEST_SAMPLE_PROJECT}" "${URSA_BINARY_ROOT}/TestProjects/SrpAcceptance/$<CONFIG>")
    set_tests_properties(UrsaEditorSrpAcceptanceTests PROPERTIES TIMEOUT 900
      ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/SrpAcceptanceUser;TEMP=${URSA_BINARY_ROOT}/Temp;TMP=${URSA_BINARY_ROOT}/Temp")
  endif()

  ursa_add_test_executable(UrsaProfilerEditorWindowTests ProfilerEditorWindowTests.cpp)
  target_link_libraries(UrsaProfilerEditorWindowTests PRIVATE Ursa::Editor glfw)
  ursa_attach_dotnet_host_runtime(UrsaProfilerEditorWindowTests)

  add_test(NAME UrsaProfilerEditorWindowTests COMMAND UrsaProfilerEditorWindowTests "${URSA_TEST_SAMPLE_PROJECT}")
  set_tests_properties(UrsaProfilerEditorWindowTests PROPERTIES TIMEOUT 90
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/ProfilingEditor")
endif()
if(URSA_ENABLE_DOTNET)

  ursa_add_test_executable(UrsaDotNetHostTests DotNetHostTests.cpp)
  target_sources(UrsaDotNetHostTests PRIVATE DotNetHost/Rendering.cpp DotNetHost/PipelineReload.cpp
    DotNetHost/Serialization.cpp DotNetHost/Editor.cpp DotNetHost/Import.cpp DotNetHost/TestSupport.hpp)
  target_link_libraries(UrsaDotNetHostTests PRIVATE Ursa::DotNetHost UrsaTestFixtures)
  target_compile_definitions(UrsaDotNetHostTests PRIVATE
    URSA_SHADER_STAGING_ROOT="${URSA_BINARY_ROOT}/ShaderStaging"
    URSA_TEST_DXC_EXECUTABLE="${URSA_DXC_EXECUTABLE}"
    URSA_TEST_SPIRV_VAL_EXECUTABLE="${URSA_SPIRV_VAL_EXECUTABLE}"
    URSA_TEST_SPIRV_CROSS_EXECUTABLE="${URSA_SPIRV_CROSS_EXECUTABLE}")
  if(TARGET Ursa::Editor)
    target_link_libraries(UrsaDotNetHostTests PRIVATE Ursa::Editor Ursa::StbImage)
    target_compile_definitions(UrsaDotNetHostTests PRIVATE URSA_DOTNET_TEST_EDITOR=1
      URSA_IMPORT_ANIMATED_FBX="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/FBX/animation_with_skeleton.fbx"
      URSA_IMPORT_EMBEDDED_FBX="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/FBX/embedded_ascii/box_embedded_texture_fragmented.fbx")
  endif()
  add_dependencies(UrsaDotNetHostTests UrsaManagedTestAssemblies)

  add_test(NAME UrsaDotNetHostSmoke
    COMMAND UrsaDotNetHostTests
    "${URSA_MANAGED_OUTPUT_DIR}"
    "${URSA_BINARY_ROOT}/temp/DotNetHostSmoke")
  if(URSA_BUILD_EDITOR)
    add_test(NAME UrsaManagedImportSessionTests COMMAND UrsaDotNetHostTests
      "${URSA_MANAGED_OUTPUT_DIR}" "${URSA_BINARY_ROOT}/temp/ManagedImportSession" --import-only)
    add_test(NAME UrsaManagedImporterInspectorTests COMMAND UrsaDotNetHostTests
      "${URSA_MANAGED_OUTPUT_DIR}" "${URSA_BINARY_ROOT}/temp/ManagedImporterInspector" --editor-only)
    add_test(NAME UrsaManagedBuiltinInspectorMultiObjectTests COMMAND UrsaDotNetHostTests
      "${URSA_MANAGED_OUTPUT_DIR}" "${URSA_BINARY_ROOT}/temp/ManagedBuiltinInspectorMultiObject" --builtin-inspector-only)
    ursa_require_test_content(UrsaManagedBuiltinInspectorMultiObjectTests UrsaDotNetHostTests)
	add_test(NAME UrsaManagedAssetPreviewCallbacksTests COMMAND UrsaDotNetHostTests
	  "${URSA_MANAGED_OUTPUT_DIR}" "${URSA_BINARY_ROOT}/temp/ManagedAssetPreview" --editor-only)
    add_test(NAME UrsaScriptedAssetImporterTests COMMAND UrsaDotNetHostTests
      "${URSA_MANAGED_OUTPUT_DIR}" "${URSA_BINARY_ROOT}/temp/ScriptedAssetImporter" --importer-registration-only)
    add_test(NAME UrsaAssetPostprocessorTests COMMAND UrsaDotNetHostTests
      "${URSA_MANAGED_OUTPUT_DIR}" "${URSA_BINARY_ROOT}/temp/AssetPostprocessor" --importer-registration-only)
    ursa_require_test_content(UrsaScriptedAssetImporterTests UrsaDotNetHostTests)
    ursa_require_test_content(UrsaAssetPostprocessorTests UrsaDotNetHostTests)
  endif()
  ursa_attach_dotnet_host_runtime(UrsaDotNetHostTests)
  ursa_require_test_content(UrsaDotNetHostSmoke UrsaDotNetHostTests)

  add_test(NAME UrsaManagedProtocolTests COMMAND "${URSA_DOTNET_EXECUTABLE}"
    "${URSA_MANAGED_TEST_OUTPUT_DIR}/Ursa.ManagedHost.ProtocolTests.dll")
  add_test(NAME UrsaManagedSourceSymbolsTests COMMAND "${URSA_DOTNET_EXECUTABLE}"
    "${URSA_MANAGED_TEST_OUTPUT_DIR}/Ursa.ManagedHost.ProtocolTests.dll" --debug-symbols-only)
  set_tests_properties(UrsaManagedProtocolTests UrsaManagedSourceSymbolsTests PROPERTIES ENVIRONMENT
    "URSA_FORMAL_MANAGED_DIR=${URSA_MANAGED_OUTPUT_DIR};URSA_SOURCE_ROOT=${URSA_SOURCE_ROOT}")
  if(NOT URSA_PROJECT_DIR STREQUAL "")

    add_test(NAME UrsaSnakeManagedLogicTests COMMAND "${URSA_DOTNET_EXECUTABLE}" run
      --project "${URSA_SOURCE_ROOT}/Tests/Managed/Ursa.SnakeLogicTests/Ursa.SnakeLogicTests.csproj"
      --configuration "$<CONFIG>" "-p:UrsaProjectDir=${URSA_TEST_SAMPLE_PROJECT}"
      --artifacts-path "${URSA_BINARY_ROOT}/ManagedTests/Snake")

    add_test(NAME UrsaProjectScriptCompile COMMAND UrsaAssetImport --project "${URSA_TEST_SAMPLE_PROJECT}" --configuration "$<CONFIG>")
    set_tests_properties(UrsaProjectScriptCompile PROPERTIES FIXTURES_SETUP UrsaProjectScripts)
    foreach(URSA_PROJECT_SCENE IN ITEMS Snake/Snake.ursascene Snake/SnakeSmoke.ursascene)
      if(URSA_PROJECT_SCENE MATCHES "Smoke")
        set(URSA_PROJECT_SMOKE_NAME UrsaProjectSnakeGameplaySmoke)
      else()
        set(URSA_PROJECT_SMOKE_NAME UrsaProjectSnakePlayerSmoke)
      endif()

      add_test(NAME ${URSA_PROJECT_SMOKE_NAME} COMMAND UrsaScriptPlayer
        --project "${URSA_TEST_SAMPLE_PROJECT}" --scene "${URSA_PROJECT_SCENE}" --hidden --frames 20 --stress-window)
      set_tests_properties(${URSA_PROJECT_SMOKE_NAME} PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
        ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/temp/ProjectUser")
    endforeach()

    add_test(NAME UrsaProjectMaterialPlayerSmoke COMMAND UrsaScriptPlayer
      --project "${URSA_TEST_SAMPLE_PROJECT}" --scene "MaterialShowcase.ursascene" --hidden --frames 8)
    set_tests_properties(UrsaProjectMaterialPlayerSmoke PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
      ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/temp/ProjectUser")

    add_test(NAME UrsaProjectMaterialEditorSmoke COMMAND UrsaEditor --project "${URSA_TEST_SAMPLE_PROJECT}"
      --scene "MaterialShowcase.ursascene" --hidden --frames 5)
    set_tests_properties(UrsaProjectMaterialEditorSmoke PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
      ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/temp/EditorProjectUser")

    add_test(NAME UrsaProjectEditorOpenSmoke COMMAND UrsaEditor --project "${URSA_TEST_SAMPLE_PROJECT}"
      --scene "Snake/Snake.ursascene" --hidden --frames 5)
    set_tests_properties(UrsaProjectEditorOpenSmoke PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
      ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/temp/EditorProjectUser")

    ursa_add_test_executable(UrsaProjectScenePrefabRoundTripTests ProjectScenePrefabRoundTripTests.cpp)
    target_link_libraries(UrsaProjectScenePrefabRoundTripTests PRIVATE Ursa::Engine)
    target_compile_definitions(UrsaProjectScenePrefabRoundTripTests PRIVATE
      URSA_PROJECT_ASSETS_DIR="${URSA_TEST_SAMPLE_PROJECT}/Assets")

    add_test(NAME UrsaProjectScenePrefabRoundTripTests COMMAND UrsaProjectScenePrefabRoundTripTests)
    if(TARGET Ursa::Editor)

      ursa_add_test_executable(UrsaProjectPlaySessionTests ProjectPlaySessionTests.cpp)
      target_link_libraries(UrsaProjectPlaySessionTests PRIVATE Ursa::Editor Ursa::DotNetHost)
      target_compile_definitions(UrsaProjectPlaySessionTests PRIVATE URSA_PROJECT_DIR="${URSA_TEST_SAMPLE_PROJECT}")
      add_dependencies(UrsaProjectPlaySessionTests UrsaManagedHostAssemblies)
      ursa_attach_dotnet_host_runtime(UrsaProjectPlaySessionTests)

      add_test(NAME UrsaProjectPlaySessionTests COMMAND UrsaProjectPlaySessionTests)
      set_tests_properties(UrsaProjectPlaySessionTests PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
        ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/temp/PlaySessionUser")

      ursa_add_test_executable(UrsaProjectEditorExtensionsTests ProjectEditorExtensionsTests.cpp)
      target_link_libraries(UrsaProjectEditorExtensionsTests PRIVATE Ursa::DotNetHost)
      target_compile_definitions(UrsaProjectEditorExtensionsTests PRIVATE
        URSA_PROJECT_DIR="${URSA_TEST_SAMPLE_PROJECT}"
        URSA_DOTNET_RUNTIME_ASSEMBLY_PATH="${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll")
      add_dependencies(UrsaProjectEditorExtensionsTests UrsaManagedHostAssemblies)
      ursa_attach_dotnet_host_runtime(UrsaProjectEditorExtensionsTests)

      add_test(NAME UrsaProjectEditorExtensionsTests COMMAND UrsaProjectEditorExtensionsTests)
      set_tests_properties(UrsaProjectEditorExtensionsTests PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
        ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/temp/EditorExtensionUser")
    endif()

    add_test(NAME UrsaProjectPackageScriptCompile COMMAND UrsaScriptCompile "${URSA_TEST_SAMPLE_PROJECT}" "$<CONFIG>")
    set_tests_properties(UrsaProjectPackageScriptCompile PROPERTIES FIXTURES_SETUP UrsaProjectPackageScripts)

    ursa_add_test_executable(UrsaProjectLibraryRecoveryTests ProjectLibraryRecoveryTests.cpp)
    target_link_libraries(UrsaProjectLibraryRecoveryTests PRIVATE Ursa::Editor)
    target_compile_definitions(UrsaProjectLibraryRecoveryTests PRIVATE
      URSA_PROJECT_DIR="${URSA_TEST_SAMPLE_PROJECT}"
      URSA_TEST_SCRATCH_ROOT="${URSA_BINARY_ROOT}/Temp/ProjectLibraryRecovery/$<CONFIG>")
    ursa_attach_dotnet_host_runtime(UrsaProjectLibraryRecoveryTests)

    add_test(NAME UrsaProjectLibraryRecoveryTests COMMAND UrsaProjectLibraryRecoveryTests)

    add_test(NAME UrsaProjectImportPrepare COMMAND UrsaAssetImport --project "${URSA_TEST_SAMPLE_PROJECT}" --configuration "$<CONFIG>")
    set_tests_properties(UrsaProjectImportPrepare PROPERTIES FIXTURES_SETUP UrsaProjectImports)

    add_test(NAME UrsaProjectPackage COMMAND UrsaPak
      --project-root "${URSA_TEST_SAMPLE_PROJECT}"
      --configuration "$<CONFIG>" --output "${URSA_BINARY_ROOT}/Package/$<CONFIG>/ProjectContent.pak")
    set_tests_properties(UrsaProjectPackage PROPERTIES FIXTURES_SETUP UrsaProjectPak
      FIXTURES_REQUIRED "UrsaProjectScripts;UrsaProjectPackageScripts;UrsaProjectImports")

    add_test(NAME UrsaProjectSnakePakSmoke COMMAND UrsaScriptPlayer
      --pak "${URSA_BINARY_ROOT}/Package/$<CONFIG>/ProjectContent.pak" --scene "Snake/Snake.ursascene"
      --hidden --frames 20 --stress-window)
    set_tests_properties(UrsaProjectSnakePakSmoke PROPERTIES FIXTURES_REQUIRED UrsaProjectPak
      ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/temp/ProjectUser")
    if(TARGET Ursa::Editor)

      ursa_add_test_executable(UrsaProjectTextureImportTests ProjectTextureImportTests.cpp)
      target_link_libraries(UrsaProjectTextureImportTests PRIVATE Ursa::Editor)
      target_compile_definitions(UrsaProjectTextureImportTests PRIVATE
        URSA_DEMO_TEXTURE_PATH="${URSA_TEST_SAMPLE_PROJECT}/Assets/Textures/UrsaEngine.png")

      add_test(NAME UrsaProjectTextureImportTests COMMAND UrsaProjectTextureImportTests)

      ursa_add_test_executable(UrsaProjectModelImportTests ProjectModelImportTests.cpp)
      target_link_libraries(UrsaProjectModelImportTests PRIVATE Ursa::Editor)
      target_compile_definitions(UrsaProjectModelImportTests PRIVATE
        URSA_DEMO_MODEL_PATH="${URSA_TEST_SAMPLE_PROJECT}/Assets/Models/Cube.fbx"
        URSA_DEMO_TEXTURE_PATH="${URSA_TEST_SAMPLE_PROJECT}/Assets/Textures/UrsaEngine.png"
        URSA_ASSIMP_EMBEDDED_FBX_PATH="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/FBX/embedded_ascii/box_embedded_texture_fragmented.fbx"
        URSA_ASSIMP_ANIMATED_FBX_PATH="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/FBX/animation_with_skeleton.fbx"
        URSA_ASSIMP_GLTF_EXTERNAL_DIR="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/glTF2/BoxTextured-glTF"
        URSA_ASSIMP_GLTF_ANIMATED="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/glTF2/simple_skin/simple_skin.gltf"
        URSA_ASSET_IMPORT_EXECUTABLE="$<TARGET_FILE:UrsaAssetImport>"
        URSA_SCRIPT_PLAYER_EXECUTABLE="$<TARGET_FILE:UrsaScriptPlayer>"
        URSA_PAK_EXECUTABLE="$<TARGET_FILE:UrsaPak>"
        URSA_DEMO_ENGINE_ASSET_PATH="${URSA_SOURCE_ROOT}/Assets"
        URSA_TEST_CONFIGURATION="$<CONFIG>")
      add_dependencies(UrsaProjectModelImportTests UrsaAssetImport UrsaScriptPlayer UrsaPak)

      add_test(NAME UrsaProjectModelImportTests COMMAND UrsaProjectModelImportTests)
      set_property(TEST UrsaProjectModelImportTests APPEND PROPERTY ENVIRONMENT
        "TEMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler;TMP=${URSA_BINARY_ROOT}/Temp/ShaderCompiler;URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/GltfImportPlayerUser")
    endif()
  endif()
endif()
if(TARGET UrsaAssetImport AND TARGET UrsaScriptPlayer AND NOT URSA_PROJECT_DIR STREQUAL "")
  find_program(URSA_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)

  if(URSA_ENABLE_DOTNET AND TARGET UrsaEditor)
    add_test(NAME UrsaProjectPipelineSamplesTests COMMAND "${URSA_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/ProjectPipelineSamplesTests.ps1"
      -Importer "$<TARGET_FILE:UrsaAssetImport>" -Player "$<TARGET_FILE:UrsaScriptPlayer>"
      -PackageTool "$<TARGET_FILE:UrsaPak>"
      -Capture "$<TARGET_FILE:UrsaCapture>"
      -Editor "$<TARGET_FILE:UrsaEditor>" -Source "${URSA_TEST_SAMPLE_PROJECT}"
      -Configuration "$<CONFIG>" -OutputRoot "${URSA_BINARY_ROOT}/TestProjects/PipelineSamples/$<CONFIG>")
    set_tests_properties(UrsaProjectPipelineSamplesTests PROPERTIES TIMEOUT 600)
  endif()

  add_test(NAME UrsaAssetImportCliTests COMMAND "${URSA_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/AssetImportCliTests.ps1"
    -Importer "$<TARGET_FILE:UrsaAssetImport>" -Player "$<TARGET_FILE:UrsaScriptPlayer>"
    -PackageTool "$<TARGET_FILE:UrsaPak>" -Configuration "$<CONFIG>"
    -Project "${URSA_BINARY_ROOT}/TestProjects/Cli/$<CONFIG>" -Source "${URSA_TEST_SAMPLE_PROJECT}")
  set_tests_properties(UrsaAssetImportCliTests PROPERTIES
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/AssetImportCliUser/$<CONFIG>")
endif()

get_property(integrationTests DIRECTORY PROPERTY TESTS)
foreach(test IN LISTS integrationTests)
  if(NOT test STREQUAL "UrsaDotNetHostSmoke" AND NOT test STREQUAL "UrsaManagedProtocolTests"
      AND NOT test STREQUAL "UrsaManagedSourceSymbolsTests"
      AND NOT test STREQUAL "UrsaManagedImportSessionTests" AND NOT test STREQUAL "UrsaScriptedAssetImporterTests"
      AND NOT test STREQUAL "UrsaAssetPostprocessorTests" AND NOT test STREQUAL "UrsaManagedAssetImportWorkflowTests"
      AND NOT test STREQUAL "UrsaManagedImporterInspectorTests"
	  AND NOT test STREQUAL "UrsaManagedAssetPreviewCallbacksTests"
      AND NOT test STREQUAL "UrsaScriptedImporterInspectorWorkflowTests"
      AND NOT test STREQUAL "UrsaGeneratedAssetPlayerWorkflowTests")
    ursa_require_sample_project(${test} "")
  endif()
endforeach()
foreach(target IN ITEMS UrsaProfilerEditorWindowTests UrsaProjectPlaySessionTests UrsaProjectEditorExtensionsTests
    UrsaProjectModelImportTests UrsaProjectTextureImportTests UrsaProjectLibraryRecoveryTests UrsaProjectScenePrefabRoundTripTests)
  if(TARGET ${target})
    add_dependencies(${target} UrsaAssetImport)
  endif()
endforeach()
if(TARGET UrsaProjectPlaySessionTests)
  add_dependencies(UrsaProjectPlaySessionTests UrsaManagedTestAssemblies)
endif()
foreach(test IN ITEMS UrsaProfilerEditorWindowTests UrsaProjectPlaySessionTests UrsaProjectModelImportTests UrsaProjectEditorExtensionsTests)
  if(TEST ${test})
    ursa_require_test_content(${test} ${test})
  endif()
endforeach()

ursa_finalize_test_directory()
