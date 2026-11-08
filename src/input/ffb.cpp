// ffb.cpp - force feedback synthesis (see ffb.h)

#include "ffb.h"
#include <algorithm>
#include <cmath>

namespace scr {

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

FfbOutput FfbModel::update(const Game &game, float wheel_pos, float dt, const FfbSettings &s)
{
    FfbOutput o;
    m_time += dt;
    if (!s.enabled) return o;

    const Mode mode = game.mode();
    if (mode != Mode::Racing || game.paused())
    {
        // Menus: gently centre the wheel, nothing else
        o.spring = 0.35f * s.gain;
        o.damper = 0.15f * s.gain;
        m_jolt_t = m_hit_t = 0.0f;
        m_rumble_low = m_rumble_high = 0.0f;
        return o;
    }

    const ScrCarTelemetry &t = game.telemetry();

    // New simulation tick: pick up event pulses
    if (game.tickCount() != m_last_tick)
    {
        m_last_tick = game.tickCount();
        if (t.landing_impact > 0.0f)
        {
            m_jolt_mag = clampf(t.landing_impact, 0.15f, 1.0f);
            m_jolt_len = 0.10f + 0.10f * m_jolt_mag;
            m_jolt_t = m_jolt_len;
            m_rumble_low = std::max(m_rumble_low, m_jolt_mag);
        }
        if (t.car_collision)
        {
            m_hit_t = 0.12f;
            m_hit_dir = (t.yaw_rate >= 0) ? 1.0f : -1.0f;
            m_rumble_high = std::max(m_rumble_high, 0.9f);
            m_rumble_low = std::max(m_rumble_low, 0.5f);
        }
    }

    const float speed = clampf(std::fabs((float)t.z_speed) / (120.0f * 256.0f), 0.0f, 1.3f);
    const bool grounded = t.touching_road && !t.on_chains;
    const bool airborne = !t.touching_road && !t.on_chains;

    // Centring: light when slow or airborne, firmer with speed
    float spring = 0.0f, damper = 0.0f;
    if (t.on_chains)        { spring = 0.30f; damper = 0.20f; }
    else if (airborne)      { spring = 0.04f; damper = 0.05f; }   // the wheel goes light in the air
    else                    { spring = 0.12f + 0.68f * std::min(speed, 1.0f); damper = 0.10f + 0.15f * speed; }
    spring *= s.centering;
    // smooth changes so landings don't snap the stiffness
    m_smoothed_spring += (spring - m_smoothed_spring) * clampf(dt * 12.0f, 0.0f, 1.0f);
    o.spring = m_smoothed_spring;
    o.damper = damper;

    // Aligning torque: the physics' yaw acceleration (how hard the car is being turned)
    // pushes back against the steering input, so cornering loads the wheel up.
    float constant = 0.0f;
    if (grounded)
    {
        float yaw_acc = clampf((float)t.yaw_acceleration / 300.0f, -1.0f, 1.0f);
        constant += -0.25f * yaw_acc * std::min(speed, 1.0f);
        // lose some centring when the car isn't following the steering (sliding)
        float lag = clampf(std::fabs((float)(t.target_yaw_rate - t.yaw_rate)) / 600.0f, 0.0f, 1.0f);
        o.spring *= 1.0f - 0.4f * lag;
    }

    // Landing jolt: a short down-up-down shake
    if (m_jolt_t > 0.0f)
    {
        float ph = 1.0f - m_jolt_t / std::max(0.001f, m_jolt_len);
        float env = 1.0f - ph;
        constant += std::sin(ph * 3.0f * 3.14159265f) * m_jolt_mag * env * 0.9f * s.impacts;
        m_jolt_t -= dt;
    }
    // Car to car hit: a sharp sideways shove
    if (m_hit_t > 0.0f)
    {
        constant += m_hit_dir * 0.7f * s.impacts;
        m_hit_t -= dt;
    }
    o.constant = clampf(constant, -1.0f, 1.0f);

    // Surface vibration
    float periodic = 0.0f, hz = 20.0f;
    if (grounded)
    {
        periodic = 0.03f * speed;   // subtle road texture
        hz = 10.0f + 25.0f * speed;
        if (t.off_road) { periodic = 0.20f + 0.25f * speed; hz = 24.0f; }
        if (t.wrecked)  { periodic = 0.30f + 0.20f * speed; hz = 38.0f; }   // scraping on the chassis
    }
    o.periodic = clampf(periodic * s.road, 0.0f, 1.0f);
    o.periodic_hz = hz;

    // Controller rumble (gamepads / VR controllers)
    float base_low = grounded ? (t.off_road ? 0.35f : 0.04f + 0.06f * speed) : 0.0f;
    float base_high = (grounded && t.wrecked) ? 0.5f : (t.boost_active ? 0.15f : 0.0f);
    o.rumble_low = clampf(std::max(base_low * s.road, m_rumble_low * s.impacts), 0.0f, 1.0f);
    o.rumble_high = clampf(std::max(base_high * s.road, m_rumble_high * s.impacts), 0.0f, 1.0f);
    m_rumble_low = std::max(0.0f, m_rumble_low - dt * 3.0f);
    m_rumble_high = std::max(0.0f, m_rumble_high - dt * 4.0f);

    // Overall gain
    o.constant *= s.gain;
    o.spring = clampf(o.spring * s.gain, 0.0f, 1.0f);
    o.damper = clampf(o.damper * s.gain, 0.0f, 1.0f);
    o.periodic *= s.gain;
    (void)wheel_pos;
    return o;
}

} // namespace scr
