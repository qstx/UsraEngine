function(ursa_add_tool target module)
  ursa_collect_sources(sources "${CMAKE_CURRENT_SOURCE_DIR}/${module}")
  add_executable(${target} ${sources})
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${URSA_BINARY_ROOT}/$<CONFIG>")
  ursa_source_groups(${target} "Tools/${module}")
endfunction()

ursa_add_tool(UrsaCapture Capture)
target_link_libraries(UrsaCapture PRIVATE Ursa::Runtime)

ursa_add_tool(UrsaPak Pak)
target_link_libraries(UrsaPak PRIVATE Ursa::Engine)
target_compile_definitions(UrsaPak PRIVATE URSA_BUILD_CONFIGURATION="$<CONFIG>")
ursa_add_tool(UrsaShaderImporter ShaderImporter)
target_link_libraries(UrsaShaderImporter PRIVATE Ursa::Engine)

if(WIN32)
  add_custom_target(UrsaEngineFormatCheck
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/Format.ps1" -Check -ProjectDir "${URSA_PROJECT_DIR}"
    WORKING_DIRECTORY "${URSA_SOURCE_ROOT}" VERBATIM)
  set_target_properties(UrsaEngineFormatCheck PROPERTIES FOLDER "Tools")
  if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    add_custom_target(UrsaProjectStructureCheck
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/RunPython.bat"
        "${CMAKE_CURRENT_SOURCE_DIR}/CheckProjectStructure.py" --build-dir "${URSA_BINARY_ROOT}"
      VERBATIM)
    set_target_properties(UrsaProjectStructureCheck PROPERTIES FOLDER "Tools")
  endif()
endif()

if(URSA_BUILD_EDITOR)
  if(WIN32 AND URSA_ENABLE_DOTNET)
    ursa_add_tool(UrsaIdeBridge IdeBridge)
    target_link_libraries(UrsaIdeBridge PRIVATE Ursa::Runtime ole32 oleaut32 comsuppw)
    set_target_properties(UrsaIdeBridge PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${URSA_BINARY_ROOT}/Editor/$<CONFIG>")
    add_dependencies(UrsaEditor UrsaIdeBridge)
  endif()
  ursa_add_tool(UrsaMeshMigrate MeshMigrate)
  target_link_libraries(UrsaMeshMigrate PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaMeshMigrate PRIVATE URSA_MESH_MIGRATION_ENGINE_ASSETS="${URSA_SOURCE_ROOT}/Assets")
  ursa_add_tool(UrsaAssetImport AssetImport)
  target_link_libraries(UrsaAssetImport PRIVATE Ursa::Editor)
  ursa_attach_dotnet_host_runtime(UrsaAssetImport)
  set_target_properties(UrsaAssetImport PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${URSA_BINARY_ROOT}/Editor/$<CONFIG>")
  ursa_add_tool(UrsaAssetMigrate AssetMigrate)
  target_link_libraries(UrsaAssetMigrate PRIVATE Ursa::Editor)
  target_link_libraries(UrsaPak PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaPak PRIVATE URSA_HAS_EDITOR_ASSET_PIPELINE=1)

  if(URSA_ENABLE_DOTNET)
    ursa_add_tool(UrsaScriptCompile ScriptCompile)
    target_link_libraries(UrsaScriptCompile PRIVATE Ursa::Editor)
    ursa_attach_dotnet_host_runtime(UrsaScriptCompile)
    add_dependencies(UrsaScriptCompile UrsaManagedHostAssemblies UrsaManagedScriptCatalog)
    ursa_add_tool(UrsaScriptPlayer ScriptPlayer)
    target_link_libraries(UrsaScriptPlayer PRIVATE Ursa::DotNetHost)
    target_compile_definitions(UrsaScriptPlayer PRIVATE URSA_SCRIPT_PLAYER_ENGINE_ASSETS="${URSA_SOURCE_ROOT}/Assets")
    ursa_attach_dotnet_host_runtime(UrsaScriptPlayer)
    if(NOT URSA_PROJECT_DIR STREQUAL "")
      set_target_properties(UrsaScriptCompile PROPERTIES
        VS_DEBUGGER_COMMAND_ARGUMENTS "\"${URSA_PROJECT_DIR}\" $<CONFIG>"
        VS_DEBUGGER_WORKING_DIRECTORY "${URSA_SOURCE_ROOT}")
      set_target_properties(UrsaScriptPlayer PROPERTIES
        VS_DEBUGGER_COMMAND_ARGUMENTS "--project \"${URSA_PROJECT_DIR}\" --scene Snake/Snake.ursascene"
        VS_DEBUGGER_WORKING_DIRECTORY "${URSA_SOURCE_ROOT}")
    endif()
  endif()
  if(NOT URSA_PROJECT_DIR STREQUAL "")
    add_custom_target(UrsaEnginePackage
      COMMAND UrsaPak --project-root "${URSA_PROJECT_DIR}" --configuration "$<CONFIG>"
        --output "${URSA_BINARY_ROOT}/Package/$<CONFIG>/Content.pak"
      DEPENDS UrsaPak VERBATIM)
    set_target_properties(UrsaEnginePackage PROPERTIES FOLDER "Tools/Pak")
  endif()
endif()
