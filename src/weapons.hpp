#pragma once
#include "common.hpp"

struct WeaponDef {
    const char* name; const char* file;
    float dmg, rpm; int mag, reserve; float reload;
    float spread, adsSpread; int pellets; float recoil; bool autoFire;
    float range, adsFov;
    Vector3 hip, ads;
    float len;
};

static const WeaponDef WDEFS[4] = {
    {"SCAR",    "scar",    24,  600, 30, 180, 2.1f, 0.022f, 0.006f, 1,  1.1f, true,  120, 48, {0.20f, -0.17f, -0.62f}, {0.0f, -0.088f, -0.50f}, 0.95f},
    {"SHOTGUN", "shotgun", 11,   70,  8,  40, 2.6f, 0.060f, 0.040f, 10, 4.5f, false,  50, 52, {0.20f, -0.17f, -0.66f}, {0.0f, -0.085f, -0.52f}, 1.00f},
    {"SNIPER",  "sniper", 110,   45,  5,  25, 3.2f, 0.030f, 0.0004f, 1, 5.5f, false, 300, 12, {0.20f, -0.19f, -0.80f}, {0.0f, -0.120f, -0.70f}, 1.35f},
    {"TEC-9",   "tec9",    14,  900, 32, 192, 1.7f, 0.040f, 0.016f, 1,  0.8f, true,   60, 54, {0.20f, -0.15f, -0.50f}, {0.0f, -0.070f, -0.42f}, 0.55f},
};
