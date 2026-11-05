"""Exercise the compact local cue, real interception and early lock teaching.

Only skips preceding course waves. Boss health, projectile speed, lock rules,
damage and normal combat time are unchanged. The executable audit drives
aim/hold/release, and never injects a successful contact.
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
LABELS = ['aim', 'hold-one-lock', 'release-two-locks', 'lasers-in-flight',
          'interception-success', 'counter-hit', 'second-heavy-normal-speed', 'miss-retry', 'early-lock-guide']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stages', type=int, nargs='+', choices=range(9), default=list(range(9)))
    args = parser.parse_args()
    out = ROOT / 'generated/heavy-intercept-review'
    work = out / 'native'
    work.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / 'Resources', work / 'Resources', dirs_exist_ok=True)
    for name in ['GE3.exe', 'dxcompiler.dll', 'dxil.dll']:
        shutil.copy2(ROOT / 'generated/attack-warning/Release' / name, work / name)
    original_course = (ROOT / 'Resources/courses/ShortCompleteRun.course').read_text(encoding='utf-8-sig')
    boss_lines = []
    intro_lines = []
    for line in original_course.splitlines():
        p = line.split('|')
        if not (p[0] == 'wave' and p[1] != 'short_lock') and not (p[0] in ('enemy_placement', 'encounter_beat') and p[4] != 'short_lock') and not (p[0] == 'event' and p[2] == 'enemy_wave'):
            intro_lines.append(line)
        if p[0] == 'wave' and p[1] != 'short-mothership':
            continue
        if p[0] in ('enemy_placement', 'encounter_beat') and p[4] != 'short-mothership':
            continue
        if p[0] == 'event' and p[2] == 'enemy_wave':
            continue
        boss_lines.append(line)
    build = hashlib.sha256((work / 'GE3.exe').read_bytes()).hexdigest()
    manifest = out / 'native-verification.json'
    previous = json.loads(manifest.read_text()) if manifest.exists() else []
    evidence = {item['stage']: item for item in previous if item.get('build_sha256') == build}
    for stage in args.stages:
        label = LABELS[stage]
        (work / 'Resources/courses/ShortCompleteRun.course').write_text('\n'.join(intro_lines if stage == 8 else boss_lines) + '\n', encoding='utf-8')
        env = os.environ.copy()
        for key in list(env):
            if key.startswith(('GE3_POST_BOSS_', 'GE3_ENERGY_SHIELD_', 'GE3_KILL_BOOST_', 'GE3_SPEED_FEEL_')):
                env.pop(key)
        env.update(GE3_SUNFALL_PREVIEW_DISTANCE='390' if stage == 8 else '3290', GE3_SUNFALL_PREVIEW_FREEZE='0',
                   GE3_HEAVY_TUTORIAL_AUDIT='1', GE3_HEAVY_TUTORIAL_CAPTURE_STAGE=str(stage),
                   GE3_HEAVY_TUTORIAL_AUDIT_MISS=str(int(stage == 7)),
                   GE3_COURSE_FRAME_EXPORT=str(out / f'{label}.png'), GE3_COURSE_FRAME_EXPORT_FRAMES='1')
        if stage == 8:
            # Complete the ordinary weapon step through real input before
            # reviewing the replacement staged lock guide.
            env.update(GE3_BASIC_TUTORIAL_AUDIT='1', GE3_BASIC_TUTORIAL_CAPTURE_STAGE='2')
        result = subprocess.run([str(work / 'GE3.exe')], cwd=work, env=env, timeout=120)
        log = work / 'logs/heavy_tutorial_audit.csv'
        shutil.copy2(log, out / f'{label}-telemetry.csv')
        rows = list(csv.DictReader(log.open()))
        last = rows[-1]
        assert result.returncode == 0 and (out / f'{label}.png').exists(), (label, last)
        assert float(last['hp']) > 0, last
        if stage < 4:
            assert last['learned'] == '0' and float(last['scale']) == 1, last
        if stage == 1:
            assert last['locks'] == '1', last
        if stage in (2, 3):
            assert last['locks'] == '2', last
        if stage in (4, 5, 6):
            assert last['learned'] == '1', last
            assert any(r['step'] == '4' for r in rows), 'success must follow travelling lasers'
            success = next(r for r in rows if r['step'] == '5')
            assert 1.79 <= float(success['counter']) <= 1.81, success
            assert float(success['age']) == 0, success
        if stage == 5:
            assert float(last['counter_damage']) > 0 and float(last['scale']) == 1, last
        if stage == 6:
            assert int(last['heavy_fires']) >= 2 and float(last['scale']) == 1 and last['step'] in ('1', '2'), last
        if stage == 7:
            assert last['learned'] == '0' and float(last['scale']) == 1 and last['step'] == '7', last
        evidence[label] = dict(stage=label, build_sha256=build, last=last,
                               real_defense_seconds=sum(float(r['real_dt']) for r in rows if 1 <= int(r['step']) <= 4))
        manifest.write_text(json.dumps(list(evidence.values()), indent=2), encoding='utf-8')
        print(json.dumps(evidence[label]), flush=True)


if __name__ == '__main__':
    main()
