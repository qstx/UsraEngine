"""Validate generated build configurations and evaluated MSBuild properties."""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--dotnet', required=True)
    args = parser.parse_args()
    engine = Path(__file__).resolve().parents[2]
    ns = {'ms': 'http://schemas.microsoft.com/developer/msbuild/2003'}
    native = ET.parse(args.build_root / 'Tests/Runtime/Core/UrsaCpuProfilerTests.vcxproj').getroot()
    for config in ('Debug', 'Release', 'Publish'):
        group = next(item for item in native.findall('ms:ItemDefinitionGroup', ns)
                     if f"'{config}|x64'" in item.get('Condition', ''))
        optimization = group.find('ms:ClCompile/ms:Optimization', ns).text
        assert optimization == ('Disabled' if config == 'Debug' else 'MaxSpeed'), (config, optimization)
        definitions = group.find('ms:ClCompile/ms:PreprocessorDefinitions', ns).text
        links = group.find('ms:Link/ms:AdditionalDependencies', ns).text
        if config == 'Publish':
            assert 'URSA_ENABLE_TRACY' not in definitions and 'TracyClient' not in links
        result = subprocess.run([args.dotnet, 'msbuild', str(engine / 'Managed/Ursa.Runtime/Ursa.Runtime.csproj'),
                                 '-nologo', f'-p:Configuration={config}',
                                 '-getProperty:Optimize,UrsaEnableProfiler,DefineConstants'],
                                check=True, capture_output=True, text=True)
        properties = json.loads(result.stdout)['Properties']
        assert properties['Optimize'] == ('false' if config == 'Debug' else 'true'), properties
        assert properties['UrsaEnableProfiler'] == ('false' if config == 'Publish' else 'true'), properties
        assert ('URSA_PUBLISH' in properties['DefineConstants']) == (config == 'Publish')
        assert ('URSA_ENABLE_PROFILER' in properties['DefineConstants']) == (config != 'Publish')
    result = subprocess.run([args.dotnet, 'msbuild', str(engine / 'Managed/Ursa.Runtime/Ursa.Runtime.csproj'),
                             '-nologo', '-p:Configuration=Publish', '-p:UrsaEnableProfiler=true',
                             '-getProperty:DefineConstants'], check=True, capture_output=True, text=True)
    assert 'URSA_ENABLE_PROFILER' not in result.stdout, result.stdout
    print('Debug/Release/Publish optimization, managed symbols and Publish Tracy exclusion verified.')


if __name__ == '__main__':
    main()
