"""Six visual beats on the existing rail; deterministic course-native blockout."""
from pathlib import Path
import json
from generate_submission_hazards import Mesh, ROOT
from generate_siege_turret import box


def bridge():
    palette = [(63, 79, 85), (104, 119, 122), (153, 121, 65), (33, 43, 48)]
    mesh = Mesh('CourseBlockoutBridge', palette)
    box(mesh, (0, -.65, 0), (8, .5, 20), 0)
    for side in (-1, 1):
        box(mesh, (side*3.55, -1.65, 0), (.7, 1.8, 20), 3)
        box(mesh, (side*4.0, 1.1, 0), (.18, .18, 20), 2)
        for z in (-9, -3, 3, 9):
            box(mesh, (side*4, .35, z), (.18, 1.6, .18), 1)
        for z in (-7, 7):
            box(mesh, (side*3.0, -44, z), (.9, 86, .9), 0)
    for z in (-7, 0, 7):
        box(mesh, (0, -2.7, z), (8.4, .45, .6), 1)
    for y in (-12, -28, -44, -60, -76):
        for z in (-7, 7):
            box(mesh, (0, y, z), (7, .6, .6), 1)
    mesh.save(ROOT/'Resources/course_meshes/CourseBlockoutBridge')


def main():
    bridge()
    path = ROOT/'Resources/courses/ShortCompleteRun.course'
    text = path.read_text(encoding='utf-8-sig')
    baseline = ROOT/'generated/course-blockout-baseline.course'
    if not baseline.exists():
        baseline.write_text(text, encoding='utf-8')
    # Replace only owned visual authoring rows. Wave membership and rail anchors
    # remain stable so encounter timing and checkpoint identities survive.
    kept = []
    for line in text.splitlines():
        if line.startswith(('camera|', 'camera_shot|', 'cinematic_shot_set|')):
            continue
        if line.startswith('# BEGIN COURSE BLOCKOUT'):
            continue
        if line.startswith(('terrain|', 'terrain_brush|', 'lighting|')) and '|blockout-' in line:
            continue
        kept.append(line)
    cameras = [
        (0,24,7,0,64,2,8,54), (160,24,7,0,64,2,8,54),
        (340,25,8,-2,70,3,8,56), (520,25,8,-2,70,3,8,56),
        (620,25,8,2,68,2,8,56), (820,25,8,2,68,2,8,56),
        (950,25,8,0,66,2,10,56), (1580,25,8,0,66,2,10,56),
        (1710,27,11,-8,65,1,10,54), (1740,27,11,-8,65,1,10,54),
        (1900,26,10,-4,67,1,10,58), (2050,25,9,0,68,1,12,59),
        (2240,25,9,0,72,1,12,58), (2360,25,10,0,80,0,12,59),
        (2460,25,13,-5,115,-2,10,61), (2540,25,13,-5,115,-2,10,61),
        (2630,26,10,0,80,1,12,60), (2800,25,9,0,68,1,13,59),
        (3150,27,9,0,70,5,13,57), (3350,27,8,0,72,7,13,56),
        (3800,27,8,0,72,7,13,56),
    ]
    rows = ['# BEGIN COURSE BLOCKOUT: six visual beats, authored front-view cameras.']
    for index, values in enumerate(cameras):
        rows.append('camera|'+'|'.join(map(str, values))+'|0|blockout-camera-'+str(index)+'|1|0')
    def terrain(distance, name, mesh, x, y, scale, yaw=0, ahead=320, behind=120):
        rows.append(f'terrain|{distance}|hero_landmark|blockout-{name}|{mesh}|{x}|{y}|0|'+
                    '|'.join(map(str, scale))+f'|0|{yaw}|0|none|14|{behind}|{ahead}|blockout-terrain-{name}|1|0')
    terrain(575, 'exit-portal', 'organic_arch_large', 0, 29, (42,31,8), ahead=260)
    for distance, name, x, height, width in [(680,'spire-left',-30,35,12),(790,'spire-right',32,43,14),
                                          (900,'spire-far-left',-38,49,15),(960,'spire-far-right',42,54,16)]:
        terrain(distance,name,'root_spire_column',x,height-18,(width,height,width),ahead=400)
    terrain(1340,'stone-gate','organic_arch_large',0,23,(32,24,8),ahead=360)
    terrain(1390,'gate-shoulder-left','root_spire_column',-44,20,(18,40,16),ahead=400)
    terrain(1405,'gate-shoulder-right','root_spire_column',45,23,(19,43,17),ahead=400)
    for distance in range(1650,1940,20):
        terrain(distance,f'bridge-{distance}','course_blockout_bridge',0,0,(1,1,1),ahead=420,behind=180)
    # Positive radial offsets push the lower surface away from the rail.
    # Keep the 1570m grounded battery's foundation outside the brush footprint.
    for i, distance in enumerate([1740,1820]):
        rows.append(f'terrain_brush|blockout-bridge-valley|blockout-valley-{i}|Sculpt|{distance}|4.712389|110|100|48|0.58|0')
    terrain(2780,'ridge-beacon','root_spire_column',-55,18,(16,38,14),ahead=480)
    terrain(3680,'arena-left','root_spire_column',-75,24,(24,48,22),ahead=500)
    terrain(3730,'arena-right','root_spire_column',78,27,(25,51,22),ahead=500)
    lights = [
        (620,'columns',100,'1|0.72|0.52|2.5|-0.50|-0.55|0.65'),
        (950,'gate',100,'1|0.70|0.48|2.6|-0.72|-0.48|0.50'),
        (1530,'bridge',130,'1|0.78|0.60|2.4|-0.50|-0.62|0.60'),
        (1950,'ridge',140,'1|0.69|0.46|2.65|-0.56|-0.48|0.68'),
        (3100,'arena',180,'1|0.72|0.50|2.6|-0.52|-0.52|0.68'),
    ]
    for distance,name,blend,sun in lights:
        rows.append(f'lighting|{distance}|blockout-light-{name}|{blend}|{sun}|0.12|0.13|0.16|0.32|0.36|0.41|0.28|220|1500|0.09|0.18|0.22|0.65|0.08|0.12')
    beats = [
        (0,620,'cave','CAVE EXIT',520,'exit-portal','intro_backlit_mist','Exit aperture ahead; cart and rail retained.'),
        (620,950,'columns','STONE NEEDLES',780,'spire-left,blockout-spire-right','blockout-light-columns','Alternating rooted silhouettes; clear central fire lane.'),
        (950,1640,'gate','STONE GATE',1210,'stone-gate','blockout-light-gate','Turret in front of the gate; fixed combat lens.'),
        (1640,1950,'bridge','CLIFF TRESTLE',1730,'bridge-1750','blockout-light-bridge','Rail support and exposed valley; gentle left camera offset.'),
        (1950,3350,'ridge','RIDGE DIVE',2650,'ridge-beacon','blockout-light-ridge','Crest anticipation and descending rail visible.'),
        (3350,11300,'carrier','CARRIER ASSAULT',3430,'arena-left,blockout-arena-right','blockout-light-arena','Broad stable combat framing; mother ship ahead.'),
    ]
    shots=[]
    for start,end,key,label,distance,heroes,light,note in beats:
        ids=','.join(x if x.startswith('blockout-') else 'blockout-'+x for x in heroes.split(','))
        rows.append(f'cinematic_shot_set|{start}|{end}|blockout-shot-{key}|{label}|{ids}|-|{light}|-|first_contact_readable_strata|clear-combat|{note}')
        shots.append(dict(id=key,label=label,start=start,end=end,distance=distance,hero_landmarks=ids.split(','),composition=note))
    path.write_text('\n'.join(kept).rstrip()+'\n\n'+'\n'.join(rows)+'\n',encoding='utf-8')
    output=ROOT/'generated/course-blockout';output.mkdir(exist_ok=True)
    (output/'shots.json').write_text(json.dumps(shots,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Authored 6 visual beats,',len(cameras),'camera keys and',sum(r.startswith('terrain|') for r in rows),'landmark placements')


if __name__ == '__main__':
    main()
