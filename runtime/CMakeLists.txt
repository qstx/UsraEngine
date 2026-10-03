file(GLOB_RECURSE URSA_RUNTIME_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/window/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/window/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/vfs/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/vfs/*.hpp")
add_library(UrsaRuntime STATIC ${URSA_RUNTIME_SOURCES})
add_library(Ursa::Runtime ALIAS UrsaRuntime)
set_target_properties(UrsaRuntime PROPERTIES FOLDER "Engine")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${URSA_RUNTIME_SOURCES})

target_include_directories(UrsaRuntime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(UrsaRuntime PUBLIC cxx_std_17)
target_link_libraries(UrsaRuntime PUBLIC EASTL PRIVATE glfw Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator spdlog::spdlog)
target_compile_definitions(UrsaRuntime PRIVATE GLFW_INCLUDE_NONE)
target_compile_definitions(UrsaRuntime PRIVATE
  URSA_ENGINE_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets"
  URSA_ASSET_MIRROR_DIR="${URSA_ASSET_MIRROR_DIR}")

if(MSVC)
  target_compile_definitions(UrsaRuntime PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
  target_compile_options(UrsaRuntime PRIVATE /W4 /permissive-)
  set_source_files_properties(rhi/vulkan/Vma.cpp PROPERTIES COMPILE_OPTIONS /W0)
else()
  target_compile_options(UrsaRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()
