ursa_add_test_executable(UrsaPublicHeaderSmoke Main.cpp)
# Keep every example in a separate translation unit to check self-contained headers.
target_sources(UrsaPublicHeaderSmoke PRIVATE ContainerHeaderSmoke.cpp LogHeaderSmoke.cpp
  RuntimeHeaderSmoke.cpp MaterialHeaderSmoke.cpp LightingHeaderSmoke.cpp SerializableHeaderSmoke.cpp)
target_link_libraries(UrsaPublicHeaderSmoke PRIVATE Ursa::Engine)
if(URSA_BUILD_EDITOR)
  target_sources(UrsaPublicHeaderSmoke PRIVATE MaterialInspectorHeaderSmoke.cpp)
  target_link_libraries(UrsaPublicHeaderSmoke PRIVATE Ursa::Editor)
  target_compile_definitions(UrsaPublicHeaderSmoke PRIVATE URSA_HEADER_SMOKE_EDITOR=1)
endif()
add_test(NAME UrsaPublicHeaderSmoke COMMAND UrsaPublicHeaderSmoke)
ursa_finalize_test_directory()
