// Exact exterior queries in authored root space. Included after the immutable
// heap accessors. Feature ordinals resolve through the same retained shape.
struct AuthoredQuerySurface {
    valid: bool, distance: f32, point: vec3<f32>, normal: vec3<f32>,
    feature: u32, source: u32,
};
struct AuthoredSegmentClosest { squared: f32, query: vec3<f32>, surface: vec3<f32>, };

// Squared distance from a segment to an axis-aligned closed box/rectangle.
// Clamp changes polynomial only at the six slab boundaries. Minimize each
// quadratic interval exactly; sampling can miss a thin crane/rail between taps.
fn authored_segment_box(a: vec3<f32>, b: vec3<f32>, lo: vec3<f32>, hi: vec3<f32>) -> AuthoredSegmentClosest {
    let v = b-a;
    var cuts: array<f32,8>;
    cuts[0] = 0.0; cuts[1] = 1.0;
    var count = 2u;
    for (var axis=0u; axis<3u; axis++) {
        if (abs(v[axis]) <= 1e-20) { continue; }
        let first = (lo[axis]-a[axis])/v[axis];
        let last = (hi[axis]-a[axis])/v[axis];
        if (first>0.0 && first<1.0) { cuts[count]=first; count++; }
        if (last>0.0 && last<1.0) { cuts[count]=last; count++; }
    }
    for (var i=1u; i<count; i++) {
        let value=cuts[i]; var at=i;
        while (at>0u) { if (cuts[at-1u]<=value) { break; } cuts[at]=cuts[at-1u]; at--; }
        cuts[at]=value;
    }
    let initial=clamp(a,lo,hi);
    var best=AuthoredSegmentClosest(dot(a-initial,a-initial),a,initial);
    for (var i=0u; i+1u<count; i++) {
        let midpoint=a+v*(.5*(cuts[i]+cuts[i+1u]));
        var numerator=0.0; var denominator=0.0;
        for (var axis=0u; axis<3u; axis++) {
            if (midpoint[axis]<lo[axis] || midpoint[axis]>hi[axis] || lo[axis]==hi[axis]) {
                let bound=select(hi[axis],lo[axis],midpoint[axis]<=lo[axis]);
                numerator+=v[axis]*(a[axis]-bound); denominator+=v[axis]*v[axis];
            }
        }
        // A flat minimum spans the interval. Its midpoint avoids cancellation
        // at a slab edge on long segments (and an artificial sideways normal).
        var t=.5*(cuts[i]+cuts[i+1u]);
        if (denominator>1e-30) { t=clamp(-numerator/denominator,cuts[i],cuts[i+1u]); }
        let query=a+v*t; let surface=clamp(query,lo,hi); let delta=query-surface;
        let squared=dot(delta,delta);
        if (squared<best.squared) { best=AuthoredSegmentClosest(squared,query,surface); }
    }
    return best;
}

fn authored_ray_interval(lo: vec3<f32>, hi: vec3<f32>, origin: vec3<f32>, direction: vec3<f32>, maximum: f32) -> vec2<f32> {
    var interval=vec2<f32>(0.0,maximum);
    for (var axis=0u; axis<3u; axis++) {
        if (abs(direction[axis])<1e-20) {
            if (origin[axis]<lo[axis] || origin[axis]>hi[axis]) { return vec2<f32>(1.0,-1.0); }
        } else {
            let a=(lo[axis]-origin[axis])/direction[axis];
            let b=(hi[axis]-origin[axis])/direction[axis];
            interval.x=max(interval.x,min(a,b)); interval.y=min(interval.y,max(a,b));
            if (interval.x>interval.y) { return vec2<f32>(1.0,-1.0); }
        }
    }
    return interval;
}

fn authored_segment_surface(shape: AuthoredShapeView, a: vec3<f32>, b: vec3<f32>) -> AuthoredQuerySurface {
    var result=AuthoredQuerySurface(false,1e30,a,vec3<f32>(0,1,0),0xffffffffu,0u);
    if (!shape.valid) { return result; }
    var squared=1e30; var inside=false; var closest=a;
    var nodeIndex=0u;
    while (nodeIndex<shape.node_count) {
        let node=authored_node(shape,nodeIndex);
        if (!node.valid) { result.valid=false; return result; }
        let lower=authored_segment_box(a,b,node.minimum,node.maximum);
        if (lower.squared>squared) { nodeIndex=node.escape; continue; }
        nodeIndex++;
        if (node.cell==0xffffffffu) { continue; }
        let cell=authored_cell(shape,node.cell);
        if (!cell.valid) { result.valid=false; return result; }
        let segmentRange=authored_ray_interval(cell.minimum,cell.maximum,a,b-a,1.0);
        inside=inside || segmentRange.x<=segmentRange.y;
        for (var i=0u; i<cell.face_count; i++) {
            let index=cell.first_face+i; let face=authored_face(shape,index);
            if (!face.valid) { result.valid=false; return result; }
            let candidate=authored_segment_box(a,b,face.minimum,face.maximum);
            if (candidate.squared<squared || (candidate.squared==squared && index<result.feature)) {
                squared=candidate.squared; closest=candidate.query;
                result.point=candidate.surface; result.feature=index; result.source=face.source;
                result.normal=vec3<f32>(0); result.normal[face.axis]=f32(face.sign);
                result.valid=true;
            }
        }
    }
    if (result.valid) {
        let distance=sqrt(squared);
        result.distance=select(distance,-distance,inside);
        if (distance>1e-7) { result.normal=(closest-result.point)/result.distance; }
        result.feature |= 0x80000000u;
    }
    return result;
}

struct AuthoredQueryRay { valid: bool, distance: f32, normal: vec3<f32>, feature: u32, source: u32, };
fn authored_ray(shape: AuthoredShapeView, origin: vec3<f32>, direction: vec3<f32>, maximum: f32) -> AuthoredQueryRay {
    var result=AuthoredQueryRay(false,maximum,vec3<f32>(0,1,0),0xffffffffu,0u);
    if (!shape.valid) { return result; }
    let start=authored_segment_surface(shape,origin,origin);
    if (start.valid && start.distance<=0.0) {
        return AuthoredQueryRay(true,0.0,start.normal,start.feature,start.source);
    }
    var nodeIndex=0u;
    while (nodeIndex<shape.node_count) {
        let node=authored_node(shape,nodeIndex);
        if (!node.valid) { result.valid=false; return result; }
        let range=authored_ray_interval(node.minimum,node.maximum,origin,direction,result.distance);
        if (range.x>range.y) { nodeIndex=node.escape; continue; }
        nodeIndex++;
        if (node.cell==0xffffffffu) { continue; }
        let cell=authored_cell(shape,node.cell);
        if (!cell.valid) { result.valid=false; return result; }
        for (var i=0u; i<cell.face_count; i++) {
            let index=cell.first_face+i; let face=authored_face(shape,index);
            if (!face.valid) { result.valid=false; return result; }
            if (abs(direction[face.axis])<1e-20) { continue; }
            let distance=(face.minimum[face.axis]-origin[face.axis])/direction[face.axis];
            if (distance<0.0 || distance>result.distance) { continue; }
            let point=origin+direction*distance;
            let u=(face.axis+1u)%3u; let v=(face.axis+2u)%3u;
            if (point[u]<face.minimum[u]-1e-6 || point[u]>face.maximum[u]+1e-6
                || point[v]<face.minimum[v]-1e-6 || point[v]>face.maximum[v]+1e-6) { continue; }
            let feature=index|0x80000000u;
            if (result.valid && distance==result.distance && feature>=result.feature) { continue; }
            var normal=vec3<f32>(0); normal[face.axis]=f32(face.sign);
            result=AuthoredQueryRay(true,distance,normal,feature,face.source);
        }
    }
    return result;
}
