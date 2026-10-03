file(GLOB_RECURSE URSA_ENGINE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/application/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/application/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/math/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/material/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/material/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/renderer3d/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/renderer3d/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/serialization/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/serialization/*.hpp")
add_library(UrsaEngine STATIC ${URSA_ENGINE_SOURCES})
add_library(Ursa::Engine ALIAS UrsaEngine)
set_target_properties(UrsaEngine PROPERTIES FOLDER "Engine")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${URSA_ENGINE_SOURCES})

file(GLOB URSA_ENGINE_SHADERS CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/assets/shaders/renderer3d/*.vert"
  "${PROJECT_SOURCE_DIR}/assets/shaders/renderer3d/*.frag")
foreach(source IN LISTS URSA_ENGINE_SHADERS)
  get_filename_component(shader "${source}" NAME)
  set(output "${URSA_ASSET_MIRROR_DIR}/shaders/renderer3d/${shader}.spv")
  add_custom_command(OUTPUT "${output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${URSA_ASSET_MIRROR_DIR}/shaders/renderer3d"
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
