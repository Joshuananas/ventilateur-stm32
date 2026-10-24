#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BME280.h>
#include <IWatchdog.h>
#include "learning.h"
#include "cc1101.h"
#include "fanstate.h"
#include "storage.h"
#include "pins.h"
#include "domain/remote_buttons.h"
#include "controller.h"
#include "ledfx.h"
#include "envctx.h"
#include "resume.h"
#include "safe_serial.h"

#define DECISION_PERIOD_MS 5000
#define STATUS_PERIOD_MS   5000
#define TEST_WINDOW_MS     5000

#define COMBO_SLOT        RemoteButton::TIMER_1H // bouton 1H
#define COMBO_WINDOW_MS   3000                  // double appui < 3 s -> AUTO

static void retainControl()
{
    Resume::Snapshot s;
    s.manual = ctrlMode == MODE_MANUAL;
    s.known = fan.known;
    s.powerOn = fan.powerOn;
    s.speed = fan.speed;
    s.reverse = fan.reverse;
    s.light = fan.light;
    s.manualRemainingMs = s.manual
        ? MANUAL_TIMEOUT_MS - min(uint32_t(millis() - manualSince), uint32_t(MANUAL_TIMEOUT_MS)) : 0;
    Resume::retain(s);
}

void setup()
{
    ledFxInit();

    Serial.begin(115200);

    // Cause du reset (RCC->CSR) : journal persistant + affichage
    uint32_t rcc = RCC->CSR;
    Resume::initBackupDomain();
    uint32_t previousStage = Resume::previousStage();
    bool resume = Resume::valid(rcc);
    Serial.printf("[BOOT] reset : %s\n", Storage::resetCause(rcc));
    __HAL_RCC_CLEAR_RESET_FLAGS();

    // Fenetre de 5 s : "TEST" -> mode manuel
    Serial.println("Envoyez 'TEST' dans les 5 s pour le mode manuel.");
    uint32_t t0 = millis();
    String boot = "";
    while (millis() - t0 < TEST_WINDOW_MS) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (boot == "TEST") {
                    enterManual();
                    Serial.println("*** MODE MANUEL ***");
                    Serial.println("Commandes : s1..s6, rev, off, auto, manual, status, sync, rxstat, sniff, learn, play, rftest");
                }
                boot = "";
            } else {
                boot += c;
            }
        }
    }
    if (ctrlMode == MODE_AUTO) Serial.println("Mode AUTO (apprentissage)");

    Wire.setSDA(I2C_SDA);
    Wire.setSCL(I2C_SCL);
    Wire.begin();

    // SPI1 pour le CC1101 (CSN inactif a l'etat haut)
    pinMode(CC1101_CSN, OUTPUT);
    digitalWrite(CC1101_CSN, HIGH);
    SPI.setMISO(SPI_MISO);
    SPI.setMOSI(SPI_MOSI);
    SPI.setSCLK(SPI_SCK);
    SPI.begin();

    radioOk = radio.begin(CC1101_CSN, CC1101_GDO0);
    Serial.println(radioOk ? "CC1101 detecte !" : "ERREUR : CC1101 non detecte");
    if (radioOk) {
        if (Storage::load(radio, fan, learner)) {
            uint8_t nb = 0;
            for (uint8_t i = 0; i < CC1101::MAX_SLOTS; i++)
                if (radio.slotHasSignal(i)) nb++;
            Serial.printf("[FLASH] %d boutons restaures, etat %s\n",
                          nb, fan.known ? "connu" : "inconnu");
        } else {
            Serial.println("[FLASH] aucune sauvegarde");
        }
        radio.startListening();
    }

    bmeOk = bme.begin(BME280_ADDR);
    if (!bmeOk) bmeOk = bme.begin(0x77);

    if (!bmeOk) {
        Serial.println("ERREUR : BME280 non detecte (verifie cablage, adresses 0x76/0x77)");
    } else {
        Serial.println("BME280 detecte !");
        Serial.print("ID capteur : 0x");
        Serial.println(bme.sensorID(), HEX);
    }

    Storage::logBoot(rcc, previousStage); // journalise ce boot (sans effacer)
    Resume::Snapshot saved = Resume::snapshot();
    if (resume && saved.speed <= 6) {
        fan.sync(saved.powerOn, saved.speed, saved.reverse, LightMode(saved.light));
        fan.known = saved.known;
        if (saved.manual && saved.manualRemainingMs > 0 && saved.manualRemainingMs <= MANUAL_TIMEOUT_MS) {
            ctrlMode = MODE_MANUAL;
            manualSince = millis() - (MANUAL_TIMEOUT_MS - saved.manualRemainingMs);
        }
        manualAction = {fan.powerOn ? fan.speed : uint8_t(0),
                        fan.reverse ? DIR_REVERSE : DIR_NORMAL};
        Serial.println("[BOOT] etat et mode recuperes apres reset sans coupure");
    }
    retainControl();
    Resume::checkpoint(0);

    // Watchdog independant ~32 s : reboot auto si la boucle gele
    // (couvre sniff 15 s et l'effacement flash ~2 s).
    IWatchdog.begin(IWDG_TIMEOUT_MAX);
    IWatchdog.reload();
}

void loop()
{
    static uint32_t lastStatus = 0;
    static uint32_t lastDecision = 0;
    static uint32_t lastComboMs = 0;
    static float lastTemp = NAN;
    static float lastHum = NAN;
    static String cmdBuf = "";
    static bool discardCommand = false;

    retainControl();
    Resume::checkpoint(1);
    ledFxTick(!radioOk || !bmeOk, ctrlMode == MODE_MANUAL);
    IWatchdog.reload(); // nourrit le chien de garde a chaque tour

    // Evenement d'apprentissage -> pulse "compris" (~1.6 s)
    if (learner.takeEvent()) {
        ledFxThinkPulse();
    }

    // Ecoute RF en arriere-plan : une trame prete ?
    if (radioOk) {
        Resume::checkpoint(2);
        radio.poll();
        if (radio.frameReady()) {
            uint16_t n = radio.frameLen();
            int8_t slot = radio.matchSlot(radio.frame(), n);
            if (slot < 0) {
                // Accumulation : chaque fragment est aligne sur le sync (bit 0
                // du message). On garde le plus long prefixe coherent et on
                // retente l'identification sur les bits cumules.
                static uint8_t acc[CC1101::MAX_BITS];
                static uint16_t accLen = 0;
                static uint32_t accT = 0;
                uint8_t fbits[CC1101::MAX_BITS];
                uint16_t nb = radio.decodeFrame(fbits, CC1101::MAX_BITS);
                if (nb >= 8) {
                    if (millis() - accT > 2000) accLen = 0;
                    uint16_t c = min(nb, accLen);
                    bool ok = true;
                    for (uint16_t i = 0; i < c; i++)
                        if (fbits[i] != acc[i]) { ok = false; break; }
                    // Les 2 derniers bits d'un fragment tronque sont peu
                    // fiables : on etend l'accumulation sans eux.
                    uint16_t trusted = nb > 2 ? nb - 2 : nb;
                    if (!ok || trusted > accLen) { memcpy(acc, fbits, trusted); accLen = trusted; }
                    accT = millis();
                    int8_t s2 = radio.matchBits(acc, accLen);
                    if (s2 >= 0) {
                        slot = s2;
                        Serial.printf("[HEARD] slot=%d (identifie par accumulation %d bits)\n",
                                      slot, accLen);
                        accLen = 0;
                    } else if (s2 == -2) slot = -2;
                }
            }
            // Double confirmation : les fragments sont courts et le prefixe
            // commun (32 bits) peut produire de faux slot=0. Un bouton n'est
            // accepte que si le meme slot est identifie 2 fois en <3 s.
            if (slot >= 0) {
                static int8_t pendIdent = -1;
                static uint32_t pendT = 0;
                if (slot == pendIdent && millis() - pendT < 3000) {
                    pendIdent = -1;
                    Serial.printf("[HEARD] slot=%d (confirme)\n", slot);
                } else {
                    Serial.printf("[HEARD] slot=%d ? (attente confirmation)\n", slot);
                    pendIdent = slot;
                    pendT = millis();
                    slot = -2; // traite comme simple activite telecommande
                }
            }
            if (slot >= 0) {
                ledFxRfPulse(700);
                static int8_t lastPressSlot = -1;
                static uint32_t lastPressFrame = 0;
                bool newPress = slot != lastPressSlot || millis() - lastPressFrame > 400;
                lastPressSlot = slot;
                lastPressFrame = millis();
                if (newPress) {
                fan.applyButton((uint8_t)slot);
                stateDirty = true;
                if (slot == COMBO_SLOT) {
                    if (millis() - lastComboMs < COMBO_WINDOW_MS) {
                        ctrlMode = MODE_AUTO;
                        Serial.println("[MODE] AUTO (combo 1H x2)");
                    } else {
                        enterManual();
                    }
                    lastComboMs = millis();
                } else {
                    enterManual();
                }
                // La telecommande repete la trame tant que le bouton est
                // maintenu : une seule observation par appui (nouveau bouton
                // ou >2 s depuis la derniere).
                if (ctrlMode == MODE_MANUAL && (slot <= RemoteButton::SPEED_LAST || slot == RemoteButton::DIRECTION)) {
                    learner.observeManual({fan.powerOn ? fan.speed : (uint8_t)0,
                                           fan.reverse ? DIR_REVERSE : DIR_NORMAL},
                                          lastTemp, lastHum);
                }
                printState();
                }
            } else if (slot == -2) {
                // Prefixe de notre telecommande mais trame trop courte pour
                // identifier le bouton (appui bref) : activite manuelle quand meme
                uint8_t fbits[CC1101::MAX_BITS];
                Serial.printf("[HEARD] activite telecommande (fragment %d bits)\n",
                              radio.decodeFrame(fbits, CC1101::MAX_BITS));
                ledFxRfPulse(300);
                if (ctrlMode == MODE_AUTO) enterManual();
            } else {
                uint8_t bits[CC1101::MAX_BITS];
                uint16_t nb = radio.decodeFrame(bits, CC1101::MAX_BITS);
                if (nb) {
                    Serial.printf("[HEARD] slot=-1 pulses=%d bits=", n);
                    CC1101::printBits(bits, nb);
                    Serial.println();
                } else {
                    Serial.printf("[HEARD] slot=-1 pulses=%d (non decodable)\n", n);
                }
                Serial.printf("[HEARD] rssi=%d dBm\n", radio.rssi());
                // Dump des 16 premiers run-lengths pour diagnostic
                const uint16_t* fp = radio.frame();
                Serial.print("[RX] raw: ");
                for (uint16_t i = 0; i < n && i < 16; i++) {
                    Serial.print(fp[i]);
                    Serial.print(i % 2 ? 'L' : 'H');
                    Serial.print(' ');
                }
                Serial.println();
            }
            radio.clearFrame();
        }
    }

    // Lecture des commandes serie (non bloquant)
    uint16_t serialBudget = 512;
    while (Serial.available() && serialBudget--) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (discardCommand) {
                discardCommand = false;
                cmdBuf = "";
                continue;
            }
            if (cmdBuf.length() > 0) {
                retainControl();
                Resume::checkpoint(3);
                handleCommand(cmdBuf, lastTemp, lastHum);
                cmdBuf = "";
            }
        } else if (!discardCommand) {
            if (cmdBuf.length() < 8192) cmdBuf += c;
            else {
                cmdBuf = "";
                discardCommand = true;
                Serial.println("[ERROR] commande serie trop longue");
            }
        }
    }

    // Retour auto en mode AUTO apres 8 h sans activite
    if (ctrlMode == MODE_MANUAL && millis() - manualSince > MANUAL_TIMEOUT_MS) {
        ctrlMode = MODE_AUTO;
        Serial.println("[MODE] AUTO (timeout 8h)");
        printState();
    }

    // Une observation completee ou une connaissance consolidee -> a sauver
    if (learner.takeLearnDirty()) stateDirty = true;

    // Sauvegarde de l'etat en flash : au plus 1 fois / 10 min, si modifie
    if (stateDirty && millis() - lastFlashSave >= STATE_SAVE_PERIOD_MS) {
        saveFlash();
    }

    if (!bmeOk) {
        Serial.println("BME280 absent - tentative de reconnexion...");
        bmeOk = bme.begin(BME280_ADDR) || bme.begin(0x77);
        delay(2000);
        return;
    }

    // Lecture + statut toutes les 5 s
    if (millis() - lastStatus >= STATUS_PERIOD_MS) {
        lastStatus = millis();
        Resume::checkpoint(51);
        lastTemp = bme.readTemperature();
        Resume::checkpoint(52);
        lastHum  = bme.readHumidity();
        Resume::checkpoint(53);
        lastPressure = bme.readPressure() / 100.0f;
        Resume::checkpoint(54);
        if (!isfinite(lastTemp) || !isfinite(lastHum) || !isfinite(lastPressure)
            || lastTemp < -40 || lastTemp > 85 || lastHum < 0 || lastHum > 100) {
            learner.invalidatePending("mesure capteur invalide");
            lastTemp = lastHum = lastPressure = NAN;
            bmeOk = false;
        }
        learner.noteTemp(lastTemp); // derive naturelle (meme en MANUEL)
        if (!isnan(lastTemp)) {
            uint8_t ev = env.noteSample(lastTemp, lastHum, millis());
            if (ev & EnvContext::EV_COOKING) {
                Serial.println("[CTX] cuisson/four probable (T++ et H++)");
                // La chaleur du four n'est pas l'effet du ventilateur :
                // l'observation en cours serait faussee.
                learner.invalidatePending("cuisson detectee");
            }
            if (ev & EnvContext::EV_AIRING) {
                Serial.println("[CTX] aeration probable (T-- rapide)");
                // La fenetre domine l'evolution : le delta mesure
                // ne serait pas attribuable au ventilateur.
                learner.invalidatePending("aeration detectee");
            }
        }
        printStatus(lastTemp, lastHum);
    }

    // Logique d'apprentissage uniquement en mode auto
    if (isfinite(lastTemp) && isfinite(lastHum) &&
        millis() - lastDecision >= DECISION_PERIOD_MS) {
        lastDecision = millis();

        // Contexte : saison chauffage = mois sync si dispo, sinon
        // temperature interieure moyenne basse (<20.5C sur ~6 h).
        uint8_t mois = currentMonth();
        bool winter = mois ? (mois >= 10 || mois <= 3)
                         : (!isnan(env.seasonEma) && env.seasonEma < 20.5f);
        // Gain solaire : radiation meteo si dispo, sinon pente rapide
        // de T en journee estimee (>0.35C / 10 min) SANS hausse d'humidite
        // (T++ et H++ en meme temps = cuisine/four, pas soleil).
        bool sunGain = !isnan(meteoRad)
            ? meteoRad > 350.0f
            : (learner.dayEst() && env.solarSlope > 0.35f && env.humidSlope < 0.5f);
        learner.setContext(winter, sunGain, meteoT, env.airingNow);

        Resume::checkpoint(6);
        learner.syncApplied({fan.powerOn ? fan.speed : uint8_t(0),
                             fan.reverse ? DIR_REVERSE : DIR_NORMAL}, fan.known);
        if (ctrlMode == MODE_MANUAL)
            learner.followManual({fan.powerOn ? fan.speed : uint8_t(0),
                                  fan.reverse ? DIR_REVERSE : DIR_NORMAL});
        learner.evaluate(lastTemp, lastHum);
        Decision d = {learner.current(), false, "choix utilisateur"};
        if (ctrlMode == MODE_AUTO) d = learner.update(lastTemp, lastHum);
        if (d.changed) {
            Serial.print("[DECISION] v");
            Serial.print(d.action.speed);
            Serial.print("/");
            Serial.print(dirName(d.action.dir));
            Serial.print(" (");
            Serial.print(d.reason);
            Serial.println(")");
            applyFan(d.action);
            printState();
        }
        learner.syncApplied({fan.powerOn ? fan.speed : uint8_t(0),
                             fan.reverse ? DIR_REVERSE : DIR_NORMAL}, fan.known);
        learner.printState();
    }
}
