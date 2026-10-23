# Each invocation owns a new fixture under the build tree. Never mutate Demo Assets.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef fixtureId)
set(projectRoot "${TEST_ROOT}/ManagedPackage-${fixtureId}")
file(MAKE_DIRECTORY "${projectRoot}/Assets/Editor")
set(gameSource "${projectRoot}/Assets/Probe.cs")
file(WRITE "${gameSource}"
  "using Ursa; public sealed class Probe : Behaviour { public int value = 7; }\n")
file(WRITE "${projectRoot}/Assets/Editor/Tools.cs"
  "using Ursa.Editor; public static class Tools { [MenuItem(\"Tools/Probe\")] private static void Probe() {} }\n")

function(compile_scripts expectedSuccess)
  execute_process(COMMAND "${COMPILER}" "${projectRoot}" "${CONFIGURATION}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(expectedSuccess AND NOT result STREQUAL "0")
    message(FATAL_ERROR "Fixture compilation failed: ${output}${error}")
  elseif(NOT expectedSuccess AND result STREQUAL "0")
    message(FATAL_ERROR "Invalid source compilation unexpectedly succeeded")
  endif()
endfunction()

function(package_scripts expectedSuccess)
  execute_process(COMMAND "${PAK_TOOL}" --engine "${ENGINE_ASSETS}"
    --engine-generated "${GENERATED_ASSETS}" --project "${projectRoot}/Assets"
    --library "${projectRoot}/Library" --configuration "${CONFIGURATION}"
    --output "${projectRoot}/Content.pak" --loose-output "${projectRoot}/Content"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(expectedSuccess AND NOT result STREQUAL "0")
    message(FATAL_ERROR "Fixture package failed: ${output}${error}")
  elseif(NOT expectedSuccess AND result STREQUAL "0")
    message(FATAL_ERROR "Stale/failed game source was accepted for publishing")
  elseif(NOT expectedSuccess AND NOT "${output}${error}" MATCHES "stale|failed|CS[0-9]+")
    message(FATAL_ERROR "Packaging failed for an unrelated reason: ${output}${error}")
  endif()
endfunction()

compile_scripts(TRUE)
package_scripts(TRUE)
file(SHA256 "${projectRoot}/Content.pak" lastGoodPackage)
if(NOT EXISTS "${projectRoot}/Content/Project/__UrsaManaged/Ursa.Game.dll" OR
   NOT EXISTS "${projectRoot}/Content/Project/__UrsaManaged/Game.json")
  message(FATAL_ERROR "Player game assembly/manifest was not published")
endif()
file(GLOB_RECURSE publishedFiles "${projectRoot}/Content/*")
foreach(path IN LISTS publishedFiles)
  if(path MATCHES "\\.(cs|csproj)$|Editor.*\\.dll$|/Library/")
    message(FATAL_ERROR "Player content contains authoring data: ${path}")
  endif()
endforeach()

file(APPEND "${gameSource}" "// new input after successful compilation\n")
package_scripts(FALSE)
file(SHA256 "${projectRoot}/Content.pak" afterStalePackage)
file(APPEND "${gameSource}" "public class Broken { invalid }\n")
compile_scripts(FALSE)
package_scripts(FALSE)
file(SHA256 "${projectRoot}/Content.pak" afterFailedPackage)
if(NOT lastGoodPackage STREQUAL afterStalePackage OR NOT lastGoodPackage STREQUAL afterFailedPackage)
  message(FATAL_ERROR "Rejected publishing replaced the last valid package")
endif()
message(STATUS "Managed package contents and stale/failed publication guards passed")
