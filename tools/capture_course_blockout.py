"""Export six engine-rendered composition references from the Release build."""
from pathlib import Path
import json
import os
import subprocess
import sys
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
PREVIEW = ROOT/'generated/course-blockout-preview'
OUTPUT = ROOT/'generated/course-blockout'


def main():
    shots = json.loads((OUTPUT/'shots.json').read_text(encoding='utf-8'))
    course_path = PREVIEW/'Resources/courses/ShortCompleteRun.course'
    original = course_path.read_text(encoding='utf-8-sig')
    active_waves = ['short_lock', 'short_intercept', 'short_finale', 'short-exit-guard', '', 'short-mothership']
    cards = []
    only = int(sys.argv[1]) if len(sys.argv)>1 else 0
    previous = json.loads((OUTPUT/'capture-manifest.json').read_text()) if only else []
    try:
        for i, (shot, active) in enumerate(zip(shots, active_waves), 1):
            path = OUTPUT/f'{i:02}-{shot["id"]}.png'
            lines = []
            for line in original.splitlines():
                parts = line.split('|')
                if parts[0] == 'wave':
                    if parts[1]!=active:
                        continue
                if parts[0] in ('enemy_placement','encounter_beat') and parts[4]!=active:
                    continue
                lines.append('|'.join(parts))
            course_path.write_text('\n'.join(lines)+'\n', encoding='utf-8')
            env = dict(os.environ, GE3_SUNFALL_PREVIEW_DISTANCE=str(shot['distance']),
                       GE3_SUNFALL_PREVIEW_FREEZE='0' if i==6 else '1', GE3_COURSE_FRAME_EXPORT=str(path))
            code=0
            if not only or only==i:
                path.unlink(missing_ok=True)
                print(f'Exporting {i}/6: {shot["label"]} at {shot["distance"]}m', flush=True)
                process = subprocess.Popen([str(ROOT/'generated/attack-warning/Release/GE3.exe')], cwd=PREVIEW, env=env)
                try:
                    code = process.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=15)
                    raise RuntimeError(f'Frame export timed out: {shot["id"]}')
                if code != 0 or not path.exists():
                    raise RuntimeError(f'Frame export failed: {shot["id"]}, exit={code}')
            frame = cv2.imdecode(np.fromfile(path, dtype=np.uint8), cv2.IMREAD_COLOR)
            if frame is None:
                raise RuntimeError(f'Unreadable output: {path}')
            height, width = frame.shape[:2]
            card = np.full((494, 800, 3), (24, 27, 32), dtype=np.uint8)
            card[:450] = cv2.resize(frame, (800,450), interpolation=cv2.INTER_AREA)
            if not only or only==i:
                capture_log=(PREVIEW/'logs/course_frame_export.log').read_text().splitlines()[-1]
                captured_distance=float(capture_log.split(',')[0])
            else:
                captured_distance=previous[i-1]['captured_distance']
            shot['captured_distance']=captured_distance
            title = f'{i:02}  {shot["label"]}  /  {captured_distance:.0f}m'
            cv2.putText(card, title, (14,480), cv2.FONT_HERSHEY_SIMPLEX, .62, (225,232,238), 1, cv2.LINE_AA)
            cards.append(card)
            print(f'Saved {path.name}: {width}x{height}, exit={code}', flush=True)
    finally:
        course_path.write_text(original, encoding='utf-8')
    board = np.vstack([np.hstack(cards[i:i+2]) for i in range(0,6,2)])
    success, data = cv2.imencode('.png', board)
    if not success:
        raise RuntimeError('Cannot encode contact sheet')
    data.tofile(OUTPUT/'representative-views.png')
    (OUTPUT/'capture-manifest.json').write_text(json.dumps(shots,indent=2)+'\n',encoding='utf-8')
    print('Saved representative-views.png; playable preview course restored.', flush=True)


if __name__ == '__main__':
    main()
