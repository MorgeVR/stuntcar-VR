// pedal.h - turns a raw joystick axis into pedal pressure (0 = released, 1 = floored).
//
// Pedals rest at one end of the axis; which end depends on the wheel and driver
// (G27 on Windows rests at +32767, others at -32768). Axes also read 0 until the
// device sends its first report, so the rest position cannot be taken from the
// first reading. Instead the rest end is the first extreme the axis is seen at;
// until then the pedal reads as released.
#pragma once

#include <cmath>

namespace scr {

struct PedalAxis
{
    int rest_sign = 0;          // 0 = not known yet, +1 = rests at +32767, -1 = rests at -32768

    bool known() const { return rest_sign != 0; }

    float update(int v)
    {
        if (rest_sign == 0)
        {
            if (v > 28000) rest_sign = 1;
            else if (v < -28000) rest_sign = -1;
            else return 0.0f;
        }
        const float rest = rest_sign > 0 ? 32767.0f : -32768.0f;
        float p = std::fabs((float)v - rest) / 65535.0f;
        p = (p - 0.03f) / 0.94f;   // small dead zone at both ends
        return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    }
};

} // namespace scr
