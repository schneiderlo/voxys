import bpy,sys,math,json,time,os
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
import argparse, hashlib
p=argparse.ArgumentParser(description='Bake physical surfaces from the imported LEGO geometry.')
p.add_argument('--blend',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--header',type=Path,required=True)
p.add_argument('--wall-section',type=Path,help='Refine this released section locally without character-sized morphological closing')
p.add_argument('--member-regions',action='store_true',help='Refine only the union of per-source-part regions, preserving coarse collision between separated members')
p.add_argument('--maximum-boxes',type=int,default=6000,help='Authored local bake limit; values above 6000 require native scene admission proof')
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
assert 1<=a.maximum_boxes<=6500, 'Local collision box limit must be within 1..6500'
bpy.ops.wm.open_mainfile(filepath=str(a.blend.resolve()))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
assert len(objects)==1, 'Use the joined runtime blend, not the editable part assembly'
ob=objects[0]
assert all(abs(ob.matrix_world[r][c]-float(r==c))<1.e-7 for r in range(4) for c in range(4)), 'Apply world transforms before collision baking'
mesh=ob.data;mesh.calc_loop_triangles()
verts=[(v.co.x,v.co.z,-v.co.y) for v in mesh.vertices];tris=[tuple(t.vertices) for t in mesh.loop_triangles]
bvh=BVHTree.FromPolygons(verts,tris,all_triangles=True)
pitch=.4
ypitch=.2
spacing=[pitch,ypitch,pitch]
lo=[math.floor(min(v[i] for v in verts)/spacing[i])*spacing[i] for i in range(3)];hi=[max(v[i] for v in verts) for i in range(3)];n=[math.ceil((hi[i]-lo[i])/spacing[i])+1 for i in range(3)]
occupied=set();start=time.monotonic()
def idx(p):return tuple(max(0,min(n[i]-1,int(math.floor((p[i]-lo[i])/spacing[i])))) for i in range(3))
# Sample visible surfaces from every axis, so vertical walls and ceilings are
# covered as well as walkable tops. Keep openings and room interiors empty.
for axis in range(3):
 axes=[i for i in range(3) if i!=axis];direction=Vector([float(i==axis) for i in range(3)])
 for u in range(n[axes[0]]):
  for v in range(n[axes[1]]):
   origin=Vector(lo);origin[axis]-=pitch;origin[axes[0]]+=(u+.5)*spacing[axes[0]];origin[axes[1]]+=(v+.5)*spacing[axes[1]]
   for hit in range(512):
    point,normal,face,distance=bvh.ray_cast(origin,direction,(hi[axis]-lo[axis])+2*pitch)
    if point is None:break
    occupied.add(idx(point-normal*.0002))
    origin=point+direction*.0001
   else:raise RuntimeError('Intersection budget exceeded')
 print('AXIS',axis,'CELLS',len(occupied),'SECONDS',time.monotonic()-start,flush=True)
# Greedy occupied-cell boxes cover exactly the sampled union, never empty rooms.
import numpy as np
mask=np.zeros(n,dtype=bool)
for cell in occupied:mask[cell]=True
# Close sub-stud seams a 2.24-stud-wide capsule cannot enter, preserving room
# openings while avoiding one collider for every stud and brick seam.
def morph(array,dilate):
 padded=np.pad(array,1);result=np.zeros_like(array) if dilate else np.ones_like(array)
 for a in range(3):
  for b in range(3):
   for c in range(3):
    view=padded[a:a+n[0],b:b+n[1],c:c+n[2]]
    if dilate:result|=view
    else:result&=view
 return result
closed=morph(morph(mask,True),False)|mask
occupied={tuple(map(int,q)) for q in np.argwhere(closed)}
original=set(occupied);boxes=[]
for x,y,z in sorted(original,key=lambda q:(q[1],q[2],q[0])):
 if (x,y,z) not in occupied:continue
 x1=x+1
 while (x1,y,z) in occupied:x1+=1
 z1=z+1
 while all((xx,y,z1) in occupied for xx in range(x,x1)):z1+=1
 y1=y+1
 while all((xx,y1,zz) in occupied for xx in range(x,x1) for zz in range(z,z1)):y1+=1
 for yy in range(y,y1):
  for zz in range(z,z1):
   for xx in range(x,x1):occupied.remove((xx,yy,zz))
 boxes.append([[round(lo[0]+x*pitch,6),max(0,round(lo[1]+y*ypitch,6)),round(lo[2]+z*pitch,6)],[round(lo[0]+x1*pitch,6),round(lo[1]+y1*ypitch,6),round(lo[2]+z1*pitch,6)]])
boxes=[b for b in boxes if all(b[0][i]<b[1][i] for i in range(3))]
refinement=None
if a.wall_section:
 section=json.loads(a.wall_section.read_text())
 # Exact catalog shell/stud occupied boxes, matching imported_assembly.cpp.
 # These are source-member exclusions, never a whole facade air-volume cut.
 from mathutils import Matrix
 exclusions=[];member_regions=[]
 for part in section['parts']:
  first_exclusion=len(exclusions)
  x=part['studsX']*.5;z=part['studsZ']*.5;h=part['height'];m=Matrix(part['matrixRows'])
  local=[([-x,-.2,-z],[x,0,z]),([-x,-h,-z],[-x+.2,-.2,z]),([x-.2,-h,-z],[x,-.2,z]),
         ([-x+.2,-h,-z],[x-.2,-.2,-z+.2]),([-x+.2,-h,z-.2],[x-.2,-.2,z])]
  for iz in range(part['studsZ']):
   for ix in range(part['studsX']):
    cx=ix-(part['studsX']-1)*.5;cz=iz-(part['studsZ']-1)*.5
    for hx,hz in [(.28,.1),(.24,.18),(.18,.24),(.1,.28)]:local.append(([cx-hx,0,cz-hz],[cx+hx,.2,cz+hz]))
  for minimum,maximum in local:
   points=[m@Vector((minimum[0] if not c&1 else maximum[0],minimum[1] if not c&2 else maximum[1],minimum[2] if not c&4 else maximum[2])) for c in range(8)]
   # The scene compiler rounds collider boundaries outward on a .02 lattice.
   # Reserve one lattice tick so clipping is not undone during physical cooking.
   exclusions.append([[min(v[k] for v in points)-.0201 for k in range(3)],[max(v[k] for v in points)+.0201 for k in range(3)]])
  member=exclusions[first_exclusion:]
  member_regions.append([[round(math.floor((min(b[0][k] for b in member)-.2)/.2)*.2,6) for k in range(3)],
                         [round(math.ceil((max(b[1][k] for b in member)+.2)/.2)*.2,6) for k in range(3)]])
 # Canonical shared cut planes avoid sliver fragments from import float noise.
 # Outward snapping adds less than one further authored-lattice tick.
 exclusions=[[[round(math.floor(b[0][k]/.02)*.02,6) for k in range(3)],
              [round(math.ceil(b[1][k]/.02)*.02,6) for k in range(3)]] for b in exclusions]
 local_pitch=.2
 local_lo=[math.floor((min(b[0][k] for b in exclusions)-.4)/local_pitch)*local_pitch for k in range(3)]
 local_hi=[math.ceil((max(b[1][k] for b in exclusions)+.4)/local_pitch)*local_pitch for k in range(3)]
 local_n=[int(round((local_hi[k]-local_lo[k])/local_pitch)) for k in range(3)]
 def subtract(box,cut):
  lower=[max(box[0][k],cut[0][k]) for k in range(3)];upper=[min(box[1][k],cut[1][k]) for k in range(3)]
  if any(lower[k]>=upper[k]-1e-9 for k in range(3)):return [box]
  pieces=[];lo=list(box[0]);hi=list(box[1])
  for k in range(3):
   if lo[k]<lower[k]:q=hi.copy();q[k]=lower[k];pieces.append([lo.copy(),q]);lo[k]=lower[k]
   if hi[k]>upper[k]:q=lo.copy();q[k]=upper[k];pieces.append([q,hi.copy()]);hi[k]=upper[k]
  return pieces
 # Preserve coarse collision unchanged outside the small selected source region.
 regions=member_regions if a.member_regions else [[local_lo,local_hi]]
 outside=boxes
 for region in regions:outside=[piece for box in outside for piece in subtract(box,region)]
 fine=set()
 def fine_idx(point):return tuple(max(0,min(local_n[k]-1,int(math.floor((point[k]-local_lo[k])/local_pitch)))) for k in range(3))
 for axis in range(3):
  others=[k for k in range(3) if k!=axis];direction=Vector([float(k==axis) for k in range(3)])
  for u in range(local_n[others[0]]):
   for v in range(local_n[others[1]]):
    origin=Vector(local_lo);origin[axis]-=.0001;origin[others[0]]+=(u+.5)*local_pitch;origin[others[1]]+=(v+.5)*local_pitch
    for hit in range(512):
     remaining=local_hi[axis]-origin[axis]
     if remaining<=0:break
     point,normal,face,distance=bvh.ray_cast(origin,direction,remaining)
     if point is None:break
     q=point-normal*.0002
     if any(all(region[0][k]<=q[k]<region[1][k] for k in range(3)) for region in regions):fine.add(fine_idx(q))
     origin=point+direction*.0001
    else:raise RuntimeError('Local intersection budget exceeded')
 fine_original=set(fine);fine_boxes=[]
 for x,y,z in sorted(fine_original,key=lambda q:(q[1],q[2],q[0])):
  if (x,y,z) not in fine:continue
  x1=x+1
  while (x1,y,z) in fine:x1+=1
  z1=z+1
  while all((xx,y,z1) in fine for xx in range(x,x1)):z1+=1
  y1=y+1
  while all((xx,y1,zz) in fine for xx in range(x,x1) for zz in range(z,z1)):y1+=1
  for yy in range(y,y1):
   for zz in range(z,z1):
    for xx in range(x,x1):fine.remove((xx,yy,zz))
  fine_boxes.append([[round(local_lo[k]+q*local_pitch,6) for k,q in enumerate((x,y,z))],[round(local_lo[k]+q*local_pitch,6) for k,q in enumerate((x1,y1,z1))]])
 # Voxel surface cells can straddle a true source-part interface. Clip only
 # occupied selected shell/stud volume; fixed neighbors remain around it.
 refined=[]
 for box in fine_boxes:
  pieces=[box]
  for cut in exclusions:
   pieces=[piece for old in pieces for piece in subtract(old,cut)]
   if not pieces:break
  refined.extend(pieces)
 # Recombine adjacent fragments after source-volume clipping. This preserves
 # the exact occupied union; it never fills an empty voxel or deleted brick.
 def merge_boxes(values):
  for _ in range(12):
   previous_count=len(values)
   for axis in range(3):
    groups={}
    for box in values:
     axes=[k for k in range(3) if k!=axis]
     key=tuple(round(box[end][k],6) for k in axes for end in (0,1))
     groups.setdefault(key,[]).append(box)
    values=[]
    for group in groups.values():
     group.sort(key=lambda b:b[0][axis]);last=None
     for box in group:
      if last is not None and abs(last[1][axis]-box[0][axis])<1e-7:last[1][axis]=box[1][axis]
      else:last=[list(box[0]),list(box[1])];values.append(last)
   if len(values)==previous_count:break
  return values
 refined=merge_boxes(refined)
 boxes=outside+refined
 if a.member_regions:boxes=merge_boxes(boxes)
 boxes=[b for b in boxes if all(b[1][k]-b[0][k]>.00001 for k in range(3))]
 refinement={'sectionSha256':hashlib.sha256(a.wall_section.read_bytes()).hexdigest(),'bounds':[local_lo,local_hi],
             'spacing':[local_pitch]*3,'morphologicalClosing':False,'selectedProxyExclusions':len(exclusions),
             'minimumQuantizationClearance':.0201,'maximumQuantizationClearance':.0401,'exclusionGrid':.02,'sampledCells':len(fine_original),'sampledBoxes':len(fine_boxes),
             'refinedBoxes':len(refined),'outsideBoxes':len(outside)}
 if a.member_regions:refinement['memberRegions']=regions
 print('LOCAL_REFINEMENT',json.dumps(refinement),flush=True)
report=dict(spacing=spacing,cells=len(original),boxes=boxes,seconds=time.monotonic()-start)
if refinement:report['localRefinement']=refinement
assert len(boxes)<=(a.maximum_boxes if refinement else 5000), f'Collision exceeds the admitted budget: {len(boxes)} boxes'
report['source_blend_sha256']=hashlib.sha256(a.blend.read_bytes()).hexdigest()
report['recipe_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
a.output.write_text(json.dumps(report,indent=2)+'\n')
def vec(v):return '{'+','.join(format(x,'.8g') for x in v)+'}'
lines=['#pragma once','// Generated from the actual imported mesh by bake_ldraw_collision.py.', '#include <array>','#include <glm/vec3.hpp>','namespace voxy::game::adventure {', 'struct BlacksmithCollisionBox {glm::dvec3 minimum,maximum;};', 'inline constexpr std::array<BlacksmithCollisionBox,'+str(len(boxes))+'> blacksmithMeshSolids{{']
lines += ['{'+vec(b[0])+','+vec(b[1])+'},' for b in boxes]
lines += ['}};','}']
if a.wall_section:lines=[line.replace('BlacksmithCollisionBox','BlacksmithRemainderBox').replace('blacksmithMeshSolids','blacksmithRemainderSolids') for line in lines]
a.header.write_text('\n'.join(lines)+'\n')
print('RESULT',len(boxes),time.monotonic()-start,flush=True)
sys.stdout.flush();os._exit(0)
