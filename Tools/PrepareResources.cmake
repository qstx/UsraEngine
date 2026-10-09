cmake_minimum_required(VERSION 3.20)
foreach(required IN ITEMS IMPORTER SOURCE_ROOT OUTPUT_ROOT CACHE_ROOT CONFIGURATION DXC SPIRV_VAL SPIRV_CROSS)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "PrepareResources requires ${required}")
  endif()
endforeach()
file(MAKE_DIRECTORY "${CACHE_ROOT}")
file(MAKE_DIRECTORY "${CACHE_ROOT}/Temp")
set(ENV{TEMP} "${CACHE_ROOT}/Temp")
set(ENV{TMP} "${CACHE_ROOT}/Temp")
# Coordinate all configurations using this compilation cache. The CLI commits
# runtime content only after staging a complete set through VFS.
file(LOCK "${CACHE_ROOT}/Prepare.lock" GUARD PROCESS TIMEOUT 180 RESULT_VARIABLE locked)
if(NOT locked STREQUAL "0")
  message(FATAL_ERROR "Cannot lock shader compilation cache: ${locked}")
endif()
set(managedArguments)
if(DEFINED MANAGED_ROOT AND NOT MANAGED_ROOT STREQUAL "")
  list(APPEND managedArguments --managed-root "${MANAGED_ROOT}")
endif()
execute_process(COMMAND "${IMPORTER}" --source-root "${SOURCE_ROOT}" --output-root "${OUTPUT_ROOT}"
  --cache-root "${CACHE_ROOT}" --configuration "${CONFIGURATION}" --dxc "${DXC}"
  --spirv-val "${SPIRV_VAL}" --spirv-cross "${SPIRV_CROSS}" ${managedArguments}
  RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Engine resource preparation failed for ${CONFIGURATION}")
endif()
