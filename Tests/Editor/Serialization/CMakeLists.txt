if(URSA_BUILD_EDITOR)

  ursa_add_test_executable(UrsaSerializedObjectTests SerializedObjectTests.cpp)
  target_link_libraries(UrsaSerializedObjectTests PRIVATE Ursa::Editor)

  add_test(NAME UrsaSerializedObjectTests COMMAND UrsaSerializedObjectTests)
endif()
ursa_finalize_test_directory()
