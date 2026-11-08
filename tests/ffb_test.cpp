// ffb_test.cpp - force feedback synthesis driven by real races (no hardware needed).

#include "ffb.h"
#include "game.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace scr;

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } } while (0)

int main()
{
    printf("Stunt Car Racer VR - force feedback tests\n");
    Game game;
    if (!game.init(48000)) { printf("FAIL: init\n"); return 1; }
    FfbSettings s;
    const float dt = 1.0f / 90.0f;

    // Menu: gentle centring only
    {
        FfbModel m;
        FfbOutput o = m.update(game, 0.0f, dt, s);
        printf("[menu] spring %.2f constant %.2f\n", o.spring, o.constant);
        CHECK(o.spring > 0.1f && o.constant == 0.0f, "menu should only centre the wheel");
    }

    // High Jump: accelerate flat out and watch the jump
    srand(7);
    game.selectTrack(6);
    game.startPreview();
    game.update(0.0);
    game.startRace();
    game.setDigitalInput(kKeyAccelBoost);
    FfbModel m;
    float spring_slow = -1, spring_fast = 0, spring_air = 1, max_jolt = 0, max_rumble = 0;
    int air_frames = 0, ground_fast_frames = 0;
    for (int i = 0; i < 90 * 40; i++)
    {
        game.update(dt);
        FfbOutput o = m.update(game, 0.0f, dt, s);
        const ScrCarTelemetry &t = game.telemetry();
        float speed = std::fabs((float)t.z_speed) / (120.0f * 256.0f);
        if (t.touching_road && !t.on_chains)
        {
            if (speed < 0.2f && spring_slow < 0) spring_slow = o.spring;
            if (speed > 0.6f) { spring_fast = std::fmax(spring_fast, o.spring); ground_fast_frames++; }
        }
        if (!t.touching_road && !t.on_chains) { spring_air = std::fmin(spring_air, o.spring); air_frames++; }
        max_jolt = std::fmax(max_jolt, std::fabs(o.constant));
        max_rumble = std::fmax(max_rumble, o.rumble_low);
    }
    printf("[race] spring slow %.2f, fast %.2f, airborne %.2f (%d airborne frames)\n", spring_slow, spring_fast, spring_air, air_frames);
    printf("[race] max constant force %.2f, max rumble %.2f\n", max_jolt, max_rumble);
    CHECK(spring_fast > spring_slow, "centring should firm up with speed");
    CHECK(air_frames > 0, "expected the car to be airborne on High Jump");
    CHECK(spring_air < 0.2f, "wheel should go light in the air");
    CHECK(max_jolt > 0.2f, "landings should jolt the wheel");
    CHECK(max_rumble > 0.2f, "landings should rumble gamepads / controllers");

    // Disabled
    {
        FfbSettings off; off.enabled = false;
        FfbModel m2;
        FfbOutput o = m2.update(game, 0.0f, dt, off);
        CHECK(o.spring == 0.0f && o.constant == 0.0f && o.rumble_low == 0.0f, "disabled FFB must output nothing");
    }

    game.shutdown();
    printf(g_fail ? "%d FAILURE(S)\n" : "ALL TESTS PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
