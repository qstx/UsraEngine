"""Verify project render pipeline pixels, pak parity and live capture lifecycle."""
import argparse
from pathlib import Path
import re
import shutil
import sys
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from AutomationSupport import environment, expect, read_json, run, write_json
from WindowsAutomation import Bitmap
from ProjectCaptureSamples import CaptureSamples
from WorkspaceCommon import main_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('importer', 'player', 'source', 'output-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('editor', 'package-tool', 'capture'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--configuration', required=True)
    args = parser.parse_args()
    project = args.output_root.resolve() / uuid.uuid4().hex[:12]
    (project / 'ProjectSettings').mkdir(parents=True)
    shutil.copytree(args.source / 'Assets', project / 'Assets')
    settings = dict(format='UrsaGraphicsSettings', version=1, renderPipeline=None)
    def selection(name):
        path = project / f'Assets/Rendering/{name}.ursaasset'
        match = re.search(r'"localFileId"\s*:\s*(\d+)', path.read_text(encoding='utf-8-sig'))
        expect(match, f'Missing asset local ID: {path}')
        return dict(guid=read_json(str(path) + '.meta')['guid'], localFileId=int(match[1]))
    def invoke(executable, arguments):
        try:
            text = run([executable, *arguments])
        except RuntimeError as error:
            with (project / 'Execution.log').open('a', encoding='utf-8') as stream:
                stream.write(str(error) + '\n')
            raise
        with (project / 'Execution.log').open('a', encoding='utf-8') as stream:
            stream.write(text)
        return text
    def import_content(pipeline):
        settings['renderPipeline'] = pipeline
        write_json(project / 'ProjectSettings/Graphics.json', settings)
        invoke(args.importer, ['--project', project, '--configuration', args.configuration])
    def capture(name, scene='MaterialShowcase.ursascene', pak=None):
        invoke(args.player, ['--pak' if pak else '--project', pak or project, '--scene', scene, '--hidden', '--frames', '8', '--capture', project / f'{name}.png', '--capture-frame', '5'])
    def green(pixel):
        return all(abs(a - b) <= 1 for a, b in zip(pixel, (25, 204, 76)))
    points = ((290, 350), (512, 350), (732, 350), (512, 475), (512, 283))
    with environment(URSA_USER_DATA_ROOT=project / 'UserData'):
        composite = selection('ShowcasePipeline')
        universal = selection('UniversalPipelineSample')
        import_content(None); capture('Default')
        import_content(composite); capture('Composite')
        with Bitmap(project / 'Default.png') as baseline, Bitmap(project / 'Composite.png') as output:
            expect(baseline.size == (1024, 720) and output.size == baseline.size, 'Unexpected Player target dimensions')
            expect(green(output.pixel(2, 2)), 'Project fullscreen shader omitted green frame')
            for point in points:
                expect(sum(baseline.pixel(*point)) >= 100 and sum(output.pixel(*point)) >= 100, f'Pipeline omitted showcase geometry at {point}')
        import_content(None); capture('SnakeDefault', 'Snake/Snake.ursascene')
        import_content(universal); capture('SnakeUniversal', 'Snake/Snake.ursascene')
        with Bitmap(project / 'SnakeDefault.png') as baseline, Bitmap(project / 'SnakeUniversal.png') as output:
            expect(baseline.size == output.size, 'Universal sample changed target dimensions')
            red = dark_green = dark_blue = 0
            for y in range(0, baseline.height, 3):
                for x in range(0, baseline.width, 3):
                    a, b = baseline.pixel(x, y), output.pixel(x, y)
                    red += abs(a[0] - b[0]) > 1
                    dark_green += b[1] < a[1]
                    dark_blue += b[2] < a[2]
            expect(red <= 4 and dark_green >= 100 and dark_blue >= 100, f'Universal tint mismatch: red={red}, green={dark_green}, blue={dark_blue}')
        import_content(composite)
        if args.editor:
            invoke(args.editor, ['--project', project, '--scene', 'MaterialShowcase.ursascene', '--play', '--hidden', '--frames', '80', '--capture', project / 'Editor.png', '--capture-frame', '70'])
            with Bitmap(project / 'Editor.png') as image:
                count = sum(green(image.pixel(x, y)) for y in range(0, image.height, 2) for x in range(0, image.width, 2))
                expect(count >= 100, 'Editor did not display project pipeline composite')
        import_content(selection('ComputeShowcasePipeline')); capture('Compute')
        with Bitmap(project / 'Compute.png') as image:
            for x in (290, 512, 732):
                r, g, b = image.pixel(x, 350)
                expect(b > r and b > g, 'RendererList replacement material was not applied')
            if args.package_tool:
                package = project / 'Content.pak'
                invoke(args.package_tool, ['--project-root', project, '--configuration', args.configuration, '--output', package])
                capture('ComputePak', pak=package)
                with Bitmap(project / 'ComputePak.png') as packed:
                    expect(all(image.argb(*point) == packed.argb(*point) for point in points), 'Loose/Pak compute pixels differ')
        if args.capture:
            import_content(universal)
            package = project / 'Content-Universal.pak'
            if args.package_tool:
                invoke(args.package_tool, ['--project-root', project, '--configuration', args.configuration, '--output', package])
            captures = CaptureSamples(args.capture, project, invoke)
            loose_args = ['--project', project, '--scene', 'MaterialShowcase.ursascene', '--hidden']
            captures.test(args.player, loose_args, 'LooseCapture', lifecycle=True)
            if args.package_tool:
                captures.test(args.player, ['--pak', package, '--scene', 'MaterialShowcase.ursascene', '--hidden'], 'PakCapture')
            if args.editor:
                captures.test(args.editor, loose_args, 'EditorCapture')
        print(f'Demo pipeline pixels passed; evidence: {project}')


if __name__ == '__main__':
    main_entry(main)
