// ffb.h - Force feedback / haptics synthesis from Stunt Car Racer physics telemetry.
//
// The original physics has no tyre model, so steering forces are synthesised from what
// the simulation does know: speed, whether the wheels are on the road, steering and yaw
// response, hard landings, car-to-car hits, off-road driving and scraping when wrecked.
// FfbModel is pure logic (unit tested); FfbDevice (ffb_sdl.cpp) sends the result to a
// force feedback wheel via SDL3, or to gamepad rumble.
#pragma once

#include "car_behaviour.h"
#include "game.h"

namespace scr {

struct FfbSettings
{
    bool  enabled = true;
    float gain = 0.8f;        // overall strength 0..1
    float centering = 1.0f;   // speed-dependent self-centring
    float road = 1.0f;        // surface vibration (off road, scraping, joins)
    float impacts = 1.0f;     // landings and collisions
};

struct FfbOutput
{
    float constant = 0.0f;      // -1..1, + pushes the wheel to the right
    float spring = 0.0f;        // 0..1 centring stiffness
    float damper = 0.0f;        // 0..1
    float periodic = 0.0f;      // 0..1 vibration magnitude on the wheel
    float periodic_hz = 20.0f;
    float rumble_low = 0.0f;    // 0..1 for gamepads / VR controllers (heavy motor)
    float rumble_high = 0.0f;   // 0..1 (light motor)
};

class FfbModel
{
public:
    // Call once per rendered frame. wheel_pos: current steering input -1..1.
    FfbOutput update(const Game &game, float wheel_pos, float dt, const FfbSettings &s);

    // Exposed for tests
    float joltRemaining() const { return m_jolt_t; }

private:
    uint32_t m_last_tick = 0;
    float m_jolt_t = 0.0f, m_jolt_len = 0.0f, m_jolt_mag = 0.0f;
    float m_hit_t = 0.0f, m_hit_dir = 1.0f;
    float m_rumble_low = 0.0f, m_rumble_high = 0.0f;
    float m_time = 0.0f;
    float m_smoothed_spring = 0.3f;
};

} // namespace scr
