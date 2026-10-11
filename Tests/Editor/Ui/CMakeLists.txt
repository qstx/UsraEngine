if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaImGuiBridgeTests ImGuiBridgeTests.cpp)
  target_link_libraries(UrsaImGuiBridgeTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaImGuiBridgeTests COMMAND UrsaImGuiBridgeTests)
endif()
ursa_require_test_content(UrsaImGuiBridgeTests UrsaImGuiBridgeTests)

ursa_finalize_test_directory()
