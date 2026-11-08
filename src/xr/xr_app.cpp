// xr_app.cpp - OpenXR session, swapchains, actions and haptics.

#if defined(_WIN32)
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>   // IUnknown, used by openxr_platform.h on Win32
#elif defined(__ANDROID__)
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <jni.h>
#include <EGL/egl.h>
#else
#define XR_USE_PLATFORM_EGL
#define XR_USE_GRAPHICS_API_OPENGL
#include <EGL/egl.h>
#endif

#include "xr_app.h"
#include "gl_api.h"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

using namespace vm;

namespace scr {

#define XR_CHECK(call)                                                                      \
    do {                                                                                    \
        XrResult _r = (call);                                                               \
        if (XR_FAILED(_r)) {                                                                \
            fprintf(stderr, "[StuntCarVR] OpenXR: %s failed (%d) at %s:%d\n", #call, (int)_r, __FILE__, __LINE__); \
            return false;                                                                   \
        }                                                                                   \
    } while (0)

typedef void(SCRGL_APIENTRY *PFN_glRenderbufferStorageMultisample_)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void(SCRGL_APIENTRY *PFN_glFramebufferTexture2DMultisampleEXT_)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
typedef void(SCRGL_APIENTRY *PFN_glRenderbufferStorageMultisampleEXT_)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);

struct EyeTarget
{
    XrSwapchain swapchain = XR_NULL_HANDLE;
    std::vector<GLuint> images;
    int32_t w = 0, h = 0;
    GLuint fbo = 0, depth = 0;
    GLuint msaa_fbo = 0, msaa_color = 0, msaa_depth = 0;   // desktop MSAA
};

struct XrApp::Impl
{
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE, view_space = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    std::vector<EyeTarget> eyes;
    std::vector<XrView> xr_views;
    std::vector<XrCompositionLayerProjectionView> proj_views;
    XrFrameState frame_state{XR_TYPE_FRAME_STATE};
    bool srgb = false;
    bool gles = false;
    int samples = 1;
    bool ext_refresh_rate = false;
    PFN_glRenderbufferStorageMultisample_ glRenderbufferStorageMultisample = nullptr;
    PFN_glFramebufferTexture2DMultisampleEXT_ glFramebufferTexture2DMultisampleEXT = nullptr;
    PFN_glRenderbufferStorageMultisampleEXT_ glRenderbufferStorageMultisampleEXT = nullptr;

    // actions
    XrActionSet action_set = XR_NULL_HANDLE;
    XrAction a_pose = XR_NULL_HANDLE, a_trigger = XR_NULL_HANDLE, a_grip = XR_NULL_HANDLE, a_stick = XR_NULL_HANDLE;
    XrAction a_a = XR_NULL_HANDLE, a_b = XR_NULL_HANDLE, a_x = XR_NULL_HANDLE, a_y = XR_NULL_HANDLE, a_menu = XR_NULL_HANDLE;
    XrAction a_haptic = XR_NULL_HANDLE;
    XrPath hand_path[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrSpace hand_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool prev_a = false, prev_b = false, prev_x = false, prev_y = false, prev_menu = false;

    XrPath path(const char *s)
    {
        XrPath p = XR_NULL_PATH;
        xrStringToPath(instance, s, &p);
        return p;
    }
};

static Pose ToPose(const XrPosef &p)
{
    return {vec3(p.position.x, p.position.y, p.position.z),
            quat(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w)};
}

static bool HasExt(const std::vector<XrExtensionProperties> &exts, const char *name)
{
    for (const auto &e : exts)
        if (strcmp(e.extensionName, name) == 0) return true;
    return false;
}

bool XrApp::init(const XrGraphicsHandles &gfx, const char *app_name)
{
    m_impl = new Impl();
    Impl &I = *m_impl;
    I.gles = gfx.gles;

#if defined(__ANDROID__)
    {
        PFN_xrInitializeLoaderKHR initLoader = nullptr;
        if (XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&initLoader)) && initLoader)
        {
            XrLoaderInitInfoAndroidKHR li{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
            li.applicationVM = gfx.java_vm;
            li.applicationContext = gfx.activity;
            initLoader((const XrLoaderInitInfoBaseHeaderKHR *)&li);
        }
    }
#endif

    // --- extensions --------------------------------------------------------------------------
    uint32_t n = 0;
    XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr));
    std::vector<XrExtensionProperties> exts(n, {XR_TYPE_EXTENSION_PROPERTIES});
    XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data()));

    std::vector<const char *> enable;
#if defined(__ANDROID__)
    const char *gfx_ext = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
    enable.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
#else
    const char *gfx_ext = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
#if !defined(_WIN32)
    if (!HasExt(exts, XR_MNDX_EGL_ENABLE_EXTENSION_NAME))
    {
        fprintf(stderr, "[StuntCarVR] OpenXR runtime lacks %s (needed on Linux)\n", XR_MNDX_EGL_ENABLE_EXTENSION_NAME);
        return false;
    }
    enable.push_back(XR_MNDX_EGL_ENABLE_EXTENSION_NAME);
#endif
#endif
    if (!HasExt(exts, gfx_ext))
    {
        fprintf(stderr, "[StuntCarVR] OpenXR runtime does not support %s\n", gfx_ext);
        return false;
    }
    enable.push_back(gfx_ext);
    if (HasExt(exts, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
    {
        enable.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        I.ext_refresh_rate = true;
    }

    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strncpy(ci.applicationInfo.applicationName, app_name, XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.applicationVersion = 1;
    strncpy(ci.applicationInfo.engineName, "StuntCarVR", XR_MAX_ENGINE_NAME_SIZE - 1);
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);   // 1.0 core; widely supported
    ci.enabledExtensionCount = (uint32_t)enable.size();
    ci.enabledExtensionNames = enable.data();
#if defined(__ANDROID__)
    XrInstanceCreateInfoAndroidKHR aci{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    aci.applicationVM = gfx.java_vm;
    aci.applicationActivity = gfx.activity;
    ci.next = &aci;
#endif
    XR_CHECK(xrCreateInstance(&ci, &I.instance));

    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(I.instance, &ip)))
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "%s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
                 XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
        m_runtime = buf;
    }

    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_CHECK(xrGetSystem(I.instance, &sgi, &I.system));
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(I.instance, I.system, &sp))) m_system = sp.systemName;

    // Graphics requirements must be queried before creating the session
#if defined(__ANDROID__)
    {
        PFN_xrGetOpenGLESGraphicsRequirementsKHR getReq = nullptr;
        XR_CHECK(xrGetInstanceProcAddr(I.instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&getReq));
        XrGraphicsRequirementsOpenGLESKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        XR_CHECK(getReq(I.instance, I.system, &req));
    }
#else
    {
        PFN_xrGetOpenGLGraphicsRequirementsKHR getReq = nullptr;
        XR_CHECK(xrGetInstanceProcAddr(I.instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&getReq));
        XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
        XR_CHECK(getReq(I.instance, I.system, &req));
    }
#endif

    // --- session ---------------------------------------------------------------------------
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.systemId = I.system;
#if defined(_WIN32)
    XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    binding.hDC = (HDC)gfx.hdc;
    binding.hGLRC = (HGLRC)gfx.hglrc;
#elif defined(__ANDROID__)
    XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = (EGLDisplay)gfx.egl_display;
    binding.config = (EGLConfig)gfx.egl_config;
    binding.context = (EGLContext)gfx.egl_context;
#else
    XrGraphicsBindingEGLMNDX binding{XR_TYPE_GRAPHICS_BINDING_EGL_MNDX};
    binding.getProcAddress = (PFN_xrEglGetProcAddressMNDX)gfx.egl_get_proc_address;
    binding.display = (EGLDisplay)gfx.egl_display;
    binding.config = (EGLConfig)gfx.egl_config;
    binding.context = (EGLContext)gfx.egl_context;
#endif
    sci.next = &binding;
    XR_CHECK(xrCreateSession(I.instance, &sci, &I.session));

    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XR_CHECK(xrCreateReferenceSpace(I.session, &rs, &I.local));
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    XR_CHECK(xrCreateReferenceSpace(I.session, &rs, &I.view_space));

    // --- swapchains ------------------------------------------------------------------------
    uint32_t vc = 0;
    XR_CHECK(xrEnumerateViewConfigurationViews(I.instance, I.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &vc, nullptr));
    std::vector<XrViewConfigurationView> vcv(vc, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    XR_CHECK(xrEnumerateViewConfigurationViews(I.instance, I.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, vc, &vc, vcv.data()));

    uint32_t nf = 0;
    XR_CHECK(xrEnumerateSwapchainFormats(I.session, 0, &nf, nullptr));
    std::vector<int64_t> formats(nf);
    XR_CHECK(xrEnumerateSwapchainFormats(I.session, nf, &nf, formats.data()));
    int64_t fmt = GL_RGBA8;
    for (int64_t f : formats)
        if (f == GL_SRGB8_ALPHA8) { fmt = f; break; }
    I.srgb = (fmt == GL_SRGB8_ALPHA8);

    // MSAA: free on mobile tilers with EXT_multisampled_render_to_texture, resolve-blit on desktop
    I.glRenderbufferStorageMultisample = (PFN_glRenderbufferStorageMultisample_)scrgl::GetProc("glRenderbufferStorageMultisample");
    if (I.gles && scrgl::HasExtension("GL_EXT_multisampled_render_to_texture"))
    {
        I.glFramebufferTexture2DMultisampleEXT = (PFN_glFramebufferTexture2DMultisampleEXT_)scrgl::GetProc("glFramebufferTexture2DMultisampleEXT");
        I.glRenderbufferStorageMultisampleEXT = (PFN_glRenderbufferStorageMultisampleEXT_)scrgl::GetProc("glRenderbufferStorageMultisampleEXT");
    }
    I.samples = 4;

    I.eyes.resize(vc);
    m_views.resize(vc);
    for (uint32_t i = 0; i < vc; i++)
    {
        EyeTarget &e = I.eyes[i];
        e.w = (int32_t)vcv[i].recommendedImageRectWidth;
        e.h = (int32_t)vcv[i].recommendedImageRectHeight;
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format = fmt;
        sc.sampleCount = 1;
        sc.width = (uint32_t)e.w;
        sc.height = (uint32_t)e.h;
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        XR_CHECK(xrCreateSwapchain(I.session, &sc, &e.swapchain));

        uint32_t ni = 0;
        XR_CHECK(xrEnumerateSwapchainImages(e.swapchain, 0, &ni, nullptr));
#if defined(__ANDROID__)
        std::vector<XrSwapchainImageOpenGLESKHR> imgs(ni, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
        std::vector<XrSwapchainImageOpenGLKHR> imgs(ni, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
        XR_CHECK(xrEnumerateSwapchainImages(e.swapchain, ni, &ni, (XrSwapchainImageBaseHeader *)imgs.data()));
        for (auto &im : imgs) e.images.push_back(im.image);

        glGenFramebuffers(1, &e.fbo);
        if (I.glFramebufferTexture2DMultisampleEXT && I.glRenderbufferStorageMultisampleEXT)
        {
            glGenRenderbuffers(1, &e.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, e.depth);
            I.glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, I.samples, GL_DEPTH_COMPONENT24, e.w, e.h);
        }
        else
        {
            glGenRenderbuffers(1, &e.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, e.depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, e.w, e.h);
            if (I.glRenderbufferStorageMultisample && I.samples > 1)
            {
                glGenFramebuffers(1, &e.msaa_fbo);
                glGenRenderbuffers(1, &e.msaa_color);
                glGenRenderbuffers(1, &e.msaa_depth);
                glBindRenderbuffer(GL_RENDERBUFFER, e.msaa_color);
                I.glRenderbufferStorageMultisample(GL_RENDERBUFFER, I.samples, (GLenum)fmt, e.w, e.h);
                glBindRenderbuffer(GL_RENDERBUFFER, e.msaa_depth);
                I.glRenderbufferStorageMultisample(GL_RENDERBUFFER, I.samples, GL_DEPTH_COMPONENT24, e.w, e.h);
                glBindFramebuffer(GL_FRAMEBUFFER, e.msaa_fbo);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, e.msaa_color);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, e.msaa_depth);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                {
                    fprintf(stderr, "[StuntCarVR] MSAA framebuffer incomplete, disabling MSAA\n");
                    glDeleteFramebuffers(1, &e.msaa_fbo);
                    e.msaa_fbo = 0;
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
        }
        m_views[i].w = e.w;
        m_views[i].h = e.h;
    }
    I.xr_views.assign(vc, {XR_TYPE_VIEW});
    I.proj_views.assign(vc, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});

    // --- actions ---------------------------------------------------------------------------
    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(asci.actionSetName, "driving");
    strcpy(asci.localizedActionSetName, "Driving");
    XR_CHECK(xrCreateActionSet(I.instance, &asci, &I.action_set));
    I.hand_path[0] = I.path("/user/hand/left");
    I.hand_path[1] = I.path("/user/hand/right");

    auto makeAction = [&](XrAction &a, XrActionType type, const char *name, const char *loc, bool per_hand) -> bool {
        XrActionCreateInfo ac{XR_TYPE_ACTION_CREATE_INFO};
        ac.actionType = type;
        strcpy(ac.actionName, name);
        strcpy(ac.localizedActionName, loc);
        if (per_hand) { ac.countSubactionPaths = 2; ac.subactionPaths = I.hand_path; }
        XR_CHECK(xrCreateAction(I.action_set, &ac, &a));
        return true;
    };
    if (!makeAction(I.a_pose, XR_ACTION_TYPE_POSE_INPUT, "hand_pose", "Hand pose", true)) return false;
    if (!makeAction(I.a_trigger, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger (throttle / brake)", true)) return false;
    if (!makeAction(I.a_grip, XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grip (hold steering wheel)", true)) return false;
    if (!makeAction(I.a_stick, XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Thumbstick (steer / menu)", true)) return false;
    if (!makeAction(I.a_a, XR_ACTION_TYPE_BOOLEAN_INPUT, "boost", "Boost", false)) return false;
    if (!makeAction(I.a_b, XR_ACTION_TYPE_BOOLEAN_INPUT, "turn_around", "Turn around", false)) return false;
    if (!makeAction(I.a_x, XR_ACTION_TYPE_BOOLEAN_INPUT, "view", "Change view", false)) return false;
    if (!makeAction(I.a_y, XR_ACTION_TYPE_BOOLEAN_INPUT, "recenter", "Recentre seat", false)) return false;
    if (!makeAction(I.a_menu, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu / pause", false)) return false;
    if (!makeAction(I.a_haptic, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Vibration", true)) return false;

    struct B { XrAction a; const char *path; };
    auto suggest = [&](const char *profile, std::initializer_list<B> list) {
        std::vector<XrActionSuggestedBinding> sb;
        for (const B &b : list) sb.push_back({b.a, I.path(b.path)});
        XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        s.interactionProfile = I.path(profile);
        s.countSuggestedBindings = (uint32_t)sb.size();
        s.suggestedBindings = sb.data();
        XrResult r = xrSuggestInteractionProfileBindings(I.instance, &s);
        if (XR_FAILED(r)) fprintf(stderr, "[StuntCarVR] bindings for %s rejected (%d)\n", profile, (int)r);
    };
    suggest("/interaction_profiles/oculus/touch_controller",
            {{I.a_pose, "/user/hand/left/input/grip/pose"}, {I.a_pose, "/user/hand/right/input/grip/pose"},
             {I.a_trigger, "/user/hand/left/input/trigger/value"}, {I.a_trigger, "/user/hand/right/input/trigger/value"},
             {I.a_grip, "/user/hand/left/input/squeeze/value"}, {I.a_grip, "/user/hand/right/input/squeeze/value"},
             {I.a_stick, "/user/hand/left/input/thumbstick"}, {I.a_stick, "/user/hand/right/input/thumbstick"},
             {I.a_a, "/user/hand/right/input/a/click"}, {I.a_b, "/user/hand/right/input/b/click"},
             {I.a_x, "/user/hand/left/input/x/click"}, {I.a_y, "/user/hand/left/input/y/click"},
             {I.a_menu, "/user/hand/left/input/menu/click"},
             {I.a_haptic, "/user/hand/left/output/haptic"}, {I.a_haptic, "/user/hand/right/output/haptic"}});
    suggest("/interaction_profiles/valve/index_controller",
            {{I.a_pose, "/user/hand/left/input/grip/pose"}, {I.a_pose, "/user/hand/right/input/grip/pose"},
             {I.a_trigger, "/user/hand/left/input/trigger/value"}, {I.a_trigger, "/user/hand/right/input/trigger/value"},
             {I.a_grip, "/user/hand/left/input/squeeze/value"}, {I.a_grip, "/user/hand/right/input/squeeze/value"},
             {I.a_stick, "/user/hand/left/input/thumbstick"}, {I.a_stick, "/user/hand/right/input/thumbstick"},
             {I.a_a, "/user/hand/right/input/a/click"}, {I.a_b, "/user/hand/right/input/b/click"},
             {I.a_x, "/user/hand/left/input/a/click"}, {I.a_y, "/user/hand/left/input/thumbstick/click"},
             {I.a_menu, "/user/hand/left/input/b/click"},
             {I.a_haptic, "/user/hand/left/output/haptic"}, {I.a_haptic, "/user/hand/right/output/haptic"}});
    suggest("/interaction_profiles/htc/vive_controller",
            {{I.a_pose, "/user/hand/left/input/grip/pose"}, {I.a_pose, "/user/hand/right/input/grip/pose"},
             {I.a_trigger, "/user/hand/left/input/trigger/value"}, {I.a_trigger, "/user/hand/right/input/trigger/value"},
             {I.a_grip, "/user/hand/left/input/squeeze/click"}, {I.a_grip, "/user/hand/right/input/squeeze/click"},
             {I.a_stick, "/user/hand/left/input/trackpad"}, {I.a_stick, "/user/hand/right/input/trackpad"},
             {I.a_a, "/user/hand/right/input/trackpad/click"}, {I.a_y, "/user/hand/left/input/trackpad/click"},
             {I.a_menu, "/user/hand/left/input/menu/click"}, {I.a_b, "/user/hand/right/input/menu/click"},
             {I.a_haptic, "/user/hand/left/output/haptic"}, {I.a_haptic, "/user/hand/right/output/haptic"}});
    suggest("/interaction_profiles/khr/simple_controller",
            {{I.a_pose, "/user/hand/left/input/grip/pose"}, {I.a_pose, "/user/hand/right/input/grip/pose"},
             {I.a_trigger, "/user/hand/left/input/select/click"}, {I.a_trigger, "/user/hand/right/input/select/click"},
             {I.a_menu, "/user/hand/left/input/menu/click"}, {I.a_b, "/user/hand/right/input/menu/click"},
             {I.a_haptic, "/user/hand/left/output/haptic"}, {I.a_haptic, "/user/hand/right/output/haptic"}});

    for (int h = 0; h < 2; h++)
    {
        XrActionSpaceCreateInfo asc{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        asc.action = I.a_pose;
        asc.subactionPath = I.hand_path[h];
        asc.poseInActionSpace.orientation.w = 1.0f;
        XR_CHECK(xrCreateActionSpace(I.session, &asc, &I.hand_space[h]));
    }
    XrSessionActionSetsAttachInfo att{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    att.countActionSets = 1;
    att.actionSets = &I.action_set;
    XR_CHECK(xrAttachSessionActionSets(I.session, &att));

    fprintf(stderr, "[StuntCarVR] OpenXR: %s on %s, %ux%u per eye, %s swapchain, MSAA %dx\n", m_runtime.c_str(),
            m_system.c_str(), (unsigned)I.eyes[0].w, (unsigned)I.eyes[0].h, I.srgb ? "sRGB" : "linear", I.samples);
    return true;
}

void XrApp::shutdown()
{
    if (!m_impl) return;
    Impl &I = *m_impl;
    for (EyeTarget &e : I.eyes)
    {
        if (e.swapchain) xrDestroySwapchain(e.swapchain);
        if (e.fbo) glDeleteFramebuffers(1, &e.fbo);
        if (e.depth) glDeleteRenderbuffers(1, &e.depth);
        if (e.msaa_fbo) glDeleteFramebuffers(1, &e.msaa_fbo);
        if (e.msaa_color) glDeleteRenderbuffers(1, &e.msaa_color);
        if (e.msaa_depth) glDeleteRenderbuffers(1, &e.msaa_depth);
    }
    for (int h = 0; h < 2; h++) if (I.hand_space[h]) xrDestroySpace(I.hand_space[h]);
    if (I.action_set) xrDestroyActionSet(I.action_set);
    if (I.view_space) xrDestroySpace(I.view_space);
    if (I.local) xrDestroySpace(I.local);
    if (I.session) xrDestroySession(I.session);
    if (I.instance) xrDestroyInstance(I.instance);
    delete m_impl;
    m_impl = nullptr;
}

bool XrApp::pollEvents()
{
    Impl &I = *m_impl;
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(I.instance, &ev) == XR_SUCCESS)
    {
        switch (ev.type)
        {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
        {
            auto *e = (XrEventDataSessionStateChanged *)&ev;
            I.state = e->state;
            if (e->state == XR_SESSION_STATE_READY)
            {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(I.session, &bi)))
                {
                    m_session_running = true;
                    m_recenter_pending = true;
                    if (I.ext_refresh_rate)
                    {
                        // Quest: ask for the highest refresh rate up to 90 Hz (72 is the default)
                        PFN_xrEnumerateDisplayRefreshRatesFB enumRates = nullptr;
                        PFN_xrRequestDisplayRefreshRateFB reqRate = nullptr;
                        xrGetInstanceProcAddr(I.instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction *)&enumRates);
                        xrGetInstanceProcAddr(I.instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction *)&reqRate);
                        uint32_t nr = 0;
                        if (enumRates && reqRate && XR_SUCCEEDED(enumRates(I.session, 0, &nr, nullptr)) && nr)
                        {
                            std::vector<float> rates(nr);
                            enumRates(I.session, nr, &nr, rates.data());
                            float best = 0;
                            for (float r : rates) if (r <= 90.5f && r > best) best = r;
                            if (best > 0 && XR_SUCCEEDED(reqRate(I.session, best))) m_refresh = best;
                        }
                    }
                }
            }
            else if (e->state == XR_SESSION_STATE_STOPPING)
            {
                xrEndSession(I.session);
                m_session_running = false;
            }
            else if (e->state == XR_SESSION_STATE_EXITING || e->state == XR_SESSION_STATE_LOSS_PENDING)
            {
                m_session_running = false;
                return false;
            }
            m_focused = (e->state == XR_SESSION_STATE_FOCUSED);
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            return false;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            m_recenter_pending = true;   // the user recentred with the system button
            break;
        default:
            break;
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    return true;
}

bool XrApp::beginFrame()
{
    Impl &I = *m_impl;
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    I.frame_state = {XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame(I.session, &wi, &I.frame_state))) return false;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(I.session, &bi))) return false;
    m_display_period = (double)I.frame_state.predictedDisplayPeriod * 1e-9;
    if (!I.frame_state.shouldRender) return false;

    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = I.frame_state.predictedDisplayTime;
    li.space = I.local;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t n = 0;
    if (XR_FAILED(xrLocateViews(I.session, &li, &vs, (uint32_t)I.xr_views.size(), &n, I.xr_views.data()))) return false;
    if (!(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) || !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        return false;

    // Recentre: seat origin = current head position, yaw only
    if (m_recenter_pending)
    {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(I.view_space, I.local, I.frame_state.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        {
            Pose head = ToPose(loc.pose);
            vec3 f = head.orientation.rotate(vec3(0, 0, -1));
            m_seat_ref = {head.position, quat::axisAngle({0, 1, 0}, std::atan2(-f.x, -f.z))};
            m_recenter_pending = false;
        }
    }

    for (size_t i = 0; i < I.xr_views.size(); i++)
    {
        m_views[i].pose = m_seat_ref.inverse() * ToPose(I.xr_views[i].pose);
        m_views[i].fov[0] = I.xr_views[i].fov.angleLeft;
        m_views[i].fov[1] = I.xr_views[i].fov.angleRight;
        m_views[i].fov[2] = I.xr_views[i].fov.angleUp;
        m_views[i].fov[3] = I.xr_views[i].fov.angleDown;
    }
    return true;
}

Pose XrApp::eyePoseSeat(int i) const { return m_views[i].pose; }

mat4 XrApp::eyeProjection(int i, float zn, float zf) const
{
    const float *f = m_views[i].fov;
    return mat4::perspectiveFov(f[0], f[1], f[2], f[3], zn, zf);
}

Pose XrApp::headPoseSeat() const
{
    if (m_views.size() < 2) return Pose();
    return {(m_views[0].pose.position + m_views[1].pose.position) * 0.5f, m_views[0].pose.orientation};
}

void XrApp::renderView(int i, const std::function<void(int, int, int, bool)> &draw)
{
    Impl &I = *m_impl;
    EyeTarget &e = I.eyes[i];
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(e.swapchain, &ai, &idx))) return;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(e.swapchain, &wi);

    const GLuint color = e.images[idx];
    glBindFramebuffer(GL_FRAMEBUFFER, e.fbo);
    if (I.glFramebufferTexture2DMultisampleEXT)
    {
        I.glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0, I.samples);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, e.depth);
    }
    else
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, e.depth);
        if (e.msaa_fbo) glBindFramebuffer(GL_FRAMEBUFFER, e.msaa_fbo);
    }
    glViewport(0, 0, e.w, e.h);
    // Desktop GL needs GL_FRAMEBUFFER_SRGB to encode into an sRGB swapchain; GLES always does.
    if (I.srgb && !I.gles) glEnable(GL_FRAMEBUFFER_SRGB);

    draw(i, e.w, e.h, I.srgb);

    if (e.msaa_fbo && !I.glFramebufferTexture2DMultisampleEXT)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, e.msaa_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, e.fbo);
        glBlitFramebuffer(0, 0, e.w, e.h, 0, 0, e.w, e.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    if (glInvalidateFramebuffer_opt)
    {
        const GLenum depth_att = GL_DEPTH_ATTACHMENT;
        glBindFramebuffer(GL_FRAMEBUFFER, e.fbo);
        glInvalidateFramebuffer_opt(GL_FRAMEBUFFER, 1, &depth_att);   // don't store depth (tilers)
    }
    if (I.srgb && !I.gles) glDisable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(e.swapchain, &ri);

    XrCompositionLayerProjectionView &pv = I.proj_views[i];
    pv = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    pv.pose = I.xr_views[i].pose;
    pv.fov = I.xr_views[i].fov;
    pv.subImage.swapchain = e.swapchain;
    pv.subImage.imageRect.offset = {0, 0};
    pv.subImage.imageRect.extent = {e.w, e.h};
    pv.subImage.imageArrayIndex = 0;
}

void XrApp::endFrame()
{
    Impl &I = *m_impl;
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = I.local;
    layer.viewCount = (uint32_t)I.proj_views.size();
    layer.views = I.proj_views.data();
    const XrCompositionLayerBaseHeader *layers[1] = {(const XrCompositionLayerBaseHeader *)&layer};

    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = I.frame_state.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool have_layer = I.frame_state.shouldRender && I.proj_views[0].subImage.swapchain != XR_NULL_HANDLE;
    ei.layerCount = have_layer ? 1 : 0;
    ei.layers = have_layer ? layers : nullptr;
    xrEndFrame(I.session, &ei);
    for (auto &pv : I.proj_views) pv.subImage.swapchain = XR_NULL_HANDLE;
}

void XrApp::syncInput(XrInputs &o)
{
    Impl &I = *m_impl;
    o = XrInputs();
    if (!m_session_running) return;
    XrActiveActionSet aas{I.action_set, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &aas;
    if (XR_FAILED(xrSyncActions(I.session, &si))) return;   // e.g. not focused

    for (int h = 0; h < 2; h++)
    {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.subactionPath = I.hand_path[h];
        XrActionStateFloat fs{XR_TYPE_ACTION_STATE_FLOAT};
        gi.action = I.a_trigger;
        if (XR_SUCCEEDED(xrGetActionStateFloat(I.session, &gi, &fs)) && fs.isActive) { o.trigger[h] = fs.currentState; o.active = true; }
        fs = {XR_TYPE_ACTION_STATE_FLOAT};
        gi.action = I.a_grip;
        if (XR_SUCCEEDED(xrGetActionStateFloat(I.session, &gi, &fs)) && fs.isActive) o.grip[h] = fs.currentState;
        XrActionStateVector2f vs{XR_TYPE_ACTION_STATE_VECTOR2F};
        gi.action = I.a_stick;
        if (XR_SUCCEEDED(xrGetActionStateVector2f(I.session, &gi, &vs)) && vs.isActive) { o.stick_x[h] = vs.currentState.x; o.stick_y[h] = vs.currentState.y; }

        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(I.hand_space[h], I.local, I.frame_state.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        {
            o.hand_pose[h] = m_seat_ref.inverse() * ToPose(loc.pose);
            o.hand_valid[h] = true;
        }
    }

    auto getBool = [&](XrAction a) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a;
        XrActionStateBoolean bs{XR_TYPE_ACTION_STATE_BOOLEAN};
        return XR_SUCCEEDED(xrGetActionStateBoolean(I.session, &gi, &bs)) && bs.isActive && bs.currentState;
    };
    o.a = getBool(I.a_a); o.b = getBool(I.a_b); o.x = getBool(I.a_x); o.y = getBool(I.a_y); o.menu = getBool(I.a_menu);
    o.a_pressed = o.a && !I.prev_a; o.b_pressed = o.b && !I.prev_b; o.x_pressed = o.x && !I.prev_x;
    o.y_pressed = o.y && !I.prev_y; o.menu_pressed = o.menu && !I.prev_menu;
    I.prev_a = o.a; I.prev_b = o.b; I.prev_x = o.x; I.prev_y = o.y; I.prev_menu = o.menu;

    XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
    if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(I.session, I.hand_path[1], &ps)) && ps.interactionProfile != XR_NULL_PATH)
    {
        char buf[XR_MAX_PATH_LENGTH];
        uint32_t len = 0;
        if (XR_SUCCEEDED(xrPathToString(I.instance, ps.interactionProfile, sizeof(buf), &len, buf))) o.profile = buf;
    }
}

void XrApp::vibrate(Hand h, float amplitude, float seconds, float frequency)
{
    Impl &I = *m_impl;
    if (!m_session_running || amplitude <= 0.0f) return;
    XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
    v.amplitude = std::min(1.0f, amplitude);
    v.duration = (XrDuration)(seconds * 1e9);
    v.frequency = frequency > 0.0f ? frequency : XR_FREQUENCY_UNSPECIFIED;
    XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
    hi.action = I.a_haptic;
    hi.subactionPath = I.hand_path[h];
    xrApplyHapticFeedback(I.session, &hi, (const XrHapticBaseHeader *)&v);
}

void XrApp::recenter() { m_recenter_pending = true; }

} // namespace scr
