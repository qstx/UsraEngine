file(MAKE_DIRECTORY "${URSA_SHADER_PAK_DIRECTORY}")

execute_process(
  COMMAND "${URSA_PAK_EXECUTABLE}"
    --engine "${URSA_ENGINE_ASSET_SOURCE}"
    --engine-generated "${URSA_ENGINE_ASSET_MIRROR}"
    --output "${URSA_SHADER_PAK_PATH}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "UrsaPak failed to build the Shader test package: ${output}${error}")
endif()
