if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaImGuiBridgeTests ImGuiBridgeTests.cpp)
  target_link_libraries(UrsaImGuiBridgeTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaImGuiBridgeTests COMMAND UrsaImGuiBridgeTests)

  ursa_add_test_executable(UrsaEditorIconsTests EditorIconsTests.cpp)
  target_link_libraries(UrsaEditorIconsTests PRIVATE Ursa::Editor Ursa::StbImage)
  target_compile_definitions(UrsaEditorIconsTests PRIVATE
    URSA_ICON_EVIDENCE_DIRECTORY="${URSA_BINARY_ROOT}/IconEvidence")
  add_test(NAME UrsaEditorIconsTests COMMAND UrsaEditorIconsTests)
endif()
ursa_require_test_content(UrsaImGuiBridgeTests UrsaImGuiBridgeTests)
ursa_require_test_content(UrsaEditorIconsTests UrsaEditorIconsTests)

ursa_finalize_test_directory()
