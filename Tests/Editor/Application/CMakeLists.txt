if(URSA_BUILD_EDITOR)
  ursa_add_test_executable(UrsaFrameCaptureTests FrameCaptureTests.cpp)
  target_link_libraries(UrsaFrameCaptureTests PRIVATE Ursa::Editor Ursa::StbImage)
  add_test(NAME UrsaFrameCaptureTests COMMAND UrsaFrameCaptureTests)

  if(URSA_ENABLE_DOTNET)
    ursa_add_test_executable(UrsaMeshEditorWindowTests MeshEditorWindowTests.cpp)
    target_link_libraries(UrsaMeshEditorWindowTests PRIVATE Ursa::Editor)
    ursa_attach_dotnet_host_runtime(UrsaMeshEditorWindowTests)
    add_test(NAME UrsaMeshEditorWindowTests COMMAND UrsaMeshEditorWindowTests "${URSA_TEST_SAMPLE_PROJECT}")
    set_tests_properties(UrsaMeshEditorWindowTests PROPERTIES TIMEOUT 120
      ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/MeshEditorUser")
    ursa_require_sample_project(UrsaMeshEditorWindowTests UrsaMeshEditorWindowTests)
  endif()

  ursa_add_test_executable(UrsaEditorCompositeWindowTests EditorCompositeWindowTests.cpp)
  target_link_libraries(UrsaEditorCompositeWindowTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorCompositeWindowTests COMMAND UrsaEditorCompositeWindowTests)

  ursa_add_test_executable(UrsaPlaySessionTests PlaySessionTests.cpp)
  target_link_libraries(UrsaPlaySessionTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaPlaySessionTests COMMAND UrsaPlaySessionTests)
endif()
ursa_require_test_content(UrsaEditorCompositeWindowTests UrsaEditorCompositeWindowTests)

ursa_finalize_test_directory()
