#pragma once
#include <cstdint>

enum FanDirection : uint8_t {
    DIR_NORMAL = 0,
    DIR_REVERSE = 1
};

struct FanAction {
    uint8_t speed;        // 0-6
    FanDirection dir;
};

// Resultat d'une decision : action + explication
struct Decision {
    FanAction action;
    bool changed;         // true si nouvelle commande a appliquer
    const char* reason;
};

enum LightMode : uint8_t {
    LIGHT_OFF = 0,
    LIGHT_WARM,
    LIGHT_NEUTRAL,
    LIGHT_COOL
};
