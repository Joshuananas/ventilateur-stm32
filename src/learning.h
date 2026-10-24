#pragma once
#include <Arduino.h>
#include "domain/fan_types.h"
#include "domain/learning_types.h"
#include "domain/limits.h"

class FanLearner {
public:
    static const uint8_t CAPACITY = LearningLimits::CAPACITY;
    static const uint32_t EVAL_DELAY_MS      = 12UL * 60UL * 1000UL; // 12 min
    static const uint32_t MIN_CMD_MS         = 90UL * 1000UL;        // 90 s
    static const uint32_t MIN_DIR_CHANGE_MS  = 3UL * 60UL * 1000UL;  // 3 min
    static const uint8_t  MIN_SAMPLES        = 5;
    static constexpr float SIMILAR_TEMP_BAND = 1.5f;
    static constexpr float SIMILAR_HUM_BAND  = 12.0f;   // degres C
    static constexpr float HUMIDITY_FACTOR   = 68.0f;  // % HR
    static constexpr float THRESH_ADAPT_RATE = 0.02f;  // 2% par observation
    static const uint32_t EXPLORE_MS         = 4UL * 3600UL * 1000UL; // ~4 h

    FanLearner();

    // A appeler periodiquement avec les dernieres mesures.
    // Retourne une Decision (changed=true si applyFan doit etre appele).
    Decision update(float temp, float humidity);
    void evaluate(float temp, float humidity);
    void syncApplied(const FanAction& a, bool known);
    void followManual(const FanAction& a) {
        _current = a;
        _hasDecision = false;
        _lastReason = "choix utilisateur";
    }

    // A appeler a CHAQUE lecture capteur (meme en MANUEL) : entretient
    // l'estimation de derive naturelle de T (degres C/min, lissée).
    void noteTemp(float temp);

    // Contexte externe (fourni par la page web, optionnel)
    void setContext(bool winter, bool sunGain, float extTemp,
                    bool airing = false);

    // Intervention manuelle pendant une evaluation -> le delta mesure ne
    // serait plus attribuable a l'action auto, on annule l'observation.
    void invalidatePending(const char* why = "intervention manuelle");

    // L'utilisateur choisit une action : signal de preference, on evalue
    // aussi son effet (y compris speed 0 = "il prefere l'arreter").
    void observeManual(const FanAction& a, float temp, float humidity);

    // Connaissance figee a long terme : consolidatee quand assez
    // d'observations concordantes existent pour une action dans des
    // conditions similaires. Survit au reboot et a l'ecrasement du buffer.
    static const uint8_t KNOW_CAP     = LearningLimits::KNOW_CAP;
    static const uint8_t KNOW_MIN_OBS = 3;

    uint8_t count() const { return _count; }
    bool dayEst() const { return _dayEst; }
    uint8_t knowCount() const { return _knowCount; }
    const FanAction& current() const { return _current; }
    void thresholds(float& lo, float& med, float& hi) const {
        lo = _thLow(); med = _thMed(); hi = _thHigh();
    }
    void printState() const;
    void printObservations(uint8_t offset = 0) const;
    void printKnowledge() const;   // liste les connaissances figees

    // Reset complet : oublie observations, connaissances et seuils adaptes
    void reset();

    // Vitesses bannies (ex. v1 bruyante) : exclues des choix auto.
    void banSpeed(uint8_t s, bool on = true)
        { if (s <= 6) { if (on) _banned |= (1 << s); else _banned &= ~(1 << s);
                        _learnDirty = true; } }
    bool banned(uint8_t s) const { return s <= 6 && (_banned & (1 << s)); }
    uint8_t bannedMask() const { return _banned; }

    // Persistance : exporte/importe tout l'etat appris (buffer + fige)
    // StoredLearn est defini dans persistence/storage_format.h — utilise par storage.cpp
    uint16_t serializedSize() const;
    uint16_t serialize(uint8_t* out) const;
    bool deserialize(const uint8_t* in, uint16_t len);

    // Positionne apres chaque observation completee ou consolidation ;
    // consomme par main.cpp pour declencher une sauvegarde flash.
    bool takeLearnDirty() { bool d = _learnDirty; _learnDirty = false; return d; }

    // Signale "le STM32 a compris quelque chose" : observation evaluee
    // ou connaissance figee/defigee/fusionnee. Consomme par la LED.
    bool takeEvent() { bool e = _event; _event = false; return e; }

private:
    Observation _buf[CAPACITY]{};
    uint8_t _head = 0;
    uint8_t _count = 0;

    // Seuils adaptatifs par saison : _th[0]=ete, _th[1]=hiver.
    // Chaque jeu derive vers les temperatures observees de sa saison.
    float _th[2][3];
    float _thLow()  const { return _th[_winter ? 1 : 0][0]; }
    float _thMed()  const { return _th[_winter ? 1 : 0][1]; }
    float _thHigh() const { return _th[_winter ? 1 : 0][2]; }

    // Connaissances figees (long terme)
    struct Knowledge {
        uint8_t speed, dir;
        float tempC, humC;  // conditions moyennes
        float delta;        // effet moyen mesure
        uint8_t n;          // nb d'observations consolidees
        float conf;         // 0..1 : baisse si les nouvelles obs contredisent
    };
    StoredPreference _preferences[16] = {};
    void recordPreference(const FanAction& a, float temp, float hum);
    bool preferred(float temp, float hum, FanAction& action) const;
    Knowledge _know[KNOW_CAP]{};
    uint8_t _knowCount = 0;
    uint8_t _banned = 0;         // bitmask des vitesses exclues de l'auto
    bool _learnDirty = false;
    bool _event = false;           // evenement d'apprentissage a signaler
    uint8_t _noResp = 0;           // obs consecutives sans effet attendu
    bool _suspect = false;         // ventilateur probablement sans reponse

    FanAction _current;
    FanAction _lastDecision{};
    bool _hasDecision = false;

    // Contexte exterieur (meteo + saison, pousses par la page web)
    bool _winter = false;      // saison chauffage (oct-mars)
    bool _sunGain = false;     // soleil fort sur la baie vitree
    float _extTemp = NAN;      // temperature exterieure (meteo)
    bool _airing = false;      // fenetre ouverte detectee (chute T rapide)
    float _pendingHum = NAN;   // hygrometrie de l'obs en cours

    // Estimation jour/nuit sans RTC : tendance T/H sur ~30 min
    // (jour typique : T monte, H descend ; nuit : inverse)
    float _trT0 = NAN, _trH0 = NAN;
    uint32_t _trTs = 0;
    bool _dayEst = true;

    // Derive naturelle de la piece (C/min), EMA des pentes mesurees.
    // Sert a corriger le delta d'une observation : effet = reel - prevu.
    float _tPrev = NAN;
    uint32_t _tPrevTs = 0;
    float _driftRate = NAN;   // C/min ; NAN = pas encore estime
    float _pendingDrift = 0;  // derive attendue figee au moment de l'action
    float _lastApparent = NAN; // dernier ressenti humidex calcule
    uint32_t _lastCmdTs = 0;
    uint32_t _lastDirTs = 0;
    bool _hasLastCmd = false;
    uint32_t _lastExploreTs = 0;
    const char* _lastReason = "demarrage";

    // Observation en cours d'evaluation
    bool _pending = false;
    uint32_t _pendingTs = 0;
    FanAction _pendingAction{};
    float _pendingTempBefore = 0;

    FanAction fallback(float temp) const;
    FanAction learned(float temp, float hum) const;
    float avgDelta(uint8_t speed, FanDirection dir,
                   float temp, float hum, bool similarOnly) const;
    void adaptThresholds(float temp);
    void completePending(float tempAfter);
    void consolidate(const Observation& o);
    uint8_t similarObs(const FanAction& a, float temp) const;
    int8_t confOf(const FanAction& a, float temp) const; // conf*100, -1 si aucune
    const Knowledge* knowFor(const FanAction& a, float temp) const;
public:
    bool suspect() const { return _suspect; }
private:
    float apparentTemp(float t, float h) const; // humidex approx.
};


