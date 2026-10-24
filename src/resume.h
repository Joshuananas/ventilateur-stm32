#pragma once
#include <Arduino.h>

// Persistance de l'etat de controle dans les registres de sauvegarde
// (RTC->BKP*) : survit aux resets sans coupure d'alimentation, mais pas
// a une perte totale de VBAT. Le format du mot 32 bits est fige :
//   bit0 manuel, bit1 etat connu, bit2 ventilateur allume,
//   bits3-5 vitesse, bit6 sens inverse, bits7-8 mode lumiere.
// BKP0R = magie de validite, BKP1R = etape atteinte (checkpoint),
// BKP2R = etat, BKP3R = temps manuel restant, BKP4R = ~BKP2R.
namespace Resume {

struct Snapshot {
    bool manual;
    bool known;
    bool powerOn;
    uint8_t speed;
    bool reverse;
    uint8_t light;
    uint32_t manualRemainingMs;
};

// Active l'acces au domaine de sauvegarde (horloges PWR/RTC + acces BKP).
void initBackupDomain();

// Vrai si la reprise est possible : pas de POR/BOR dans rccCsr, magie
// presente et complement de l'etat coherent.
bool valid(uint32_t rccCsr);

// Etape atteinte avant le reset, ou 0xFFFFFFFF si la magie est absente.
uint32_t previousStage();

// Instantane decode de l'etat sauvegarde (a n'appeler que si valid()).
Snapshot snapshot();

// Etape courante a restaurer en cas de reset (BKP1R).
void checkpoint(uint32_t stage);

// Sauvegarde l'etat de controle et le temps manuel restant, puis pose
// la magie de validite. manualRemainingMs = 0 en mode AUTO.
void retain(const Snapshot& s);

} // namespace Resume
