set(bindingsGenerated "${URSA_BINARY_ROOT}/Tests/Bindings/Generated/$<CONFIG>")
set(bindingsManaged "${URSA_BINARY_ROOT}/Tests/Bindings/Managed/$<CONFIG>")
file(GLOB bindingsContracts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Contracts/*.cs")
add_custom_command(OUTPUT "${bindingsGenerated}/Built.stamp"
  BYPRODUCTS "${bindingsGenerated}/Bindings.manifest.json"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" "${URSA_BINDINGS_GENERATOR}"
    --input "${CMAKE_CURRENT_SOURCE_DIR}/Contracts" --output "${bindingsGenerated}"
    --source "${URSA_SOURCE_ROOT}/Managed/Ursa.Runtime/MathValues.cs"
  COMMAND "${CMAKE_COMMAND}" -E touch "${bindingsGenerated}/Built.stamp"
  DEPENDS UrsaBindingsGenerator "${URSA_BINDINGS_GENERATOR_STAMP}" ${bindingsContracts}
    "${URSA_SOURCE_ROOT}/Managed/Ursa.Runtime/MathValues.cs"
  VERBATIM)
add_custom_target(UrsaBindingsFixtureGenerated DEPENDS "${bindingsGenerated}/Built.stamp")

add_executable(UrsaBindingsNativeFixture Native/BindingFixture.cpp Native/BindingFixture.hpp Signature/Entry.cpp)
add_dependencies(UrsaBindingsNativeFixture UrsaBindingsFixtureGenerated)
# The executable owns Runtime once and hosts CoreCLR; no instrumented DLL unload or duplicate core sources.
target_link_libraries(UrsaBindingsNativeFixture PRIVATE Ursa::Engine "${URSA_DOTNET_HOST_PACK}/nethost.lib")
target_include_directories(UrsaBindingsNativeFixture PRIVATE
  "${URSA_SOURCE_ROOT}/Engine" "${CMAKE_CURRENT_SOURCE_DIR}/Native" "${bindingsGenerated}/Native"
  "${URSA_DOTNET_HOST_PACK}")
ursa_source_groups(UrsaBindingsNativeFixture "Tests/Bindings")
ursa_attach_dotnet_host_runtime(UrsaBindingsNativeFixture)

file(GLOB bindingsTestSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Managed/*.cs"
  "${CMAKE_CURRENT_SOURCE_DIR}/Managed/*.csproj")
add_custom_command(OUTPUT "${bindingsManaged}/Ursa.BindingsTests.dll"
  COMMAND "${URSA_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Managed/Ursa.BindingsTests.csproj"
    --configuration "$<CONFIG>" --nologo -m:1
    --artifacts-path "${URSA_BINARY_ROOT}/Tests/Bindings/Artifacts"
    --output "${bindingsManaged}"
    "-p:BindingsGeneratedDir=${bindingsGenerated}/Managed"
  DEPENDS UrsaBindingsFixtureGenerated "${bindingsGenerated}/Built.stamp"
    ${bindingsContracts} ${bindingsTestSources} ${bindingGeneratorSources}
    "${URSA_SOURCE_ROOT}/Managed/Ursa.Runtime/MathValues.cs"
  VERBATIM)
add_custom_target(UrsaBindingsManagedTests DEPENDS "${bindingsManaged}/Ursa.BindingsTests.dll")
add_custom_target(UrsaBindingsTests ALL)
add_dependencies(UrsaBindingsTests UrsaBindingsNativeFixture UrsaBindingsManagedTests)
set_target_properties(UrsaBindingsTests UrsaBindingsManagedTests UrsaBindingsFixtureGenerated
  PROPERTIES FOLDER "Tests/Bindings")
add_test(NAME UrsaBindingsTests COMMAND "$<TARGET_FILE:UrsaBindingsNativeFixture>"
  "${bindingsManaged}/Ursa.BindingsTests.dll" "${URSA_BINDINGS_GENERATOR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/Contracts" "${URSA_BINARY_ROOT}/Tests/Bindings/Scratch/$<CONFIG>")
set_tests_properties(UrsaBindingsTests PROPERTIES TIMEOUT 180)
add_test(NAME UrsaBindingsGeneratorTests COMMAND "${URSA_DOTNET_EXECUTABLE}" "${bindingsManaged}/Ursa.BindingsTests.dll"
  --generator-only "${URSA_BINDINGS_GENERATOR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/Contracts" "${URSA_BINARY_ROOT}/Tests/Bindings/GeneratorScratch/$<CONFIG>")
set_tests_properties(UrsaBindingsGeneratorTests PROPERTIES TIMEOUT 180)
add_test(NAME UrsaBindingsSignatureTests COMMAND "${CMAKE_COMMAND}"
  "-DURSA_SOURCE_ROOT=${URSA_SOURCE_ROOT}"
  "-DPROBE_ROOT=${URSA_BINARY_ROOT}/Tests/Bindings/Signature/$<CONFIG>"
  "-DBINDINGS_GENERATOR=${URSA_BINDINGS_GENERATOR}"
  "-DDOTNET_EXECUTABLE=${URSA_DOTNET_EXECUTABLE}"
  "-DPROBE_GENERATOR=${CMAKE_GENERATOR}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/SignatureTests.cmake")
set_tests_properties(UrsaBindingsSignatureTests PROPERTIES TIMEOUT 180)
