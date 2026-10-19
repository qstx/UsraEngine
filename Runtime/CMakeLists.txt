ursa_collect_sources(URSA_RUNTIME_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/Core" "${CMAKE_CURRENT_SOURCE_DIR}/Window"
  "${CMAKE_CURRENT_SOURCE_DIR}/Rhi" "${CMAKE_CURRENT_SOURCE_DIR}/Vfs"
  "${CMAKE_CURRENT_SOURCE_DIR}/Diagnostics")
add_library(UrsaRuntime STATIC ${URSA_RUNTIME_SOURCES})
add_library(Ursa::Runtime ALIAS UrsaRuntime)
ursa_source_groups(UrsaRuntime "Runtime")

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
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${URSA_SOURCE_ROOT}" rev-parse --short HEAD
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
  URSA_ENGINE_ASSET_SOURCE_DIR="${URSA_SOURCE_ROOT}/Assets"
  URSA_ENGINE_FRAMEWORK_DIR="${URSA_BINARY_ROOT}/Managed/$<CONFIG>"
  URSA_BUILD_CONFIGURATION="$<CONFIG>")

if(MSVC)
  target_compile_definitions(UrsaRuntime PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
  target_compile_options(UrsaRuntime PRIVATE /W4 /permissive-)
  set_source_files_properties(Rhi/Vulkan/Vma.cpp PROPERTIES COMPILE_OPTIONS /W0)
else()
  target_compile_options(UrsaRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()
