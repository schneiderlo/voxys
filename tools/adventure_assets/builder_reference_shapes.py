"""Original builder geometry following the owner's 2026-09-16 visual reference.

Uses the existing rigid authoring coordinates before the minifigure proportion
transform. Jacket and face details are thin geometry prints, not floating decals.
"""
import math
import bpy
from mathutils import Vector


def sleeve(m, sign):
    # A molded arm is a swept sleeve, not stacked horizontal ellipses. Rings
    # stay perpendicular to a filleted shoulder/elbow/cuff centreline so the
    # elbow has continuous volume without a pinched miter or separate joint.
    # Work in final body dimensions, then undo only the baked X/Z proportions.
    shoulder=Vector((.258,1.057,0))
    elbow=Vector((.332,.917,-.025))
    cuff=Vector((.354,.715,-.074*1.25))
    upper=(elbow-shoulder).normalized();lower=(cuff-elbow).normalized()
    enter=elbow-upper*.043;leave=elbow+lower*.043
    rings=[]
    cap_steps=(5,4,3)[m.lod]
    for i in range(cap_steps):
        a=math.radians(88*(1-i/(cap_steps-1)))
        rings.append((shoulder-upper*(.054*math.sin(a)),upper,
                      .068*math.cos(a),.076*math.cos(a)))
    rings.append((enter,upper,.064,.071))
    elbow_steps=(4,3,2)[m.lod]
    for i in range(1,elbow_steps+1):
        t=i/elbow_steps
        point=(1-t)**2*enter+2*t*(1-t)*elbow+t*t*leave
        tangent=((1-t)*(elbow-enter)+t*(leave-elbow)).normalized()
        rings.append((point,tangent,.064-.004*t,.071-.004*t))
    # Small molded radius around a definite flat cuff; the existing yellow
    # wrist and hand centres remain unchanged, including the bike grip anchor.
    for distance,inset in ((-.005,0),(-.0015,.0015),(0,.005)):
        rings.append((cuff+lower*distance,lower,.066-inset,.069-inset))
    n=(18,14,10)[m.lod];points=[]
    for center,tangent,rx,rz in rings:
        depth=Vector((0,0,1));depth=(depth-tangent*depth.dot(tangent)).normalized()
        side=depth.cross(tangent).normalized()
        for i in range(n):
            a=i*2*math.pi/n;p=center+side*(rx*math.cos(a))+depth*(rz*math.sin(a))
            points.append((sign*p.x/1.22,p.y,p.z/1.25))
    faces=[tuple(reversed(range(n)))]
    faces.extend((r*n+i,r*n+(i+1)%n,(r+1)*n+(i+1)%n,(r+1)*n+i)
                 for r in range(len(rings)-1) for i in range(n))
    faces.append(tuple(range((len(rings)-1)*n,len(rings)*n)))
    m.mesh('continuous_molded_arm',points,faces,'rust')



def torso(m, print_patch):
    # The enclosing trapezoid is supplied by the shared recipe.
    print_patch('grey_tee',[(-.074,.878),(.074,.878),(.071,1.265),(-.071,1.265)],'shirt')
    def line(name, points, width=.0035, color='dark', depth=-.1275):
        for a,b in zip(points,points[1:]):
            dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
            nx,ny=-dy/length*width,dx/length*width
            print_patch(name,[(a[0]+nx,a[1]+ny),(a[0]-nx,a[1]-ny),
                             (b[0]-nx,b[1]-ny),(b[0]+nx,b[1]+ny)],color,depth)
    collar_steps=(16,10,6)[m.lod]
    collar=[(-.071+i*.142/collar_steps,1.240+.035*((-.071+i*.142/collar_steps)/.071)**2) for i in range(collar_steps+1)]
    print_patch('yellow_neckline',[(-.071,1.283),(.071,1.283),*reversed(collar)],'skin',-.127)
    line('tee_collar',collar,.0045,depth=-.129)
    line('inner_tee_collar',[(x,y-.019) for x,y in collar],.0025,depth=-.129)
    line('tee_lower_hem',[(-.051,.902),(0,.900),(.051,.902)],.002,'dark')
    for sign in (-1,1):
        line('jacket_opening',[(sign*.052,.878),(sign*.050,1.145),(sign*.069,1.265)])
        line('lapel',[(sign*.118,1.282),(sign*.117,1.250),(sign*.079,1.201),(sign*.051,1.145)],.004)
        line('pocket_top',[(sign*.053,1.040),(sign*.145,1.046)],.004)
        pocket=[]
        pocket_steps=(12,8,4)[m.lod]
        for i in range(pocket_steps+1):
            t=i/pocket_steps;pocket.append((sign*(.145+.044*t*t),1.046-.108*t))
        line('pocket_opening',pocket,.004)
        if m.lod<2:
            for i in range(6):
                t=.12+i*.13;x=sign*(.145+.044*t*t-.007);y=1.046-.108*t
                line('pocket_stitch',[(x,y),(x+sign*.002,y-.006)],.0015,'cream',-.129)
        line('jacket_side_seam',[(sign*.183,1.035),(sign*.197,.886)],.003)
        if m.lod<2:
            for i in range(9):
                y=.900+i*.026
                line('zip_teeth',[(sign*.051,y),(sign*.057,y+.003)],.0015,'cream',-.129)
    m.cylinder('yellow_neck',(0,1.331,0),.085,.075,1,'skin')


def head(m):
    radius=.152
    head_rings=[]
    for i in range(5):
        a=-math.pi/2+i*math.pi/8
        head_rings.append((1.417+.032*math.sin(a),0,0,.120+.032*math.cos(a),.120+.032*math.cos(a)))
    for i in range(5):
        a=i*math.pi/8
        head_rings.append((1.563+.032*math.sin(a),0,0,.120+.032*math.cos(a),.120+.032*math.cos(a)))
    m.loft('rounded_yellow_head',head_rings,'skin',n=(32,24,16)[m.lod])
    def patch(name,xy,color):
        offset=.0014 if name=='eye_glint' else 0
        pts=[(x,y,-math.sqrt((radius+lift+offset)**2-x*x)) for lift in (.0004,.0012) for x,y in xy]
        n=len(xy);faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
        faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
        m.mesh(name,pts,faces,color,False)
    n=(20,14,10)[m.lod]
    for x in (-.043,.043):
        patch('eye',[(x+.0175*math.cos(i*2*math.pi/n),1.492+.0175*math.sin(i*2*math.pi/n)) for i in range(n)],'dark')
        patch('eye_glint',[(x-.003+.004*math.cos(i*2*math.pi/n),1.498+.005*math.sin(i*2*math.pi/n)) for i in range(n)],'cream')
        xs=[x-.033+i*.066/8 for i in range(9)]
        patch('friendly_eyebrow',[(u,1.532+.010*(1-((u-x)/.033)**2)+d)
              for d,series in ((-.005,xs),(.005,list(reversed(xs)))) for u in series],'hair')
    angles=[-1.05+i*2.10/16 for i in range(17)]
    patch('smile',[((.048+d)*math.sin(a),1.468-(.033+d)*math.cos(a))
          for d,series in ((-.0025,angles),(.0025,list(reversed(angles)))) for a in series],'dark')
    hair(m)


def hair(m):
    # Sculpt one continuous molded hairpiece. Broad S-shaped locks are fused
    # before simplification; no intersecting ribbons or exposed cap seams.
    begin=len(m.objects)
    paths=[
        [(-.035,1.647,-.128),(-.021,1.623,-.163),(.002,1.595,-.178),(.030,1.573,-.178),(.055,1.567,-.164),(.067,1.578,-.145)],
        [(-.049,1.667,-.063),(.010,1.668,-.094),(.066,1.640,-.130),(.123,1.615,-.116),(.164,1.619,-.054),(.156,1.627,.013)],
        [(-.038,1.675,-.015),(.016,1.677,-.029),(.091,1.661,-.060),(.149,1.641,-.019),(.171,1.616,.051),(.154,1.579,.088)],
        [(-.033,1.674,.024),(.033,1.676,.031),(.094,1.658,.047),(.145,1.631,.089),(.150,1.583,.114),(.139,1.537,.112)],
        [(-.026,1.670,.060),(.012,1.663,.094),(.069,1.641,.125),(.105,1.599,.145),(.111,1.553,.138),(.103,1.493,.124)],
        [(-.055,1.650,-.123),(-.090,1.636,-.147),(-.131,1.603,-.138),(-.158,1.569,-.099),(-.160,1.534,-.052),(-.150,1.492,-.007)],
        [(-.061,1.670,-.075),(-.108,1.655,-.083),(-.146,1.622,-.078),(-.174,1.585,-.019),(-.172,1.540,.030),(-.151,1.500,.065)],
        [(-.060,1.678,-.015),(-.112,1.660,.018),(-.155,1.622,.055),(-.164,1.576,.096),(-.139,1.527,.119),(-.112,1.490,.120)],
        [(-.049,1.671,.047),(-.078,1.656,.094),(-.092,1.623,.143),(-.069,1.578,.158),(-.029,1.534,.160),(.004,1.500,.148)],
    ]
    for sign in (-1,1):
        paths.extend([[(sign*x,y,z) for x,y,z in path] for path in [
            [(.135,1.635,-.048),(.163,1.606,-.063),(.179,1.564,-.036),(.170,1.515,.005),(.153,1.482,.029)],
            [(.151,1.621,.025),(.179,1.583,.034),(.181,1.540,.066),(.165,1.492,.092),(.144,1.463,.097)],
        ]])
    # Short overlapping back tiers, with a low scalloped nape. Keep the front
    # fringe and the accepted shoulder/arm geometry independent of this view.
    paths=[path for index,path in enumerate(paths) if index not in (3,4,7,8)]
    # Distinct short nape, diagonal middle and swept crown tiers. Roots are
    # covered by the tier above; exposed ends stagger instead of forming a bob.
    rear_paths=[
        [(-.139,1.530,.105),(-.151,1.492,.127),(-.138,1.458,.137),(-.116,1.442,.127)],
        [(-.080,1.522,.153),(-.091,1.485,.164),(-.075,1.451,.160),(-.053,1.434,.141)],
        [(-.012,1.522,.175),(-.030,1.482,.180),(-.018,1.447,.168),(.004,1.426,.143)],
        [(.059,1.530,.161),(.041,1.492,.177),(.060,1.455,.162),(.079,1.443,.138)],
        [(.126,1.541,.117),(.119,1.502,.145),(.137,1.472,.132),(.148,1.454,.111)],
        [(-.167,1.598,.065),(-.162,1.565,.121),(-.122,1.532,.164),(-.083,1.522,.161)],
        [(-.128,1.622,.092),(-.100,1.586,.157),(-.057,1.552,.186),(-.012,1.543,.174)],
        [(-.059,1.629,.125),(-.016,1.593,.174),(.029,1.562,.185),(.076,1.552,.152)],
        [(.035,1.636,.121),(.078,1.606,.153),(.135,1.576,.138),(.174,1.564,.082)],
        [(-.087,1.670,-.008),(-.133,1.649,.037),(-.165,1.617,.075),(-.156,1.580,.110)],
        [(-.077,1.674,.009),(-.117,1.651,.075),(-.128,1.613,.126),(-.097,1.572,.161)],
        [(-.071,1.677,.023),(-.077,1.651,.103),(-.053,1.613,.156),(-.006,1.568,.181)],
        [(-.060,1.681,.026),(-.029,1.657,.095),(.033,1.617,.154),(.089,1.578,.150)],
        [(-.039,1.683,.009),(.022,1.668,.071),(.099,1.632,.113),(.150,1.591,.093)],
        [(-.012,1.682,-.016),(.066,1.670,.022),(.139,1.644,.058),(.177,1.612,.044)],
    ]
    def catmull(path,t):
        f=t*(len(path)-1);i=min(len(path)-2,int(f));u=f-i
        a,b,c,d=[Vector(path[max(0,min(len(path)-1,k))]) for k in (i-1,i,i+1,i+2)]
        return .5*((2*b)+(-a+c)*u+(2*a-5*b+4*c-d)*u*u+(-a+3*b-3*c+d)*u*u*u)
    import numpy as np
    # Project the directional locks onto a scalp, then sculpt broad flat ridges
    # into that ONE surface. This has no tube intersections, holes or cap seams.
    radii=np.array((.187,.140,.185));center=np.array((0,1.525,0))
    samples=[];amplitudes=[]
    for index,path in enumerate(paths):
        for j in range(41):
            t=j/40;v=(np.array(catmull(path,t))-center)/radii
            samples.append(v/np.linalg.norm(v))
            amplitudes.append((.004+.001*(index%3))*(.25+.75*math.sin(math.pi*t)**.5))
    samples=np.array(samples);amplitudes=np.array(amplitudes)
    n=128;rings=70;directions=[]
    for ring in range(rings):
        t=(ring+1)/rings
        for i in range(n):
            a=i*2*math.pi/n;front=max(0.,-math.sin(a))
            # Broad forehead edge with an asymmetric lock dipping down at
            # the centre-right; only the side/back fall into sideburns.
            central=math.exp(-((math.atan2(math.sin(a+math.pi/2-.16),math.cos(a+math.pi/2-.16)))/.28)**2)
            bottom=1.461+.117*front**.5-.025*central+.007*math.sin(7*a+.8)*front
            bottom+=max(0.,math.sin(a))**.8*(-.024+.009*math.sin(7*a+.4))
            theta=t*math.acos((bottom-center[1])/radii[1])
            directions.append((math.sin(theta)*math.cos(a),math.cos(theta),math.sin(theta)*math.sin(a)))
    directions=np.array([(0,1,0),*directions]);heights=[]
    for chunk in np.array_split(directions,24):
        distance=np.sqrt(np.maximum(0,2-2*chunk@samples.T))*.167
        ridge=np.exp(-(distance/.027)**4)*amplitudes
        heights.extend(np.max(ridge,axis=1))
    points=center+directions*radii+directions*np.array(heights)[:,None]
    faces=[(0,1+i,1+(i+1)%n) for i in range(n)]
    faces.extend((1+r*n+i,1+(r+1)*n+i,1+(r+1)*n+(i+1)%n,1+r*n+(i+1)%n) for r in range(rings-1) for i in range(n))
    # A nonplanar bottom n-gon cuts across the forehead. Close the outer
    # rim into an inner ring buried inside the yellow head instead.
    points=points.tolist();inner_start=len(points)
    for i in range(n):
        x,y,z=points[1+(rings-1)*n+i];r=math.hypot(x,z)
        points.append((x*.133/r,y-.005,z*.133/r))
    for i in range(n):
        a=1+(rings-1)*n+i;b=1+(rings-1)*n+(i+1)%n
        faces.append((a,inner_start+i,inner_start+(i+1)%n,b))
    faces.append(tuple(range(inner_start,inner_start+n)))
    m.mesh('molded_hair_foundation',points,faces,'hair')
    # Wide, low flattened locks intersect the foundation, unlike round tubes
    # suspended over a cap. Projecting their paths guarantees joined roots.
    def surface(path,t):
        q=(np.array(catmull(path,t))-center)/radii;q/=np.linalg.norm(q)
        return Vector(center+q*radii+q*.009),Vector(q)
    for path in paths:
        steps=32;cross=12;pts=[]
        for j in range(steps+1):
            t=j/steps;p,normal=surface(path,t)
            tangent=(surface(path,min(1,t+.003))[0]-surface(path,max(0,t-.003))[0]).normalized()
            side=tangent.cross(normal).normalized()
            normal=(normal-tangent*normal.dot(tangent)).normalized()
            taper=.18+.82*math.sin(math.pi*t)**.4
            for k in range(cross):
                a=k*2*math.pi/cross
                pts.append(tuple(p+side*(.026*taper*math.cos(a))+normal*(.012*taper*math.sin(a))))
        fs=[tuple(reversed(range(cross)))]
        fs.extend((j*cross+k,j*cross+(k+1)%cross,(j+1)*cross+(k+1)%cross,(j+1)*cross+k) for j in range(steps) for k in range(cross))
        fs.append(tuple(range(steps*cross,(steps+1)*cross)))
        m.mesh('flattened_swept_lock',pts,fs,'hair')
    for sign in (-1,1):
        m.ellipsoid('layered_sideburn',(sign*.168,1.485,.027),(.023,.052,.049),'hair')
    bpy.ops.object.select_all(action='DESELECT')
    for obj in m.objects[begin:]:obj.select_set(True)
    bpy.context.view_layer.objects.active=m.objects[begin];bpy.ops.object.join();obj=bpy.context.object
    remesh=obj.modifiers.new('fuse_molded_locks','REMESH');remesh.mode='VOXEL';remesh.voxel_size=.002
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    m.objects[begin:]=[obj];obj.name='single_molded_wavy_hair'
    smooth=obj.modifiers.new('flowing_wave_surface','SMOOTH');smooth.factor=.35;smooth.iterations=2
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    decimate=obj.modifiers.new('bounded_hair_detail','DECIMATE');decimate.ratio=(1100,650,300)[m.lod]/len(obj.data.vertices);decimate.use_collapse_triangulate=True
    bpy.ops.object.modifier_apply(modifier=decimate.name)
    highest=max(v.co.z for v in obj.data.vertices)
    for v in obj.data.vertices:
        x,y,z=v.co.x*.95,v.co.z,v.co.y*.97
        y=1.46+(y-1.46)*(.24)/(highest-1.46)
        dome=Vector((x*1.368,1.7+(y-1.7)*1.65-1.3,z*1.368))
        if dome.length>.417:
            dome*=.417/dome.length
            x,y,z=dome.x/1.368,1.7+(dome.y+1.3-1.7)/1.65,dome.z/1.368
        v.co=(-x,z,y)
    obj['surface_kind']='sculpted'
    # Keep the short back tiers as closed overlapping molded lobes. Fusing
    # these shallow overlaps into one remesh erases the layer boundaries.
    for layer_index,path in enumerate(rear_paths):
        tier=0 if layer_index<5 else 1 if layer_index<9 else 2
        steps=(12,9,6)[m.lod];cross=((10,8,6) if tier==2 else (12,10,8))[m.lod];pts=[]
        for j in range(steps+1):
            t=j/steps;p,normal=surface(path,t)
            tangent=(surface(path,min(1,t+.003))[0]-surface(path,max(0,t-.003))[0]).normalized()
            side=tangent.cross(normal).normalized()
            normal=(normal-tangent*normal.dot(tangent)).normalized()
            taper=.045+.955*math.sin(math.pi*t)**.70
            # Middle/crown tips remain proud of the lower tier, with roots buried.
            p+=normal*(-.014+.012*math.sin(math.pi*t)**.5+.003*t+tier*.002)
            for k in range(cross):
                a=k*2*math.pi/cross
                v=p+side*((.031 if tier==0 else .034 if tier==1 else .026)*taper*math.cos(a))+normal*(.006*taper*math.sin(a))
                x,y,z=v.x*.95,1.46+(v.y-1.46)*.24/(highest-1.46),v.z*.97
                dome=Vector((x*1.368,1.7+(y-1.7)*1.65-1.3,z*1.368))
                if dome.length>.417:
                    dome*=.417/dome.length
                    x,y,z=dome.x/1.368,1.7+(dome.y+1.3-1.7)/1.65,dome.z/1.368
                if y>1.690:y=1.690+.009*math.tanh((y-1.690)/.009)
                pts.append((x,y,z))
        fs=[tuple(reversed(range(cross)))]
        fs.extend((j*cross+k,j*cross+(k+1)%cross,(j+1)*cross+(k+1)%cross,(j+1)*cross+k)
                  for j in range(steps) for k in range(cross))
        fs.append(tuple(range(steps*cross,(steps+1)*cross)))
        layer=m.mesh('rear_overlapping_hair_layer',pts,fs,'hair');layer['surface_kind']='sculpted'


def hand(m,x,center_z):
    # A thick C-shaped extrusion with broad flat faces and rounded rim edges,
    # as on the reference grip. Counter the body width transform for a circle.
    n=(24,20,16)[m.lod];outer=.090;inner=.047;depth=.041;bevel=.006
    profile=[]
    for radial,z,a0 in ((outer-bevel,-depth+bevel,-math.pi/2),(outer-bevel,depth-bevel,0),
                         (inner+bevel,depth-bevel,math.pi/2),(inner+bevel,-depth+bevel,math.pi)):
        for j in range(4):
            a=a0+j*math.pi/6;profile.append((radial+bevel*math.cos(a),z+bevel*math.sin(a)))
    pts=[];k=len(profile)
    for i in range(n+1):
        a=math.radians(-65+310*i/n)
        for r,z in profile:pts.append((x+r*math.cos(a)/1.22,.590+r*math.sin(a),center_z+z))
    faces=[tuple(reversed(range(k)))]
    faces.extend((i*k+j,i*k+(j+1)%k,(i+1)*k+(j+1)%k,(i+1)*k+j) for i in range(n) for j in range(k))
    faces.append(tuple(range(n*k,(n+1)*k)))
    obj=m.mesh('molded_c_grip',pts,faces,'skin')
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    bevel=obj.modifiers.new('rounded_grip_tips','BEVEL');bevel.width=.010
    bevel.segments=(3,2,1)[m.lod];bevel.limit_method='ANGLE';bevel.angle_limit=.65
    bpy.ops.object.modifier_apply(modifier=bevel.name)


def thigh(m,x):
    # The visible upper barrel wraps below the axle before meeting the
    # recessed straight shin. It is one continuous leg, not a cylinder laid
    # over a box. Side caps stay flat; the front hip arc shades smoothly.
    n=(24,18,12)[m.lod];radius=.130;axle=.480
    start=-math.acos(.085/radius)
    profile=[(.115,-.085),(axle+radius*math.sin(start),-.085)]
    profile += [(axle+radius*math.sin(start+i*(math.pi-start)/n),
                 -radius*math.cos(start+i*(math.pi-start)/n)) for i in range(1,n+1)]
    profile += [(.400,.105),(.108,.105),(.105,.118),(.008,.118),(.008,-.169),(.115,-.169)]
    def half_width(y):
        return .102 if y<=.105 else .091+.008*max(0.,min(1.,(.50-y)/.392))
    points=[(x+sign*half_width(y),y,z) for sign in (-1,1) for y,z in profile]
    k=len(profile);faces=[tuple(reversed(range(k))),tuple(range(k,2*k))]
    faces.extend((i,(i+1)%k,(i+1)%k+k,i+k) for i in range(k))
    obj=m.mesh('continuous_leg_with_round_hip',points,faces,'moss')
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    # Blind rear sockets are cut into the leg, never painted circles. Undo the
    # baked width scale in the cutter so the installed openings remain round.
    for cy in (.485,.190):
        segments=(28,20,12)[m.lod];verts=[]
        for depth,radius in ((-.053,.062),(.098,.080),(.200,.080)):
            verts.extend((-x-radius/1.30*math.cos(i*2*math.pi/segments),depth,
                          cy+radius*math.sin(i*2*math.pi/segments)) for i in range(segments))
        cuts=[tuple(reversed(range(segments))),tuple(range(2*segments,3*segments))]
        cuts.extend((r*segments+i,r*segments+(i+1)%segments,(r+1)*segments+(i+1)%segments,(r+1)*segments+i)
                    for r in range(2) for i in range(segments))
        mesh=bpy.data.meshes.new('tapered_socket_tool');mesh.from_pydata(verts,[],cuts);mesh.update()
        cutter=bpy.data.objects.new('rear_socket_cutter',mesh);bpy.context.collection.objects.link(cutter)
        bpy.context.view_layer.objects.active=obj
        cut=obj.modifiers.new('blind_rear_leg_socket','BOOLEAN')
        cut.operation='DIFFERENCE';cut.solver='EXACT';cut.object=cutter
        bpy.ops.object.modifier_apply(modifier=cut.name)
        bpy.data.objects.remove(cutter,do_unlink=True)
        hit,location,normal,index=obj.ray_cast(Vector((-x,.3,cy)),Vector((0,-1,0)))
        assert hit and location.y<0, ('rear socket must have a recessed floor',m.lod,cy,location)
    bevel=obj.modifiers.new('molded_leg_edge_radius','BEVEL');bevel.width=.004
    bevel.segments=(2,1,1)[m.lod];bevel.limit_method='ANGLE';bevel.angle_limit=.65
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    # Mirrored medial D-shaped ledge in the upper socket. Its face remains
    # behind the rear skin, leaving the lateral portion visibly open and deep.
    inward=-1 if x>0 else 1;n=(18,12,8)[m.lod]
    end=math.acos(.022/.080)
    arc=[(x+inward*.080/1.30*math.cos(-end+2*end*i/n),
          .485+.080*math.sin(-end+2*end*i/n)) for i in range(n+1)]
    pts=[(px,py,z) for z in (-.055,.070) for px,py in arc];count=len(arc)
    faces=[tuple(reversed(range(count))),tuple(range(count,2*count))]
    faces.extend((i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count))
    m.mesh('upper_socket_medial_ledge',pts,faces,'moss',False)


def shin(m,x):
    # Retain the rigid ABI's internal shin node inside the continuous toy leg.
    m.box('internal_leg_pin',(x,.199,-.072),(.025,.04,.006),'moss',0)


def foot(m,x):
    # The visible toe is part of the continuous leg above. This thin sole
    # retains the foot node and its real ground-contact bounds without coplanar
    # side faces between an overlapping toe box and the leg shaft.
    m.box('blue_sole',(x,.004,-.032),(.091,.004,.130),'moss',.001)


def hip_bridge(m):
    profile=[]
    for i in range(17):
        y=.385+i*(.60-.385)/16
        z=-math.sqrt(max(0,.130**2-(y-.480)**2))*1.25+.006
        profile.append((y,z))
    profile.extend(((.60,.060),(.385,.060)))
    k=len(profile)
    pts=[(sign*.010/1.42,(y-.00225)/.75,z/1.25) for sign in (-1,1) for y,z in profile]
    faces=[tuple(reversed(range(k))),tuple(range(k,2*k))]
    faces.extend((i,(i+1)%k,(i+1)%k+k,i+k) for i in range(k))
    m.mesh('narrow_curved_hip_connection',pts,faces,'moss')
    # A rounded rear tongue fills the upper cleft, without enlarging the
    # previously corrected front hip arc or closing the lower walking gap.
    width=.017;bottom_center=.402
    outline=[(-width,.610),(width,.610)]
    outline.extend((width*math.cos(i*math.pi/12),bottom_center-width*math.sin(i*math.pi/12)) for i in range(13))
    count=len(outline)
    points=[(x/1.42,(y-.00225)/.75,z/1.25) for z in (.067,.125) for x,y in outline]
    faces=[tuple(reversed(range(count))),tuple(range(count,2*count))]
    faces.extend((i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count))
    m.mesh('rounded_rear_hip_connector',points,faces,'moss')
