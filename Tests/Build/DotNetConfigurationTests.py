"""Exercise required .NET toolchain checks and migration against isolated CMake inputs."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest


class DotNetConfigurationTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix='Ursa DotNet ')
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.host = self.root / 'packs/Microsoft.NETCore.App.Host.win-x64/10.0.0/runtimes/win-x64/native'
        self.reference = self.root / 'packs/Microsoft.NETCore.App.Ref/10.0.0/ref/net10.0/System.Runtime.dll'
        for path in [*(self.host / name for name in ('nethost.h', 'hostfxr.h', 'coreclr_delegates.h', 'nethost.lib', 'nethost.dll')), self.reference]:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.probe = self.root / 'probe.cmake'
        self.probe.write_text('''
set(WIN32 TRUE)
set(CMAKE_SIZEOF_VOID_P 8)
set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID "x64")
set(URSA_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(URSA_DOTNET_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/dotnet.exe")
if(CASE STREQUAL "platform")
  set(WIN32 FALSE)
elseif(CASE STREQUAL "architecture")
  set(CMAKE_SIZEOF_VOID_P 4)
elseif(CASE STREQUAL "arm64")
  set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID "ARM64")
elseif(CASE STREQUAL "missing")
  set(URSA_DOTNET_EXECUTABLE "")
endif()
# Model only the dotnet process boundary; the production CMake checks run unchanged.
function(execute_process)
  cmake_parse_arguments(PROBE "" "OUTPUT_VARIABLE;ERROR_VARIABLE;RESULT_VARIABLE" "COMMAND" ${ARGN})
  if("--list-sdks" IN_LIST PROBE_COMMAND)
    set(value "10.0.100 [sdk]")
    if(CASE STREQUAL "sdk")
      set(value "9.0.100 [sdk]")
    endif()
  elseif("--list-runtimes" IN_LIST PROBE_COMMAND)
    set(value "Microsoft.NETCore.App 10.0.0 [shared]")
    if(CASE STREQUAL "runtime")
      set(value "Microsoft.NETCore.App 9.0.0 [shared]")
    endif()
  else()
    set(value "10.0.100")
    if(CASE STREQUAL "active")
      set(value "9.0.100")
    endif()
  endif()
  set(${PROBE_OUTPUT_VARIABLE} "${value}" PARENT_SCOPE)
  if(PROBE_RESULT_VARIABLE)
    set(${PROBE_RESULT_VARIABLE} 0 PARENT_SCOPE)
  endif()
  if(PROBE_ERROR_VARIABLE)
    set(${PROBE_ERROR_VARIABLE} "" PARENT_SCOPE)
  endif()
endfunction()
include("${ENGINE_ROOT}/CMake/DotNet.cmake")
''', encoding='utf-8')

    def probe_result(self, case='valid'):
        result = subprocess.run([CMAKE, '-Wno-dev', f'-DENGINE_ROOT={ENGINE.as_posix()}', f'-DCASE={case}', '-P', str(self.probe)],
                                capture_output=True, text=True, encoding="utf-8")
        return result.returncode, result.stdout + result.stderr

    def test_supported_toolchain(self):
        code, output = self.probe_result()
        self.assertEqual(code, 0, output)

    def test_missing_and_incompatible_toolchains(self):
        for case, diagnostic in (('missing', '.NET 10 SDK'), ('platform', 'Windows x64'),
                                 ('architecture', 'Windows x64'), ('arm64', 'Windows x64'), ('sdk', '.NET 10 SDK'),
                                 ('runtime', '10 runtime'), ('active', 'active dotnet SDK')):
            with self.subTest(case=case):
                code, output = self.probe_result(case)
                self.assertNotEqual(code, 0, output)
                self.assertIn(diagnostic, output)

    def test_incomplete_packages(self):
        for path in [self.host / name for name in ('nethost.h', 'hostfxr.h', 'coreclr_delegates.h', 'nethost.lib', 'nethost.dll')] + [self.reference]:
            with self.subTest(file=path.name):
                path.unlink()
                code, output = self.probe_result()
                self.assertNotEqual(code, 0, output)
                self.assertIn(path.name, output)
                path.touch()

    def test_retired_arguments_and_cache_migration(self):
        source = self.root / 'source'
        source.mkdir()
        (source / 'CMakeLists.txt').write_text(
            f'cmake_minimum_required(VERSION 4.4.5)\ninclude("{ENGINE.as_posix()}/CMake/RetiredOptions.cmake")\n'
            'project(Migration NONE)\nset(PRESERVED "kept" CACHE STRING "Other setting")\n', encoding='utf-8')
        for cached in (False, True):
            for value in ('ON', 'OFF'):
                with self.subTest(cached=cached, value=value):
                    build = self.root / f'build-{cached}-{value}'
                    command = [CMAKE, '-S', str(source), '-B', str(build)]
                    if cached:
                        build.mkdir()
                        (build / 'CMakeCache.txt').write_text(f'URSA_ENABLE_DOTNET:BOOL={value}\nPRESERVED:STRING=custom\n', encoding='utf-8')
                        request = command
                    else:
                        request = command + [f'-DURSA_ENABLE_DOTNET={value}', '-DPRESERVED=custom']
                    result = subprocess.run(request, capture_output=True, text=True, encoding="utf-8")
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('has been removed', result.stderr)
                    self.assertIn('-UURSA_ENABLE_DOTNET', result.stderr)
                    migrated = subprocess.run(command + ['-UURSA_ENABLE_DOTNET'], capture_output=True, text=True, encoding="utf-8")
                    self.assertEqual(migrated.returncode, 0, migrated.stdout + migrated.stderr)
                    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
                    self.assertIn('PRESERVED:STRING=custom', cache)
                    self.assertNotIn('URSA_ENABLE_DOTNET:', cache)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default='cmake')
    args, rest = parser.parse_known_args()
    CMAKE = args.cmake
    ENGINE = Path(__file__).resolve().parents[2]
    unittest.main(argv=[__file__, *rest])
