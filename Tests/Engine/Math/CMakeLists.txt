ursa_add_test_executable(UrsaMathTests MathTests.cpp)
target_link_libraries(UrsaMathTests PRIVATE Ursa::Engine)

add_test(NAME UrsaMathTests COMMAND UrsaMathTests)
ursa_finalize_test_directory()
