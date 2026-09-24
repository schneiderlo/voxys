@group(0) @binding(0) var coverage: texture_2d<f32>;
@group(0) @binding(1) var coverageSampler: sampler;
@group(0) @binding(2) var sprites: texture_2d<f32>;
struct Out {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) local: vec2<f32>,
    @location(3) @interpolate(flat) shape: vec3<f32>,
    @location(4) @interpolate(flat) sprite: u32,
};
@vertex fn vs(@builtin(vertex_index) index:u32,
    @location(0) bounds:vec4<f32>, @location(1) uv:vec4<f32>,
    @location(2) color:vec4<f32>) -> Out {
    var corners=array<vec2<f32>,6>(vec2(0,0),vec2(1,0),vec2(0,1),
        vec2(0,1),vec2(1,0),vec2(1,1));
    let p=corners[index];
    var out:Out;
    out.position=vec4(bounds.xy+p*bounds.zw,0,1);
    out.uv=uv.xy+p*uv.zw;
    out.color=color;
    // Negative UV.x selects a rounded solid surface. UV.y carries the
    // radius and UV.zw the original pixel size, independent of viewport.
    out.local=p*uv.zw;
    out.shape=vec3<f32>(0.0);
    out.sprite=0u;
    if(uv.x<0.0) { out.shape=vec3<f32>(uv.zw,uv.y); }
    if(uv.x==-2.0) { out.sprite=4u;out.local=p; }
    if(uv.x<=-3.0) { out.sprite=5u;out.uv.x=-uv.x-3.0; }
    if(uv.x>=2.0) {
        out.sprite=u32(floor(uv.x/2.0));
        out.uv=vec2<f32>(uv.x-f32(out.sprite)*2.0,uv.y)+p*uv.zw;
        out.local=p;
    }
    return out;
}
@fragment fn fs(input:Out) -> @location(0) vec4<f32> {
    let footprint=fwidth(input.local);
    if(input.sprite==4u) {
        let p=(input.local-0.5)*2.0;
        let c=cos(input.shape.z);let s=sin(input.shape.z);
        let q=vec2<f32>(c*p.x+s*p.y,-s*p.x+c*p.y);
        let distance=max(abs(q.x)*1.5-q.y*0.6-0.54,q.y-0.65);
        let aa=max(2.0*max(footprint.x,footprint.y),0.0001);
        return vec4<f32>(input.color.rgb,input.color.a*clamp(0.5-distance/aa,0.0,1.0));
    }
    if(input.sprite>=1u&&input.sprite<=3u) {
        let texel=textureSampleLevel(sprites,coverageSampler,input.uv,0.0);
        var rgb=texel.rgb;
        var alpha=texel.a;
        if(input.sprite==2u) {
            // Painted previews retain the baked model's light and relief.
            // The paint is also the runtime's actual placement material.
            let shade=clamp(max(texel.r,max(texel.g,texel.b))/0.91,0.0,1.15);
            rgb=input.color.rgb*shade;
        } else { rgb*=input.color.rgb; }
        if(input.sprite==3u) {
            let r=length(input.local*2.0-1.0);
            let aa=max(2.0*max(footprint.x,footprint.y),0.0001);
            alpha*=1.0-smoothstep(1.0-aa,1.0+aa,r);
        }
        return vec4<f32>(rgb,alpha*input.color.a);
    }
    var alpha:f32;
    if(input.shape.x>0.0) {
        let halfSize=input.shape.xy*0.5;
        let radius=min(input.shape.z,min(halfSize.x,halfSize.y));
        let q=abs(input.local-halfSize)-halfSize+radius;
        let distance=length(max(q,vec2<f32>(0.0)))+min(max(q.x,q.y),0.0)-radius;
        alpha=clamp(0.5-distance,0.0,1.0);
        if(input.sprite==5u) {alpha*=clamp(distance+input.uv.x+0.5,0.0,1.0);}
    } else {
        alpha=textureSampleLevel(coverage,coverageSampler,input.uv,0.0).r;
    }
    return vec4(input.color.rgb,input.color.a*alpha);
}
