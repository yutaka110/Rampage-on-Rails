"""Native prototype review: fight the real miniboss, travel, then fight its pursuers.

The host audit only drives ordinary reticle and fire inputs. The isolated
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
    parser.add_argument('--stages',type=int,nargs='+',choices=range(20),default=[0,1,2,3])
    args=parser.parse_args()
    out=ROOT/'generated/post-boss-review';work=out/'native';work.mkdir(parents=True,exist_ok=True)
    shutil.copytree(ROOT/'Resources',work/'Resources',dirs_exist_ok=True)
    for name in ['GE3.exe','dxcompiler.dll','dxil.dll']:
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
            'fang-followup','veil-interrupt','fang-cannon-break','veil-sensor-break','fang-panel-evade','fang-lightning-release','fang-lightning-flight','fang-lightning-hit','fang-lightning-surface']
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
        if stage==3:assert last['alive']=='0' and int(last['rear_primary'])>=6 and int(last['rear_lock'])>=2,last
        if stage in (4,5):assert last['phase']=='4' and last['alive']=='0' and float(last['camera_weight'])==0,last
        if stage==6:assert last['phase']=='5' and last['alive']=='1',last
        if stage==7:assert last['phase']=='6' and last['alive']=='2' and float(last['camera_weight'])==1,last
        if stage==9:
            assert last['fang_rounds']=='3' and last['veil_rounds']=='1' and last['cycles']=='1',last
            assert next(i for i,r in enumerate(rows) if int(r['fang_rounds'])==3) < next(i for i,r in enumerate(rows) if int(r['veil_rounds'])==1)
        if stage==10:
            assert last['alive']=='2' and last['fang_rounds']=='3' and last['veil_rounds']=='0',last
            with (out/f'{label}.enemies.csv').open() as enemies:
                rows_by_asset={row['asset']:row for row in csv.DictReader(line for line in enemies if not line.startswith('#'))}
            assert all(rows_by_asset[asset]['targetable']=='1' for asset in ('rear_fang','rear_veil')),rows_by_asset
        if stage==11:
            assert float(last['fang_bonus_damage'])==18 and last['veil_rounds']=='1',last
        if stage==12:
            assert last['veil_rounds']=='0' and last['rear_lock']=='1' and last['alive']=='2',last
            assert any(row['veil_interrupted']=='1' for row in rows),last
        notice=next((r for r in rows if r['phase']=='4'),None)
        if stage==13:
            assert int(last['fang_rounds'])==5 and float(last['fang_hull_hp'])==120 and float(last['fang_cannon_hp'])==0,last
            assert any(row['fang_rounds']=='3' and float(row['fang_cannon_hp'])==36 for row in rows),last
            assert any(row['fang_rounds']=='3' and float(row['fang_cannon_hp'])==0 for row in rows),last
        if stage==14:
            assert int(last['rear_primary'])==2 and int(last['veil_rounds'])==1,last
            assert float(last['veil_hull_hp'])==68 and float(last['veil_sensor_hp'])==0,last
            assert abs(float(last['veil_descent_lane'])-float(last['veil_shot_lane']))<.01,last
            assert any(row['veil_sensor_hp']=='0' and row['veil_hull_hp']=='68' and row['veil_rounds']=='0' for row in rows),last
        if stage==15:
            first_break=next(i for i,row in enumerate(rows) if row['fang_panels']=='5')
            before=rows[first_break-1]
            retreat=rows[first_break:]
            assert float(before['fang_gap'])<1000 and int(before['fang_rounds'])>=1,before
            assert any(float(row['player_speed'])>58 and float(row['panel_boost'])>0 for row in retreat),last
            assert any(float(before['fang_gap'])+2<float(row['fang_gap'])<1000 for row in retreat),last
            assert all(row['player_hp']==before['player_hp'] for row in retreat if row['veil_rounds']=='0'),last
        if stage>=2:assert notice and float(notice['age'])>=12 and float(notice['distance'])-float(notice['anchor'])>=489,notice
        item=dict(stage=label,last=last,notice=notice,build_sha256=build,fixture_boss_hp=[60,60])
        by_stage[label]=item;print(json.dumps(item),flush=True)
        manifest.write_text(json.dumps([by_stage[name] for name in labels if name in by_stage],indent=2),encoding='utf-8')
if __name__=='__main__':main()
