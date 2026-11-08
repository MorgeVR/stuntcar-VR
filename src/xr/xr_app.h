// xr_app.h - OpenXR session for Stunt Car Racer VR (PCVR and Meta Quest).
//
// One code path for:
//   * Windows  - XR_KHR_opengl_enable   + Win32 binding (SteamVR, Meta Link / Air Link, WMR, Pimax, ...)
//   * Android  - XR_KHR_opengl_es_enable + Android binding (Meta Quest standalone)
//   * Linux    - XR_KHR_opengl_enable   + XR_MNDX_egl_enable (Monado)
// The platform layer creates the GL context (SDL3) and passes the native handles in.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "vmath.h"
#include "renderer.h"

namespace scr {

struct XrGraphicsHandles
{
#if defined(_WIN32)
    void *hdc = nullptr;      // HDC
    void *hglrc = nullptr;    // HGLRC
#elif defined(__ANDROID__)
    void *egl_display = nullptr, *egl_config = nullptr, *egl_context = nullptr;
    void *java_vm = nullptr;  // JavaVM*
    void *activity = nullptr; // jobject (global ref)
#else
    void *egl_display = nullptr, *egl_config = nullptr, *egl_context = nullptr;
    void *(*egl_get_proc_address)(const char *) = nullptr;
#endif
    bool gles = false;
};

enum Hand { kLeft = 0, kRight = 1 };

// Controller state for this frame (poses are in the recentred seat space, metres)
struct XrInputs
{
    bool active = false;                 // an interaction profile is bound
    vm::Pose hand_pose[2];
    bool hand_valid[2] = {false, false};
    float trigger[2] = {0, 0};
    float grip[2] = {0, 0};
    float stick_x[2] = {0, 0}, stick_y[2] = {0, 0};
    bool a = false, b = false, x = false, y = false, menu = false;   // current state
    bool a_pressed = false, b_pressed = false, x_pressed = false, y_pressed = false, menu_pressed = false;   // edges
    std::string profile;
};

class XrApp
{
public:
    bool init(const XrGraphicsHandles &gfx, const char *app_name);
    void shutdown();

    // Process OpenXR events. Returns false when the app should exit.
    bool pollEvents();
    bool sessionRunning() const { return m_session_running; }
    bool focused() const { return m_focused; }

    // Frame loop: beginFrame() -> renderView() for each eye -> endFrame().
    // beginFrame returns false if this frame should not be rendered (still call endFrame).
    bool beginFrame();
    int viewCount() const { return (int)m_views.size(); }
    // Pose of eye i in the recentred seat space, and its projection.
    vm::Pose eyePoseSeat(int i) const;
    vm::mat4 eyeProjection(int i, float znear, float zfar) const;
    vm::Pose headPoseSeat() const;
    // Bind eye i's framebuffer and viewport; calls draw; resolves and releases the image.
    void renderView(int i, const std::function<void(int eye, int w, int h, bool srgb)> &draw);
    void endFrame();
    double predictedDisplayPeriod() const { return m_display_period; }

    // Input / haptics
    void syncInput(XrInputs &out);
    void vibrate(Hand h, float amplitude, float seconds, float frequency_hz = 0.0f);
    void recenter();   // make the current head position / yaw the seat origin

    const std::string &runtimeName() const { return m_runtime; }
    const std::string &systemName() const { return m_system; }
    float refreshRate() const { return m_refresh; }

private:
    struct Impl;
    Impl *m_impl = nullptr;
    struct ViewInfo { vm::Pose pose; float fov[4]; int w = 0, h = 0; };
    std::vector<ViewInfo> m_views;
    vm::Pose m_seat_ref;            // head pose in LOCAL space at recentre (yaw only)
    bool m_recenter_pending = true;
    bool m_session_running = false, m_focused = false;
    double m_display_period = 1.0 / 72.0;
    float m_refresh = 0.0f;
    std::string m_runtime, m_system;
};

} // namespace scr
