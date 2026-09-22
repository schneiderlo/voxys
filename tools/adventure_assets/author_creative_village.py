#!/usr/bin/env python3
"""Original walkable toy village kit. Closed meshes; exported collision recipe."""
import argparse,json,math,sys,struct
from pathlib import Path
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from author_creative_props import Toy,linear,sha
NAMES=('red_cottage','blue_cottage','tower','windmill','garden','path','well','foundation','tall_house','workshop','market','hedge','flower_border','supplies','fence')
COLORS={'plaster':'E8CEA0','timber':'68442F','red':'BE5D40','blue':'507586','stone':'C9BA99','glass':'375A61','green':'668954','wheat':'E8C774','red_tile':'D47B52','blue_tile':'718995','plaster_light':'F3DDB0','moss':'779F53','flowers':'DA9567','soil':'70503A','paving':'E7D3A6'}
class VillageModel(Toy):
    def __init__(self,name,materials):super().__init__(name,0,materials);self.solids=[]
    def solid(self,name,lo,hi,color,bevel=.025):
        self.box(name,lo,hi,color,bevel);self.solids.append([lo,hi])
    def window(self,x,y,z):
        self.box('Window dark recess',(x-.80,y-1,z-.02),(x+.80,y+1,z+.06),'glass',.018)
        for dx in (-.9,.9):self.box('Deep frame',(x+dx-.13,y-1.12,z-.28),(x+dx+.13,y+1.12,z+.10),'timber',.035)
        for dy in (-1.12,1.12):self.box('Lintel and sill',(x-1.1,y+dy-.12,z-.36),(x+1.1,y+dy+.12,z+.12),'timber',.035)
        self.box('Mullion',(x-.07,y-.98,z-.17),(x+.07,y+.98,z+.03),'plaster_light',.018)
        self.box('Transom',(x-.8,y+.06,z-.17),(x+.8,y+.2,z+.03),'plaster_light',.018)
        for side in (-1,1):
            for row in range(4):
                cx=x+side*1.34
                self.box('Shutter slat',(cx-.24,y-.93+row*.48,z-.21),(cx+.24,y-.5+row*.48,z+.06),'green',.03)
    def facing_window(self,x,y,z,yaw):
        from mathutils import Matrix,Vector
        start=len(self.objects);self.window(0,y,0)
        rotation=Matrix.Rotation(yaw,4,'Z');offset=Vector((x,-z,0))
        for obj in self.objects[start:]:
            for vertex in obj.data.vertices:vertex.co=rotation@vertex.co+offset
            obj.data.update()
    def roof(self,w,d,y,color,steps=7):
        rise=w*.52
        for side in (-1,1):
            for course in range(steps):
                a=w*.5*(1-course/steps);b=w*.5*(1-(course+1)/steps)
                x0,x1=sorted((side*a,side*b));x0+=.022;x1-=.022
                y0=y+rise*(1-abs(x0)/(w*.5));y1=y+rise*(1-abs(x1)/(w*.5))
                count=max(2,round(d/1.45));dz=d/count
                for row in range(count):
                    z0=-d/2+row*dz+.025;z1=z0+dz-.05
                    pts=[(x0,y0,z0),(x1,y1,z0),(x1,y1,z1),(x0,y0,z1),
                         (x0,y0-.26,z0),(x1,y1-.26,z0),(x1,y1-.26,z1),(x0,y0-.26,z1)]
                    shade=color+'_tile' if (course*7+row*3)%5==0 else color
                    self.mesh('Molded roof slope',pts,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],shade,.018)
                # Conservative roof course collider, always above clear room height.
                self.solids.append([(x0,min(y0,y1)-.26,-d/2),(x1,max(y0,y1),d/2)])
            x=side*(w/2-.08)
            self.box('Deep eave', (x-.12,y-.40,-d/2),(x+.12,y-.08,d/2),'timber',.035)
        self.box('Roof ridge',(-.26,y+rise-.08,-d/2),(.26,y+rise+.20,d/2),color,.045)
        for z in range(-int(d/2)+1,int(d/2),2):self.stud(0,y+rise+.20,z,color)
    def gable(self,y,z,width,height):
        self.mesh('Gable',[(-width/2,y,z-.16),(width/2,y,z-.16),(0,y+height,z-.16),(-width/2,y,z+.16),(width/2,y,z+.16),(0,y+height,z+.16)],[(0,2,1),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)],'plaster',.012)
        for side in (-1,1):self.cylinder('Gable timber',(side*width/2,y,z-.19),(0,y+height,z-.19),.13,'timber',4)

def cottage(m,color,variant=0):
    h=9.4 if variant==2 else 6.8
    w=5.;d=4.
    m.solid('Floor',(-w,0,-d),(w,.32,d),'stone')
    for x in (-w,w-.48):m.solid('Side wall',(x,.32,-d),(x+.48,h,d),'plaster')
    m.solid('Rear wall',(-w+.48,.32,d-.48),(w-.48,h,d),'plaster')
    for a,b in ((-w+.48,-1.65),(1.65,w-.48)):m.solid('Front wall',(a,.32,-d),(b,h,-d+.48),'plaster')
    m.solid('Door lintel',(-1.65,5.92,-d),(1.65,h,-d+.48),'plaster')
    # Chunky timber framing and brick foundation courses on all elevations.
    for side in (-1,1):
        z=side*d
        for x in (-4.85,-1.83,1.83,4.85):m.box('Oak upright',(x-.16,.32,z-.17),(x+.16,h+.12,z+.17),'timber',.04)
        for y in (1.15,h-.35):
            spans=((-4.95,-1.65),(1.65,4.95)) if side==-1 and y<2 else ((-4.95,4.95),)
            for a,b in spans:m.box('Oak crossbeam',(a,y,z-.2),(b,y+.3,z+.2),'timber',.03)
        for x in (-3.25,3.25):m.facing_window(x,3.7,z+side*.08,0 if side==-1 else math.pi)
        for row in range(3):
            for x in range(-5,5):
                if side==-1 and abs(x+.5)<1.8:continue
                m.box('Foundation brick',(x+.025,.32+row*.27,z-.10),(x+.975,.56+row*.27,z+.10),'stone',.025)
    for side in (-1,1):
        x=side*5.04
        for z in (-3.85,0,3.85):m.box('Side oak upright',(x-.17,.32,z-.16),(x+.17,h,z+.16),'timber',.03)
        for y in (1.15,h-.35):m.box('Side crossbeam',(x-.18,y,-4),(x+.18,y+.3,4),'timber',.03)
        for z in (-2,2):m.facing_window(x+side*.05,3.7,z,-side*math.pi/2)
        for z in (-2,2):m.cylinder('Diagonal wall brace',(x,4.9,z-.8),(x,h-.5,z+.8),.17,'timber',4)
    for z in (-d,d):
        m.gable(h-.26,z,11.6,11.6*.52)
        m.box('Gable king post',(-.18,h-.1,z-.24),(.18,h+5,z+.24),'timber',.03)
        m.box('Gable tie',(-4.95,h-.12,z-.24),(4.95,h+.2,z+.24),'timber',.03)
        m.facing_window(0,h+1.9,z+( -.23 if z<0 else .23),0 if z<0 else math.pi)
    m.roof(11.6,9.6,h,color)
    for y in range(9):
        m.box('Chimney brick',(2.9,h+1.24+y*.52,1.2),(4.05,h+1.73+y*.52,2.45),'stone' if y%3 else 'timber',.03)
    m.box('Chimney crown',(2.73,h+5.92,1.05),(4.22,h+6.24,2.60),'stone',.04)
    m.box('Chimney opening',(3.04,h+6.245,1.35),(3.91,h+6.26,2.3),'glass',0)
    for x in (-3.3,3.3):
        m.box('Flower planter',(x-1.15,2.1,-4.75),(x+1.15,2.55,-4.05),'timber',.04)
        for j in range(5):
            px=x-.86+j*.43;m.stud(px,2.55,-4.43,'green');m.cylinder('Bloom',(px,2.75,-4.43),(px,2.90,-4.43),.18,'flowers' if j%2 else 'wheat',8)
    if variant==1:
        # Broad covered porch, open straight through its middle.
        for x in (-2.45,2.45):m.solid('Porch post',(x-.16,.32,-6.7),(x+.16,6.15,-6.38),'timber',.03)
        m.box('Porch lintel',(-2.65,5.85,-6.75),(2.65,6.15,-6.32),'timber')
        for x in range(-3,3):
            m.box('Porch canopy',(x+.02,6.15,-6.85),(x+.98,6.40,-3.95),color,.03)
    if variant==2:
        for side in (-1,1):
            for x in (-3.25,3.25):m.facing_window(x,7.15,side*4.10,0 if side<0 else math.pi)
            m.box('Upper storey beam',(-5.1,5.62,side*4-.22),(5.1,5.96,side*4+.22),'timber')
    if variant==3:
        # A lower side workshop wing changes the footprint and roof silhouette.
        m.solid('Workshop wing',(5,0,-2),(8.5,4.2,3.5),'plaster')
        for x in (5.1,8.35):m.box('Wing post',(x-.13,.3,-2.12),(x+.13,4.4,-1.92),'timber')
        for row in range(5):m.box('Wing roof slope',(5+row*.68,5.65-row*.30,-2.6),(5.8+row*.68,5.92-row*.30,4),'blue' if row%2 else 'blue_tile',.03)
        m.facing_window(6.8,2.3,-2.13,0)


def build(i,materials):
    m=VillageModel(f'{i:02d}_{NAMES[i]}',materials)
    if i in (0,1,8,9):cottage(m,'blue' if i==1 else 'red', {0:0,1:1,8:2,9:3}[i])
    elif i==2:
        m.solid('Tower floor',(-3,0,-3),(3,.32,3),'stone')
        for x in (-3,2.5):m.solid('Tower side',(x,.32,-3),(x+.5,15,3),'plaster')
        m.solid('Tower rear',(-2.5,.32,2.5),(2.5,15,3),'plaster')
        for a,b in ((-2.5,-1.65),(1.65,2.5)):m.solid('Tower front',(a,.32,-3),(b,15,-2.5),'plaster')
        m.solid('Tower lintel',(-1.65,5.92,-3),(1.65,15,-2.5),'plaster')
        for y in (.32,6.7,13.8):
            for x in (-3.12,2.5):m.box('Side stone belt',(x,y,-3.12),(x+.62,y+.32,3.12),'stone')
            m.box('Rear stone belt',(-2.5,y,2.5),(2.5,y+.32,3.12),'stone')
            for a,b in (((-2.5,-1.65),(1.65,2.5)) if y<1 else ((-2.5,2.5),)):m.box('Front stone belt',(a,y,-3.12),(b,y+.32,-2.5),'stone')
        for x,z,yaw in ((0,-3.18,0),(0,3.18,math.pi),(-3.18,0,math.pi/2),(3.18,0,-math.pi/2)):
            m.facing_window(x,11.8,z,yaw)
        for x in (-2.85,2.85):
            for z in (-2.85,2.85):m.box('Tower corner stone',(x-.21,.32,z-.21),(x+.21,15,z+.21),'stone')
        for y in range(15):
            r=3.8-y*.24;m.solid('Tower spire',(-r,15+y*.58,-r),(r,15+(y+1)*.58,r),'blue' if y%3 else 'blue_tile',.035)
        m.cylinder('Finial',(0,23.7,0),(0,25.0,0),.10,'wheat',8)
    elif i==3:
        m.solid('Mill floor',(-3.5,0,-3.5),(3.5,.32,3.5),'stone')
        for x in (-3.5,3):m.solid('Mill side',(x,.32,-3.5),(x+.5,10.4,3.5),'plaster')
        m.solid('Mill rear',(-3,.32,3),(3,10.4,3.5),'plaster')
        for a,b in ((-3,-1.65),(1.65,3)):m.solid('Mill front',(a,.32,-3.5),(b,10.4,-3),'plaster')
        m.solid('Mill lintel',(-1.65,5.92,-3.5),(1.65,10.4,-3),'plaster')
        for z in (-3.5,3.5):m.gable(10.4-.26,z,8.2,8.2*.52)
        m.roof(8.2,8.3,10.4,'red',6)
        for x in (-3.25,3.25):m.box('Mill upright',(x-.17,.32,-3.66),(x+.17,10.4,-3.38),'timber')
        for y in (1.1,6.3,10.1):m.box('Mill belt',(-3.6,y,3.36),(3.6,y+.28,3.64),'timber')
        for angle in (math.pi/4,3*math.pi/4):
            dx,dy=math.cos(angle),math.sin(angle)
            m.cylinder('Sail spar',(-dx*6,10.7-dy*6,-4.25),(dx*6,10.7+dy*6,-4.25),.13,'timber',4)
            for side in (-1,1):
                for t in (2,3,4,5):
                    x=side*dx*t;y=10.7+side*dy*t
                    # Four paddle frames, open slats rather than opaque windmill discs.
                    m.cylinder('Sail lattice',(x-dy*.8,y+dx*.8,-4.28),(x+dy*.8,y-dx*.8,-4.28),.12,'wheat',4)
        m.cylinder('Sail hub',(0,10.7,-4.6),(0,10.7,-4),.42,'timber')
    elif i==4:
        m.solid('Garden bed',(-5,0,-3),(5,.32,3),'soil')
        for z in (-2.3,-1.15,0,1.15,2.3):
            for ix in range(12):
                x=-4.5+ix*.8
                m.cylinder('Crop stem',(x,.32,z),(x,1.02,z),.085,'green',8)
                m.cylinder('Wheat ear',(x,.84,z),(x,1.4,z),.16,'wheat',8)
        for z in (-3,3):
            for x in (-5,0,5):m.box('Fence post',(x-.12,0,z-.12),(x+.12,1.6,z+.12),'timber')
            for y in (.6,1.15):m.box('Fence rail',(-5,y,z-.08),(5,y+.12,z+.08),'timber')
    elif i==5:
        m.solid('Paving tile',(-1,0,-1),(1,.32,1),'paving',.018)
        for x in (-.5,.5):
            for z in (-.5,.5):m.box('Inset paver',(x-.46,.32,z-.46),(x+.46,.35,z+.46),'paving',.012)
    elif i==6:
        for y in range(4):
            for x,z,w,d in ((0,-1.25,3,.5),(0,1.25,3,.5),(-1.25,0,.5,2),(1.25,0,.5,2)):
                m.solid('Well course',(x-w/2,y*.32,z-d/2),(x+w/2,(y+1)*.32-.015,z+d/2),'stone')
        m.box('Water',(-.98,.2,-.98),(.98,.25,.98),'glass',0)
        for x in (-1.8,1.8):m.solid('Well post',(x-.12,0,-.12),(x+.12,4.1,.12),'timber')
        m.cylinder('Cross beam',(-1.9,3.7,0),(1.9,3.7,0),.17,'timber',8)
        m.roof(4.4,3.8,4,'red',3)
    elif i==10:
        for x in (-3.5,3.5):
            for z in (-1.8,1.8):m.solid('Stall upright',(x-.13,0,z-.13),(x+.13,5.7,z+.13),'timber')
        for j in range(8):
            x=-4+j;m.box('Striped canvas',(x+.01,5.4,-2.5),(x+.99,5.62,2.4),'plaster_light' if j%2 else 'red',.025)
            m.box('Scalloped valance',(x+.01,4.94,-2.5),(x+.99,5.42,-2.26),'plaster_light' if j%2 else 'red',.04)
        m.solid('Market counter',(-3.5,0,-1.9),(3.5,2.15,-.9),'timber')
        for x in (-2.3,0,2.3):
            m.box('Produce tray',(x-.85,2.15,-1.85),(x+.85,2.4,-.95),'soil')
            for dx in (-.5,0,.5):m.stud(x+dx,2.4,-1.4,'wheat' if x<0 else 'flowers')
    elif i==11:
        m.solid('Hedge base',(-3,0,-.65),(3,.45,.65),'stone')
        for j in range(6):
            x=-2.5+j
            for y,w in ((.45,1.),(.98,.70)):
                m.box('Clipped leaf brick',(x-w/2,y,-w/2),(x+w/2,y+.48,w/2),'green' if j%2 else 'moss',.09)
            m.stud(x,1.46,0,'moss')
    elif i==12:
        m.solid('Border soil',(-3,0,-.8),(3,.3,.8),'soil')
        for j in range(12):
            x=-2.75+j*.5;z=.3 if j%2 else -.3
            m.box('Bush leaf',(x-.3,.3,z-.27),(x+.3,.78,z+.27),'green',.09)
            m.cylinder('Petal cluster',(x,.78,z),(x,.92,z),.24,'flowers' if j%3 else 'wheat',8)
    elif i==13:
        for x,z in ((-.9,0),(.8,.25)):
            m.cylinder('Barrel body',(x,0,z),(x,1.8,z),.67,'timber',12)
            for y in (.18,1.4):m.cylinder('Barrel hoop',(x,y,z),(x,y+.18,z),.7,'stone',12)
            m.stud(x,1.8,z,'timber')
            m.solids.append([(x-.7,0,z-.7),(x+.7,1.98,z+.7)])
    elif i==14:
        for x in (-3,0,3):m.solid('Fence post',(x-.16,0,-.16),(x+.16,1.9,.16),'timber')
        for y in (.6,1.3):m.solid('Fence rail',(-3,y,-.1),(3,y+.22,.1),'timber')
    else:m.solid('Foundation' ,(-.5,0,-.5),(.5,1,.5),'stone',.008)
    obj,report=m.finish();return dict(name=NAMES[i],kind=i,solids=m.solids,**report)

def main():
    p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);out=a.output_dir.resolve();assert not out.exists();out.mkdir()
    bpy.ops.wm.read_factory_settings(use_empty=True);materials={}
    for name,color in COLORS.items():
        mat=bpy.data.materials.new('village_'+name);mat.use_nodes=True;mat.use_backface_culling=True
        bs=mat.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=tuple(linear(int(color[j:j+2],16)) for j in (0,2,4))+(1.,);bs.inputs['Roughness'].default_value=.57 if name in ('red','blue','red_tile','blue_tile') else .70;materials[name]=mat
    props=[build(i,materials) for i in range(len(NAMES))]
    bpy.ops.object.select_all(action='SELECT');blend=out/'creative-village.blend';glb=out/'creative-village.glb';bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)
    bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_yup=True,export_apply=True,export_materials='EXPORT',export_normals=True,export_texcoords=False,export_tangents=False,export_animations=False,export_skins=False,export_morph=False,export_cameras=False,export_lights=False,export_extras=False,export_vertex_color='NONE',export_all_vertex_colors=False)
    raw=glb.read_bytes();n,tag=struct.unpack_from('<II',raw,12);doc=json.loads(raw[20:20+n]);assert len(doc['meshes'])==len(doc['nodes'])==len(NAMES)
    order=sorted(range(len(NAMES)),key=lambda i:doc['meshes'][i]['name']);mapping={old:new for new,old in enumerate(order)};doc['meshes']=[doc['meshes'][i] for i in order]
    for node in doc['nodes']:node['mesh']=mapping[node['mesh']];assert not any(k in node for k in ('translation','rotation','scale','children'))
    doc['nodes'].sort(key=lambda q:q['mesh']);doc['scenes'][0]['nodes']=list(range(len(NAMES)));text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);tail=raw[20+n:];glb.write_bytes(struct.pack('<III',0x46546c67,2,20+len(text)+len(tail))+struct.pack('<II',len(text),tag)+text+tail)
    report=dict(schema=1,asset_id='voxys-creative-village-r01',render_to_canonical=0,author_sha256=sha(__file__),helper_sha256=sha(Path(__file__).with_name('author_creative_props.py')),blend_sha256=sha(blend),glb_sha256=sha(glb),props=props,provenance='Original procedural LEGO-style village geometry. No external images/models. Closed meshes; collision boxes authored with physical pieces. Editable Blender/GLB.')
    (out/'provenance.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
