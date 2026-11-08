// sim_test.cpp - headless checks of the ported game core (no window, no GPU).
//
//  1. Every track loads and builds a mesh; the car drops onto the track and drives.
//  2. Frame-rate independence: the same race rendered at 60, 90, 120 and 144 Hz
//     produces bit-identical physics (the original's speed depended on refresh rate).
//  3. Analog steering at full lock is identical to digital steering, and a centred
//     analog stick is identical to no steering.
//  4. The software mixer produces engine sound while racing.

#include "game.h"
#include "track.h"
#include "car.h"
#include "scr_compat.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); g_failures++; } } while (0)

struct Trace { std::vector<float> x, y, z; ScrCarTelemetry last{}; };

static scr::Game g_game;

// Run a race for `seconds` with constant input, rendering at `hz`; sample the car pose once per tick.
static Trace RunRace(int track, double hz, double seconds, uint32_t keys, ScrAnalogInput analog)
{
    srand(1234);
    g_game.backToMenu();
    g_game.selectTrack(track);
    g_game.startPreview();
    g_game.update(0.0);
    g_game.startRace();
    g_game.setDigitalInput(keys);
    g_game.setAnalogInput(analog);

    Trace tr;
    const double dt = 1.0 / hz;
    uint32_t last_tick = g_game.tickCount();
    for (double t = 0; t < seconds; t += dt)
    {
        g_game.update(dt);
        if (g_game.tickCount() != last_tick)
        {
            last_tick = g_game.tickCount();
            scr::GamePose p = g_game.playerCarPose(1.0f);
            // alpha is ~0 right after a tick in a fixed sequence; sample the current tick
            tr.x.push_back(p.position.x);
            tr.y.push_back(p.position.y);
            tr.z.push_back(p.position.z);
        }
    }
    tr.last = g_game.telemetry();
    return tr;
}

static float MaxDiff(const Trace &a, const Trace &b, size_t n)
{
    float m = 0;
    for (size_t i = 0; i < n; i++)
        m = std::fmax(m, std::fabs(a.x[i] - b.x[i]) + std::fabs(a.y[i] - b.y[i]) + std::fabs(a.z[i] - b.z[i]));
    return m;
}

int main()
{
    printf("Stunt Car Racer VR - core simulation tests\n");
    if (!g_game.init(48000)) { printf("FAIL: init (assets not found?)\n"); return 1; }

    // 1. All tracks load + drive -----------------------------------------------------------
    printf("[1] tracks load and the car drives\n");
    ScrAnalogInput noAnalog = {};
    for (int t = 0; t < scr::Game::numTracks(); t++)
    {
        CHECK(g_game.selectTrack(t), "track %d failed to load", t);
        int32_t nv = 0;
        GetTrackVertices(&nv);
        CHECK(nv > 0 && nv % 3 == 0, "track %d has bad vertex count %d", t, nv);

        Trace tr = RunRace(t, 90.0, 8.0, scr::kKeyAccelBoost, noAnalog);
        float dist = 0;
        for (size_t i = 1; i < tr.x.size(); i++)
            dist += std::sqrt((tr.x[i] - tr.x[i - 1]) * (tr.x[i] - tr.x[i - 1]) + (tr.z[i] - tr.z[i - 1]) * (tr.z[i] - tr.z[i - 1]));
        bool finite = true;
        for (size_t i = 0; i < tr.x.size(); i++) finite &= std::isfinite(tr.x[i]) && std::isfinite(tr.y[i]) && std::isfinite(tr.z[i]);
        printf("  %-16s verts %5d  ticks %3zu  travelled %7.0f units (%.0f m)  speed %3d  on road %d\n",
               scr::Game::trackName(t).c_str(), nv, tr.x.size(), dist, dist * scr::kDefaultMetresPerUnit,
               tr.last.display_speed, (int)tr.last.touching_road);
        CHECK(finite, "non-finite positions on track %d", t);
        const size_t expect = (size_t)(8.0 * g_game.simHz());
        CHECK(tr.x.size() + 1 >= expect && tr.x.size() <= expect + 1, "expected ~%zu ticks in 8 s, got %zu", expect, tr.x.size());
        CHECK(dist > 1000.0f, "car didn't move on track %d", t);
    }

    // 2. Frame-rate independence ---------------------------------------------------------
    printf("[2] identical physics at different display rates\n");
    {
        Trace ref = RunRace(0, 60.0, 10.0, scr::kKeyAccelBoost | scr::kKeyRight, noAnalog);
        for (double hz : {72.0, 90.0, 120.0, 144.0})
        {
            Trace tr = RunRace(0, hz, 10.0, scr::kKeyAccelBoost | scr::kKeyRight, noAnalog);
            size_t n = std::min(ref.x.size(), tr.x.size());
            float d = MaxDiff(ref, tr, n);
            printf("  %5.0f Hz render: %zu sim ticks, max deviation from 60 Hz run = %g units\n", hz, tr.x.size(), d);
            CHECK(n >= 80 && d == 0.0f, "physics differs at %.0f Hz", hz);
        }
    }

    // 3. Analog steering endpoints ---------------------------------------------------------
    printf("[3] analog steering matches digital at the endpoints\n");
    {
        ScrAnalogInput full = {true, 1.0f, 1.0f, 0.0f, false, 0.08f};
        ScrAnalogInput centred = {true, 0.0f, 1.0f, 0.0f, false, 0.08f};
        ScrAnalogInput half = {true, 0.5f, 1.0f, 0.0f, false, 0.08f};

        Trace digital_right = RunRace(2, 90.0, 10.0, scr::kKeyAccel | scr::kKeyRight, noAnalog);
        Trace digital_none  = RunRace(2, 90.0, 10.0, scr::kKeyAccel, noAnalog);
        Trace analog_full   = RunRace(2, 90.0, 10.0, 0, full);
        Trace analog_centre = RunRace(2, 90.0, 10.0, 0, centred);
        Trace analog_half   = RunRace(2, 90.0, 10.0, 0, half);

        size_t n = std::min({digital_right.x.size(), analog_full.x.size(), analog_centre.x.size(), digital_none.x.size()});
        float d_full = MaxDiff(digital_right, analog_full, n);
        float d_none = MaxDiff(digital_none, analog_centre, n);
        float d_half_r = MaxDiff(digital_right, analog_half, n);
        float d_half_n = MaxDiff(digital_none, analog_half, n);
        printf("  full lock vs digital right: %g   centred vs no steering: %g\n", d_full, d_none);
        printf("  half lock differs from both (right %g, none %g)\n", d_half_r, d_half_n);
        CHECK(d_full == 0.0f, "analog full lock should equal digital right");
        CHECK(d_none == 0.0f, "centred analog should equal no steering");
        CHECK(d_half_r > 0.0f && d_half_n > 0.0f, "half lock should be in between");
    }

    // 4. Audio ----------------------------------------------------------------------------
    printf("[4] mixer produces engine sound while racing\n");
    {
        RunRace(0, 90.0, 3.0, scr::kKeyAccelBoost, noAnalog);
        std::vector<float> buf(4800 * 2);
        ScrMixer::Get().Mix(buf.data(), 4800);
        double rms = 0;
        for (float v : buf) rms += v * v;
        rms = std::sqrt(rms / buf.size());
        printf("  engine RMS level %.4f\n", rms);
        CHECK(rms > 0.001, "no audio output");
    }

    g_game.shutdown();
    printf(g_failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
