ursa_add_test_executable(UrsaSceneGraphTests SceneTests.cpp)
target_link_libraries(UrsaSceneGraphTests PRIVATE Ursa::Engine)

add_test(NAME UrsaSceneGraphTests COMMAND UrsaSceneGraphTests)

ursa_add_test_executable(UrsaGameObjectTests GameObjectTests.cpp)
target_link_libraries(UrsaGameObjectTests PRIVATE Ursa::Engine)

add_test(NAME UrsaGameObjectTests COMMAND UrsaGameObjectTests)

ursa_add_test_executable(UrsaLightingTests LightingTests.cpp)
target_link_libraries(UrsaLightingTests PRIVATE Ursa::Engine)

add_test(NAME UrsaLightingTests COMMAND UrsaLightingTests)

ursa_add_test_executable(UrsaLightingHeaderSmoke LightingHeaderSmoke.cpp)
target_link_libraries(UrsaLightingHeaderSmoke PRIVATE Ursa::Engine)

add_test(NAME UrsaLightingHeaderSmoke COMMAND UrsaLightingHeaderSmoke)
ursa_finalize_test_directory()
