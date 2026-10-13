ursa_collect_sources(URSA_ENGINE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/Application" "${CMAKE_CURRENT_SOURCE_DIR}/Animation"
  "${CMAKE_CURRENT_SOURCE_DIR}/Math" "${CMAKE_CURRENT_SOURCE_DIR}/Material" "${CMAKE_CURRENT_SOURCE_DIR}/Mesh"
  "${CMAKE_CURRENT_SOURCE_DIR}/Shader" "${CMAKE_CURRENT_SOURCE_DIR}/Scene"
  "${CMAKE_CURRENT_SOURCE_DIR}/Renderer3D" "${CMAKE_CURRENT_SOURCE_DIR}/Rendering"
  "${CMAKE_CURRENT_SOURCE_DIR}/ScriptableAssets" "${CMAKE_CURRENT_SOURCE_DIR}/Serialization"
  "${CMAKE_CURRENT_SOURCE_DIR}/Scripting")
list(FILTER URSA_ENGINE_SOURCES EXCLUDE REGEX "/Scripting/DotNet/")
add_library(UrsaEngine STATIC ${URSA_ENGINE_SOURCES})
add_library(Ursa::Engine ALIAS UrsaEngine)
ursa_source_groups(UrsaEngine "Engine")

target_include_directories(UrsaEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(UrsaEngine PUBLIC Ursa::Runtime nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(UrsaEngine PRIVATE /W4 /permissive-)
else()
  target_compile_options(UrsaEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()
