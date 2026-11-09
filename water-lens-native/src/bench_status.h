#pragma once
#include <cstdio>
#include <string>
#include <unistd.h>
#include "panel_controls.h"

// Opt-in, local USB/ADB test control. No socket, network permission, or input
// injection is involved. The file is consumed once; normal launches stay normal.
struct BenchStatus {
    bool enabled=false;
    int mode=0;
    EffectSettings effect;
    std::string directory;
    double lastWrite=0;
    uint64_t frames=0;
    void configure(const char* path){
        if(!path)return;directory=path;
        const auto filePath=directory+"/bench-test.txt";
        FILE* file=std::fopen(filePath.c_str(),"r");if(!file)return;
        float v[6]{};
        const int count=std::fscanf(file,"%d %f %f %f %f %f %f",&mode,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5]);
        std::fclose(file);std::remove(filePath.c_str());
        if(count!=7||mode<0||mode>2)return;
        for(float x:v)if(!std::isfinite(x))return;
        for(int i=0;i<6;i++)PanelControls::set(effect,i,v[i]);
        effect.cycling=false;effect.clear=mode==2;effect.fullView=mode==1;
        enabled=true;
    }
    void write(double now,const EffectSettings& s,bool focused,bool render,bool headValid,bool viewsValid,
               bool fresh,bool waterSubmitted,bool surfaceSubmitted,float renderFps,float cameraFps){
        if(!enabled)return;frames++;
        if(now-lastWrite<.5)return;lastWrite=now;
        const auto temporary=directory+"/bench-status.tmp",destination=directory+"/bench-status.json";
        FILE* file=std::fopen(temporary.c_str(),"w");if(!file)return;
        std::fprintf(file,"{\"version\":9,\"pid\":%d,\"frames\":%llu,\"mode\":%d,"
            "\"focused\":%s,\"should_render\":%s,\"head_valid\":%s,\"views_valid\":%s,"
            "\"camera_fresh\":%s,\"water_submitted\":%s,\"surface_submitted\":%s,"
            "\"render_fps\":%.2f,\"camera_fps\":%.2f,\"strength\":%.3f,\"speed\":%.3f,"
            "\"density\":%.3f,\"complexity\":%.3f,\"sharpness\":%.3f,\"coverage\":%.3f}\n",
            getpid(),static_cast<unsigned long long>(frames),mode,focused?"true":"false",render?"true":"false",
            headValid?"true":"false",viewsValid?"true":"false",fresh?"true":"false",waterSubmitted?"true":"false",
            surfaceSubmitted?"true":"false",renderFps,cameraFps,s.strength,s.speed,s.density,s.complexity,s.sharpness,s.coverage);
        std::fclose(file);std::rename(temporary.c_str(),destination.c_str());
    }
};
