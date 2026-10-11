if(URSA_ENABLE_DOTNET)
  if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "URSA_ENABLE_DOTNET currently supports Windows x64 only")
  endif()

  find_program(URSA_DOTNET_EXECUTABLE NAMES dotnet)
  if(NOT URSA_DOTNET_EXECUTABLE)
    message(FATAL_ERROR "URSA_ENABLE_DOTNET requires the .NET 10 SDK and runtime. Install .NET 10 or configure with -DURSA_ENABLE_DOTNET=OFF")
  endif()

  execute_process(
    COMMAND "${URSA_DOTNET_EXECUTABLE}" --list-sdks
    RESULT_VARIABLE URSA_DOTNET_SDK_RESULT
    OUTPUT_VARIABLE URSA_DOTNET_SDKS
    ERROR_VARIABLE URSA_DOTNET_SDK_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(
    COMMAND "${URSA_DOTNET_EXECUTABLE}" --list-runtimes
    RESULT_VARIABLE URSA_DOTNET_RUNTIME_RESULT
    OUTPUT_VARIABLE URSA_DOTNET_RUNTIMES
    ERROR_VARIABLE URSA_DOTNET_RUNTIME_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT URSA_DOTNET_SDK_RESULT STREQUAL "0" OR NOT URSA_DOTNET_RUNTIME_RESULT STREQUAL "0")
    message(FATAL_ERROR "Cannot inspect the installed .NET SDK/runtime: ${URSA_DOTNET_SDK_ERROR}${URSA_DOTNET_RUNTIME_ERROR}")
  endif()

  string(REGEX MATCH "(^|\n)10\\.[0-9]+\\.[0-9]+([ \t]|$)" URSA_DOTNET_SDK_10 "${URSA_DOTNET_SDKS}")
  string(REGEX MATCH "Microsoft\\.NETCore\\.App 10\\.[0-9]+\\.[0-9]+" URSA_DOTNET_RUNTIME_10 "${URSA_DOTNET_RUNTIMES}")
  if(NOT URSA_DOTNET_SDK_10 OR NOT URSA_DOTNET_RUNTIME_10)
    message(FATAL_ERROR "URSA_ENABLE_DOTNET requires both a .NET 10 SDK and Microsoft.NETCore.App 10 runtime. Installed SDKs: ${URSA_DOTNET_SDKS}; runtimes: ${URSA_DOTNET_RUNTIMES}")
  endif()

  get_filename_component(URSA_DOTNET_ROOT "${URSA_DOTNET_EXECUTABLE}" DIRECTORY)
  string(REGEX MATCH "10\\.[0-9]+\\.[0-9]+" URSA_DOTNET_HOST_PACK_VERSION "${URSA_DOTNET_RUNTIME_10}")
  set(URSA_DOTNET_HOST_PACK "${URSA_DOTNET_ROOT}/packs/Microsoft.NETCore.App.Host.win-x64/${URSA_DOTNET_HOST_PACK_VERSION}/runtimes/win-x64/native")
  set(URSA_DOTNET_REFERENCE_PACK "${URSA_DOTNET_ROOT}/packs/Microsoft.NETCore.App.Ref/${URSA_DOTNET_HOST_PACK_VERSION}/ref/net10.0")
  foreach(URSA_DOTNET_HOST_FILE IN ITEMS nethost.h hostfxr.h coreclr_delegates.h)
    if(NOT EXISTS "${URSA_DOTNET_HOST_PACK}/${URSA_DOTNET_HOST_FILE}")
      message(FATAL_ERROR "The .NET host pack is incomplete: missing ${URSA_DOTNET_HOST_PACK}/${URSA_DOTNET_HOST_FILE}")
    endif()
  endforeach()
  foreach(URSA_DOTNET_BINARY IN ITEMS nethost.lib nethost.dll)
    if(NOT EXISTS "${URSA_DOTNET_HOST_PACK}/${URSA_DOTNET_BINARY}")
      message(FATAL_ERROR "The .NET host pack is incomplete: missing ${URSA_DOTNET_BINARY}")
    endif()
  endforeach()
  if(NOT EXISTS "${URSA_DOTNET_REFERENCE_PACK}/System.Runtime.dll")
    message(FATAL_ERROR "The .NET reference pack is incomplete: missing ${URSA_DOTNET_REFERENCE_PACK}/System.Runtime.dll")
  endif()

  message(STATUS "Ursa C# scripting enabled with .NET SDK/runtime and Windows x64 host pack ${URSA_DOTNET_HOST_PACK_VERSION}")
endif()

function(ursa_attach_dotnet_host_runtime target)
  if(URSA_ENABLE_DOTNET)
    if(NOT TARGET ${target})
      message(FATAL_ERROR "Cannot configure the .NET native host for missing target ${target}")
    endif()
    get_target_property(nethostDll UrsaDotNetHostSupport URSA_NETHOST_DLL)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${nethostDll}" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endif()
endfunction()


set(URSA_MANAGED_OUTPUT_DIR "${URSA_BINARY_ROOT}/Managed/$<CONFIG>")
set(URSA_MANAGED_STAGE_ROOT "${URSA_BINARY_ROOT}/Managed/Source")
set(URSA_MANAGED_WARNING_FLAGS)
if(URSA_WARNINGS_AS_ERRORS)
  list(APPEND URSA_MANAGED_WARNING_FLAGS -warnaserror)
endif()
