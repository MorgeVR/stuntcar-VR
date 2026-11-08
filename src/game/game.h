// game.h - Platform independent Stunt Car Racer game loop.
//
// Replaces the DXUT OnFrameMove/OnFrameRender of the original Windows remake.
// The simulation runs at a fixed rate (the original ran its physics every
// `frameGap` (4) display frames, i.e. speed depended on the monitor refresh rate).
// Rendering interpolates between the two most recent simulation states so that
// the low physics rate (12.5 Hz) looks smooth at 72/90/120 Hz in a headset.
//
// Spaces:
//   * "game units" - the original Direct3D left-handed render space:
//     x right, y up, z forward, 1 unit = original vertex unit (CUBE_SIZE >> 14 = 4096 per track cube).
//   * "world" (metres) - OpenXR / OpenGL right-handed space: x right, y up, -z forward.
//     See scr::GameToWorld().

#pragma once

#include <cstdint>
#include <string>
#include "vmath.h"
#include "car_behaviour.h"

namespace scr {

enum class Mode { TrackMenu, TrackPreview, Racing, GameOver };

// Default metres per game unit. The car model is 256 units long, giving ~4.0 m.
constexpr float kDefaultMetresPerUnit = 1.0f / 64.0f;

// Simulation rate. The Amiga original ran its physics at most every 6th 50 Hz frame
// (MIN.FRAMES = 6, i.e. 8.33 Hz); the Windows remake used every 4th display frame
// (12.5-15 Hz, which players found too fast). Default to the Amiga pace.
constexpr double kAmigaSimHz = 50.0 / 6.0;
constexpr double kRemakeSimHz = 50.0 / 4.0;
constexpr double kDefaultSimHz = kAmigaSimHz;

// Driver's eye inside the car body for VR, in car-model units (+y up, +z forward).
// The car model spans y -40 (wheel bottom) .. +40 (roof); this is ~1 m above the road.
constexpr float kSeatEyeX = 0.0f, kSeatEyeY = 24.0f, kSeatEyeZ = -16.0f;
// The engine sound routine is documented as needing 50 Hz.
constexpr double kEngineSoundHz = 50.0;

// Digital controls (bit flags, same values as the original KEY_P1_*)
enum : uint32_t
{
    kKeyLeft  = 0x01,
    kKeyRight = 0x02,
    kKeyBrake = 0x04,         // original "HASH": brake / reverse
    kKeyBrakeBoost = 0x08,    // original SPACE
    kKeyAccelBoost = 0x10,    // original RETURN
    kKeyAccel = 0x20,         // accelerate without boost (new; keyboard couldn't before)
};

struct HudState
{
    int lap = 0;                 // 0 = not started
    int boost = 0;
    int speed = 0;
    int damage = 0;
    int opponent_distance = 0;
    bool show_opponent_name = false;
    std::string opponent_name;
    bool race_finished = false;
    bool race_won = false;
    double race_time = 0.0;
};

// Pose of the player's eye in game units, LH space. Used for flat-screen rendering
// and as the "seat" origin for the VR head pose.
struct GamePose
{
    vm::vec3 position;      // game units (LH)
    vm::quat orientation;   // rotation in LH game space (column-vector, Ry*Rx*Rz)
};

class Game
{
public:
    bool init(uint32_t audio_rate);
    void shutdown();

    // ---- input ------------------------------------------------------------------
    void setDigitalInput(uint32_t keys) { m_keys = keys; }
    void setAnalogInput(const ScrAnalogInput &in) { m_analog = in; }

    // ---- flow control (menu actions) -------------------------------------------
    bool selectTrack(int track);         // 0..7, loads and builds the mesh
    int  selectedTrack() const;
    static int numTracks() { return 8; }
    static std::string trackName(int track);
    void startPreview();                 // TRACK_MENU -> TRACK_PREVIEW
    void startRace();                    // TRACK_PREVIEW -> GAME_IN_PROGRESS
    void backToMenu();
    void turnAround();                   // original 'R' key: face the other way
    void setPaused(bool p);
    bool paused() const { return m_paused; }
    void setOutsideView(bool o) { m_outside_view = o; }
    bool outsideView() const { return m_outside_view; }

    // ---- per frame -----------------------------------------------------------------
    // Advance real time; runs 0..n fixed simulation ticks. Returns ticks run.
    int update(double dt_seconds);

    Mode mode() const;
    double simHz() const { return m_sim_hz; }
    void setSimHz(double hz) { m_sim_hz = hz; }
    // Interpolation factor between previous and current tick (0..1).
    float alpha() const { return (float)(m_accum * m_sim_hz); }

    // Interpolated poses (game units, LH). Car poses are the drawn car origin.
    // t: interpolation factor override (0 = previous tick, 1 = latest tick); < 0 uses alpha().
    GamePose playerCarPose(float t = -1.0f) const;
    GamePose opponentCarPose(float t = -1.0f) const;
    GamePose cockpitEyePose(float t = -1.0f) const;  // driver's eye (seat origin for VR)
    GamePose cameraPose() const;                     // menu / preview / race camera for flat mode
    bool opponentVisible() const;

    const HudState &hud() const { return m_hud; }
    const ScrCarTelemetry &telemetry() const { return m_telemetry; }
    uint32_t tickCount() const { return m_ticks; }

private:
    struct Snapshot
    {
        int32_t px = 0, py = 0, pz = 0, pxa = 0, pya = 0, pza = 0;   // player (fixed point)
        int32_t ox = 0, oy = 0, oz = 0;                              // opponent
        float oxa = 0, oya = 0, oza = 0;
        int32_t vx = 0, vy = 0, vz = 0, vxa = 0, vya = 0, vza = 0;   // camera
        bool valid = false;
    };

    void tick();
    void capture(Snapshot &s) const;
    void calcMenuViewpoint();
    void calcPreviewViewpoint();
    void calcGameViewpoint();
    void updateHud();
    static GamePose poseFromFixed(int32_t x, int32_t y, int32_t z, float xa, float ya, float za, float y_offset);

    uint32_t m_keys = 0;
    uint32_t m_prev_keys = 0;
    ScrAnalogInput m_analog = {};
    double m_accum = 0.0;
    double m_engine_accum = 0.0;
    double m_sim_hz = kDefaultSimHz;
    double m_time = 0.0;
    double m_race_start = 0.0, m_race_end = 0.0;
    bool m_paused = false;
    bool m_outside_view = false;
    bool m_turn_around = false;
    uint32_t m_ticks = 0;
    int32_t m_menu_circle_angle = 0;
    Snapshot m_prev, m_cur;
    HudState m_hud;
    ScrCarTelemetry m_telemetry = {};
};

} // namespace scr
