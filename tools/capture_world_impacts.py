"""Capture real primary-shot surface contacts at the normal camera and speed."""
from pathlib import Path
import csv
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'generated/lock-practice-departure/Release'
OUT = ROOT / 'generated/world-impact-review'
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0

for name, mode, expected in [('sand', 1, 'sand'), ('rock', 2, 'rock'), ('prop', 3, 'rock'), ('burst', 4, 'sand'),
                             ('arch', 5, 'rock'), ('pillar', 6, 'rock')]:
    if len(sys.argv) > 1 and name not in sys.argv[1:]:
        continue
    folder = OUT / name
    folder.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('GE3_')}
    env.update(GE3_SUNFALL_PREVIEW_DISTANCE='420' if mode == 5 else '580' if mode == 6 else '2130', GE3_SUNFALL_PREVIEW_FREEZE='0',
        GE3_NORMAL_TRAVEL_AUDIT='1', GE3_WORLD_IMPACT_AUDIT='1', GE3_WORLD_IMPACT_REVIEW=str(mode),
        GE3_COURSE_FRAME_EXPORT=str(folder / 'impact.png'), GE3_COURSE_FRAME_EXPORT_FRAMES='1')
    print('Capturing', name, flush=True)
    result = subprocess.run([str(WORK / 'GE3.exe')], cwd=WORK, env=env, startupinfo=startup, timeout=70)
    for log in ['world_impact_contacts.csv', 'world_impact_draw.csv', 'world_impact_review.csv',
                'course_frame_export.log', 'shader_compile_errors.log', 'app_pipelines_error.log', 'scenery_impact_rays.csv',
                'world_impact_queue.csv']:
        f = WORK / 'logs' / log
        if f.exists():
            shutil.copy2(f, folder / log)
    assert result.returncode == 0, result.returncode
    for log in ['shader_compile_errors.log', 'app_pipelines_error.log']:
        assert not (folder / log).stat().st_size, (folder / log).read_text()
    rows = list(csv.DictReader((folder / 'world_impact_review.csv').open()))
    contacts = list(csv.DictReader((folder / 'world_impact_contacts.csv').open()))
    draws = list(csv.DictReader((folder / 'world_impact_draw.csv').open()))
    assert len(rows) == 8 and all((folder / f'impact-{i}.png').exists() for i in range(8)), rows
    assert rows[0]['kind'] == expected and rows[-1]['kind'] == 'expired', rows
    assert all(15 < float(r['speed']) < 70 and int(r['marks']) <= 8 for r in rows), rows
    assert contacts and all(float(r['age']) >= float(r['travel']) for r in contacts), contacts
    assert draws and all(1 <= int(r['slots']) <= 6 for r in draws), draws[-1:]
    # The early pillar is inside the passive lock-on lesson. The arch and the
    # regular combat fixtures retain live enemy fire for readability checks.
    if mode != 6:
        assert any(int(r['projectiles']) > 0 for r in rows), rows
    if mode in (5, 6):
        rays = list(csv.DictReader((folder / 'scenery_impact_rays.csv').open()))
        mesh = 'organic_arch_large' if mode == 5 else 'root_spire_column'
        assert any(r['mesh'] == mesh and r['hit'] == '1' and abs(float(r['limit']) - 150) < .01 for r in rays), rays
        queue = list(csv.DictReader((folder / 'world_impact_queue.csv').open()))
        assert queue and all(r['kind'] == '3' and r['pending'] == '1' for r in queue), queue
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        str(ROOT / 'tools/check_world_impact_pixels.ps1'), '-Folder', str(folder)], check=True)
    print(name, 'PASS', 'contacts=', len(contacts), 'max_patches=', max(int(r['marks']) for r in rows),
        'speed=', rows[0]['speed'], 'frames=', len(rows), flush=True)
