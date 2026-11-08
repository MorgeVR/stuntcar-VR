// pedal_test.cpp - pedal calibration, including a wheel that reads 0 before its first report
#include "pedal.h"
#include <cstdio>
using namespace scr;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } } while (0)

int main()
{
    printf("Pedal tests\n");
    // G27 on Windows: axis reads 0 before the first report, then rests at +32767
    {
        PedalAxis p;
        float first = p.update(0);           // device not reported yet
        float rest = p.update(32767);        // pedal released
        float half = p.update(0);            // pedal half pressed
        float floored = p.update(-32768);    // pedal floored
        printf("[G27 style] before report %.2f, released %.2f, half %.2f, floored %.2f\n", first, rest, half, floored);
        CHECK(first == 0.0f, "unknown rest must read as released");
        CHECK(rest == 0.0f, "released pedal must read 0");
        CHECK(half > 0.45f && half < 0.55f, "half pressed should read ~0.5");
        CHECK(floored == 1.0f, "floored should read 1");
    }
    // Wheels whose pedals rest at -32768
    {
        PedalAxis p;
        float rest = p.update(-32768);
        float floored = p.update(32767);
        CHECK(rest == 0.0f && floored == 1.0f, "inverted pedals");
    }
    // Axis that never reaches an end (e.g. a centred, unused axis) never reads as pressed
    {
        PedalAxis p;
        float v = 0;
        for (int i = -20000; i <= 20000; i += 1000) v = p.update(i);
        CHECK(v == 0.0f && !p.known(), "a centred axis must not become a pressed pedal");
    }
    printf(g_fail ? "%d FAILURE(S)\n" : "ALL TESTS PASSED\n", g_fail);
    return g_fail;
}
