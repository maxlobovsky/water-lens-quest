#include <jni.h>
#include <time.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>
#include "xr_linear.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"WaterLens",__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"WaterLens",__VA_ARGS__)
static PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC getNativeBuffer;
static PFNEGLCREATEIMAGEKHRPROC createImage;
static PFNEGLDESTROYIMAGEKHRPROC destroyImage;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTarget;
#include "camera.h"
#include "shaders.h"
#include "camera_diagnostic.h"
#include "panel_controls.h"
#include "bench_status.h"

static void check(XrResult result,const char* message){
    if(XR_FAILED(result))throw std::runtime_error(std::string(message)+" ["+std::to_string(result)+"]");
}
#define XR(call) check((call),#call)
template<class T> static T function(XrInstance instance,const char* name){
    PFN_xrVoidFunction fn=nullptr;XR(xrGetInstanceProcAddr(instance,name,&fn));return reinterpret_cast<T>(fn);
}
static XrMatrix4x4f poseMatrix(const XrPosef& pose){
    XrMatrix4x4f m;const XrVector3f scale{1,1,1};
    XrMatrix4x4f_CreateTranslationRotationScale(&m,&pose.position,&pose.orientation,&scale);return m;
}
static XrMatrix4x4f multiply(const XrMatrix4x4f& a,const XrMatrix4x4f& b){
    XrMatrix4x4f out;XrMatrix4x4f_Multiply(&out,&a,&b);return out;
}
static XrMatrix4x4f inverse(const XrMatrix4x4f& m){
    XrMatrix4x4f out;XrMatrix4x4f_InvertRigidBody(&out,&m);return out;
}
static int64_t nanos(clockid_t clock){
    timespec t{};clock_gettime(clock,&t);return t.tv_sec*1000000000LL+t.tv_nsec;
}
#include "hud.h"
#include "projected_water.h"

struct Swapchain {
    XrSwapchain handle=XR_NULL_HANDLE;
    int width=0,height=0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
};

struct Application {
    android_app* app;
    JNIEnv* java=nullptr;
    bool resumed=false,running=false,focused=false,exit=false,camerasStarted=false;
    bool clear=false,cycling=true,depthAttempted=false,depthRunning=false,depthSupported=false,stereoAnnounced=false;
    float strength=.35f,speed=.6f,density=1.f,complexity=.45f,sharpness=0,coverage=1.f,waveTime=0,cycleTime=0;
    bool handSupported=false,aimSupported=false,handVisible[2]{},panelPlaced=false;
    double lastUiLog=0;
    XrPosef panelPose{{0,0,0,1},{0,0,0}},launcherPose{{0,0,0,1},{0,0,0}};
    Hud hud;
    PanelControls controls;
    XrHandTrackerEXT handTrackers[2]{};
    PFN_xrLocateHandJointsEXT locateHandJoints=nullptr;
    PFN_xrDestroyHandTrackerEXT destroyHandTracker=nullptr;
    std::array<float,5> cycleStart{.35f,.6f,1.f,.45f,0};
    double lastTick=0,lastStats=0,retryCameraAt=0;
    uint32_t renderCount=0,pairCount=0;
    XrInstance instance=XR_NULL_HANDLE;
    XrSession session=XR_NULL_HANDLE;
    XrSystemId system=XR_NULL_SYSTEM_ID;
    XrSpace local=XR_NULL_HANDLE,head=XR_NULL_HANDLE;
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    XrPassthroughFB passthrough=XR_NULL_HANDLE;
    XrPassthroughLayerFB passthroughLayer=XR_NULL_HANDLE;
    XrEnvironmentDepthProviderMETA depthProvider=XR_NULL_HANDLE;
    XrEnvironmentDepthSwapchainMETA depthSwapchain=XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLESKHR> depthImages;
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    EGLConfig config{};
    GLuint program=0,framebuffer=0,vao=0,dummyDepth=0,depthSampler=0;
    std::unordered_map<std::string_view,GLint> uniformLocations;
    bool srgb=false;
    Swapchain chains[2];
    Camera cameras[2];
    CameraDiagnostic diagnostic;
    ProjectedWater projected;
    BenchStatus bench;
    float measuredRenderFps=0,measuredCameraFps=0;
    bool fullView=false,projectedSupported=false;
    bool reconstructionPaused=false;
    ACameraManager* cameraManager=nullptr;
    XrActionSet actionSet=XR_NULL_HANDLE;
    XrAction trigger=XR_NULL_HANDLE,grip=XR_NULL_HANDLE,stick=XR_NULL_HANDLE,toggle=XR_NULL_HANDLE,cycle=XR_NULL_HANDLE;
    XrPath hands[2]{};
    bool triggerWas=false,gripWas=false;
    PFN_xrConvertTimespecTimeToTimeKHR toXrTime=nullptr;
    PFN_xrCreatePassthroughFB createPassthrough=nullptr;
    PFN_xrDestroyPassthroughFB destroyPassthrough=nullptr;
    PFN_xrCreatePassthroughLayerFB createPassthroughLayer=nullptr;
    PFN_xrDestroyPassthroughLayerFB destroyPassthroughLayer=nullptr;
    PFN_xrCreateEnvironmentDepthProviderMETA createDepth=nullptr;
    PFN_xrDestroyEnvironmentDepthProviderMETA destroyDepth=nullptr;
    PFN_xrStartEnvironmentDepthProviderMETA startDepth=nullptr;
    PFN_xrStopEnvironmentDepthProviderMETA stopDepth=nullptr;
    PFN_xrCreateEnvironmentDepthSwapchainMETA createDepthSwapchain=nullptr;
    PFN_xrDestroyEnvironmentDepthSwapchainMETA destroyDepthSwapchain=nullptr;
    PFN_xrEnumerateEnvironmentDepthSwapchainImagesMETA enumerateDepthImages=nullptr;
    PFN_xrAcquireEnvironmentDepthImageMETA acquireDepth=nullptr;

    explicit Application(android_app* activity):app(activity){
        app->userData=this;
        app->onAppCmd=[](android_app* a,int32_t command){
            auto* self=static_cast<Application*>(a->userData);
            if(command==APP_CMD_RESUME)self->resumed=true;
            if(command==APP_CMD_PAUSE)self->resumed=false;
        };
        app->activity->vm->AttachCurrentThread(&java,nullptr);
        diagnostic.configure(app->activity->externalDataPath);
        projected.configure(app->activity->externalDataPath);
        bench.configure(app->activity->externalDataPath);
        if(diagnostic.enabled){strength=0;cycling=false;coverage=1;}
    }
    bool permission(const char* name){
        jclass cls=java->GetObjectClass(app->activity->clazz);
        jmethodID method=java->GetMethodID(cls,"checkSelfPermission","(Ljava/lang/String;)I");
        jstring value=java->NewStringUTF(name);
        const bool ok=java->CallIntMethod(app->activity->clazz,method,value)==0;
        java->DeleteLocalRef(value);java->DeleteLocalRef(cls);return ok;
    }
    void status(const std::string& message){
        LOGI("%s",message.c_str());
        jclass cls=java->GetObjectClass(app->activity->clazz);
        jmethodID method=java->GetMethodID(cls,"notifyStatus","(Ljava/lang/String;)V");
        jstring value=java->NewStringUTF(message.c_str());
        java->CallVoidMethod(app->activity->clazz,method,value);
        java->DeleteLocalRef(value);java->DeleteLocalRef(cls);
        if(java->ExceptionCheck()){java->ExceptionDescribe();java->ExceptionClear();}
    }
    bool hasExtension(const std::vector<XrExtensionProperties>& extensions,const char* name){
        return std::any_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(e.extensionName,name)==0;});
    }
    void init(){
        auto initLoader=function<PFN_xrInitializeLoaderKHR>(XR_NULL_HANDLE,"xrInitializeLoaderKHR");
        XrLoaderInitInfoAndroidKHR loader{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loader.applicationVM=app->activity->vm;loader.applicationContext=app->activity->clazz;
        XR(initLoader(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loader)));
        uint32_t count=0;XR(xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr));
        std::vector<XrExtensionProperties> available(count,{XR_TYPE_EXTENSION_PROPERTIES});
        XR(xrEnumerateInstanceExtensionProperties(nullptr,count,&count,available.data()));
        std::vector<const char*> enabled{XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,XR_FB_PASSTHROUGH_EXTENSION_NAME,
            XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME};
        for(auto name:enabled)if(!hasExtension(available,name))throw std::runtime_error(std::string("Missing extension: ")+name);
        const bool refresh=hasExtension(available,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        const bool performance=hasExtension(available,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
        depthSupported=hasExtension(available,XR_META_ENVIRONMENT_DEPTH_EXTENSION_NAME);
        projectedSupported=hasExtension(available,XR_FB_TRIANGLE_MESH_EXTENSION_NAME);
        if(projectedSupported)enabled.push_back(XR_FB_TRIANGLE_MESH_EXTENSION_NAME);
        handSupported=hasExtension(available,XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        if(handSupported)enabled.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        aimSupported=handSupported&&hasExtension(available,XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
        if(aimSupported)enabled.push_back(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
        if(refresh)enabled.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        if(performance)enabled.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
        if(depthSupported)enabled.push_back(XR_META_ENVIRONMENT_DEPTH_EXTENSION_NAME);
        XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        android.applicationVM=app->activity->vm;android.applicationActivity=app->activity->clazz;
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};info.next=&android;
        std::strcpy(info.applicationInfo.applicationName,"Water Lens Stereo");
        info.applicationInfo.applicationVersion=10;info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,34);
        info.enabledExtensionCount=enabled.size();info.enabledExtensionNames=enabled.data();
        XR(xrCreateInstance(&info,&instance));
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XR(xrGetSystem(instance,&systemInfo,&system));
        toXrTime=function<PFN_xrConvertTimespecTimeToTimeKHR>(instance,"xrConvertTimespecTimeToTimeKHR");
        auto requirements=function<PFN_xrGetOpenGLESGraphicsRequirementsKHR>(instance,"xrGetOpenGLESGraphicsRequirementsKHR");
        XrGraphicsRequirementsOpenGLESKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        XR(requirements(instance,system,&req));
        initGL();
        XrGraphicsBindingOpenGLESAndroidKHR graphics{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
        graphics.display=display;graphics.config=config;graphics.context=context;
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&graphics;sessionInfo.systemId=system;
        XR(xrCreateSession(instance,&sessionInfo,&session));
        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};spaceInfo.poseInReferenceSpace.orientation.w=1;
        spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;XR(xrCreateReferenceSpace(session,&spaceInfo,&local));
        spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;XR(xrCreateReferenceSpace(session,&spaceInfo,&head));
        if(refresh){
            auto enumerate=function<PFN_xrEnumerateDisplayRefreshRatesFB>(instance,"xrEnumerateDisplayRefreshRatesFB");
            auto request=function<PFN_xrRequestDisplayRefreshRateFB>(instance,"xrRequestDisplayRefreshRateFB");
            uint32_t n=0;
            if(XR_SUCCEEDED(enumerate(session,0,&n,nullptr))&&n){
                std::vector<float> rates(n);XR(enumerate(session,n,&n,rates.data()));
                const float rate=*std::max_element(rates.begin(),rates.end());
                LOGI("Requesting %.0f Hz display: %d",rate,request(session,rate));
            }
        }
        if(performance){
            auto set=function<PFN_xrPerfSettingsSetPerformanceLevelEXT>(instance,"xrPerfSettingsSetPerformanceLevelEXT");
            set(session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
            set(session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
        }
        initPassthrough();initSwapchains();initInput();initHands();hud.init(app->activity->assetManager);
        if(projectedSupported)projected.init(instance,session,passthrough,local);
        // API acceptance did not produce visible distortion in the worn-headset
        // test. Keep the working camera renderer as the ordinary default.
        fullView=projected.ready&&projected.visibilityProbe&&!diagnostic.enabled;
        if(fullView){strength=.8f;speed=2;cycling=false;}
        if(bench.enabled){
            const auto& s=bench.effect;
            strength=s.strength;speed=s.speed;density=s.density;complexity=s.complexity;sharpness=s.sharpness;
            coverage=s.fullView?1:s.coverage;cycling=false;clear=s.clear;fullView=s.fullView&&projected.ready;
            LOGI("BENCH mode=%d configured; deterministic controls, no hand input required",bench.mode);
        }
        cameraManager=ACameraManager_create();
        status(fullView?"Surface visibility check: look for a green window changing size.":"Water Lens: Camera mode. Pinch Controls to adjust.");
    }
    void initGL(){
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if(!eglInitialize(display,nullptr,nullptr))throw std::runtime_error("EGL initialize failed");
        const EGLint attrs[]{EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,0,EGL_NONE};
        EGLint count=0;
        if(!eglChooseConfig(display,attrs,&config,1,&count)||!count)throw std::runtime_error("EGL config unavailable");
        const EGLint contextAttrs[]{EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
        context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
        const EGLint surfaceAttrs[]{EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
        surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
        if(!eglMakeCurrent(display,surface,surface,context))throw std::runtime_error("EGL context unavailable");
        getNativeBuffer=reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(eglGetProcAddress("eglGetNativeClientBufferANDROID"));
        createImage=reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
        destroyImage=reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
        imageTarget=reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        if(!getNativeBuffer||!createImage||!destroyImage||!imageTarget)throw std::runtime_error("GPU camera import unavailable");
        auto shader=[](GLenum type,const char* source){
            GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);
            GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
            if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),nullptr,log);throw std::runtime_error(log);}return s;
        };
        GLuint vs=shader(GL_VERTEX_SHADER,vertexSource),fs=shader(GL_FRAGMENT_SHADER,fragmentSource);
        program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
        GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){char log[2048];glGetProgramInfoLog(program,sizeof(log),nullptr,log);throw std::runtime_error(log);}
        glDeleteShader(vs);glDeleteShader(fs);glUseProgram(program);
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenFramebuffers(1,&framebuffer);
        glGenTextures(1,&dummyDepth);glBindTexture(GL_TEXTURE_2D_ARRAY,dummyDepth);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY,1,GL_R32F,1,1,2);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glGenSamplers(1,&depthSampler);
        glSamplerParameteri(depthSampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glSamplerParameteri(depthSampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glSamplerParameteri(depthSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glSamplerParameteri(depthSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glSamplerParameteri(depthSampler,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glBindSampler(1,depthSampler);
        glUniform1i(uniform("cameraImage"),0);glUniform1i(uniform("depthImage"),1);
        glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
    }
    GLint uniform(const char* name){
        auto it=uniformLocations.find(name);
        if(it!=uniformLocations.end())return it->second;
        GLint location=glGetUniformLocation(program,name);
        uniformLocations.emplace(name,location);return location;
    }
    void initPassthrough(){
        createPassthrough=function<PFN_xrCreatePassthroughFB>(instance,"xrCreatePassthroughFB");
        destroyPassthrough=function<PFN_xrDestroyPassthroughFB>(instance,"xrDestroyPassthroughFB");
        createPassthroughLayer=function<PFN_xrCreatePassthroughLayerFB>(instance,"xrCreatePassthroughLayerFB");
        destroyPassthroughLayer=function<PFN_xrDestroyPassthroughLayerFB>(instance,"xrDestroyPassthroughLayerFB");
        XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};info.flags=XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
        XR(createPassthrough(session,&info,&passthrough));
        XrPassthroughLayerCreateInfoFB layer{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
        layer.passthrough=passthrough;layer.purpose=XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
        layer.flags=XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
        XR(createPassthroughLayer(session,&layer,&passthroughLayer));
    }
    void initSwapchains(){
        uint32_t count=0;XR(xrEnumerateSwapchainFormats(session,0,&count,nullptr));
        std::vector<int64_t> formats(count);XR(xrEnumerateSwapchainFormats(session,count,&count,formats.data()));
        int64_t format=GL_SRGB8_ALPHA8;srgb=true;
        if(std::find(formats.begin(),formats.end(),format)==formats.end()){format=GL_RGBA8;srgb=false;}
        if(std::find(formats.begin(),formats.end(),format)==formats.end())throw std::runtime_error("No RGBA swapchain format");
        XR(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr));
        if(count!=2)throw std::runtime_error("This app requires two XR eye views");
        XrViewConfigurationView views[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        XR(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views));
        for(int eye=0;eye<2;eye++){
            auto& chain=chains[eye];chain.width=views[eye].recommendedImageRectWidth;chain.height=views[eye].recommendedImageRectHeight;
            XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            info.format=format;info.sampleCount=1;info.width=chain.width;info.height=chain.height;
            info.faceCount=1;info.arraySize=1;info.mipCount=1;
            XR(xrCreateSwapchain(session,&info,&chain.handle));
            XR(xrEnumerateSwapchainImages(chain.handle,0,&count,nullptr));
            chain.images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            XR(xrEnumerateSwapchainImages(chain.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.images.data())));
        }
    }
    XrPath path(const char* value){XrPath p;XR(xrStringToPath(instance,value,&p));return p;}
    void initInput(){
        hands[0]=path("/user/hand/left");hands[1]=path("/user/hand/right");
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strcpy(set.actionSetName,"water_controls");std::strcpy(set.localizedActionSetName,"Water controls");
        XR(xrCreateActionSet(instance,&set,&actionSet));
        auto action=[&](const char* name,XrActionType type,XrAction& out,bool perHand){
            XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};info.actionType=type;
            std::strcpy(info.actionName,name);std::strcpy(info.localizedActionName,name);
            info.countSubactionPaths=perHand?2:0;info.subactionPaths=perHand?hands:nullptr;
            XR(xrCreateAction(actionSet,&info,&out));
        };
        action("clear",XR_ACTION_TYPE_BOOLEAN_INPUT,toggle,false);action("cycle",XR_ACTION_TYPE_BOOLEAN_INPUT,cycle,false);
        action("trigger",XR_ACTION_TYPE_FLOAT_INPUT,trigger,true);action("grip",XR_ACTION_TYPE_FLOAT_INPUT,grip,true);
        action("adjust",XR_ACTION_TYPE_VECTOR2F_INPUT,stick,true);
        std::vector<XrActionSuggestedBinding> bindings{{toggle,path("/user/hand/right/input/a/click")},
            {cycle,path("/user/hand/left/input/x/click")}};
        for(auto side:{"left","right"}){
            std::string prefix=std::string("/user/hand/")+side;
            bindings.push_back({trigger,path((prefix+"/input/trigger/value").c_str())});
            bindings.push_back({grip,path((prefix+"/input/squeeze/value").c_str())});
            bindings.push_back({stick,path((prefix+"/input/thumbstick").c_str())});
        }
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile=path("/interaction_profiles/oculus/touch_controller");
        suggested.countSuggestedBindings=bindings.size();suggested.suggestedBindings=bindings.data();
        XR(xrSuggestInteractionProfileBindings(instance,&suggested));
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets=1;attach.actionSets=&actionSet;XR(xrAttachSessionActionSets(session,&attach));
    }
    void changeCycle(){
        cycling=!cycling;cycleTime=0;cycleStart={strength,speed,density,complexity,sharpness};
        status(cycling?"Slow effect cycle started":"Effect cycle paused");
    }
    EffectSettings settings()const{return {strength,speed,density,complexity,sharpness,coverage,cycling,clear,fullView,projected.ready};}
    void initHands(){
        if(!handSupported)return;
        XrSystemHandTrackingPropertiesEXT supported{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};properties.next=&supported;
        if(XR_FAILED(xrGetSystemProperties(instance,system,&properties))||!supported.supportsHandTracking){handSupported=false;return;}
        auto create=function<PFN_xrCreateHandTrackerEXT>(instance,"xrCreateHandTrackerEXT");
        locateHandJoints=function<PFN_xrLocateHandJointsEXT>(instance,"xrLocateHandJointsEXT");
        destroyHandTracker=function<PFN_xrDestroyHandTrackerEXT>(instance,"xrDestroyHandTrackerEXT");
        for(int h=0;h<2;h++){
            XrHandTrackerCreateInfoEXT info{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
            info.hand=h?XR_HAND_RIGHT_EXT:XR_HAND_LEFT_EXT;info.handJointSet=XR_HAND_JOINT_SET_DEFAULT_EXT;
            const auto result=create(session,&info,&handTrackers[h]);
            if(XR_FAILED(result)){LOGE("Hand tracker %d unavailable: %d",h,result);handSupported=false;}
        }
        LOGI("HAND tracking initialized=%d aim=%d",handSupported,aimSupported);
    }
    void placePanel(const XrPosef& headPose){
        const auto m=poseMatrix(headPose);
        const float yaw=std::atan2(m.m[8],m.m[10]);
        panelPose.position=headPose.position;panelPose.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};panelPlaced=true;
    }
    static bool panelIntersection(const XrMatrix4x4f& ray,float& x,float& y,float depth=1){
        const float dz=-ray.m[10];
        if(dz>=-.05f)return false;
        const float t=(-depth-ray.m[14])/dz;
        if(t<.02f||t>5)return false;
        x=ray.m[12]-ray.m[8]*t;y=ray.m[13]-ray.m[9]*t;return true;
    }
    void inputHands(XrTime time,double now,const XrSpaceLocation& headLocation){
        handVisible[0]=handVisible[1]=false;
        const auto flags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if(!focused||(headLocation.locationFlags&flags)!=flags){controls.resetInput();return;}
        if(!panelPlaced)placePanel(headLocation.pose);
        // Render and hit-test the tab from exactly the same predicted head pose.
        // It follows the screen in yaw, pitch, roll and translation; pinch selects it.
        launcherPose=headLocation.pose;
        PanelInput inputs[2];
        const auto panelFromWorld=inverse(poseMatrix(panelPose));
        const auto launcherFromWorld=inverse(poseMatrix(launcherPose));
        for(int h=0;h<(handSupported?2:0);h++){
            XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT]{};
            XrHandTrackingAimStateFB aim{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
            XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
            if(aimSupported)locations.next=&aim;
            locations.jointCount=XR_HAND_JOINT_COUNT_EXT;locations.jointLocations=joints;
            XrHandJointsLocateInfoEXT info{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};info.baseSpace=local;info.time=time;
            if(XR_FAILED(locateHandJoints(handTrackers[h],&info,&locations))||!locations.isActive)continue;
            handVisible[h]=true;
            const auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            const auto& tip=joints[XR_HAND_JOINT_INDEX_TIP_EXT];
            const bool systemGesture=aimSupported&&(aim.status&XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB);
            XrPosef rayPose{};bool pinch=false,haveRay=false;
            if(aimSupported){
                haveRay=(aim.status&XR_HAND_TRACKING_AIM_VALID_BIT_FB)&&!systemGesture;
                rayPose=aim.aimPose;pinch=(aim.status&XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB)!=0;
            }else {
                const auto& thumb=joints[XR_HAND_JOINT_THUMB_TIP_EXT];
                haveRay=(thumb.locationFlags&valid)==valid&&(tip.locationFlags&valid)==valid;
                const float dx=thumb.pose.position.x-tip.pose.position.x,dy=thumb.pose.position.y-tip.pose.position.y,dz=thumb.pose.position.z-tip.pose.position.z;
                pinch=dx*dx+dy*dy+dz*dz<(controls.hands[h].down?.040f*.040f:.025f*.025f);rayPose=headLocation.pose;
            }
            auto& p=inputs[h];
            if(haveRay){
                p.tracked=true;p.pinch=pinch;const auto ray=poseMatrix(rayPose);float x=0,y=0;
                if(controls.open&&panelIntersection(multiply(panelFromWorld,ray),x,y)&&PanelControls::background.contains(x,y,.035f)){
                    p.onSurface=true;p.surface=0;p.x=x;p.y=y;
                }
                if(!controls.open&&panelIntersection(multiply(launcherFromWorld,ray),x,y,PanelControls::launcherDepth)&&PanelControls::launcher.contains(x,y,.015f)){
                    p.onSurface=true;p.surface=1;p.x=x;p.y=y;
                }
            }
            // A head ray cannot reach an off-center head-locked tab. If the runtime
            // lacks hand aim, an intentional open/pinch/release can still reopen it.
            if(!aimSupported&&!controls.open&&haveRay){
                const auto r=PanelControls::launcher;p={true,pinch,true,1,r.x+r.w*.5f,r.y+r.h*.5f};
            }
        }
        auto value=settings();
        auto changed=controls.update(inputs,now,value);
        strength=value.strength;speed=value.speed;density=value.density;complexity=value.complexity;sharpness=value.sharpness;coverage=value.coverage;
        if(changed&PanelControls::CycleChanged){cycleTime=0;cycleStart={strength,speed,density,complexity,sharpness};}
        cycling=value.cycling;clear=value.clear;
        if(fullView!=value.fullView){fullView=value.fullView;retryCameraAt=0;status(fullView?"Surface test: this has not produced visible water. Switch back to Camera for the effect.":"Camera water: calibrated stereo at 100%");}
        if(changed&PanelControls::Recenter)placePanel(headLocation.pose);
        if(changed&&now-lastUiLog>.3){lastUiLog=now;LOGI("UI event=%u open=%d cycle=%d clear=%d view=%.2f",changed,controls.open,cycling,clear,coverage);}
    }
    void input(float dt){
        if(!focused)return;
        XrActiveActionSet active{actionSet,XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
        if(XR_FAILED(xrSyncActions(session,&sync)))return;
        auto clicked=[&](XrAction action){
            XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action;
            XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};xrGetActionStateBoolean(session,&info,&value);
            return value.isActive&&value.changedSinceLastSync&&value.currentState;
        };
        bool pressed=false,squeezed=false;
        for(int eye=0;eye<2;eye++){
            XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.subactionPath=hands[eye];
            XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
            info.action=trigger;xrGetActionStateFloat(session,&info,&value);pressed|=value.isActive&&value.currentState>.75f;
            info.action=grip;xrGetActionStateFloat(session,&info,&value);squeezed|=value.isActive&&value.currentState>.75f;
            info.action=stick;XrActionStateVector2f axis{XR_TYPE_ACTION_STATE_VECTOR2F};
            xrGetActionStateVector2f(session,&info,&axis);
            if(axis.isActive&&(std::abs(axis.currentState.x)>.2f||std::abs(axis.currentState.y)>.2f)){
                cycling=false;
                if(eye==0){strength=std::clamp(strength+axis.currentState.x*dt*.3f,0.f,1.f);speed=std::clamp(speed+axis.currentState.y*dt*2.f,0.f,12.f);}
                else {density=std::clamp(density+axis.currentState.x*dt*.5f,.3f,2.5f);complexity=std::clamp(complexity+axis.currentState.y*dt*.3f,0.f,1.f);}
            }
        }
        if(clicked(toggle)||(pressed&&!triggerWas)){clear=!clear;status(clear?"Normal system passthrough":"Stereo water effect");}
        if(clicked(cycle)||(squeezed&&!gripWas))changeCycle();
        triggerWas=pressed;gripWas=squeezed;
    }
    void updateEffect(float dt){
        if(cycling){
            cycleTime+=dt;
            constexpr float presets[4][5]{{.2f,.3f,.55f,.15f,0},{.45f,.65f,2.1f,.55f,.2f},{.85f,.15f,.85f,.9f,.15f},{.7f,7.f,1.4f,.65f,.9f}};
            const float phase=std::max(0.f,(cycleTime-12)/24);
            const int index=int(phase)%4;
            float t=cycleTime<12?cycleTime/12:phase-std::floor(phase);t=t*t*(3-2*t);
            const float* from=cycleTime<12?cycleStart.data():presets[index];
            const float* to=cycleTime<12?presets[0]:presets[(index+1)%4];
            float* values[]{&strength,&speed,&density,&complexity,&sharpness};
            for(int i=0;i<5;i++)*values[i]=from[i]+(to[i]-from[i])*t;
        }
        waveTime=std::fmod(waveTime+dt*speed,628.3185307f);
    }
    void discoverCameras(){
        ACameraIdList* ids=nullptr;
        if(ACameraManager_getCameraIdList(cameraManager,&ids)!=ACAMERA_OK)throw std::runtime_error("Camera discovery failed");
        for(int i=0;i<ids->numCameras;i++){
            ACameraMetadata* metadata=nullptr;
            const auto metadataResult=ACameraManager_getCameraCharacteristics(cameraManager,ids->cameraIds[i],&metadata);
            if(metadataResult!=ACAMERA_OK){
                if(bench.enabled)LOGI("BENCH camera id=%s metadata result=%d",ids->cameraIds[i],metadataResult);
                continue;
            }
            ACameraMetadata_const_entry source{},position{};
            ACameraMetadata_const_entry format{};
            if(bench.enabled&&bench.mode==3){
                const int src=Camera::entry(metadata,0x80004d00,source)?Camera::integer(source):-1;
                const int fmt=Camera::entry(metadata,0x80004d08,format)?Camera::integer(format):-1;
                LOGI("BENCH visible camera id=%s source=%d spatial_format=%d",ids->cameraIds[i],src,fmt);
                if(Camera::entry(metadata,0x80004d00,source)&&Camera::integer(source)==3&&
                   Camera::entry(metadata,0x80004d08,format)&&Camera::integer(format)==0){
                    try{for(int eye=0;eye<2;eye++){
                        cameras[eye].id=ids->cameraIds[i];cameras[eye].eye=eye;
                        cameras[eye].configure(metadata,1728,true);
                    }}catch(...){ACameraMetadata_free(metadata);ACameraManager_deleteCameraIdList(ids);throw;}
                }
                ACameraMetadata_free(metadata);continue;
            }
            if(Camera::entry(metadata,0x80004d00,source)&&Camera::integer(source)==0&&Camera::entry(metadata,0x80004d01,position)){
                const int eye=Camera::integer(position);
                if(eye==0||eye==1){
                    cameras[eye].id=ids->cameraIds[i];cameras[eye].eye=eye;
                    try{cameras[eye].configure(metadata,diagnostic.requestedSize);}catch(...){ACameraMetadata_free(metadata);ACameraManager_deleteCameraIdList(ids);throw;}
                }
            }
            ACameraMetadata_free(metadata);
        }
        ACameraManager_deleteCameraIdList(ids);
        if(bench.enabled&&bench.mode==3&&cameras[0].id.empty())
            throw std::runtime_error("Spatial stereo camera is not exposed to this app");
        if(cameras[0].id.empty()||cameras[1].id.empty()||(!cameras[0].spatialSbs&&cameras[0].id==cameras[1].id))
            throw std::runtime_error("Two distinct passthrough cameras are required");
    }
    void manageCameras(double now){
        const bool wanted=!fullView&&resumed&&focused&&permission("horizonos.permission.HEADSET_CAMERA")&&permission("android.permission.CAMERA");
        if(camerasStarted&&(!wanted||cameras[0].failed||cameras[1].failed)){
            for(auto& c:cameras)c.stop();camerasStarted=false;retryCameraAt=now+2;
            LOGI("Cameras stopped; system passthrough remains active");
        }
        if(!camerasStarted&&wanted&&now>=retryCameraAt){
            try{
                discoverCameras();
                cameras[0].start(cameraManager,display);
                if(!cameras[0].spatialSbs)cameras[1].start(cameraManager,display);
                camerasStarted=true;pairCount=0;stereoAnnounced=false;
                LOGI("Both cameras open. Waiting for a synchronized stereo pair.");
            }catch(const std::exception& e){
                for(auto& c:cameras)c.stop();
                retryCameraAt=now+10;status(std::string("Stereo camera error: ")+e.what());
            }
        }
    }
    void capturePose(Camera& camera,CameraFrame& frame,XrTime predicted){
        if(!camera.bootTimeTimestamps)return; // Never invent a capture time.
        const int64_t timestamp=frame.timestamp+nanos(CLOCK_MONOTONIC)-nanos(CLOCK_BOOTTIME);
        timespec ts{time_t(timestamp/1000000000LL),long(timestamp%1000000000LL)};
        XrTime time=0;
        if(XR_FAILED(toXrTime(instance,&ts,&time))||predicted-time>300000000LL||time>predicted)return;
        XrSpaceLocation pose{XR_TYPE_SPACE_LOCATION};
        if(XR_SUCCEEDED(xrLocateSpace(head,local,time,&pose))&&
            (pose.locationFlags&(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT))==
                (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT)){
            frame.headPose=pose.pose;frame.poseValid=true;
        }
    }
    void updateCameras(XrTime predicted){
        if(!camerasStarted)return;
        if(cameras[0].spatialSbs){
            auto& c=cameras[0];c.acquire();if(!c.pending.image)return;
            capturePose(c,c.pending,predicted);c.promote();pairCount++;return;
        }
        for(auto& c:cameras)c.acquire();
        auto& left=cameras[0];auto& right=cameras[1];
        if(!left.pending.image||!right.pending.image)return;
        const int64_t difference=left.pending.timestamp-right.pending.timestamp;
        if(std::abs(difference)>10000000LL){
            if(difference<0)left.release(left.pending);else right.release(right.pending);
            return;
        }
        for(auto& c:cameras){capturePose(c,c.pending,predicted);c.promote();}
        pairCount++;
        if(!stereoAnnounced){stereoAnnounced=true;status("Water is ready. Point at a control, then pinch to select.");}
    }
    void manageDepth(){
        if(fullView){if(depthRunning){stopDepth(depthProvider);depthRunning=false;}return;}
        if(depthProvider&&!depthRunning&&focused){depthRunning=XR_SUCCEEDED(startDepth(depthProvider));return;}
        if(depthAttempted||!depthSupported||!focused||!permission("com.oculus.permission.USE_SCENE"))return;
        depthAttempted=true;
        try{
            createDepth=function<PFN_xrCreateEnvironmentDepthProviderMETA>(instance,"xrCreateEnvironmentDepthProviderMETA");
            destroyDepth=function<PFN_xrDestroyEnvironmentDepthProviderMETA>(instance,"xrDestroyEnvironmentDepthProviderMETA");
            startDepth=function<PFN_xrStartEnvironmentDepthProviderMETA>(instance,"xrStartEnvironmentDepthProviderMETA");
            stopDepth=function<PFN_xrStopEnvironmentDepthProviderMETA>(instance,"xrStopEnvironmentDepthProviderMETA");
            createDepthSwapchain=function<PFN_xrCreateEnvironmentDepthSwapchainMETA>(instance,"xrCreateEnvironmentDepthSwapchainMETA");
            destroyDepthSwapchain=function<PFN_xrDestroyEnvironmentDepthSwapchainMETA>(instance,"xrDestroyEnvironmentDepthSwapchainMETA");
            enumerateDepthImages=function<PFN_xrEnumerateEnvironmentDepthSwapchainImagesMETA>(instance,"xrEnumerateEnvironmentDepthSwapchainImagesMETA");
            acquireDepth=function<PFN_xrAcquireEnvironmentDepthImageMETA>(instance,"xrAcquireEnvironmentDepthImageMETA");
            XrEnvironmentDepthProviderCreateInfoMETA info{XR_TYPE_ENVIRONMENT_DEPTH_PROVIDER_CREATE_INFO_META};
            XR(createDepth(session,&info,&depthProvider));
            XrEnvironmentDepthSwapchainCreateInfoMETA chain{XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_CREATE_INFO_META};
            XR(createDepthSwapchain(depthProvider,&chain,&depthSwapchain));
            uint32_t count=0;XR(enumerateDepthImages(depthSwapchain,0,&count,nullptr));
            depthImages.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            XR(enumerateDepthImages(depthSwapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(depthImages.data())));
            XR(startDepth(depthProvider));depthRunning=true;
            LOGI("Environment depth enabled for stereo reprojection.");
        }catch(const std::exception& e){LOGE("Depth unavailable: %s",e.what());status("Nearby objects may look less aligned while room depth is unavailable.");}
    }
    void events(){
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while(xrPollEvent(instance,&event)==XR_SUCCESS){
            if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
                auto* changed=reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
                state=changed->state;focused=state==XR_SESSION_STATE_FOCUSED;
                if(state==XR_SESSION_STATE_READY){
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XR(xrBeginSession(session,&begin));running=true;
                }
                if(state==XR_SESSION_STATE_STOPPING){for(auto& c:cameras)c.stop();camerasStarted=false;XR(xrEndSession(session));running=false;}
                if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)exit=true;
                LOGI("XR session state %d",state);
            }else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)exit=true;
            event={XR_TYPE_EVENT_DATA_BUFFER};
        }
    }
    void setFov(const char* name,const XrFovf& fov){
        glUniform4f(uniform(name),std::tan(fov.angleLeft),std::tan(fov.angleRight),std::tan(fov.angleDown),std::tan(fov.angleUp));
    }
    void render(){
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame{XR_TYPE_FRAME_STATE};
        XR(xrWaitFrame(session,&wait,&frame));
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};XR(xrBeginFrame(session,&begin));
        const double now=nanos(CLOCK_MONOTONIC)*1e-9;
        const float dt=lastTick?float(std::clamp(now-lastTick,0.,.1)):0;lastTick=now;
        if(diagnostic.compare&&diagnostic.captured&&diagnostic.requestedSize==1280){
            for(auto& c:cameras)c.stop();camerasStarted=false;retryCameraAt=0;
            diagnostic.requestedSize=1728;diagnostic.captured=false;diagnostic.firstFrame=0;
            LOGI("DIAG switching to 1728 within the same tracking space");
        }
        input(dt);manageCameras(now);manageDepth();
        updateCameras(frame.predictedDisplayTime);
        const bool pauseReconstruction=projected.ready&&projected.solo()&&fullView&&!clear&&strength>.001f;
        if(pauseReconstruction!=reconstructionPaused){
            if(pauseReconstruction){
                auto pause=function<PFN_xrPassthroughLayerPauseFB>(instance,"xrPassthroughLayerPauseFB");
                XR(pause(passthroughLayer));
            }else{
                auto resume=function<PFN_xrPassthroughLayerResumeFB>(instance,"xrPassthroughLayerResumeFB");
                XR(resume(passthroughLayer));
            }
            reconstructionPaused=pauseReconstruction;
            LOGI("SURFACE reconstruction paused=%d",reconstructionPaused);
        }
        XrCompositionLayerPassthroughFB passthroughComposition{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
        passthroughComposition.layerHandle=passthroughLayer;
        const XrCompositionLayerBaseHeader* layers[3]{reinterpret_cast<XrCompositionLayerBaseHeader*>(&passthroughComposition),nullptr,nullptr};
        const bool reconstructionSubmitted=frame.shouldRender&&!reconstructionPaused;
        uint32_t layerCount=reconstructionSubmitted?1:0;
        XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime=frame.predictedDisplayTime;locate.space=local;
        XrViewState viewState{XR_TYPE_VIEW_STATE};uint32_t count=0;
        XR(xrLocateViews(session,&locate,&viewState,2,&count,views));
        XrSpaceLocation currentHead{XR_TYPE_SPACE_LOCATION};
        XR(xrLocateSpace(head,local,frame.predictedDisplayTime,&currentHead));
        inputHands(frame.predictedDisplayTime,now,currentHead);updateEffect(dt);
        const auto headFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        XrCompositionLayerPassthroughFB surfaceComposition{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
        surfaceComposition.layerHandle=projected.layer;
        surfaceComposition.flags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        bool surfaceSubmitted=false;
        if(fullView&&!clear&&strength>.001f&&frame.shouldRender&&(currentHead.locationFlags&headFlags)==headFlags){
            if(projected.update(local,frame.predictedDisplayTime,currentHead.pose,waveTime,settings())){
                layers[layerCount++]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&surfaceComposition);surfaceSubmitted=true;
            }
            else {fullView=false;retryCameraAt=0;status("Full view unavailable. Restoring Camera mode.");}
        }
        const bool showHud=focused&&panelPlaced&&(currentHead.locationFlags&headFlags)==headFlags;
        if(showHud)hud.update(settings(),controls,handSupported,aimSupported);
        XrCompositionLayerProjectionView projections[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const auto flags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        bool fresh=false;
        const auto& rightFrame=cameras[cameras[0].spatialSbs?0:1].current;
        if(camerasStarted&&cameras[0].current.image&&rightFrame.image){
            const int64_t clockNow=nanos(CLOCK_MONOTONIC);
            fresh=clockNow-cameras[0].current.receivedAt<250000000LL&&
                  clockNow-rightFrame.receivedAt<250000000LL;
        }
        const bool showWater=!fullView&&!clear&&fresh;
        if(frame.shouldRender&&(showWater||showHud)&&count==2&&(viewState.viewStateFlags&flags)==flags){
            XrEnvironmentDepthImageMETA depth{XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_META};
            depth.views[0].type=depth.views[1].type=XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_VIEW_META;
            XrEnvironmentDepthImageAcquireInfoMETA acquire{XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_ACQUIRE_INFO_META};
            acquire.space=local;acquire.displayTime=frame.predictedDisplayTime;
            const bool validDepth=depthRunning&&acquireDepth(depthProvider,&acquire,&depth)==XR_SUCCESS&&depth.swapchainIndex<depthImages.size();
            glUseProgram(program);glBindVertexArray(vao);
            glUniform1f(uniform("waveTime"),waveTime);glUniform1f(uniform("strength"),strength);
            glUniform1f(uniform("density"),density);glUniform1f(uniform("complexity"),complexity);
            glUniform1f(uniform("coverage"),coverage);glUniform1f(uniform("sharpness"),sharpness);
            glUniform1i(uniform("srgbOutput"),srgb);glUniform1i(uniform("hasDepth"),validDepth);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D_ARRAY,validDepth?depthImages[depth.swapchainIndex].image:dummyDepth);
            glBindSampler(1,depthSampler);
            glActiveTexture(GL_TEXTURE0);
            const bool captureDiagnostic=diagnostic.ready(now,showWater);
            for(int eye=0;eye<2;eye++){
                glUseProgram(program);glBindVertexArray(vao);
                auto& chain=chains[eye];auto& camera=cameras[eye];
                auto& frameOwner=cameras[cameras[0].spatialSbs?0:eye];
                const auto& cameraFrame=frameOwner.current;
                const auto worldFromEye=poseMatrix(views[eye].pose);
                const auto captureHead=poseMatrix(cameraFrame.poseValid?cameraFrame.headPose:currentHead.pose);
                const auto cameraFromEye=multiply(multiply(camera.cameraFromHead,inverse(captureHead)),worldFromEye);
                glUniformMatrix4fv(uniform("cameraFromEye"),1,GL_FALSE,cameraFromEye.m);
                glUniform4fv(uniform("calibration"),1,camera.calibration);
                glUniform1f(uniform("skew"),camera.intrinsics[4]);
                glUniform3fv(uniform("radial"),1,camera.distortion);
                glUniform2fv(uniform("tangential"),1,camera.distortion+3);
                glUniform1i(uniform("eye"),eye);setFov("fov",views[eye].fov);
                glUniform1i(uniform("spatialSbs"),camera.spatialSbs);
                if(validDepth){
                    const auto depthFromEye=multiply(inverse(poseMatrix(depth.views[eye].pose)),worldFromEye);
                    glUniformMatrix4fv(uniform("depthFromEye"),1,GL_FALSE,depthFromEye.m);
                    setFov("depthFov",depth.views[eye].fov);
                    glUniform1f(uniform("depthNear"),depth.nearZ);glUniform1f(uniform("depthFar"),depth.farZ);
                }
                glBindTexture(GL_TEXTURE_EXTERNAL_OES,cameraFrame.texture);
                uint32_t index=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                XR(xrAcquireSwapchainImage(chain.handle,&ai,&index));
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
                XR(xrWaitSwapchainImage(chain.handle,&wi));
                glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,chain.images[index].image,0);
                if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete XR framebuffer");
                glViewport(0,0,chain.width,chain.height);
                if(showWater){glDrawArrays(GL_TRIANGLES,0,3);frameOwner.markUsed();}
                else {const GLfloat transparent[4]{};glClearBufferfv(GL_COLOR,0,transparent);}
                if(captureDiagnostic)diagnostic.capture(eye,camera,cameraFrame,framebuffer,chain.width,chain.height,views[eye],currentHead.pose);
                if(showHud)hud.draw(views[eye],panelPose,launcherPose);
                glBindFramebuffer(GL_FRAMEBUFFER,0);glFlush();
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR(xrReleaseSwapchainImage(chain.handle,&release));
                projections[eye].pose=views[eye].pose;projections[eye].fov=views[eye].fov;
                projections[eye].subImage.swapchain=chain.handle;
                projections[eye].subImage.imageRect.extent={chain.width,chain.height};
            }
            projection.space=local;projection.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            projection.viewCount=2;projection.views=projections;
            layers[layerCount++]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection);
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;
        end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=layerCount;end.layers=layers;
        XR(xrEndFrame(session,&end));
        renderCount++;
        if(now-lastStats>=2){
            const double elapsed=lastStats?now-lastStats:2;
            measuredRenderFps=renderCount/elapsed;measuredCameraFps=pairCount/elapsed;
            LOGI("STATS v10 fullView=%d camera=%d render=%.1ffps stereo=%.1ffps depth=%d capturePose=%d/%d hands=%d/%d view=%.2f strength=%.2f speed=%.2f density=%.2f mix=%.2f sharp=%.2f",
                fullView,cameras[0].width,
                renderCount/elapsed,pairCount/elapsed,depthRunning,cameras[0].current.poseValid,cameras[1].current.poseValid,handVisible[0],handVisible[1],coverage,strength,speed,density,complexity,sharpness);
            if(projected.updates){LOGI("FULLVIEW update=%.3fms mesh=%.1ffps",projected.updateMilliseconds/projected.updates,projected.updates/elapsed);projected.updates=0;projected.updateMilliseconds=0;}
            renderCount=pairCount=0;lastStats=now;
        }
        bench.write(now,settings(),focused,frame.shouldRender,(currentHead.locationFlags&headFlags)==headFlags,
            count==2&&(viewState.viewStateFlags&flags)==flags,fresh,showWater&&projection.viewCount==2,
            surfaceSubmitted,measuredRenderFps,measuredCameraFps,projected.experiment,reconstructionSubmitted);
    }
    void run(){
        while(!app->destroyRequested&&!exit){
            int events;android_poll_source* source=nullptr;
            while(ALooper_pollOnce(running?0:50,nullptr,&events,reinterpret_cast<void**>(&source))>=0){
                if(source)source->process(app,source);
                if(app->destroyRequested)break;
                if(!running)break;
            }
            if(app->destroyRequested)break;
            if(!instance){if(resumed&&app->window)init();else continue;}
            this->events();
            if(running)render();
        }
    }
    ~Application(){
        if(diagnostic.cameraProgram)glDeleteProgram(diagnostic.cameraProgram);
        for(auto& c:cameras)c.stop();
        if(cameraManager)ACameraManager_delete(cameraManager);
        if(depthRunning)stopDepth(depthProvider);
        if(depthSwapchain&&destroyDepthSwapchain)destroyDepthSwapchain(depthSwapchain);
        if(depthProvider&&destroyDepth)destroyDepth(depthProvider);
        projected.destroy();
        if(passthroughLayer)destroyPassthroughLayer(passthroughLayer);
        if(passthrough)destroyPassthrough(passthrough);
        for(auto& c:chains)if(c.handle)xrDestroySwapchain(c.handle);
        for(auto tracker:handTrackers)if(tracker&&destroyHandTracker)destroyHandTracker(tracker);
        if(head)xrDestroySpace(head);if(local)xrDestroySpace(local);
        if(session)xrDestroySession(session);if(actionSet)xrDestroyActionSet(actionSet);
        if(instance)xrDestroyInstance(instance);
        if(display!=EGL_NO_DISPLAY){
            hud.destroy();
            if(program)glDeleteProgram(program);
            if(framebuffer)glDeleteFramebuffers(1,&framebuffer);
            if(vao)glDeleteVertexArrays(1,&vao);
            if(dummyDepth)glDeleteTextures(1,&dummyDepth);
            if(depthSampler)glDeleteSamplers(1,&depthSampler);
            eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
            if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
            if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
            eglTerminate(display);
        }
        app->activity->vm->DetachCurrentThread();
    }
};
void android_main(android_app* app){
    Application application(app);
    try{application.run();}
    catch(const std::exception& error){
        application.status(std::string("Water Lens could not start: ")+error.what());
        LOGE("Fatal: %s",error.what());ANativeActivity_finish(app->activity);
    }
}
