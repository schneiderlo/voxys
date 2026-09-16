// Authored exterior against the same two heightfield triangles per grid cell
// used by terrain_surface. No vertex-only sampling of a large hull face.
struct AuthoredTerrainPoint {
    point: vec3<f32>, separation: f32, normal: vec3<f32>, feature: u32,
};
struct AuthoredTerrainResult {
    items: array<AuthoredTerrainPoint,16>, count: u32,
    // 1 invalid shape/field, 2 cell-work limit. Nonzero means incomplete.
    status: u32, cells: u32, reductions: u32,
};
struct AuthoredTerrainPolygon { points: array<vec3<f32>,16>, count: u32, };

fn authored_terrain_clip(input: AuthoredTerrainPolygon, axis: vec2<f32>, offset: f32) -> AuthoredTerrainPolygon {
    var source = input; var result: AuthoredTerrainPolygon;
    if (source.count == 0u) { return result; }
    var previous = source.points[source.count-1u];
    var previousDistance = dot(previous.xz,axis)-offset;
    for (var i=0u; i<source.count; i++) {
        let point=source.points[i]; let distance=dot(point.xz,axis)-offset;
        if ((previousDistance<=0.0) != (distance<=0.0)) {
            let t=previousDistance/(previousDistance-distance);
            result.points[result.count]=mix(previous,point,t); result.count++;
        }
        if (distance<=0.0) { result.points[result.count]=point; result.count++; }
        previous=point; previousDistance=distance;
    }
    return result;
}

fn authored_terrain_height(raw: u32, heightScale: f32) -> f32 {
    return heightScale*(2.0*f32(raw)/65535.0-1.0);
}

// Max-mip rejection is only an acceleration. A texture without enough mips
// falls back to triangle coverage instead of scanning an unbounded base level.
fn authored_terrain_above(field: texture_2d<u32>, params: vec4<f32>, size: vec2<u32>,
                          lower: vec3<f32>, upper: vec3<f32>, margin: f32) -> bool {
    let maximum=vec2<f32>(size-vec2<u32>(1u));
    let lo=(lower.xz-vec2<f32>(margin)+params.xy)/params.z;
    let hi=(upper.xz+vec2<f32>(margin)+params.xy)/params.z;
    if (any(hi<vec2<f32>(0)) || any(lo>maximum)) { return true; }
    let first=vec2<u32>(floor(clamp(lo,vec2<f32>(0),maximum)));
    let last=vec2<u32>(ceil(clamp(hi,vec2<f32>(0),maximum)));
    var level=0u;
    while (level+1u<textureNumLevels(field) && any((last>>vec2<u32>(level))-(first>>vec2<u32>(level))>vec2<u32>(3u))) { level++; }
    let mipMaximum=textureDimensions(field,i32(level))-vec2<u32>(1u);
    let a=min(first>>vec2<u32>(level),mipMaximum);
    let b=min(last>>vec2<u32>(level),mipMaximum);
    if (any(b-a>vec2<u32>(3u))) { return false; }
    var highest=0u;
    for (var z=a.y; z<=b.y; z++) {
        for (var x=a.x; x<=b.x; x++) {
            highest=max(highest,textureLoad(field,vec2<i32>(i32(x),i32(z)),i32(level)).x);
        }
    }
    return lower.y>authored_terrain_height(highest,params.w)+margin;
}

// Bounded manifold preparation: merge coincident points, keep the deepest,
// and replace the most redundant other point when the 16-point reservoir fills.
// This avoids keeping sixteen adjacent grid points at one end of a long hull.
fn authored_terrain_append(result: ptr<function,AuthoredTerrainResult>, value: AuthoredTerrainPoint) {
    for (var i=0u; i<(*result).count; i++) {
        let delta=(*result).items[i].point-value.point;
        if (dot(delta,delta)<1e-8 && dot((*result).items[i].normal,value.normal)>.9999) {
            if (value.separation<(*result).items[i].separation
                || (value.separation==(*result).items[i].separation && value.feature<(*result).items[i].feature)) {
                (*result).items[i]=value;
            }
            return;
        }
    }
    if ((*result).count<16u) { (*result).items[(*result).count]=value; (*result).count++; return; }
    // Read the reservoir in place. A second 17-point function-local array
    // inflated scratch across the nested face/stud calls and failed in the
    // browser's full static-contact pipeline even when the helper ran alone.
    // Keep the same deepest-point and nearest-neighbor removal rule.
    var deepest=16u;
    var deepestSeparation=value.separation;
    for (var i=0u; i<16u; i++) {
        if ((*result).items[i].separation<deepestSeparation) {
            deepest=i; deepestSeparation=(*result).items[i].separation;
        }
    }
    var remove=16u; var closest=1e30; var removeSeparation=value.separation;
    for (var i=0u; i<17u; i++) {
        if (i==deepest) { continue; }
        var candidate=value;
        if (i<16u) { candidate=(*result).items[i]; }
        var nearest=1e30;
        for (var j=0u; j<17u; j++) {
            if (i==j) { continue; }
            var other=value;
            if (j<16u) { other=(*result).items[j]; }
            let delta=candidate.point-other.point;
            nearest=min(nearest,dot(delta,delta));
        }
        if (nearest<closest || (nearest==closest && candidate.separation>removeSeparation)) {
            remove=i; closest=nearest; removeSeparation=candidate.separation;
        }
    }
    if (remove<16u) { (*result).items[remove]=value; }
    (*result).reductions++;
}

// Write into caller-owned scratch: returning the complete contact array causes
// SwiftShader to fail compiling the combined static-contact kernel. Clearing
// here preserves the original zero-initialized result on every call.
fn authored_terrain_contacts(result: ptr<function,AuthoredTerrainResult>, field: texture_2d<u32>, params: vec4<f32>, size: vec2<u32>,
                             shape: AuthoredShapeView, rootPosition: vec3<f32>, rootOrientation: vec4<f32>,
                             margin: f32, cellBudget: u32) {
    (*result) = AuthoredTerrainResult();
    if (!shape.valid || any(size<vec2<u32>(2u)) || params.z<=0.0 || params.w<=0.0) {
        (*result).status=1u; return;
    }
    let axisX=authored_quat_rotate(rootOrientation,vec3<f32>(1,0,0));
    let axisY=authored_quat_rotate(rootOrientation,vec3<f32>(0,1,0));
    let axisZ=authored_quat_rotate(rootOrientation,vec3<f32>(0,0,1));
    var nodeIndex=0u;
    while (nodeIndex<shape.node_count) {
        let node=authored_node(shape,nodeIndex);
        if (!node.valid) { (*result).status=1u; (*result).count=0u; return; }
        let center=rootPosition+authored_quat_rotate(rootOrientation,.5*(node.minimum+node.maximum));
        let half=.5*(node.maximum-node.minimum);
        let extent=abs(axisX)*half.x+abs(axisY)*half.y+abs(axisZ)*half.z;
        if (authored_terrain_above(field,params,size,center-extent,center+extent,margin)) { nodeIndex=node.escape; continue; }
        nodeIndex++;
        if (node.cell==0xffffffffu) { continue; }
        let cell=authored_cell(shape,node.cell);
        if (!cell.valid) { (*result).status=1u; (*result).count=0u; return; }
        for (var f=cell.first_face; f<cell.first_face+cell.face_count; f++) {
            let face=authored_face(shape,f);
            if (!face.valid) { (*result).status=1u; (*result).count=0u; return; }
            var localNormal=vec3<f32>(0); localNormal[face.axis]=f32(face.sign);
            let outward=authored_quat_rotate(rootOrientation,localNormal);
            // The lower envelope and its vertical boundary meet a heightfield.
            if (outward.y>1e-5) { continue; }
            let u=(face.axis+1u)%3u; let v=(face.axis+2u)%3u;
            var polygon: AuthoredTerrainPolygon; polygon.count=4u;
            var lower=vec3<f32>(1e30); var upper=vec3<f32>(-1e30);
            for (var corner=0u; corner<4u; corner++) {
                let order=corner^(corner>>1u); var point=face.minimum;
                point[u]=select(face.minimum[u],face.maximum[u],(order&1u)!=0u);
                point[v]=select(face.minimum[v],face.maximum[v],(order&2u)!=0u);
                point=rootPosition+authored_quat_rotate(rootOrientation,point);
                polygon.points[corner]=point; lower=min(lower,point); upper=max(upper,point);
            }
            if (authored_terrain_above(field,params,size,lower,upper,margin)) { continue; }
            let maximum=vec2<f32>(size-vec2<u32>(2u));
            let first=vec2<u32>(floor(clamp((lower.xz+params.xy)/params.z,vec2<f32>(0),maximum)));
            let last=vec2<u32>(floor(clamp((upper.xz+params.xy)/params.z,vec2<f32>(0),maximum)));
            for (var z=first.y; z<=last.y; z++) {
                for (var x=first.x; x<=last.x; x++) {
                    if ((*result).cells>=cellBudget) { (*result).status=2u; (*result).count=0u; return; }
                    (*result).cells++;
                    let origin=vec2<f32>(f32(x),f32(z))*params.z-params.xy;
                    var clipped=authored_terrain_clip(polygon,vec2<f32>(-1,0),-origin.x);
                    clipped=authored_terrain_clip(clipped,vec2<f32>(1,0),origin.x+params.z);
                    clipped=authored_terrain_clip(clipped,vec2<f32>(0,-1),-origin.y);
                    clipped=authored_terrain_clip(clipped,vec2<f32>(0,1),origin.y+params.z);
                    if (clipped.count==0u) { continue; }
                    let xy=vec2<i32>(i32(x),i32(z));
                    let tl=authored_terrain_height(textureLoad(field,xy,0).x,params.w);
                    let tr=authored_terrain_height(textureLoad(field,xy+vec2<i32>(1,0),0).x,params.w);
                    let bl=authored_terrain_height(textureLoad(field,xy+vec2<i32>(0,1),0).x,params.w);
                    let br=authored_terrain_height(textureLoad(field,xy+vec2<i32>(1,1),0).x,params.w);
                    for (var triangle=0u; triangle<2u; triangle++) {
                        let sign=select(1.0,-1.0,triangle==1u);
                        var trianglePolygon=authored_terrain_clip(clipped,vec2<f32>(sign,-sign),sign*(origin.x-origin.y));
                        let gradient=select(vec2<f32>(br-bl,bl-tl),vec2<f32>(tr-tl,br-tr),triangle==1u)/params.z;
                        let normal=normalize(vec3<f32>(-gradient.x,1,-gradient.y));
                        if (dot(outward,normal)>=-1e-5) { continue; }
                        for (var i=0u; i<trianglePolygon.count; i++) {
                            let point=trianglePolygon.points[i];
                            let height=tl+dot(point.xz-origin,gradient);
                            let separation=(point.y-height)*normal.y;
                            if (separation<=margin) {
                                authored_terrain_append(result,AuthoredTerrainPoint(point,separation,normal,0x80000000u|f));
                            }
                        }
                    }
                }
            }
        }
    }
    return;
}

// LEGO uses the same quantized columns and analytic round studs as the shared
// renderer/player surface. Work visits exposed authored faces, never a filled
// bounding box of the machine, so pontoon gaps remain open.
fn authored_lego_clip(input: AuthoredTerrainPolygon, axis: vec3<f32>, offset: f32) -> AuthoredTerrainPolygon {
    var source=input; var result: AuthoredTerrainPolygon;
    if(source.count==0u) { return result; }
    var previous=source.points[source.count-1u];
    var previousDistance=dot(previous,axis)-offset;
    for(var i=0u;i<source.count;i++) {
        let point=source.points[i]; let distance=dot(point,axis)-offset;
        if((previousDistance<=0.0)!=(distance<=0.0)) {
            result.points[result.count]=mix(previous,point,previousDistance/(previousDistance-distance)); result.count++;
        }
        if(distance<=0.0) { result.points[result.count]=point; result.count++; }
        previous=point;previousDistance=distance;
    }
    return result;
}

fn authored_lego_inside(face: AuthoredTerrainPolygon, point: vec3<f32>) -> bool {
    var polygon=face;
    let delta=point-polygon.points[0];
    let u=polygon.points[1]-polygon.points[0];let v=polygon.points[3]-polygon.points[0];
    let uv=vec2<f32>(dot(delta,u)/dot(u,u),dot(delta,v)/dot(v,v));
    return all(uv>=vec2<f32>(-1e-5)) && all(uv<=vec2<f32>(1.00001));
}

fn authored_lego_patch(result: ptr<function,AuthoredTerrainResult>, face: AuthoredTerrainPolygon,
                       outward: vec3<f32>, axis: u32, sign: f32, lower: vec3<f32>, upper: vec3<f32>,
                       margin: f32, feature: u32, studCenter: vec2<f32>, studRadius: f32) {
    var normal=vec3<f32>(0);normal[axis]=sign;
    if(dot(outward,normal)>=-1e-5) { return; }
    var polygon=face;
    for(var k=1u;k<=2u;k++) {
        let tangent=(axis+k)%3u;var plane=vec3<f32>(0);plane[tangent]=1.0;
        polygon=authored_lego_clip(polygon,-plane,-lower[tangent]);
        polygon=authored_lego_clip(polygon,plane,upper[tangent]);
    }
    for(var i=0u;i<polygon.count;i++) {
        let point=polygon.points[i];
        // The part of a column top inside its stud is not exposed terrain.
        if(axis==1u && distance(point.xz,studCenter)<studRadius-1e-6) { continue; }
        let separation=(point[axis]-lower[axis])*sign;
        if(separation<=margin) { authored_terrain_append(result,AuthoredTerrainPoint(point,separation,normal,feature)); }
    }
}

fn authored_lego_cap_point(result: ptr<function,AuthoredTerrainResult>, point: vec3<f32>,
                           cap: f32, margin: f32, feature: u32) {
    if(point.y-cap<=margin) {
        authored_terrain_append(result,AuthoredTerrainPoint(point,point.y-cap,vec3<f32>(0,1,0),feature));
    }
}

fn authored_lego_stud(result: ptr<function,AuthoredTerrainResult>, face: AuthoredTerrainPolygon,
                      outward: vec3<f32>, center: vec2<f32>, radius: f32, bottom: f32, cap: f32,
                      margin: f32, feature: u32, cellLower: vec3<f32>, cellUpper: vec3<f32>) {
    var polygon=face;
    let verticalDepth=cap-cellLower.y;
    let radialDepth=min(min(cellUpper.x-center.x+radius,center.x+radius-cellLower.x),
                        min(cellUpper.z-center.y+radius,center.y+radius-cellLower.z));
    // Resolve a shallow landing at the cap instead of ejecting the hull through
    // a deeper cylindrical side. Conversely, a side impact must not pop up to
    // the cap when its horizontal overlap is the smaller correction.
    if(outward.y < -1e-5 && verticalDepth<=radialDepth+1e-5) {
        // Exact rectangle/disk intersection: inside corners, circular edge
        // intersections, and disk support points when the disk lies inside.
        for(var i=0u;i<4u;i++) {
            let a=polygon.points[i];let b=polygon.points[(i+1u)%4u];
            let p=a.xz-center;let d=b.xz-a.xz;let dd=dot(d,d);
            if(dot(p,p)<=radius*radius) { authored_lego_cap_point(result,a,cap,margin,feature); }
            if(dd>1e-12) {
                let pd=dot(p,d);let determinant=pd*pd-dd*(dot(p,p)-radius*radius);
                if(determinant>=0.0) {
                    for(var side=0u;side<2u;side++) {
                        let t=(-pd+select(-1.0,1.0,side==1u)*sqrt(determinant))/dd;
                        if(t>=0.0 && t<=1.0) { authored_lego_cap_point(result,mix(a,b,t),cap,margin,feature); }
                    }
                }
            }
        }
        let slope=outward.xz/outward.y;
        let downhill=select(vec2<f32>(1,0),slope/max(length(slope),1e-6),dot(slope,slope)>1e-12);
        var directions=array<vec2<f32>,5>(vec2<f32>(1,0),vec2<f32>(-1,0),vec2<f32>(0,1),vec2<f32>(0,-1),downhill);
        for(var i=0u;i<5u;i++) {
            let xz=center+radius*directions[i];
            let point=vec3<f32>(xz.x,polygon.points[0].y-dot(slope,xz-polygon.points[0].xz),xz.y);
            if(authored_lego_inside(polygon,point)) { authored_lego_cap_point(result,point,cap,margin,feature); }
        }
    }
    if(dot(outward.xz,outward.xz)<1e-10) { return; }
    // Smooth cylindrical sides. Clip the authored face to the stud's actual
    // height; use circle support against its plane plus closest face edges.
    var clipped=authored_lego_clip(polygon,vec3<f32>(0,-1,0),-bottom);
    clipped=authored_lego_clip(clipped,vec3<f32>(0,1,0),cap);
    if(clipped.count==0u) { return; }
    let normal=normalize(vec3<f32>(-outward.x,0,-outward.z));
    for(var i=0u;i<clipped.count;i++) {
        let a=clipped.points[i];let b=clipped.points[(i+1u)%clipped.count];
        let terrain=vec3<f32>(center.x,a.y,center.y)+normal*radius;
        let separation=dot(outward,polygon.points[0]-terrain)/dot(outward,normal);
        let point=terrain+normal*separation;
        if(separation<=margin && -separation<=verticalDepth+1e-5
            && distance(point.xz,center)<=radius+margin && authored_lego_inside(polygon,point)) {
            authored_terrain_append(result,AuthoredTerrainPoint(point,separation,normal,feature));
        }
        let d=b.xz-a.xz;
        let t=clamp(dot(center-a.xz,d)/max(dot(d,d),1e-12),0.0,1.0);
        let edge=mix(a,b,t);let radial=edge.xz-center;let length=length(radial);
        if(length>1e-7 && length-radius<=margin && radius-length<=verticalDepth+1e-5) {
            let edgeNormal=vec3<f32>(radial.x,0,radial.y)/length;
            if(dot(outward,edgeNormal)<-1e-5) {
                authored_terrain_append(result,AuthoredTerrainPoint(edge,length-radius,edgeNormal,feature));
            }
        }
    }
}

fn authored_lego_contacts(result: ptr<function,AuthoredTerrainResult>, field: texture_2d<u32>, params: vec4<f32>, size: vec2<u32>,
                          shape: AuthoredShapeView, rootPosition: vec3<f32>, rootOrientation: vec4<f32>,
                          margin: f32, cellBudget: u32) {
    (*result) = AuthoredTerrainResult();
    if(!shape.valid || any(size<vec2<u32>(2u)) || params.z<=0.0 || params.w<=0.0) { (*result).status=1u;return; }
    let axisX=authored_quat_rotate(rootOrientation,vec3<f32>(1,0,0));
    let axisY=authored_quat_rotate(rootOrientation,vec3<f32>(0,1,0));
    let axisZ=authored_quat_rotate(rootOrientation,vec3<f32>(0,0,1));
    let crestPadding=params.w/f32(legoPlateCount(params.w,params.z))+.18*params.z;
    var nodeIndex=0u;
    while(nodeIndex<shape.node_count) {
        let node=authored_node(shape,nodeIndex);
        if(!node.valid) { (*result).status=1u;(*result).count=0u;return; }
        let center=rootPosition+authored_quat_rotate(rootOrientation,.5*(node.minimum+node.maximum));
        let half=.5*(node.maximum-node.minimum);
        let extent=abs(axisX)*half.x+abs(axisY)*half.y+abs(axisZ)*half.z;
        // Include plate rounding even when the plate count hits its ceiling.
        if(authored_terrain_above(field,params,size,center-extent,center+extent,margin+crestPadding)) { nodeIndex=node.escape;continue; }
        nodeIndex++;
        if(node.cell==0xffffffffu) { continue; }
        let cell=authored_cell(shape,node.cell);
        if(!cell.valid) { (*result).status=1u;(*result).count=0u;return; }
        for(var f=cell.first_face;f<cell.first_face+cell.face_count;f++) {
            let face=authored_face(shape,f);
            if(!face.valid) { (*result).status=1u;(*result).count=0u;return; }
            var localNormal=vec3<f32>(0);localNormal[face.axis]=f32(face.sign);
            let outward=authored_quat_rotate(rootOrientation,localNormal);
            let u=(face.axis+1u)%3u;let v=(face.axis+2u)%3u;
            var polygon: AuthoredTerrainPolygon;polygon.count=4u;
            var lower=vec3<f32>(1e30);var upper=vec3<f32>(-1e30);
            for(var corner=0u;corner<4u;corner++) {
                let order=corner^(corner>>1u);var p=face.minimum;
                p[u]=select(face.minimum[u],face.maximum[u],(order&1u)!=0u);
                p[v]=select(face.minimum[v],face.maximum[v],(order&2u)!=0u);
                p=rootPosition+authored_quat_rotate(rootOrientation,p);
                polygon.points[corner]=p;lower=min(lower,p);upper=max(upper,p);
            }
            if(authored_terrain_above(field,params,size,lower,upper,margin+crestPadding)) { continue; }
            let maximum=vec2<f32>(size-vec2<u32>(2u));
            let first=vec2<u32>(floor(clamp((lower.xz+params.xy-vec2<f32>(margin))/params.z,vec2<f32>(0),maximum)));
            let last=vec2<u32>(floor(clamp((upper.xz+params.xy+vec2<f32>(margin))/params.z,vec2<f32>(0),maximum)));
            for(var z=first.y;z<=last.y;z++) { for(var x=first.x;x<=last.x;x++) {
                if((*result).cells>=cellBudget) { (*result).status=2u;(*result).count=0u;return; }
                (*result).cells++;
                let grid=vec2<i32>(i32(x),i32(z));
                let origin=vec2<f32>(f32(x),f32(z))*params.z-params.xy;
                let top=legoPlateTop(textureLoad(field,grid,0).x,params.w,params.z);
                let stud=origin+vec2<f32>(.5*params.z);let radius=.30*params.z;let cap=top+.18*params.z;
                if(lower.y>cap+margin) { continue; }
                let feature=0x80000000u|f;
                // A raised column underneath a broad hull must support its
                // bottom, not select a much deeper horizontal exit through
                // the hull. Choose the least overlap along terrain axes.
                let columnHalf=vec3<f32>(.5*params.z,.5*(top+params.w),.5*params.z);
                let delta=center-vec3<f32>(stud.x,.5*(top-params.w),stud.y);
                let overlap=extent+columnHalf-abs(delta);
                var contactAxis=0u;
                if(overlap.y<overlap.x) { contactAxis=1u; }
                if(overlap.z<overlap[contactAxis]) { contactAxis=2u; }
                let contactSign=select(-1.0,1.0,delta[contactAxis]>=0.0);
                let columnNear=all(overlap>=vec3<f32>(-margin));
                if(columnNear && contactAxis==1u && contactSign>0.0) {
                    authored_lego_patch(result,polygon,outward,1u,1.0,
                        vec3<f32>(origin.x,top,origin.y),vec3<f32>(origin.x+params.z,top,origin.y+params.z),margin,feature,stud,radius);
                }
                for(var side=0u;side<4u;side++) {
                    let axis=select(0u,2u,side>=2u);let sign=select(-1.0,1.0,(side&1u)!=0u);
                    if(!columnNear || axis!=contactAxis || sign!=contactSign) { continue; }
                    var neighbor=grid;neighbor[axis/2u]+=i32(sign);
                    var bottom=-params.w;
                    if(all(neighbor>=vec2<i32>(0)) && all(neighbor<vec2<i32>(size)-vec2<i32>(1))) {
                        bottom=legoPlateTop(textureLoad(field,neighbor,0).x,params.w,params.z);
                    }
                    if(bottom>=top) { continue; }
                    var lo=vec3<f32>(origin.x,bottom,origin.y);var hi=vec3<f32>(origin.x+params.z,top,origin.y+params.z);
                    let plane=select(lo[axis],hi[axis],sign>0.0);lo[axis]=plane;hi[axis]=plane;
                    authored_lego_patch(result,polygon,outward,axis,sign,lo,hi,margin,feature,stud,0.0);
                }
                authored_lego_stud(result,polygon,outward,stud,radius,top,cap,margin,feature,center-extent,center+extent);
            } }
        }
    }
    return;
}
