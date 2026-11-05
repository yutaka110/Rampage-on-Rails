"""Author Veil as hinged, rigid OBJ parts in the engine's existing PBR bands.

Source space is +Z forward, runtime is -Z forward after Assimp conversion.
Each part is exported about its own pivot. No downloaded model is used.
"""
from pathlib import Path
from math import cos, sin, tau, sqrt
import json
import struct
from generate_elite_pursuers import Mesh, section, tube, wing_section

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Resources/enemies/VeilInterceptor'
COLORS = {
    'coal': (.105,.12,.14), 'plate': (.26,.29,.33),
    'steel': (.38,.42,.47), 'joint': (.025,.030,.038),
    'panel': (.10,.125,.15), 'machinery': (.23,.26,.29),
    'ceramic': (.055,.06,.07), 'heat': (.28,.20,.17),
    'edge': (.48,.51,.55), 'red': (.50,.012,.024),
    'glass': (.012,.024,.038), 'trim': (.15,.17,.19),
}
PIVOTS = {
    'VeilHull': (0,0,0), 'VeilWingLeft': (-1.02,0,-.08),
    'VeilWingRight': (1.02,0,-.08), 'VeilCannon': (0,.10,.72),
    'VeilArmourTop': (0,.64,.70), 'VeilArmourBottom': (0,-.44,.70),
    'VeilSensor': (-1.10,.38,.68), 'VeilSensorCover': (-1.10,.65,.57),
    'VeilCannonGlow': (0,.10,.72), 'VeilEngineGlow': (0,0,0),
}
SHROUD_ANGLE=.64
SHROUD_RETRACT=.42


def face(mesh, name, material, vertices, expected):
    a,b,c = vertices[:3]
    u,v = [b[i]-a[i] for i in range(3)],[c[i]-a[i] for i in range(3)]
    n = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
    length = sqrt(sum(x*x for x in n))
    if length < 1e-8:
        raise ValueError(name)
    sign = 1 if sum(n[i]*expected[i] for i in range(3)) >= 0 else -1
    mesh.faces.append((name,material,vertices if sign > 0 else vertices[::-1],tuple(sign*x/length for x in n)))


def ring(mesh, name, profile, material, x=0, y=0, sides=32):
    """Closed annular cross section, including the inside of an open barrel."""
    for (r,z),(nr,nz) in zip(profile,profile[1:]+profile[:1]):
        for i in range(sides):
            a,b = tau*i/sides,tau*(i+1)/sides
            vertices=[(x+r*cos(a),y+r*sin(a),z),(x+r*cos(b),y+r*sin(b),z),
                      (x+nr*cos(b),y+nr*sin(b),nz),(x+nr*cos(a),y+nr*sin(a),nz)]
            face(mesh,name,material,vertices,((nz-z)*cos((a+b)/2),(nz-z)*sin((a+b)/2),r-nr))


def plate(mesh, name, outline, z0, z1, material):
    mesh.loft(name,[[(x,y,z) for x,y in outline] for z in (z0,z1)],material)


def box(mesh, name, x,y,z, rx,ry,rz, material):
    plate(mesh,name,[(x-rx,y-ry),(x+rx,y-ry),(x+rx,y+ry),(x-rx,y+ry)],z-rz,z+rz,material)


def bevel_plate(mesh,name,outline,front,material='plate',thickness=.15):
    """Thin tapered armour with real bevels; front may be a swept plane."""
    cx=sum(p[0] for p in outline)/len(outline);cy=sum(p[1] for p in outline)/len(outline)
    rings=[]
    for amount,depth in [(.94,-thickness),(1,-thickness*.60),(1,-thickness*.12),(.90,0)]:
        ring_points=[]
        for x,y in outline:
            x=cx+(x-cx)*amount;y=cy+(y-cy)*amount
            ring_points.append((x,y,front(x,y)+depth))
        rings.append(ring_points)
    mesh.loft(name,rings,material,{0:'joint',1:'trim',2:'steel'})


def sector(mesh,name,profile,start,end,material='plate',steps=24,bands=None):
    """Closed curved armour petal, including inner wall and radial end caps."""
    for edge,((r,z),(nr,nz)) in enumerate(zip(profile,profile[1:]+profile[:1])):
        for i in range(steps):
            a=start+(end-start)*i/steps;b=start+(end-start)*(i+1)/steps
            pts=[(r*cos(a),.10+r*sin(a),z),(r*cos(b),.10+r*sin(b),z),
                 (nr*cos(b),.10+nr*sin(b),nz),(nr*cos(a),.10+nr*sin(a),nz)]
            surface=bands.get(edge,material) if bands is not None else ('joint' if edge==2 else 'steel' if edge==1 else material)
            face(mesh,name,surface,pts,((nz-z)*cos((a+b)/2),(nz-z)*sin((a+b)/2),r-nr))
    for a,sign in [(start,-1),(end,1)]:
        # Folded jaw sections are concave. Ear clipping avoids the overlapping
        # fan triangles a naive cap would create across their hollow interior.
        remaining=list(range(len(profile)))
        cross=lambda p,q,r:(q[0]-p[0])*(r[1]-p[1])-(q[1]-p[1])*(r[0]-p[0])
        triangles=[]
        while len(remaining)>3:
            for k,b in enumerate(remaining):
                left,right=remaining[k-1],remaining[(k+1)%len(remaining)]
                p,q,r=profile[left],profile[b],profile[right]
                if cross(p,q,r)<=1e-9:continue
                if any(cross(p,q,profile[j])>=-1e-9 and cross(q,r,profile[j])>=-1e-9 and cross(r,p,profile[j])>=-1e-9
                       for j in remaining if j not in (left,b,right)):continue
                triangles.append((left,b,right));remaining.pop(k);break
            else:raise ValueError('Cannot triangulate jaw profile: '+name)
        triangles.append(tuple(remaining))
        for indices in triangles:
            pts=[(profile[i][0]*cos(a),.10+profile[i][0]*sin(a),profile[i][1]) for i in indices]
            face(mesh,name,'trim',pts,(-sign*sin(a),sign*cos(a),0))


def build():
    parts={name:Mesh() for name in PIVOTS}
    hull=parts['VeilHull']
    hull.loft('armoured_pressure_hull',[section(z,0,y,w,h) for z,y,w,h in
        [(-1.03,-.02,.28,.25),(-.81,0,.62,.46),(-.40,.02,.75,.58),
         (.16,.04,.78,.59),(.53,.07,.66,.51),(.76,.10,.48,.38)]], 'coal',{2:'panel'})
    # Large, sloping shoulder masses carry the bore. Broad planes survive the
    # real rear-battle camera distance; tiny greebles cannot replace this shape.
    for side in (-1,1):
        hull.loft('heavy_shoulder',[wing_section(side*x,y,f,b,t) for x,y,f,b,t in
            [(.43,.04,.80,-.64,.29),(.65,.06,.96,-.62,.40),
             (.92,.04,.77,-.54,.37),(1.15,.01,.44,-.31,.20)]],
            'plate',{0:'coal',2:'coal'})
        outline=[(.54,-.20),(.80,-.26),(1.04,-.07),(.97,.26),(.76,.39),(.52,.25)]
        front=lambda x,y:.99-.57*(side*x-.65)
        bevel_plate(hull,'shoulder_face',[(side*x,y) for x,y in outline],front,'plate',.09)
        outline=[(.66,-.10),(.83,-.13),(.94,-.03),(.90,.15),(.76,.24),(.63,.17)]
        bevel_plate(hull,'shoulder_recess',[(side*x,y) for x,y in outline],lambda x,y:front(x,y)+.008,'joint',.016)
        for y in (-.04,.035,.11):
            box(hull,'shoulder_inlet',side*.78,y,.94,.065,.012,.025,'machinery')
        box(hull,'shoulder_status',side*.69,-.24,1.015,.055,.010,.008,'red')
        hull.loft('rear_engine',[tube(z,side*.37,-.12,r) for z,r in [(-1.08,.24),(-.94,.30),(-.62,.29)]],'joint',{1:'machinery'})
        ring(hull,'exhaust_collar',[(.27,-1.10),(.27,-1.01),(.19,-1.01),(.19,-1.10)],'steel',side*.37,-.12,24)
        for z in (-.58,-.41,-.24):
            box(hull,'rear_heat_vent',side*.46,.50,z,.09,.025,.040,'joint')
        # Fixed rails become visible when the gun's two armoured jaws fold back.
        hull.loft('bore_support_rail',[section(z,side*.46,.10,.075,.14) for z in (.57,1.18)],'machinery',{0:'steel'})
    hull.loft('dorsal_spine',[section(z,0,y,w,h) for z,y,w,h in
        [(-.86,.47,.13,.07),(-.44,.65,.23,.13),(.12,.68,.23,.14),(.48,.55,.17,.10)]],'coal',{1:'plate'})
    hull.loft('dorsal_panel_mount',[section(z,0,y,w,h) for z,y,w,h in
        [(-.35,.66,.27,.15),(.12,.75,.32,.24),(.43,.81,.26,.23),(.50,.80,.22,.16)]],'coal',{1:'plate'})
    hull.loft('sensor_bracket',[wing_section(x,.38,.48,.14,.10) for x in (-.88,-1.12)],'machinery')
    hull.loft('sensor_housing',[tube(z,-1.10,.38,r) for z,r in [(.22,.13),(.40,.23),(.67,.235),(.76,.20)]],'coal',{1:'plate'})
    ring(hull,'sensor_protective_collar',[(.235,.66),(.235,.81),(.205,.87),(.13,.87),(.13,.66)],'steel',-1.10,.38,16)
    ring(hull,'broken_sensor_socket',[(.132,.76),(.132,.82),(.108,.82),(.108,.76)],'heat',-1.10,.38,16)
    for side,name in [(-1,'VeilWingLeft'),(1,'VeilWingRight')]:
        wing=parts[name]
        for up in (-1,1):
            # Tall, raked upper blades and shorter, broad lower stabilisers.
            profile=[(.94,.44),(1.49,.38),(1.84,.88),(1.49,2.22),(1.22,2.53),(1.00,1.78)] if up>0 else \
                [(.94,.37),(1.56,.32),(1.89,.69),(1.63,1.42),(1.18,1.75),(.88,1.05)]
            outline=[(side*x,up*y) for x,y in profile]
            sweep=lambda x,y:.38-.31*(up*y-.4)-.16*(side*x-1.2)
            bevel_plate(wing,'swept_blade',outline,sweep,'plate',.16)
            cx=sum(x for x,y in outline)/6;cy=sum(y for x,y in outline)/6
            inset=[(cx+(x-cx)*.76,cy+(y-cy)*.84) for x,y in outline]
            bevel_plate(wing,'blade_inset',inset,lambda x,y:sweep(x,y)+.008,'panel',.022)
            spar=[(1.29,.56),(1.65,.88),(1.34,1.89),(1.22,2.10)] if up>0 else \
                [(1.22,.48),(1.65,.70),(1.46,1.30),(1.21,1.50)]
            bevel_plate(wing,'load_bearing_ridge',[(side*x,up*y) for x,y in spar],lambda x,y:sweep(x,y)+.033,'trim',.028)
            strip=[(1.09,.65),(1.13,.95),(1.15,.96),(1.12,.65)]
            bevel_plate(wing,'short_power_slit',[(side*x,up*y) for x,y in strip],lambda x,y:sweep(x,y)+.047,'red',.008)
        outline=[(1.10,-.18),(1.64,-.18),(2.12,.03),(1.90,.50),(1.55,.57),(1.14,.20)]
        pod_front=lambda x,y:.82-.14*(side*x-1.64)
        bevel_plate(wing,'armoured_outrigger',[(side*x,y) for x,y in outline],pod_front,'coal',.36)
        outline=[(1.45,-.10),(1.68,-.10),(1.98,.04),(1.81,.12),(1.43,.10)]
        bevel_plate(wing,'outrigger_cheek',[(side*x,y) for x,y in outline],lambda x,y:pod_front(x,y)+.015,'plate',.045)
        for z in (-.20,.06):
            hull_ring=[[(side*x,.02+cos(tau*i/16)*.21,z+sin(tau*i/16)*.21) for i in range(16)] for x in (.94,1.07)]
            wing.loft('wing_root_bearing',hull_ring,'joint',{0:'steel'})
    cannon=parts['VeilCannon']
    ring(cannon,'structural_breech',[(.47,.53),(.49,.85),(.40,1.20),(.28,1.20),(.27,.53)],'machinery',sides=24)
    ring(cannon,'optical_sleeve',[(.40,1.10),(.405,1.28),(.335,1.53),(.235,1.53),(.26,1.10)],'coal',sides=24)
    ring(cannon,'focus_bezel',[(.35,1.49),(.36,1.57),(.325,1.64),(.248,1.64),(.24,1.49)],'steel',sides=24)
    ring(cannon,'recessed_iris',[(.25,1.41),(.25,1.53),(.14,1.53),(.14,1.41)],'joint')
    for i in range(8):
        a=tau*i/8
        ring(cannon,'bezel_fastener',[(.015,1.62),(.015,1.64),(.008,1.64),(.008,1.62)],'edge',.313*cos(a),.313*sin(a),8)
    for up,name in [(1,'VeilArmourTop'),(-1,'VeilArmourBottom')]:
        armour=parts[name]
        start,end=(.025,tau*.5-.025) if up>0 else (tau*.5+.025,tau-.025)
        # A thick faceted nose, almost closed over the bore in idle. It folds
        # back as a solid jaw, exposing the gun rather than a fan of cooling fins.
        sector(armour,'armoured_bore_jaw',[(.55,.70),(.64,.91),(.61,1.38),(.44,1.69),
            (.18,1.69),(.18,1.59),(.40,1.42),(.46,.70)],start,end,'plate',4,
            {0:'coal',1:'plate',2:'steel',3:'coal',4:'joint',5:'joint',6:'machinery',7:'joint'})
        sector(armour,'nose_armour_face',[(.44,1.687),(.44,1.71),(.185,1.71),(.185,1.687)],start+.005,end-.005,'coal',4,
            {0:'trim',1:'coal',2:'joint',3:'joint'})
        sector(armour,'jaw_reinforcement',[(.637,.92),(.605,1.30),(.57,1.30),(.60,.92)],start+.12,end-.12,'coal',4)
        forehead=[(-.28,.25),(.28,.25),(.20,.38),(-.20,.38)]
        bevel_plate(armour,'jaw_forehead_plate',[(x,.10+up*y) for x,y in forehead],lambda x,y:1.722,'plate',.024)
        for side in (-1,1):
            box(armour,'hinge_trunnion',side*.40,.10+up*.48,.73,.065,.055,.10,'machinery')
            box(armour,'exposed_actuator',side*.39,.10+up*.36,.89,.040,.040,.17,'steel')
    sensor=parts['VeilSensor']
    sensor.loft('sensor_lens',[tube(z,0,0,r) for z,r in [(.10,.118),(.16,.108)]],'glass')
    # Sensor is already authored about its gameplay centre. Other parts are
    # authored in assembled space and translated to their own pivots on export.
    pivot=PIVOTS['VeilSensor']
    sensor.faces=[(n,m,[(x+pivot[0],y+pivot[1],z+pivot[2]) for x,y,z in pts],v) for n,m,pts,v in sensor.faces]
    cover=parts['VeilSensorCover']
    outline=[(-.15,-.23),(.15,-.23),(.245,-.10),(.245,.11),(.14,.25),(-.14,.25),(-.245,.11),(-.245,-.10)]
    bevel_plate(cover,'sensor_hatch',[(-1.10+x,.38+y) for x,y in outline],lambda x,y:.95,'plate',.09)
    box(cover,'hatch_hinge',-1.10,.65,.59,.16,.025,.035,'joint')
    box(cover,'hatch_mark',-1.10,.38,.958,.055,.009,.004,'red')
    glow=parts['VeilCannonGlow']
    ring(glow,'charge_ring',[(.291,1.645),(.291,1.655),(.282,1.655),(.282,1.645)],'red',sides=48)
    glow.loft('optical_core',[[(cos(tau*i/32)*r,sin(tau*i/32)*r,z) for i in range(32)]
        for z,r in [(1.48,.11),(1.50,.105)]],'red')
    for side in (-1,1):
        parts['VeilEngineGlow'].loft('engine_core',[tube(z,side*.37,-.12,r) for z,r in [(-1.12,.17),(-1.10,.175)]],'red')
    return parts


def write_obj(path, mesh, pivot=(0,0,0)):
    lines=['# Veil modular interceptor. Source +Z forward; pivot-local vertices.', 'mtllib VeilInterceptor.mtl', 'o '+path.stem]
    index=1
    for name,material,pts,normal in mesh.faces:
        band=list(COLORS).index(material)
        lines += ['g '+name,'usemtl '+material]
        for p in pts:
            lines.append('v %.7f %.7f %.7f'%tuple(p[i]-pivot[i] for i in range(3)))
        axis=max(range(3),key=lambda i:abs(normal[i]))
        axes=[i for i in range(3) if i!=axis]
        low=[min(p[i] for p in pts) for i in axes]
        span=[max(p[i] for p in pts)-low[k] for k,i in enumerate(axes)]
        for point in pts:
            u,v=[(point[i]-low[k])/max(span[k],1e-7) for k,i in enumerate(axes)]
            lines.append('vt %.7f %.7f'%(band*4+.125+.75*u,.06+.88*v))
        for _ in pts:
            lines.append('vn %.7f %.7f %.7f'%normal)
        for i in range(1,len(pts)-1):
            ids=(index,index+i,index+i+1)
            lines.append('f '+' '.join(f'{j}/{j}/{j}' for j in ids))
        index+=len(pts)
    path.write_text('\n'.join(lines)+'\n',encoding='ascii')


def write_glb(parts):
    """Portable named rigid nodes and an editable mechanical preview action."""
    binary=bytearray();views=[];accessors=[]
    def data(values,kind,components):
        while len(binary)%4:binary.append(0)
        offset=len(binary)
        flat=[v for row in values for v in row]
        binary.extend(struct.pack('<'+('I' if kind==5125 else 'f')*len(flat),*flat))
        view=len(views);views.append(dict(buffer=0,byteOffset=offset,byteLength=len(binary)-offset))
        item=dict(bufferView=view,componentType=kind,count=len(values),type={1:'SCALAR',3:'VEC3',4:'VEC4'}[components])
        if kind==5126 and components in (1,3):
            item.update(min=[min(row[i] for row in values) for i in range(components)],max=[max(row[i] for row in values) for i in range(components)])
        accessors.append(item);return len(accessors)-1
    materials=[]
    for name,color in COLORS.items():
        material=dict(name=name,pbrMetallicRoughness=dict(baseColorFactor=[*color,1],metallicFactor=.65 if name in ('steel','edge','machinery') else .15,roughnessFactor=.46 if name in ('steel','edge') else .7))
        if name=='red':material['emissiveFactor']=[.45,.008,.012]
        materials.append(material)
    meshes=[];nodes=[dict(name='Veil',children=list(range(1,len(parts)+1)))]
    for name,mesh in parts.items():
        primitives=[];pivot=PIVOTS[name]
        for mat in COLORS:
            positions=[];normals=[];indices=[]
            for _,material,points,normal in mesh.faces:
                if material!=mat:continue
                base=len(positions)
                positions.extend(tuple(p[i]-pivot[i] for i in range(3)) for p in points)
                normals.extend([normal]*len(points))
                for i in range(1,len(points)-1):indices.extend([(base,),(base+i,),(base+i+1,)])
            if positions:primitives.append(dict(attributes=dict(POSITION=data(positions,5126,3),NORMAL=data(normals,5126,3)),indices=data(indices,5125,1),material=list(COLORS).index(mat)))
        nodes.append(dict(name=name,mesh=len(meshes),translation=list(pivot)))
        meshes.append(dict(name=name,primitives=primitives))
    times=[0,.2,.7,1.3,1.4,2,2.04,2.15,2.4,2.8,3]
    timeline=data([(t,) for t in times],5126,1)
    opening=[0,.3,1,1,1,1,1,1,.7,0,0]
    samplers=[];channels=[]
    for name,axis,angle in [('VeilWingLeft',1,-.12),('VeilWingRight',1,.12),('VeilArmourTop',0,-SHROUD_ANGLE),('VeilArmourBottom',0,SHROUD_ANGLE),('VeilSensorCover',0,-1.45)]:
        amounts=[0,1,1,1,0,0,0,0,0,0,0] if name=='VeilSensorCover' else opening
        quaternions=[]
        for value in amounts:
            q=[0,0,0,cos(angle*value/2)];q[axis]=sin(angle*value/2);quaternions.append(q)
        samplers.append(dict(input=timeline,output=data(quaternions,5126,4),interpolation='LINEAR'))
        channels.append(dict(sampler=len(samplers)-1,target=dict(node=list(parts).index(name)+1,path='rotation')))
    for name in ('VeilCannon','VeilCannonGlow'):
        pivot=PIVOTS[name]
        recoil=[0,0,0,0,0,0,.14,.06,0,0,0]
        positions=[(pivot[0],pivot[1],pivot[2]-r) for r in recoil]
        samplers.append(dict(input=timeline,output=data(positions,5126,3),interpolation='LINEAR'))
        channels.append(dict(sampler=len(samplers)-1,target=dict(node=list(parts).index(name)+1,path='translation')))
    for name in ('VeilArmourTop','VeilArmourBottom'):
        pivot=PIVOTS[name]
        positions=[(pivot[0],pivot[1],pivot[2]-SHROUD_RETRACT*value) for value in opening]
        samplers.append(dict(input=timeline,output=data(positions,5126,3),interpolation='LINEAR'))
        channels.append(dict(sampler=len(samplers)-1,target=dict(node=list(parts).index(name)+1,path='translation')))
    document=dict(asset=dict(version='2.0',generator='Veil modular mesh authoring'),scene=0,scenes=[dict(nodes=[0])],
        nodes=nodes,meshes=meshes,materials=materials,bufferViews=views,accessors=accessors,
        buffers=[dict(byteLength=len(binary))],animations=[dict(name='Charge_Fire_Recover',samplers=samplers,channels=channels)])
    encoded=json.dumps(document,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4)
    binary.extend(b'\0'*((-len(binary))%4))
    total=12+8+len(encoded)+8+len(binary)
    (OUT/'VeilModular.glb').write_bytes(struct.pack('<4sII',b'glTF',2,total)+struct.pack('<I4s',len(encoded),b'JSON')+encoded+struct.pack('<I4s',len(binary),b'BIN\0')+binary)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    parts=build()
    materials=[]
    for name,color in COLORS.items():
        materials.extend(['newmtl '+name,'Kd %.4f %.4f %.4f'%color,'Ks .18 .18 .18','Ns 35',''])
    (OUT/'VeilInterceptor.mtl').write_text('\n'.join(materials),encoding='ascii')
    row=b''.join(bytes(round(c*255) for c in color[::-1])*16 for color in COLORS.values())
    pixels=row*16
    header=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)+struct.pack('<IiiHHIIiiII',40,192,16,1,24,0,len(pixels),0,0,0,0)
    (OUT/'VeilPalette.bmp').write_bytes(header+pixels)
    combined=Mesh();manifest=[]
    for name,mesh in parts.items():
        pivot=PIVOTS[name]
        # Cannon/petals were authored about y=.10 for the same optical axis.
        if name in ('VeilCannon','VeilCannonGlow'):
            mesh.faces=[(n,m,[(x,y+.10,z) for x,y,z in pts],v) for n,m,pts,v in mesh.faces]
        write_obj(OUT/(name+'.obj'),mesh,pivot)
        combined.faces.extend(mesh.faces)
        manifest.append(dict(name=name,file=name+'.obj',source_pivot=pivot,
            runtime_pivot=[pivot[0],pivot[1],-pivot[2]],triangles=sum(len(f[2])-2 for f in mesh.faces)))
    write_obj(OUT/'VeilAssembled.obj',combined)
    panel=Mesh()
    outline=[(-.14,-.22),(.14,-.22),(.22,-.10),(.22,.10),(.14,.22),(-.14,.22),(-.22,.10),(-.22,-.10)]
    bevel_plate(panel,'breakable_armour',outline,lambda x,y:.20,'coal',.22)
    bevel_plate(panel,'armour_badge',[(-.10,0),(0,-.12),(.10,0),(0,.12)],lambda x,y:.215,'edge',.018)
    box(panel,'armour_status',0,0,.228,.055,.012,.005,'red')
    write_obj(OUT/'VeilBoostPanel.obj',panel)
    write_glb(parts)
    (OUT/'rig.json').write_text(json.dumps(dict(parts=manifest,muzzle_runtime=[0,.10,-1.64],
        sensor_runtime=[-1.10,.38,-.68],source_forward='+Z',runtime_forward='-Z',
        material='existing mode 16 PBR bands',extra_meshes=['VeilBoostPanel.obj'],
        design='armoured floating heavy cannon; long swept upper blades and shorter lower stabilisers',
        shroud_open_radians=SHROUD_ANGLE,shroud_retraction=SHROUD_RETRACT,
        triangles=sum(p['triangles'] for p in manifest)),indent=2))
    print(json.dumps(dict(parts=len(parts),triangles=sum(p['triangles'] for p in manifest))))


if __name__=='__main__':
    main()
