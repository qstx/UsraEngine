cmake_minimum_required(VERSION 3.20)
project(UrsaBindingSignatureProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(SignatureProbe STATIC Entry.cpp)
target_include_directories(SignatureProbe PRIVATE
  "${BINDING_PROBE_ROOT}" "${BINDING_PROBE_ROOT}/Generated/Native"
  "${URSA_SOURCE_ROOT}/Tests/Bindings/Native"
  "${URSA_SOURCE_ROOT}/Engine" "${URSA_SOURCE_ROOT}/Runtime"
  "${URSA_SOURCE_ROOT}/ThirdParty/eastl/include"
  "${URSA_SOURCE_ROOT}/ThirdParty/eabase/include/Common")
if(MSVC)
  target_compile_options(SignatureProbe PRIVATE /utf-8 /permissive-)
endif()
