"""Release-engine composition references; the downhill shot runs the real flow."""
from pathlib import Path
import os
import subprocess
import cv2
import numpy as np

ROOT=Path(__file__).resolve().parents[1]


def main():
    folder=ROOT/'generated/course-blockout-preview/RidgeCombat'
    output=ROOT/'generated/ridge-combat'
    output.mkdir(exist_ok=True)
    cards=[]
    for name,distance,frames in [('01-climb',2350,30),('02-crest',2506,30),('03-dive',2560,65)]:
        path=output/(name+'.png')
        path.unlink(missing_ok=True)
        env=dict(os.environ,GE3_SUNFALL_PREVIEW_DISTANCE=str(distance),
                 GE3_SUNFALL_PREVIEW_FREEZE='0',GE3_COURSE_FRAME_EXPORT=str(path),
                 GE3_COURSE_FRAME_EXPORT_FRAMES=str(frames))
        print('Exporting '+name,flush=True)
        process=subprocess.Popen([str(ROOT/'generated/attack-warning/Release/GE3.exe')],cwd=folder,env=env)
        try:
            code=process.wait(timeout=120)
        except subprocess.TimeoutExpired:
            process.terminate();process.wait(timeout=15)
            raise
        if code!=0 or not path.exists():
            raise RuntimeError(f'Failed frame export: {name}, exit={code}')
        frame=cv2.imdecode(np.fromfile(path,dtype=np.uint8),cv2.IMREAD_COLOR)
        card=np.full((486,800,3),(24,27,32),dtype=np.uint8)
        card[:450]=cv2.resize(frame,(800,450),interpolation=cv2.INTER_AREA)
        cv2.putText(card,name.upper()+' / LIVE',(14,476),
                    cv2.FONT_HERSHEY_SIMPLEX,.6,(225,232,238),1,cv2.LINE_AA)
        cards.append(card)
    success,data=cv2.imencode('.png',np.hstack(cards))
    if not success:raise RuntimeError('Cannot encode reference sheet')
    data.tofile(output/'ridge-reference.png')
    print('Saved ridge-reference.png',flush=True)


if __name__=='__main__':
    main()
