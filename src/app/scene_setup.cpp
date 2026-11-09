// scene_setup.cpp - Game state -> views, scene parameters and HUD panels.

#include "scene_setup.h"
#include "track.h"

#include <cmath>
#include <cstdio>
#include <algorithm>

using namespace vm;

#ifndef SCR_VERSION
#define SCR_VERSION "dev"
#endif

namespace scr {

static inline uint32_t RGBA(int r, int g, int b, int a = 255)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static const uint32_t kYellow = RGBA(255, 255, 0);
static const uint32_t kWhite = RGBA(255, 255, 255);
static const uint32_t kGrey = RGBA(170, 170, 170);
static const uint32_t kRed = RGBA(238, 0, 85);
static const uint32_t kPanelBg = RGBA(16, 16, 28, 210);

static quat YawOnly(const quat &q)
{
    vec3 f = q.rotate(vec3(0, 0, -1));
    float yaw = std::atan2(-f.x, -f.z);
    return quat::axisAngle({0, 1, 0}, yaw);
}

Pose SeatWorldPose(const Game &game, const Space &space, const ComfortSettings &comfort)
{
    GamePose eye = game.cockpitEyePose();
    Pose p = space.poseToWorld(eye.position, eye.orientation);
    if (comfort.horizon_lock > 0.0f)
        p.orientation = slerp(p.orientation, YawOnly(p.orientation), std::min(1.0f, comfort.horizon_lock));
    return p;
}

View FlatView(const Game &game, const Space &space, int width, int height, bool cockpit, float vfov_deg)
{
    const bool racing = game.mode() == Mode::Racing || game.mode() == Mode::GameOver;
    GamePose cam = (racing && cockpit && !game.outsideView()) ? game.cockpitEyePose() : game.cameraPose();
    Pose w = space.poseToWorld(cam.position, cam.orientation);
    View v;
    v.view = w.inverse().matrix();
    v.proj = mat4::perspective(vfov_deg * kPi / 180.0f, (float)width / (float)std::max(1, height), 0.05f, 6000.0f);
    return v;
}

mat4 DioramaTransform(const Pose &anchor, float table_width_m)
{
    const float track_units = (float)(NUM_TRACK_CUBES * (CUBE_SIZE >> LOG_PRECISION));
    const float k = table_width_m / track_units;
    const float c = track_units * 0.5f;
    return anchor.matrix() * mat4::scale({k, k, -k}) * mat4::translation({-c, 0.0f, -c});
}

Pose DefaultDioramaAnchor()
{
    // in front of and below the head, tilted slightly toward the viewer
    return {vec3(0.0f, -0.55f, -1.25f), quat::axisAngle({1, 0, 0}, 0.12f)};
}

mat4 MenuEnvironmentTransform(const Space &space, float floor_below_head_m)
{
    // put the player in the middle of the (empty) world at ground level
    const float c = (float)(NUM_TRACK_CUBES * (CUBE_SIZE >> LOG_PRECISION)) * 0.5f * space.metres_per_unit;
    return mat4::translation({-c, -floor_below_head_m, c}) * space.gameToWorld();
}

SceneParams BuildScene(const Game &game, const Space &space, bool vr, bool cockpit, const Pose &diorama_anchor)
{
    SceneParams p;
    const Mode mode = game.mode();
    const bool racing = (mode == Mode::Racing || mode == Mode::GameOver);
    const bool diorama = vr && !racing;

    p.game_to_world = diorama ? DioramaTransform(diorama_anchor) : space.gameToWorld();
    p.draw_track = true;
    p.draw_ground = !diorama;
    p.draw_scenery = !diorama;
    p.draw_opponent = game.opponentVisible();
    p.draw_player_car = racing && game.outsideView();
    p.draw_cockpit = racing && cockpit && !game.outsideView();
    p.draw_shadow = !diorama;
    p.draw_table = diorama;
    p.car_scale = diorama ? 6.0f : 1.0f;

    GamePose pc = game.playerCarPose(), oc = game.opponentCarPose();
    p.player_pos = pc.position; p.player_rot = pc.orientation;
    p.opponent_pos = oc.position; p.opponent_rot = oc.orientation;
    return p;
}

Pose ChaseCamera::update(const Game &game, const Space &space, float dt, bool look_at_car)
{
    GamePose car = game.playerCarPose();
    const float units = 1.0f / space.metres_per_unit;
    const float dist = distance_m * units, height = height_m * units;

    // heading of the car in game space (+z forward); keep the last one if pointing straight up/down
    vec3 f = car.orientation.rotate(vec3(0, 0, 1));
    float target_yaw = m_yaw;
    if (f.x * f.x + f.z * f.z > 0.05f) target_yaw = std::atan2(f.x, f.z);

    float dyaw = target_yaw - m_yaw;
    while (dyaw > kPi) dyaw -= 2 * kPi;
    while (dyaw < -kPi) dyaw += 2 * kPi;

    auto desired = [&](float yaw) { return car.position + vec3(-std::sin(yaw) * dist, height, -std::cos(yaw) * dist); };

    if (!m_valid || std::fabs(dyaw) > kPi * 0.55f || length(desired(target_yaw) - m_pos) > dist * 4.0f)
    {
        // first frame, turned around, or reset onto the chains: jump straight there
        m_yaw = target_yaw;
        m_pos = desired(m_yaw);
        m_valid = true;
    }
    else
    {
        m_yaw += dyaw * std::min(1.0f, dt * 3.5f);
        vec3 want = desired(m_yaw);
        m_pos = m_pos + (want - m_pos) * std::min(1.0f, dt * 6.0f);
    }

    GamePose cam;
    cam.position = m_pos;
    if (look_at_car)
    {
        vec3 d = normalize((car.position + vec3(0, 40, 0)) - m_pos);
        float yaw = std::atan2(d.x, d.z);
        float pitch = -std::asin(std::max(-1.0f, std::min(1.0f, d.y)));
        cam.orientation = normalize(quat::axisAngle({0, 1, 0}, yaw) * quat::axisAngle({1, 0, 0}, pitch));
    }
    else
    {
        cam.orientation = quat::axisAngle({0, 1, 0}, m_yaw);
    }
    m_pose = space.poseToWorld(cam.position, cam.orientation);
    return m_pose;
}

float ComfortVignette(const Game &game, const ComfortSettings &comfort, float dt)
{
    static float level = 0.0f;
    float target = 0.0f;
    if (game.mode() == Mode::Racing && comfort.vignette > 0.0f)
    {
        const ScrCarTelemetry &t = game.telemetry();
        if (!t.touching_road && !t.on_chains) target = 1.0f;               // airborne
        float rot = (float)(std::abs(t.pitch_rate) + std::abs(t.roll_rate)) / 400.0f;   // tumbling
        target = std::max(target, std::min(1.0f, rot));
        target *= comfort.vignette;
    }
    // fast in, slower out
    float rate = target > level ? 8.0f : 2.5f;
    level += (target - level) * std::min(1.0f, rate * dt);
    return level;
}

void DrawDashboard(TextPanel &panel, const Game &game)
{
    if (panel.width() == 0) panel.create(512, 160);
    const HudState &h = game.hud();
    panel.clear(kPanelBg);
    panel.fillRect(0, 0, panel.width(), 3, kYellow);

    char buf[64];
    snprintf(buf, sizeof(buf), "%3d", h.speed);
    panel.text(16, 20, buf, 6, kYellow);
    panel.text(176, 52, "MPH", 2, kGrey);

    snprintf(buf, sizeof(buf), "LAP %s", h.lap > 0 ? std::to_string(h.lap).c_str() : "-");
    panel.text(270, 16, buf, 3, kWhite);
    snprintf(buf, sizeof(buf), "BOOST %d", h.boost);
    panel.text(270, 52, buf, 3, game.telemetry().boost_active ? kRed : kWhite);
    snprintf(buf, sizeof(buf), "OPP %+d", h.opponent_distance);
    panel.text(270, 88, buf, 3, kWhite);

    // damage bar (the original drew a crack along the top of the screen)
    panel.text(16, 124, "DAMAGE", 2, kGrey);
    int dmg = std::max(0, std::min(h.damage, 0x1400));
    int bw = (int)(380.0f * (float)dmg / (float)0x1400);
    panel.fillRect(120, 124, 380, 18, RGBA(60, 60, 70, 255));
    panel.fillRect(120, 124, bw, 18, kRed);
    panel.upload();
}

void DrawMenuPanel(TextPanel &panel, const Game &game, int highlighted, bool vr, const std::string &notice)
{
    if (panel.width() == 0) panel.create(1024, 640);
    panel.clear(kPanelBg);
    panel.fillRect(0, 0, panel.width(), 6, kYellow);
    panel.text(40, 30, "STUNT CAR RACER VR", 5, kYellow);
    {
        std::string ver = std::string("V") + SCR_VERSION;
        panel.text(panel.width() - TextPanel::textWidth(ver, 2) - 16, 18, ver, 2, kGrey);
    }

    if (game.mode() == Mode::TrackMenu)
    {
        panel.text(40, 100, "CHOOSE TRACK", 3, kWhite);
        for (int i = 0; i < Game::numTracks(); i++)
        {
            int y = 150 + i * 44;
            bool sel = (i == highlighted);
            if (sel) panel.fillRect(30, y - 6, 600, 40, RGBA(70, 70, 110, 255));
            char buf[64];
            snprintf(buf, sizeof(buf), "%d  %s", i + 1, Game::trackName(i).c_str());
            panel.text(44, y, buf, 3, sel ? kYellow : kWhite);
        }
        if (vr)
        {
            panel.text(40, 530, "THUMBSTICK  CHOOSE TRACK", 2, kGrey);
            panel.text(40, 560, "TRIGGER     SELECT", 2, kGrey);
        }
        else
        {
            panel.text(40, 530, "1-8 / UP DOWN  CHOOSE TRACK", 2, kGrey);
            panel.text(40, 560, "S / ENTER      SELECT     ESC  QUIT", 2, kGrey);
        }
    }
    else if (game.mode() == Mode::TrackPreview)
    {
        panel.text(40, 110, "SELECTED TRACK", 3, kWhite);
        panel.text(40, 160, Game::trackName(game.selectedTrack()), 5, kYellow);
        if (vr)
        {
            panel.text(40, 300, "TRIGGER       START RACE", 3, kWhite);
            panel.text(40, 350, "MENU BUTTON   TRACK MENU", 3, kWhite);
            panel.text(40, 440, "IN RACE:  RIGHT TRIGGER ACCELERATE", 2, kGrey);
            panel.text(40, 470, "          LEFT TRIGGER BRAKE / REVERSE", 2, kGrey);
            panel.text(40, 500, "          GRIP BOTH HANDS ON WHEEL OR STICK TO STEER", 2, kGrey);
            panel.text(40, 530, "          A BOOST   B TURN AROUND   Y RECENTRE", 2, kGrey);
        }
        else
        {
            panel.text(40, 300, "S / ENTER  START RACE", 3, kWhite);
            panel.text(40, 350, "M          TRACK MENU", 3, kWhite);
            panel.text(40, 440, "IN RACE:  LEFT/RIGHT STEER  UP ACCELERATE", 2, kGrey);
            panel.text(40, 470, "          DOWN BRAKE  SPACE BOOST  R TURN AROUND", 2, kGrey);
            panel.text(40, 500, "          C COCKPIT  V OUTSIDE VIEW  P PAUSE", 2, kGrey);
        }
    }
    if (!notice.empty())
    {
        // banner (e.g. the wheel button setup): "small line|BIG LINE|small line"
        std::string parts[3];
        size_t p0 = 0;
        for (int i = 0; i < 3; i++)
        {
            size_t c = notice.find('|', p0);
            parts[i] = notice.substr(p0, c == std::string::npos ? std::string::npos : c - p0);
            if (c == std::string::npos) break;
            p0 = c + 1;
        }
        panel.fillRect(0, 410, panel.width(), 230, RGBA(120, 20, 40, 245));
        panel.fillRect(0, 410, panel.width(), 6, kYellow);
        auto centred = [&](const std::string &t, int y, int scale, uint32_t c) {
            int w = TextPanel::textWidth(t, scale);
            while (w > panel.width() - 20 && scale > 1) w = TextPanel::textWidth(t, --scale);
            panel.text((panel.width() - w) / 2, y, t, scale, c);
        };
        centred(parts[0], 440, 3, kWhite);
        centred(parts[1], 500, 6, kYellow);
        centred(parts[2], 590, 2, kGrey);
    }
    panel.upload();
}

void DrawRaceOverlay(TextPanel &panel, const Game &game, bool vr)
{
    if (panel.width() == 0) panel.create(1024, 256);
    const HudState &h = game.hud();
    panel.clear(RGBA(0, 0, 0, 0));
    bool any = false;
    if (h.show_opponent_name)
    {
        std::string s = "OPPONENT: " + h.opponent_name;
        panel.text((1024 - TextPanel::textWidth(s, 4)) / 2, 40, s, 4, kYellow);
        any = true;
    }
    if (game.mode() == Mode::GameOver)
    {
        const char *s = vr ? "GAME OVER - MENU BUTTON FOR TRACKS" : "GAME OVER - PRESS M FOR TRACK MENU";
        panel.text((1024 - TextPanel::textWidth(s, 3)) / 2, 120, s, 3, kWhite);
        any = true;
    }
    else if (h.race_finished)
    {
        const char *s = h.race_won ? "RACE WON" : "RACE LOST";
        bool white = std::fmod(h.race_time, 1.0) < 0.5;   // flashes like the original
        panel.text((1024 - TextPanel::textWidth(s, 8)) / 2, 100, s, 8, white ? kWhite : RGBA(0, 0, 0));
        any = true;
    }
    if (game.paused())
    {
        panel.text((1024 - TextPanel::textWidth("PAUSED", 6)) / 2, 150, "PAUSED", 6, kWhite);
        const char *hint = vr ? "MENU BUTTON RESUME   B TRACK MENU" : "P / O RESUME   R OR M TRACK MENU";
        panel.text((1024 - TextPanel::textWidth(hint, 2)) / 2, 210, hint, 2, kGrey);
        any = true;
    }
    (void)any;
    panel.upload();
}

} // namespace scr
