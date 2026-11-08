#version 450
// RDR-derived confidence blend. Explicitly copy rejected pixels into the separate destination.
// Optional sharpening affects display output only, never the next history.
precision highp float;precision highp int;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D taaColor; // resolve output (perceptual rgb, a = weight), texelFetch only
layout(set=0,binding=1) uniform sampler2D currentColor;
layout(push_constant) uniform Push {mat4 reproject;vec4 winv;vec4 tune;vec3 range;uint jitter;uvec4 info;} pc;
const uint kPure=2u;    // debug: display = pure TAA (b = 1)
const uint kUnorm=4u;
const uint kSharpen=8u;
const uint kFull=32u;
const uint kLite=64u;   // TAA LITE: taaColor is the LITE history (eye format, rgb = the displayed colour): plain copy
// TAA FULL and TAA LITE run this shader through the "copy" pipeline (no blending, eye loadOp DONT_CARE: every pixel of
// the render area is written, alpha = 1); TAA keeps the blended LOAD pipeline with the discard.
void main(){
    const ivec2 last=ivec2(pc.info.xy)-1;
    const ivec2 p=clamp(ivec2(uv*vec2(pc.info.xy)),ivec2(0),last);
    if((pc.info.z&kLite)!=0u){outColor=vec4(texelFetch(taaColor,p,0).rgb,1.0);return;}
    const mediump vec4 t=texelFetch(taaColor,p,0);
    const bool full=(pc.info.z&kFull)!=0u;
    const float b=(full||(pc.info.z&kPure)!=0u)?1.0:clamp((t.a-1.0)*0.5,0.0,1.0);
    // Separate destination: explicitly copy reset/rejected pixels.
    mediump vec3 c=t.rgb;
    if((pc.info.z&kSharpen)!=0u){
        const mediump vec3 n=texelFetch(taaColor,ivec2(p.x,max(p.y-1,0)),0).rgb;
        const mediump vec3 s=texelFetch(taaColor,ivec2(p.x,min(p.y+1,last.y)),0).rgb;
        const mediump vec3 w=texelFetch(taaColor,ivec2(max(p.x-1,0),p.y),0).rgb;
        const mediump vec3 e=texelFetch(taaColor,ivec2(min(p.x+1,last.x),p.y),0).rgb;
        const mediump vec3 lo=min(min(min(n,s),min(w,e)),c),hi=max(max(max(n,s),max(w,e)),c);
        c=clamp(c+pc.range.z*0.25*(4.0*c-n-s-w-e),lo,hi);
    }
    outColor=vec4(mix(texelFetch(currentColor,p,0).rgb,(pc.info.z&kUnorm)!=0u?c:c*c,b),1.0);
}
