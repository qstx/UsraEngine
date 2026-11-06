"""Real Player processes; controls receive replayed Window events, never direct loads."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Tools'))
from WindowsAutomation import Bitmap


def run(command, cwd, env):
    result = subprocess.run(list(map(str, command)), cwd=cwd, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=600)
    (cwd / ('command-' + str(len(list(cwd.glob('command-*.log')))) + '.log')).write_text(' '.join(map(str, command)) + '\n' + result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)


def recording(direct=False):
    name = 'Ursa.Demo.Tests.OnGUITest, Ursa.Game'
    steps = []
    def expect(frame, scene, fields=None):
        state = {'scene': scene, 'buildIndex': 1 if scene == 'OnGUI' else 0, 'objects': 3}
        if fields is not None: state['behaviours'] = {name: fields}
        steps.append({'frame': frame, 'expect': state})
    def click(frame, x, y):
        steps.extend([{'frame':frame, 'events':[{'type':'Move','x':x,'y':y},{'type':'Down'}]},
                      {'frame':frame+1, 'events':[{'type':'Up'}]}])
    def select_all():
        return [{'type':'KeyDown','key':22}, {'type':'KeyDown','key':7},
                {'type':'KeyUp','key':7}, {'type':'KeyUp','key':22}]
    if direct:
        expect(1, 'OnGUI', {'clicks':0, 'visits':1})
        click(2, 60, 26)
        expect(4, 'Menu')
        return steps
    expect(1, 'Menu')
    click(2, 90, 116)
    expect(4, 'OnGUI', {'clicks':0, 'visits':1})
    click(5, 64, 228)
    expect(7, 'OnGUI', {'clicks':1})
    click(8, 230, 228)
    expect(10, 'OnGUI', {'toggle':True})
    steps.extend([{'frame':11,'events':[{'type':'Move','x':180,'y':274},{'type':'Down'}]},
                  {'frame':12,'events':[{'type':'Move','x':800,'y':274}]},
                  {'frame':13,'events':[{'type':'Up'}]}])
    expect(14, 'OnGUI', {'slider':1})
    click(15, 100, 324)
    steps.extend([{'frame':17,'events':select_all()},
                  {'frame':18,'events':[{'type':'Text','codepoint':0x1f600},{'type':'Text','codepoint':0x4e2d}]}])
    expect(19, 'OnGUI', {'clicks':1,'text':'😀中','otherText':'独立焦点'})
    click(20, 230, 180)
    expect(22, 'OnGUI', {'customCaptures':0})
    click(23, 80, 348)
    expect(25, 'OnGUI', {'dynamicActions':1})
    steps.extend([{'frame':26,'events':[{'type':'Move','x':40,'y':234},{'type':'Down'}]},
                  {'frame':27,'events':[{'type':'Move','x':900,'y':450}]},
                  {'frame':28,'events':[{'type':'Up'}]}])
    expect(29, 'OnGUI', {'customCaptures':1,'unconsumedClicks':0})
    steps.append({'frame':30,'events':[{'type':'KeyDown','key':6},{'type':'KeyUp','key':6}]})
    expect(31, 'OnGUI', {'consumedW':1})
    click(32, 400, 180)
    click(34, 100, 228)
    steps.append({'frame':36,'events':[{'type':'KeyDown','key':11},{'type':'KeyUp','key':11}]})
    steps.append({'frame':37,'events':select_all()+[{'type':'Text','codepoint':0x6587},{'type':'Text','codepoint':0x4ef6}]})
    expect(38, 'OnGUI', {'otherText':'文件','text':'😀中'})
    click(39, 175, 355)
    expect(41, 'OnGUI', {'clippedClicks':1})
    click(42, 250, 355)
    expect(44, 'OnGUI', {'clippedClicks':1})
    click(45, 230, 26)
    expect(47, 'OnGUI', {'clicks':0,'edits':0,'visits':1,'toggle':False,'slider':0.35,'text':'中文 / Unicode 😀','clippedClicks':0})
    expect(48, 'OnGUI', {'clicks':0,'text':'中文 / Unicode 😀'})
    steps[-1]['expect'].update(hasTextFocus=False,hasPointerCapture=False)
    click(49, 60, 26)
    expect(51, 'Menu')
    click(52, 90, 116)
    expect(54, 'OnGUI', {'clicks':0, 'visits':2})
    click(55, 64, 228)
    expect(57, 'OnGUI', {'clicks':1})
    click(58, 60, 26)
    expect(60, 'Menu')
    return steps


def main():
    parser = argparse.ArgumentParser()
    for name in ('player', 'package-tool', 'project', 'output'): parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--configuration', default='Debug')
    parser.add_argument('--fixture-tool', type=Path)
    args = parser.parse_args()
    args.player, args.package_tool, args.project, args.output = (path.resolve() for path in (args.player, args.package_tool, args.project, args.output))
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, URSA_USER_DATA_ROOT=str(args.output / 'user'))
    for mode in ('loose', 'pak', 'direct'):
        directory = args.output / mode
        directory.mkdir(exist_ok=True)
        path = directory / 'Replay.json'
        steps = recording(mode == 'direct')
        path.write_text(json.dumps({'format':'UrsaFrameReplay','version':1,'steps':steps}), encoding='utf-8')
        if mode == 'pak':
            package = directory / 'Content.pak'
            run([args.package_tool, '--project-root', args.project, '--configuration', args.configuration, '--output', package], directory, env)
            startup = ['--pak', package]
        else: startup = ['--project', args.project]
        if mode == 'direct': startup += ['--scene', 'Tests/OnGUI/OnGUI.ursascene']
        capture = directory / 'OnGUI.png'
        command = [args.player, *startup, '--hidden', '--frames', steps[-1]['frame'], '--replay', path]
        if mode != 'direct': command += ['--capture', capture, '--capture-frame', 19]
        run(command, directory, env)
        result = json.loads((directory / 'ReplayResult.json').read_text(encoding='utf-8'))
        assert result['injectedWindowInput'] and all(item['passed'] for item in result['evidence'])
        if mode != 'direct':
            with Bitmap(capture) as image:
                assert (image.width, image.height) == (1024, 720)
                red, green = image.pixel(470, 318), image.pixel(520, 318)
                assert red[0] > red[1] + 60 and green[1] > green[0] + 60, (red, green)
            tokens = [item['state']['token'] for item in result['evidence']]
            assert len(set(tokens)) == 6, tokens
    print('3 Player runs passed: loose and independent Pak Menu -> OnGUI -> reset -> Menu -> OnGUI -> Menu; direct startup -> Menu. Input is injected through Window subscriptions.')
    if args.fixture_tool:
        directory = args.output/'fault-pak'
        directory.mkdir(exist_ok=True)
        package = directory/'Content.pak'
        run([args.fixture_tool.resolve(), '--fault-scene-pak', args.output/'pak/Content.pak', package,
             'prj:Tests/OnGUI/OnGUI.ursascene'], directory, env)
        steps = recording()[:4]
        steps[-1]['expect'] = {'scene':'Menu','objects':3,'hasLoadError':True}
        steps.extend([{'frame':5,'events':[{'type':'Move','x':90,'y':116},{'type':'Down'}]},
                      {'frame':6,'events':[{'type':'Up'}]}, {'frame':7,'expect':{'scene':'Menu','hasLoadError':True}}])
        replay = directory/'Replay.json'
        replay.write_text(json.dumps({'format':'UrsaFrameReplay','version':1,'steps':steps}), encoding='utf-8')
        run([args.player, '--pak', package, '--hidden', '--frames', 7, '--replay', replay], directory, env)
        evidence = json.loads((directory/'ReplayResult.json').read_text(encoding='utf-8'))['evidence']
        assert len({item['state']['token'] for item in evidence}) == 1
        print('Fault Pak preserved the Menu scene and remained retryable after two real navigation-button clicks.')


if __name__ == '__main__':
    main()
