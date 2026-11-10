"""Refresh the blockout preview without changing existing encounter fixtures."""
from pathlib import Path
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
PREVIEW = ROOT/'generated/course-blockout-preview'
SHARED_COURSE_KINDS = {'rail', 'camera', 'camera_shot', 'cinematic_shot_set', 'terrain', 'terrain_brush', 'lighting', 'ride_profile'}


def main():
    PREVIEW.mkdir(exist_ok=True)
    shutil.copytree(ROOT/'Resources', PREVIEW/'Resources', dirs_exist_ok=True)
    exe = ROOT/'generated/attack-warning/Release/GE3.exe'
    for name in ['GE3.exe', 'dxcompiler.dll', 'dxil.dll', 'THIRD_PARTY_NOTICES.md']:
        source = exe if name == 'GE3.exe' else exe.parent/name
        shutil.copy2(source, PREVIEW/name)
    course = (ROOT/'Resources/courses/ShortCompleteRun.course').read_text(encoding='utf-8-sig')
    battery_events = {parts[3]: int(float(parts[1])) for line in course.splitlines()
                      if (parts := line.split('|'))[0] == 'event' and parts[4] == 'route:turret'}
    first_battery = battery_events['short_turret_left']
    second_battery = battery_events['short_turret_right']
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
    shared = [line for line in course.splitlines() if line.split('|')[0] in SHARED_COURSE_KINDS]
    for folder in ['attack-warning/Release', 'miniboss-reading-preview',
                   'battery-warning-preview', 'gatling-aim-preview', 'ridge-readability-preview']:
        target = ROOT/'generated'/folder
        if not target.exists():
            continue
        if (target/'GE3.exe').resolve() != exe.resolve():
            shutil.copy2(exe, target/'GE3.exe')
        for relative in ['Object3D.PS.hlsl','Terrain.PS.hlsl','TitleLighting.hlsli','RailHudAtlas.PS.hlsl','TitleDust.PS.hlsl','TitleTracer.PS.hlsl','RailGatlingRing.hlsli','RailKillBoostOverlay.hlsli','RailEnergyShield.hlsli','FullscreenComposite.PS.hlsl',
                         'audio/elite_fang_fire.wav','audio/elite_veil_fire.wav','audio/elite_veil_shock.wav','audio/elite_veil_charge.wav',
                         'audio/bridge_escape_break.wav','audio/bridge_escape_release.wav','audio/bridge_escape_land.wav',
                         'audio/bridge_escape_charge.wav','audio/bridge_escape_cannon.wav','audio/bridge_escape_tear.wav',
                         'enemies/ElitePursuers/BridgeEscapeRamp.obj','enemies/ElitePursuers/BridgeEscapeClamp.obj','enemies/ElitePursuers/BridgeEscapeDebris.obj',
                         'enemies/ElitePursuers/BridgeEscapeSpan.obj','enemies/ElitePursuers/BridgeEscapeLeaf.obj','enemies/ElitePursuers/BridgeEscapeLink.obj','enemies/ElitePursuers/BridgeEscapePylon.obj',
                         'audio/rail_kill_boost_release.wav',
                         'audio/rail_lock_contact.wav',
                         'audio/rail_lock_confirm.wav','audio/rail_lock_launch.wav','audio/rail_lock_flight.wav',
                         'audio/rail_pulse_fire.wav',
                         'audio/rail_primary_hull_contact.wav',
                         'audio/rail_primary_plate_contact.wav',
                         'audio/rail_primary_hull_resonance.wav',
                         'audio/rail_primary_plate_resonance.wav',
                         'effects/CombatImpactSmoke.effect',
                         'audio/rail_brake_scrape.wav','audio/rail_brake_bite.wav','audio/rail_brake_loop.wav',
                         'audio/rail_brake_release.wav','audio/rail_brake_round_pass.wav',
                         'audio/rail_siege_turret_fire.wav','audio/rail_siege_turret_latch.wav',
                         'audio/rail_energy_shield_hit.wav','audio/rail_energy_shield_break.wav',
                         'courses/actors/short_shield_scout.actor',
                         'courses/actors/short_rear_pursuer.actor',
                         'courses/actors/rear_fang.actor',
                         'courses/actors/rear_veil.actor',
                         'courses/bullet_patterns/rear_fang_burst.pattern',
                         'courses/bullet_patterns/rear_pursuit_single.pattern',
                         'courses/projectiles/enemy_fang_chase.projectile',
                         'courses/projectile_visuals/enemy_fang_chase.projectilevisual',
                         'courses/projectiles/enemy_rear_pursuit.projectile',
                         'courses/projectile_visuals/enemy_rear_pursuit.projectilevisual',
                         'courses/actors/short_turret_predictive.actor',
                         'courses/actors/route_scout.actor',
                         'courses/actors/route_sniper.actor',
                         'courses/actors/short_sniper.actor',
                         'courses/actors/short_interceptor.actor',
                         'courses/bullet_patterns/turret_predictive_spread.pattern',
                         'courses/projectiles/enemy_turret_predictive.projectile',
                         'courses/projectile_visuals/enemy_turret_predictive.projectilevisual',
                         'courses/waves/short_turret_left.wave',
                         'courses/waves/short_turret_right.wave',
                         'courses/waves/route_turret_left.wave',
                         'courses/waves/route_turret_right.wave',
                         'courses/obstacles/route_arch_slab.obstacle',
                         'courses/obstacles/route_break_barricade.obstacle']:
            for suffix in ['', '.meta']:
                source=ROOT/'Resources'/(relative+suffix)
                if source.exists():shutil.copy2(source,target/'Resources'/(relative+suffix))
        for projectile in ['enemy_mothership_rapid','enemy_mothership_heavy']:
            source=ROOT/'Resources/courses/projectiles'/(projectile+'.projectile')
            shutil.copy2(source,target/'Resources/courses/projectiles'/source.name)
        shutil.copytree(ROOT/'Resources/enemies/ElitePursuers',
                        target/'Resources/enemies/ElitePursuers', dirs_exist_ok=True)
        shutil.copytree(ROOT/'Resources/enemies/VeilInterceptor',
                        target/'Resources/enemies/VeilInterceptor', dirs_exist_ok=True)
        for mesh in ('MothershipBlockout','SiegeTurret','SiegeTurretBase','SiegeTurretHead','SiegeTurretBarrel','SiegeTurretSingleBarrel',
                     'GatlingTurretBase','GatlingTurretHead','GatlingTurretRotor','CarrierHeavyTurretBase',
                     'CarrierHeavyTurretHead','CarrierHeavyTurretBarrel','CarrierHeavyTurretVent','CarrierHeavyTurretShutter'):
            shutil.copytree(ROOT/'Resources/enemies'/mesh,
                            target/'Resources/enemies'/mesh,dirs_exist_ok=True)
        shutil.copytree(ROOT/'Resources/course_meshes/CourseBlockoutBridge',
                        target/'Resources/course_meshes/CourseBlockoutBridge', dirs_exist_ok=True)
        shutil.copytree(ROOT/'Resources/course_meshes/RailSpeedMarker',
                        target/'Resources/course_meshes/RailSpeedMarker', dirs_exist_ok=True)
        shutil.copytree(ROOT/'Resources/course_meshes/EnergyShieldEmitter',
                        target/'Resources/course_meshes/EnergyShieldEmitter', dirs_exist_ok=True)
        shutil.copytree(ROOT/'Resources/course_meshes/RouteBarricadePanel',
                        target/'Resources/course_meshes/RouteBarricadePanel', dirs_exist_ok=True)
        shutil.copytree(ROOT/'Resources/course_meshes/RouteNaturalArch',
                        target/'Resources/course_meshes/RouteNaturalArch', dirs_exist_ok=True)
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
                if p[0] in SHARED_COURSE_KINDS or p[0]=='event':continue
                if p[0]=='enemy_placement' and (p[0],p[1]) not in source_rows:continue
                if p[0]=='enemy_placement' and (p[0],p[1]) in source_rows:
                    p[16]=source_rows[(p[0],p[1])][16]
                    p[2]=source_rows[(p[0],p[1])][2]
                if p[0] in ('wave','encounter_beat') and (p[0],p[1]) in source_rows:
                    updated=source_rows[(p[0],p[1])].copy()
                    if p[0]=='wave':updated[10:13]=p[10:13]
                    p=updated
                rest.append('|'.join(p))
            events=[l for l in course.splitlines() if l.startswith('event|')]
            path.write_text('\n'.join(rest+events+shared)+'\n', encoding='utf-8')
    from capture_energy_shield import fixture_course
    shield_resources=PREVIEW/'EnergyShield/Resources'
    shutil.copytree(ROOT/'Resources',shield_resources,dirs_exist_ok=True)
    (shield_resources/'courses/ShortCompleteRun.course').write_text(fixture_course(course,comparison=True),encoding='utf-8')
    (PREVIEW/'PlayEnergyShield.cmd').write_text(
        '@echo off\nsetlocal\ncd /d "%~dp0EnergyShield"\n'
        'set "GE3_SUNFALL_PREVIEW_DISTANCE=710"\nset "GE3_SUNFALL_PREVIEW_FREEZE=0"\n'
        'set "GE3_COURSE_FRAME_EXPORT="\nstart "" "%~dp0GE3.exe"\n',encoding='utf-8')
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
        f'set "GE3_SUNFALL_PREVIEW_DISTANCE={first_battery-90}"\nset "GE3_SUNFALL_PREVIEW_FREEZE=0"\n'
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
    first_resources=PREVIEW/'FirstGroundBattery/Resources'
    shutil.copytree(ROOT/'Resources',first_resources,dirs_exist_ok=True)
    first=[]
    for line in course.splitlines():
        p=line.split('|')
        if p[0]=='wave' and p[1]!='short-mothership':continue
        if p[0] in ('enemy_placement','encounter_beat') and p[4]!='short-mothership':continue
        if p[0]=='event' and p[2]=='enemy_wave' and p[3]!='short_turret_left':continue
        first.append(line)
    (first_resources/'courses/ShortCompleteRun.course').write_text('\n'.join(first)+'\n',encoding='utf-8')
    (PREVIEW/'PlayFirstGroundBattery.cmd').write_text(
        '@echo off\nsetlocal\ncd /d "%~dp0FirstGroundBattery"\n'
        f'set "GE3_SUNFALL_PREVIEW_DISTANCE={first_battery-30}"\nset "GE3_SUNFALL_PREVIEW_FREEZE=0"\n'
        'set "GE3_COURSE_FRAME_EXPORT="\nstart "" "%~dp0GE3.exe"\n',encoding='utf-8')
    battery_resources=PREVIEW/'GroundBatteries/Resources'
    shutil.copytree(ROOT/'Resources',battery_resources,dirs_exist_ok=True)
    batteries=[]
    for line in course.splitlines():
        p=line.split('|')
        if p[0]=='wave' and p[1]!='short-mothership':continue
        if p[0] in ('enemy_placement','encounter_beat') and p[4]!='short-mothership':continue
        if p[0]=='event' and p[2]=='enemy_wave' and p[4]!='route:turret':continue
        batteries.append(line)
    (battery_resources/'courses/ShortCompleteRun.course').write_text('\n'.join(batteries)+'\n',encoding='utf-8')
    for name,distance in [('PlayGroundBatteries',first_battery-30),('PlaySecondGroundBattery',second_battery-30),
                          ('PlayClimbGroundBattery',2030),('PlayFinalGroundBattery',2810)]:
        (PREVIEW/(name+'.cmd')).write_text(
            '@echo off\nsetlocal\ncd /d "%~dp0GroundBatteries"\n'
            f'set "GE3_SUNFALL_PREVIEW_DISTANCE={distance}"\nset "GE3_SUNFALL_PREVIEW_FREEZE=0"\n'
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
    (PREVIEW/'PlayPostBossBridge.cmd').write_text(
        '@echo off\nsetlocal\ncd /d "%~dp0CarrierShot"\n'
        'set "GE3_SUNFALL_PREVIEW_DISTANCE=3290"\nset "GE3_SUNFALL_PREVIEW_FREEZE=0"\n'
        'set "GE3_COURSE_FRAME_EXPORT="\nset "GE3_POST_BOSS_AUDIT="\nstart "" "%~dp0GE3.exe"\n',encoding='utf-8')
    (PREVIEW/'PlayHeavyInterceptTutorial.cmd').write_text(
        '@echo off\nsetlocal\ncd /d "%~dp0CarrierShot"\n'
        'set "GE3_SUNFALL_PREVIEW_DISTANCE=3290"\nset "GE3_SUNFALL_PREVIEW_FREEZE=0"\n'
        'set "GE3_COURSE_FRAME_EXPORT="\nset "GE3_POST_BOSS_AUDIT="\n'
        'set "GE3_HEAVY_TUTORIAL_AUDIT="\nstart "" "%~dp0GE3.exe"\n',encoding='utf-8')
    shutil.copy2(PREVIEW/'PlayHeavyInterceptTutorial.cmd', PREVIEW/'PlayHeavyIntercept.cmd')
    print('Course blockout preview and five existing packages refreshed.')


if __name__ == '__main__':
    main()
