#pragma once
#include <Arduino.h>
#include <Adafruit_BME280.h>
#include "learning.h"
#include "cc1101.h"
#include "fanstate.h"

// Etat partage entre l'orchestration (main.cpp) et le controleur
// (commands/telemetry). Les definitions vivent dans controller.cpp.

enum CtrlMode : uint8_t { MODE_AUTO, MODE_MANUAL };

// Retour automatique en mode AUTO
#define MANUAL_TIMEOUT_MS (8UL*3600UL*1000UL)  // 8 h

// Sauvegarde flash : au plus 1 ecriture / 10 min, si modifie
#define STATE_SAVE_PERIOD_MS (10UL*60UL*1000UL)

extern Adafruit_BME280 bme;
extern bool bmeOk;
extern float lastPressure;

extern FanLearner learner;
extern CC1101 radio;
extern bool radioOk;

extern CtrlMode ctrlMode;
extern uint32_t manualSince;

extern FanState fan;
extern FanAction manualAction;

// Horloge approximative + meteo (pousses par la page web)
extern uint32_t epochBase;   // epoch Unix a la reception de timesync
extern uint32_t epochMillis; // millis() a la reception de timesync
extern bool timeSynced;
extern float meteoT, meteoH, meteoRad;

// Sauvegarde flash : etat modifie depuis la derniere ecriture
extern bool stateDirty;
extern uint32_t lastFlashSave;

const char* dirName(FanDirection d);
uint32_t nowEpoch();
uint8_t currentMonth();
void enterManual();
uint32_t manualLeftMin();
void printState();
void printStatus(float temp, float hum);
void applyFan(const FanAction& a);
void saveFlash();
void handleCommand(const String& cmd, float temp, float hum);
