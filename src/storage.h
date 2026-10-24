#pragma once
#include <Arduino.h>
#include "cc1101.h"
#include "fanstate.h"
#include "learning.h"
#include "persistence/storage_format.h"

// Persistance en flash interne, redondance A/B :
//   copie A : secteur 7 (0x08060000)   copie B : secteur 5 (0x08020000)
// Chaque copie = en-tete {CPY1, seq} + blob identique. save() ecrit dans
// l'autre secteur : une coupure pendant l'operation laisse toujours une
// copie coherente. L'ancien format (blob seul en A) est relu comme seq=0.
// Le blob (~16,5 Ko) n'est jamais instancie en RAM : lecture par pointeur
// direct sur la flash mappee, ecriture mot par mot generee a la volee.

namespace Storage {
    bool load(CC1101& radio, FanState& fan, FanLearner& learner);
    bool save(const CC1101& radio, const FanState& fan, const FanLearner& learner);
    void erase();
    bool valid();          // magic + crc corrects en flash

    // Journal de boot : appende dans la zone libre du secteur 7 SANS
    // effacer (flash vierge = 0xFF, on ne fait que programmer des mots).
    // Survit aux resets ; disparait au prochain save() (erase secteur).
    void     logBoot(uint32_t rccFlags, uint32_t stage = 0xFFFFFFFFUL);   // enregistre cause du reset
    void     dumpBootLog();                // 'bootlog' : liste les boots
    uint32_t bootLogCount();               // nb d'enregistrements
    void     logClear();                   // 'logclear' : efface le journal
    const char* resetCause(uint32_t rccFlags); // nomme le drapeau dominant
}
