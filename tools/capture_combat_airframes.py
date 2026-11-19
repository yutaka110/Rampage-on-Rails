"""Review production airframes under the ordinary moving gameplay camera."""
from pathlib import Path
import csv
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'generated/lock-practice-departure/Release'
OUT = ROOT / 'generated/combat-airframe-review'
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0

for name, mode in [('flight', 1), ('sniper-hit', 2), ('interceptor-hit', 3), ('wing-hit', 4)]:
    if len(sys.argv) > 1 and name not in sys.argv[1:]:
        continue
    folder = OUT / name
    folder.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('GE3_')}
    env.update(GE3_SUNFALL_PREVIEW_DISTANCE='2130', GE3_SUNFALL_PREVIEW_FREEZE='0',
        GE3_NORMAL_TRAVEL_AUDIT='1', GE3_AIRFRAME_REVIEW=str(mode),
        GE3_COURSE_FRAME_EXPORT=str(folder / 'airframes.png'), GE3_COURSE_FRAME_EXPORT_FRAMES='1')
    print('Capturing', name, flush=True)
    result = subprocess.run([str(WORK / 'GE3.exe')], cwd=WORK, env=env,
        startupinfo=startup, timeout=90)
    for log in ['airframe_review.csv', 'course_frame_export.log', 'shader_compile_errors.log',
                'app_pipelines_error.log', 'course_frame_enemy_state.csv', 'airframe_surface_hits.csv']:
        src = WORK / 'logs' / log
        if src.exists():
            shutil.copy2(src, folder / log)
    assert result.returncode == 0, result.returncode
    for log in ['shader_compile_errors.log', 'app_pipelines_error.log']:
        assert not (folder / log).stat().st_size, (folder / log).read_text()
    rows = list(csv.DictReader((folder / 'airframe_review.csv').open()))
    assert all((folder / f'airframes-{i}.png').exists() for i in range(7))
    assert rows and all(r['material'] == '21' for r in rows), rows
    assert all(15 < float(r['speed']) < 70 for r in rows), rows
    for actor in ['short_sniper', 'short_interceptor']:
        samples = [r for r in rows if r['actor'] == actor]
        # Exactly one complete mesh per actor; no old pods/cores/outlines.
        assert len({r['sample'] for r in samples}) == len(samples) >= 4, samples
        assert sum(r['onscreen'] == '1' for r in samples) >= 4, samples
    # The production fire-safety gate may defer a sniper behind another ship.
    # Do not bypass it just to obtain an asset shot. The untouched flight pass
    # must still show both charge states and a real projectile launch.
    if mode == 1:
        assert all(any(float(r['charge']) > 0 for r in rows if r['actor'] == actor)
            for actor in ['short_sniper', 'short_interceptor']), rows
        assert any(int(r['fire']) > 0 for r in rows), rows
    if mode >= 2:
        actor = 'short_sniper' if mode == 2 else 'short_interceptor'
        samples = [r for r in rows if r['actor'] == actor]
        assert min(float(r['hp']) for r in samples) < float(samples[0]['hp']), samples
    if mode == 4:
        contacts = list(csv.DictReader((folder / 'airframe_surface_hits.csv').open()))
        assert any(r['mesh'] == 'combat_interceptor_hull' and float(r['x']) > 1.5
            for r in contacts), contacts
    print(name, 'PASS', len(rows), 'draw samples', flush=True)
