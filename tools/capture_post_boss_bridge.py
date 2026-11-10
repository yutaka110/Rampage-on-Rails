"""Native prototype review: fight the real miniboss, travel, then fight its pursuers.

The host audit drives ordinary reticle, fire and mounted evasion inputs. The isolated
fixture authors each boss weapon at 60 HP so this review tests the transition
without requiring an automated counter-attack strategy. Production boss HP
and all rear-pursuit data remain unchanged. No runtime damage/defeat is injected.
"""
from pathlib import Path
import argparse,csv,hashlib,json,os,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]

def fixture_course(course):
    rows=[]
    for line in course.splitlines():
        p=line.split('|')
        if p[0]=='wave' and p[1]!='short-mothership':continue
        if p[0] in ('enemy_placement','encounter_beat') and p[4]!='short-mothership':continue
        if p[0]=='event' and p[2]=='enemy_wave':continue
        rows.append(line)
    return '\n'.join(rows)+'\n'

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--stages',type=int,nargs='+',choices=range(62),default=[0,1,2,3])
    parser.add_argument('--exe',type=Path,default=ROOT/'generated/attack-warning/Release/GE3.exe')
    parser.add_argument('--output',type=Path,default=ROOT/'generated/post-boss-review')
    args=parser.parse_args()
    out=args.output.resolve();work=out/'native';work.mkdir(parents=True,exist_ok=True)
    shutil.copytree(ROOT/'Resources',work/'Resources',dirs_exist_ok=True)
    shutil.copy2(args.exe,work/'GE3.exe')
    for name in ['dxcompiler.dll','dxil.dll']:
        shutil.copy2(ROOT/'generated/attack-warning/Release'/name,work/name)
    course=(ROOT/'Resources/courses/ShortCompleteRun.course').read_text(encoding='utf-8-sig')
    (work/'Resources/courses/ShortCompleteRun.course').write_text(fixture_course(course),encoding='utf-8')
    for actor in ['short_mothership_rapid','short_mothership_heavy']:
        path=work/'Resources/courses/actors'/f'{actor}.actor'
        rows=[]
        for line in path.read_text(encoding='utf-8-sig').splitlines():
            p=line.split('|')
            if p[0]=='actor':p[5]='60'
            rows.append('|'.join(p))
        path.write_text('\n'.join(rows)+'\n',encoding='utf-8')
    build=hashlib.sha256((work/'GE3.exe').read_bytes()).hexdigest()
    manifest=out/'native-verification.json'
    existing=json.loads(manifest.read_text()) if manifest.exists() else []
    by_stage={item['stage']:item for item in existing if item.get('build_sha256')==build}
    labels=['valley-vista','bridge-entry','rear-combat','pursuit-clear','first-pursuit-shadow','second-pursuit-shadow',
            'first-pursuer-reveal','pair-reveal','fang-close-pass','elite-cycle','pursuit-handoff',
            'fang-followup','veil-interrupt','fang-cannon-break','veil-sensor-break','fang-panel-evade','fang-lightning-release','fang-lightning-flight','fang-lightning-hit','fang-lightning-surface',
            'veil-beam-tracking','veil-beam-locked','veil-beam-impact','veil-beam-boost','fang-fire-punch','veil-pressure-shock',
            'bridge-cut','bridge-release-aim','bridge-release-hit','bridge-jump','bridge-landing','bridge-escape-clear','bridge-escape-retry','bridge-damage-escape','bridge-retry-then-clear',
            'bridge-structure-sequence','bridge-structure-retry',
            'coordination-cruise','coordination-accelerate','coordination-brake','coordination-occluded',
            'rear-panel-available','rear-panel-lock','rear-panel-broken','rear-panel-socket',
            'fang-crossfire-warning','fang-crossfire-counter','fang-crossfire-interrupted','fang-crossfire-hit',
            'rear-beam-warning','rear-beam-evade','rear-beam-interrupted','rear-beam-hit','rear-attacks-overlap',
             'veil-lightning-warning','veil-lightning-counter','veil-lightning-hit','veil-lightning-columns',
            'fang-blue-impact','veil-blue-impact','fang-critical-smoke','veil-critical-smoke']
    fang_hp,veil_hp=(float((ROOT/f'Resources/courses/actors/rear_{actor}.actor')
        .read_text(encoding='utf-8-sig').splitlines()[0].split('|')[5]) for actor in ('fang','veil'))
    for stage in args.stages:
        label=labels[stage]
        env=os.environ.copy();env.update(GE3_SUNFALL_PREVIEW_DISTANCE='3290',GE3_SUNFALL_PREVIEW_FREEZE='0',
            GE3_POST_BOSS_AUDIT='1',GE3_POST_BOSS_CAPTURE_STAGE=str(stage),GE3_NORMAL_TRAVEL_AUDIT='1',
            GE3_COURSE_FRAME_EXPORT=str(out/f'{label}.png'),GE3_COURSE_FRAME_EXPORT_FRAMES='1')
        for key in ['GE3_KILL_BOOST_AUDIT','GE3_SPEED_FEEL_AUDIT','GE3_ENERGY_SHIELD_AUDIT']:env.pop(key,None)
        result=subprocess.run([str(work/'GE3.exe')],cwd=work,env=env,timeout=150)
        log=work/'logs/post_boss_audit.csv'
        if log.exists():shutil.copy2(log,out/f'{label}-telemetry.csv')
        assert result.returncode==0 and (out/f'{label}.png').exists(),label
        rows=list(csv.DictReader(log.open()));last=rows[-1]
        assert float(last['anchor'])>=3290 and float(last['player_hp'])>0,last
        if stage<2:assert last['alive']=='0' and float(last['camera_weight'])==0,last
        if stage==2:assert last['alive']=='2' and float(last['rear_max'])<-35 and float(last['camera_weight'])==1,last
        if stage==3:
            assert last['alive']=='0' and int(last['rear_primary'])>=6 and int(last['rear_lock'])>=2,last
            escape_start=next(r for r in rows if r['phase']=='9')
            assert float(escape_start['veil_hull_hp'])==0,escape_start
        if stage in (4,5):assert last['phase']=='4' and last['alive']=='0' and float(last['camera_weight'])==0,last
        if stage==6:assert last['phase']=='5' and last['alive']=='1',last
        if stage==7:assert last['phase']=='6' and last['alive']=='2' and float(last['camera_weight'])==1,last
        if stage==9:
            assert last['fang_rounds']=='3' and last['veil_beams']=='1' and last['cycles']=='1',last
            assert next(i for i,r in enumerate(rows) if int(r['fang_rounds'])==3) < next(i for i,r in enumerate(rows) if int(r['veil_beams'])==1)
        if stage==10:
            assert last['alive']=='2' and int(last['fang_rounds'])>0 and last['veil_beams']=='0',last
            with (out/f'{label}.enemies.csv').open() as enemies:
                rows_by_asset={row['asset']:row for row in csv.DictReader(line for line in enemies if not line.startswith('#'))}
            assert all(rows_by_asset[asset]['targetable']=='1' for asset in ('rear_fang','rear_veil')),rows_by_asset
        if stage==11:
            assert float(last['fang_bonus_damage'])==18 and last['veil_beams']=='1',last
        if stage==12:
            assert last['veil_beams']=='0' and last['rear_lock']=='1' and last['alive']=='2',last
            assert any(row['veil_interrupted']=='1' for row in rows),last
        notice=next((r for r in rows if r['phase']=='4'),None)
        if stage==13:
            assert int(last['fang_rounds'])==5 and float(last['fang_hull_hp'])==fang_hp and float(last['fang_cannon_hp'])==0,last
            assert any(row['fang_rounds']=='3' and float(row['fang_cannon_hp'])==36 for row in rows),last
            assert any(row['fang_rounds']=='3' and float(row['fang_cannon_hp'])==0 for row in rows),last
        if stage in (8,13,24):
            with (out/f'{label}.parts.csv').open() as parts_file:parts=list(csv.DictReader(parts_file))
            names={p['node'] for p in parts}
            expected={'fang_body','fang_wing_left','fang_wing_right','fang_barrel_left','fang_engine_left','fang_engine_right','fang_sensor'}
            if stage!=13:expected.add('fang_barrel_right')
            assert names==expected,parts
            assert all(p['actor']!='0' for p in parts),parts
            if stage==13:assert next(p for p in parts if p['node']=='fang_body')['mesh']=='fang_body_damaged',parts
        if stage==14:
            assert int(last['rear_primary'])==2 and int(last['veil_beams'])==1,last
            assert float(last['veil_hull_hp'])==veil_hp and float(last['veil_sensor_hp'])==0,last
            assert any(row['veil_sensor_hp']=='0' and float(row['veil_hull_hp'])==veil_hp and row['veil_beams']=='0' for row in rows),last
        if stage==15:
            first_break=next(i for i,row in enumerate(rows) if row['fang_panels']=='5')
            before=rows[first_break-1]
            retreat=rows[first_break:]
            assert float(before['fang_gap'])<1000 and int(before['fang_rounds'])>=1,before
            assert any(float(row['player_speed'])>58 and float(row['panel_boost'])>0 for row in retreat),last
            assert any(float(before['fang_gap'])+2<float(row['fang_gap'])<1000 for row in retreat),last
            assert all(row['player_hp']==before['player_hp'] for row in retreat if row['veil_beams']=='0'),last
        if stage in (41,42,43,44):
            assert last['camera_weight']=='1' and last['alive']=='2',last
            assert last['fang_panels']==('5' if stage>=43 else '7'),last
            if stage>=43:
                assert last['rear_lock']=='1' and float(last['fang_hull_hp'])==fang_hp,last
        if stage in (45,46,47,48):
            warning=[row for row in rows if row['crossfire_phase']=='1']
            assert warning and last['camera_weight']=='1' and last['alive']=='2',last
            assert warning[0]['crossfire_panel'] in ('0','2') and len({r['crossfire_lane'] for r in warning})==1,warning
            if stage==45:
                assert last['crossfire_rounds']=='0' and abs(float(last['crossfire_bank']))>.75,last
            if stage==46:
                assert .74<=float(warning[-1]['crossfire_age'])<=.81,warning[-1]
                assert int(last['evasion_count'])>int(warning[0]['evasion_count']),last
                assert last['player_hp']==warning[0]['player_hp'] and last['crossfire_rounds']=='3',last
                assert last['fang_followup']=='1' and float(last['fang_bonus_damage'])>0,last
            if stage==47:
                assert last['crossfire_rounds']=='0' and last['rear_lock']=='1',last
                assert int(last['fang_panels'])==7-(1<<int(last['crossfire_panel'])),last
            if stage==48:
                assert last['crossfire_rounds']=='3' and last['evasion_count']==warning[0]['evasion_count'],last
                assert float(last['player_hp'])<float(warning[0]['player_hp']),last
        if stage in (22,23,49,50,51,52,53):
            charging=[r for r in rows if r['veil_beam_phase'] in ('1','2')]
            assert charging and last['camera_weight']=='1' and last['alive']=='2',last
            first_charge=[r for r in charging if r['veil_beams']=='0']
            assert len({r['beam_end'] for r in first_charge})==1,last
            if stage in (22,23,49,50,52):
                assert abs(float(first_charge[0]['veil_beam_remaining'])-1.80)<.002,last
                first_locked=next(r for r in first_charge if r['veil_beam_phase']=='2')
                assert .648<=float(first_locked['veil_beam_remaining'])<=.701,first_locked
                assert abs(float(first_locked['phase_age'])-float(first_charge[0]['phase_age'])-1.10)<.052,first_locked
            if stage==49:assert last['veil_beams']=='0' and last['veil_beam_phase']=='2',last
            if stage in (22,23,50,52):
                boosted=stage in (23,50)
                flight=[r for r in rows if r['veil_beam_phase']=='3']
                assert abs(float(flight[0]['phase_age'])-float(first_charge[0]['phase_age'])-1.80)<.055,flight[0]
                assert last['veil_beams']=='1' and last['beam_front']==last['beam_end'],last
                assert len({r['beam_front'] for r in flight})>2,flight
                assert sum(r['veil_beam_contact']=='1' for r in rows)==int(not boosted),last
                if boosted:
                    assert last['boost_held']=='1' and float(last['distance'])>float(last['beam_end']),last
                    assert float(last['player_speed'])>float(charging[0]['player_speed'])+4,last
                else:assert last['boost_held']=='0',last
            if stage==51:
                assert last['veil_beams']=='0' and last['veil_interrupted']=='1',last
                assert last['rear_lock']=='1' and last['veil_sensor_hp']=='0' and float(last['veil_hull_hp'])==veil_hp,last
            if stage==53:
                assert last['fang_threat']==last['veil_threat']=='1',last
                assert any(r['fang_threat']=='1' and r['veil_threat']=='0' for r in rows),last
        if stage in (54,55,56,57):
            attack=[row for row in rows if float(row['lightning_age'])>=0]
            assert attack and last['camera_weight']=='1' and last['alive']=='2',last
            assert len({r['lightning_nearest'] for r in attack})==1,attack
            if stage==54:assert last['lightning_fired']=='0' and last['lightning_hits']=='0',last
            else:
                stages=list(dict.fromkeys(r['lightning_fired'] for r in attack))
                assert stages==['0','1','3','7'],stages
            if stage in (55,57):
                first_brake=next(i for i,r in enumerate(attack) if r['brake_held']=='1')
                assert first_brake>0 and .699<=float(attack[first_brake-1]['lightning_age'])<=.751,attack[first_brake]
            if stage==55:
                slow=min(float(r['player_speed']) for r in attack if r['brake_held']=='1')
                assert last['lightning_hits']=='0' and last['player_hp']==attack[0]['player_hp'],last
                clear=next(r for r in attack if float(r['lightning_age'])>=1.545)
                assert float(clear['lightning_nearest'])-float(clear['distance'])>1.25,clear
                assert float(last['lightning_bonus_damage'])==18 and float(last['veil_hull_hp'])<veil_hp,last
                assert float(last['player_speed'])>slow+2,last
            if stage==56:
                assert last['lightning_hits']=='4' and float(last['player_hp'])<float(attack[0]['player_hp']),last
            if stage==57:assert last['lightning_hits']=='0' and last['brake_held']=='1',last
        if 58<=stage<=61:
            target,other=('veil','fang') if stage%2 else ('fang','veil')
            target_hp,other_hp=(veil_hp,fang_hp) if stage%2 else (fang_hp,veil_hp)
            assert last['camera_weight']=='1' and last['alive']=='2',last
            assert 0<float(last[f'{target}_hull_hp'])<target_hp and float(last[f'{other}_hull_hp'])==other_hp,last
            if stage>=60:
                tiers=list(dict.fromkeys(r[f'{target}_smoke_stage'] for r in rows))
                assert tiers==['0','1','2','3'] and float(last[f'{target}_hull_hp'])<=.25*target_hp,last
                assert last[f'{other}_smoke_stage']=='0',last
            else:assert last[f'{target}_smoke_stage']=='0' and int(last['rear_primary'])>0,last
        if stage in (20,21):
            assert last['veil_projectiles']=='0' and last['veil_beam_phase']==str(stage-19),last
        if stage==24:
            assert last['elite_fang_fire']=='1' and float(last['fang_recoil'])>=.10 and last['fang_rounds']=='1',last
            assert float(last['elite_shake'])<=.0181,last
        if stage==25:
            assert last['elite_lens']=='1' and float(last['veil_recoil'])>.05 and abs(float(last['wing_kick']))>.01,last
            assert any(r['elite_veil_fire']=='1' for r in rows),last
            assert any(r['elite_charge_loop']=='1' and r['veil_beam_phase']=='2' for r in rows),last
            assert any(float(r['elite_shake'])>.035 for r in rows if r['veil_beam_phase']=='3'),last
        if 37<=stage<=40:
            log=work/'logs/pursuer_coordination.csv';shutil.copy2(log,out/f'{label}-coordination.csv')
            coordination=list(csv.DictReader(log.open()))
            assert coordination and float(coordination[-1]['age'])>=20,coordination[-1]
            assert any(int(r['fang_shots'])>0 for r in coordination),coordination[-1]
            assert any(int(r['veil_shots'])>0 for r in coordination),coordination[-1]
            previous=(0,0)
            warning_times=[]
            for row in coordination:
                grants=int(row['fang_grants']),int(row['veil_grants'])
                if grants==previous:continue
                assert sum(grants)-sum(previous)==1,row
                warning_times.append(float(row['age']))
                previous=grants
            assert all(b-a>=.577 for a,b in zip(warning_times,warning_times[1:])),warning_times
            if stage==38:assert max(float(r['speed']) for r in coordination)>40,coordination[-1]
            if stage==39:assert min(float(r['speed']) for r in coordination)<28,coordination[-1]
        if stage==3 or 26<=stage<=36:
            bridge_log=work/'logs/bridge_escape_audit.csv'
            shutil.copy2(bridge_log,out/f'{label}-bridge.csv')
            bridge=list(csv.DictReader(bridge_log.open()));b=bridge[-1]
            assert float(b['speed'])>0 and float(last['player_hp'])>0,b
            if stage==3:
                charge=[r for r in bridge if r['veil_shot']=='1']
                assert any(r['feeds']=='4' and r['red_tips']=='0' for r in charge),b
                assert any(r['feeds']=='4' and float(r['red_mouth'])>.95 for r in charge),b
                assert any(float(r['opening'])>.85 and float(r['extension'])>.79 and float(r['charge'])>.95 for r in charge),b
                assert any(float(r['recoil'])>.55 for r in charge),b
                wreck_frames={}
                for name in ('damage','charge'):
                    assert (out/f'{label}-{name}.png').exists(),(label,name)
                    state=(out/f'{label}-{name}.csv').read_text().splitlines()
                    wreck=next(line for line in state if line.startswith('# veil_wreck '))
                    wreck_frames[name]=dict(item.split('=') for item in wreck.split()[2:])
                    assert int(wreck_frames[name]['smoke'])>=5 and int(wreck_frames[name]['sparks'])>=3,wreck
                assert abs(float(wreck_frames['damage']['bank']))>.10 and abs(float(wreck_frames['charge']['bank']))<.10,wreck_frames
                with (out/f'{label}-charge.parts.csv').open() as parts_file:
                    wreck_parts={r['node'] for r in csv.DictReader(parts_file) if r['actor']=='0'}
                assert {'veil_hull','veil_wing_left','veil_wing_right','veil_cannon','veil_armour_top','veil_armour_bottom'} <= wreck_parts,wreck_parts
                assert 'veil_sensor' not in wreck_parts,wreck_parts
                assert any(r['phase']=='1' and 0<float(r['beam_travel'])<1 and float(r['beam_power'])>.9 for r in bridge),b
                assert any(r['phase']=='2' and float(r['beam_travel'])==1 and float(r['beam_power'])>.5 for r in bridge),b
                assert all(float(r['beam_power'])==0 for r in bridge if int(r['phase'])>=3),b
                assert b['phase']=='8' and b['clear']=='1' and any(r['lock_hit']=='1' for r in bridge),b
            if stage==27:assert b['phase']=='3' and b['target_hp']=='30' and b['locks']=='0',b
            if stage in (28,29,30,31,33,34,35,36):
                assert any(r['lock_hit']=='1' for r in bridge),bridge[-5:]
                assert any(r['in_flight']=='1' for r in bridge),bridge[-5:]
            if stage==29:assert b['phase']=='6' and float(b['lift'])>6 and b['supported']=='0' and float(b['speed'])>60,b
            if stage==30:assert b['phase']=='7' and float(b['lift'])==0 and b['supported']=='1',b
            if stage in (31,33,34):assert b['phase']=='8' and b['clear']=='1' and last['alive']=='0',b
            if stage==32:
                assert int(b['attempt'])>int(bridge[0]['attempt']) and b['target_hp']=='30' and b['phase']=='3',b
                assert abs(float(b['distance'])-float(b['checkpoint']))<8,b
                assert float(b['checkpoint'])>=float(bridge[0]['distance']),b
            if stage==33:assert float(b['damage'])>=2852 and 28<=float(b['combat_seconds'])<65,b
            if stage==34:assert int(b['attempt'])>int(bridge[0]['attempt']),b
            if stage in (35,36):
                assert all((out/f'{label}-{i}.png').exists() for i in range(21)),label
                assert 'victory=1' in (out/f'{label}.csv').read_text(),b
                assert any(r['attached']=='1' and float(r['span_y'])<-2 for r in bridge),b
                assert any(r['attached']=='0' and float(r['span_y'])<-20 for r in bridge),b
                aiming=[r for r in bridge if r['phase']=='3']
                assert aiming and max(float(r['shake']) for r in aiming)==0,aiming[-1]
                assert all(abs(float(r['ramp_tip'])+2.4)<.01 for r in aiming),aiming[-1]
                assert any(float(r['deploy'])>.45 and float(r['ramp_tip'])>0 for r in bridge if r['phase']=='4'),b
                assert any(float(r['brace'])>.4 for r in bridge if r['phase']=='1'),b
                veil_shot=[r for r in bridge if r['veil_shot']=='1']
                assert any(float(r['opening'])>.85 and float(r['extension'])<.4 for r in veil_shot),veil_shot[-1:]
                assert any(float(r['extension'])>.79 and float(r['charge'])>.95 for r in veil_shot),veil_shot[-1:]
                assert any(float(r['recoil'])>.55 for r in veil_shot),veil_shot[-1:]
                impact=next(r for r in bridge if r['phase']=='2')
                assert impact['veil_shot']=='0' and float(impact['cut_flash'])>.45,impact
                flow=next(i for i,r in enumerate(bridge) if r['feeds']=='4')
                assert bridge[flow]['red_tips']=='0' and float(bridge[flow]['red_mouth'])==0,bridge[flow]
                mixed=next(i for i,r in enumerate(bridge) if r['red_tips']=='4' and float(r['red_mouth'])<.1)
                red=next(i for i,r in enumerate(bridge) if r['feeds']=='4' and float(r['red_mouth'])>.95)
                compression=next(i for i,r in enumerate(bridge) if i>flow and float(r['compression'])>.90)
                fire=next(i for i,r in enumerate(bridge) if float(r['beam_power'])>.9)
                assert flow<mixed<red<compression<fire and bridge[compression]['feeds']=='0',bridge[compression]
                assert 0<float(bridge[fire]['beam_travel'])<1 and float(impact['beam_travel'])==1,impact
                assert float(impact['shake'])>float(bridge[fire]['shake']),impact
                collapse=[r for r in bridge if r['phase']=='2']
                since_impact=lambda r:float(r['age'])-1.84
                settled=[r for r in collapse if since_impact(r)>=.14]
                assert settled and all(float(r[k])==0 for r in settled for k in ('cut_flash','impact_pulse','shake')),settled[:1]
                bending=next(r for r in collapse if .33<=since_impact(r)<.45)
                assert bending['attached']=='1' and float(bending['span_y'])<-2 and float(bending['beam_power'])>.6,bending
                falling=next(r for r in collapse if .65<=since_impact(r)<.75)
                assert falling['attached']=='0' and float(falling['span_y'])<float(bending['span_y']),falling
                assert 0<float(falling['beam_width'])<float(bending['beam_width']) and float(falling['beam_power'])>0,falling
                beam_end=next(r for r in collapse if float(r['beam_power'])==0)
                assert .79<=since_impact(beam_end)<.90,beam_end
                assert 2.54<=since_impact(aiming[0])<2.65 and abs(float(aiming[0]['view_yaw']))<.01,aiming[0]
                launch=[r for r in bridge if r['phase']=='5']
                assert max(float(r['spin']) for r in launch)>=359,launch[-1]
                assert max(float(r['ignition_shock']) for r in launch)>.30,launch[0]
                assert any(float(r['spin'])>90 and float(r['blur'])>.05 for r in launch),launch[0]
                assert any(float(r['thrust'])>.60 for r in launch),launch[0]
                assert min(float(r['blur']) for r in launch if .20<=float(r['phase_age'])<=.80)>.05,launch[0]
                births=[r for r in bridge if int(r['spark_births'])>0]
                assert len(births)==1 and births[0]['phase']=='7' and births[0]['supported']=='1',births
                assert int(births[0]['spark_births'])>=26 and float(births[0]['flash'])>1.0,births
                assert 1<=max(int(r['bounces']) for r in bridge)<=5,births
                assert all(r['sparks']=='0' for r in bridge if r['phase']=='6'),births
                for a,c in zip(bridge,bridge[1:]):
                    if a['phase'] in ('4','5','6') and c['phase'] in ('4','5','6','7'):
                        assert abs(float(c['time_scale'])-float(a['time_scale']))<.35,(a,c)
                        assert abs(float(c['view_fov'])-float(a['view_fov']))<.10,(a,c)
                    if a['phase']=='7' and c['phase']=='8':
                        assert abs(float(c['blur'])-float(a['blur']))<.10,(a,c)
                if stage==36:assert int(b['attempt'])>int(bridge[0]['attempt']),b
        if stage>=2:
            assert notice and float(notice['age'])>=4.34 and float(notice['distance'])-float(notice['anchor'])>=111.9,notice
            first_combat=next((r for r in rows if r['phase']=='6'),None)
            if first_combat:assert 7.0<=float(first_combat['age'])<=9.0,first_combat
        item=dict(stage=label,last=last,notice=notice,build_sha256=build,fixture_boss_hp=[60,60])
        by_stage[label]=item;print(json.dumps(item),flush=True)
        manifest.write_text(json.dumps([by_stage[name] for name in labels if name in by_stage],indent=2),encoding='utf-8')
if __name__=='__main__':main()
