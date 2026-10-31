"""Refresh the blockout preview without changing existing encounter fixtures."""
from pathlib import Path
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
PREVIEW = ROOT/'generated/course-blockout-preview'
VISUAL_KINDS = {'camera', 'camera_shot', 'cinematic_shot_set', 'terrain', 'terrain_brush', 'lighting'}


def main():
    PREVIEW.mkdir(exist_ok=True)
    shutil.copytree(ROOT/'Resources', PREVIEW/'Resources', dirs_exist_ok=True)
    exe = ROOT/'generated/attack-warning/Release/GE3.exe'
    for name in ['GE3.exe', 'dxcompiler.dll', 'dxil.dll', 'THIRD_PARTY_NOTICES.md']:
        source = exe if name == 'GE3.exe' else exe.parent/name
        shutil.copy2(source, PREVIEW/name)
    course = (ROOT/'Resources/courses/ShortCompleteRun.course').read_text(encoding='utf-8-sig')
    # The sixth shot enters after earlier encounters have been cleared. Keep a
    # separate fixture, so jumping to it cannot queue every earlier wave first.
    carrier_resources=PREVIEW/'CarrierShot/Resources'
    shutil.copytree(ROOT/'Resources',carrier_resources,dirs_exist_ok=True)
    carrier=[]
    for line in course.splitlines():
        parts=line.split('|')
        if parts[0]=='wave' and parts[1]!='short-mothership':
            continue
        if parts[0] in ('enemy_placement','encounter_beat') and parts[4]!='short-mothership':
            continue
        carrier.append(line)
    (carrier_resources/'courses/ShortCompleteRun.course').write_text('\n'.join(carrier)+'\n',encoding='utf-8')
    visual = [line for line in course.splitlines() if line.split('|')[0] in VISUAL_KINDS]
    for folder in ['attack-warning/Release', 'miniboss-reading-preview',
                   'battery-warning-preview', 'gatling-aim-preview', 'ridge-readability-preview']:
        target = ROOT/'generated'/folder
        if not target.exists():
            continue
        if (target/'GE3.exe').resolve() != exe.resolve():
            shutil.copy2(exe, target/'GE3.exe')
        for projectile in ['enemy_mothership_rapid','enemy_mothership_heavy']:
            source=ROOT/'Resources/courses/projectiles'/(projectile+'.projectile')
            shutil.copy2(source,target/'Resources/courses/projectiles'/source.name)
        shutil.copytree(ROOT/'Resources/course_meshes/CourseBlockoutBridge',
                        target/'Resources/course_meshes/CourseBlockoutBridge', dirs_exist_ok=True)
        for wave in ['ridge_dive_left','ridge_dive_right']:
            for suffix in ['.wave','.wave.meta']:
                shutil.copy2(ROOT/'Resources/courses/waves'/(wave+suffix),
                             target/'Resources/courses/waves'/(wave+suffix))
        path = target/'Resources/courses/ShortCompleteRun.course'
        fixture = path.read_text(encoding='utf-8-sig')
        if folder=='attack-warning/Release':
            path.write_text(course,encoding='utf-8')
        else:
            source_rows={}
            for line in course.splitlines():
                p=line.split('|')
                if p[0] in ('wave','encounter_beat','enemy_placement'):source_rows[(p[0],p[1])]=p
            rest=[]
            for line in fixture.splitlines():
                p=line.split('|')
                if p[0] in VISUAL_KINDS or p[0]=='event':continue
                if p[0]=='enemy_placement' and (p[0],p[1]) not in source_rows:continue
                if p[0] in ('wave','encounter_beat') and (p[0],p[1]) in source_rows:
                    updated=source_rows[(p[0],p[1])].copy()
                    if p[0]=='wave':updated[10:13]=p[10:13]
                    p=updated
                rest.append('|'.join(p))
            events=[l for l in course.splitlines() if l.startswith('event|')]
            path.write_text('\n'.join(rest+events+visual)+'\n', encoding='utf-8')
    gate_resources=PREVIEW/'GateBridge/Resources'
    shutil.copytree(ROOT/'Resources',gate_resources,dirs_exist_ok=True)
    opening={'short_fire','short_lock','short_intercept'}
    gate=[]
    for line in course.splitlines():
        p=line.split('|')
        if p[0]=='wave' and p[1] in opening:continue
        if p[0] in ('enemy_placement','encounter_beat') and p[4] in opening:continue
        gate.append(line)
    (gate_resources/'courses/ShortCompleteRun.course').write_text('\n'.join(gate)+'\n',encoding='utf-8')
    (PREVIEW/'PlayGateBridge.cmd').write_text(
        '@echo off\nsetlocal\ncd /d "%~dp0GateBridge"\n'
        'set "GE3_SUNFALL_PREVIEW_DISTANCE=1050"\nset "GE3_SUNFALL_PREVIEW_FREEZE=0"\n'
        'set "GE3_COURSE_FRAME_EXPORT="\nstart "" "%~dp0GE3.exe"\n',encoding='utf-8')
    ridge_resources=PREVIEW/'RidgeCombat/Resources'
    shutil.copytree(ROOT/'Resources',ridge_resources,dirs_exist_ok=True)
    ridge=[]
    for line in course.splitlines():
        p=line.split('|')
        if p[0]=='wave' and p[1]!='short-mothership':continue
        if p[0] in ('enemy_placement','encounter_beat') and p[4]!='short-mothership':continue
        ridge.append(line)
    (ridge_resources/'courses/ShortCompleteRun.course').write_text('\n'.join(ridge)+'\n',encoding='utf-8')
    (PREVIEW/'PlayRidgeCombat.cmd').write_text(
        '@echo off\nsetlocal\ncd /d "%~dp0RidgeCombat"\n'
        'set "GE3_SUNFALL_PREVIEW_DISTANCE=2100"\nset "GE3_SUNFALL_PREVIEW_FREEZE=0"\n'
        'set "GE3_COURSE_FRAME_EXPORT="\nstart "" "%~dp0GE3.exe"\n',encoding='utf-8')
    shots = json.loads((ROOT/'generated/course-blockout/shots.json').read_text(encoding='utf-8'))
    for i, shot in enumerate(shots, 1):
        launcher = ('@echo off\nsetlocal\ncd /d "%~dp0"\n'
                    f'set "GE3_SUNFALL_PREVIEW_DISTANCE={shot["distance"]}"\n'
                    f'set "GE3_SUNFALL_PREVIEW_FREEZE={0 if i==6 else 1}"\n'
                    'set "GE3_COURSE_FRAME_EXPORT="\nstart "" "%~dp0GE3.exe"\n')
        if i==6:
            launcher=launcher.replace('cd /d "%~dp0"','cd /d "%~dp0CarrierShot"')
        (PREVIEW/f'{i:02}-{shot["id"]}.cmd').write_text(launcher, encoding='utf-8')
    (PREVIEW/'PlayCourse.cmd').write_text(
        '@echo off\nsetlocal\ncd /d "%~dp0"\n'
        'set "GE3_SUNFALL_PREVIEW_DISTANCE="\nset "GE3_SUNFALL_PREVIEW_FREEZE="\n'
        'set "GE3_COURSE_FRAME_EXPORT="\nstart "" "%~dp0GE3.exe"\n', encoding='utf-8')
    print('Course blockout preview and five existing packages refreshed.')


if __name__ == '__main__':
    main()
