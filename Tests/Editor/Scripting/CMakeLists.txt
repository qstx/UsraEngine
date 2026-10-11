if(URSA_BUILD_EDITOR)
  if(URSA_ENABLE_DOTNET)

    ursa_add_test_executable(UrsaManagedEditorWorkflowTests ManagedEditorWorkflowTests.cpp)
    target_link_libraries(UrsaManagedEditorWorkflowTests PRIVATE Ursa::Editor)
    ursa_attach_dotnet_host_runtime(UrsaManagedEditorWorkflowTests)

    add_test(NAME UrsaManagedEditorWorkflowTests COMMAND UrsaManagedEditorWorkflowTests)
    set_tests_properties(UrsaManagedEditorWorkflowTests PROPERTIES TIMEOUT 120
      ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")

    ursa_add_test_executable(UrsaScriptBuildTests ScriptBuildTests.cpp)
    target_link_libraries(UrsaScriptBuildTests PRIVATE Ursa::Editor)
    add_dependencies(UrsaScriptBuildTests UrsaManagedHostAssemblies UrsaManagedScriptCatalog)

    add_test(NAME UrsaScriptBuildTests COMMAND UrsaScriptBuildTests
      "${URSA_DOTNET_EXECUTABLE}"
      "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll"
      "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Editor.dll"
      "${URSA_BINARY_ROOT}/Managed/Tools/Ursa.ScriptCatalogGenerator.dll"
      "${URSA_DOTNET_REFERENCE_PACK}")
    set_tests_properties(UrsaScriptBuildTests PROPERTIES
      ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")
  endif()
endif()
ursa_finalize_test_directory()

if(TEST UrsaManagedEditorWorkflowTests)
  set_tests_properties(UrsaManagedEditorWorkflowTests PROPERTIES
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/ManagedWorkflowUser")
  add_dependencies(UrsaManagedEditorWorkflowTests UrsaManagedTestAssemblies)
endif()
