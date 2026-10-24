#include "fanstate.h"
#include "domain/remote_buttons.h"

// Ordre de cycle de la lumiere : chaud -> neutre -> froid -> eteint
static const LightMode LIGHT_CYCLE[4] = {LIGHT_WARM, LIGHT_NEUTRAL, LIGHT_COOL, LIGHT_OFF};

void FanState::applyButton(uint8_t slot)
{
    if (slot >= RemoteButton::SPEED_FIRST && slot <= RemoteButton::SPEED_LAST) {
        known = true;
        powerOn = true;
        speed = slot;
    } else if (slot == RemoteButton::OFF) {
        // Bouton central = OFF uniquement (inerte quand le ventilateur est deja eteint)
        known = true;
        powerOn = false;
        speed = 0;
    } else if (slot == RemoteButton::DIRECTION) {
        known = true;
        reverse = !reverse;
    } else if (slot == RemoteButton::LIGHT) {
        known = true;
        LightMode next = LIGHT_CYCLE[0];
        for (uint8_t i = 0; i < 4; i++) {
            if (LIGHT_CYCLE[i] == light) { next = LIGHT_CYCLE[(i + 1) % 4]; break; }
        }
        light = next;
    }
    // 9 (moins), 10 (plus), 11/12/13 (timers) : pas d'effet sur l'etat suivi
}

void FanState::sync(bool power, uint8_t spd, bool rev, LightMode l)
{
    known = true;
    powerOn = power;
    speed = spd;
    reverse = rev;
    light = l;
}

const char* FanState::lightName() const
{
    switch (light) {
        case LIGHT_WARM:    return "WARM";
        case LIGHT_NEUTRAL: return "NEUTRAL";
        case LIGHT_COOL:    return "COOL";
        default:            return "OFF";
    }
}
