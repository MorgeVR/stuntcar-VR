// main.cpp - Stunt Car Racer VR application (SDL3 + OpenGL/GLES + OpenXR).
//
//   StuntCarVR [--vr | --flat] [--autotest SECONDS] [--screenshot FILE]
//
// Default mode comes from settings.ini ("auto": VR if an OpenXR runtime with a headset is
// available, otherwise a normal window). On Meta Quest the app always runs in VR.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__ANDROID__)
#include <jni.h>
#include <EGL/egl.h>
#else
#include <EGL/egl.h>
#endif

#include "game.h"
#include "renderer.h"
#include "scene_setup.h"
#include "settings.h"
#include "input_sdl.h"
#include "ffb.h"
#include "vr_controls.h"
#include "xr_app.h"
#include "backdrop.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace scr;
using namespace vm;

namespace scr { extern bool g_ffb_invert; }

static void *SdlGetProc(const char *name) { return (void *)SDL_GL_GetProcAddress(name); }

static void AudioCallback(void *, SDL_AudioStream *stream, int additional, int)
{
    static std::vector<float> buf;
    int frames = additional / (int)(2 * sizeof(float));
    if (frames <= 0) return;
    buf.resize((size_t)frames * 2);
    ScrMixer::Get().Mix(buf.data(), frames);
    SDL_PutAudioStreamData(stream, buf.data(), frames * 2 * (int)sizeof(float));
}

static void SaveScreenshot(const char *path, int w, int h)
{
    std::vector<unsigned char> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
    fclose(f);
    fprintf(stderr, "[StuntCarVR] screenshot saved to %s\n", path);
}

struct App
{
    Settings settings;
    std::string settings_path;
    SDL_Window *window = nullptr;
    SDL_GLContext gl = nullptr;
    SDL_AudioStream *audio = nullptr;
    bool gles = false;

    Game game;
    Space space;
    Renderer renderer;
    InputSystem input;
    FfbModel ffb;
    VrWheel vr_wheel;
    ChaseCamera chase;
    XrApp xr;
    bool vr = false;

    TextPanel dash, menu_panel, overlay, status_panel;
    int highlight = 0;
    bool flat_cockpit = false;
    bool auto_paused = false;
    float vignette = 0.0f;

    // autotest
    double autotest_seconds = 0.0, run_time = 0.0;
    std::string screenshot_path;

    bool init(int argc, char **argv);
    void run();
    void shutdown();
    bool initXr();
    void handleActions(InputFrame &f);
    void renderFlat(int w, int h);
    void renderVr(bool should_render, const XrInputs &in);
    void drawStatus(int w, int h);
};

bool App::init(int argc, char **argv)
{
    std::string mode_override;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--vr")) mode_override = "vr";
        else if (!strcmp(argv[i], "--flat")) mode_override = "flat";
        else if (!strcmp(argv[i], "--autotest") && i + 1 < argc) autotest_seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshot_path = argv[++i];
    }

#if defined(__ANDROID__)
    // Quest: keep running while the activity is paused (system menu); OpenXR tells us when to idle
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC))
    {
        // Haptic / audio subsystems can be missing; retry with the essentials.
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK))
        {
            fprintf(stderr, "[StuntCarVR] SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
    }

    // Settings
    if (char *pref = SDL_GetPrefPath("StuntCarVR", "StuntCarVR"))
    {
        settings_path = std::string(pref) + "settings.ini";
#if defined(_WIN32) || defined(__ANDROID__)
        // no console on Windows GUI apps / Quest: keep a log next to the settings
        freopen((std::string(pref) + "log.txt").c_str(), "w", stderr);
        setvbuf(stderr, nullptr, _IONBF, 0);
#endif
        SDL_free(pref);
        if (!settings.load(settings_path)) settings.save(settings_path);   // write defaults for the user to edit
    }
    if (!mode_override.empty()) settings.mode = mode_override;
#if defined(__ANDROID__)
    settings.mode = "vr";
#endif
    g_ffb_invert = settings.ffb_invert;
    flat_cockpit = settings.flat_cockpit;
    space.metres_per_unit = settings.metres_per_unit;
    vr_wheel.lock_deg = settings.vr_wheel_lock_deg;
    chase.distance_m = settings.chase_distance_m;
    chase.height_m = settings.chase_height_m;

    // OpenGL ES on Android (Quest); desktop GL 3.3 core elsewhere (override: SCR_GLES=1)
#if defined(__ANDROID__)
    gles = true;
#else
    gles = SDL_getenv("SCR_GLES") != nullptr;
#endif
#if !defined(_WIN32) && !defined(__ANDROID__)
    // Linux OpenXR (Monado) uses EGL
    SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");
#endif
    if (gles)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    }
    else
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#if !defined(__ANDROID__)
    // MSAA for the desktop window (VR eyes have their own MSAA targets)
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
#endif

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window = SDL_CreateWindow("Stunt Car Racer VR", settings.window_w, settings.window_h, flags);
    if (!window)
    {
        // retry without multisampling
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow("Stunt Car Racer VR", settings.window_w, settings.window_h, flags);
    }
    if (!window) { fprintf(stderr, "[StuntCarVR] window: %s\n", SDL_GetError()); return false; }
    gl = SDL_GL_CreateContext(window);
    if (!gl) { fprintf(stderr, "[StuntCarVR] GL context: %s\n", SDL_GetError()); return false; }
    SDL_GL_MakeCurrent(window, gl);

    const char *missing = nullptr;
    if (!scrgl::Load(SdlGetProc, gles, &missing))
    {
        fprintf(stderr, "[StuntCarVR] missing GL function %s\n", missing ? missing : "?");
        return false;
    }
    fprintf(stderr, "[StuntCarVR] GL %s (%s)\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));

    // Audio
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 48000};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioCallback, nullptr);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else fprintf(stderr, "[StuntCarVR] no audio: %s\n", SDL_GetError());

    // Game + renderer
    if (!game.init(48000))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Stunt Car Racer VR",
                                 "Could not load the game data (Tracks, Sounds, Bitmap). Make sure the 'assets' folder is next to the program.", window);
        return false;
    }
    game.setSimHz(settings.sim_hz);
    if (!renderer.init(space)) return false;
    input.init(settings.input);

    if (settings.mode != "flat")
    {
        vr = initXr();
        if (!vr)
        {
            if (settings.mode == "vr")
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Stunt Car Racer VR",
                                         "Could not start VR (no OpenXR runtime or headset). Starting on the flat screen.", window);
            fprintf(stderr, "[StuntCarVR] running on the flat screen\n");
        }
    }
    // In VR the headset paces the frame loop; on the flat screen use vsync
    SDL_GL_SetSwapInterval(vr ? 0 : 1);
    highlight = game.selectedTrack() >= 0 ? game.selectedTrack() : 0;
    return true;
}

bool App::initXr()
{
    XrGraphicsHandles h;
    h.gles = gles;
#if defined(_WIN32)
    h.hdc = wglGetCurrentDC();
    h.hglrc = wglGetCurrentContext();
#elif defined(__ANDROID__)
    h.egl_display = eglGetCurrentDisplay();
    h.egl_context = eglGetCurrentContext();
    h.egl_config = (void *)SDL_EGL_GetCurrentConfig();
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    JavaVM *vm = nullptr;
    env->GetJavaVM(&vm);
    h.java_vm = vm;
    h.activity = env->NewGlobalRef((jobject)SDL_GetAndroidActivity());
#else
    h.egl_display = eglGetCurrentDisplay();
    h.egl_context = eglGetCurrentContext();
    h.egl_config = (void *)SDL_EGL_GetCurrentConfig();
    h.egl_get_proc_address = (void *(*)(const char *))SDL_EGL_GetProcAddress;
    if (!h.egl_display || !h.egl_context) { fprintf(stderr, "[StuntCarVR] VR on Linux needs an EGL context\n"); return false; }
#endif
    if (!xr.init(h, "Stunt Car Racer VR"))
    {
        xr.shutdown();
        return false;
    }
    return true;
}

void App::handleActions(InputFrame &f)
{
    const Mode mode = game.mode();
    if (f.next_scenery) NextSceneryType();
    if (f.toggle_cockpit) flat_cockpit = !flat_cockpit;
    if (f.toggle_outside) game.setOutsideView(!game.outsideView());
    if (f.toggle_pace)
    {
        game.setSimHz(game.simHz() < 10.0 ? kRemakeSimHz : kAmigaSimHz);
        fprintf(stderr, "[StuntCarVR] physics rate %.2f Hz (%s pace)\n", game.simHz(), game.simHz() < 10.0 ? "Amiga" : "remake");
    }
    if (f.toggle_comfort)
    {
        settings.comfort.horizon_lock = settings.comfort.horizon_lock > 0.5f ? 0.0f : 1.0f;
        fprintf(stderr, "[StuntCarVR] horizon lock %s\n", settings.comfort.horizon_lock > 0.5f ? "on" : "off");
    }
    if (f.recenter && vr) xr.recenter();
    if (f.pause) game.setPaused(!game.paused());
    if (f.unpause) game.setPaused(false);

    switch (mode)
    {
    case Mode::TrackMenu:
        if (f.track_hotkey >= 0) { highlight = f.track_hotkey; game.selectTrack(highlight); }
        if (f.up) { highlight = (highlight + Game::numTracks() - 1) % Game::numTracks(); game.selectTrack(highlight); }
        if (f.down) { highlight = (highlight + 1) % Game::numTracks(); game.selectTrack(highlight); }
        if (f.select) game.startPreview();
        break;
    case Mode::TrackPreview:
        if (f.select) { game.startRace(); if (vr) xr.recenter(); }
        if (f.back) game.backToMenu();
        break;
    case Mode::Racing:
    case Mode::GameOver:
        // paused: B / R goes back to the track menu (the VR way out of a race)
        if (game.paused() && f.turn_around) { game.setPaused(false); game.backToMenu(); break; }
        if (f.turn_around) game.turnAround();
        if (f.back) { game.setPaused(false); game.backToMenu(); }
        break;
    }
}

void App::drawStatus(int w, int h)
{
    if (status_panel.width() == 0) status_panel.create(1024, 128);
    status_panel.clear(0);
    std::string s = input.status();
    if (vr) s = "VR: " + xr.systemName() + " (" + xr.runtimeName() + ")" + (s.empty() ? "" : "   ") + s;
    if (!s.empty())
    {
        status_panel.fillRect(0, 0, 1024, 28, (uint32_t)0xB0000000);
        status_panel.text(8, 6, s.substr(0, 126), 2, 0xFFFFFFFF);
    }
    status_panel.upload();
    renderer.drawPanel2D(status_panel, 0, h - h / 12, w, h / 12, w, h, false);
}

void App::renderFlat(int w, int h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    vec3 sky = renderer.skyColour(false);
    glClearColor(sky.x, sky.y, sky.z, 1.0f);
    scrgl::ClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Mode mode = game.mode();
    const bool racing = mode == Mode::Racing || mode == Mode::GameOver;
    View v = FlatView(game, space, w, h, flat_cockpit, flat_cockpit ? 75.0f : 60.0f);
    if (racing && game.outsideView())
        v.view = chase.pose().inverse().matrix();
    SceneParams sp = BuildScene(game, space, false, flat_cockpit, Pose());
    if (racing && flat_cockpit) { DrawDashboard(dash, game); sp.dashboard = &dash; }
    renderer.drawScene(v, sp);

    if (!racing)
    {
        DrawMenuPanel(menu_panel, game, highlight, false);
        int pw = std::min(w - 40, 900), ph = pw * 10 / 16;
        renderer.drawPanel2D(menu_panel, 20, 20, pw, ph, w, h, false);
    }
    else
    {
        if (!flat_cockpit)
        {
            DrawDashboard(dash, game);
            int dw = std::min(w / 2, 640), dh = dw * 160 / 512;
            renderer.drawPanel2D(dash, (w - dw) / 2, h - dh - 10, dw, dh, w, h, false);
        }
        DrawRaceOverlay(overlay, game, false);
        renderer.drawPanel2D(overlay, 0, h / 6, w, w / 4, w, h, false);
    }
    drawStatus(w, h);
}

void App::renderVr(bool should_render, const XrInputs &in)
{
    if (!should_render) { xr.endFrame(); return; }
    const Mode mode = game.mode();
    const bool racing = mode == Mode::Racing || mode == Mode::GameOver;
    const bool third_person = racing && game.outsideView();
    const Pose seat = !racing ? Pose() : third_person ? chase.pose() : SeatWorldPose(game, space, settings.comfort);
    const Pose anchor = DefaultDioramaAnchor();

    if (racing) DrawDashboard(dash, game);
    else DrawMenuPanel(menu_panel, game, highlight, true);
    DrawRaceOverlay(overlay, game, true);

    for (int i = 0; i < xr.viewCount(); i++)
    {
        xr.renderView(i, [&](int eye, int w, int h, bool srgb) {
            (void)w; (void)h;
            vec3 sky = renderer.skyColour(srgb);
            glClearColor(sky.x, sky.y, sky.z, 1.0f);
            scrgl::ClearDepth(1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            View v;
            Pose eye_world = seat * xr.eyePoseSeat(eye);
            v.view = eye_world.inverse().matrix();
            v.proj = xr.eyeProjection(eye, 0.03f, 6000.0f);

            SceneParams sp = BuildScene(game, space, true, true, anchor);
            sp.linear_output = srgb;
            sp.dashboard = racing ? &dash : nullptr;
            renderer.drawScene(v, sp);

            if (!racing)
            {
                SceneParams env;
                env.game_to_world = MenuEnvironmentTransform(space);
                env.draw_track = env.draw_opponent = env.draw_shadow = false;
                env.linear_output = srgb;
                renderer.drawScene(v, env);
                renderer.drawPanel(v, menu_panel, Pose{vec3(0.0f, 0.15f, -1.9f), quat()}, 1.1f, 0.69f, srgb);
            }
            // third person: speed / boost readout floats low in front of you
            if (third_person)
                renderer.drawPanel(v, dash, seat * Pose{vec3(0.0f, -0.75f, -1.2f), quat::axisAngle({1, 0, 0}, -0.6f)}, 0.6f, 0.19f, srgb);
            // race messages float ahead of the car
            renderer.drawPanel(v, overlay, seat * Pose{vec3(0.0f, 0.35f, -2.5f), quat()}, 2.0f, 0.5f, srgb);

            Pose hands[2] = {seat * in.hand_pose[0], seat * in.hand_pose[1]};
            bool grip[2] = {in.grip[0] > 0.55f, in.grip[1] > 0.55f};
            renderer.drawControllers(v, hands, in.hand_valid, grip, srgb);
            renderer.drawVignette(vignette);
        });
    }
    xr.endFrame();
}

void App::run()
{
    uint64_t last = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();
    bool running = true;
    XrInputs xr_in;

    // autotest: drive the menu -> preview -> race flow automatically
    double next_step = 1.0;
    int step = 0;

    while (running)
    {
        uint64_t now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freq;
        last = now;
        if (autotest_seconds > 0.0) dt = 1.0 / 90.0;   // deterministic
        run_time += dt;

        InputFrame f;
        const bool racing = game.mode() == Mode::Racing;
        SDL_Event e;
        while (SDL_PollEvent(&e)) input.handleEvent(e, racing, f);
        input.poll(racing, f);

        bool should_render = false;
        if (vr)
        {
            if (!xr.pollEvents()) running = false;
            // pause automatically when the headset is taken off / the system menu is open
            if (racing && !xr.focused() && !game.paused()) { game.setPaused(true); auto_paused = true; }
            if (xr.focused() && auto_paused) { game.setPaused(false); auto_paused = false; }
            if (xr.sessionRunning())
            {
                // xrWaitFrame paces the loop; then read controllers for this frame
                should_render = xr.beginFrame();
                xr.syncInput(xr_in);
                vr_wheel.update(xr_in, racing, (float)dt, f);
            }
        }

        if (autotest_seconds > 0.0)
        {
            if (run_time >= next_step)
            {
                if (step == 0) { f.track_hotkey = 3; }
                else if (step == 1) f.select = true;   // preview
                else if (step == 2) f.select = true;   // race
                step++;
                next_step += 1.0;
            }
            if (game.mode() == Mode::Racing) f.keys |= kKeyAccel;
            if (run_time >= autotest_seconds) running = false;
        }

        if (f.quit) running = false;
        handleActions(f);

        game.setDigitalInput(f.keys);
        game.setAnalogInput(f.analog);
        game.update(dt);
        renderer.sync();

        // Cockpit wheel animation: physical wheel angle, else the analog / digital steering
        float wheel_rad = 0.0f;
        if (f.vr_steering) wheel_rad = vr_wheel.angle;
        else if (f.has_wheel) wheel_rad = f.wheel_angle_deg * 3.14159265f / 180.0f;
        else if (f.analog.active) wheel_rad = f.analog.steer * 1.57f;
        else if (f.keys & kKeyLeft) wheel_rad = -1.57f;
        else if (f.keys & kKeyRight) wheel_rad = 1.57f;
        renderer.setSteeringWheelAngle(wheel_rad);

        // Force feedback / rumble
        FfbOutput fo = ffb.update(game, f.analog.steer, (float)dt, settings.ffb);
        input.applyForceFeedback(fo, f.analog.steer, (float)dt);
        vignette = ComfortVignette(game, settings.comfort, (float)dt);
        if ((game.mode() == Mode::Racing || game.mode() == Mode::GameOver) && game.outsideView())
            chase.update(game, space, (float)dt, !vr);
        else
            chase.reset();

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        if (vr && xr.sessionRunning())
        {
            renderVr(should_render, xr_in);
            // controller haptics from the same effects as the wheel
            if (fo.rumble_low > 0.05f || fo.rumble_high > 0.05f)
            {
                float amp = std::max(fo.rumble_low, fo.rumble_high);
                xr.vibrate(kLeft, amp, 0.03f, fo.rumble_high > fo.rumble_low ? 160.0f : 60.0f);
                xr.vibrate(kRight, amp, 0.03f, fo.rumble_high > fo.rumble_low ? 160.0f : 60.0f);
            }
            if (settings.mirror) renderFlat(w, h);
            else
            {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, w, h);
                glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                drawStatus(w, h);
            }
            SDL_GL_SwapWindow(window);
        }
        else if (vr)
        {
            // waiting for the session to start (headset idle / not worn)
            SDL_Delay(10);
        }
        else
        {
            renderFlat(w, h);
            if (!running && !screenshot_path.empty()) SaveScreenshot(screenshot_path.c_str(), w, h);
            SDL_GL_SwapWindow(window);
        }
    }
}

void App::shutdown()
{
    if (!settings_path.empty())
    {
        settings.flat_cockpit = flat_cockpit;
        settings.sim_hz = game.simHz();
        settings.save(settings_path);
    }
    if (vr) xr.shutdown();
    input.shutdown();
    renderer.shutdown();
    game.shutdown();
    if (audio) SDL_DestroyAudioStream(audio);
    if (gl) SDL_GL_DestroyContext(gl);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char **argv)
{
    App app;
    int rc = 0;
    if (app.init(argc, argv)) app.run();
    else rc = 1;
    app.shutdown();
    return rc;
}
