file(GLOB_RECURSE URSA_ENGINE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Math/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Math/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Material/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Material/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Shader/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Shader/*.hpp"
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

target_include_directories(UrsaEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(UrsaEngine PUBLIC Ursa::Runtime nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(UrsaEngine PRIVATE /W4 /permissive-)
else()
  target_compile_options(UrsaEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(UrsaShaderImporter "${PROJECT_SOURCE_DIR}/Tools/ShaderImporter/Main.cpp")
target_link_libraries(UrsaShaderImporter PRIVATE Ursa::Engine)
set_target_properties(UrsaShaderImporter PROPERTIES FOLDER "Tools")

file(GLOB_RECURSE URSA_SHADER_SOURCE_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/Assets/Shaders/*.ursashader"
  "${PROJECT_SOURCE_DIR}/Assets/Shaders/*.hlsli"
  "${PROJECT_SOURCE_DIR}/Assets/Shaders/*.hlsl"
  "${PROJECT_SOURCE_DIR}/Assets/*.ursamat")
set(URSA_SHADER_IMPORT_STAMP "${CMAKE_CURRENT_BINARY_DIR}/shader-import.stamp")
set(URSA_SHADER_IMPORT_CACHE "${URSA_ASSET_CACHE_DIR}/ShaderImportCache")
add_custom_command(OUTPUT "${URSA_SHADER_IMPORT_STAMP}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${URSA_ASSET_MIRROR_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${URSA_SHADER_IMPORT_CACHE}"
  COMMAND $<TARGET_FILE:UrsaShaderImporter>
    --source-root "${PROJECT_SOURCE_DIR}/Assets"
    --output-root "${URSA_ASSET_MIRROR_DIR}"
    --cache-root "${URSA_SHADER_IMPORT_CACHE}"
    --dxc "${URSA_DXC_EXECUTABLE}"
    --spirv-val "${URSA_SPIRV_VAL_EXECUTABLE}"
    --spirv-cross "${URSA_SPIRV_CROSS_EXECUTABLE}"
  COMMAND ${CMAKE_COMMAND} -E touch "${URSA_SHADER_IMPORT_STAMP}"
  DEPENDS UrsaShaderImporter ${URSA_SHADER_SOURCE_FILES}
  VERBATIM)
add_custom_target(UrsaEngineShaderAssets ALL DEPENDS "${URSA_SHADER_IMPORT_STAMP}")
set_target_properties(UrsaEngineShaderAssets PROPERTIES FOLDER "Assets")
