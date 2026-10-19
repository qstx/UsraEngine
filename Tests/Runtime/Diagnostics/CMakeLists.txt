ursa_add_test_executable(UrsaWindowCaptureTests WindowCaptureTests.cpp)
target_link_libraries(UrsaWindowCaptureTests PRIVATE Ursa::Runtime nlohmann_json::nlohmann_json)
add_dependencies(UrsaWindowCaptureTests UrsaCapture)
add_test(NAME UrsaWindowCaptureTests COMMAND "${CMAKE_COMMAND}"
  "-DTEST_EXE=$<TARGET_FILE:UrsaWindowCaptureTests>" "-DCLI=$<TARGET_FILE:UrsaCapture>"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/WindowCaptureTests.cmake")
ursa_finalize_test_directory()
