if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaEditorMaterialInspectorTests MaterialInspectorsTests.cpp)
  target_link_libraries(UrsaEditorMaterialInspectorTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorMaterialInspectorTests COMMAND UrsaEditorMaterialInspectorTests)

  ursa_add_test_executable(UrsaComponentEditorsTests ComponentEditorsTests.cpp)
  target_link_libraries(UrsaComponentEditorsTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaComponentEditorsTests COMMAND UrsaComponentEditorsTests)

  ursa_add_test_executable(UrsaReferencePickerTests ReferencePickerTests.cpp)
  target_link_libraries(UrsaReferencePickerTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaReferencePickerTests COMMAND UrsaReferencePickerTests)
endif()
ursa_finalize_test_directory()
