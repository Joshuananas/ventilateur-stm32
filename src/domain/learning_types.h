#pragma once
#include "fan_types.h"

struct Observation {
    uint32_t timestamp;   // millis() de l'action
    FanAction action;
    float tempBefore;
    float tempAfter;      // mesuree 12 min plus tard
    float humBefore;      // hygrometrie au moment de l'action
    float delta;          // tempAfter - tempBefore - derive attendue
    float expected;       // derive predite sans action (sur 12 min)
};

struct StoredPreference {
    float temp, hum;
    uint8_t speed, dir, votes, winter;
};
