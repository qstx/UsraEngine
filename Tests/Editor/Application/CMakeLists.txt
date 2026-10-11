if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaEditorCompositeWindowTests EditorCompositeWindowTests.cpp)
  target_link_libraries(UrsaEditorCompositeWindowTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorCompositeWindowTests COMMAND UrsaEditorCompositeWindowTests)

  ursa_add_test_executable(UrsaPlaySessionTests PlaySessionTests.cpp)
  target_link_libraries(UrsaPlaySessionTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaPlaySessionTests COMMAND UrsaPlaySessionTests)
endif()
ursa_require_test_content(UrsaEditorCompositeWindowTests UrsaEditorCompositeWindowTests)

ursa_finalize_test_directory()
