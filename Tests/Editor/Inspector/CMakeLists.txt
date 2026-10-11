if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaEditorMaterialInspectorTests MaterialInspectorsTests.cpp)
  target_link_libraries(UrsaEditorMaterialInspectorTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorMaterialInspectorTests COMMAND UrsaEditorMaterialInspectorTests)

  ursa_add_test_executable(UrsaMaterialInspectorHeaderSmoke MaterialInspectorHeaderSmoke.cpp)
  target_link_libraries(UrsaMaterialInspectorHeaderSmoke PRIVATE Ursa::Editor)

  add_test(NAME UrsaMaterialInspectorHeaderSmoke COMMAND UrsaMaterialInspectorHeaderSmoke)

  ursa_add_test_executable(UrsaComponentEditorsTests ComponentEditorsTests.cpp)
  target_link_libraries(UrsaComponentEditorsTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaComponentEditorsTests COMMAND UrsaComponentEditorsTests)
endif()
ursa_finalize_test_directory()
