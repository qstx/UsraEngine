import argparse
import json
import os
from pathlib import Path
import subprocess
from DemoSuitePlayerTests import recording

parser = argparse.ArgumentParser()
for name in ('editor', 'project', 'output'): parser.add_argument('--'+name, type=Path, required=True)
parser.add_argument('--player-evidence', type=Path)
args = parser.parse_args()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
path = args.output / 'EditorReplay.json'
path.write_text(json.dumps({'format':'UrsaFrameReplay','version':1,'output':'EditorReplayResult.json','steps':recording()}), encoding='utf-8')
result = subprocess.run([str(args.editor.resolve()), str(args.project.resolve()), str(path), str(args.output)],
    env=dict(os.environ, URSA_USER_DATA_ROOT=str(args.output/'user')), capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=590)
(args.output/'Editor.log').write_text(result.stdout+result.stderr, encoding='utf-8')
if result.returncode: raise AssertionError(result.stdout+result.stderr)
print(result.stdout)
if args.player_evidence:
    editor = json.loads((args.output/'EditorReplayResult.json').read_text(encoding='utf-8'))
    assert editor['injectedWindowInput']
    def comparable(state):
        state = {key:state[key] for key in ('scene','buildIndex','objects','behaviours','hasTextFocus','hasPointerCapture')}
        state['behaviours'] = {name:{key:value for key,value in fields.items() if key != 'updates'}
                               for name,fields in state['behaviours'].items()}
        return state
    comparisons = []
    def equivalent(left, right):
        if type(left) in (int, float) and type(right) in (int, float):
            return abs(left-right) <= 1e-5 if float in (type(left), type(right)) else left == right
        if type(left) is not type(right): return False
        if isinstance(left, dict):
            return left.keys() == right.keys() and all(equivalent(value, right[key]) for key,value in left.items())
        if isinstance(left, list):
            return len(left) == len(right) and all(equivalent(a,b) for a,b in zip(left,right))
        return left == right
    for mode in ('loose','pak'):
        player = json.loads((args.player_evidence.resolve()/mode/'ReplayResult.json').read_text(encoding='utf-8'))
        assert player['injectedWindowInput']
        assert len(editor['evidence']) == len(player['evidence']) == 60
        for left, right in zip(editor['evidence'], player['evidence']):
            assert left['passed'] and right['passed'] and left['frame'] == right['frame']
            assert equivalent(comparable(left['state']), comparable(right['state'])), f"{mode} parity at frame {left['frame']}: {left['state']} / {right['state']}"
        comparisons.append({'mode':mode,'frames':60,'passed':True})
    (args.output/'HostParity.json').write_text(json.dumps({'injectedWindowInput':True,'comparisons':comparisons,'floatAbsoluteTolerance':1e-5,
        'excludedFields':{'updates':'Editor explicitly exercises Pause/Step; Player simulates every frame.'}},ensure_ascii=False,indent=2),encoding='utf-8')
    print('120 Editor/Player frame comparisons passed, including all GUI callback/action counts, focus and asset identities; simulation Update counts differ by the explicit Pause/Step schedule.')
