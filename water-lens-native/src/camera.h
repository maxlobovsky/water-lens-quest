#pragma once
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <deque>
#include <atomic>

struct CameraFrame {
    AImage* image=nullptr;
    EGLImageKHR eglImage=EGL_NO_IMAGE_KHR;
    GLuint texture=0;
    GLsync fence=nullptr;
    int64_t timestamp=0;
    int64_t receivedAt=0;
    XrPosef headPose{{0,0,0,1},{0,0,0}};
    bool poseValid=false;
};

struct Camera {
    std::string id;
    int eye=0, width=1280, height=1280;
    bool bootTimeTimestamps=false;
    bool distortionOffSupported=false;
    float intrinsics[5]{}, distortion[5]{};
    // Intrinsics are normalized to the actual aspect-ratio crop.
    float calibration[4]{};
    XrMatrix4x4f cameraFromHead{};
    ACameraDevice* device=nullptr;
    ACameraCaptureSession* capture=nullptr;
    ACaptureSessionOutputContainer* outputs=nullptr;
    ACaptureSessionOutput* output=nullptr;
    ACameraOutputTarget* target=nullptr;
    ACaptureRequest* request=nullptr;
    AImageReader* reader=nullptr;
    std::atomic<bool> failed{false};
    CameraFrame pending{}, current{};
    std::deque<CameraFrame> retired;
    EGLDisplay display=EGL_NO_DISPLAY;

    static int integer(ACameraMetadata_const_entry& e) {
        return e.type==ACAMERA_TYPE_BYTE?e.data.u8[0]:e.data.i32[0];
    }
    static bool entry(const ACameraMetadata* m,uint32_t tag,ACameraMetadata_const_entry& e) {
        return ACameraMetadata_getConstEntry(m,tag,&e)==ACAMERA_OK && e.count;
    }
    void configure(const ACameraMetadata* metadata,int requestedSize=1728) {
        ACameraMetadata_const_entry e{};
        auto floats=[&](uint32_t tag,float* dest,uint32_t count){
            if(!entry(metadata,tag,e)||e.type!=ACAMERA_TYPE_FLOAT||e.count<count)
                throw std::runtime_error("Camera calibration is missing");
            std::copy(e.data.f,e.data.f+count,dest);
        };
        floats(ACAMERA_LENS_INTRINSIC_CALIBRATION,intrinsics,5);
        XrQuaternionf rotation;
        XrVector3f translation;
        floats(ACAMERA_LENS_POSE_ROTATION,&rotation.x,4);
        floats(ACAMERA_LENS_POSE_TRANSLATION,&translation.x,3);
        // Android specifies head/sensor -> optical-camera rotation. Coordinates
        // in this camera basis project as x/z, y/z with a top-left image origin.
        XrMatrix4x4f rotate, translate;
        XrMatrix4x4f_CreateFromQuaternion(&rotate,&rotation);
        XrMatrix4x4f_CreateTranslation(&translate,-translation.x,-translation.y,-translation.z);
        XrMatrix4x4f_Multiply(&cameraFromHead,&rotate,&translate);
        if(entry(metadata,ACAMERA_DISTORTION_CORRECTION_AVAILABLE_MODES,e))
            for(uint32_t i=0;i<e.count;i++)distortionOffSupported|=e.data.u8[i]==0;
        const uint32_t sensorTag=distortionOffSupported?ACAMERA_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE:ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE;
        if(!entry(metadata,sensorTag,e)||e.count<4)
            throw std::runtime_error("Camera sensor bounds are missing");
        const float left=e.data.i32[0], top=e.data.i32[1];
        const float sensorWidth=e.data.i32[2]-left, sensorHeight=e.data.i32[3]-top;
        if(!entry(metadata,ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,e))
            throw std::runtime_error("Camera resolutions unavailable");
        bool high=false, square=false, classic=false;
        for(uint32_t i=0;i+3<e.count;i+=4) {
            if(e.data.i32[i]!=AIMAGE_FORMAT_YUV_420_888||e.data.i32[i+3]!=0)continue;
            high |= e.data.i32[i+1]==1728 && e.data.i32[i+2]==1728;
            square |= e.data.i32[i+1]==1280 && e.data.i32[i+2]==1280;
            classic |= e.data.i32[i+1]==1280 && e.data.i32[i+2]==960;
        }
        if(!high&&!square&&!classic)throw std::runtime_error("No supported camera resolution");
        // Select an explicit supported mode; do not confuse pixel count with FOV.
        width=(requestedSize==1728&&high)?1728:1280;
        height=width==1728?1728:square?1280:960;
        if(width==1280&&!square&&!classic){width=height=1728;}
        float cropWidth=sensorWidth, cropHeight=sensorHeight;
        const float ratio=float(width)/height;
        if(cropWidth/cropHeight>ratio)cropWidth=cropHeight*ratio;
        else cropHeight=cropWidth/ratio;
        calibration[0]=intrinsics[0]/cropWidth;
        calibration[1]=intrinsics[1]/cropHeight;
        calibration[2]=(intrinsics[2]-left-(sensorWidth-cropWidth)*.5f)/cropWidth;
        calibration[3]=(intrinsics[3]-top-(sensorHeight-cropHeight)*.5f)/cropHeight;
        intrinsics[4]/=cropWidth;
        if(distortionOffSupported&&entry(metadata,ACAMERA_LENS_DISTORTION,e)&&e.count>=5)
            std::copy(e.data.f,e.data.f+5,distortion);
        bootTimeTimestamps=entry(metadata,ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE,e)&&integer(e)==1;
        LOGI("CAMERA %d id=%s size=%dx%d K=%f,%f,%f,%f q=%f,%f,%f,%f t=%f,%f,%f clock=%s",
             eye,id.c_str(),width,height,calibration[0],calibration[1],calibration[2],calibration[3],
             rotation.x,rotation.y,rotation.z,rotation.w,translation.x,translation.y,translation.z,
             bootTimeTimestamps?"boottime":"unknown");
    }
    static void disconnected(void* ctx,ACameraDevice*){static_cast<Camera*>(ctx)->failed=true;}
    static void deviceError(void* ctx,ACameraDevice*,int error){
        LOGE("Camera device error %d",error);static_cast<Camera*>(ctx)->failed=true;
    }
    static void sessionState(void*,ACameraCaptureSession*) {}
    void start(ACameraManager* manager,EGLDisplay eglDisplay) {
        display=eglDisplay;failed=false;
        auto check=[](int result,const char* operation){if(result!=0)throw std::runtime_error(std::string(operation)+": "+std::to_string(result));};
        check(AImageReader_newWithUsage(width,height,AIMAGE_FORMAT_YUV_420_888,
            AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,6,&reader),"Create GPU image reader");
        ANativeWindow* surface=nullptr;
        check(AImageReader_getWindow(reader,&surface),"Get camera surface");
        ACameraDevice_stateCallbacks callbacks{this,disconnected,deviceError};
        check(ACameraManager_openCamera(manager,id.c_str(),&callbacks,&device),"Open camera");
        check(ACaptureSessionOutputContainer_create(&outputs),"Camera output container");
        check(ACaptureSessionOutput_create(surface,&output),"Camera output");
        check(ACaptureSessionOutputContainer_add(outputs,output),"Add camera output");
        check(ACameraOutputTarget_create(surface,&target),"Camera target");
        check(ACameraDevice_createCaptureRequest(device,TEMPLATE_PREVIEW,&request),"Camera request");
        check(ACaptureRequest_addTarget(request,target),"Add target");
        int32_t fps[2]{60,60};
        // Camera API advertises 60 Hz. Rejecting this setting leaves the device default.
        ACaptureRequest_setEntry_i32(request,ACAMERA_CONTROL_AE_TARGET_FPS_RANGE,2,fps);
        uint8_t off=0;
        if(distortionOffSupported&&ACaptureRequest_setEntry_u8(request,ACAMERA_DISTORTION_CORRECTION_MODE,1,&off)!=ACAMERA_OK)
            std::fill(std::begin(distortion),std::end(distortion),0.f);
        ACameraCaptureSession_stateCallbacks sessionCallbacks{this,sessionState,sessionState,sessionState};
        check(ACameraDevice_createCaptureSession(device,outputs,&sessionCallbacks,&capture),"Camera session");
        check(ACameraCaptureSession_setRepeatingRequest(capture,nullptr,1,&request,nullptr),"Camera streaming");
    }
    void release(CameraFrame& frame) {
        if(frame.fence)glDeleteSync(frame.fence);
        if(frame.texture)glDeleteTextures(1,&frame.texture);
        if(frame.eglImage!=EGL_NO_IMAGE_KHR)destroyImage(display,frame.eglImage);
        if(frame.image)AImage_delete(frame.image);
        frame={};
    }
    void collect() {
        while(!retired.empty()){
            auto& f=retired.front();
            auto state=f.fence?glClientWaitSync(f.fence,0,0):GL_ALREADY_SIGNALED;
            if(state!=GL_ALREADY_SIGNALED&&state!=GL_CONDITION_SATISFIED)break;
            release(f);retired.pop_front();
        }
    }
    void acquire() {
        collect();
        if(!reader||retired.size()>2)return;
        AImage* image=nullptr;
        const auto result=AImageReader_acquireLatestImage(reader,&image);
        if(result==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE||result==AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED)return;
        if(result!=AMEDIA_OK){failed=true;return;}
        release(pending);
        pending.image=image;
        AImage_getTimestamp(image,&pending.timestamp);
        timespec received{};clock_gettime(CLOCK_MONOTONIC,&received);
        pending.receivedAt=received.tv_sec*1000000000LL+received.tv_nsec;
    }
    void promote() {
        AHardwareBuffer* buffer=nullptr;
        if(AImage_getHardwareBuffer(pending.image,&buffer)!=AMEDIA_OK)throw std::runtime_error("No GPU camera buffer");
        const EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
        pending.eglImage=createImage(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,
            getNativeBuffer(buffer),attrs);
        if(pending.eglImage==EGL_NO_IMAGE_KHR)throw std::runtime_error("Camera buffer import failed");
        glGenTextures(1,&pending.texture);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES,pending.texture);
        imageTarget(GL_TEXTURE_EXTERNAL_OES,pending.eglImage);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(current.image)retired.push_back(current);
        current=pending;pending={};
    }
    void markUsed() {
        if(current.fence)glDeleteSync(current.fence);
        current.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    }
    void stop() {
        if(capture){ACameraCaptureSession_stopRepeating(capture);ACameraCaptureSession_close(capture);capture=nullptr;}
        if(device){ACameraDevice_close(device);device=nullptr;}
        glFinish(); // Lifecycle only; steady-state rendering uses nonblocking fences.
        release(pending);release(current);
        for(auto& f:retired)release(f);
        retired.clear();
        if(request){ACaptureRequest_free(request);request=nullptr;}
        if(target){ACameraOutputTarget_free(target);target=nullptr;}
        if(output){ACaptureSessionOutput_free(output);output=nullptr;}
        if(outputs){ACaptureSessionOutputContainer_free(outputs);outputs=nullptr;}
        if(reader){AImageReader_delete(reader);reader=nullptr;}
    }
};
