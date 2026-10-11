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

add_test(NAME UrsaRhiTests COMMAND UrsaRhiTests)

ursa_add_test_executable(UrsaVulkanValidationLogTests VulkanValidationLogTests.cpp)
target_link_libraries(UrsaVulkanValidationLogTests PRIVATE Ursa::Runtime Vulkan::Vulkan)

add_test(NAME UrsaVulkanValidationLogTests COMMAND UrsaVulkanValidationLogTests)
set_tests_properties(UrsaVulkanValidationLogTests PROPERTIES PASS_REGULAR_EXPRESSION "fallback before initialization")

add_test(NAME UrsaVulkanValidationShutdownSmoke COMMAND UrsaVulkanValidationLogTests)
set_tests_properties(UrsaVulkanValidationShutdownSmoke PROPERTIES PASS_REGULAR_EXPRESSION "fallback after shutdown")
ursa_require_test_content(UrsaRhiTests UrsaRhiTests)

ursa_finalize_test_directory()
