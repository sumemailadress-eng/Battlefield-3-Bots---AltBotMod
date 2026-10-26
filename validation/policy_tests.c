#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "../files/BotMod/src/enhanced.h"

static int checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
int main(void)
{
    BMConfig c; bm_defaults(&c);
    CHECK(!c.armored && !c.driverWeapons);
    CHECK(c.driving && c.light && c.boats);
    const BMVehicleProfile *tank = bm_vehicle_profile("Vehicles/M1A2/M1Abrams");
    const BMVehicleProfile *ifv = bm_vehicle_profile("LAV-25");
    CHECK(tank->kind == BM_TANK && tank->muzzleSpeed == 200);
    CHECK(ifv->kind == BM_IFV && fabsf(ifv->gravity - 4.9f) < 0.001f);
    CHECK(bm_vehicle_profile("VDV Buggy")->kind == BM_LIGHT);
    CHECK(bm_vehicle_profile("GAZ-3937_Vodnik")->kind == BM_LIGHT);
    CHECK(bm_vehicle_profile("KLR650")->kind == BM_LIGHT);
    CHECK(bm_vehicle_profile("rhib")->kind == BM_BOAT);
    CHECK(bm_vehicle_profile("2S25_SPRUT-SD")->kind == BM_TANK);
    CHECK(!bm_vehicle_allowed(&c, tank));
    CHECK(!bm_vehicle_allowed(&c, ifv));
    CHECK(bm_set(&c, "ArmoredVehicles", "1") == 1);
    CHECK(bm_vehicle_allowed(&c, tank) && bm_vehicle_allowed(&c, ifv));
    const char *excluded[] = {"AH1Z", "Ka-60", "F16", "F/A-18E", "Venom", "Su-35BM Flanker-E", "Humvee_ASRAD", "VodnikPhoenix", "HIMARS", "EODBot", "unknown vehicle"};
    for (unsigned k = 0; k < sizeof excluded / sizeof excluded[0]; k++) CHECK(!bm_vehicle_allowed(&c, bm_vehicle_profile(excluded[k])));
    const char *extra[] = {"LAV_AD", "9K22_Tunguska_M", "AAV-7A1"};
    for(unsigned k=0;k<3;k++)CHECK(bm_vehicle_allowed(&c,bm_vehicle_profile(extra[k])));
    CHECK(bm_vehicle_profile("AAV-7A1")->kind==BM_AMTRAC);
    CHECK(bm_vehicle_target_score(bm_vehicle_profile("LAV_AD"),BM_TARGET_AIR,400)<bm_vehicle_target_score(bm_vehicle_profile("LAV_AD"),BM_TARGET_GROUND,200));
    CHECK(bm_set(&c, "TankSpeedMps", "8.5") == 1);
    CHECK(fabsf(bm_speed_limit(&c, tank) - 8.5f) < 0.001f);
    BMConfig before = c;
    const char *invalid[] = {"NaN", "inf", "-1", "0", "21", "8oops", "", "1e100", "8 9"};
    for (unsigned k = 0; k < sizeof invalid / sizeof invalid[0]; k++) {
        CHECK(bm_set(&c, "TankSpeedMps", invalid[k]) == -1);
        CHECK(memcmp(&c, &before, sizeof c) == 0);
    }
    CHECK(bm_set(&c, "ArmoredVehicles", "0.5") == -1);
    CHECK(bm_set(&c, "not-a-setting", "1") == 0);
    CHECK(bm_set(&c, "driving", "0") == 1);
    CHECK(!bm_vehicle_allowed(&c, bm_vehicle_profile("GrowlerITV")));
    CHECK(bm_set(&c, "Driving", "1 ") == 1);
    // Normal forward progress checks fire at the configured time; reverse,
    // backing out, and intentional waits cannot trigger a second recovery.
    CHECK(!bm_stuck_due(89, 0, 0, 0, 3));
    CHECK(bm_stuck_due(90, 0, 0, 0, 3));
    CHECK(!bm_stuck_due(90, 0, 1, 0, 3));
    CHECK(!bm_stuck_due(90, 0, 0, 1, 3));
    CHECK(!bm_stuck_due(90, 90, 0, 0, 3));
    CHECK(bm_stuck_due(45, UINT_MAX - 44, 0, 0, 3));
    BMSteering steer = {0};
    CHECK(bm_steer(&steer, 0.5f, 1.0f/30.0f, &c) < 0);
    CHECK(bm_steer(&steer, -0.5f, 1.0f/30.0f, &c) > 0);
    CHECK(bm_set(&c, "SmoothSteering", "1") == 1);
    memset(&steer, 0, sizeof steer);
    for (int k = 0; k < 300; k++) {
        float out = bm_steer(&steer, sinf(k * 0.03f), 1.0f/30.0f, &c);
        CHECK(isfinite(out) && fabsf(out) <= 1);
    }
    CHECK(bm_steer(&steer, NAN, 1.0f/30.0f, &c) == 0);
    float flight, rise;
    bm_ballistics(200, 9.81f, 200, &flight, &rise);
    CHECK(fabsf(flight - 1) < 0.001f && fabsf(rise - 4.905f) < 0.001f);
    bm_ballistics(600, 0, 300, &flight, &rise);
    CHECK(fabsf(flight - 0.5f) < 0.001f && rise == 0);
    bm_ballistics(0, -1, -100, &flight, &rise);
    CHECK(flight == 0 && rise == 0);
    struct Slot { int id; float points[3]; } slots[4] = {{10,{1,2,3}},{20,{4,5,6}},{30,{7,8,9}},{40,{10,11,12}}};
    bm_forget_slot(slots, sizeof slots[0], 1, 4);
    CHECK(slots[0].id == 10 && slots[1].id == 30 && slots[2].id == 40 && slots[3].id == 0);
    CHECK(slots[1].points[2] == 9);
    bm_forget_slot(slots, sizeof slots[0], 2, 3);
    CHECK(slots[2].id == 0);
    bm_forget_slot(slots, sizeof slots[0], -1, 2);
    CHECK(slots[0].id == 10 && slots[1].id == 30);
    printf("PASS: %d vehicle/config/recovery/steering/ballistic/compaction checks\n", checks);
    return 0;
}
