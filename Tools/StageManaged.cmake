cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED OUTPUT_ROOT)
  message(FATAL_ERROR "StageManaged requires SOURCE_ROOT and OUTPUT_ROOT")
endif()

file(GLOB_RECURSE inputs RELATIVE "${SOURCE_ROOT}"
  "${SOURCE_ROOT}/*.cs" "${SOURCE_ROOT}/*.csproj" "${SOURCE_ROOT}/*.props" "${SOURCE_ROOT}/*.targets")
list(FILTER inputs EXCLUDE REGEX "(^|/)(bin|obj)/")
file(GLOB_RECURSE staged RELATIVE "${OUTPUT_ROOT}"
  "${OUTPUT_ROOT}/*.cs" "${OUTPUT_ROOT}/*.csproj" "${OUTPUT_ROOT}/*.props" "${OUTPUT_ROOT}/*.targets")
list(FILTER staged EXCLUDE REGEX "(^|/)(bin|obj)/")
foreach(relative IN LISTS staged)
  if(NOT relative IN_LIST inputs)
    file(REMOVE "${OUTPUT_ROOT}/${relative}")
  endif()
endforeach()
foreach(relative IN LISTS inputs)
  get_filename_component(directory "${OUTPUT_ROOT}/${relative}" DIRECTORY)
  file(MAKE_DIRECTORY "${directory}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${SOURCE_ROOT}/${relative}" "${OUTPUT_ROOT}/${relative}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
