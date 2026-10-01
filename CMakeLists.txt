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

function(ursa_shader target source)
  get_filename_component(name "${source}" NAME)
  set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv")
  add_custom_command(OUTPUT "${output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND Vulkan::glslc "${CMAKE_CURRENT_SOURCE_DIR}/${source}" -o "${output}"
    DEPENDS "${source}" VERBATIM)
  target_sources(${target} PRIVATE "${output}")
  set_source_files_properties("${output}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
endfunction()

add_executable(UrsaTriangle examples/Triangle.cpp)
target_link_libraries(UrsaTriangle PRIVATE Ursa::Engine)
target_compile_definitions(UrsaTriangle PRIVATE URSA_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
ursa_shader(UrsaTriangle shaders/triangle.vert)
ursa_shader(UrsaTriangle shaders/triangle.frag)

enable_testing()
add_executable(UrsaRhiTests tests/RhiTests.cpp)
target_link_libraries(UrsaRhiTests PRIVATE Ursa::Runtime)
target_compile_definitions(UrsaRhiTests PRIVATE URSA_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
ursa_shader(UrsaRhiTests shaders/test.comp)
ursa_shader(UrsaRhiTests shaders/test_image.comp)
ursa_shader(UrsaRhiTests shaders/test_vertex.vert)
ursa_shader(UrsaRhiTests shaders/test.frag)
add_test(NAME UrsaRhiTests COMMAND UrsaRhiTests)
add_test(NAME UrsaWindowSmoke COMMAND UrsaTriangle --hidden --frames 20 --stress-window)
