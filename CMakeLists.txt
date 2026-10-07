cmake_minimum_required(VERSION 3.20)
project(UrsaEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

if(WIN32)
  add_custom_target(UrsaEngineFormatCheck
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Format.ps1" -Check
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
  set_target_properties(UrsaEngineFormatCheck PROPERTIES FOLDER "Tools")
endif()

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
endif()
set(FETCHCONTENT_SOURCE_DIR_EABASE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eabase")
add_subdirectory(third_party/eastl)
target_compile_definitions(EASTL PUBLIC EASTL_USER_DEFINED_ALLOCATOR=1)

# Keep vendored projects out of the way in the generated solution.
foreach(URSA_DEPENDENCY IN ITEMS EASTL EABase glfw spdlog spdlog_header_only update_mappings uninstall)
  if(TARGET ${URSA_DEPENDENCY})
    set_target_properties(${URSA_DEPENDENCY} PROPERTIES FOLDER "Dependencies")
  endif()
endforeach()

find_package(Vulkan 1.2 REQUIRED COMPONENTS glslc)

# Generated engine assets (compiled shaders) are staged into the project's cache
# directory. The engine source assets stay in the engine repository and are
# mounted ahead of the mirror.
set(URSA_ASSET_CACHE_DIR "${CMAKE_SOURCE_DIR}/cache" CACHE PATH
    "Directory that holds generated engine assets")
set(URSA_ASSET_MIRROR_DIR "${URSA_ASSET_CACHE_DIR}/Assets" CACHE PATH
    "Generated engine asset mirror mounted as the engine resource scheme")

add_subdirectory(Runtime)
add_subdirectory(Engine)

add_executable(UrsaPak Tools/Pak/Main.cpp)
target_link_libraries(UrsaPak PRIVATE Ursa::Runtime)
set_target_properties(UrsaPak PROPERTIES FOLDER "Tools")
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  add_custom_target(UrsaEnginePackage
    COMMAND UrsaPak --engine "${PROJECT_SOURCE_DIR}/Assets"
      --engine-generated "${URSA_ASSET_MIRROR_DIR}" --output "${CMAKE_BINARY_DIR}/package/Content.pak"
    DEPENDS UrsaPak UrsaEngine VERBATIM)
endif()

option(URSA_BUILD_EDITOR "Build the Ursa editor" ON)
if(URSA_BUILD_EDITOR)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp" OR
     NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imguizmo/src/ImGuizmo.cpp")
    message(FATAL_ERROR "Editor submodules are missing. Run: git submodule update --init third_party/imgui third_party/imguizmo")
  endif()
  add_subdirectory(Editor)
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
  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures")
  add_executable(UrsaResourceVfsTests Tests/Serialization/ResourceVfsTests.cpp)
  target_link_libraries(UrsaResourceVfsTests PRIVATE Ursa::Engine)
  add_test(NAME UrsaResourceVfsTests COMMAND UrsaResourceVfsTests)
  add_executable(UrsaSerializableHeaderSmoke Tests/Serialization/SerializableHeaderSmoke.cpp)
  target_link_libraries(UrsaSerializableHeaderSmoke PRIVATE Ursa::Engine)
  add_test(NAME UrsaSerializableHeaderSmoke COMMAND UrsaSerializableHeaderSmoke)
  enable_testing()

  if(URSA_BUILD_EDITOR)
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
    get_filename_component(name "${source}" NAME)
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/Shaders/${name}.spv")
    add_custom_command(OUTPUT "${output}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
      COMMAND Vulkan::glslc "${input}" -o "${output}"
      DEPENDS "${input}" VERBATIM)
    target_sources(${target} PRIVATE "${input}" "${output}")
    set_source_files_properties("${input}" PROPERTIES HEADER_FILE_ONLY TRUE)
    set_source_files_properties("${output}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES "${input}")
    source_group("Generated Shaders" FILES "${output}")
  endfunction()

  add_executable(UrsaRhiTests Tests/Rhi/RhiTests.cpp)
  target_link_libraries(UrsaRhiTests PRIVATE Ursa::Runtime)
  target_compile_definitions(UrsaRhiTests PRIVATE URSA_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/Shaders")
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/Test.comp)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestImage.comp)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestVertex.vert)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/Test.frag)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestInstance.vert)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestInstance.frag)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestOffset.vert)
  ursa_test_shader(UrsaRhiTests Tests/Rhi/Shaders/TestOffset.frag)
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
endif()
