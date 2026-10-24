#pragma once
#include <Arduino.h>

// Effets LED embarquee (PWM logiciel ~500 Hz, non bloquant).
//  AUTO    : respiration d'humain endormi ~7 s — inspiration lente
//            (ease-in-out, pic doux), retombee exponentielle, puis
//            ~2 s d'apnee completement eteinte entre chaque souffle
//  MANUEL  : battement de coeur "lub-dub" (deux pulsations, cycle 1.4 s)
//  RF vue  : tir laser — flash immediat + retombee exponentielle
//  Apprentissage : pulse fort ~1.6 s (le STM32 "a compris" quelque
//            chose : observation evaluee, connaissance figee/defigee)
//  Erreur  : clignotement dur 4 Hz (capteur ou radio absent)

void ledFxInit();
// A appeler a chaque tour de loop(). fault = capteur/radio absent,
// manual = mode MANUEL actif (prioritaire sur la respiration AUTO).
void ledFxTick(bool fault, bool manual);
// Activite telecommande detectee (tir laser, duree parametrable).
void ledFxRfPulse(uint32_t durationMs);
// Evenement d'apprentissage (pulse "compris" ~1.6 s).
void ledFxThinkPulse();
