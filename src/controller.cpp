#include "controller.h"
#include "storage.h"
#include "pins.h"
#include "resume.h"
#include "domain/remote_buttons.h"

Adafruit_BME280 bme;
bool bmeOk = false;
float lastPressure = NAN;

FanLearner learner;
CC1101 radio;
bool radioOk = false;

CtrlMode ctrlMode = MODE_AUTO;
uint32_t manualSince = 0;

FanState fan;
FanAction manualAction = {0, DIR_NORMAL};

// Horloge approximative + meteo (pousses par la page web)
uint32_t epochBase = 0;      // epoch Unix a la reception de timesync
uint32_t epochMillis = 0;    // millis() a la reception de timesync
bool timeSynced = false;
float meteoT = NAN, meteoH = NAN, meteoRad = NAN;

// Sauvegarde flash : etat modifie depuis la derniere ecriture
bool stateDirty = false;
uint32_t lastFlashSave = 0;

uint32_t nowEpoch() { return timeSynced ? epochBase + (millis() - epochMillis)/1000UL : 0; }

// Mois (1-12) a partir de l'epoch — algorithme civil days-from-epoch
uint8_t currentMonth()
{
    if (!timeSynced) return 0;
    uint32_t days = nowEpoch() / 86400UL;
    // Algorithme de Hinnant : jours -> annee/mois/jour
    days += 719468;
    uint32_t era = days / 146097;
    uint32_t doe = days - era * 146097;
    uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    uint32_t y = yoe + era * 400;
    uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    uint32_t mp = (5*doy + 2)/153;
    uint32_t m = mp + (mp < 10 ? 3 : -9);
    return (uint8_t)m;
}

void saveFlash()
{
    bool wasListening = radioOk && radio.listening();
    if (wasListening) radio.stopListening();
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
    Resume::checkpoint(4);
    bool ok = Storage::save(radio, fan, learner);
    if (wasListening) radio.startListening();
    lastFlashSave = millis();
    stateDirty = !ok;
    Serial.println(ok ? "[FLASH] sauvegarde OK" : "[FLASH] sauvegarde ECHEC");
}

const char* dirName(FanDirection d) { return d == DIR_NORMAL ? "NORMAL" : "REVERSE"; }

void enterManual()
{
    ctrlMode = MODE_MANUAL;
    manualSince = millis();
    learner.invalidatePending(); // delta non attribuable a l'action auto
}

uint32_t manualLeftMin()
{
    if (ctrlMode == MODE_AUTO) return 0;
    uint32_t elapsed = millis() - manualSince;
    if (elapsed >= MANUAL_TIMEOUT_MS) return 0;
    return (MANUAL_TIMEOUT_MS - elapsed) / 60000UL;
}

void printState()
{
    learner.syncApplied({fan.powerOn ? fan.speed : uint8_t(0),
                         fan.reverse ? DIR_REVERSE : DIR_NORMAL}, fan.known);
    Serial.printf("[STATE] mode=%s known=%d power=%d speed=%d rev=%d light=%s manual_left_min=%lu\n",
                  ctrlMode == MODE_AUTO ? "AUTO" : "MANUAL",
                  (int)fan.known, (int)fan.powerOn, (int)fan.speed,
                  (int)fan.reverse, fan.lightName(),
                  (unsigned long)manualLeftMin());
}

void applyFan(const FanAction& a)
{
    Resume::checkpoint(7);
    Serial.print(">>> COMMANDE : vitesse ");
    Serial.print(a.speed);
    Serial.print(" / ");
    Serial.println(dirName(a.dir));

    if (!fan.known) {
        Serial.println(">>> Etat ventilateur inconnu : synchronise via la page web (sync)");
        return;
    }

    // Direction AVANT la vitesse
    if (((a.dir == DIR_REVERSE) != fan.reverse)) {
        if (radioOk && radio.slotHasSignal(RemoteButton::DIRECTION)) {
            radio.selectSlot(RemoteButton::DIRECTION);
            radio.replay(3, false, true); // appui bref : 3 trames propres
            fan.applyButton(RemoteButton::DIRECTION);
            stateDirty = true;
        } else {
            Serial.println(">>> slot 8 (reverse) non appris");
        }
    }

    if (a.speed == 0) {
        if (fan.powerOn) {
            if (radioOk && radio.slotHasSignal(RemoteButton::OFF)) {
                radio.selectSlot(RemoteButton::OFF);
                radio.replay(3, false, true);
                fan.applyButton(RemoteButton::OFF);
                stateDirty = true;
            } else {
                Serial.println(">>> slot 0 (power) non appris");
            }
        }
    } else if (a.speed <= 6 && (!fan.powerOn || fan.speed != a.speed)) {
        if (radioOk && radio.slotHasSignal(a.speed)) {
            radio.selectSlot(a.speed);
            radio.replay(3, false, true);
            fan.applyButton(a.speed);
            stateDirty = true;
        } else {
            Serial.print(">>> slot ");
            Serial.print(a.speed);
            Serial.println(" non appris");
        }
    }
}

void printStatus(float temp, float hum)
{
    const FanAction a = {fan.powerOn ? fan.speed : uint8_t(0),
                         fan.reverse ? DIR_REVERSE : DIR_NORMAL};
    Resume::checkpoint(55);
    float pres = lastPressure;
    Serial.print("[STATUS] mode=");
    Serial.print(ctrlMode == MODE_MANUAL ? "MANUEL" : "AUTO");
    Serial.print(" T=");
    Serial.print(temp, 1);
    Serial.print("C H=");
    Serial.print(hum, 1);
    Serial.print("% P=");
    Serial.print(pres, 1);
    Serial.print("hPa Fan=");
    Serial.print(a.speed);
    Serial.print("/");
    Serial.print(dirName(a.dir));
    Serial.print(" obs=");
    Serial.print(learner.count());
    Serial.print("/");
    Serial.println(FanLearner::CAPACITY);
    printState();
}

void handleCommand(const String& cmd, float temp, float hum)
{
    if (cmd == "auto") {
        ctrlMode = MODE_AUTO;
        Serial.println("[MODE] AUTO (commande)");
        printState();
        return;
    }
    if (cmd == "manual") {
        enterManual();
        Serial.println("[MODE] MANUEL (commande)");
        printState();
        return;
    }
    if (cmd == "status") {
        printStatus(temp, hum);
        return;
    }
    if (cmd == "txtest") {
        radio.txTest();
        return;
    }
    if (cmd == "txscan") {
        radio.txScan();
        return;
    }
    if (cmd == "dumpf") {
        radio.dumpFrame();
        return;
    }
    if (cmd == "rxstat") {
        Serial.printf("[RX] listening=%d edges=%lu frames_pending=%d\n",
                      (int)radio.listening(),
                      (unsigned long)radio.edgesTotal(),
                      (int)radio.frameReady());
        return;
    }
    if (cmd.startsWith("sync")) {
        // sync P S R L  (P 0/1, S 1..6, R 0/1, L 0..3)
        int p = -1, s = 0, r = 0, l = 0;
        sscanf(cmd.c_str(), "sync %d %d %d %d", &p, &s, &r, &l);
        if (p < 0 || s < 1 || s > 6 || l < 0 || l > 3) {
            Serial.println("[STATE] usage: sync P S R L  (P 0/1, S 1..6, R 0/1, L 0..3)");
            return;
        }
        fan.sync(p != 0, (uint8_t)s, r != 0, (LightMode)l);
        Serial.println("[STATE] ventilateur synchronise");
        printState();
        saveFlash();
        return;
    }
    if (cmd.startsWith("timesync")) {
        uint32_t e = (uint32_t)cmd.substring(8).toInt(); // toInt = long -> epoch ok
        epochBase = e;
        epochMillis = millis();
        timeSynced = (e > 1000000000UL);
        Serial.printf("[TIME] sync epoch=%lu mois=%d\n", (unsigned long)e, currentMonth());
        return;
    }
    if (cmd.startsWith("meteo")) {
        // meteo <t_ext> <h_ext> <radiation W/m2>
        float t, h, r;
        if (sscanf(cmd.c_str(), "meteo %f %f %f", &t, &h, &r) == 3) {
            meteoT = t; meteoH = h; meteoRad = r;
            Serial.printf("[METEO] Text=%.1fC Hext=%.0f%% rad=%.0f W/m2\n", t, h, r);
        }
        return;
    }
    if (cmd == "forgesync") {
        // Synchro deterministe sans intervention : OFF est inerte quand le
        // ventilateur est deja eteint -> apres play0 il est forcement OFF,
        // puis vitesse 1 -> ON vitesse 1. Sens/lumiere restent a confirmer.
        if (!radioOk) { Serial.println("CC1101 absent"); return; }
        if (!radio.slotHasSignal(RemoteButton::OFF) || !radio.slotHasSignal(RemoteButton::SPEED_FIRST)) {
            Serial.println("[STATE] slots 0 et 1 requis pour forgesync");
            return;
        }
        Serial.println("[STATE] Forgesync : OFF puis vitesse 1...");
        radio.selectSlot(RemoteButton::OFF);
        radio.replay(3, false, true);
        delay(800);
        radio.selectSlot(RemoteButton::SPEED_FIRST);
        radio.replay(3, false, true);
        fan.sync(true, 1, fan.reverse, fan.light);
        enterManual();
        stateDirty = true;
        printState();
        Serial.println("[STATE] Etat force : ON vitesse 1. Verifie sens/lumiere une fois.");
        return;
    }
    if (cmd == "rftest") {
        radioOk = radio.begin(CC1101_CSN, CC1101_GDO0);
        Serial.println(radioOk ? "CC1101 detecte !" : "ERREUR : CC1101 non detecte");
        if (radioOk) radio.startListening();
        return;
    }
    if (cmd == "rfscan") {
        if (!radioOk) { Serial.println("CC1101 absent"); return; }
        enterManual();
        radio.stopListening();
        static const float freqs[] = {303.875f, 315.0f, 330.0f, 390.0f, 418.0f, 433.92f, 868.35f, 915.0f};
        Serial.println("[RF] Scan des frequences (2 s chacune) — maintiens un bouton de la telecommande enfonce !");
        pinMode(CC1101_GDO0, INPUT);
        for (uint8_t f = 0; f < sizeof(freqs)/sizeof(freqs[0]); f++) {
            radio.idle();
            radio.rxOn();
            radio.setFreq(freqs[f]);
            delay(50);
            uint32_t edges = 0, t0 = millis();
            int last = digitalRead(CC1101_GDO0);
            int maxR = -120;
            while (millis() - t0 < 2000) {
                int l = digitalRead(CC1101_GDO0);
                if (l != last) { edges++; last = l; }
                int r = radio.rssi();
                if (r > maxR) maxR = r;
            }
            Serial.print("[RF] ");
            Serial.print(freqs[f], 3);
            Serial.print(" MHz : edges=");
            Serial.print(edges);
            Serial.print(" RSSI_max=");
            Serial.print(maxR);
            Serial.println(" dBm");
        }
        radio.idle();
        radio.startListening();
        Serial.println("[RF] Scan termine");
        return;
    }
    if (cmd == "learn" || (cmd.startsWith("learn") && cmd.length() > 5
        && cmd[5] >= '0' && cmd[5] <= '9')) {
        if (!radioOk) { Serial.println("CC1101 absent"); return; }
        int slot = cmd.substring(5).toInt(); // "learn" ou "learn3"
        if (slot < 0 || slot >= CC1101::MAX_SLOTS) { Serial.println("[RF] slot invalide 0-15"); return; }
        enterManual();
        radio.selectSlot(slot);
        Serial.print("[RF] Apprentissage slot ");
        Serial.println(slot);
        uint16_t n = radio.sniff(15000);
        Serial.print("[RF] ");
        Serial.print(n);
        Serial.println(" transitions capturees");
        if (radio.hasSignal()) { radio.printSignal(); saveFlash(); }
        else Serial.println("[RF] Rien capte (verifie GDO0->PA0, portee)");
        return;
    }
    if (cmd.startsWith("playi")) { // play inverse : test de polarite
        if (!radioOk) { Serial.println("CC1101 absent"); return; }
        int slot = cmd.substring(5).toInt();
        if (slot < 0 || slot >= CC1101::MAX_SLOTS) { Serial.println("[RF] slot invalide 0-15"); return; }
        radio.selectSlot(slot);
        if (!radio.hasSignal()) { Serial.println("[RF] Slot vide"); return; }
        Serial.printf("[RF] Reemission INVERSEE slot %d x6...\n", slot);
        radio.replay(6, true, false);
        Serial.println("[RF] Termine");
        return;
    }
    if (cmd.startsWith("playf")) { // trame unique propre sync->sync
        if (!radioOk) { Serial.println("CC1101 absent"); return; }
        int slot = cmd.substring(5).toInt();
        if (slot < 0 || slot >= CC1101::MAX_SLOTS) { Serial.println("[RF] slot invalide 0-15"); return; }
        radio.selectSlot(slot);
        if (!radio.hasSignal()) { Serial.println("[RF] Slot vide"); return; }
        Serial.printf("[RF] Reemission TRAME slot %d x8...\n", slot);
        radio.replay(8, false, true);
        Serial.println("[RF] Termine");
        return;
    }
    if (cmd.startsWith("play")) {
        if (!radioOk) { Serial.println("CC1101 absent"); return; }
        int slot = cmd.substring(4).toInt();
        if (slot < 0 || slot >= CC1101::MAX_SLOTS) { Serial.println("[RF] slot invalide 0-15"); return; }
        radio.selectSlot(slot);
        if (!radio.hasSignal()) { Serial.println("[RF] Slot vide — lance 'learn' d'abord"); return; }
        Serial.print("[RF] Reemission slot ");
        Serial.print(slot);
        Serial.println(" (appui bref)...");
        radio.replay(3, false, true); // 3 trames propres ~200 ms, pas le buffer entier
        fan.applyButton((uint8_t)slot);
        stateDirty = true;
        enterManual();
        if (slot <= RemoteButton::SPEED_LAST || slot == RemoteButton::DIRECTION)
            learner.observeManual({fan.powerOn ? fan.speed : (uint8_t)0,
                                   fan.reverse ? DIR_REVERSE : DIR_NORMAL},
                                  temp, hum);
        printState();
        Serial.println("[RF] Termine");
        return;
    }
    if (cmd.startsWith("dump")) {
        radio.dumpSlot(cmd.substring(4).toInt());
        return;
    }
    if (cmd.startsWith("load")) {
        int sp = cmd.indexOf(' ');
        if (sp < 0) { Serial.println("[RF] usage: loadN p1,p2,..."); return; }
        int slot = cmd.substring(4, sp).toInt();
        if (radio.loadSlot(slot, cmd.c_str() + sp + 1)) {
            Serial.print("[RF] slot ");
            Serial.print(slot);
            Serial.print(" charge : ");
            Serial.print(radio.slotPulses(slot));
            Serial.println(" pulses");
            saveFlash();
        } else {
            Serial.println("[RF] load echoue");
        }
        return;
    }
    if (cmd.startsWith("wipe") && cmd.length() > 4) {
        int s = cmd.substring(4).toInt();
        radio.clearSlot((uint8_t)s);
        Serial.printf("[RF] slot %d efface\n", s);
        saveFlash();
        return;
    }
    if (cmd == "slots") {
        bool flashOk = Storage::valid();
        for (uint8_t i = 0; i < CC1101::MAX_SLOTS; i++) {
            Serial.print("[RF] slot ");
            Serial.print(i);
            Serial.print(" : ");
            Serial.print(radio.slotPulses(i));
            if (flashOk) Serial.print(" (flash)");
            Serial.println();
        }
        return;
    }
    if (cmd == "bits") {
        for (uint8_t i = 0; i < CC1101::MAX_SLOTS; i++) {
            if (!radio.slotHasSignal(i)) continue;
            Serial.printf("[BITS] slot %d : ", i);
            CC1101::printBits(radio.slotBits(i), radio.slotBitLen(i));
            Serial.println();
        }
        return;
    }
    if (cmd == "bootlog") {
        Storage::dumpBootLog();
        return;
    }
    if (cmd == "logclear") {
        // Effacement explicite : a reserver une fois le probleme
        // compris et resolu, le journal est precieux.
        Storage::logClear();
        Serial.println("[BOOTLOG] journal efface");
        return;
    }
    if (cmd == "save" || cmd == "learnsave") {
        saveFlash();
        return;
    }
    if (cmd.startsWith("unban")) {
        learner.banSpeed(cmd.substring(5).toInt(), false);
        Serial.printf("[LEARN] vitesse %d autorisee\n", cmd.substring(5).toInt());
        return;
    }
    if (cmd.startsWith("ban")) {
        learner.banSpeed(cmd.substring(3).toInt(), true);
        Serial.printf("[LEARN] vitesse %d bannie de l'auto\n", cmd.substring(3).toInt());
        return;
    }
    if (cmd == "observations" || cmd.startsWith("observations ")) {
        int offset = 0;
        if (cmd != "observations" && (sscanf(cmd.c_str(), "observations %d", &offset) != 1
            || offset < 0 || offset >= FanLearner::CAPACITY)) {
            Serial.println("[OBS] erreur offset invalide");
            return;
        }
        learner.printObservations(uint8_t(offset));
        return;
    }
    if (cmd == "savoir") {
        learner.printKnowledge();
        return;
    }
    if (cmd == "learnreset") {
        learner.reset();
        saveFlash();
        Serial.println("[LEARN] apprentissage remis a zero (obs, connaissances, seuils)");
        return;
    }
    if (cmd == "flasherase") {
        bool wasListening = radioOk && radio.listening();
        if (wasListening) radio.stopListening();
        Storage::erase();
        if (wasListening) radio.startListening();
        Serial.println("[FLASH] secteur efface");
        return;
    }
    if (cmd == "sniff") {
        if (!radioOk) { Serial.println("CC1101 absent"); return; }
        enterManual();
        Serial.println("[RF] Pret : appuie sur la telecommande (attente 15 s max)");
        uint16_t n = radio.sniff(15000);
        Serial.print("[RF] ");
        Serial.print(n);
        Serial.println(" transitions capturees");
        if (radio.hasSignal()) radio.printSignal();
        else Serial.println("[RF] Rien capte (verifie GDO0->PA0, portee)");
        return;
    }
    if (cmd == "replay") {
        if (!radioOk) { Serial.println("CC1101 absent"); return; }
        if (!radio.hasSignal()) { Serial.println("[RF] Pas de signal — lance 'sniff' d'abord"); return; }
        Serial.println("[RF] Reemission (appui bref)...");
        radio.replay(3, false, true);
        Serial.println("[RF] Termine");
        return;
    }
    if (cmd == "off") {
        enterManual();
        manualAction = {0, fan.reverse ? DIR_REVERSE : DIR_NORMAL};
        applyFan(manualAction);
        learner.observeManual({fan.powerOn ? fan.speed : uint8_t(0),
                               fan.reverse ? DIR_REVERSE : DIR_NORMAL}, temp, hum);
        printState();
        Serial.println("Mode MANUEL : ventilateur OFF");
        return;
    }
    if (cmd == "rev") {
        enterManual();
        FanDirection nd = (manualAction.dir == DIR_NORMAL) ? DIR_REVERSE : DIR_NORMAL;
        if (fan.known) nd = fan.reverse ? DIR_NORMAL : DIR_REVERSE;
        manualAction = {(uint8_t)(fan.speed ? fan.speed : 1), nd};
        applyFan(manualAction);
        learner.observeManual({fan.powerOn ? fan.speed : uint8_t(0),
                               fan.reverse ? DIR_REVERSE : DIR_NORMAL}, temp, hum);
        printState();
        Serial.println("Mode MANUEL : sens inverse");
        return;
    }
    if (cmd.length() == 2 && cmd[0] == 's' && cmd[1] >= '1' && cmd[1] <= '6') {
        enterManual();
        manualAction.speed = cmd[1] - '0';
        manualAction.dir = fan.reverse ? DIR_REVERSE : DIR_NORMAL;
        applyFan(manualAction);
        learner.observeManual({fan.powerOn ? fan.speed : uint8_t(0),
                               fan.reverse ? DIR_REVERSE : DIR_NORMAL}, temp, hum);
        printState();
        Serial.print("Mode MANUEL : vitesse ");
        Serial.println(manualAction.speed);
        return;
    }
    Serial.print("Commande inconnue : ");
    Serial.println(cmd);
    Serial.println("Commandes : s1..s6, rev, off, auto, manual, status, sync P S R L, rxstat, "
                   "sniff, learnN, playN, replay, slots, bits, dumpN, loadN p1,p2,..., "
                   "save, learnsave, learnreset, savoir, observations, bootlog, logclear, flasherase, rftest, rfscan");
}
