// render_test.cpp - offscreen rendering test (headless EGL, e.g. Mesa llvmpipe).
// Renders the main views of the game to PPM images and checks the face culling rule.
//   render_test [gl|gles] [output_dir]

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gl_api.h"
#include "renderer.h"
#include "scene_setup.h"
#include "game.h"
#include "backdrop.h"

using namespace scr;
using namespace vm;

static PFNEGLGETPROCADDRESSPROC p_eglGetProcAddress;
static void *GetProc(const char *n) { return (void *)p_eglGetProcAddress(n); }

static const int W = 960, H = 540;
static GLuint g_fbo, g_col, g_dep;
static std::string g_dir = ".";

static void SavePPM(const std::string &name, int w, int h)
{
    std::vector<unsigned char> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::string path = g_dir + "/" + name + ".ppm";
    FILE *f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
    fclose(f);
    printf("  wrote %s\n", path.c_str());
}

static void Begin(const Renderer &r)
{
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, W, H);
    vec3 s = r.skyColour(false);
    glClearColor(s.x, s.y, s.z, 1);
    scrgl::ClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void Run(Game &g, double seconds, uint32_t keys = 0)
{
    g.setDigitalInput(keys);
    for (double t = 0; t < seconds; t += 1.0 / 90.0) g.update(1.0 / 90.0);
}

int main(int argc, char **argv)
{
    bool gles = argc > 1 && strcmp(argv[1], "gles") == 0;
    if (argc > 2) g_dir = argv[2];

    void *lib = dlopen("libEGL.so.1", RTLD_NOW);
    if (!lib) { printf("no libEGL\n"); return 1; }
    p_eglGetProcAddress = (PFNEGLGETPROCADDRESSPROC)dlsym(lib, "eglGetProcAddress");
#define EGLF(T, n) T n = (T)dlsym(lib, #n)
    EGLF(PFNEGLINITIALIZEPROC, eglInitialize);
    EGLF(PFNEGLCHOOSECONFIGPROC, eglChooseConfig);
    EGLF(PFNEGLBINDAPIPROC, eglBindAPI);
    EGLF(PFNEGLCREATECONTEXTPROC, eglCreateContext);
    EGLF(PFNEGLMAKECURRENTPROC, eglMakeCurrent);
    auto eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)p_eglGetProcAddress("eglGetPlatformDisplayEXT");

    EGLDisplay dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min)) { printf("eglInitialize failed\n"); return 1; }
    EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, gles ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n);
    eglBindAPI(gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
    EGLint ctx_es[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLint ctx_gl[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                       EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, n ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, gles ? ctx_es : ctx_gl);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { printf("context failed\n"); return 1; }

    const char *missing = nullptr;
    if (!scrgl::Load(GetProc, gles, &missing)) { printf("GL load failed: %s\n", missing); return 1; }
    printf("GL: %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    glGenFramebuffers(1, &g_fbo);
    glGenTextures(1, &g_col);
    glBindTexture(GL_TEXTURE_2D, g_col);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W * 2, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenRenderbuffers(1, &g_dep);
    glBindRenderbuffer(GL_RENDERBUFFER, g_dep);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W * 2, H);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_col, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_dep);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("fbo incomplete\n"); return 1; }

    Game game;
    if (!game.init(48000)) return 1;
    Space space;
    Renderer r;
    if (!r.init(space)) { printf("renderer init failed\n"); return 1; }
    TextPanel dash, menu, overlay;
    int failures = 0;
    const std::string pre = gles ? "gles_" : "gl_";

    // ---- culling rule: look straight down on the start of Little Ramp ---------------------
    {
        game.selectTrack(0);
        r.sync();
        int counts[2];
        for (int cw = 0; cw < 2; cw++)
        {
            r.setFrontFaceCW(cw == 1);
            Begin(r);
            View v;
            v.proj = mat4::perspective(1.0f, (float)W / H, 1.0f, 4000.0f);
            // world: track centre at (512, 0, -512) m; look down from 900 m
            Pose cam{vec3(512, 900, -512), quat::axisAngle({1, 0, 0}, -kPi / 2)};
            v.view = cam.inverse().matrix();
            SceneParams p; p.game_to_world = space.gameToWorld();
            p.draw_ground = p.draw_scenery = false;
            r.drawScene(v, p);
            std::vector<unsigned char> px((size_t)W * H * 4);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            vec3 s = r.skyColour(false);
            int c = 0;
            for (size_t i = 0; i < px.size(); i += 4)
                if (std::abs(px[i] - (int)(s.x * 255)) + std::abs(px[i + 1] - (int)(s.y * 255)) + std::abs(px[i + 2] - (int)(s.z * 255)) > 30) c++;
            counts[cw] = c;
            if (cw == 1) SavePPM(pre + "00_top_down", W, H);
        }
        printf("[cull] track pixels seen from above: front=CCW %d, front=CW %d\n", counts[0], counts[1]);
        if (!(counts[1] > counts[0] * 2)) { printf("  FAIL: expected road tops visible with CW front faces\n"); failures++; }
        r.setFrontFaceCW(true);
    }

    // ---- flat views ----------------------------------------------------------------------
    game.selectTrack(3);   // Big Ramp
    r.sync();
    Run(game, 2.0);
    Begin(r); r.drawScene(FlatView(game, space, W, H, false), BuildScene(game, space, false, false, Pose()));
    DrawMenuPanel(menu, game, 3, false);
    r.drawPanel2D(menu, 40, 40, 512, 320, W, H, false);
    SavePPM(pre + "01_menu", W, H);

    game.startPreview();
    Run(game, 4.0);
    r.sync();
    Begin(r); r.drawScene(FlatView(game, space, W, H, false), BuildScene(game, space, false, false, Pose()));
    SavePPM(pre + "02_preview", W, H);

    game.startRace();
    Run(game, 7.0, kKeyAccel);
    r.sync();
    Begin(r); r.drawScene(FlatView(game, space, W, H, false), BuildScene(game, space, false, false, Pose()));
    DrawRaceOverlay(overlay, game, false);
    r.drawPanel2D(overlay, 0, 0, W, W / 4, W, H, false);
    SavePPM(pre + "03_race_classic", W, H);

    Begin(r);
    {
        SceneParams sp = BuildScene(game, space, false, true, Pose());
        DrawDashboard(dash, game);
        sp.dashboard = &dash;
        r.setSteeringWheelAngle(0.5f);
        r.drawScene(FlatView(game, space, W, H, true, 75.0f), sp);
    }
    SavePPM(pre + "04_race_cockpit", W, H);

    game.setOutsideView(true);
    Run(game, 0.3, kKeyAccel);
    Begin(r); r.drawScene(FlatView(game, space, W, H, false), BuildScene(game, space, false, false, Pose()));
    SavePPM(pre + "05_race_outside", W, H);
    // ---- third-person chase camera (flat: looks at the car; VR: level) ----------------------
    {
        ChaseCamera chase;
        for (int i = 0; i < 180; i++) { game.update(1.0 / 90.0); chase.update(game, space, 1.0f / 90.0f, true); }
        Begin(r);
        View v = FlatView(game, space, W, H, false);
        v.view = chase.pose().inverse().matrix();
        r.drawScene(v, BuildScene(game, space, false, false, Pose()));
        SavePPM(pre + "05b_chase_flat", W, H);

        ChaseCamera vrc;
        for (int i = 0; i < 90; i++) { game.update(1.0 / 90.0); vrc.update(game, space, 1.0f / 90.0f, false); }
        Pose seat = vrc.pose();
        Pose head{vec3(0, 0, 0), quat::axisAngle({1, 0, 0}, -0.3f)};   // looking down a little
        Begin(r);
        View vv;
        vv.view = (seat * head).inverse().matrix();
        vv.proj = mat4::perspectiveFov(-0.9f, 0.9f, 0.75f, -0.85f, 0.03f, 6000.0f);
        DrawDashboard(dash, game);
        r.drawScene(vv, BuildScene(game, space, true, true, Pose()));
        r.drawPanel(vv, dash, seat * Pose{vec3(0.0f, -0.75f, -1.2f), quat::axisAngle({1, 0, 0}, -0.6f)}, 0.6f, 0.19f, false);
        SavePPM(pre + "05c_chase_vr", W, H);
    }
    game.setOutsideView(false);

    // ---- VR style stereo pair from the seat (head looking slightly left and down) ----------
    {
        ComfortSettings comfort;
        Pose seat = SeatWorldPose(game, space, comfort);
        Pose head{vec3(0, 0, 0), normalize(quat::axisAngle({0, 1, 0}, 0.25f) * quat::axisAngle({1, 0, 0}, -0.2f))};
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        vec3 s = r.skyColour(false);
        glClearColor(s.x, s.y, s.z, 1); scrgl::ClearDepth(1.0f);
        glViewport(0, 0, W * 2, H);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (int eye = 0; eye < 2; eye++)
        {
            glViewport(eye * W, 0, W, H);
            Pose eye_local{head.orientation.rotate(vec3(eye == 0 ? -0.032f : 0.032f, 0, 0)), head.orientation};
            Pose eye_world = seat * eye_local;
            View v;
            v.view = eye_world.inverse().matrix();
            // Quest 2-like asymmetric FOV (radians)
            v.proj = eye == 0 ? mat4::perspectiveFov(-0.942f, 0.698f, 0.698f, -0.768f, 0.05f, 6000.0f)
                              : mat4::perspectiveFov(-0.698f, 0.942f, 0.698f, -0.768f, 0.05f, 6000.0f);
            SceneParams sp = BuildScene(game, space, true, true, Pose());
            sp.dashboard = &dash;
            r.drawScene(v, sp);
            r.drawVignette(0.5f);
        }
        SavePPM(pre + "06_vr_stereo_cockpit", W * 2, H);
    }

    // ---- VR menu: tabletop diorama with the menu panel ---------------------------------------
    {
        game.backToMenu();
        game.selectTrack(7);   // Roller Coaster
        game.startPreview();
        Run(game, 3.0);
        r.sync();
        Pose anchor = DefaultDioramaAnchor();
        Begin(r);
        View v;
        Pose head{vec3(0, 0, 0), quat::axisAngle({1, 0, 0}, -0.45f)};
        v.view = head.inverse().matrix();
        v.proj = mat4::perspectiveFov(-0.85f, 0.85f, 0.7f, -0.8f, 0.05f, 6000.0f);
        Space floor_space = space;
        SceneParams sp = BuildScene(game, space, true, false, anchor);
        r.drawScene(v, sp);
        // environment at real scale around the player: ground 1.2 m below the head
        SceneParams env; env.game_to_world = MenuEnvironmentTransform(space);
        env.draw_track = false; env.draw_opponent = false; env.draw_shadow = false;
        r.drawScene(v, env);
        DrawMenuPanel(menu, game, 7, true, "WHEEL SETUP 3/7 - PRESS A WHEEL BUTTON FOR|TURN AROUND / RESET|OR WAIT 8 SECONDS TO SKIP");
        r.drawPanel(v, menu, Pose{vec3(0.0f, 0.15f, -1.9f), quat()}, 1.1f, 0.69f, false);
        (void)floor_space;
        SavePPM(pre + "07_vr_menu_diorama", W, H);
    }

    // ---- scenery types ----------------------------------------------------------------------
    {
        game.backToMenu();
        game.selectTrack(0);
        NextSceneryType();   // 4 -> 0 (standard)
        NextSceneryType();   // taller
        NextSceneryType();   // snow capped
        r.sync();
        Run(game, 1.0);
        Begin(r); r.drawScene(FlatView(game, space, W, H, false), BuildScene(game, space, false, false, Pose()));
        SavePPM(pre + "08_menu_snowcapped", W, H);
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) { printf("FAIL: GL error 0x%x\n", err); failures++; }
    r.shutdown();
    game.shutdown();
    printf(failures ? "%d FAILURE(S)\n" : "RENDER TEST PASSED\n", failures);
    return failures ? 1 : 0;
}
