# Resource labels describe requirements; fast/slow select daily or extended regression.
function(ursa_classify_test test folder)
  set(gpuTests
    UrsaProfilerGpuMemoryTests UrsaRhiTests UrsaRendererTests UrsaMaterialRendererTests
    UrsaMaterialSceneSmoke UrsaRendererWindowTests UrsaRenderFrameTests
    UrsaRenderCommandExecutorTests UrsaRenderingBindingsTests UrsaManagedSceneApiTests
    UrsaMeshRendererTests UrsaShaderReflectionTests UrsaShaderPackageTests UrsaShaderExampleImportTests
    UrsaImGuiBridgeTests UrsaFrameCaptureTests UrsaEditorCompositeWindowTests
    UrsaEditorPreviewTests UrsaEditorGizmoInteractionTests UrsaMaterialDraftTests
    UrsaTextureImporterTests UrsaScriptableAssetInspectorTests UrsaEditorShaderImportTests
    UrsaMeshEditorWindowTests UrsaAssetRefreshWindowTests UrsaEditorWorkspaceWindowTests
    UrsaProfilerEditorWindowTests UrsaEditorSrpAcceptanceTests UrsaDotNetHostSmoke
    UrsaProjectModelImportTests UrsaProjectPlaySessionTests UrsaProjectPipelineSamplesTests
    UrsaAssetImportCliTests UrsaProjectSnakePlayerSmoke UrsaProjectSnakeGameplaySmoke
    UrsaProjectMaterialPlayerSmoke UrsaProjectMaterialEditorSmoke UrsaProjectEditorOpenSmoke
    UrsaProjectSnakePakSmoke)
  set(windowTests
    UrsaWindowInputTests UrsaRendererWindowTests UrsaEditorCompositeWindowTests
    UrsaMeshEditorWindowTests UrsaAssetRefreshWindowTests UrsaEditorWorkspaceWindowTests
    UrsaProfilerEditorWindowTests UrsaEditorSrpAcceptanceTests
    UrsaProjectMaterialEditorSmoke UrsaProjectEditorOpenSmoke UrsaProjectPipelineSamplesTests
    UrsaProjectSnakePlayerSmoke UrsaProjectSnakeGameplaySmoke UrsaProjectSnakePakSmoke)
  set(buildTests
    UrsaBindingsGeneratorTests UrsaBindingsSignatureTests UrsaBindingsTests
    UrsaShaderCompilerTests UrsaShaderCompileCacheTests UrsaSnakeManagedLogicTests)

  set(labels)
  if(test IN_LIST gpuTests)
    list(APPEND labels gpu)
  endif()
  if(test IN_LIST windowTests)
    list(APPEND labels window)
  endif()
  if(folder MATCHES "^Tests/Editor/Assets$" OR test MATCHES "Import|PipelineSamples")
    list(APPEND labels import)
  endif()
  if(folder MATCHES "^Tests/(Build|Editor/Scripting)$" OR test IN_LIST buildTests)
    list(APPEND labels build)
  endif()
  if(folder STREQUAL "Tests/Integration")
    list(APPEND labels integration)
  endif()
  get_property(setup TEST ${test} PROPERTY FIXTURES_SETUP)
  get_property(required TEST ${test} PROPERTY FIXTURES_REQUIRED)
  if(setup)
    list(APPEND labels fixture)
  endif()
  if(labels OR required)
    list(APPEND labels slow)
  else()
    list(APPEND labels fast)
  endif()
  set_property(TEST ${test} APPEND PROPERTY LABELS ${labels})
endfunction()
