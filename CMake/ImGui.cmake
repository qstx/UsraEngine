# The drawing backend is shared by the runtime and the editor. ImGuizmo stays
# in the editor target and must never become a Player dependency.
set(URSA_IMGUI_ROOT "${URSA_SOURCE_ROOT}/ThirdParty/imgui")
if(NOT EXISTS "${URSA_IMGUI_ROOT}/imgui.cpp")
  message(FATAL_ERROR "ImGui submodule is missing; run git submodule update --init ThirdParty/imgui")
endif()
add_library(UrsaImGui STATIC
  "${URSA_IMGUI_ROOT}/imgui.cpp"
  "${URSA_IMGUI_ROOT}/imgui_draw.cpp"
  "${URSA_IMGUI_ROOT}/imgui_tables.cpp"
  "${URSA_IMGUI_ROOT}/imgui_widgets.cpp")
set_target_properties(UrsaImGui PROPERTIES FOLDER "ThirdParty")
target_include_directories(UrsaImGui PUBLIC "${URSA_IMGUI_ROOT}")
