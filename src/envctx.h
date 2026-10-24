#pragma once
#include <Arduino.h>

// Estimation du contexte environnemental sans horloge ni meteo :
// - saison : moyenne lente de la temperature interieure (~6 h)
// - gain solaire : pente de T sur ~10 min en journee
// - signature "four/cuisine" : T monte vite ET humidite monte
//   (contraire du soleil qui fait baisser l'humidite relative)
// - signature "aeration" : chute rapide de T (>0.5C/10min), trop vite
//   pour l'inertie de la maison -> fenetre ouverte / air exterieur
struct EnvContext {
    float seasonEma = NAN;
    float solarRefT = NAN, solarRefH = NAN;
    uint32_t solarRefTs = 0;
    float solarSlope = 0;   // degres C sur les dernieres ~10 min
    float humidSlope = 0;   // % HR sur la meme fenetre
    bool cookingNow = false;
    bool airingNow = false;

    enum Event : uint8_t {
        EV_NONE = 0,
        EV_COOKING = 1, // debut de cuisson detecte
        EV_AIRING = 2,  // debut d'aeration detecte
    };

    // Integre un echantillon capteur. Retourne les evenements declenches
    // (transition cuisson/aeration), a traiter par l'appelant.
    uint8_t noteSample(float temp, float hum, uint32_t nowMs);
};

extern EnvContext env;
