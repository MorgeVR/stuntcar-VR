// game.cpp - Platform independent game loop for Stunt Car Racer VR.
//
// Logic derived from StuntCarRacer.cpp of the Windows remake (OnFrameMove,
// OnFrameRender, HandleTrackMenu/Preview, the viewpoint functions and DSSetMode),
// with DXUT / Direct3D / DirectSound removed and a fixed-timestep simulation.

#include "game.h"

#include "scr_compat.h"
#include "stuntcarracer.h"
#include "3d_engine.h"
#include "track.h"
#include "car.h"
#include "car_behaviour.h"
#include "opponent_behaviour.h"

#include <cmath>
#include <ctime>
#include <algorithm>

using namespace vm;

// =====================================================================================
// Globals the original game modules expect from the main program
// =====================================================================================
FILE *out = nullptr;
GameModeType GameMode = TRACK_MENU;
bool bNewGame = FALSE;
bool bPaused = FALSE;
bool bPlayerPaused = FALSE;
bool bOpponentPaused = FALSE;
int32_t engineSoundPlaying = FALSE;
int32_t VALUE1 = 1, VALUE2 = 2, VALUE3 = 3;

ScrSound *WreckSoundBuffer = nullptr;
ScrSound *HitCarSoundBuffer = nullptr;
ScrSound *GroundedSoundBuffer = nullptr;
ScrSound *CreakSoundBuffer = nullptr;
ScrSound *SmashSoundBuffer = nullptr;
ScrSound *OffRoadSoundBuffer = nullptr;
ScrSound *EngineSoundBuffers[8] = {};

extern int32_t TrackID;
extern int32_t boostReserve, boostUnit, StandardBoost, SuperBoost;
extern int32_t INITIALISE_PLAYER;
extern bool raceFinished, raceWon;
extern int32_t lapNumber[];
extern int32_t new_damage;
extern int32_t opponentsID;
extern WCHAR *opponentNames[];

// Player / opponent / viewpoint state (was static in StuntCarRacer.cpp)
static int32_t player1_x = 0, player1_y = 0, player1_z = 0;
static int32_t player1_x_angle = 0, player1_y_angle = 0, player1_z_angle = 0;
static int32_t opponent_x = 0, opponent_y = 0, opponent_z = 0;
static float opponent_x_angle = 0.0f, opponent_y_angle = 0.0f, opponent_z_angle = 0.0f;
static int32_t viewpoint1_x, viewpoint1_y, viewpoint1_z;
static int32_t viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle;

#define HEIGHT_ABOVE_ROAD (100)

// =====================================================================================
// Palette (from StuntCarRacer.cpp)
// =====================================================================================
struct PaletteEntry { BYTE r, g, b; };
#define NUM_PALETTE_ENTRIES (42)

static const PaletteEntry SCPalette[NUM_PALETTE_ENTRIES] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00},
    // car colours 1
    {0x00, 0x00, 0x00}, {0x88, 0x00, 0x22}, {0xaa, 0x00, 0x33}, {0xcc, 0x00, 0x44}, {0xee, 0x00, 0x55},
    {0x22, 0x22, 0x33}, {0x44, 0x44, 0x44}, {0x33, 0x33, 0x33},
    // car colours 2
    {0x00, 0x00, 0x00}, {0x22, 0x00, 0x88}, {0x33, 0x00, 0xaa}, {0x44, 0x00, 0xcc}, {0x55, 0x00, 0xee},
    {0x22, 0x22, 0x33}, {0x44, 0x44, 0x44}, {0x33, 0x33, 0x33},
    // track colours (i.e. Stunt Car Racer car colours)
    {0x00, 0x00, 0x00}, {0x99, 0x99, 0x77}, {0xbb, 0xbb, 0x99}, {0xff, 0xff, 0x00}, {0x99, 0xbb, 0x33},
    {0x55, 0x77, 0x77}, {0x55, 0xbb, 0xff}, {0x55, 0x99, 0xff}, {0x33, 0x55, 0x77}, {0x55, 0x00, 0x00},
    {0x77, 0x33, 0x33}, {0x99, 0x55, 0x55}, {0xdd, 0x99, 0x99}, {0x77, 0x77, 0x55}, {0xbb, 0xbb, 0xbb},
    {0xff, 0xff, 0xff}};

DWORD SCRGB(int32_t colour_index)
{
    if (colour_index < 0 || colour_index >= NUM_PALETTE_ENTRIES) colour_index = 0;
    const PaletteEntry &e = SCPalette[colour_index];
    return D3DCOLOR_XRGB(e.r, e.g, e.b);
}

DWORD SCColour(int32_t colour_index) { return SCRGB(colour_index); }

DWORD Fill_Colour = 0, Line_Colour = 0;
void SetSolidColour(int32_t colour_index) { Fill_Colour = SCRGB(colour_index); }
void SetLineColour(int32_t colour_index) { Line_Colour = SCRGB(colour_index); }
void SetTextureColour(int32_t colour_index) { Fill_Colour = SCRGB(colour_index); }

void GetScreenDimensions(int32_t *screen_width, int32_t *screen_height)
{
    // The 2D code paths (original backdrop, z-clipping) work in a 640x480 virtual screen.
    *screen_width = 640;
    *screen_height = 480;
}

// =====================================================================================
// Sound setup (was DSSetMode)
// =====================================================================================
static ScrSound *LoadSound(const char *name)
{
    ScrSound *s = ScrMixer::Get().CreateFromAsset(name);
    if (!s)
    {
        fprintf(stderr, "[StuntCarVR] failed to load sound %s\n", name);
        // Fall back to a silent buffer so game code can call it safely.
        static const uint8_t silent_wav[] = {
            'R','I','F','F', 37,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
            0x11,0x2b,0,0, 0x11,0x2b,0,0, 1,0, 8,0, 'd','a','t','a', 1,0,0,0, 128};
        s = ScrMixer::Get().CreateFromWav(silent_wav, sizeof(silent_wav));
    }
    return s;
}

static void InitSounds()
{
    // Amiga channels 1 and 2 are right side, channels 0 and 3 are left side
    WreckSoundBuffer = LoadSound("Sounds/Wreck.wav");
    WreckSoundBuffer->SetPan(DSBPAN_RIGHT);
    WreckSoundBuffer->SetVolume(AmigaVolumeToDirectX(64));

    HitCarSoundBuffer = LoadSound("Sounds/HitCar.wav");
    HitCarSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 238);
    HitCarSoundBuffer->SetPan(DSBPAN_RIGHT);
    HitCarSoundBuffer->SetVolume(AmigaVolumeToDirectX(56));

    GroundedSoundBuffer = LoadSound("Sounds/Grounded.wav");
    GroundedSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 400);
    GroundedSoundBuffer->SetPan(DSBPAN_RIGHT);

    CreakSoundBuffer = LoadSound("Sounds/Creak.wav");
    CreakSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 238);
    CreakSoundBuffer->SetPan(DSBPAN_RIGHT);
    CreakSoundBuffer->SetVolume(AmigaVolumeToDirectX(64));

    SmashSoundBuffer = LoadSound("Sounds/Smash.wav");
    SmashSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 280);
    SmashSoundBuffer->SetPan(DSBPAN_LEFT);
    SmashSoundBuffer->SetVolume(AmigaVolumeToDirectX(64));

    OffRoadSoundBuffer = LoadSound("Sounds/OffRoad.wav");
    OffRoadSoundBuffer->SetPan(DSBPAN_RIGHT);
    OffRoadSoundBuffer->SetVolume(AmigaVolumeToDirectX(64));

    static const char *engine[8] = {"Sounds/TickOver.wav", "Sounds/EnginePitch2.wav", "Sounds/EnginePitch3.wav",
                                    "Sounds/EnginePitch4.wav", "Sounds/EnginePitch5.wav", "Sounds/EnginePitch6.wav",
                                    "Sounds/EnginePitch7.wav", "Sounds/EnginePitch8.wav"};
    for (int i = 0; i < 8; i++)
    {
        EngineSoundBuffers[i] = LoadSound(engine[i]);
        EngineSoundBuffers[i]->SetPan(DSBPAN_LEFT);
        // Original Amiga volume was 48, but the remake reduced this for testing
        EngineSoundBuffers[i]->SetVolume(AmigaVolumeToDirectX(48 / 2));
    }
}

static void StopEngineSound()
{
    if (engineSoundPlaying)
    {
        for (int i = 0; i < 8; i++) EngineSoundBuffers[i]->Stop();
        engineSoundPlaying = FALSE;
    }
}

// Convert a wide string (WCHAR is 16-bit on Windows, 32-bit elsewhere) to UTF-8 (ASCII here).
static std::string Narrow(const WCHAR *w)
{
    std::string s;
    if (!w) return s;
    for (; *w; ++w) s.push_back((*w < 128) ? (char)*w : '?');
    return s;
}

// =====================================================================================
// Game
// =====================================================================================
namespace scr {

static inline float FixedToUnits(int32_t v) { return (float)v / (float)PRECISION; }
static inline float AngleToRadians(int32_t a) { return (float)a * 2.0f * kPi / (float)MAX_ANGLE; }

// Original car rotation: D3DX row-vector Z * X * Y, i.e. column-vector Ry * Rx * Rz.
static quat GameRotation(float xa, float ya, float za)
{
    return normalize(quat::axisAngle({0, 1, 0}, ya) * quat::axisAngle({1, 0, 0}, xa) * quat::axisAngle({0, 0, 1}, za));
}

GamePose Game::poseFromFixed(int32_t x, int32_t y, int32_t z, float xa, float ya, float za, float y_offset)
{
    GamePose p;
    p.position = vec3(FixedToUnits(x), FixedToUnits(-y) + y_offset, FixedToUnits(z));
    p.orientation = GameRotation(xa, ya, za);
    return p;
}

bool Game::init(uint32_t audio_rate)
{
    out = stderr;
    ScrMixer::Get().SetOutputRate(audio_rate);

    CreateSinCosTable();
    InitSounds();
    BuildCarMesh();
    srand((unsigned)time(nullptr));

    if (!selectTrack(LITTLE_RAMP))
    {
        fprintf(stderr, "[StuntCarVR] failed to load track data - check the assets folder\n");
        return false;
    }
    GameMode = TRACK_MENU;
    m_prev.valid = m_cur.valid = false;
    return true;
}

void Game::shutdown()
{
    StopEngineSound();
    FreeTrackData();
}

bool Game::selectTrack(int track)
{
    if (track < 0 || track >= NUM_TRACKS) return false;
    if (!ConvertAmigaTrack(track)) return false;
    if (BuildTrackMesh() != S_OK) return false;
    return true;
}

int Game::selectedTrack() const { return TrackID; }

std::string Game::trackName(int track) { return Narrow(GetTrackName(track)); }

Mode Game::mode() const
{
    switch (GameMode)
    {
    case TRACK_MENU: return Mode::TrackMenu;
    case TRACK_PREVIEW: return Mode::TrackPreview;
    case GAME_IN_PROGRESS: return Mode::Racing;
    default: return Mode::GameOver;
    }
}

void Game::startPreview()
{
    if (GameMode != TRACK_MENU || TrackID == NO_TRACK) return;
    bNewGame = TRUE;   // used here just to reset the opponent's car, which is shown during the preview
    ResetPlayer();     // also reset player to clear values from a previous game
    GameMode = TRACK_PREVIEW;
    bPlayerPaused = bOpponentPaused = FALSE;
    m_prev.valid = m_cur.valid = false;
}

void Game::startRace()
{
    if (GameMode != TRACK_PREVIEW) return;
    bNewGame = TRUE;
    GameMode = GAME_IN_PROGRESS;
    ResetLapData(OPPONENT);
    ResetLapData(PLAYER);
    m_race_start = m_time;
    m_race_end = 0.0;
    boostReserve = StandardBoost;   // SuperBoost for super league
    boostUnit = 0;
    bPlayerPaused = bOpponentPaused = FALSE;
    m_prev.valid = m_cur.valid = false;
}

void Game::backToMenu()
{
    if (GameMode == TRACK_MENU) return;
    GameMode = TRACK_MENU;
    opponentsID = NO_OPPONENT;
    StopEngineSound();
    ResetDrawBridge();   // reset all animated objects
    m_prev.valid = m_cur.valid = false;
}

void Game::turnAround() { m_turn_around = true; }

void Game::setPaused(bool p)
{
    m_paused = p;
    bPaused = p;
    if (p)
    {
        StopEngineSound();
        ScrMixer::Get().PauseAll();
    }
}

bool Game::opponentVisible() const { return GameMode != TRACK_MENU && opponentsID != NO_OPPONENT; }

void Game::capture(Snapshot &s) const
{
    s.px = player1_x; s.py = player1_y; s.pz = player1_z;
    s.pxa = player1_x_angle; s.pya = player1_y_angle; s.pza = player1_z_angle;
    s.ox = opponent_x; s.oy = opponent_y; s.oz = opponent_z;
    s.oxa = opponent_x_angle; s.oya = opponent_y_angle; s.oza = opponent_z_angle;
    s.vx = viewpoint1_x; s.vy = viewpoint1_y; s.vz = viewpoint1_z;
    s.vxa = viewpoint1_x_angle; s.vya = viewpoint1_y_angle; s.vza = viewpoint1_z_angle;
    s.lift = m_lift;
    s.valid = true;
}

int Game::update(double dt)
{
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.25) dt = 0.25;   // avoid a spiral of death after a stall (e.g. headset removed)
    m_time += dt;

    // Menu camera circles the track (originally 128 angle units per ~60 Hz frame)
    if (GameMode == TRACK_MENU && !m_paused)
        m_menu_circle_angle = (int32_t)std::fmod(m_time * 128.0 * 60.0, (double)MAX_ANGLE);

    // Engine sound runs at 50 Hz, independent of the physics rate
    if (GameMode == GAME_IN_PROGRESS && !m_paused)
    {
        m_engine_accum += dt;
        while (m_engine_accum >= 1.0 / kEngineSoundHz)
        {
            m_engine_accum -= 1.0 / kEngineSoundHz;
            FramesWheelsEngine(EngineSoundBuffers);
        }
    }
    else
    {
        m_engine_accum = 0.0;
        StopEngineSound();
    }

    int ticks = 0;
    if (GameMode == TRACK_PREVIEW || GameMode == GAME_IN_PROGRESS)
    {
        if (!m_paused)
        {
            m_accum += dt;
            const double step = 1.0 / m_sim_hz;
            while (m_accum >= step && ticks < 8)
            {
                m_accum -= step;
                tick();
                ticks++;
            }
            if (ticks == 8) m_accum = 0.0;
        }
    }
    else
    {
        m_accum = 0.0;
    }

    // Race finished: show result for six seconds, then end the game (was in RenderText)
    if (GameMode == GAME_IN_PROGRESS && raceFinished)
    {
        if (m_race_end == 0.0) m_race_end = m_time;
        if (m_time - m_race_end > 6.0) GameMode = GAME_OVER;
    }

    updateHud();
    return ticks;
}

void Game::tick()
{
    m_prev = m_cur;

    if (GameMode == GAME_IN_PROGRESS && m_turn_around)
    {
        // point car in opposite direction
        player1_y_angle += _180_DEGREES;
        player1_y_angle &= (MAX_ANGLE - 1);
        INITIALISE_PLAYER = TRUE;
    }
    m_turn_around = false;

    MoveDrawBridge();

    // Car behaviour
    if (GameMode == GAME_IN_PROGRESS && !bPlayerPaused)
    {
        SetAnalogDriverInput(&m_analog);
        DWORD input = 0;
        if (m_keys & kKeyLeft) input |= KEY_P1_LEFT;
        if (m_keys & kKeyRight) input |= KEY_P1_RIGHT;
        if (m_keys & kKeyBrake) input |= KEY_P1_HASH;
        if (m_keys & kKeyBrakeBoost) input |= KEY_P1_BRAKE_BOOST;
        if (m_keys & kKeyAccelBoost) input |= KEY_P1_ACCEL_BOOST;
        if ((m_keys & kKeyAccel) && !(input & (KEY_P1_HASH | KEY_P1_BRAKE_BOOST)))
        {
            // Accelerate without boost isn't possible with the original key mapping;
            // route it through the analog path instead.
            ScrAnalogInput a = m_analog;
            a.active = true;
            a.throttle = 1.0f;
            if (input & KEY_P1_LEFT) a.steer = -1.0f;
            else if (input & KEY_P1_RIGHT) a.steer = 1.0f;
            SetAnalogDriverInput(&a);
            input &= ~(KEY_P1_LEFT | KEY_P1_RIGHT);
        }
        CarBehaviour(input, &player1_x, &player1_y, &player1_z, &player1_x_angle, &player1_y_angle, &player1_z_angle);
    }

    OpponentBehaviour(&opponent_x, &opponent_y, &opponent_z, &opponent_x_angle, &opponent_y_angle, &opponent_z_angle,
                      bOpponentPaused);

    LimitViewpointY(&player1_y);

    if (GameMode == GAME_IN_PROGRESS)
    {
        calcGameViewpoint();
        DrawOtherGraphics();   // (sound effects for dust clouds and sparks)
        UpdateDamage();
        UpdateLapData();
        GetCarTelemetry(&m_telemetry);
        // visual ground correction (see game.h)
        m_lift = 0.0f;
        const ScrCarTelemetry &t = m_telemetry;
        if (t.road_under_wheels && !t.on_chains)
        {
            float c = std::min(t.wheel_clearance[0], std::min(t.wheel_clearance[1], t.wheel_clearance[2]));
            float lift = (kCarRestClearance - kMaxVisualSink) - c;
            if (lift > 0.0f && lift < 160.0f) m_lift = lift;   // larger = road of another piece (falling off)
        }
    }
    else
    {
        calcPreviewViewpoint();
        m_telemetry = ScrCarTelemetry();
    }

    capture(m_cur);
    if (!m_prev.valid) m_prev = m_cur;
    m_ticks++;
}

void Game::calcGameViewpoint()
{
    if (m_outside_view)
    {
        // set Viewpoint 1 to behind Player 1
        CalcYXZTrigCoefficients(player1_x_angle, player1_y_angle, player1_z_angle);
        int32_t x_offset = 0, y_offset = 0xc0, z_offset = 0x300;   // vector from centre of car
        WorldOffset(&x_offset, &y_offset, &z_offset);
        viewpoint1_x = player1_x - x_offset;
        viewpoint1_y = player1_y - y_offset;
        viewpoint1_z = player1_z - z_offset;
    }
    else
    {
        viewpoint1_x = player1_x;
        viewpoint1_y = player1_y - (HEIGHT_ABOVE_ROAD << LOG_PRECISION);
        viewpoint1_z = player1_z;
    }
    viewpoint1_x_angle = player1_x_angle;
    viewpoint1_y_angle = player1_y_angle;
    viewpoint1_z_angle = player1_z_angle;
}

void Game::calcPreviewViewpoint()
{
    // camera half way between the centre of the world and the opponent, looking at it
    const int32_t centre = (NUM_TRACK_CUBES * CUBE_SIZE) / 2;
    viewpoint1_x = centre + (opponent_x - centre) / 2;
    viewpoint1_z = centre + (opponent_z - centre) / 2;
    if (TrackID == DRAW_BRIDGE)
        viewpoint1_y = opponent_y - (CUBE_SIZE * 5) / 2;   // Draw Bridge requires a higher viewpoint
    else
        viewpoint1_y = opponent_y - CUBE_SIZE / 2;
}

void Game::calcMenuViewpoint() {}

// Interpolate two fixed-point snapshots for the player car
static void InterpFixed(int32_t a, int32_t b, float t, float &outv) { outv = (float)a + (float)(b - a) * t; }

GamePose Game::playerCarPose(float t) const
{
    const Snapshot &a = m_prev.valid ? m_prev : m_cur;
    const Snapshot &b = m_cur;
    if (t < 0.0f) t = alpha();
    t = std::min(1.0f, std::max(0.0f, t));

    GamePose pa = poseFromFixed(a.px, a.py, a.pz, AngleToRadians(a.pxa), AngleToRadians(a.pya), AngleToRadians(a.pza), kCarDrawOffset + a.lift);
    GamePose pb = poseFromFixed(b.px, b.py, b.pz, AngleToRadians(b.pxa), AngleToRadians(b.pya), AngleToRadians(b.pza), kCarDrawOffset + b.lift);

    // Snap on discontinuities (reset onto the chains, turning around)
    if (length(pb.position - pa.position) > 2048.0f || angleBetween(pa.orientation, pb.orientation) > kPi * 0.5f)
        return pb;
    return {lerp(pa.position, pb.position, t), slerp(pa.orientation, pb.orientation, t)};
}

GamePose Game::opponentCarPose(float t) const
{
    const Snapshot &a = m_prev.valid ? m_prev : m_cur;
    const Snapshot &b = m_cur;
    if (t < 0.0f) t = alpha();
    t = std::min(1.0f, std::max(0.0f, t));
    GamePose pa = poseFromFixed(a.ox, a.oy, a.oz, a.oxa, a.oya, a.oza, VCAR_HEIGHT / 4.0f);
    GamePose pb = poseFromFixed(b.ox, b.oy, b.oz, b.oxa, b.oya, b.oza, VCAR_HEIGHT / 4.0f);
    if (length(pb.position - pa.position) > 2048.0f || angleBetween(pa.orientation, pb.orientation) > kPi * 0.5f)
        return pb;
    return {lerp(pa.position, pb.position, t), slerp(pa.orientation, pb.orientation, t)};
}

GamePose Game::cockpitEyePose(float t) const
{
    // The driver's eye is attached to the car body (VR seat origin), inside the cockpit.
    // (The original flat view placed it HEIGHT_ABOVE_ROAD units straight up from the
    // car's physics origin, just above the roof - see cameraPose().)
    GamePose car = playerCarPose(t);
    GamePose eye;
    eye.orientation = car.orientation;
    eye.position = car.position + car.orientation.rotate(vec3(kSeatEyeX, kSeatEyeY, kSeatEyeZ));
    return eye;
}

static quat LookRotationLH(const vec3 &eye, const vec3 &target)
{
    vec3 f = normalize(target - eye);
    float yaw = std::atan2(f.x, f.z);
    float pitch = -std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
    return normalize(quat::axisAngle({0, 1, 0}, yaw) * quat::axisAngle({1, 0, 0}, pitch));
}

GamePose Game::cameraPose() const
{
    GamePose cam;
    const float centre = (NUM_TRACK_CUBES * CUBE_SIZE) / 2 / (float)PRECISION;

    if (GameMode == TRACK_MENU)
    {
        // camera moves in a circle around the track, 3 cubes up, looking at the centre of the world
        const float radius = (float)((NUM_TRACK_CUBES - 2) * (CUBE_SIZE / PRECISION));
        const float a = AngleToRadians(m_menu_circle_angle);
        cam.position = vec3(centre + std::sin(a) * radius, (float)(3 * (CUBE_SIZE / PRECISION)), centre + std::cos(a) * radius);
        cam.orientation = LookRotationLH(cam.position, vec3(centre, 0.0f, centre));
        return cam;
    }

    if (GameMode == TRACK_PREVIEW)
    {
        GamePose opp = opponentCarPose();
        vec3 target = opp.position;
        cam.position = vec3(centre + (target.x - centre) * 0.5f, 0.0f, centre + (target.z - centre) * 0.5f);
        float above = (TrackID == DRAW_BRIDGE) ? (CUBE_SIZE * 5.0f / 2.0f) : (CUBE_SIZE / 2.0f);
        cam.position.y = target.y - VCAR_HEIGHT / 4.0f + above / PRECISION;
        cam.orientation = LookRotationLH(cam.position, target);
        return cam;
    }

    // Racing / game over: interpolated original viewpoint
    const Snapshot &a = m_prev.valid ? m_prev : m_cur;
    const Snapshot &b = m_cur;
    float t = std::min(1.0f, std::max(0.0f, alpha()));
    GamePose pa = poseFromFixed(a.vx, a.vy, a.vz, AngleToRadians(a.vxa), AngleToRadians(a.vya), AngleToRadians(a.vza), a.lift);
    GamePose pb = poseFromFixed(b.vx, b.vy, b.vz, AngleToRadians(b.vxa), AngleToRadians(b.vya), AngleToRadians(b.vza), b.lift);
    if (length(pb.position - pa.position) > 2048.0f || angleBetween(pa.orientation, pb.orientation) > kPi * 0.5f)
        return pb;
    return {lerp(pa.position, pb.position, t), slerp(pa.orientation, pb.orientation, t)};
}

void Game::updateHud()
{
    HudState &h = m_hud;
    if (GameMode == GAME_IN_PROGRESS || GameMode == GAME_OVER)
    {
        h.lap = lapNumber[PLAYER];
        h.boost = boostReserve;
        h.speed = CalculateDisplaySpeed();
        h.damage = new_damage;
        h.opponent_distance = CalculateOpponentsDistance();
        h.show_opponent_name = (m_time - m_race_start) < 4.0 && opponentsID != NO_OPPONENT;
        h.opponent_name = (opponentsID != NO_OPPONENT) ? Narrow(opponentNames[opponentsID]) : std::string();
        h.race_finished = raceFinished;
        h.race_won = raceWon;
        h.race_time = m_time - m_race_start;
    }
    else
    {
        h = HudState();
    }
}

} // namespace scr
