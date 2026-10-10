file(GLOB_RECURSE URSA_ENGINE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Animation/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Animation/*.hpp"
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
  "${CMAKE_CURRENT_SOURCE_DIR}/Serialization/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scripting/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scripting/*.hpp")
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
