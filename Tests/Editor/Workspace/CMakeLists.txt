if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaAssetPreviewWorkspaceTests AssetPreviewWorkspaceTests.cpp)
  target_link_libraries(UrsaAssetPreviewWorkspaceTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaAssetPreviewWorkspaceTests COMMAND "${URSA_POWERSHELL_EXECUTABLE}" -NoProfile
    -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/AssetPreviewWorkspaceTests.ps1"
    -Executable "$<TARGET_FILE:UrsaAssetPreviewWorkspaceTests>"
    -EvidenceRoot "${URSA_BINARY_ROOT}/Temp/AssetPreviewWorkspaceEvidence/$<CONFIG>")
  set_tests_properties(UrsaAssetPreviewWorkspaceTests PROPERTIES TIMEOUT 600
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/AssetPreviewWorkspaceUser/$<CONFIG>;TEMP=${URSA_BINARY_ROOT}/Temp;TMP=${URSA_BINARY_ROOT}/Temp")

  ursa_add_test_executable(UrsaEditorWorkspaceWindowTests EditorWorkspaceWindowTests.cpp)
  target_link_libraries(UrsaEditorWorkspaceWindowTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorWorkspaceWindowTests COMMAND UrsaEditorWorkspaceWindowTests)
  set_tests_properties(UrsaEditorWorkspaceWindowTests PROPERTIES
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/IconWorkspaceUser;TEMP=${URSA_BINARY_ROOT}/Temp;TMP=${URSA_BINARY_ROOT}/Temp")

  ursa_add_test_executable(UrsaProjectWorkspaceTests ProjectWorkspaceTests.cpp)
  target_link_libraries(UrsaProjectWorkspaceTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaProjectWorkspaceTests COMMAND UrsaProjectWorkspaceTests)

  ursa_add_test_executable(UrsaEditorDocumentsTests EditorDocumentsTests.cpp)
  target_link_libraries(UrsaEditorDocumentsTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaEditorDocumentsTests COMMAND UrsaEditorDocumentsTests)

  ursa_add_test_executable(UrsaProjectBrowserModelTests ProjectBrowserModelTests.cpp)
  target_link_libraries(UrsaProjectBrowserModelTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaProjectBrowserModelTests COMMAND UrsaProjectBrowserModelTests)
  set_tests_properties(UrsaProjectBrowserModelTests PROPERTIES
    ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/Temp;TMP=${URSA_BINARY_ROOT}/Temp")
endif()
ursa_finalize_test_directory()
