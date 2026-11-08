// vr_controls.cpp - see vr_controls.h

#include "vr_controls.h"
#include <algorithm>
#include <cmath>

namespace scr {

static float WrapPi(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

float VrWheel::steer() const
{
    float half = lock_deg * 0.5f * 3.14159265f / 180.0f;
    return std::max(-1.0f, std::min(1.0f, angle / half));
}

void VrWheel::update(const XrInputs &in, bool racing, float dt, InputFrame &f)
{
    if (!in.active) return;
    const float half = lock_deg * 0.5f * 3.14159265f / 180.0f;

    // Hand angle around the wheel centre, in the wheel plane (x right, y up), clockwise positive
    auto handAngle = [&](int h) {
        vm::vec3 d = in.hand_pose[h].position - centre;
        return -std::atan2(d.y, d.x);
    };

    int hands = 0;
    for (int h = 0; h < 2; h++)
        if (in.hand_valid[h] && in.grip[h] > 0.55f) hands |= (1 << h);

    if (racing && hands)
    {
        if (hands == 3)
        {
            // Two hands: the wheel angle follows the line between the hands
            vm::vec3 d = in.hand_pose[kRight].position - in.hand_pose[kLeft].position;
            float a = -std::atan2(d.y, d.x);
            if (held_hands != 3) two_hand_offset = angle - a;   // no jump when the second hand grabs
            angle = a + two_hand_offset;
            // let go of the offset slowly so a level grip means a straight wheel
            two_hand_offset *= std::max(0.0f, 1.0f - dt * 0.5f);
        }
        else
        {
            int h = (hands & 2) ? 1 : 0;
            float a = handAngle(h);
            if (held_hands != hands) { grab_hand_angle = a; grab_wheel_angle = angle; }
            angle = grab_wheel_angle + WrapPi(a - grab_hand_angle);
        }
        held = true;
    }
    else
    {
        held = false;
        // Thumbstick fallback (left stick), otherwise self-centre the virtual wheel
        float sx = in.stick_x[kLeft];
        if (racing && std::fabs(sx) > 0.15f)
            angle = sx * half;
        else
            angle *= std::max(0.0f, 1.0f - dt * 6.0f);
    }
    angle = std::max(-half, std::min(half, angle));
    held_hands = racing ? hands : 0;

    if (racing)
    {
        float th = in.trigger[kRight], br = in.trigger[kLeft];
        bool steering = held || std::fabs(in.stick_x[kLeft]) > 0.15f;
        if (steering || th > 0.05f || br > 0.05f || in.a)
        {
            // VR controllers only replace a quieter analog device
            if (!f.analog.active || (!f.has_wheel && std::fabs(steer()) >= std::fabs(f.analog.steer)))
            {
                f.analog.active = true;
                f.analog.steer = steer();
                f.analog.steer_deadzone = 0.03f;
            }
            f.analog.throttle = std::max(f.analog.throttle, th);
            f.analog.brake = std::max(f.analog.brake, br);
            f.analog.boost = f.analog.boost || in.a;
        }
        if (in.b_pressed) f.turn_around = true;
        if (in.x_pressed) f.toggle_outside = true;
        if (in.menu_pressed) f.pause = true;
    }
    else
    {
        float sy = std::fabs(in.stick_y[kLeft]) > std::fabs(in.stick_y[kRight]) ? in.stick_y[kLeft] : in.stick_y[kRight];
        if (!menu_stick_held && sy > 0.6f) f.up = true;
        if (!menu_stick_held && sy < -0.6f) f.down = true;
        menu_stick_held = std::fabs(sy) > 0.4f;
        static bool trig_held = false;
        bool trig = in.trigger[kRight] > 0.75f || in.trigger[kLeft] > 0.75f;
        if (trig && !trig_held) f.select = true;
        trig_held = trig;
        if (in.a_pressed) f.select = true;
        if (in.menu_pressed || in.b_pressed) f.back = true;
    }
    if (in.y_pressed) f.recenter = true;
}

} // namespace scr
