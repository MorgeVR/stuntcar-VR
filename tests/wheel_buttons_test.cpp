// wheel_buttons_test.cpp - wheel button setup and mapping
#include "wheel_buttons.h"
#include <cstdio>
using namespace scr;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } } while (0)

int main()
{
    printf("Wheel button tests\n");
    WheelButtons wb;
    // before setup: no actions, any button boosts
    bool down[24] = {};
    down[7] = true;
    CHECK(wb.press(7) == -1 && wb.boostHeld(down, 24), "unconfigured: any button boosts, nothing else");

    wb.startSetup();
    printf("  %s\n", wb.prompt().c_str());
    wb.press(4);            // boost
    wb.press(6);            // view
    wb.press(7);            // turn around
    wb.tick(9.0f);          // pause: skipped (timeout)
    wb.press(6);            // menu: re-uses 6 -> view loses it
    wb.press(19);           // select
    wb.press(20);           // recentre
    printf("  mapping %s\n", wb.toString().c_str());
    CHECK(!wb.setup_active && wb.configured && wb.changed, "setup should finish and mark changed");
    CHECK(wb.map[kWbBoost] == 4 && wb.map[kWbTurnAround] == 7 && wb.map[kWbPause] == -1, "assignments");
    CHECK(wb.map[kWbMenu] == 6 && wb.map[kWbView] == -1, "a button belongs to one action only");
    CHECK(wb.press(7) == kWbTurnAround && wb.press(19) == kWbSelect && wb.press(3) == -1, "presses map to actions");
    down[7] = false; down[4] = true;
    CHECK(wb.boostHeld(down, 24), "boost button held");
    down[4] = false; down[12] = true;
    CHECK(!wb.boostHeld(down, 24), "other buttons don't boost once boost is assigned");

    WheelButtons copy;
    copy.fromString(wb.toString());
    CHECK(copy.toString() == wb.toString(), "settings round trip");
    printf(g_fail ? "%d FAILURE(S)\n" : "ALL TESTS PASSED\n", g_fail);
    return g_fail;
}
