#pragma once
#include <cmath>
#include <cstdint>

// Ray directions never change: this is a depth surface, not a camera zoom.
// The compositor reprojects its own stereo passthrough onto this shared mesh.
// Deliberately incorrect depth is the experiment; it can change apparent depth
// and size, and is not equivalent to an arbitrary screen-space refraction shader.
struct WaterSurface {
    static constexpr uint32_t columns=64,rows=56;
    static constexpr uint32_t vertexCount=(columns+1)*(rows+1),triangleCount=columns*rows*2;
    static constexpr float extent=2.2f;
    static float wave(float phase,float sharpness){
        float t=phase*.1591549431f+.25f;t-=std::floor(t);
        const float triangle=1-4*std::abs(t-.5f);
        const float pointed=triangle*(1+sharpness*(triangle*triangle-1));
        return std::sin(phase)*(1-sharpness)+pointed*sharpness;
    }
    template<class Vertex> static void vertices(Vertex* out,float time,float strength,float density,float complexity,float sharpness){
        for(uint32_t y=0;y<=rows;y++)for(uint32_t x=0;x<=columns;x++){
            const float a=extent*(2.f*x/columns-1),b=extent*(2.f*y/rows-1);
            const float qx=a*density,qy=b*density;
            const float ripple=(wave(qy*9+time*.85f,sharpness)+wave(qx*8+qy*5-time*.57f,sharpness)*complexity)/(1+complexity);
            // Bound depth to 0.8..20 m even at maximum strength. A single surface
            // is shared by both eyes; never give each eye independent wave phases.
            const float z=1.f/(.65f+.60f*strength*ripple);
            out[y*(columns+1)+x]={a*z,b*z,-z};
        }
    }
    static void indices(uint32_t* out){
        for(uint32_t y=0;y<rows;y++)for(uint32_t x=0;x<columns;x++){
            const uint32_t a=y*(columns+1)+x,b=a+1,c=a+columns+1,d=c+1;
            // CCW viewed from the wearer, facing toward +Z.
            *out++=a;*out++=b;*out++=c;*out++=b;*out++=d;*out++=c;
        }
    }
    template<class Vertex> static void plane(Vertex* out,float depth){
        for(uint32_t y=0;y<=rows;y++)for(uint32_t x=0;x<=columns;x++)
            out[y*(columns+1)+x]={extent*(2.f*x/columns-1)*depth,extent*(2.f*y/rows-1)*depth,-depth};
    }
    template<class Vertex> static void diagnosticWindow(Vertex* vertices,float phase){
        // A visibly changing footprint distinguishes mutable-buffer updates from
        // depth-only changes that the compositor may not visibly reproject.
        const float scale=.20f+.06f*std::sin(phase);
        for(uint32_t i=0;i<vertexCount;i++){vertices[i].x*=scale;vertices[i].y*=scale;}
    }
};
