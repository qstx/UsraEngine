set(URSA_IMGUI_ROOT "${PROJECT_SOURCE_DIR}/third_party/imgui")
set(URSA_IMGUIZMO_ROOT "${PROJECT_SOURCE_DIR}/third_party/imguizmo/src")

add_library(UrsaImGui STATIC
  "${URSA_IMGUI_ROOT}/imgui.cpp"
  "${URSA_IMGUI_ROOT}/imgui_draw.cpp"
  "${URSA_IMGUI_ROOT}/imgui_tables.cpp"
  "${URSA_IMGUI_ROOT}/imgui_widgets.cpp"
  "${URSA_IMGUIZMO_ROOT}/ImGuizmo.cpp")
set_target_properties(UrsaImGui PROPERTIES FOLDER "Engine")
target_include_directories(UrsaImGui PUBLIC "${URSA_IMGUI_ROOT}" "${URSA_IMGUIZMO_ROOT}")

file(GLOB_RECURSE URSA_EDITOR_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scene/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scene/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Serialization/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Serialization/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scripting/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scripting/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Tools/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Tools/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Ui/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Ui/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Workspace/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Workspace/*.hpp")
add_library(UrsaEditorCore STATIC ${URSA_EDITOR_SOURCES})
add_library(Ursa::Editor ALIAS UrsaEditorCore)
set_target_properties(UrsaEditorCore PROPERTIES FOLDER "Engine")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${URSA_EDITOR_SOURCES})
target_include_directories(UrsaEditorCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(UrsaEditorCore PUBLIC Ursa::Engine UrsaImGui PRIVATE assimp::assimp Ursa::StbImage)
target_compile_definitions(UrsaEditorCore PRIVATE
  URSA_DXC_EXECUTABLE="${URSA_DXC_EXECUTABLE}"
  URSA_SPIRV_VAL_EXECUTABLE="${URSA_SPIRV_VAL_EXECUTABLE}"
  URSA_SPIRV_CROSS_EXECUTABLE="${URSA_SPIRV_CROSS_EXECUTABLE}")

foreach(shader IN ITEMS ImGui.vert ImGui.frag)
  set(input "${CMAKE_CURRENT_SOURCE_DIR}/Ui/Shaders/${shader}")
  set(output "${URSA_ASSET_MIRROR_DIR}/Shaders/Ui/${shader}.spv")
  add_custom_command(OUTPUT "${output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${URSA_ASSET_MIRROR_DIR}/Shaders/Ui"
    COMMAND Vulkan::glslc "${input}" -o "${output}"
    DEPENDS "${input}" VERBATIM)
  target_sources(UrsaEditorCore PRIVATE "${input}" "${output}")
  set_source_files_properties("${input}" PROPERTIES HEADER_FILE_ONLY TRUE)
  set_source_files_properties("${output}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
  source_group("Generated Shaders" FILES "${output}")
endforeach()

add_executable(UrsaEditor Launcher/Main.cpp)
set_target_properties(UrsaEditor PROPERTIES FOLDER "Engine")
target_link_libraries(UrsaEditor PRIVATE Ursa::Editor)
ursa_attach_dotnet_host_runtime(UrsaEditor)
add_dependencies(UrsaEditor UrsaEngineShaderAssets)
if(NOT URSA_PROJECT_DIR STREQUAL "")
  set_target_properties(UrsaEditor PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "--project \"${URSA_PROJECT_DIR}\" --scene Snake/Snake.ursascene"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()

if(MSVC)
  target_compile_options(UrsaEditorCore PRIVATE /W4 /permissive-)
else()
  target_compile_options(UrsaEditorCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
