"""Exercise dotnet/UTF-8 output through the actual VC CustomBuild task."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ENGINE = Path(__file__).absolute().parents[2]
sys.path.insert(0, str(ENGINE / 'Tools'))
from WindowsToolchain import get_toolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--scratch')
    args = parser.parse_args()
    toolchain = get_toolchain(args.cmake)
    if args.scratch:
        Path(args.scratch).mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='Ursa 中文构建 ', dir=args.scratch) as temporary:
        root = Path(temporary).resolve()
        source, build = root / '中文源码', root / 'Build with spaces'
        source.mkdir()
        env = dict(os.environ, DOTNET_CLI_UI_LANGUAGE='zh-CN', DOTNET_CLI_TELEMETRY_OPTOUT='1')
        def run(command):
            result = subprocess.run(list(map(str, command)), env=env, capture_output=True)
            output = (result.stdout + result.stderr).decode('utf-8', errors='strict')
            return result.returncode, output
        (source / 'Probe.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup></Project>', encoding='utf-8')
        (source / 'emit.py').write_text('''import pathlib, sys
sys.stdout.buffer.write(('中文标准输出：构建成功，路径=' + str(pathlib.Path(__file__)) + '\\n').encode('utf-8'))
sys.stdout.buffer.flush()
sys.stderr.buffer.write('中文错误流：诊断文本\\n'.encode('utf-8'))
sys.stderr.buffer.flush()
sys.exit(23 if '--fail' in sys.argv else 0)
''', encoding='utf-8')
        (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 4.4.5)
project(UrsaEncodingProbe LANGUAGES CXX)
set(CMAKE_VS_GLOBALS "DirectoryBuildPropsPath=${ENGINE_ROOT}/Directory.Build.props")
add_custom_target(Utf8Success
  COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/emit.py"
  COMMAND "${DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/Probe.csproj" --artifacts-path "${CMAKE_BINARY_DIR}/Artifacts" --nologo --disable-build-servers -m:1 -nr:false
  VERBATIM)
add_custom_target(Utf8Failure COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/emit.py" --fail VERBATIM)
''', encoding='utf-8')
        code, output = run([toolchain['CMake'], '-S', source, '-B', build, '-G', toolchain['Generator'], '-A', 'x64', '-T', toolchain['Toolset'],
                            f'-DCMAKE_GENERATOR_INSTANCE={toolchain["VisualStudio"]}', f'-DENGINE_ROOT={ENGINE.as_posix()}', f'-DPYTHON={Path(sys.executable).as_posix()}', '-DDOTNET=dotnet'])
        if code:
            raise RuntimeError(output)
        def compile(target, extra=()):
            return run([toolchain['CMake'], '--build', build, '--config', 'Debug', '--target', target, '--', '/nodeReuse:false', *extra])
        code, output = compile('Utf8Success')
        if code or '中文标准输出：构建成功' not in output or '中文错误流：诊断文本' not in output or str(source / 'emit.py') not in output:
            raise RuntimeError('UTF-8 stdout/stderr/path did not survive CustomBuild:\n' + output)
        if '正在确定要还原的项目' not in output or '已成功生成' not in output:
            raise RuntimeError('Chinese dotnet build output was lost:\n' + output)
        # Compare a real OEM-decoding baseline when the host OEM page is not UTF-8.
        import ctypes
        oem = ctypes.windll.kernel32.GetOEMCP()
        baseline = source / 'Baseline.props'
        baseline.write_text('<Project />', encoding='utf-8')
        baseline_code, baseline_output = compile('Utf8Success', [f'/p:DirectoryBuildPropsPath={baseline}'])
        if baseline_code:
            raise RuntimeError(baseline_output)
        if oem != 65001 and '中文标准输出：构建成功' in baseline_output and '中文错误流：诊断文本' in baseline_output:
            raise RuntimeError('The OEM baseline did not exercise a decoding mismatch')
        code, output = compile('Utf8Failure')
        if not code or '中文错误流：诊断文本' not in output:
            raise RuntimeError('Failure status or UTF-8 diagnostic was lost:\n' + output)
        print(json.dumps(dict(Passed=True, OemCodePage=oem, Stdout=True, Stderr=True, ChinesePath=True, DotnetChinese=True, RedirectedOutput=True, FailureStatus=code), ensure_ascii=False))


if __name__ == '__main__':
    main()
