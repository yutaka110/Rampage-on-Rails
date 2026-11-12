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
import math
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LABELS = ['primary-prompt', 'primary-confirmed', 'lock-hold', 'lock-sweep',
          'lock-release', 'lock-confirmed', 'accelerator-prompt',
          'accelerator-confirmed', 'brake-prompt', 'brake-confirmed', 'brake-contact-sparks',
          'combat-battery-recoil', 'live-battery-coast', 'live-battery-brake',
          'live-combat-to-carrier', 'live-battery-muzzle-launch', 'primary-hull-contact',
          'primary-armor-contact', 'primary-kill-contact', 'cart-projectile-hit', 'cart-projectile-settle',
          'live-battery-latched', 'live-battery-shell', 'route-battery-aim', 'route-battery-shell',
          'route-battery-coast', 'route-battery-brake', 'route-battery-overview']
STAGES = dict(enumerate(LABELS))
STAGES[45] = 'speed-lessons-through-battery'
STAGES[47] = 'speed-lessons-after-runway'
STAGES[48] = 'lock-then-speed-lessons'
STAGES[49] = 'primary-clear-then-speed-lessons'
STAGES[50] = 'pass-then-speed-lessons'


def first_battery_fixture(course):
    rows = []
    for line in course.splitlines():
        parts = line.split('|')
        if parts[0] == 'wave' and parts[1] != 'short-mothership':
            continue
        if parts[0] in ('enemy_placement', 'encounter_beat') and parts[4] != 'short-mothership':
            continue
        if parts[0] == 'event' and parts[2] == 'enemy_wave' and parts[3] != 'short_turret_left':
            continue
        rows.append(line)
    return '\n'.join(rows)+'\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stages', nargs='+', type=int, choices=STAGES, default=list(range(10)))
    parser.add_argument('--exe', type=Path, default=ROOT / 'generated/attack-warning/Release/GE3.exe')
    parser.add_argument('--speed-delay-ms', type=int, default=1000)
    parser.add_argument('--brake-delay-ms', type=int, default=450)
    parser.add_argument('--battery-event', type=int, choices=(1455, 2060, 2840), default=1455)
    parser.add_argument('--battery-start', type=int)
    parser.add_argument('--battery-approach-brake', type=int, nargs=2, metavar=('FROM', 'UNTIL'))
    parser.add_argument('--battery-escort-stop', type=int)
    parser.add_argument('--output', type=Path, default=ROOT / 'generated/basic-tutorial-review')
    args = parser.parse_args()
    out = args.output.resolve()
    work = out / 'native'
    work.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / 'Resources', work / 'Resources', dirs_exist_ok=True)
    shutil.copy2(args.exe, work / 'GE3.exe')
    for name in ['dxcompiler.dll', 'dxil.dll']:
        shutil.copy2(ROOT / 'generated/attack-warning/Release' / name, work / name)
    build = hashlib.sha256((work / 'GE3.exe').read_bytes()).hexdigest()
    course_path = work / 'Resources/courses/ShortCompleteRun.course'
    production = course_path.read_bytes()
    battery_fixture = first_battery_fixture(production.decode('utf-8-sig')).encode('utf-8')
    production_hash = hashlib.sha256(production).hexdigest()
    battery_hash = hashlib.sha256(battery_fixture).hexdigest()
    manifest = out / 'native-verification.json'
    previous = json.loads(manifest.read_text()) if manifest.exists() else []
    evidence = {
        item['stage']: item for item in previous
        if item['build_sha256'] == build and item.get('course_sha256') ==
        (battery_hash if item['stage'] == LABELS[11] else production_hash)
    }
    for stage in args.stages:
        label = STAGES[stage]
        screenshot = out / f'{label}.png'
        screenshot.unlink(missing_ok=True)
        course_path.write_bytes(battery_fixture if stage == 11 else production)
        latch = work / 'logs/rail_siege_turret_latch.csv'
        latch.unlink(missing_ok=True)
        course = battery_hash if stage == 11 else production_hash
        env = {k: v for k, v in os.environ.items() if not k.startswith('GE3_')}
        start = 1200 if 11 <= stage <= 27 else 210
        if stage == 47:
            start = 1210
        if 23 <= stage <= 27:
            start = args.battery_start or args.battery_event - 20
            env['GE3_ROUTE_BATTERY_EVENT'] = str(args.battery_event)
            if args.battery_approach_brake:
                env['GE3_ROUTE_BRAKE_FROM'], env['GE3_ROUTE_BRAKE_UNTIL'] = map(str, args.battery_approach_brake)
            if args.battery_escort_stop:
                env['GE3_ROUTE_ESCORT_STOP'] = str(args.battery_escort_stop)
            (work / 'logs/route_battery_hud.csv').unlink(missing_ok=True)
        env.update(GE3_SUNFALL_PREVIEW_DISTANCE=str(start), GE3_SUNFALL_PREVIEW_FREEZE='0',
                   GE3_BASIC_TUTORIAL_AUDIT='1', GE3_BASIC_TUTORIAL_CAPTURE_STAGE=str(stage),
                   GE3_BASIC_TUTORIAL_SPEED_DELAY_MS=str(args.speed_delay_ms),
                   GE3_BASIC_TUTORIAL_BRAKE_DELAY_MS=str(args.brake_delay_ms),
                   GE3_CART_DAMAGE_AUDIT=str(int(stage in (13,19,20))),
                   GE3_COURSE_FRAME_EXPORT=str(screenshot), GE3_COURSE_FRAME_EXPORT_FRAMES='1')
        result = subprocess.run([str(work / 'GE3.exe')], cwd=work, env=env, timeout=105)
        log = work / 'logs/basic_tutorial_audit.csv'
        shutil.copy2(log, out / f'{label}-telemetry.csv')
        rows = list(csv.DictReader(log.open()))
        last = rows[-1]
        assert result.returncode == 0 and screenshot.exists(), (label, last)
        assert float(last['hp']) > 0 and (stage >= 11 or last['visible'] == '1'), (label, last)
        if stage not in (10, 45):
            assert last['success'] == str(int(stage in [1, 5, 7, 9, 47, 48, 49, 50])), (label, last)
        if 6 <= stage <= 10:
            assert float(last['distance']) >= 630, 'speed lessons begin on the authored runway'
        if stage in (6, 7):
            assert int(last['learned']) & 16 and last['reward_boost'] == '0', last
        if stage == 7:
            assert float(last['speed']) > float(last['normal']) * 1.03
        if stage == 47:
            brake = next(r for r in rows if r['lesson']=='4' and r['success']=='1')
            assert float(brake['distance']) >= 1200 and int(brake['learned']) & 24 == 16, brake
            assert any(r['lesson']=='3' and r['visible']=='1' and r['threat']=='1' for r in rows), last
            assert last['lesson']=='3' and int(last['learned']) & 24 == 24, last
        if 48 <= stage <= 50:
            brake = next(i for i, r in enumerate(rows) if r['lesson']=='4' and r['visible']=='1')
            assert any(r['lesson']=='2' and r['visible']=='1' and r['success']=='0' for r in rows[:brake]), label
            assert rows[brake]['lock_target_available']=='0' or stage==48, rows[brake]
            assert last['lesson']=='3' and int(last['learned']) & 24 == 24 and last['reward_boost']=='0', last
            assert any(r['lesson']=='4' and r['success']=='1' for r in rows[brake:]), label
            if stage == 48:
                launch = next(i for i, r in enumerate(rows) if r['lesson']=='2' and r['success']=='1')
                assert float(rows[launch]['distance']) >= 630 and int(rows[launch]['volley']) > 0, rows[launch]
                assert all(r['lesson']=='2' and r['visible']=='1' and r['success']=='1' for r in rows[launch:brake])
                elapsed = sum(float(r['dt']) for r in rows[launch:brake])
                assert 1.18 <= elapsed <= 1.30, elapsed
            else:
                assert all(r['volley']=='0' for r in rows) and int(last['learned']) & 4 == 0, last
                formation = [r for r in rows[:brake] if int(r['lock_formation_alive'])>0]
                assert formation, label
                if stage == 49:
                    assert any(r['lesson']=='2' and int(r['shots'])>0 for r in formation), label
                    assert int(formation[-1]['reward_kills']) > int(formation[0]['reward_kills']), label
                else:
                    assert any(r['lesson']=='2' and r['lock_target_available']=='1' and
                               float(r['distance'])>=630 for r in formation), label
                    assert all(r['shots']=='0' for r in formation if r['lesson']=='2'), label
        if stage in (9, 45):
            prompt = next(i for i, r in enumerate(rows) if r['lesson']=='4' and r['visible']=='1')
            pressed = next(i for i in range(prompt, len(rows)) if rows[i]['brake']=='1')
            confirmed = next(i for i in range(pressed, len(rows)) if rows[i]['lesson']=='4' and rows[i]['success']=='1')
            assert prompt < pressed < confirmed, (label, last)
            assert all(r['lesson']=='4' and r['visible']=='1' for r in rows[prompt:confirmed+1]), (label, last)
            assert int(rows[confirmed]['learned']) & 24 == 16, rows[confirmed]
            assert float(rows[confirmed]['speed']) < float(rows[confirmed]['normal']) * .92, rows[confirmed]
            if stage == 45:
                reward_start = next(i for i, r in enumerate(rows) if r['reward_boost']=='1')
                reward_end = next(i for i in range(reward_start+1, len(rows)) if rows[i]['reward_boost']=='0')
                accelerator = next(i for i, r in enumerate(rows) if r['lesson']=='3' and r['visible']=='1')
                acceleration = next(i for i in range(accelerator, len(rows))
                                    if rows[i]['lesson']=='3' and rows[i]['success']=='1')
                assert reward_start <= confirmed < reward_end <= accelerator < acceleration, (label, last)
                assert int(rows[confirmed]['reward_kills']) >= 6, rows[confirmed]
                assert rows[acceleration]['reward_boost']=='0' and rows[acceleration]['boost']=='1', rows[acceleration]
                assert int(rows[acceleration]['learned']) & 24 == 24, rows[acceleration]
                assert last['battery_owner']!='0' and last['visible']=='0', last
                contact = out / f'{label}-contacts.csv'
                shutil.copy2(work / 'logs/rail_primary_contact.csv', contact)
                with contact.open() as stream:
                    hits = list(csv.DictReader(stream))
                assert any(h['actor']==last['battery_owner'] and h['shape']=='kill' and
                           int(h['frame'])>int(rows[confirmed]['frame']) and float(h['damage'])>0
                           for h in hits), (label, hits[-8:])
        if stage == 10:
            assert last['brake']=='1' and int(last['sparks'])>=4,last
        if stage == 11:
            assert float(last['battery_recoil'])>.57,last
        if stage == 15:
            with (out / f'{label}.parts.csv').open() as parts:
                points = list(csv.DictReader(parts))
            def positions(node):
                return [tuple(float(row[axis]) for axis in ('x', 'y', 'z')) for row in points if row['node']==node]
            launch, = positions('launch')
            flash, = positions('flash')
            projectile, = positions('projectile')
            rims = positions('rim')
            assert len(rims)==2 and min(math.dist(rim,launch) for rim in rims)<.005, points
            assert math.dist(flash,launch)<.005 and math.dist(projectile,launch)<.005, points
            assert last['visible']=='0', last
            audio = work / 'logs/siege_fire_audio.csv'
            shutil.copy2(audio, out / f'{label}-audio.csv')
            with audio.open() as cues:
                cue, = csv.DictReader(cues)
            assert cue['frame']==last['frame'] and cue['played']=='1' and float(cue['gain'])>.20, cue
            with latch.open() as cues:
                frame, actor, gain, played = next(csv.reader(cues))
            assert actor == cue['actor'] and int(frame)<int(cue['frame']) and float(gain)>0 and played=='1', (frame, actor, gain, played, cue)
            shutil.copy2(latch, out / f'{label}-latch.csv')
        if stage == 21:
            assert last['battery_onscreen']=='1' and float(last['live_shot_age'])<0, last
        if stage == 22:
            assert last['battery_onscreen']=='1' and float(last['battery_recoil'])>.49, last
        if 23<=stage<=27:
            shutil.copy2(work / 'logs/route_battery_fire.csv', out / f'{label}-fire.csv')
        if stage==27:
            shutil.copy2(work / 'logs/route_battery_hud.csv', out / f'{label}-hud.csv')
            assert any(r['battery_owner']!='0' for r in rows), last
        if 23<=stage<=26:
            hud = work / 'logs/route_battery_hud.csv'
            shutil.copy2(hud, out / f'{label}-hud.csv')
            with hud.open() as stream:
                cues = list(csv.DictReader(stream))
            owner = next(r['battery_owner'] for r in rows if r['battery_owner']!='0')
            assert owner != '0' and all(r['visible']=='0' for r in rows if float(r['distance'])>=1200), last
            aim = [r for r in cues if r['aim_actor']==owner]
            shell = [r for r in cues if r['owner']==owner and r['shell']!='0']
            assert aim, (label, owner, cues)
            if args.battery_event==2840 and stage in (23,24):
                assert any(r['frame']==last['frame'] and int(r['visible_hostiles'])>=2
                           for r in (aim if stage==23 else shell)), (label, last)
            if stage==23:
                assert any(r['frame']==last['frame'] for r in aim), (label, last, aim[-1])
            else:
                assert shell and int(aim[0]['frame'])<int(shell[0]['frame']), (label, owner, cues)
                shot = shell[0]['shell']
                launched = [r for r in rows if r['battery_shell']==shot]
                assert launched and float(launched[0]['shell_age'])<.07, (label, shot)
                assert math.isclose(float(launched[0]['shell_speed']), 52, abs_tol=.01), (label, launched[0])
                if stage==24:
                    assert any(r['frame']==last['frame'] for r in shell), (label, last, shell[-1])
                if stage==25:
                    assert sum(float(r['battery_damage']) for r in rows)>0, (label, last)
                if stage==26:
                    pressed = next(r for r in rows if r['brake']=='1' and int(r['frame'])>int(launched[0]['frame']))
                    assert int(pressed['frame'])>int(launched[0]['frame']) and float(pressed['shell_age'])>=args.brake_delay_ms*.001
                    assert any(r['brake']=='1' and r['battery_shell']==shot and float(r['live_crossing_gap'])>2
                               for r in rows), (label, last)
                    assert all(float(r['battery_damage'])==0 for r in rows), (label, last)
        if 12 <= stage <= 14:
            live = [r for r in rows if float(r['distance']) >= 1200]
            assert all(r['visible']=='0' for r in live), label
            launch = next(i for i,r in enumerate(live) if float(r['live_shot_age'])>=0)
            assert 1200 < float(live[launch]['distance']) < 1320, live[launch]
            assert any(r['battery_onscreen']=='1' and float(r['battery_recoil'])>.36
                       for r in live[launch:] if float(r['live_shot_age'])<.2), live[launch]
            crossing = [r for r in live[launch:] if float(r['live_shot_age'])<1.5]
            if stage == 12:
                assert min(float(r['hp']) for r in crossing)<float(live[launch]['hp']), crossing[-1]
                assert all(float(r['brake_mix'])==1 for r in live), 'coasting must not duck friction'
            else:
                brake = next(r for r in crossing if r['brake']=='1')
                assert float(brake['live_shot_age'])>=args.brake_delay_ms*.001, brake
                assert max(float(r['live_crossing_gap']) for r in crossing)>2, crossing[-1]
                assert all(r['hp']==live[launch]['hp'] for r in crossing), crossing[-1]
                dips=[i for i,r in enumerate(crossing) if float(r['brake_mix'])<1]
                assert dips and dips==list(range(dips[0],dips[-1]+1)), 'one contiguous duck for the clear pass'
                onset=crossing[dips[0]]
                assert float(onset['live_crossing_gap'])>2 and float(onset['brake_mix'])<.35 and int(onset['pass_projectile'])>0, onset
                assert sum(float(crossing[i]['dt']) for i in dips[1:])<=.15 and dips[-1]<len(crossing)-1, dips
            if stage == 14:
                assert live[launch]['boost']=='1', live[launch]
                for begin, end in ((1450, 2480), (2820, 3270)):
                    combat = [r for r in live if begin<=float(r['distance'])<end]
                    assert any(int(r['shots'])>0 for r in combat), (begin, combat[-1])
                    assert any(int(r['lock_hits'])>0 and float(r['speed'])>float(r['normal'])*1.03
                               for r in combat), (begin, combat[-1])
        if 16<=stage<=18:
            contact=work / 'logs/rail_primary_contact.csv'
            shutil.copy2(contact, out / f'{label}-audio.csv')
            with contact.open() as stream:
                hits=list(csv.DictReader(stream))
            wanted={16:'hull', 17:'plate', 18:'kill'}[stage]
            assert any(h['shape']==wanted and h['played']=='1' and 1200<float(h['distance'])<1500 and float(h['damage'])>0
                       for h in hits), (label, hits[-4:])
        if stage in (13,19,20):
            cart=out / f'{label}-damage.csv'
            shutil.copy2(work / 'logs/cart_damage_feel.csv',cart)
            with cart.open() as stream:
                reactions=list(csv.DictReader(stream))
            impacts=[r for r in reactions if r['hit']]
            if stage==13:
                assert not impacts and all(int(r['particles'])==0 for r in reactions), impacts
            else:
                assert len(impacts)==1 and impacts[0]['hit']=='projectile' and impacts[0]['audio']=='1', impacts
                assert int(impacts[0]['particles'])>0 and float(impacts[0]['hp'])<100 and 1200<float(impacts[0]['distance'])<1320, impacts
                if stage==20:
                    assert min(float(r['heave']) for r in reactions)<-.02, reactions[-4:]
        evidence[label] = dict(stage=label, build_sha256=build, course_sha256=course, last=last,
                               speed_delay_ms=args.speed_delay_ms, brake_delay_ms=args.brake_delay_ms,
                               actual_primary_shots=sum(int(r['shots']) for r in rows),
                               actual_lock_shots=sum(int(r['volley']) for r in rows))
        if 23<=stage<=27:
            evidence[label].update(battery_event=args.battery_event, start_distance=start,
                                   approach_brake=args.battery_approach_brake,
                                   escort_fire_stop=args.battery_escort_stop)
        manifest.write_text(json.dumps(list(evidence.values()), indent=2), encoding='utf-8')
        print(json.dumps(evidence[label]), flush=True)


if __name__ == '__main__':
    main()
