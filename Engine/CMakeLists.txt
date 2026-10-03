file(GLOB_RECURSE URSA_ENGINE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Math/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Math/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Material/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Material/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scene/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scene/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Renderer3D/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Renderer3D/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Serialization/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Serialization/*.hpp")
add_library(UrsaEngine STATIC ${URSA_ENGINE_SOURCES})
add_library(Ursa::Engine ALIAS UrsaEngine)
set_target_properties(UrsaEngine PROPERTIES FOLDER "Engine")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${URSA_ENGINE_SOURCES})

file(GLOB URSA_ENGINE_SHADERS CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/Assets/Shaders/Renderer3D/*.vert"
  "${PROJECT_SOURCE_DIR}/Assets/Shaders/Renderer3D/*.frag")
foreach(source IN LISTS URSA_ENGINE_SHADERS)
  get_filename_component(shader "${source}" NAME)
  set(output "${URSA_ASSET_MIRROR_DIR}/Shaders/Renderer3D/${shader}.spv")
  add_custom_command(OUTPUT "${output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${URSA_ASSET_MIRROR_DIR}/Shaders/Renderer3D"
    COMMAND Vulkan::glslc "${source}" -o "${output}"
    DEPENDS "${source}" VERBATIM)
  target_sources(UrsaEngine PRIVATE "${source}" "${output}")
  set_source_files_properties("${source}" PROPERTIES HEADER_FILE_ONLY TRUE)
  set_source_files_properties("${output}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
  source_group(TREE "${PROJECT_SOURCE_DIR}" FILES "${source}")
  source_group("Generated Shaders" FILES "${output}")
endforeach()

target_include_directories(UrsaEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(UrsaEngine PUBLIC Ursa::Runtime nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(UrsaEngine PRIVATE /W4 /permissive-)
else()
  target_compile_options(UrsaEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()
