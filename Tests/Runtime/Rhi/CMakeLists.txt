ursa_add_test_executable(UrsaRhiTests RhiTests.cpp)
target_link_libraries(UrsaRhiTests PRIVATE Ursa::Runtime)
target_compile_definitions(UrsaRhiTests PRIVATE URSA_SHADER_DIR="${URSA_BINARY_ROOT}/TestProjects/Runtime/$<CONFIG>/Library/TestShaders")
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/Test.comp)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/TestImage.comp)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/TestVertex.vert)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/Test.frag)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/TestInstance.vert)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/TestInstance.frag)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/TestOffset.vert)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/TestOffset.frag)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/State.vert)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/State.frag)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/DepthSample.vert)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/DepthSample.frag)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/Fullscreen.vert)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/MultipleTargets.frag)
ursa_test_shader(UrsaRhiTests ${URSA_SOURCE_ROOT}/Tests/Runtime/Rhi/Shaders/Parameters.frag)

add_test(NAME UrsaRhiTests COMMAND UrsaRhiTests)

ursa_add_test_executable(UrsaVulkanValidationLogTests VulkanValidationLogTests.cpp)
target_link_libraries(UrsaVulkanValidationLogTests PRIVATE Ursa::Runtime Vulkan::Vulkan)

ursa_add_output_test(UrsaVulkanValidationLogTests UrsaVulkanValidationLogTests
  "fallback before initialization" "fallback after shutdown")
ursa_require_test_content(UrsaRhiTests UrsaRhiTests)

ursa_finalize_test_directory()
