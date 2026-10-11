file(GLOB_RECURSE URSA_RUNTIME_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Window/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Window/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Rhi/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Rhi/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Vfs/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Vfs/*.hpp")
add_library(UrsaRuntime STATIC ${URSA_RUNTIME_SOURCES})
add_library(Ursa::Runtime ALIAS UrsaRuntime)
set_target_properties(UrsaRuntime PROPERTIES FOLDER "Engine")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${URSA_RUNTIME_SOURCES})

target_include_directories(UrsaRuntime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(UrsaRuntime PUBLIC cxx_std_17)
target_link_libraries(UrsaRuntime PUBLIC EASTL PRIVATE glfw Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator spdlog::spdlog)
if(URSA_ENABLE_TRACY)
  target_link_libraries(UrsaRuntime PUBLIC Tracy::TracyClient)
  target_compile_definitions(UrsaRuntime PUBLIC URSA_ENABLE_TRACY=1)
  target_compile_definitions(UrsaRuntime PRIVATE TRACY_VK_USE_SYMBOL_TABLE=1)
endif()
if(URSA_ENABLE_TRACY_MEMORY)
  target_compile_definitions(UrsaRuntime PUBLIC URSA_ENABLE_TRACY_MEMORY=1)
endif()
if(URSA_ENABLE_TRACY_DIAGNOSTICS)
  target_compile_definitions(UrsaRuntime PUBLIC URSA_ENABLE_TRACY_DIAGNOSTICS=1)
endif()
set(URSA_PROFILING_REVISION "unknown")
if(URSA_ENABLE_TRACY_DIAGNOSTICS)
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --short HEAD
      RESULT_VARIABLE URSA_REVISION_RESULT OUTPUT_VARIABLE URSA_REVISION_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(URSA_REVISION_RESULT EQUAL 0)
      set(URSA_PROFILING_REVISION "${URSA_REVISION_OUTPUT}")
    endif()
  endif()
endif()
target_compile_definitions(UrsaRuntime PRIVATE
  URSA_TRACY_MEMORY_CALLSTACK_DEPTH=${URSA_TRACY_MEMORY_CALLSTACK_DEPTH}
  URSA_PROFILING_VERSION="${PROJECT_VERSION}"
  URSA_PROFILING_REVISION="${URSA_PROFILING_REVISION}")
target_link_libraries(UrsaRuntime PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(UrsaRuntime PRIVATE GLFW_INCLUDE_NONE)
target_compile_definitions(UrsaRuntime PRIVATE
  URSA_ENGINE_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/Assets"
  URSA_ENGINE_FRAMEWORK_DIR="${CMAKE_BINARY_DIR}/Managed/$<CONFIG>"
  URSA_BUILD_CONFIGURATION="$<CONFIG>")

if(MSVC)
  target_compile_definitions(UrsaRuntime PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
  target_compile_options(UrsaRuntime PRIVATE /W4 /permissive-)
  set_source_files_properties(Rhi/Vulkan/Vma.cpp PROPERTIES COMPILE_OPTIONS /W0)
else()
  target_compile_options(UrsaRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(URSA_ENABLE_DOTNET)
  add_subdirectory(Scripting)
endif()
