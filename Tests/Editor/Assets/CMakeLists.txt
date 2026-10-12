if(URSA_BUILD_EDITOR)

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
