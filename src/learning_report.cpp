// Telemetry serie du FanLearner : etat, observations et connaissances.
#include "learning.h"
#include "safe_serial.h"

void FanLearner::printObservations(uint8_t offset) const
{
    uint8_t end = min(int(_count), int(offset) + 8);
    Serial.printf("[OBS] begin count=%u cap=%u offset=%u\n", _count, CAPACITY, offset);
    for (uint8_t i = offset; i < end; i++) {
        const Observation& o = _buf[(_head + CAPACITY - 1 - i) % CAPACITY];
        Serial.printf("[OBS] i=%u v=%u dir=%c t0=", i, o.action.speed,
                      o.action.dir == DIR_NORMAL ? 'N' : 'R');
        Serial.print(o.tempBefore, 2);
        Serial.print(" t1="); Serial.print(o.tempAfter, 2);
        Serial.print(" hum="); Serial.print(o.humBefore, 1);
        Serial.print(" raw="); Serial.print(o.tempAfter - o.tempBefore, 2);
        Serial.print(" expected="); Serial.print(o.expected, 2);
        Serial.print(" effect="); Serial.println(o.delta, 2);
    }
    Serial.printf("[OBS] end count=%u next=%u\n", _count, end);
}

void FanLearner::printKnowledge() const
{
    Serial.println("[PREF] begin");
    for (const StoredPreference& p : _preferences) {
        if (p.votes) {
            Serial.printf("[PREF] v%u/%s temp=", p.speed, p.dir == DIR_NORMAL ? "N" : "R");
            Serial.print(p.temp, 1);
            Serial.print(" hum="); Serial.print(p.hum, 0);
            Serial.printf(" hiver=%u poids=%u/10\n", p.winter, p.votes);
        }
    }
    Serial.println("[PREF] end");
    Serial.printf("[KNOW] %u cas figes\n", _knowCount);
    for (uint8_t i = 0; i < _knowCount; i++) {
        const Knowledge& k = _know[i];
        Serial.printf("[KNOW] i=%u v%u/%s temp=", i, k.speed, k.dir == DIR_NORMAL ? "N" : "R");
        Serial.print(k.tempC, 1);
        Serial.print(" hum="); Serial.print(k.humC, 0);
        Serial.print(" delta="); Serial.print(k.delta, 2);
        Serial.printf(" n=%u conf=%d\n", k.n, (int)(k.conf * 100));
    }
}

void FanLearner::printState() const
{
    // Ligne machine-lisible pour la page web :
    // [LEARN] obs=N/30 mode=X action=vS/D raison=.. eval=MIN jour=o|n
    //         hiver=o|n soleil=o|n Text=..
    Serial.print("[LEARN] obs=");
    Serial.print(_count);
    Serial.print("/");
    Serial.print(CAPACITY);
    Serial.print(" mode=");
    Serial.print(_count >= MIN_SAMPLES ? "APPRIS" : "SEUILS");
    Serial.print(" action=v");
    Serial.print(_current.speed);
    Serial.print(_current.dir == DIR_NORMAL ? "/N" : "/R");
    Serial.print(" raison=");
    Serial.print(_lastReason);
    Serial.print(" eval=");
    if (_pending) {
        uint32_t elapsed = millis() - _pendingTs;
        Serial.print(elapsed >= EVAL_DELAY_MS ? 0UL : (EVAL_DELAY_MS - elapsed + 59999UL) / 60000UL);
    } else
        Serial.print('-');
    Serial.print(" jour=");
    Serial.print(_dayEst ? 'o' : 'n');
    Serial.print(" hiver=");
    Serial.print(_winter ? 'o' : 'n');
    Serial.print(" soleil=");
    Serial.print(_sunGain ? 'o' : 'n');
    Serial.print(" Text=");
    if (isnan(_extTemp)) Serial.print("--"); else Serial.print(_extTemp, 1);
    Serial.print(" seuils=");
    Serial.print(_thLow(), 1);
    Serial.print("/");
    Serial.print(_thMed(), 1);
    Serial.print("/");
    Serial.print(_thHigh(), 1);
    Serial.print(" savoir=");
    Serial.print(_knowCount);
    Serial.print(" ban=");
    Serial.print(_banned);
    Serial.print(" ress=");
    if (isnan(_lastApparent)) Serial.print("--"); else Serial.print(_lastApparent, 1);
    Serial.print(" suspect=");
    Serial.print(_suspect ? 'o' : 'n');
    // Progression vers le figement du cas en cours : nb d'observations
    // concordantes deja recoltees pour l'action evaluee (ou l'action
    // courante a la derniere temperature vue), temp de reference, conf.
    Serial.print(" fige=");
    if (_pending || _count > 0) {
        FanAction a = _pending ? _pendingAction : _current;
        float tref = _pending ? _pendingTempBefore
                              : _buf[(_head + CAPACITY - 1) % CAPACITY].tempBefore;
        Serial.print(similarObs(a, tref));
        Serial.print(" figetemp=");
        Serial.print(tref, 1);
        Serial.print(" conf=");
        Serial.print(confOf(a, tref));
    } else {
        Serial.print("- figetemp=-- conf=-");
    }
    Serial.print(" decision=");
    if (_hasDecision) {
        Serial.print('v'); Serial.print(_lastDecision.speed);
        Serial.print(_lastDecision.dir == DIR_NORMAL ? "/N" : "/R");
    } else Serial.print('-');
    Serial.print(" evaluating=");
    if (_pending) {
        Serial.print('v'); Serial.print(_pendingAction.speed);
        Serial.print(_pendingAction.dir == DIR_NORMAL ? "/N" : "/R");
    } else Serial.print('-');
    Serial.println();
}
