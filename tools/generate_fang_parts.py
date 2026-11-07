"""Partition Fang's existing closed solids into eight pivot-local rigid parts.

Source OBJ nose is +Z; imported runtime nose is -Z. The housing/socket stays
on the body so its existing gameplay weak-point anchor never recoils.
"""
import json
from generate_elite_pursuers import Mesh, fighter, DEST

PARTS = [
    ('fang_body', 'FangBody.obj', (0, 0, 0)),
    ('fang_wing_left', 'FangWingLeft.obj', (-.58, -.04, .185)),
    ('fang_wing_right', 'FangWingRight.obj', (.58, -.04, .185)),
    ('fang_barrel_left', 'FangBarrelLeft.obj', (-.73, -.18, -.30)),
    ('fang_barrel_right', 'FangBarrelRight.obj', (.73, -.18, -.30)),
    ('fang_engine_left', 'FangEngineLeft.obj', (-.54, -.36, 1.19)),
    ('fang_engine_right', 'FangEngineRight.obj', (.54, -.36, 1.19)),
    ('fang_sensor', 'FangSensor.obj', (0, .015, -2.18)),
]

def owner(group):
    if group == 'visor': return 7
    for side, wing, barrel, engine in [('port',1,3,5),('starboard',2,4,6)]:
        if group == side+'_wing': return wing
        if group == side+'_cannon': return barrel
        if group in (side+'_engine', side+'_exhaust'): return engine
    return 0

def main():
    manifest = {'coordinate_system':'runtime nose -Z; pivots in imported model space', 'parts':[]}
    for index,(name,file,pivot) in enumerate(PARTS):
        manifest['parts'].append(dict(id=name,file=file,pivot=pivot))
    for state,suffix in [('fitted',''),('exposed','Exposed'),('damaged','Damaged')]:
        source=fighter(True,state)
        pieces=[Mesh() for _ in PARTS]
        for group,material,points,normal in source.faces:
            index=owner(group);px,py,pz=PARTS[index][2]
            local=[(x-px,y-py,z+pz) for x,y,z in points]
            pieces[index].faces.append((group,material,local,normal))
        assert sum(len(p.faces) for p in pieces)==len(source.faces)
        for index,part in enumerate(pieces):
            # Only the body changes with exposure/damage; a broken right gun
            # is hidden by its rigid-node visibility, not replaced with a stub.
            if state!='fitted' and index!=0:continue
            file=PARTS[index][1]
            if index==0:file='FangBody'+suffix+'.obj'
            wound=('starboard_cannon_housing',1.02,.22,.92,.27) if state=='exposed' else None
            part.write(DEST/file,wound)
    (DEST/'FangRig.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('Wrote eight rigid parts, two body variants, and FangRig.json')

if __name__=='__main__':main()
