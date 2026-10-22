if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaNativeAssetInspectorTests NativeAssetInspectorTests.cpp)
  target_link_libraries(UrsaNativeAssetInspectorTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaNativeAssetInspectorTests COMMAND UrsaNativeAssetInspectorTests)

  ursa_add_test_executable(UrsaMaterialDraftPropertyTests MaterialDraftPropertyTests.cpp)
  target_link_libraries(UrsaMaterialDraftPropertyTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaMaterialDraftPropertyTests COMMAND UrsaMaterialDraftPropertyTests)

  ursa_add_test_executable(UrsaScriptableAssetPropertyTests ScriptableAssetPropertyTests.cpp)
  target_link_libraries(UrsaScriptableAssetPropertyTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaScriptableAssetPropertyTests COMMAND UrsaScriptableAssetPropertyTests)

  ursa_add_test_executable(UrsaAssetImporterInspectorTests AssetImporterInspectorTests.cpp)
  target_link_libraries(UrsaAssetImporterInspectorTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaAssetImporterInspectorTests COMMAND UrsaAssetImporterInspectorTests)

  ursa_add_test_executable(UrsaImportInvocationTests ImportInvocationTests.cpp)
  target_link_libraries(UrsaImportInvocationTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaImportInvocationTests COMMAND UrsaImportInvocationTests)

  ursa_add_test_executable(UrsaAssetImportRulesTests AssetImportRulesTests.cpp)
  target_link_libraries(UrsaAssetImportRulesTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaAssetImportRulesTests COMMAND UrsaAssetImportRulesTests)

  ursa_add_test_executable(UrsaImportSettingsDraftTests ImportSettingsDraftTests.cpp)
  target_link_libraries(UrsaImportSettingsDraftTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaImportSettingsDraftTests COMMAND UrsaImportSettingsDraftTests)

  ursa_add_test_executable(UrsaAssetImportCandidateTests AssetImportCandidateTests.cpp)
  target_link_libraries(UrsaAssetImportCandidateTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaAssetImportCandidateTests COMMAND UrsaAssetImportCandidateTests)

  ursa_add_test_executable(UrsaModelImportCandidateTests ModelImportCandidateTests.cpp)
  target_link_libraries(UrsaModelImportCandidateTests PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaModelImportCandidateTests PRIVATE
    URSA_CANDIDATE_ANIMATED_FBX="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/FBX/animation_with_skeleton.fbx"
    URSA_CANDIDATE_EMBEDDED_FBX="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/FBX/embedded_ascii/box_embedded_texture_fragmented.fbx")
  add_test(NAME UrsaModelImportCandidateTests COMMAND UrsaModelImportCandidateTests)

  ursa_add_test_executable(UrsaAssetPreviewPanelTests AssetPreviewPanelTests.cpp)
  target_link_libraries(UrsaAssetPreviewPanelTests PRIVATE Ursa::Editor Ursa::StbImage)
  target_compile_definitions(UrsaAssetPreviewPanelTests PRIVATE
    URSA_PREVIEW_ANIMATED_FBX="${URSA_SOURCE_ROOT}/ThirdParty/assimp/test/models/FBX/animation_with_skeleton.fbx")
  add_test(NAME UrsaAssetPreviewPanelTests COMMAND UrsaAssetPreviewPanelTests
    "${URSA_BINARY_ROOT}/Temp/AssetPreviewEvidence/$<CONFIG>")
  ursa_require_test_content(UrsaAssetPreviewPanelTests UrsaAssetPreviewPanelTests)
	if(URSA_ENABLE_DOTNET)
	  target_compile_definitions(UrsaAssetPreviewPanelTests PRIVATE URSA_PREVIEW_MANAGED_OUTPUT="${URSA_MANAGED_OUTPUT_DIR}")
	  add_dependencies(UrsaAssetPreviewPanelTests UrsaManagedTestAssemblies)
	  ursa_attach_dotnet_host_runtime(UrsaAssetPreviewPanelTests)
	  add_test(NAME UrsaManagedAssetPreviewTests COMMAND UrsaAssetPreviewPanelTests
	    "${URSA_BINARY_ROOT}/Temp/ManagedAssetPreviewEvidence/$<CONFIG>" --managed-only)
	  ursa_require_test_content(UrsaManagedAssetPreviewTests UrsaAssetPreviewPanelTests)
	endif()

  ursa_add_test_executable(UrsaAssetPreviewStateTests AssetPreviewStateTests.cpp)
  target_link_libraries(UrsaAssetPreviewStateTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaAssetPreviewStateTests COMMAND UrsaAssetPreviewStateTests)

  ursa_add_test_executable(UrsaScriptableAssetSourceTests ScriptableAssetSourceTests.cpp)
  target_link_libraries(UrsaScriptableAssetSourceTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaScriptableAssetSourceTests COMMAND UrsaScriptableAssetSourceTests)

  ursa_add_test_executable(UrsaScriptableAssetInspectorTests ScriptableAssetInspectorTests.cpp)
  target_link_libraries(UrsaScriptableAssetInspectorTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaScriptableAssetInspectorTests COMMAND UrsaScriptableAssetInspectorTests
    "${URSA_BINARY_ROOT}/Temp/ScriptableAssetInspector.png")
  ursa_require_test_content(UrsaScriptableAssetInspectorTests UrsaScriptableAssetInspectorTests)

  ursa_add_test_executable(UrsaMeshMigrationTests MeshMigrationTests.cpp)
  target_link_libraries(UrsaMeshMigrationTests PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaMeshMigrationTests PRIVATE URSA_TEST_BUILTIN_SOURCE="${URSA_SOURCE_ROOT}/Assets")
  add_test(NAME UrsaMeshMigrationTests COMMAND UrsaMeshMigrationTests)

  ursa_add_test_executable(UrsaMeshAssetEditorTests MeshAssetEditorTests.cpp)
  target_link_libraries(UrsaMeshAssetEditorTests PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaMeshAssetEditorTests PRIVATE URSA_TEST_BUILTIN_SOURCE="${URSA_SOURCE_ROOT}/Assets")
  add_test(NAME UrsaMeshAssetEditorTests COMMAND UrsaMeshAssetEditorTests)

  ursa_add_test_executable(UrsaAssetDatabaseTests AssetDatabaseTests.cpp)

  ursa_add_test_executable(UrsaProjectAssetDatabaseTests ProjectAssetDatabaseTests.cpp)

  ursa_add_test_executable(UrsaAssetRefreshWindowTests AssetRefreshWindowTests.cpp)
  target_link_libraries(UrsaAssetRefreshWindowTests PRIVATE Ursa::Editor)
  ursa_attach_dotnet_host_runtime(UrsaAssetRefreshWindowTests)

  add_test(NAME UrsaAssetRefreshWindowTests COMMAND UrsaAssetRefreshWindowTests)

  ursa_add_test_executable(UrsaAssetImportPipelineTests AssetImportPipelineTests.cpp)
  target_link_libraries(UrsaAssetImportPipelineTests PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaAssetImportPipelineTests PRIVATE URSA_TEST_ENGINE_SOURCE="${URSA_SOURCE_ROOT}/Assets")

  add_test(NAME UrsaAssetImportPipelineTests COMMAND UrsaAssetImportPipelineTests)

  ursa_add_test_executable(UrsaProjectShaderImportTests ProjectShaderImportTests.cpp)
  target_link_libraries(UrsaProjectShaderImportTests PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaProjectShaderImportTests PRIVATE URSA_TEST_ENGINE_SOURCE="${URSA_SOURCE_ROOT}/Assets")

  add_test(NAME UrsaProjectShaderImportTests COMMAND UrsaProjectShaderImportTests)
  target_link_libraries(UrsaProjectAssetDatabaseTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaProjectAssetDatabaseTests COMMAND UrsaProjectAssetDatabaseTests)
  target_link_libraries(UrsaAssetDatabaseTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaAssetDatabaseTests COMMAND UrsaAssetDatabaseTests)
  set_tests_properties(UrsaAssetDatabaseTests PROPERTIES
    ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")

  ursa_add_test_executable(UrsaAssetMigrationTests AssetMigrationTests.cpp)
  target_link_libraries(UrsaAssetMigrationTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaAssetMigrationTests COMMAND UrsaAssetMigrationTests)

  ursa_add_test_executable(UrsaTextureImporterTests TextureImporterTests.cpp)
  target_link_libraries(UrsaTextureImporterTests PRIVATE Ursa::Editor Ursa::StbImage)

  add_test(NAME UrsaTextureImporterTests COMMAND UrsaTextureImporterTests)

  ursa_add_test_executable(UrsaEditorShaderImportTests ShaderImportViewTests.cpp)
  target_link_libraries(UrsaEditorShaderImportTests PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaEditorShaderImportTests PRIVATE
    URSA_DXC_EXECUTABLE="${URSA_DXC_EXECUTABLE}"
    URSA_SPIRV_VAL_EXECUTABLE="${URSA_SPIRV_VAL_EXECUTABLE}"
    URSA_SPIRV_CROSS_EXECUTABLE="${URSA_SPIRV_CROSS_EXECUTABLE}")

  add_test(NAME UrsaEditorShaderImportTests COMMAND UrsaEditorShaderImportTests)

  ursa_add_test_executable(UrsaTextureDraftTests TextureDraftTests.cpp)
  target_link_libraries(UrsaTextureDraftTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaTextureDraftTests COMMAND UrsaTextureDraftTests)

  ursa_add_test_executable(UrsaMaterialDraftTests MaterialDraftTests.cpp)
  target_link_libraries(UrsaMaterialDraftTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaMaterialDraftTests COMMAND UrsaMaterialDraftTests)

  ursa_add_test_executable(UrsaSceneDraftTests SceneDraftTests.cpp)
  target_link_libraries(UrsaSceneDraftTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaSceneDraftTests COMMAND UrsaSceneDraftTests)
endif()
ursa_require_test_content(UrsaMaterialDraftTests UrsaMaterialDraftTests)
ursa_require_test_content(UrsaTextureImporterTests UrsaTextureImporterTests)
ursa_require_test_content(UrsaEditorShaderImportTests UrsaEditorShaderImportTests)

ursa_finalize_test_directory()
