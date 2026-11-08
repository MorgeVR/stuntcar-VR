// vr_controls_test.cpp - the VR virtual wheel, including with a physical wheel plugged in
#include "vr_controls.h"
#include <cmath>
#include <cstdio>

using namespace scr;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } } while (0)

// Hands on a 0.18 m radius wheel at `turn` radians clockwise
static XrInputs Hands(float turn, float grip_l, float grip_r)
{
    XrInputs in;
    in.active = true;
    vm::vec3 c(0.0f, -0.23f, -0.44f);
    const float r = 0.18f;
    in.hand_pose[kLeft].position = c + vm::vec3(-std::cos(turn) * r, std::sin(turn) * r, 0);
    in.hand_pose[kRight].position = c + vm::vec3(std::cos(turn) * r, -std::sin(turn) * r, 0);
    in.hand_valid[0] = in.hand_valid[1] = true;
    in.grip[kLeft] = grip_l;
    in.grip[kRight] = grip_r;
    return in;
}

// What the input system reports for an idle G27 that is plugged in
static InputFrame G27Idle()
{
    InputFrame f;
    f.has_wheel = true;
    f.analog.active = true;
    f.analog.steer = 0.0f;
    return f;
}

int main()
{
    printf("VR controls tests\n");
    const float dt = 1.0f / 90.0f;

    // 1. two hands (a ~70 degree turn with a 180 degree wheel = ~75% lock), physical wheel plugged in: turning right steers right
    {
        VrWheel w;
        InputFrame f;
        for (int i = 0; i <= 30; i++)
        {
            f = G27Idle();
            w.update(Hands(1.2f * i / 30.0f, 1, 1), true, dt, f);
        }
        printf("[two hands, G27 plugged in] steer %.2f, vr_steering %d\n", f.analog.steer, (int)f.vr_steering);
        CHECK(f.vr_steering && f.analog.steer > 0.6f, "gripping should steer right even with a wheel plugged in");
        // and left
        for (int i = 0; i <= 60; i++)
        {
            f = G27Idle();
            w.update(Hands(1.2f - 2.4f * i / 60.0f, 1, 1), true, dt, f);
        }
        printf("[two hands, turned left] steer %.2f\n", f.analog.steer);
        CHECK(f.analog.steer < -0.6f, "turning the hands left should steer left");
        // let go: the physical wheel takes over again
        f = G27Idle();
        w.update(Hands(0.0f, 0, 0), true, dt, f);
        CHECK(!f.vr_steering && f.analog.steer == 0.0f, "releasing the grips should give control back to the wheel");
    }

    // 2. one hand (right) only
    {
        VrWheel w;
        InputFrame f;
        for (int i = 0; i <= 30; i++)
        {
            f = InputFrame();
            w.update(Hands(1.0f * i / 30.0f, 0, 1), true, dt, f);
        }
        printf("[right hand only] steer %.2f\n", f.analog.steer);
        CHECK(f.analog.steer > 0.5f, "one-handed steering should work");
    }

    // 3. thumbstick
    {
        VrWheel w;
        InputFrame f = G27Idle();
        XrInputs in = Hands(0, 0, 0);
        in.stick_x[kLeft] = -1.0f;
        w.update(in, true, dt, f);
        printf("[left stick] steer %.2f\n", f.analog.steer);
        CHECK(f.analog.steer < -0.9f, "left thumbstick should steer");
    }

    // 4. triggers
    {
        VrWheel w;
        InputFrame f = G27Idle();
        XrInputs in = Hands(0, 0, 0);
        in.trigger[kRight] = 0.8f;
        w.update(in, true, dt, f);
        CHECK(f.analog.throttle >= 0.8f && f.analog.steer == 0.0f, "trigger accelerates without stealing steering");
    }

    printf(g_fail ? "%d FAILURE(S)\n" : "ALL TESTS PASSED\n", g_fail);
    return g_fail;
}
