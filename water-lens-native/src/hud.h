#pragma once
#include <android/asset_manager.h>
#include "panel_mesh.h"

struct Hud {
    GLuint program=0,texture=0,vao=0,vbo=0;
    GLint matrix=-1,planeDepth=-1;
    PanelMesh mesh;
    void init(AAssetManager* assets){
        const char* vs=R"glsl(#version 300 es
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 tint;
layout(location=3) in vec4 shape;
uniform mat4 transform;
uniform float planeDepth;
out vec2 uv;
out vec4 colorTint;
out vec4 shapeInfo;
void main(){uv=texcoord;colorTint=tint;shapeInfo=shape;gl_Position=transform*vec4(position,-planeDepth,1.);}
)glsl";
        const char* fs=R"glsl(#version 300 es
precision highp float;
uniform sampler2D atlas;
in vec2 uv;
in vec4 colorTint;
in vec4 shapeInfo;
out vec4 color;
void main(){
    float a;
    if(shapeInfo.x>.5)a=texture(atlas,uv).r;
    else {
        vec2 halfSize=shapeInfo.yz*.5;
        float radius=min(shapeInfo.w,min(halfSize.x,halfSize.y));
        vec2 q=abs((uv-.5)*shapeInfo.yz)-halfSize+radius;
        float d=length(max(q,0.))+min(max(q.x,q.y),0.)-radius;
        float aa=max(fwidth(d),.0002);
        a=1.-smoothstep(-aa,aa,d);
    }
    a*=colorTint.a;color=vec4(colorTint.rgb*a,a);
}
)glsl";
        program=glCreateProgram();
        for(int i=0;i<2;i++){
            GLuint shader=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
            const char* source=i?fs:vs;glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
            GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
            if(!ok)throw std::runtime_error("Control panel shader failed");
            glAttachShader(program,shader);glDeleteShader(shader);
        }
        glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked)throw std::runtime_error("Control panel program failed");
        matrix=glGetUniformLocation(program,"transform");
        planeDepth=glGetUniformLocation(program,"planeDepth");
        AAsset* asset=AAssetManager_open(assets,"ui-font.r8",AASSET_MODE_BUFFER);
        if(!asset||AAsset_getLength(asset)!=512*264)throw std::runtime_error("UI font asset missing");
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,512,264,0,GL_RED,GL_UNSIGNED_BYTE,AAsset_getBuffer(asset));AAsset_close(asset);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        const int sizes[]{2,2,4,4},offsets[]{0,2,4,8};
        for(int i=0;i<4;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,12*sizeof(float),reinterpret_cast<void*>(offsets[i]*sizeof(float)));}
    }
    void update(const EffectSettings& s,const PanelControls& ui,bool available,bool handAim){
        mesh.build(s,ui,available,handAim);
        glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,mesh.vertices.size()*sizeof(float),mesh.vertices.data(),GL_DYNAMIC_DRAW);
    }
    void draw(const XrView& view,const XrPosef& panelPose,const XrPosef& launcherPose){
        XrMatrix4x4f projection;
        XrMatrix4x4f_CreateProjectionFov(&projection,GRAPHICS_OPENGL_ES,view.fov,.05f,10.f);
        auto clipFromWorld=multiply(projection,inverse(poseMatrix(view.pose)));
        glUseProgram(program);glBindVertexArray(vao);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glBindSampler(0,0);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        auto panel=multiply(clipFromWorld,poseMatrix(panelPose));
        glUniform1f(planeDepth,1);glUniformMatrix4fv(matrix,1,GL_FALSE,panel.m);glDrawArrays(GL_TRIANGLES,0,mesh.mainCount);
        auto launcher=multiply(clipFromWorld,poseMatrix(launcherPose));
        glUniform1f(planeDepth,PanelControls::launcherDepth);glUniformMatrix4fv(matrix,1,GL_FALSE,launcher.m);glDrawArrays(GL_TRIANGLES,mesh.mainCount,GLsizei(mesh.vertices.size()/12)-mesh.mainCount);
        glDisable(GL_BLEND);
    }
    void destroy(){
        if(program)glDeleteProgram(program);if(texture)glDeleteTextures(1,&texture);
        if(vao)glDeleteVertexArrays(1,&vao);if(vbo)glDeleteBuffers(1,&vbo);
    }
};
