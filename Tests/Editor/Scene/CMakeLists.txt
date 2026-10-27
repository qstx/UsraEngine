if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaEditorPreviewTests EditorPreviewTests.cpp)
  target_link_libraries(UrsaEditorPreviewTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorPreviewTests COMMAND UrsaEditorPreviewTests)

  ursa_add_test_executable(UrsaEditorHierarchyTests EditorHierarchyTests.cpp)
  target_link_libraries(UrsaEditorHierarchyTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorHierarchyTests COMMAND UrsaEditorHierarchyTests)

  ursa_add_test_executable(UrsaEditorGizmoTests EditorGizmoTests.cpp)
  target_link_libraries(UrsaEditorGizmoTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorGizmoTests COMMAND UrsaEditorGizmoTests)

  ursa_add_test_executable(UrsaSceneInspectorMultiObjectTests SceneInspectorMultiObjectTests.cpp)
  target_link_libraries(UrsaSceneInspectorMultiObjectTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaSceneInspectorMultiObjectTests COMMAND UrsaSceneInspectorMultiObjectTests)

  ursa_add_test_executable(UrsaEditorGizmoInteractionTests EditorGizmoInteractionTests.cpp)
  target_link_libraries(UrsaEditorGizmoInteractionTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorGizmoInteractionTests COMMAND UrsaEditorGizmoInteractionTests)

  ursa_add_test_executable(UrsaViewportLayoutTests ViewportLayoutTests.cpp)
  target_link_libraries(UrsaViewportLayoutTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaViewportLayoutTests COMMAND UrsaViewportLayoutTests)

  ursa_add_test_executable(UrsaSceneViewInputTests SceneViewInputTests.cpp)
  target_link_libraries(UrsaSceneViewInputTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaSceneViewInputTests COMMAND UrsaSceneViewInputTests)

  ursa_add_test_executable(UrsaGameViewSettingsTests GameViewSettingsTests.cpp)
  target_link_libraries(UrsaGameViewSettingsTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaGameViewSettingsTests COMMAND UrsaGameViewSettingsTests)
endif()
ursa_require_test_content(UrsaEditorPreviewTests UrsaEditorPreviewTests)
ursa_require_test_content(UrsaEditorGizmoInteractionTests UrsaEditorGizmoInteractionTests)
ursa_require_test_content(UrsaSceneViewInputTests UrsaSceneViewInputTests)

ursa_finalize_test_directory()
