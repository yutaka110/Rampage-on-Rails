"""Repeatable native review through real reticle, weapon, damage and render paths.

The opt-in engine audit only aims and holds/releases ordinary inputs. It never
injects tokens, damage, shield health, defeats, invulnerability or render poses.
"""
from pathlib import Path
import csv,json,os,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]

def fixture_course(course,comparison=False):
    rows=[];keep={'short_intercept-member-0'}
    if comparison:keep.add('short_intercept-member-1')
    for line in course.splitlines():
        p=line.split('|')
        if p[0]=='wave' and p[1]!='short_intercept':continue
        if p[0]=='encounter_beat' and p[4]!='short_intercept':continue
        if p[0]=='enemy_placement' and p[1] not in keep:continue
        if p[0]=='event' and p[2]=='enemy_wave':continue
        rows.append(line)
    return '\n'.join(rows)+'\n'

def main():
    out=ROOT/'generated/energy-shield-review';work=out/'native';work.mkdir(parents=True,exist_ok=True)
    shutil.copytree(ROOT/'Resources',work/'Resources',dirs_exist_ok=True)
    for name in ['GE3.exe','dxcompiler.dll','dxil.dll']:shutil.copy2(ROOT/'generated/attack-warning/Release'/name,work/name)
    course=(ROOT/'Resources/courses/ShortCompleteRun.course').read_text(encoding='utf-8-sig')
    (work/'Resources/courses/ShortCompleteRun.course').write_text(fixture_course(course),encoding='utf-8')
    report=[]
    for stage,label in enumerate(['active','blocked-lock','fracture','exposed','lock-defeat']):
        env=os.environ.copy();env.update(GE3_SUNFALL_PREVIEW_DISTANCE='710',GE3_SUNFALL_PREVIEW_FREEZE='0',
            GE3_ENERGY_SHIELD_AUDIT='1',GE3_ENERGY_SHIELD_CAPTURE_STAGE=str(stage),
            GE3_NORMAL_TRAVEL_AUDIT='1',GE3_COURSE_FRAME_EXPORT=str(out/f'{label}.png'),GE3_COURSE_FRAME_EXPORT_FRAMES='1')
        for key in ['GE3_KILL_BOOST_AUDIT','GE3_SPEED_FEEL_AUDIT']:env.pop(key,None)
        result=subprocess.run([str(work/'GE3.exe')],cwd=work,env=env,timeout=90)
        assert result.returncode==0 and (out/f'{label}.png').exists(),label
        log=work/'logs/energy_shield_audit.csv';rows=list(csv.DictReader(log.open()));shutil.copy2(log,out/f'{label}-telemetry.csv')
        last=rows[-1];shield=float(last['shield']);body=float(last['hull'])
        if stage<2:assert shield==48 and body==28,last
        elif stage<4:assert shield==0 and body==28,last
        else:assert shield==0 and body==0 and last['final_lock']=='1',last
        if stage>0:assert any(x['first_lock']=='1' and int(x['last_shot'])>0 and float(x['shield'])==48 for x in rows),label
        state=(out/f'{label}.csv').read_text(encoding='utf-8').splitlines()
        entry=dict(stage=label,last=last,state=[x for x in state if x.startswith('#')]);report.append(entry)
        print(json.dumps(entry),flush=True)
    (out/'native-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

if __name__=='__main__':main()
