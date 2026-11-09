#pragma once
static const char* vertexSource=R"glsl(#version 300 es
out vec2 uv;
void main(){
    vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    uv=p;
    gl_Position=vec4(p*2.-1.,0.,1.);
}
)glsl";
static const char* fragmentSource=R"glsl(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
precision highp sampler2DArray;
in vec2 uv;
out vec4 color;
uniform samplerExternalOES cameraImage;
uniform sampler2DArray depthImage;
uniform vec4 fov, calibration, depthFov;
uniform mat4 cameraFromEye, depthFromEye;
uniform float skew, waveTime, strength, density, complexity, coverage, sharpness;
uniform float depthNear, depthFar;
uniform vec3 radial;
uniform vec2 tangential;
uniform int eye, hasDepth, srgbOutput, spatialSbs;

float waterWave(float phase){
    // A continuous triangular crest has a sharp change in slope at its peak.
    // Cubing it narrows the ridge without a texture lookup or an expensive power.
    float triangle=1.-4.*abs(fract(phase*.1591549431+.25)-.5);
    float pointed=triangle*mix(1.,triangle*triangle,sharpness);
    return mix(sin(phase),pointed,sharpness);
}

float depthAt(vec3 position){
    vec2 tangent=position.xy/max(-position.z,.001);
    vec2 p=(tangent-depthFov.xz)/(depthFov.yw-depthFov.xz);
    if(any(lessThan(p,vec2(.002)))||any(greaterThan(p,vec2(.998))))return 0.;
    float d=texture(depthImage,vec3(p,float(eye))).r;
    if(d>=.99999||d<=0.)return 0.;
    return depthFar>depthNear ? depthNear*depthFar/(depthFar-d*(depthFar-depthNear)) : depthNear/(1.-d);
}
void main(){
    // 1.0 preserves calibrated alignment. Wider settings magnify the finite
    // camera image instead of inventing missing peripheral camera pixels.
    vec2 a=mix(fov.xz,fov.yw,uv)/max(coverage,1.);
    vec2 q=a*density;
    // Angular perturbation is shared by both eyes; camera calibration is applied
    // afterwards. Distorting each camera independently would harm stereo fusion.
    vec2 flow=vec2(waterWave(q.y*12.+waveTime*.85)+waterWave(q.x*8.+q.y*6.-waveTime*.57)*complexity,
                   waterWave(q.x*11.-waveTime*.74+1.5707963268)+waterWave(q.y*9.-q.x*5.+waveTime*.63)*complexity);
    vec3 ray=vec3(a+flow*strength*.025,-1.);
    float distance=2.;
    if(hasDepth==1){
        vec3 origin=depthFromEye[3].xyz;
        vec3 direction=mat3(depthFromEye)*ray;
        // Two bounded reprojection iterations account for a different depth-camera
        // origin. Missing/disoccluded depth retains the explicit 2 m approximation.
        for(int i=0;i<2;i++){
            float z=depthAt(origin+direction*distance);
            if(z>.15&&direction.z<-.05)distance=clamp((-z-origin.z)/direction.z,.2,20.);
        }
    }
    vec3 p=(cameraFromEye*vec4(ray*distance,1.)).xyz;
    if(p.z<=.001){color=vec4(0.);return;}
    vec2 n=p.xy/p.z;
    float r2=dot(n,n);
    vec2 distorted=n*(1.+r2*(radial.x+r2*(radial.y+r2*radial.z)));
    distorted+=vec2(2.*tangential.x*n.x*n.y+tangential.y*(r2+2.*n.x*n.x),
                     tangential.x*(r2+2.*n.y*n.y)+2.*tangential.y*n.x*n.y);
    vec2 t=distorted*calibration.xy+calibration.zw;
    t.x+=skew*distorted.y;
    float edge=min(min(t.x,t.y),min(1.-t.x,1.-t.y));
    float alpha=smoothstep(.001,.005,edge);
    if(alpha==0.){color=vec4(0.);return;}
    if(spatialSbs==1)t.x=(t.x+float(eye))*.5;
    vec3 c=texture(cameraImage,t).rgb;
    if(srgbOutput==1)c=mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));
    color=vec4(c*alpha,alpha);
}
)glsl";
