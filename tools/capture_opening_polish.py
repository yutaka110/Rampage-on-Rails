"""Capture production title, READY/GO and the opening with normal camera/speed settings.

Uses opt-in input replay; an ordinary launch never enables these flags.
Usage: python tools/capture_opening_polish.py [normal|pass-practice]
"""
from pathlib import Path
import csv,os,subprocess,shutil,sys,hashlib,time
root=Path(__file__).resolve().parents[1];work=root/'generated/lock-practice-departure/Release';base=root/'generated/opening-polish-review'
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
for name,stage in [('normal',0),('pass-practice',50)]:
 if len(sys.argv)>1 and name not in sys.argv[1:]:continue
 out=base/name;out.mkdir(parents=True,exist_ok=True)
 env={k:v for k,v in os.environ.items() if not k.startswith('GE3_')}
 env.update(GE3_OPENING_REVIEW='2',GE3_BASIC_TUTORIAL_AUDIT='1',GE3_BASIC_TUTORIAL_CAPTURE_STAGE=str(stage),GE3_BASIC_TUTORIAL_SPEED_DELAY_MS='1000',GE3_BASIC_TUTORIAL_BRAKE_DELAY_MS='450',GE3_COURSE_FRAME_EXPORT=str(out/'opening.png'),GE3_COURSE_FRAME_EXPORT_FRAMES='1')
 print('Capturing actual title/start/READY/GO and opening:',name,flush=True)
 failed=None
 try:p=subprocess.run([str(work/'GE3.exe')],cwd=work,env=env,startupinfo=startup,timeout=190)
 except subprocess.TimeoutExpired:failed='timeout'
 for log in ['opening_review.csv','basic_tutorial_audit.csv','shader_compile_errors.log','app_pipelines_error.log','rail_primary_contact.csv','lock_practice_departure.csv']:
  f=work/'logs'/log
  if f.exists():shutil.copy2(f,out/log)
 assert failed is None,(name,failed)
 assert p.returncode==0,(name,p.returncode)
 for log in ['shader_compile_errors.log','app_pipelines_error.log']:
  assert not (out/log).stat().st_size,(name,log)
 rows=list(csv.DictReader((out/'opening_review.csv').open()))
 assert len(rows)==15 and all((out/f'opening-{i}.png').exists() for i in range(15)),rows
 assert rows[0]['title']=='1' and rows[1]['title']=='0' and float(rows[2]['entry'])>=3.9,rows[:4]
 assert all(float(r['hp'])>0 for r in rows),rows
 assert float(rows[-1]['distance'])>=1735,rows[-1]
 if stage==50:
  timeline=list(csv.DictReader((out/'basic_tutorial_audit.csv').open()))
  aircraft=list(csv.DictReader((out/'lock_practice_departure.csv').open()))
  departed={r['actor'] for r in aircraft if r['exit']=='1'}
  assert len(departed)==6 and timeline[-1]['lock_formation_alive']=='0'
  assert all(r['contacts']=='0' for r in aircraft)
  for actor in departed:
   exit_rows=[r for r in aircraft if r['actor']==actor and r['exit']=='1']
   assert all(r['alpha']=='1' for r in exit_rows)
   last=exit_rows[-1]
   assert last['visible']=='0' and (float(last['screen_x'])<0 or float(last['screen_x'])>1280 or float(last['screen_y'])<0),last
  brake=next(r for r in timeline if r['lesson']=='4' and r['success']=='1')
  boost=next(r for r in timeline if r['lesson']=='3' and r['success']=='1')
  assert float(brake['distance'])<float(boost['distance'])
 print(name,'PASS',len(rows),'frames; HP',rows[-1]['hp'],'distance',rows[-1]['distance'],flush=True)
