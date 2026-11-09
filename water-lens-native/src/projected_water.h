#pragma once
#include "water_surface.h"

// Included after the OpenXR function/check helpers in main.cpp.
struct ProjectedWater {
    XrPassthroughLayerFB layer=XR_NULL_HANDLE;
    XrTriangleMeshFB mesh=XR_NULL_HANDLE;
    XrGeometryInstanceFB geometry=XR_NULL_HANDLE;
    bool ready=false;
    bool visibilityProbe=false;
    uint32_t updates=0;
    double updateMilliseconds=0;
    PFN_xrDestroyPassthroughLayerFB destroyLayer=nullptr;
    PFN_xrDestroyTriangleMeshFB destroyMesh=nullptr;
    PFN_xrDestroyGeometryInstanceFB destroyGeometry=nullptr;
    PFN_xrTriangleMeshBeginVertexBufferUpdateFB beginVertices=nullptr;
    PFN_xrTriangleMeshGetVertexBufferFB getVertices=nullptr;
    PFN_xrTriangleMeshEndVertexBufferUpdateFB endVertices=nullptr;
    PFN_xrGeometryInstanceSetTransformFB transform=nullptr;

    void configure(const char* directory){
        if(!directory)return;
        const auto path=std::string(directory)+"/surface-test.txt";
        FILE* file=std::fopen(path.c_str(),"r");if(!file)return;
        int mode=0;visibilityProbe=std::fscanf(file,"%d",&mode)==1&&mode==1;
        std::fclose(file);
        // One launch only. A failed/abandoned capture must not change the next
        // ordinary startup from the working Camera mode.
        std::remove(path.c_str());
    }

    void init(XrInstance instance,XrSession session,XrPassthroughFB passthrough,XrSpace space){
        try{
            auto createLayer=function<PFN_xrCreatePassthroughLayerFB>(instance,"xrCreatePassthroughLayerFB");
            auto createMesh=function<PFN_xrCreateTriangleMeshFB>(instance,"xrCreateTriangleMeshFB");
            auto createGeometry=function<PFN_xrCreateGeometryInstanceFB>(instance,"xrCreateGeometryInstanceFB");
            auto begin=function<PFN_xrTriangleMeshBeginUpdateFB>(instance,"xrTriangleMeshBeginUpdateFB");
            auto end=function<PFN_xrTriangleMeshEndUpdateFB>(instance,"xrTriangleMeshEndUpdateFB");
            auto getIndices=function<PFN_xrTriangleMeshGetIndexBufferFB>(instance,"xrTriangleMeshGetIndexBufferFB");
            destroyLayer=function<PFN_xrDestroyPassthroughLayerFB>(instance,"xrDestroyPassthroughLayerFB");
            destroyMesh=function<PFN_xrDestroyTriangleMeshFB>(instance,"xrDestroyTriangleMeshFB");
            destroyGeometry=function<PFN_xrDestroyGeometryInstanceFB>(instance,"xrDestroyGeometryInstanceFB");
            beginVertices=function<PFN_xrTriangleMeshBeginVertexBufferUpdateFB>(instance,"xrTriangleMeshBeginVertexBufferUpdateFB");
            getVertices=function<PFN_xrTriangleMeshGetVertexBufferFB>(instance,"xrTriangleMeshGetVertexBufferFB");
            endVertices=function<PFN_xrTriangleMeshEndVertexBufferUpdateFB>(instance,"xrTriangleMeshEndVertexBufferUpdateFB");
            transform=function<PFN_xrGeometryInstanceSetTransformFB>(instance,"xrGeometryInstanceSetTransformFB");
            XrPassthroughLayerCreateInfoFB li{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
            li.passthrough=passthrough;li.purpose=XR_PASSTHROUGH_LAYER_PURPOSE_PROJECTED_FB;
            li.flags=XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
            XR(createLayer(session,&li,&layer));
            XrTriangleMeshCreateInfoFB mi{XR_TYPE_TRIANGLE_MESH_CREATE_INFO_FB};
            mi.flags=XR_TRIANGLE_MESH_MUTABLE_BIT_FB;mi.windingOrder=XR_WINDING_ORDER_CCW_FB;
            mi.vertexCount=WaterSurface::vertexCount;mi.triangleCount=WaterSurface::triangleCount;
            XR(createMesh(session,&mi,&mesh));
            XR(begin(mesh));XrVector3f* vertices=nullptr;uint32_t* indices=nullptr;
            XR(getVertices(mesh,&vertices));XR(getIndices(mesh,&indices));
            WaterSurface::vertices(vertices,0,0,1,0,0);WaterSurface::indices(indices);
            XR(end(mesh,WaterSurface::vertexCount,WaterSurface::triangleCount));
            XrGeometryInstanceCreateInfoFB gi{XR_TYPE_GEOMETRY_INSTANCE_CREATE_INFO_FB};
            gi.layer=layer;gi.mesh=mesh;gi.baseSpace=space;gi.pose.orientation.w=1;gi.scale={1,1,1};
            XR(createGeometry(session,&gi,&geometry));
            if(visibilityProbe){
                auto setStyle=function<PFN_xrPassthroughLayerSetStyleFB>(instance,"xrPassthroughLayerSetStyleFB");
                XrPassthroughColorMapMonoToRgbaFB map{XR_TYPE_PASSTHROUGH_COLOR_MAP_MONO_TO_RGBA_FB};
                for(int i=0;i<XR_PASSTHROUGH_COLOR_MAP_MONO_SIZE_FB;i++){
                    const float v=float(i)/(XR_PASSTHROUGH_COLOR_MAP_MONO_SIZE_FB-1);
                    map.textureColorMap[i]={.04f*v,.18f+.72f*v,.04f*v,1};
                }
                XrPassthroughStyleFB style{XR_TYPE_PASSTHROUGH_STYLE_FB};
                style.next=&map;style.textureOpacityFactor=1;XR(setStyle(layer,&style));
                LOGI("SURFACE PROBE: green projected layer, pulsing vertex footprint, normal reconstruction underneath");
            }
            ready=true;
            LOGI("FULLVIEW projected passthrough ready: %u vertices, %u triangles, scale=1",mi.vertexCount,mi.triangleCount);
        }catch(const std::exception& error){LOGE("FULLVIEW unavailable: %s",error.what());destroy();}
    }
    bool update(XrSpace space,XrTime time,const XrPosef& pose,float phase,const EffectSettings& s){
        if(!ready)return false;
        const auto start=nanos(CLOCK_MONOTONIC);
        try{
            uint32_t count=0;XR(beginVertices(mesh,&count));
            XrVector3f* vertices=nullptr;const auto result=getVertices(mesh,&vertices);
            if(XR_SUCCEEDED(result)&&count==WaterSurface::vertexCount){
                WaterSurface::vertices(vertices,phase,s.strength,s.density,s.complexity,s.sharpness);
                if(visibilityProbe)WaterSurface::diagnosticWindow(vertices,phase);
            }
            XR(endVertices(mesh));XR(result);
            if(count!=WaterSurface::vertexCount)throw std::runtime_error("Unexpected projected mesh size");
            XrGeometryInstanceTransformFB t{XR_TYPE_GEOMETRY_INSTANCE_TRANSFORM_FB};
            t.baseSpace=space;t.time=time;t.pose=pose;t.scale={1,1,1};XR(transform(geometry,&t));
            updates++;updateMilliseconds+=(nanos(CLOCK_MONOTONIC)-start)*1e-6;
            return true;
        }catch(const std::exception& error){LOGE("FULLVIEW disabled after update failure: %s",error.what());ready=false;return false;}
    }
    void destroy(){
        ready=false;
        if(geometry&&destroyGeometry)destroyGeometry(geometry);geometry=XR_NULL_HANDLE;
        if(mesh&&destroyMesh)destroyMesh(mesh);mesh=XR_NULL_HANDLE;
        if(layer&&destroyLayer)destroyLayer(layer);layer=XR_NULL_HANDLE;
    }
};
