set(URSA_IMGUI_ROOT "${URSA_SOURCE_ROOT}/ThirdParty/imgui")
set(URSA_IMGUIZMO_ROOT "${URSA_SOURCE_ROOT}/ThirdParty/imguizmo/src")

add_library(UrsaImGui STATIC
  "${URSA_IMGUI_ROOT}/imgui.cpp"
  "${URSA_IMGUI_ROOT}/imgui_draw.cpp"
  "${URSA_IMGUI_ROOT}/imgui_tables.cpp"
  "${URSA_IMGUI_ROOT}/imgui_widgets.cpp"
  "${URSA_IMGUIZMO_ROOT}/ImGuizmo.cpp")
set_target_properties(UrsaImGui PROPERTIES FOLDER "ThirdParty")
target_include_directories(UrsaImGui PUBLIC "${URSA_IMGUI_ROOT}" "${URSA_IMGUIZMO_ROOT}")

ursa_collect_sources(URSA_EDITOR_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/Application" "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
  "${CMAKE_CURRENT_SOURCE_DIR}/Console" "${CMAKE_CURRENT_SOURCE_DIR}/Inspector"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scene" "${CMAKE_CURRENT_SOURCE_DIR}/Serialization"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scripting" "${CMAKE_CURRENT_SOURCE_DIR}/Tools"
  "${CMAKE_CURRENT_SOURCE_DIR}/Ui" "${CMAKE_CURRENT_SOURCE_DIR}/Workspace")
add_library(UrsaEditorCore STATIC ${URSA_EDITOR_SOURCES})
add_library(Ursa::Editor ALIAS UrsaEditorCore)
ursa_source_groups(UrsaEditorCore "Editor")
target_include_directories(UrsaEditorCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(UrsaEditorCore PUBLIC Ursa::Engine UrsaImGui PRIVATE assimp::assimp Ursa::StbImage)
if(WIN32)
  target_link_libraries(UrsaEditorCore PRIVATE advapi32)
endif()
target_compile_definitions(UrsaEditorCore PRIVATE
  URSA_DXC_EXECUTABLE="${URSA_DXC_EXECUTABLE}"
  URSA_SPIRV_VAL_EXECUTABLE="${URSA_SPIRV_VAL_EXECUTABLE}"
  URSA_SPIRV_CROSS_EXECUTABLE="${URSA_SPIRV_CROSS_EXECUTABLE}")

ursa_collect_sources(editorLauncherSources "${CMAKE_CURRENT_SOURCE_DIR}/Launcher")
add_executable(UrsaEditor ${editorLauncherSources})
ursa_source_groups(UrsaEditor "Editor/Launcher")
target_link_libraries(UrsaEditor PRIVATE Ursa::Editor)
ursa_attach_dotnet_host_runtime(UrsaEditor)
if(NOT URSA_PROJECT_DIR STREQUAL "")
  set_target_properties(UrsaEditor PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "--project \"${URSA_PROJECT_DIR}\" --scene Snake/Snake.ursascene"
    VS_DEBUGGER_WORKING_DIRECTORY "${URSA_SOURCE_ROOT}")
endif()

if(MSVC)
  target_compile_options(UrsaEditorCore PRIVATE /W4 /permissive-)
else()
  target_compile_options(UrsaEditorCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(URSA_ENABLE_DOTNET)
    target_link_libraries(UrsaEditorCore PRIVATE Ursa::DotNetHost)
    add_dependencies(UrsaEditorCore UrsaManagedScriptCatalog)
    target_compile_definitions(UrsaEditorCore PUBLIC
      URSA_ENABLE_DOTNET=1
      URSA_DOTNET_EXECUTABLE_PATH="${URSA_DOTNET_EXECUTABLE}"
      URSA_DOTNET_RUNTIME_ASSEMBLY_PATH="${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll"
      URSA_DOTNET_EDITOR_ASSEMBLY_PATH="${URSA_MANAGED_OUTPUT_DIR}/Ursa.Editor.dll"
      URSA_DOTNET_CATALOG_GENERATOR_PATH="${URSA_BINARY_ROOT}/Managed/Tools/Ursa.ScriptCatalogGenerator.dll"
      URSA_DOTNET_REFERENCE_PACK_PATH="${URSA_DOTNET_REFERENCE_PACK}")
endif()
