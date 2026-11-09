#pragma once
#include <cstdio>

// Explicit one-shot local diagnostic. In ordinary launches there are no readbacks
// or saved camera images. A camera-test.txt file in externalDataPath enables it.
struct CameraDiagnostic {
    bool enabled=false,captured=false,compare=false;
    int requestedSize=1728;
    double firstFrame=0;
    std::string directory;
    GLuint cameraProgram=0;
    void configure(const char* path){
        if(!path)return;directory=path;
        FILE* f=std::fopen((directory+"/camera-test.txt").c_str(),"r");
        if(!f)return;
        int size=0;const bool read=std::fscanf(f,"%d",&size)==1;std::fclose(f);
        enabled=read&&(size==0||size==1280||size==1728);
        compare=enabled&&size==0;
        if(enabled){requestedSize=compare?1280:size;LOGI("DIAG enabled camera=%d compare=%d, view=100%%, effect=0",requestedSize,compare);}
    }
    bool ready(double now,bool fresh){
        if(!enabled||captured||!fresh)return false;
        if(firstFrame==0)firstFrame=now;
        return now-firstFrame>=(compare?2:8);
    }
    static void writeImage(const std::string& path,const std::vector<unsigned char>& pixels,int n,bool alpha){
        FILE* f=std::fopen(path.c_str(),"wb");if(!f)throw std::runtime_error("Diagnostic output unavailable");
        std::fprintf(f,alpha?"P5\n%d %d\n255\n":"P6\n%d %d\n255\n",n,n);
        for(int y=n-1;y>=0;y--)for(int x=0;x<n;x++){
            const auto* p=&pixels[(y*n+x)*4];std::fwrite(p+(alpha?3:0),1,alpha?1:3,f);
        }
        std::fclose(f);
    }
    void createProgram(){
        if(cameraProgram)return;
        const char* fs=R"glsl(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
in vec2 uv;
uniform samplerExternalOES cameraImage;
uniform vec2 packing;
out vec4 color;
void main(){color=vec4(texture(cameraImage,vec2(uv.x*packing.x+packing.y,1.-uv.y)).rgb,1.);}
)glsl";
        cameraProgram=glCreateProgram();
        for(int i=0;i<2;i++){
            GLuint shader=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
            const char* source=i?fs:vertexSource;glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
            GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
            if(!ok)throw std::runtime_error("Diagnostic camera shader failed");
            glAttachShader(cameraProgram,shader);glDeleteShader(shader);
        }
        glLinkProgram(cameraProgram);GLint ok=0;glGetProgramiv(cameraProgram,GL_LINK_STATUS,&ok);
        if(!ok)throw std::runtime_error("Diagnostic camera program failed");
    }
    void capture(int eye,const Camera& camera,const CameraFrame& source,GLuint sourceFramebuffer,int w,int h,const XrView& view,const XrPosef& headPose){
        createProgram();
        constexpr int n=512;
        GLint oldProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
        GLuint tex=0,fb=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,n,n);glGenFramebuffers(1,&fb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Diagnostic framebuffer incomplete");
        glBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFramebuffer);
        glBlitFramebuffer(0,0,w,h,0,0,n,n,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,fb);glViewport(0,0,n,n);
        std::vector<unsigned char> pixels(n*n*4);glReadPixels(0,0,n,n,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        const std::string base=directory+"/camera-"+std::to_string(camera.width)+"-eye"+std::to_string(eye);
        writeImage(base+"-render.ppm",pixels,n,false);writeImage(base+"-alpha.pgm",pixels,n,true);
        int covered=0;for(int i=0;i<n*n;i++)covered+=pixels[i*4+3]>127;
        glUseProgram(cameraProgram);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,source.texture);
        glUniform2f(glGetUniformLocation(cameraProgram,"packing"),camera.spatialSbs?.5f:1.f,camera.spatialSbs?eye*.5f:0.f);
        glUniform1i(glGetUniformLocation(cameraProgram,"cameraImage"),0);glDrawArrays(GL_TRIANGLES,0,3);
        glReadPixels(0,0,n,n,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());writeImage(base+"-raw.ppm",pixels,n,false);
        const auto err=glGetError();if(err!=GL_NO_ERROR)throw std::runtime_error("Diagnostic GL readback failed: "+std::to_string(err));
        LOGI("DIAG size=%dx%d eye=%d projectionCoverage=%.2f%% K=%f,%f,%f,%f fov=%f,%f,%f,%f head=%f,%f,%f,%f,%f,%f,%f",
             camera.width,camera.height,eye,100.f*covered/(n*n),camera.calibration[0],camera.calibration[1],camera.calibration[2],camera.calibration[3],
             view.fov.angleLeft,view.fov.angleRight,view.fov.angleUp,view.fov.angleDown,
             headPose.position.x,headPose.position.y,headPose.position.z,headPose.orientation.x,headPose.orientation.y,headPose.orientation.z,headPose.orientation.w);
        glBindFramebuffer(GL_FRAMEBUFFER,sourceFramebuffer);glViewport(0,0,w,h);glUseProgram(oldProgram);
        glDeleteFramebuffers(1,&fb);glDeleteTextures(1,&tex);
        if(eye==1){captured=true;LOGI("DIAG complete; readbacks disabled for remaining session");}
    }
};
