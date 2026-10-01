add_library(UrsaEngine STATIC src/Application.cpp)
add_library(Ursa::Engine ALIAS UrsaEngine)

target_include_directories(UrsaEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(UrsaEngine PUBLIC Ursa::Runtime)

if(MSVC)
  target_compile_options(UrsaEngine PRIVATE /W4 /permissive-)
else()
  target_compile_options(UrsaEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()
