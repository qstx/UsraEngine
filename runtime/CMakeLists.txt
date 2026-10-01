add_library(UrsaRuntime STATIC
  src/Window.cpp
  src/VulkanRHI.cpp
  src/Vma.cpp)
add_library(Ursa::Runtime ALIAS UrsaRuntime)

target_include_directories(UrsaRuntime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(UrsaRuntime PUBLIC cxx_std_17)
target_link_libraries(UrsaRuntime PRIVATE glfw Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
target_compile_definitions(UrsaRuntime PRIVATE GLFW_INCLUDE_NONE)

if(MSVC)
  target_compile_options(UrsaRuntime PRIVATE /W4 /permissive-)
  set_source_files_properties(src/Vma.cpp PROPERTIES COMPILE_OPTIONS /W0)
else()
  target_compile_options(UrsaRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()
