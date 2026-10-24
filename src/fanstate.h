#pragma once
#include "domain/fan_types.h"

// Suivi de l'etat du ventilateur (derniere commande connue, envoyee ou entendue)

struct FanState {
    bool known = false;        // false tant qu'aucun sync ni bouton vu
    bool powerOn = false;
    uint8_t speed = 0;         // derniere vitesse connue 1..6 (0 = inconnue)
    bool reverse = false;
    LightMode light = LIGHT_OFF;

    void applyButton(uint8_t slot); // met a jour selon le bouton (emetteur ou telecommande)
    void sync(bool power, uint8_t spd, bool rev, LightMode l);
    const char* lightName() const;
};
