if(URSA_BUILD_EDITOR)
  ursa_add_test_executable(UrsaScriptIdeProjectTests ScriptIdeProjectTests.cpp)
  target_link_libraries(UrsaScriptIdeProjectTests PRIVATE Ursa::Editor)
  add_test(NAME UrsaScriptIdeProjectTests COMMAND UrsaScriptIdeProjectTests)
  set_tests_properties(UrsaScriptIdeProjectTests PROPERTIES
    ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")
  if(WIN32)
    ursa_add_test_executable(UrsaExternalScriptEditorTests ExternalScriptEditorTests.cpp)
    target_link_libraries(UrsaExternalScriptEditorTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaExternalScriptEditorTests COMMAND UrsaExternalScriptEditorTests)
    set_tests_properties(UrsaExternalScriptEditorTests PROPERTIES
      ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")
  endif()

  if(URSA_ENABLE_DOTNET)
    # Manual integration host: a real installed debugger drives its workspace-scoped command file.
    ursa_add_test_executable(UrsaManagedDebugAcceptance ManagedDebugAcceptance.cpp)
    target_link_libraries(UrsaManagedDebugAcceptance PRIVATE Ursa::Editor)
    target_compile_definitions(UrsaManagedDebugAcceptance PRIVATE
      URSA_IDE_ACCEPTANCE_ROOT="${URSA_BINARY_ROOT}/IdeAcceptance")
    ursa_attach_dotnet_host_runtime(UrsaManagedDebugAcceptance)

    ursa_add_test_executable(UrsaManagedEditorWorkflowTests ManagedEditorWorkflowTests.cpp)
    target_link_libraries(UrsaManagedEditorWorkflowTests PRIVATE Ursa::Editor)
    ursa_attach_dotnet_host_runtime(UrsaManagedEditorWorkflowTests)

    add_test(NAME UrsaManagedEditorWorkflowTests COMMAND UrsaManagedEditorWorkflowTests)
    set_tests_properties(UrsaManagedEditorWorkflowTests PROPERTIES TIMEOUT 240
      ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")

    ursa_add_test_executable(UrsaScriptBuildTests ScriptBuildTests.cpp)
    target_link_libraries(UrsaScriptBuildTests PRIVATE Ursa::Editor)
    add_dependencies(UrsaScriptBuildTests UrsaManagedHostAssemblies UrsaManagedScriptCatalog)

    add_test(NAME UrsaScriptBuildTests COMMAND UrsaScriptBuildTests
      "${URSA_DOTNET_EXECUTABLE}"
      "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll"
      "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Editor.dll"
      "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>/Ursa.ScriptCatalogGenerator.dll"
      "${URSA_DOTNET_REFERENCE_PACK}")
    set_tests_properties(UrsaScriptBuildTests PROPERTIES
      ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")

    ursa_add_test_executable(UrsaScriptIdentityTests ScriptIdentityTests.cpp)
    target_link_libraries(UrsaScriptIdentityTests PRIVATE Ursa::Editor)
    add_dependencies(UrsaScriptIdentityTests UrsaManagedHostAssemblies UrsaManagedScriptCatalog)
    add_test(NAME UrsaScriptIdentityTests COMMAND UrsaScriptIdentityTests
      "${URSA_DOTNET_EXECUTABLE}"
      "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll"
      "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Editor.dll"
      "${URSA_BINARY_ROOT}/Managed/Tools/$<CONFIG>/Ursa.ScriptCatalogGenerator.dll"
      "${URSA_DOTNET_REFERENCE_PACK}")
    set_tests_properties(UrsaScriptIdentityTests PROPERTIES
      ENVIRONMENT "TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")
  endif()
endif()
ursa_finalize_test_directory()

if(TEST UrsaManagedEditorWorkflowTests)
  set_tests_properties(UrsaManagedEditorWorkflowTests PROPERTIES
    ENVIRONMENT "URSA_USER_DATA_ROOT=${URSA_BINARY_ROOT}/Temp/ManagedWorkflowUser;TEMP=${URSA_BINARY_ROOT}/temp;TMP=${URSA_BINARY_ROOT}/temp")
  add_dependencies(UrsaManagedEditorWorkflowTests UrsaManagedTestAssemblies)
endif()
