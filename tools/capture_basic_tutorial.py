"""Review the real introductory course and accepted operation tutorial.

Uses the production course, HP, timing, aim friction, lock acquisition, weapons
and vehicle simulation. Opt-in native input drives aim/hold/release and E/Q;
no kills, learned flags, speed changes or success events are injected.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LABELS = ['primary-prompt', 'primary-confirmed', 'lock-hold', 'lock-sweep',
          'lock-release', 'lock-confirmed', 'accelerator-prompt',
          'accelerator-confirmed', 'brake-prompt', 'brake-confirmed', 'brake-contact-sparks']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stages', nargs='+', type=int, choices=range(len(LABELS)), default=list(range(10)))
    args = parser.parse_args()
    out = ROOT / 'generated/basic-tutorial-review'
    work = out / 'native'
    work.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / 'Resources', work / 'Resources', dirs_exist_ok=True)
    for name in ['GE3.exe', 'dxcompiler.dll', 'dxil.dll']:
        shutil.copy2(ROOT / 'generated/attack-warning/Release' / name, work / name)
    build = hashlib.sha256((work / 'GE3.exe').read_bytes()).hexdigest()
    manifest = out / 'native-verification.json'
    previous = json.loads(manifest.read_text()) if manifest.exists() else []
    evidence = {item['stage']: item for item in previous if item['build_sha256'] == build}
    for stage in args.stages:
        label = LABELS[stage]
        env = {k: v for k, v in os.environ.items() if not k.startswith('GE3_')}
        env.update(GE3_SUNFALL_PREVIEW_DISTANCE='210', GE3_SUNFALL_PREVIEW_FREEZE='0',
                   GE3_BASIC_TUTORIAL_AUDIT='1', GE3_BASIC_TUTORIAL_CAPTURE_STAGE=str(stage),
                   GE3_COURSE_FRAME_EXPORT=str(out / f'{label}.png'), GE3_COURSE_FRAME_EXPORT_FRAMES='1')
        result = subprocess.run([str(work / 'GE3.exe')], cwd=work, env=env, timeout=105)
        log = work / 'logs/basic_tutorial_audit.csv'
        shutil.copy2(log, out / f'{label}-telemetry.csv')
        rows = list(csv.DictReader(log.open()))
        last = rows[-1]
        assert result.returncode == 0 and (out / f'{label}.png').exists(), (label, last)
        assert float(last['hp']) > 0 and last['visible'] == '1', (label, last)
        assert last['success'] == str(int(stage in [1, 5, 7, 9])), (label, last)
        if stage >= 6:
            assert int(last['learned']) & 6 == 6, 'speed lessons follow actual primary and lock mastery'
            assert 680 <= float(last['distance']) < 1080, 'speed lesson fits the authored runway'
        if stage == 7:
            assert float(last['speed']) > float(last['normal']) * 1.03
        if stage == 9:
            assert int(last['learned']) == 30 and float(last['speed']) < float(last['normal']) * .92
            assert any(int(r['shots']) > 0 for r in rows)
            assert any(int(r['volley']) >= 2 for r in rows)
            braking=[r for r in rows if r['brake']=='1']
            assert braking and max(float(r['flash']) for r in braking)>.35, last
            assert all(r['spark_births']=='0' for r in rows if r['brake']=='0'),last
        if stage == 10:
            assert last['brake']=='1' and int(last['sparks'])>=4,last
        evidence[label] = dict(stage=label, build_sha256=build, last=last,
                               actual_primary_shots=sum(int(r['shots']) for r in rows),
                               actual_lock_shots=sum(int(r['volley']) for r in rows))
        manifest.write_text(json.dumps(list(evidence.values()), indent=2), encoding='utf-8')
        print(json.dumps(evidence[label]), flush=True)


if __name__ == '__main__':
    main()
