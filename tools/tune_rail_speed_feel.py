"""Deterministic camera and near-field markers for the complete rail course."""
from pathlib import Path
from generate_submission_hazards import Mesh, ROOT
from generate_siege_turret import box

# Preserve framing accents, crest anticipation, shot IDs and the firing lane.
CAMERA_BASE = [
    (24,7),(24,7),(25,8),(25,8),(25,8),(25,8),(25,8),(25,8),
    (27,11),(27,11),(26,10),(25,9),(25,9),(25,10),(25,13),(25,13),
    (26,10),(25,9),(27,9),(27,8),(27,8),
]

# Promote the former held-E targets into ordinary travel. Values are authored
# before the existing 1.35 normal-speed scale; the opening preserves its old
# low-speed assist exactly. Keeping this map explicit makes regeneration safe.
NORMAL_RIDE_TARGETS = {
    'short-ride-0': 29.45/1.35, 'short-ride-1': 29.45/1.35,
    'short-ride-2': 29.45/1.35, 'short-ride-3': 33.0,
    'short-ride-4': 24.0, 'short-ride-climb': 34.5,
    'short-ride-dive': 40.5, 'short-ride-boss': 27.0,
}


def marker():
    mesh = Mesh('RailSpeedMarker', [(42,55,61),(100,119,123),(222,165,72),(172,218,216)])
    box(mesh,(0,-.25,0),(.9,.5,.9),0)
    box(mesh,(0,1.45,0),(.30,3.4,.34),1)
    box(mesh,(0,2.65,0),(.62,.24,.52),2)
    box(mesh,(0,2.98,0),(.52,.20,.46),3)
    box(mesh,(0,.7,0),(.48,.15,.46),2)
    # Cantilever reaches the existing track/bridge edge; bases never float
    # beside the exposed bridge or the elevated ridge rail.
    box(mesh,(-2.0,-.65,0),(4.5,.30,.40),0)
    box(mesh,(-.30,-.45,0),(.40,.60,.48),1)
    mesh.save(ROOT/'Resources/course_meshes/RailSpeedMarker')


def apply_visuals(text):
    rows=[]
    for line in text.splitlines():
        if line.startswith('# Near-field trackside markers:'):
            continue
        p=line.split('|')
        if p[0]=='ride_profile' and len(p)>8 and p[1] in NORMAL_RIDE_TARGETS:
            p[7]=f'{NORMAL_RIDE_TARGETS[p[1]]:.6f}'
            # Vehicle acceleration/braking/jerk perform the smooth handover.
            # Fading the override itself would briefly fall back to the old
            # slower rail request at every section boundary.
            p[16]=p[17]='0.000'
            if p[1]=='short-ride-4':p[3]='1480.000'
            line='|'.join(p)
        if p[0]=='ride_profile' and len(p)>1 and p[1]=='short-ride-bridge':
            continue
        if p[0]=='terrain' and len(p)>3 and p[3].startswith('speed-feel-'):
            continue
        if p[0]=='camera' and len(p)>10 and p[10].startswith('blockout-camera-'):
            index=int(p[10].rsplit('-',1)[1])
            if index<len(CAMERA_BASE):
                back,height=CAMERA_BASE[index]
                p[2]=f'{back*.86:.3f}'
                p[3]=f'{height*.84:.3f}'
                # The old bridge portrait was far off the rail. Keep both
                # near-field edges visible in the new, closer combat camera.
                p[4]=f'{float(p[4])*.70:.3f}' if abs(float(p[4]))>=7 else p[4]
                line='|'.join(p)
        rows.append(line)
    rows.append('ride_profile|short-ride-bridge|1480.000|1950.000|BRIDGE RUN|Combat|1.000|26.666667|1.000|1.000|120.000|48.000|1.000|30.000|0.850|14.000|0.000|0.000|-|1|1|0')
    rows.append('# Near-field trackside markers: visual only, clear central fire lane.')
    for begin,end,spacing,width in [(640,1060,28,6.6),(1630,1940,16,6.6),(2010,3310,28,6.6)]:
        for i,distance in enumerate(range(begin,end,spacing)):
            # Stagger the sides rather than producing a bright tunnel/fence.
            side=-1 if i%2 else 1
            key=f'speed-feel-marker-{distance}'
            yaw=180 if side<0 else 0
            rows.append(f'terrain|{distance}|hero_landmark|{key}|rail_speed_marker|{side*width}|0|0|1|1|1|0|{yaw}|0|none|14|70|140|{key}|1|0')
    return '\n'.join(rows)+'\n'


def main():
    marker()
    path=ROOT/'Resources/courses/ShortCompleteRun.course'
    path.write_text(apply_visuals(path.read_text(encoding='utf-8-sig')),encoding='utf-8')
    from tune_fast_normal_combat import main as tune_combat
    tune_combat()


if __name__=='__main__':
    main()
