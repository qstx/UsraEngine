"""Workspace migration regressions, using isolated inputs and the public BAT API."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ENGINE = Path(__file__).absolute().parents[2]
sys.path.insert(0, str(ENGINE / 'Tools'))
from GenerateManagedIde import generate
from ReadVisualStudioSolution import read_projects
from WindowsToolchain import get_toolchain


class WorkspaceToolsTests(unittest.TestCase):
    def setUp(self):
        scratch = ENGINE / 'Build/Temp'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='Python workspace ', dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def bat(self, *args, env=None):
        return subprocess.run([str(ENGINE / 'Tools/GenerateSolution.bat'), *map(str, args)],
                              cwd=self.root, env=env, capture_output=True, text=True)

    def test_metadata_original_sources_references_and_incremental_refresh(self):
        self.write('Source & space/Managed/Base/Base.csproj', '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework></PropertyGroup></Project>')
        self.write('Source & space/Managed/Base/Base.cs', 'class Base {}')
        self.write('Source & space/Managed/Probe/Probe.csproj', '''<Project Sdk="Microsoft.NET.Sdk">
<!-- keep build metadata -->
<PropertyGroup><TargetFramework>net10.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks></PropertyGroup>
<ItemGroup><ProjectReference Include="../Base/Base.csproj" /><Reference Include="Microsoft.CodeAnalysis"><HintPath>$(MSBuildSDKsPath)/../Roslyn/bincore/Microsoft.CodeAnalysis.dll</HintPath></Reference></ItemGroup></Project>''')
        source = self.write('Source & space/Managed/Probe/Sub Dir/A.cs', 'class A : Base {}')
        self.write('Source & space/Managed/Probe/obj/Generated.cs', 'class Generated {}')
        build = self.root / 'Build & space'
        def refresh():
            generate(self.root / 'Source & space', build, roslyn_directory='C:/SDK & space/Roslyn')
        refresh()
        ide = build / 'Ide/Managed'
        manifest = json.loads((ide / 'Projects.json').read_text())
        probe = next(entry for entry in manifest if entry['name'] == 'Probe')
        # Golden value from the original .NET Guid(byte[]) implementation.
        self.assertEqual(probe['guid'], 'D6A77C91-1D55-024B-ECC0-A65EDD7CA620')
        xml = ET.parse(probe['path']).getroot()
        self.assertEqual(xml.find('.//AllowUnsafeBlocks').text, 'true')
        self.assertEqual(xml.find('.//HintPath').text, 'C:/SDK & space/Roslyn/Microsoft.CodeAnalysis.dll')
        self.assertEqual(xml.find('.//Compile').attrib, dict(Include=source.as_posix(), Link='Sub Dir/A.cs'))
        self.assertEqual(xml.find('.//ProjectReference').attrib['Include'], (ide / 'Base/Base.csproj').as_posix())
        self.assertIn('keep build metadata', Path(probe['path']).read_text())
        before = {file: (file.read_bytes(), file.stat().st_mtime_ns) for file in ide.rglob('*') if file.is_file()}
        refresh()
        self.assertEqual(before, {file: (file.read_bytes(), file.stat().st_mtime_ns) for file in before})
        added = self.write('Source & space/Managed/Probe/Added.cs', 'class Added {}')
        refresh()
        self.assertIn('Added.cs', Path(probe['path']).read_text())
        added.unlink()
        refresh()
        self.assertEqual(before[Path(probe['path'])][0], Path(probe['path']).read_bytes())
        self.assertEqual(manifest, json.loads((ide / 'Projects.json').read_text()))

    def test_unknown_reference_fails(self):
        self.write('Source/Managed/Probe/Probe.csproj', '<Project><ItemGroup><ProjectReference Include="Missing.csproj" /></ItemGroup></Project>')
        with self.assertRaisesRegex(RuntimeError, 'Unknown project reference'):
            generate(self.root / 'Source', self.root / 'Build')

    def test_slnx_and_legacy_sln_map_to_the_same_projects(self):
        self.write('Solution/UrsaEngine.sln', '''Project("{2150E333-8FDC-42A3-9474-1A3956D46DE8}") = "Managed", "Managed", "{A}"
EndProject
Project("{2150E333-8FDC-42A3-9474-1A3956D46DE8}") = "Framework", "Framework", "{B}"
EndProject
Project("{9A19103F-16F7-4668-BE54-9A1E7A4F7556}") = "Probe", "Managed/Probe.csproj", "{C}"
EndProject
GlobalSection(NestedProjects) = preSolution
{B} = {A}
{C} = {B}
EndGlobalSection
''')
        legacy = read_projects(self.root / 'Solution')['Probe']
        self.assertEqual(legacy['folder'], 'Managed/Framework')
        self.write('Solution/UrsaEngine.slnx', '<Solution><Folder Name="/Managed/Framework/"><Project Path="Managed/Probe.csproj"><Platform Project="Any CPU" /></Project></Folder></Solution>')
        current = read_projects(self.root / 'Solution')['Probe']
        self.assertEqual(current['path'], legacy['path'])
        self.assertEqual(current['folder'], legacy['folder'])
        self.assertIsNotNone(current['node'].find("Platform[@Project='Any CPU']"))

    def test_bat_help_and_invalid_arguments_from_another_directory(self):
        result = self.bat('--help')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--project-dir', result.stdout)
        self.assertNotEqual(self.bat('--unknown').returncode, 0)
        result = self.bat('--project-dir', self.root / 'Missing project')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Project Assets directory is missing', result.stderr)

    def test_explicit_missing_python_is_reported(self):
        env = dict(os.environ, URSA_PYTHON=str(self.root / 'Missing Python/python.exe'))
        result = self.bat('--help', env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Python 3.11+', result.stdout)

    def test_engine_project_environment_and_argument_precedence(self):
        engine = self.root / 'Engine checkout with spaces'
        self.write('Engine checkout with spaces/CMakeLists.txt', '# engine fixture')
        self.write('Engine checkout with spaces/ThirdParty/tracy/CMakeLists.txt', '# dependency fixture')
        project = self.root / 'Project with spaces'
        self.write('Project with spaces/Assets/Probe.cs', 'class Probe {}')
        capture = self.root / 'Configure arguments.txt'
        cmake = self.write('Capture cmake.cmd', '@echo off\nif "%~1"=="--version" (\n echo cmake version 4.4.5\n exit /b 0\n)\n> "' + str(capture) + '" echo %*\nexit /b 42\n')
        env = dict(os.environ, URSA_ENGINE_DIR=str(engine), URSA_PROJECT_DIR=str(project))
        result = self.bat('--cmake', cmake, env=env)
        self.assertEqual(result.returncode, 42, result.stdout + result.stderr)
        args = capture.read_text()
        self.assertIn(str(engine), args)
        self.assertIn(str(engine / 'Build/VS2026'), args)
        self.assertIn('-DURSA_ENGINE_DIR=' + engine.as_posix(), args)
        self.assertIn('-DURSA_PROJECT_DIR=' + project.as_posix(), args)
        env.update(URSA_ENGINE_DIR=str(self.root / 'Missing engine'), URSA_PROJECT_DIR=str(self.root / 'Missing project'))
        result = self.bat('--cmake', cmake, '--engine-dir', engine, '--project-dir', project,
                          '--build-dir', self.root / 'Selected build', env=env)
        self.assertEqual(result.returncode, 42, result.stdout + result.stderr)
        args = capture.read_text()
        self.assertIn(str(self.root / 'Selected build'), args)
        self.assertNotIn('Missing engine', args)
        self.assertNotIn('Missing project', args)

    def test_missing_engine_is_reported(self):
        result = self.bat('--engine-dir', self.root / 'Missing engine')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Engine CMakeLists.txt is missing', result.stderr)

    def test_managed_debugger_defaults_preserve_explicit_user_choices(self):
        toolchain = get_toolchain()
        msbuild = Path(toolchain['VisualStudio']) / 'MSBuild/Current/Bin/MSBuild.exe'
        user = self.write('CppUser/Microsoft.Cpp.x64.user.props', '<Project><PropertyGroup><PreservedSetting>kept</PreservedSetting></PropertyGroup></Project>')
        project = ET.Element('Project', ToolsVersion='Current')
        group = ET.SubElement(project, 'PropertyGroup')
        ET.SubElement(group, 'Platform').text = 'x64'
        ET.SubElement(group, 'UserRootDir').text = str(user.parent) + os.sep
        ET.SubElement(group, 'LocalDebuggerDebuggerType').text = 'Auto'
        ET.SubElement(project, 'Import', Project=str(ENGINE / 'CMake/ManagedDebugger.props'))
        path = self.root / 'Debugger.vcxproj'
        def inspect():
            path.write_text(ET.tostring(project, encoding='unicode'), encoding='utf-8')
            result = subprocess.run([str(msbuild), str(path), '-nologo', '-getProperty:LocalDebuggerDebuggerType,PreservedSetting'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads(result.stdout)['Properties']
        self.assertEqual(inspect(), dict(LocalDebuggerDebuggerType='NativeWithManagedCore', PreservedSetting='kept'))
        user.write_text('<Project><PropertyGroup><PreservedSetting>kept</PreservedSetting><LocalDebuggerDebuggerType>NativeOnly</LocalDebuggerDebuggerType></PropertyGroup></Project>', encoding='utf-8')
        self.assertEqual(inspect()['LocalDebuggerDebuggerType'], 'NativeOnly')
        user.write_text('<Project><PropertyGroup><PreservedSetting>kept</PreservedSetting></PropertyGroup></Project>', encoding='utf-8')
        ET.SubElement(ET.SubElement(project, 'PropertyGroup'), 'LocalDebuggerDebuggerType').text = 'NativeOnly'
        self.assertEqual(inspect()['LocalDebuggerDebuggerType'], 'NativeOnly')

    def test_old_cache_is_preserved(self):
        cache = self.write('Old build/CMakeCache.txt', 'CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n')
        before = cache.read_bytes()
        with self.assertRaisesRegex(RuntimeError, 'Incompatible build cache'):
            get_toolchain(build_directory=cache.parent)
        self.assertEqual(cache.read_bytes(), before)

    def test_old_cmake_is_rejected(self):
        cmake = self.write('Old cmake/cmake.cmd', '@echo off\necho cmake version 3.28.0\n')
        with self.assertRaisesRegex(RuntimeError, '4.4.5 or newer'):
            get_toolchain(cmake)

    def test_failed_configure_returns_child_status_and_does_not_publish(self):
        cmake = self.write('Fake cmake/cmake.cmd', '@echo off\nif "%~1"=="--version" (\n echo cmake version 4.4.5\n exit /b 0\n)\nexit /b 42\n')
        self.write('Game with spaces/Assets/Probe.cs', 'class Probe {}')
        build = self.root / 'Build with spaces'
        result = self.bat('--cmake', cmake, '--project-dir', self.root / 'Game with spaces', '--build-dir', build)
        self.assertEqual(result.returncode, 42, result.stdout + result.stderr)
        self.assertFalse((build / 'UnifiedWorkspace.json').exists())


if __name__ == '__main__':
    unittest.main()
