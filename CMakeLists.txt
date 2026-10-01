cmake_minimum_required(VERSION 3.20)
project(UrsaEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/CMakeLists.txt" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma/CMakeLists.txt")
  message(FATAL_ERROR "Third-party submodules are missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory(third_party/glfw)
add_subdirectory(third_party/vma)
find_package(Vulkan 1.2 REQUIRED COMPONENTS glslc)

add_subdirectory(runtime)
add_subdirectory(engine)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(URSA_BUILD_TESTS_DEFAULT ON)
else()
  set(URSA_BUILD_TESTS_DEFAULT OFF)
endif()
option(URSA_BUILD_TESTS "Build the engine GPU tests" ${URSA_BUILD_TESTS_DEFAULT})

if(URSA_BUILD_TESTS)
  enable_testing()

  function(ursa_test_shader target source)
    get_filename_component(name "${source}" NAME)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv")
    add_custom_command(OUTPUT "${output}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
      COMMAND Vulkan::glslc "${CMAKE_CURRENT_SOURCE_DIR}/${source}" -o "${output}"
      DEPENDS "${source}" VERBATIM)
    target_sources(${target} PRIVATE "${output}")
    set_source_files_properties("${output}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
  endfunction()

  add_executable(UrsaRhiTests tests/RhiTests.cpp)
  target_link_libraries(UrsaRhiTests PRIVATE Ursa::Runtime)
  target_compile_definitions(UrsaRhiTests PRIVATE URSA_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
  ursa_test_shader(UrsaRhiTests shaders/test.comp)
  ursa_test_shader(UrsaRhiTests shaders/test_image.comp)
  ursa_test_shader(UrsaRhiTests shaders/test_vertex.vert)
  ursa_test_shader(UrsaRhiTests shaders/test.frag)
  add_test(NAME UrsaRhiTests COMMAND UrsaRhiTests)
endif()
