cmake_minimum_required(VERSION 3.20)
project(UrsaEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Keep the debug library naming consistent before dependencies configure their own cache settings.
set(CMAKE_DEBUG_POSTFIX "d")
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

option(URSA_MSVC_PARALLEL_COMPILE "Compile multiple source files concurrently with Visual Studio/MSVC" ON)
if(URSA_MSVC_PARALLEL_COMPILE AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
   AND CMAKE_GENERATOR MATCHES "^Visual Studio")
  # MSBuild /m schedules projects; /MP parallelizes source files within each project.
  # Without a numeric limit MSVC uses the available logical processors.
  add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/MP>)
endif()

set(URSA_PROJECT_DIR "" CACHE PATH "Optional C# project path used by Engine integration tests and package commands")
if(NOT URSA_PROJECT_DIR STREQUAL "")
  get_filename_component(URSA_PROJECT_DIR "${URSA_PROJECT_DIR}" ABSOLUTE)
  if(NOT EXISTS "${URSA_PROJECT_DIR}/Assets")
    message(FATAL_ERROR "URSA_PROJECT_DIR must point to a project with an Assets directory: ${URSA_PROJECT_DIR}")
  endif()
endif()

if(WIN32)
  add_custom_target(UrsaEngineFormatCheck
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Format.ps1" -Check -ProjectDir "${URSA_PROJECT_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
  set_target_properties(UrsaEngineFormatCheck PROPERTIES FOLDER "Tools")
endif()

option(URSA_BUILD_EDITOR "Build the Ursa editor" ON)
option(URSA_ENABLE_DOTNET "Enable the optional .NET 10 scripting host" OFF)
option(URSA_ENABLE_TRACY "Enable optional Tracy CPU and Vulkan GPU profiling" OFF)
option(URSA_WARNINGS_AS_ERRORS "Treat warnings in Ursa native and managed targets as errors" ON)

function(ursa_configure_warning_policy directory)
  get_property(directoryTargets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS directoryTargets)
    get_target_property(targetType ${target} TYPE)
    if(target STREQUAL "UrsaImGui" OR NOT targetType MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
      continue()
    endif()
    if(MSVC)
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)
    else()
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
    endif()
  endforeach()
  get_property(childDirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(childDirectory IN LISTS childDirectories)
    if(NOT childDirectory MATCHES "/third_party(/|$)")
      ursa_configure_warning_policy("${childDirectory}")
    endif()
  endforeach()
endfunction()

if(URSA_ENABLE_DOTNET)
  if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "URSA_ENABLE_DOTNET currently supports Windows x64 only")
  endif()

  find_program(URSA_DOTNET_EXECUTABLE NAMES dotnet)
  if(NOT URSA_DOTNET_EXECUTABLE)
    message(FATAL_ERROR "URSA_ENABLE_DOTNET requires the .NET 10 SDK and runtime. Install .NET 10 or configure with -DURSA_ENABLE_DOTNET=OFF")
  endif()

  execute_process(
    COMMAND "${URSA_DOTNET_EXECUTABLE}" --list-sdks
    RESULT_VARIABLE URSA_DOTNET_SDK_RESULT
    OUTPUT_VARIABLE URSA_DOTNET_SDKS
    ERROR_VARIABLE URSA_DOTNET_SDK_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(
    COMMAND "${URSA_DOTNET_EXECUTABLE}" --list-runtimes
    RESULT_VARIABLE URSA_DOTNET_RUNTIME_RESULT
    OUTPUT_VARIABLE URSA_DOTNET_RUNTIMES
    ERROR_VARIABLE URSA_DOTNET_RUNTIME_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT URSA_DOTNET_SDK_RESULT STREQUAL "0" OR NOT URSA_DOTNET_RUNTIME_RESULT STREQUAL "0")
    message(FATAL_ERROR "Cannot inspect the installed .NET SDK/runtime: ${URSA_DOTNET_SDK_ERROR}${URSA_DOTNET_RUNTIME_ERROR}")
  endif()

  string(REGEX MATCH "(^|\n)10\\.[0-9]+\\.[0-9]+([ \t]|$)" URSA_DOTNET_SDK_10 "${URSA_DOTNET_SDKS}")
  string(REGEX MATCH "Microsoft\\.NETCore\\.App 10\\.[0-9]+\\.[0-9]+" URSA_DOTNET_RUNTIME_10 "${URSA_DOTNET_RUNTIMES}")
  if(NOT URSA_DOTNET_SDK_10 OR NOT URSA_DOTNET_RUNTIME_10)
    message(FATAL_ERROR "URSA_ENABLE_DOTNET requires both a .NET 10 SDK and Microsoft.NETCore.App 10 runtime. Installed SDKs: ${URSA_DOTNET_SDKS}; runtimes: ${URSA_DOTNET_RUNTIMES}")
  endif()

  get_filename_component(URSA_DOTNET_ROOT "${URSA_DOTNET_EXECUTABLE}" DIRECTORY)
  string(REGEX MATCH "10\\.[0-9]+\\.[0-9]+" URSA_DOTNET_HOST_PACK_VERSION "${URSA_DOTNET_RUNTIME_10}")
  set(URSA_DOTNET_HOST_PACK "${URSA_DOTNET_ROOT}/packs/Microsoft.NETCore.App.Host.win-x64/${URSA_DOTNET_HOST_PACK_VERSION}/runtimes/win-x64/native")
  set(URSA_DOTNET_REFERENCE_PACK "${URSA_DOTNET_ROOT}/packs/Microsoft.NETCore.App.Ref/${URSA_DOTNET_HOST_PACK_VERSION}/ref/net10.0")
  foreach(URSA_DOTNET_HOST_FILE IN ITEMS nethost.h hostfxr.h coreclr_delegates.h)
    if(NOT EXISTS "${URSA_DOTNET_HOST_PACK}/${URSA_DOTNET_HOST_FILE}")
      message(FATAL_ERROR "The .NET host pack is incomplete: missing ${URSA_DOTNET_HOST_PACK}/${URSA_DOTNET_HOST_FILE}")
    endif()
  endforeach()
  foreach(URSA_DOTNET_BINARY IN ITEMS nethost.lib nethost.dll)
    if(NOT EXISTS "${URSA_DOTNET_HOST_PACK}/${URSA_DOTNET_BINARY}")
      message(FATAL_ERROR "The .NET host pack is incomplete: missing ${URSA_DOTNET_BINARY}")
    endif()
  endforeach()
  if(NOT EXISTS "${URSA_DOTNET_REFERENCE_PACK}/System.Runtime.dll")
    message(FATAL_ERROR "The .NET reference pack is incomplete: missing ${URSA_DOTNET_REFERENCE_PACK}/System.Runtime.dll")
  endif()

  message(STATUS "Ursa C# scripting enabled with .NET SDK/runtime and Windows x64 host pack ${URSA_DOTNET_HOST_PACK_VERSION}")
endif()

function(ursa_attach_dotnet_host_runtime target)
  if(URSA_ENABLE_DOTNET)
    if(NOT TARGET ${target})
      message(FATAL_ERROR "Cannot configure the .NET native host for missing target ${target}")
    endif()
    get_target_property(nethostDll UrsaDotNetHostSupport URSA_NETHOST_DLL)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${nethostDll}" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endif()
endfunction()

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/CMakeLists.txt" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma/CMakeLists.txt" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/CMakeLists.txt" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/CMakeLists.txt" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eastl/CMakeLists.txt" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eabase/CMakeLists.txt")
  message(FATAL_ERROR "Third-party submodules are missing. Run: git submodule update --init --recursive")
endif()
if(URSA_ENABLE_TRACY)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy/CMakeLists.txt")
    message(FATAL_ERROR "Tracy submodule is missing. Run: git submodule update --init third_party/tracy")
  endif()
  set(TRACY_ENABLE ON CACHE BOOL "Enable profiling" FORCE)
  set(TRACY_ON_DEMAND ON CACHE BOOL "On-demand profiling" FORCE)
  add_subdirectory(third_party/tracy)
endif()
add_subdirectory(third_party/glfw)
add_subdirectory(third_party/vma)
add_subdirectory(third_party/json)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/spdlog)
if(MSVC)
  target_compile_definitions(spdlog PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
  target_compile_options(spdlog PRIVATE "$<$<CONFIG:Debug,RelWithDebInfo>:/Z7>")
endif()
set(FETCHCONTENT_SOURCE_DIR_EABASE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eabase")
# EABase is header-only. Supply its interface target directly rather than
# running its legacy CMake 3.1 project. EASTL reuses this local declaration.
include(FetchContent)
FetchContent_Declare(EABase
  SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_EABASE}"
  SOURCE_SUBDIR include)
add_library(EABase INTERFACE)
target_include_directories(EABase INTERFACE "${FETCHCONTENT_SOURCE_DIR_EABASE}/include/Common")
add_subdirectory(third_party/eastl)
target_compile_definitions(EASTL PUBLIC EASTL_USER_DEFINED_ALLOCATOR=1)

if(URSA_BUILD_EDITOR)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/CMakeLists.txt" OR
     NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_image.h")
    message(FATAL_ERROR "Asset importer submodules are missing. Run: git submodule update --init third_party/assimp third_party/stb")
  endif()
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Assimp as a static library" FORCE)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL_PDB OFF CACHE BOOL "" FORCE)
  set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/assimp)
  if(MSVC)
    # Keep dependency debug symbols in object files; parallel compilation then
    # does not require a shared compiler PDB server. Executable PDBs retain them.
    target_compile_options(assimp PRIVATE "$<$<CONFIG:Debug,RelWithDebInfo>:/Z7>")
    if(TARGET zlibstatic)
      target_compile_options(zlibstatic PRIVATE "$<$<CONFIG:Debug,RelWithDebInfo>:/Z7>")
    endif()
  endif()
  add_library(UrsaStbImage INTERFACE)
  target_include_directories(UrsaStbImage INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
  add_library(Ursa::StbImage ALIAS UrsaStbImage)
  set_target_properties(assimp UrsaStbImage PROPERTIES FOLDER "Dependencies")
endif()

# Keep vendored projects out of the way in the generated solution.
foreach(URSA_DEPENDENCY IN ITEMS EASTL EABase TracyClient glfw spdlog spdlog_header_only update_mappings uninstall)
  if(TARGET ${URSA_DEPENDENCY})
    set_target_properties(${URSA_DEPENDENCY} PROPERTIES FOLDER "Dependencies")
  endif()
endforeach()

# Apply the engine source encoding only to first-party targets and Editor UI.
# Assimp configures its own equivalent /source-charset:utf-8 flag.
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

find_package(Vulkan 1.2 REQUIRED)
find_program(URSA_GLSLC_EXECUTABLE NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin")
find_program(URSA_DXC_EXECUTABLE NAMES dxc HINTS "$ENV{VULKAN_SDK}/Bin")
find_program(URSA_SPIRV_VAL_EXECUTABLE NAMES spirv-val HINTS "$ENV{VULKAN_SDK}/Bin")
find_program(URSA_SPIRV_CROSS_EXECUTABLE NAMES spirv-cross HINTS "$ENV{VULKAN_SDK}/Bin")
foreach(tool IN ITEMS URSA_DXC_EXECUTABLE URSA_SPIRV_VAL_EXECUTABLE URSA_SPIRV_CROSS_EXECUTABLE)
  if(NOT ${tool})
    set(${tool} "")
  endif()
endforeach()

include(Tools/BuildResourcePaths.cmake)
ursa_configure_resource_paths()

if(URSA_ENABLE_DOTNET)
  set(URSA_MANAGED_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Managed/$<CONFIG>")
endif()

add_subdirectory(Runtime)
add_subdirectory(Engine)
if(URSA_ENABLE_DOTNET)
  target_include_directories(UrsaDotNetHost PUBLIC "${PROJECT_SOURCE_DIR}/Engine")
  target_link_libraries(UrsaDotNetHost PUBLIC Ursa::Engine)
endif()

add_executable(UrsaPak Tools/Pak/Main.cpp)
target_link_libraries(UrsaPak PRIVATE Ursa::Engine)
target_compile_definitions(UrsaPak PRIVATE URSA_BUILD_CONFIGURATION="$<CONFIG>")
set_target_properties(UrsaPak PROPERTIES FOLDER "Tools")

if(URSA_BUILD_EDITOR)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp" OR
     NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imguizmo/src/ImGuizmo.cpp")
    message(FATAL_ERROR "Editor submodules are missing. Run: git submodule update --init third_party/imgui third_party/imguizmo")
  endif()
  add_subdirectory(Editor)
  if(URSA_ENABLE_DOTNET)
    target_link_libraries(UrsaEditorCore PRIVATE Ursa::DotNetHost)
    add_dependencies(UrsaEditorCore UrsaManagedScriptCatalog)
    target_compile_definitions(UrsaEditorCore PUBLIC
      URSA_ENABLE_DOTNET=1
      URSA_DOTNET_EXECUTABLE_PATH="${URSA_DOTNET_EXECUTABLE}"
      URSA_DOTNET_RUNTIME_ASSEMBLY_PATH="${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll"
      URSA_DOTNET_EDITOR_ASSEMBLY_PATH="${URSA_MANAGED_OUTPUT_DIR}/Ursa.Editor.dll"
      URSA_DOTNET_CATALOG_GENERATOR_PATH="${CMAKE_BINARY_DIR}/Managed/Tools/Ursa.ScriptCatalogGenerator.dll"
      URSA_DOTNET_REFERENCE_PACK_PATH="${URSA_DOTNET_REFERENCE_PACK}")
    add_executable(UrsaScriptCompile Tools/ScriptCompile/Main.cpp)
    target_link_libraries(UrsaScriptCompile PRIVATE Ursa::Editor)
    add_dependencies(UrsaScriptCompile UrsaManagedHostAssemblies UrsaManagedScriptCatalog)
    set_target_properties(UrsaScriptCompile PROPERTIES FOLDER "Tools")
    add_executable(UrsaScriptPlayer Tools/ScriptPlayer/Main.cpp)
    target_link_libraries(UrsaScriptPlayer PRIVATE Ursa::DotNetHost)
    target_compile_definitions(UrsaScriptPlayer PRIVATE
      URSA_SCRIPT_PLAYER_ENGINE_ASSETS="${PROJECT_SOURCE_DIR}/Assets")
    ursa_attach_dotnet_host_runtime(UrsaScriptPlayer)
    set_target_properties(UrsaScriptPlayer PROPERTIES FOLDER "Tools")
    if(NOT URSA_PROJECT_DIR STREQUAL "")
      set_target_properties(UrsaScriptCompile PROPERTIES
        VS_DEBUGGER_COMMAND_ARGUMENTS "\"${URSA_PROJECT_DIR}\" $<CONFIG>"
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
      set_target_properties(UrsaScriptPlayer PROPERTIES
        VS_DEBUGGER_COMMAND_ARGUMENTS "--project \"${URSA_PROJECT_DIR}\" --scene Snake/Snake.ursascene"
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endif()
  endif()
  target_link_libraries(UrsaPak PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaPak PRIVATE URSA_HAS_EDITOR_ASSET_PIPELINE=1)
  add_executable(UrsaAssetMigrate Tools/AssetMigrate/Main.cpp)
  target_link_libraries(UrsaAssetMigrate PRIVATE Ursa::Editor)
  set_target_properties(UrsaAssetMigrate PROPERTIES FOLDER "Tools")
  if(TARGET UrsaEnginePackage)
    add_dependencies(UrsaEnginePackage UrsaEditorCore)
  endif()
endif()

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(URSA_BUILD_TESTS_DEFAULT ON)
else()
  set(URSA_BUILD_TESTS_DEFAULT OFF)
endif()
option(URSA_BUILD_TESTS "Build the engine GPU tests" ${URSA_BUILD_TESTS_DEFAULT})

if(URSA_BUILD_TESTS)
  set(URSA_TEST_SAMPLE_PROJECT "${CMAKE_BINARY_DIR}/TestProjects/Sample/$<CONFIG>")
  enable_testing()
  add_executable(UrsaProfilerCompileTests Tests/Core/ProfilerCompileTests.cpp)
  target_link_libraries(UrsaProfilerCompileTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaProfilerCompileTests COMMAND UrsaProfilerCompileTests)
  add_test(NAME UrsaBuildResourcePathsTests COMMAND "${CMAKE_COMMAND}"
    "-DENGINE_ROOT=${PROJECT_SOURCE_DIR}" "-DSCRATCH=${CMAKE_BINARY_DIR}/Temp/ResourcePaths"
    -P "${PROJECT_SOURCE_DIR}/Tests/Resources/BuildResourcePathsTests.cmake")
  add_executable(UrsaBuildResourceManifestTests Tests/Resources/BuildResourceManifestTests.cpp)
  target_link_libraries(UrsaBuildResourceManifestTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaBuildResourceManifestTests COMMAND UrsaBuildResourceManifestTests)
  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures")
  add_executable(UrsaResourceVfsTests Tests/Serialization/ResourceVfsTests.cpp)
  target_link_libraries(UrsaResourceVfsTests PRIVATE Ursa::Engine)
  target_compile_definitions(UrsaResourceVfsTests PRIVATE URSA_TEST_ENGINE_ASSET_ROOT="${PROJECT_SOURCE_DIR}/Assets")
  add_test(NAME UrsaResourceVfsTests COMMAND UrsaResourceVfsTests)
  add_executable(UrsaSerializableHeaderSmoke Tests/Serialization/SerializableHeaderSmoke.cpp)
  target_link_libraries(UrsaSerializableHeaderSmoke PRIVATE Ursa::Engine)
  add_test(NAME UrsaSerializableHeaderSmoke COMMAND UrsaSerializableHeaderSmoke)
  add_executable(UrsaManagedHandleTests Tests/Scripting/ManagedHandleTests.cpp)
  target_link_libraries(UrsaManagedHandleTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaManagedHandleTests COMMAND UrsaManagedHandleTests)
  add_executable(UrsaManagedSceneApiTests Tests/Scripting/ManagedSceneApiTests.cpp)
  target_link_libraries(UrsaManagedSceneApiTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaManagedSceneApiTests COMMAND UrsaManagedSceneApiTests)
  add_executable(UrsaManagedAssetApiTests Tests/Scripting/ManagedAssetApiTests.cpp)
  target_link_libraries(UrsaManagedAssetApiTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaManagedAssetApiTests COMMAND UrsaManagedAssetApiTests)
  add_executable(UrsaManagedBehaviourRuntimeTests Tests/Scripting/ManagedBehaviourRuntimeTests.cpp)
  target_link_libraries(UrsaManagedBehaviourRuntimeTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaManagedBehaviourRuntimeTests COMMAND UrsaManagedBehaviourRuntimeTests)

  if(URSA_BUILD_EDITOR)
    add_executable(UrsaSerializedObjectTests Tests/Editor/SerializedObjectTests.cpp)
    target_link_libraries(UrsaSerializedObjectTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaSerializedObjectTests COMMAND UrsaSerializedObjectTests)

    add_executable(UrsaImGuiBridgeTests Tests/Editor/ImGuiBridgeTests.cpp)
    target_link_libraries(UrsaImGuiBridgeTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaImGuiBridgeTests COMMAND UrsaImGuiBridgeTests)

    add_executable(UrsaEditorCompositeWindowTests Tests/Editor/EditorCompositeWindowTests.cpp)
    target_link_libraries(UrsaEditorCompositeWindowTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaEditorCompositeWindowTests COMMAND UrsaEditorCompositeWindowTests)

    add_executable(UrsaEditorWorkspaceWindowTests Tests/Editor/EditorWorkspaceWindowTests.cpp)
    target_link_libraries(UrsaEditorWorkspaceWindowTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaEditorWorkspaceWindowTests COMMAND UrsaEditorWorkspaceWindowTests)

    add_executable(UrsaEditorPreviewTests Tests/Editor/EditorPreviewTests.cpp)
    target_link_libraries(UrsaEditorPreviewTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaEditorPreviewTests COMMAND UrsaEditorPreviewTests)

    add_executable(UrsaEditorHierarchyTests Tests/Editor/EditorHierarchyTests.cpp)
    target_link_libraries(UrsaEditorHierarchyTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaEditorHierarchyTests COMMAND UrsaEditorHierarchyTests)

    add_executable(UrsaEditorGizmoTests Tests/Editor/EditorGizmoTests.cpp)
    target_link_libraries(UrsaEditorGizmoTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaEditorGizmoTests COMMAND UrsaEditorGizmoTests)

    add_executable(UrsaProjectWorkspaceTests Tests/Editor/ProjectWorkspaceTests.cpp)
    target_link_libraries(UrsaProjectWorkspaceTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaProjectWorkspaceTests COMMAND UrsaProjectWorkspaceTests)

    add_executable(UrsaEditorDocumentsTests Tests/Editor/EditorDocumentsTests.cpp)
    target_link_libraries(UrsaEditorDocumentsTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaEditorDocumentsTests COMMAND UrsaEditorDocumentsTests)

    add_executable(UrsaAssetDatabaseTests Tests/Editor/AssetDatabaseTests.cpp)
    add_executable(UrsaProjectAssetDatabaseTests Tests/Editor/ProjectAssetDatabaseTests.cpp)
    add_executable(UrsaAssetRefreshWindowTests Tests/Editor/AssetRefreshWindowTests.cpp)
    target_link_libraries(UrsaAssetRefreshWindowTests PRIVATE Ursa::Editor)
    ursa_attach_dotnet_host_runtime(UrsaAssetRefreshWindowTests)
    add_test(NAME UrsaAssetRefreshWindowTests COMMAND UrsaAssetRefreshWindowTests)
    add_executable(UrsaAssetImportPipelineTests Tests/Editor/AssetImportPipelineTests.cpp)
    target_link_libraries(UrsaAssetImportPipelineTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaAssetImportPipelineTests COMMAND UrsaAssetImportPipelineTests)
    add_executable(UrsaProjectShaderImportTests Tests/Editor/ProjectShaderImportTests.cpp)
    target_link_libraries(UrsaProjectShaderImportTests PRIVATE Ursa::Editor)
    target_compile_definitions(UrsaProjectShaderImportTests PRIVATE URSA_TEST_ENGINE_SOURCE="${PROJECT_SOURCE_DIR}/Assets")
    add_test(NAME UrsaProjectShaderImportTests COMMAND UrsaProjectShaderImportTests)
    target_link_libraries(UrsaProjectAssetDatabaseTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaProjectAssetDatabaseTests COMMAND UrsaProjectAssetDatabaseTests)
    target_link_libraries(UrsaAssetDatabaseTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaAssetDatabaseTests COMMAND UrsaAssetDatabaseTests)
    set_tests_properties(UrsaAssetDatabaseTests PROPERTIES
      ENVIRONMENT "TEMP=${CMAKE_BINARY_DIR}/temp;TMP=${CMAKE_BINARY_DIR}/temp")

    add_executable(UrsaProjectBrowserModelTests Tests/Editor/ProjectBrowserModelTests.cpp)
    target_link_libraries(UrsaProjectBrowserModelTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaProjectBrowserModelTests COMMAND UrsaProjectBrowserModelTests)
    if(URSA_ENABLE_DOTNET)
      add_executable(UrsaManagedEditorWorkflowTests Tests/Editor/ManagedEditorWorkflowTests.cpp)
      target_link_libraries(UrsaManagedEditorWorkflowTests PRIVATE Ursa::Editor)
      ursa_attach_dotnet_host_runtime(UrsaManagedEditorWorkflowTests)
      add_test(NAME UrsaManagedEditorWorkflowTests COMMAND UrsaManagedEditorWorkflowTests)
      set_tests_properties(UrsaManagedEditorWorkflowTests PROPERTIES TIMEOUT 120
        ENVIRONMENT "TEMP=${CMAKE_BINARY_DIR}/temp;TMP=${CMAKE_BINARY_DIR}/temp")
      add_executable(UrsaScriptBuildTests Tests/Editor/ScriptBuildTests.cpp)
      target_link_libraries(UrsaScriptBuildTests PRIVATE Ursa::Editor)
      add_dependencies(UrsaScriptBuildTests UrsaManagedHostAssemblies UrsaManagedScriptCatalog)
      add_test(NAME UrsaScriptBuildTests COMMAND UrsaScriptBuildTests
        "${URSA_DOTNET_EXECUTABLE}"
        "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll"
        "${URSA_MANAGED_OUTPUT_DIR}/Ursa.Editor.dll"
        "${CMAKE_BINARY_DIR}/Managed/Tools/Ursa.ScriptCatalogGenerator.dll"
        "${URSA_DOTNET_REFERENCE_PACK}")
      set_tests_properties(UrsaScriptBuildTests PROPERTIES
        ENVIRONMENT "TEMP=${CMAKE_BINARY_DIR}/temp;TMP=${CMAKE_BINARY_DIR}/temp")
    endif()
    add_executable(UrsaAssetMigrationTests Tests/Editor/AssetMigrationTests.cpp)
    target_link_libraries(UrsaAssetMigrationTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaAssetMigrationTests COMMAND UrsaAssetMigrationTests)

    add_executable(UrsaTextureImporterTests Tests/Editor/TextureImporterTests.cpp)
    target_link_libraries(UrsaTextureImporterTests PRIVATE Ursa::Editor Ursa::StbImage)
    add_test(NAME UrsaTextureImporterTests COMMAND UrsaTextureImporterTests)

    add_executable(UrsaEditorShaderImportTests Tests/Editor/ShaderImportViewTests.cpp)
    target_link_libraries(UrsaEditorShaderImportTests PRIVATE Ursa::Editor)
    target_compile_definitions(UrsaEditorShaderImportTests PRIVATE
      URSA_DXC_EXECUTABLE="${URSA_DXC_EXECUTABLE}"
      URSA_SPIRV_VAL_EXECUTABLE="${URSA_SPIRV_VAL_EXECUTABLE}"
      URSA_SPIRV_CROSS_EXECUTABLE="${URSA_SPIRV_CROSS_EXECUTABLE}")
    add_test(NAME UrsaEditorShaderImportTests COMMAND UrsaEditorShaderImportTests)

    add_executable(UrsaEditorMaterialInspectorTests Tests/Editor/MaterialInspectorsTests.cpp)
    target_link_libraries(UrsaEditorMaterialInspectorTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaEditorMaterialInspectorTests COMMAND UrsaEditorMaterialInspectorTests)

    add_executable(UrsaMaterialInspectorHeaderSmoke Tests/Editor/MaterialInspectorHeaderSmoke.cpp)
    target_link_libraries(UrsaMaterialInspectorHeaderSmoke PRIVATE Ursa::Editor)
    add_test(NAME UrsaMaterialInspectorHeaderSmoke COMMAND UrsaMaterialInspectorHeaderSmoke)

    add_executable(UrsaComponentEditorsTests Tests/Editor/ComponentEditorsTests.cpp)
    target_link_libraries(UrsaComponentEditorsTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaComponentEditorsTests COMMAND UrsaComponentEditorsTests)

    add_executable(UrsaTextureDraftTests Tests/Editor/TextureDraftTests.cpp)
    target_link_libraries(UrsaTextureDraftTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaTextureDraftTests COMMAND UrsaTextureDraftTests)

    add_executable(UrsaMaterialDraftTests Tests/Editor/MaterialDraftTests.cpp)
    target_link_libraries(UrsaMaterialDraftTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaMaterialDraftTests COMMAND UrsaMaterialDraftTests)

    add_executable(UrsaSceneDraftTests Tests/Editor/SceneDraftTests.cpp)
    target_link_libraries(UrsaSceneDraftTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaSceneDraftTests COMMAND UrsaSceneDraftTests)

    add_executable(UrsaPlaySessionTests Tests/Editor/PlaySessionTests.cpp)
    target_link_libraries(UrsaPlaySessionTests PRIVATE Ursa::Editor)
    add_test(NAME UrsaPlaySessionTests COMMAND UrsaPlaySessionTests)
  endif()

  add_executable(UrsaEastlAllocatorTests Tests/Core/EastlAllocatorTests.cpp)
  target_link_libraries(UrsaEastlAllocatorTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaEastlAllocatorTests COMMAND UrsaEastlAllocatorTests)

  add_executable(UrsaMemoryAllocatorTests Tests/Core/MemoryAllocatorTests.cpp)
  target_link_libraries(UrsaMemoryAllocatorTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaMemoryAllocatorTests COMMAND UrsaMemoryAllocatorTests)

  add_executable(UrsaTemporaryAllocatorTests Tests/Core/TemporaryAllocatorTests.cpp)
  target_link_libraries(UrsaTemporaryAllocatorTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaTemporaryAllocatorTests COMMAND UrsaTemporaryAllocatorTests)

  add_executable(UrsaMemoryFactoryTests Tests/Core/MemoryFactoryTests.cpp)
  target_link_libraries(UrsaMemoryFactoryTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaMemoryFactoryTests COMMAND UrsaMemoryFactoryTests)

  add_executable(UrsaMemoryFrameTests Tests/Application/MemoryFrameTests.cpp)
  target_link_libraries(UrsaMemoryFrameTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaMemoryFrameTests COMMAND UrsaMemoryFrameTests)

  add_executable(UrsaMemoryBenchmarks Tests/Core/MemoryBenchmarks.cpp)
  target_link_libraries(UrsaMemoryBenchmarks PRIVATE Ursa::Runtime)
  set_target_properties(UrsaMemoryBenchmarks PROPERTIES FOLDER "Tests/Benchmarks")

  add_executable(UrsaMemoryWorkloadBenchmarks Tests/Application/MemoryWorkloadBenchmarks.cpp)
  target_link_libraries(UrsaMemoryWorkloadBenchmarks PRIVATE Ursa::Engine)
  set_target_properties(UrsaMemoryWorkloadBenchmarks PROPERTIES FOLDER "Tests/Benchmarks")

  add_executable(UrsaLogTests Tests/Core/LogTests.cpp)
  target_link_libraries(UrsaLogTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaLogTests COMMAND UrsaLogTests)
  add_test(NAME UrsaLogConsoleSmoke COMMAND UrsaLogTests)
  set_tests_properties(UrsaLogConsoleSmoke PROPERTIES PASS_REGULAR_EXPRESSION "\\[RHI\\].*")

  add_executable(UrsaLogHeaderSmoke Tests/Core/LogHeaderSmoke.cpp)
  target_link_libraries(UrsaLogHeaderSmoke PRIVATE Ursa::Runtime)
  add_test(NAME UrsaLogHeaderSmoke COMMAND UrsaLogHeaderSmoke)

  add_executable(UrsaContainerHeaderSmoke Tests/Core/ContainerHeaderSmoke.cpp)
  target_link_libraries(UrsaContainerHeaderSmoke PRIVATE Ursa::Runtime)
  add_test(NAME UrsaContainerHeaderSmoke COMMAND UrsaContainerHeaderSmoke)

  add_executable(UrsaStringInteropTests Tests/Core/StringInteropTests.cpp)
  target_link_libraries(UrsaStringInteropTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaStringInteropTests COMMAND UrsaStringInteropTests)

  add_executable(UrsaVirtualFileSystemTests Tests/Vfs/VirtualFileSystemTests.cpp)
  add_executable(UrsaRuntimeContentTests Tests/Vfs/RuntimeContentTests.cpp)
  target_link_libraries(UrsaRuntimeContentTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaRuntimeContentTests COMMAND UrsaRuntimeContentTests)
  target_link_libraries(UrsaVirtualFileSystemTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaVirtualFileSystemTests COMMAND UrsaVirtualFileSystemTests)

  add_executable(UrsaPakTests Tests/Vfs/PakTests.cpp)
  target_link_libraries(UrsaPakTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaPakTests COMMAND UrsaPakTests)

  add_executable(UrsaRuntimeHeaderSmoke Tests/Core/RuntimeHeaderSmoke.cpp)
  target_link_libraries(UrsaRuntimeHeaderSmoke PRIVATE Ursa::Runtime)
  add_test(NAME UrsaRuntimeHeaderSmoke COMMAND UrsaRuntimeHeaderSmoke)

  add_executable(UrsaWindowInputTests Tests/Window/WindowInputTests.cpp)
  target_link_libraries(UrsaWindowInputTests PRIVATE Ursa::Runtime)
  add_test(NAME UrsaWindowInputTests COMMAND UrsaWindowInputTests)

  add_executable(UrsaMathTests Tests/Math/MathTests.cpp)
  target_link_libraries(UrsaMathTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaMathTests COMMAND UrsaMathTests)

  add_executable(UrsaMaterialTests Tests/Material/MaterialTests.cpp)
  target_link_libraries(UrsaMaterialTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaMaterialTests COMMAND UrsaMaterialTests)

  add_executable(UrsaShaderMaterialTests Tests/Material/ShaderMaterialTests.cpp)
  target_link_libraries(UrsaShaderMaterialTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaShaderMaterialTests COMMAND UrsaShaderMaterialTests)

  add_executable(UrsaMaterialMigrationTests Tests/Material/MaterialMigrationTests.cpp)
  target_link_libraries(UrsaMaterialMigrationTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaMaterialMigrationTests COMMAND UrsaMaterialMigrationTests)

  add_executable(UrsaShaderSourceTests Tests/Shader/ShaderSourceTests.cpp)
  target_link_libraries(UrsaShaderSourceTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaShaderSourceTests COMMAND UrsaShaderSourceTests)

  add_executable(UrsaShaderVariantTests Tests/Shader/ShaderVariantTests.cpp)
  target_link_libraries(UrsaShaderVariantTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaShaderVariantTests COMMAND UrsaShaderVariantTests)

  add_executable(UrsaShaderSourceLibraryTests Tests/Shader/ShaderSourceLibraryTests.cpp)
  target_link_libraries(UrsaShaderSourceLibraryTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaShaderSourceLibraryTests COMMAND UrsaShaderSourceLibraryTests)

  add_executable(UrsaShaderCompilerTests Tests/Shader/ShaderCompilerTests.cpp)
  target_link_libraries(UrsaShaderCompilerTests PRIVATE Ursa::Engine)
  target_compile_definitions(UrsaShaderCompilerTests PRIVATE
    URSA_TEST_DXC_EXECUTABLE="${URSA_DXC_EXECUTABLE}"
    URSA_TEST_SPIRV_VAL_EXECUTABLE="${URSA_SPIRV_VAL_EXECUTABLE}"
    URSA_TEST_SPIRV_CROSS_EXECUTABLE="${URSA_SPIRV_CROSS_EXECUTABLE}")
  add_test(NAME UrsaShaderCompilerTests COMMAND UrsaShaderCompilerTests)

  add_executable(UrsaShaderCompileCacheTests Tests/Shader/ShaderCompileCacheTests.cpp)
  target_link_libraries(UrsaShaderCompileCacheTests PRIVATE Ursa::Engine)
  target_compile_definitions(UrsaShaderCompileCacheTests PRIVATE
    URSA_TEST_DXC_EXECUTABLE="${URSA_DXC_EXECUTABLE}"
    URSA_TEST_SPIRV_VAL_EXECUTABLE="${URSA_SPIRV_VAL_EXECUTABLE}"
    URSA_TEST_SPIRV_CROSS_EXECUTABLE="${URSA_SPIRV_CROSS_EXECUTABLE}")
  add_test(NAME UrsaShaderCompileCacheTests COMMAND UrsaShaderCompileCacheTests)

  add_executable(UrsaShaderExampleImportTests Tests/Shader/ShaderExampleImportTests.cpp)
  target_link_libraries(UrsaShaderExampleImportTests PRIVATE Ursa::Engine)
  target_compile_definitions(UrsaShaderExampleImportTests PRIVATE
    URSA_ENGINE_SHADER_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/Assets"
    URSA_TEST_DXC_EXECUTABLE="${URSA_DXC_EXECUTABLE}"
    URSA_TEST_SPIRV_VAL_EXECUTABLE="${URSA_SPIRV_VAL_EXECUTABLE}"
    URSA_TEST_SPIRV_CROSS_EXECUTABLE="${URSA_SPIRV_CROSS_EXECUTABLE}")
  add_test(NAME UrsaShaderExampleImportTests COMMAND UrsaShaderExampleImportTests)

  add_executable(UrsaShaderAssetTests Tests/Shader/ShaderAssetTests.cpp)
  target_link_libraries(UrsaShaderAssetTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaShaderAssetTests COMMAND UrsaShaderAssetTests)

  set(URSA_SHADER_TEST_PAK "${CMAKE_CURRENT_BINARY_DIR}/Package/$<CONFIG>/ShaderContent.pak")
  add_test(NAME UrsaShaderPackageBuild
    COMMAND ${CMAKE_COMMAND}
      "-DURSA_PAK_EXECUTABLE=$<TARGET_FILE:UrsaPak>"
      "-DURSA_ENGINE_ASSET_SOURCE=${PROJECT_SOURCE_DIR}/Assets"
      "-DURSA_TEST_PROJECT=${CMAKE_BINARY_DIR}/TestProjects/Runtime/$<CONFIG>"
      "-DURSA_CONFIGURATION=$<CONFIG>"
      "-DURSA_SHADER_PAK_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/Package/$<CONFIG>"
      "-DURSA_SHADER_PAK_PATH=${URSA_SHADER_TEST_PAK}"
      -P "${PROJECT_SOURCE_DIR}/Tools/Pak/BuildShaderTestPak.cmake")
  set_tests_properties(UrsaShaderPackageBuild PROPERTIES FIXTURES_SETUP UrsaShaderContentPak)

  add_executable(UrsaShaderPackageTests Tests/Shader/ShaderPackageTests.cpp)
  target_link_libraries(UrsaShaderPackageTests PRIVATE Ursa::Engine)
  target_compile_definitions(UrsaShaderPackageTests PRIVATE URSA_ENGINE_SHADER_PAK="${URSA_SHADER_TEST_PAK}")
  add_dependencies(UrsaShaderPackageTests UrsaPak)
  add_test(NAME UrsaShaderPackageTests COMMAND UrsaShaderPackageTests)
  set_tests_properties(UrsaShaderPackageTests PROPERTIES FIXTURES_REQUIRED UrsaShaderContentPak)

  add_executable(UrsaShaderReflectionTests Tests/Shader/ShaderReflectionTests.cpp)
  target_link_libraries(UrsaShaderReflectionTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaShaderReflectionTests COMMAND UrsaShaderReflectionTests)

  add_executable(UrsaSerializationBaseTests Tests/Serialization/SerializationBaseTests.cpp Tests/Fixtures/AtomicFile.cpp)
  target_link_libraries(UrsaSerializationBaseTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaSerializationBaseTests COMMAND UrsaSerializationBaseTests)

  add_executable(UrsaArchiveTests Tests/Serialization/ArchiveTests.cpp)
  target_link_libraries(UrsaArchiveTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaArchiveTests COMMAND UrsaArchiveTests)

  add_executable(UrsaAssetPersistenceTests Tests/Serialization/AssetPersistenceTests.cpp)
  target_link_libraries(UrsaAssetPersistenceTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaAssetPersistenceTests COMMAND UrsaAssetPersistenceTests)

  add_executable(UrsaScenePersistenceTests Tests/Serialization/ScenePersistenceTests.cpp)
  target_link_libraries(UrsaScenePersistenceTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaScenePersistenceTests COMMAND UrsaScenePersistenceTests)

  add_executable(UrsaCustomComponentTests Tests/Serialization/CustomComponentTests.cpp)
  target_link_libraries(UrsaCustomComponentTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaCustomComponentTests COMMAND UrsaCustomComponentTests)

  add_executable(UrsaPrefabTests Tests/Serialization/PrefabTests.cpp)
  target_link_libraries(UrsaPrefabTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaPrefabTests COMMAND UrsaPrefabTests)

  add_executable(UrsaMaterialHeaderSmoke Tests/Material/MaterialHeaderSmoke.cpp)
  target_link_libraries(UrsaMaterialHeaderSmoke PRIVATE Ursa::Engine)
  add_test(NAME UrsaMaterialHeaderSmoke COMMAND UrsaMaterialHeaderSmoke)

  add_executable(UrsaSceneGraphTests Tests/Scene/SceneTests.cpp)
  target_link_libraries(UrsaSceneGraphTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaSceneGraphTests COMMAND UrsaSceneGraphTests)

  add_executable(UrsaGameObjectTests Tests/Scene/GameObjectTests.cpp)
  target_link_libraries(UrsaGameObjectTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaGameObjectTests COMMAND UrsaGameObjectTests)

  add_executable(UrsaLightingTests Tests/Scene/LightingTests.cpp)
  target_link_libraries(UrsaLightingTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaLightingTests COMMAND UrsaLightingTests)

  add_executable(UrsaLightingHeaderSmoke Tests/Scene/LightingHeaderSmoke.cpp)
  target_link_libraries(UrsaLightingHeaderSmoke PRIVATE Ursa::Engine)
  add_test(NAME UrsaLightingHeaderSmoke COMMAND UrsaLightingHeaderSmoke)

  add_executable(UrsaFrameSchedulerTests Tests/Application/FrameSchedulerTests.cpp)
  target_link_libraries(UrsaFrameSchedulerTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaFrameSchedulerTests COMMAND UrsaFrameSchedulerTests)

  add_executable(UrsaApplicationSceneTests Tests/Application/ApplicationSceneTests.cpp)
  target_link_libraries(UrsaApplicationSceneTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaApplicationSceneTests COMMAND UrsaApplicationSceneTests)

  add_executable(UrsaRendererTests Tests/Renderer3D/RendererTests.cpp)
  target_link_libraries(UrsaRendererTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaRendererTests COMMAND UrsaRendererTests)

  add_executable(UrsaMaterialRendererTests Tests/Renderer3D/MaterialRendererTests.cpp)
  target_link_libraries(UrsaMaterialRendererTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaMaterialRendererTests COMMAND UrsaMaterialRendererTests)

  add_executable(UrsaMaterialSceneSmoke Tests/Renderer3D/MaterialSceneSmoke.cpp)
  target_link_libraries(UrsaMaterialSceneSmoke PRIVATE Ursa::Engine)
  add_test(NAME UrsaMaterialSceneSmoke COMMAND UrsaMaterialSceneSmoke)

  add_executable(UrsaRendererWindowTests Tests/Renderer3D/RendererWindowTests.cpp)
  target_link_libraries(UrsaRendererWindowTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaRendererWindowTests COMMAND UrsaRendererWindowTests)

  function(ursa_test_shader target source)
    target_sources(${target} PRIVATE "${source}")
    set_source_files_properties("${source}" PROPERTIES HEADER_FILE_ONLY TRUE)
  endfunction()

  add_executable(UrsaRhiTests Tests/Rhi/RhiTests.cpp)
  target_link_libraries(UrsaRhiTests PRIVATE Ursa::Runtime)
  target_compile_definitions(UrsaRhiTests PRIVATE URSA_SHADER_DIR="${CMAKE_BINARY_DIR}/TestProjects/Runtime/$<CONFIG>/Library/TestShaders")
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/Test.comp)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestImage.comp)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestVertex.vert)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/Test.frag)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestInstance.vert)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestInstance.frag)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestOffset.vert)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestOffset.frag)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/State.vert)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/State.frag)
  add_test(NAME UrsaRhiTests COMMAND UrsaRhiTests)

  add_executable(UrsaVulkanValidationLogTests Tests/Rhi/VulkanValidationLogTests.cpp)
  target_link_libraries(UrsaVulkanValidationLogTests PRIVATE Ursa::Runtime Vulkan::Vulkan)
  add_test(NAME UrsaVulkanValidationLogTests COMMAND UrsaVulkanValidationLogTests)
  set_tests_properties(UrsaVulkanValidationLogTests PROPERTIES PASS_REGULAR_EXPRESSION "fallback before initialization")
  add_test(NAME UrsaVulkanValidationShutdownSmoke COMMAND UrsaVulkanValidationLogTests)
  set_tests_properties(UrsaVulkanValidationShutdownSmoke PROPERTIES PASS_REGULAR_EXPRESSION "fallback after shutdown")

  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    Tests/Math/MathTests.cpp
    Tests/Vfs/VirtualFileSystemTests.cpp
    Tests/Material/MaterialTests.cpp
    Tests/Window/WindowInputTests.cpp
    Tests/Serialization/SerializationBaseTests.cpp
    Tests/Serialization/AssetPersistenceTests.cpp
    Tests/Serialization/ScenePersistenceTests.cpp
    Tests/Serialization/CustomComponentTests.cpp
    Tests/Serialization/PrefabTests.cpp
    Tests/Material/MaterialHeaderSmoke.cpp
    Tests/Scene/SceneTests.cpp
    Tests/Scene/LightingTests.cpp
    Tests/Scene/LightingHeaderSmoke.cpp
    Tests/Application/FrameSchedulerTests.cpp
    Tests/Application/ApplicationSceneTests.cpp
    Tests/Renderer3D/RendererTests.cpp
    Tests/Renderer3D/MaterialRendererTests.cpp
    Tests/Renderer3D/MaterialSceneSmoke.cpp
    Tests/Renderer3D/RendererWindowTests.cpp
    Tests/Rhi/RhiTests.cpp)

  # Group the engine test executables under one solution folder.
  get_property(URSA_TEST_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(URSA_TEST_TARGET IN LISTS URSA_TEST_TARGETS)
    if(URSA_TEST_TARGET MATCHES "Tests$" OR URSA_TEST_TARGET MATCHES "Smoke$")
      set_target_properties(${URSA_TEST_TARGET} PROPERTIES FOLDER "Tests/Engine")
    endif()
  endforeach()

  if(URSA_ENABLE_DOTNET)
    add_executable(UrsaDotNetHostTests Tests/Scripting/DotNetHostTests.cpp)
    target_link_libraries(UrsaDotNetHostTests PRIVATE Ursa::DotNetHost)
    if(TARGET Ursa::Editor)
      target_link_libraries(UrsaDotNetHostTests PRIVATE Ursa::Editor)
      target_compile_definitions(UrsaDotNetHostTests PRIVATE URSA_DOTNET_TEST_EDITOR=1)
    endif()
    add_dependencies(UrsaDotNetHostTests UrsaManagedHostAssemblies)
    add_test(NAME UrsaDotNetHostSmoke
      COMMAND UrsaDotNetHostTests
	        "${URSA_MANAGED_OUTPUT_DIR}"
	        "${CMAKE_BINARY_DIR}/temp/DotNetHostSmoke")
    ursa_attach_dotnet_host_runtime(UrsaDotNetHostTests)
    set_target_properties(UrsaDotNetHostTests PROPERTIES FOLDER "Tests/Scripting")
    add_test(NAME UrsaManagedProtocolTests COMMAND "${URSA_DOTNET_EXECUTABLE}" run --project "${CMAKE_BINARY_DIR}/runtime/Scripting/Managed/Source/Ursa.ManagedHost.ProtocolTests/Ursa.ManagedHost.ProtocolTests.csproj" --configuration "$<CONFIG>" --no-build --no-restore)
    if(NOT URSA_PROJECT_DIR STREQUAL "")
      add_test(NAME UrsaSnakeManagedLogicTests COMMAND "${URSA_DOTNET_EXECUTABLE}" run
        --project "${PROJECT_SOURCE_DIR}/Managed/Ursa.SnakeLogicTests/Ursa.SnakeLogicTests.csproj"
        --configuration "$<CONFIG>" "-p:UrsaProjectDir=${URSA_TEST_SAMPLE_PROJECT}"
        "-p:BaseOutputPath=${CMAKE_BINARY_DIR}/ManagedTests/Snake/bin/"
        "-p:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/ManagedTests/Snake/obj/")
      add_test(NAME UrsaProjectScriptCompile COMMAND UrsaAssetImport --project "${URSA_TEST_SAMPLE_PROJECT}" --configuration "$<CONFIG>")
      set_tests_properties(UrsaProjectScriptCompile PROPERTIES FIXTURES_SETUP UrsaProjectScripts)
      foreach(URSA_PROJECT_SCENE IN ITEMS Snake/Snake.ursascene Snake/SnakeSmoke.ursascene)
        if(URSA_PROJECT_SCENE MATCHES "Smoke")
          set(URSA_PROJECT_SMOKE_NAME UrsaProjectSnakeGameplaySmoke)
        else()
          set(URSA_PROJECT_SMOKE_NAME UrsaProjectSnakePlayerSmoke)
        endif()
        add_test(NAME ${URSA_PROJECT_SMOKE_NAME} COMMAND UrsaScriptPlayer
          --project "${URSA_TEST_SAMPLE_PROJECT}" --scene "${URSA_PROJECT_SCENE}" --hidden --frames 20 --stress-window)
        set_tests_properties(${URSA_PROJECT_SMOKE_NAME} PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
          ENVIRONMENT "URSA_USER_DATA_ROOT=${CMAKE_BINARY_DIR}/temp/ProjectUser")
      endforeach()
      add_test(NAME UrsaProjectMaterialPlayerSmoke COMMAND UrsaScriptPlayer
        --project "${URSA_TEST_SAMPLE_PROJECT}" --scene "MaterialShowcase.ursascene" --hidden --frames 8)
      set_tests_properties(UrsaProjectMaterialPlayerSmoke PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
        ENVIRONMENT "URSA_USER_DATA_ROOT=${CMAKE_BINARY_DIR}/temp/ProjectUser")
      add_test(NAME UrsaProjectMaterialEditorSmoke COMMAND UrsaEditor --project "${URSA_TEST_SAMPLE_PROJECT}"
        --scene "MaterialShowcase.ursascene" --hidden --frames 5)
      set_tests_properties(UrsaProjectMaterialEditorSmoke PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
        ENVIRONMENT "URSA_USER_DATA_ROOT=${CMAKE_BINARY_DIR}/temp/EditorProjectUser")
      add_test(NAME UrsaProjectEditorOpenSmoke COMMAND UrsaEditor --project "${URSA_TEST_SAMPLE_PROJECT}"
        --scene "Snake/Snake.ursascene" --hidden --frames 5)
      set_tests_properties(UrsaProjectEditorOpenSmoke PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
        ENVIRONMENT "URSA_USER_DATA_ROOT=${CMAKE_BINARY_DIR}/temp/EditorProjectUser")
      add_executable(UrsaProjectScenePrefabRoundTripTests Tests/Scripting/ProjectScenePrefabRoundTripTests.cpp)
      target_link_libraries(UrsaProjectScenePrefabRoundTripTests PRIVATE Ursa::Engine)
      target_compile_definitions(UrsaProjectScenePrefabRoundTripTests PRIVATE
        URSA_PROJECT_ASSETS_DIR="${URSA_TEST_SAMPLE_PROJECT}/Assets")
      add_test(NAME UrsaProjectScenePrefabRoundTripTests COMMAND UrsaProjectScenePrefabRoundTripTests)
      if(TARGET Ursa::Editor)
        add_executable(UrsaProjectPlaySessionTests Tests/Scripting/ProjectPlaySessionTests.cpp)
        target_link_libraries(UrsaProjectPlaySessionTests PRIVATE Ursa::Editor Ursa::DotNetHost)
        target_compile_definitions(UrsaProjectPlaySessionTests PRIVATE URSA_PROJECT_DIR="${URSA_TEST_SAMPLE_PROJECT}")
        add_dependencies(UrsaProjectPlaySessionTests UrsaManagedHostAssemblies)
        ursa_attach_dotnet_host_runtime(UrsaProjectPlaySessionTests)
        add_test(NAME UrsaProjectPlaySessionTests COMMAND UrsaProjectPlaySessionTests)
        set_tests_properties(UrsaProjectPlaySessionTests PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
          ENVIRONMENT "URSA_USER_DATA_ROOT=${CMAKE_BINARY_DIR}/temp/PlaySessionUser")
        add_executable(UrsaProjectEditorExtensionsTests Tests/Scripting/ProjectEditorExtensionsTests.cpp)
        target_link_libraries(UrsaProjectEditorExtensionsTests PRIVATE Ursa::DotNetHost)
        target_compile_definitions(UrsaProjectEditorExtensionsTests PRIVATE
          URSA_PROJECT_DIR="${URSA_TEST_SAMPLE_PROJECT}"
          URSA_DOTNET_RUNTIME_ASSEMBLY_PATH="${URSA_MANAGED_OUTPUT_DIR}/Ursa.Runtime.dll")
        add_dependencies(UrsaProjectEditorExtensionsTests UrsaManagedHostAssemblies)
        ursa_attach_dotnet_host_runtime(UrsaProjectEditorExtensionsTests)
        add_test(NAME UrsaProjectEditorExtensionsTests COMMAND UrsaProjectEditorExtensionsTests)
        set_tests_properties(UrsaProjectEditorExtensionsTests PROPERTIES FIXTURES_REQUIRED UrsaProjectScripts
          ENVIRONMENT "URSA_USER_DATA_ROOT=${CMAKE_BINARY_DIR}/temp/EditorExtensionUser")
      endif()
      add_test(NAME UrsaProjectPackageScriptCompile COMMAND UrsaScriptCompile "${URSA_TEST_SAMPLE_PROJECT}" "$<CONFIG>")
      set_tests_properties(UrsaProjectPackageScriptCompile PROPERTIES FIXTURES_SETUP UrsaProjectPackageScripts)
      add_executable(UrsaProjectLibraryRecoveryTests Tests/Resources/ProjectLibraryRecoveryTests.cpp)
      target_link_libraries(UrsaProjectLibraryRecoveryTests PRIVATE Ursa::Editor)
      target_compile_definitions(UrsaProjectLibraryRecoveryTests PRIVATE
        URSA_PROJECT_DIR="${URSA_TEST_SAMPLE_PROJECT}"
        URSA_TEST_SCRATCH_ROOT="${CMAKE_BINARY_DIR}/Temp/ProjectLibraryRecovery/$<CONFIG>")
      ursa_attach_dotnet_host_runtime(UrsaProjectLibraryRecoveryTests)
      add_test(NAME UrsaProjectLibraryRecoveryTests COMMAND UrsaProjectLibraryRecoveryTests)
      add_test(NAME UrsaProjectImportPrepare COMMAND UrsaAssetImport --project "${URSA_TEST_SAMPLE_PROJECT}" --configuration "$<CONFIG>")
      set_tests_properties(UrsaProjectImportPrepare PROPERTIES FIXTURES_SETUP UrsaProjectImports)
      add_test(NAME UrsaProjectPackage COMMAND UrsaPak
        --project-root "${URSA_TEST_SAMPLE_PROJECT}"
        --configuration "$<CONFIG>" --output "${CMAKE_BINARY_DIR}/Package/$<CONFIG>/ProjectContent.pak")
      set_tests_properties(UrsaProjectPackage PROPERTIES FIXTURES_SETUP UrsaProjectPak
        FIXTURES_REQUIRED "UrsaProjectScripts;UrsaProjectPackageScripts;UrsaProjectImports")
      add_test(NAME UrsaProjectSnakePakSmoke COMMAND UrsaScriptPlayer
        --pak "${CMAKE_BINARY_DIR}/Package/$<CONFIG>/ProjectContent.pak" --scene "Snake/Snake.ursascene"
        --hidden --frames 20 --stress-window)
      set_tests_properties(UrsaProjectSnakePakSmoke PROPERTIES FIXTURES_REQUIRED UrsaProjectPak
        ENVIRONMENT "URSA_USER_DATA_ROOT=${CMAKE_BINARY_DIR}/temp/ProjectUser")
      if(TARGET Ursa::Editor)
        add_executable(UrsaProjectTextureImportTests Tests/Assets/ProjectTextureImportTests.cpp)
        target_link_libraries(UrsaProjectTextureImportTests PRIVATE Ursa::Editor)
        target_compile_definitions(UrsaProjectTextureImportTests PRIVATE
          URSA_DEMO_TEXTURE_PATH="${URSA_TEST_SAMPLE_PROJECT}/Assets/Textures/UrsaEngine.png")
        add_test(NAME UrsaProjectTextureImportTests COMMAND UrsaProjectTextureImportTests)
        add_executable(UrsaProjectModelImportTests Tests/Assets/ProjectModelImportTests.cpp)
        target_link_libraries(UrsaProjectModelImportTests PRIVATE Ursa::Editor)
        target_compile_definitions(UrsaProjectModelImportTests PRIVATE
          URSA_DEMO_MODEL_PATH="${URSA_TEST_SAMPLE_PROJECT}/Assets/Models/Cube.fbx"
          URSA_DEMO_TEXTURE_PATH="${URSA_TEST_SAMPLE_PROJECT}/Assets/Textures/UrsaEngine.png"
          URSA_ASSIMP_EMBEDDED_FBX_PATH="${PROJECT_SOURCE_DIR}/third_party/assimp/test/models/FBX/embedded_ascii/box_embedded_texture_fragmented.fbx"
          URSA_ASSIMP_ANIMATED_FBX_PATH="${PROJECT_SOURCE_DIR}/third_party/assimp/test/models/FBX/animation_with_skeleton.fbx"
          URSA_PAK_EXECUTABLE="$<TARGET_FILE:UrsaPak>"
          URSA_DEMO_ENGINE_ASSET_PATH="${PROJECT_SOURCE_DIR}/Assets"
          URSA_TEST_CONFIGURATION="$<CONFIG>")
        add_dependencies(UrsaProjectModelImportTests UrsaPak)
        add_test(NAME UrsaProjectModelImportTests COMMAND UrsaProjectModelImportTests)
      endif()
    endif()
  endif()
endif()


if(URSA_BUILD_TESTS AND TARGET UrsaAssetImport)
  set(URSA_TEST_CONTENT_PROJECT "${CMAKE_BINARY_DIR}/TestProjects/Runtime/$<CONFIG>")
  add_test(NAME UrsaTestContentPrepare COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT=${URSA_TEST_CONTENT_PROJECT}" "-DIMPORTER=$<TARGET_FILE:UrsaAssetImport>"
    "-DCONFIGURATION=$<CONFIG>" "-DGLSLC=${URSA_GLSLC_EXECUTABLE}"
    "-DSHADER_SOURCES=${PROJECT_SOURCE_DIR}/Tests/Rhi/Shaders"
    -P "${PROJECT_SOURCE_DIR}/Tests/Resources/PrepareTestContent.cmake")
  set_tests_properties(UrsaTestContentPrepare PROPERTIES FIXTURES_SETUP UrsaTestContent)
  if(NOT URSA_PROJECT_DIR STREQUAL "")
    add_test(NAME UrsaTestSamplePrepare COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE=${URSA_PROJECT_DIR}/Assets" "-DPROJECT=${URSA_TEST_SAMPLE_PROJECT}"
      "-DIMPORTER=$<TARGET_FILE:UrsaAssetImport>" "-DCONFIGURATION=$<CONFIG>"
      -P "${PROJECT_SOURCE_DIR}/Tests/Resources/PrepareSampleProject.cmake")
    set_tests_properties(UrsaTestSamplePrepare PROPERTIES FIXTURES_SETUP UrsaTestSample)
    if(TARGET UrsaScriptPlayer)
      find_program(URSA_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
      add_test(NAME UrsaAssetImportCliTests COMMAND "${URSA_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File "${PROJECT_SOURCE_DIR}/Tests/Resources/AssetImportCliTests.ps1"
        -Importer "$<TARGET_FILE:UrsaAssetImport>" -Player "$<TARGET_FILE:UrsaScriptPlayer>"
        -PackageTool "$<TARGET_FILE:UrsaPak>" -Configuration "$<CONFIG>"
        -Project "${CMAKE_BINARY_DIR}/TestProjects/Cli/$<CONFIG>" -Source "${URSA_TEST_SAMPLE_PROJECT}")
    endif()

  endif()
  # Fixtures execute tools, so compiling a test also builds their programs;
  # these dependencies never execute content preparation.
  get_property(ursaFixtureConsumers DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(consumer IN LISTS ursaFixtureConsumers)
    get_target_property(type ${consumer} TYPE)
    if(type STREQUAL "EXECUTABLE" AND consumer MATCHES "(Tests|Smoke)$")
      add_dependencies(${consumer} UrsaAssetImport UrsaPak)
    endif()
  endforeach()
  get_property(ursaTests DIRECTORY PROPERTY TESTS)
  foreach(test IN LISTS ursaTests)
    if(NOT test STREQUAL "UrsaTestContentPrepare" AND NOT test STREQUAL "UrsaTestSamplePrepare")
      if(TEST UrsaTestSamplePrepare)
        set_property(TEST ${test} APPEND PROPERTY FIXTURES_REQUIRED UrsaTestSample)
      endif()
      set_property(TEST ${test} APPEND PROPERTY FIXTURES_REQUIRED UrsaTestContent)
      set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "URSA_CONTENT_PROJECT=${URSA_TEST_CONTENT_PROJECT}")
    endif()
  endforeach()

endif()

if(TARGET UrsaAssetImport AND NOT URSA_PROJECT_DIR STREQUAL "")
  add_custom_target(UrsaEnginePackage
    COMMAND UrsaPak --project-root "${URSA_PROJECT_DIR}" --configuration "$<CONFIG>"
      --output "${CMAKE_BINARY_DIR}/Package/$<CONFIG>/Content.pak"
    DEPENDS UrsaPak VERBATIM)
endif()

if(TEST UrsaManagedEditorWorkflowTests)
  set_tests_properties(UrsaManagedEditorWorkflowTests PROPERTIES
    ENVIRONMENT "URSA_USER_DATA_ROOT=${CMAKE_BINARY_DIR}/Temp/ManagedWorkflowUser")
endif()

if(URSA_WARNINGS_AS_ERRORS)
  ursa_configure_warning_policy("${CMAKE_CURRENT_SOURCE_DIR}")
endif()
