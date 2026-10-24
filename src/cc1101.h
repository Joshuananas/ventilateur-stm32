#pragma once
#include <Arduino.h>
#include "domain/limits.h"

// Driver minimal CC1101 pour OOK 433.92 MHz (sniff + replay brut)
// RX : sortie serie asynchrone sur GDO0 (PA0).
// TX : entree serie asynchrone sur GDO0, rejouee en bit-bang.
// 12 slots memoire : un signal appris par bouton de telecommande.

class CC1101 {
public:
    static const uint16_t MAX_PULSES = RadioLimits::MAX_PULSES;
    static const uint8_t  MAX_SLOTS  = RadioLimits::MAX_SLOTS;

    bool begin(uint8_t csn, uint8_t gdo0);

    // Attente d'activite (max durationMs) puis capture dans le slot courant.
    uint16_t sniff(uint32_t durationMs);

    // Reemet le signal du slot courant.
    // inverted=true : inverse les niveaux (test de polarite)
    // singleFrame=true : n'emet qu'une trame (sync -> sync suivant)
    void replay(uint8_t repeats = 4, bool inverted = false, bool singleFrame = false);

    // Gestion des slots (0..11)
    void selectSlot(uint8_t s);
    uint8_t slot() const { return _slot; }
    bool slotHasSignal(uint8_t s) const { return _counts[s] > 8; }
    uint16_t slotPulses(uint8_t s) const { return _counts[s]; }

    uint16_t pulseCount() const { return _counts[_slot]; }
    uint16_t pulse(uint16_t i) const { return _buf[_slot][i]; }
    bool hasSignal() const { return pulseCount() > 8; }
    void clearSlot(uint8_t s);

    void printSignal() const;

    // Persistance : export/import CSV des pulses d'un slot
    void dumpSlot(uint8_t s) const;
    bool loadSlot(uint8_t s, const char* csv);

    // Reglages
    void setFreq(float mhz);
    int8_t rssi();
    uint8_t gdoLevel() const;
    void rxOn();
    void idle();

    // ---- Ecoute RF en arriere-plan (non bloquante) ----
    void startListening();
    void stopListening();
    bool listening() const { return _listening; }
    void poll();                              // a appeler dans loop()
    bool frameReady() const { return _frameReady; }
    uint16_t frameLen() const { return _rxLen; }
    const uint16_t* frame() const { return (const uint16_t*)_rxLast; }
    void clearFrame();
    void dumpFrame() const;
    void txTest();
    void txScan();
    uint32_t edgesTotal() const { return _edgesTotal; }

    // ---- Decodage / reconnaissance de boutons ----
    static const uint16_t MAX_BITS = RadioLimits::MAX_BITS;
    static uint16_t decodePacket(const uint16_t* pulses, uint16_t n,
                                 uint8_t* bitsOut, uint16_t maxBits);
    static void printBits(const uint8_t* bits, uint16_t nb);
    int8_t matchSlot(const uint16_t* pulses, uint16_t n);
    int8_t matchBits(const uint8_t* bits, uint16_t nb);
    uint16_t decodeFrame(uint8_t* bitsOut, uint16_t maxBits) const;
    const uint8_t* slotBits(uint8_t s);      // bits decodes du slot (cache)
    uint16_t slotBitLen(uint8_t s);          // nb de bits decodes du slot (cache)

    // ---- Persistance ----
    const uint16_t* slotData(uint8_t s) const { return _buf[s]; }
    void setSlot(uint8_t s, const uint16_t* pulses, uint16_t n);

private:
    uint8_t _csn = 0xFF, _gdo0 = 0xFF;
    uint8_t _slot = 0;
    uint16_t _buf[MAX_SLOTS][MAX_PULSES];
    uint16_t _counts[MAX_SLOTS];

    // Capture RX en arriere-plan (remplie par l'ISR ou le timer)
    volatile uint16_t _rx[MAX_PULSES];
    uint16_t _rxLast[MAX_PULSES];
    uint16_t _rxLastLen = 0;
    volatile uint16_t _rxCount = 0;
    volatile uint32_t _lastEdgeUs = 0;
    volatile uint32_t _edgesTotal = 0;
    uint32_t _rxStartMs = 0;      // millis() du premier front de la trame
    uint16_t _rxLen = 0;
    bool _frameReady = false;
    bool _rxActive = false;       // capture en cours depuis le sync
    uint8_t _rxLevel = 0;         // niveau du dernier run-length stocke
    bool _listening = false;
    uint32_t _ignoreUntil = 0;    // ignore RX apres un replay (anti auto-ecoute)
    float _txFreq = 0.0f;         // surcharge de frequence pour txScan (0 = defaut)

    // Cache des bits decodes par slot (reconstruit apres learn/load)
    bool _cacheValid = false;
    uint8_t _slotBits[MAX_SLOTS][MAX_BITS];
    uint16_t _slotBitLen[MAX_SLOTS];

    void ensureCache();
    void onEdge(uint32_t nowUs, int curLevel);
    uint8_t _tmrPrev = 0;           // dernier niveau GDO0 (polling dans poll())
    static CC1101* _instance;

    uint8_t spiRead(uint8_t addr);
    uint8_t spiReadStatus(uint8_t addr);
    void spiWrite(uint8_t addr, uint8_t val);
    void spiWriteBurst(uint8_t addr, const uint8_t* vals, uint8_t n);
    void strobe(uint8_t cmd);
    void reset();
    void configRxOok();
    void configTxOok();
};
