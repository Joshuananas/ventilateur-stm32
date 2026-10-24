#include "learning.h"
#include <string.h>
#include "safe_serial.h"

FanLearner::FanLearner()
    : _th{{24.0f, 27.0f, 30.0f}, {24.0f, 27.0f, 30.0f}}
{
    _current = {0, DIR_NORMAL};
}

FanAction FanLearner::fallback(float temp) const
{
    uint8_t s = temp >= _thHigh() ? 6 : temp >= _thMed() ? 4
              : temp >= _thLow() ? 2 : 0;
    // Vitesses bannies : on monte au cran libre superieur, sinon inferieur
    while (s < 6 && banned(s)) s++;
    while (s > 0 && banned(s)) s--;
    return {s, DIR_NORMAL};
}

float FanLearner::avgDelta(uint8_t speed, FanDirection dir,
                           float temp, float hum, bool similarOnly) const
{
    float sum = 0;
    uint16_t n = 0;
    for (uint8_t i = 0; i < _count; i++) {
        const Observation& o = _buf[i];
        if (o.action.speed == speed && o.action.dir == dir) {
            if (similarOnly &&
                (fabsf(o.tempBefore - temp) > SIMILAR_TEMP_BAND ||
                 (!isnan(o.humBefore) && !isnan(hum) &&
                  fabsf(o.humBefore - hum) > SIMILAR_HUM_BAND)))
                continue;
            sum += o.delta;
            n++;
        }
    }
    // Les connaissances figees pesent n * confiance : une entree
    // contredite recemment influence moins les decisions.
    for (uint8_t i = 0; i < _knowCount; i++) {
        const Knowledge& k = _know[i];
        if (k.speed == speed && k.dir == dir) {
            if (similarOnly &&
                (fabsf(k.tempC - temp) > SIMILAR_TEMP_BAND ||
                 (!isnan(k.humC) && !isnan(hum) &&
                  fabsf(k.humC - hum) > SIMILAR_HUM_BAND)))
                continue;
            float w = k.n * k.conf;
            sum += k.delta * w;
            n += (uint16_t)(w + 0.5f);
        }
    }
    return n > 0 ? sum / n : 1e9f;
}

FanAction FanLearner::learned(float temp, float hum) const
{
    // Privilegie les observations dans des conditions similaires
    // (tempBefore proche), repli sur toutes les observations sinon.
    FanAction best = fallback(temp);
    float bestAvg = 1e9f;

    for (int pass = 0; pass < 2; pass++) {
        bool similarOnly = (pass == 0);
        for (uint8_t speed = 0; speed <= 6; speed++) {
            if (banned(speed)) continue;   // ex. v1 bruyante
            for (uint8_t d = 0; d <= 1; d++) {
                float avg = avgDelta(speed, (FanDirection)d, temp, hum, similarOnly);
                if (avg < bestAvg) {
                    bestAvg = avg;
                    best = {speed, (FanDirection)d};
                }
            }
        }
        if (bestAvg < 1e8f) break; // trouve en conditions similaires
    }

    // Economie : parmi les vitesses dont le delta moyen est quasiment
    // equivalent au meilleur (+0.10C), on prend la PLUS BASSE — inutile
    // de tourner a fond si vitesse 1 fait le meme effet.
    for (uint8_t speed = 0; speed < best.speed; speed++) {
        if (banned(speed)) continue;
        float avg = avgDelta(speed, best.dir, temp, hum, false);
        if (avg <= bestAvg + 0.10f) {
            best = {speed, best.dir};
            break;
        }
    }
    return best;
}

void FanLearner::adaptThresholds(float temp)
{
    // Ajuste lentement les seuils DE LA SAISON COURANTE vers les
    // temperatures observees — c'est ainsi que le systeme "apprend"
    // la temperature typique ete vs hiver.
    float* th = _th[_winter ? 1 : 0];
    for (int i = 0; i < 3; i++) th[i] += (temp - th[i]) * THRESH_ADAPT_RATE;
    // Garde l'ordre et un ecart minimum de 1.5 degre
    if (th[1] < th[0] + 1.5f) th[1] = th[0] + 1.5f;
    if (th[2] < th[1] + 1.5f) th[2] = th[1] + 1.5f;
}

void FanLearner::setContext(bool winter, bool sunGain, float extTemp,
                            bool airing)
{
    _winter = winter;
    _sunGain = sunGain;
    _extTemp = extTemp;
    _airing = airing;
}

void FanLearner::noteTemp(float temp)
{
    if (isnan(temp)) return;
    uint32_t now = millis();
    // Pente mesuree toutes les ~5 min minimum, lissee par EMA (0.3).
    // La derive capte l'evolution "naturelle" de la piece (meteo,
    // chauffage, VMC...) — elle servira a corriger les observations.
    if (!isnan(_tPrev) && now - _tPrevTs >= 5UL*60UL*1000UL) {
        float dtMin = (now - _tPrevTs) / 60000.0f;
        float rate = (temp - _tPrev) / dtMin;           // C/min
        _driftRate = isnan(_driftRate) ? rate
                                     : _driftRate + (rate - _driftRate) * 0.3f;
        _tPrev = temp;
        _tPrevTs = now;
    } else if (isnan(_tPrev)) {
        _tPrev = temp;
        _tPrevTs = now;
    }
}

void FanLearner::invalidatePending(const char* why)
{
    if (_pending) {
        _pending = false;
        Serial.print(">>> Observation annulee : ");
        Serial.println(why);
    }
}

void FanLearner::syncApplied(const FanAction& a, bool known)
{
    if (_pending && (!known || a.speed != _pendingAction.speed
        || (a.speed > 0 && a.dir != _pendingAction.dir)))
        invalidatePending("action non appliquee ou etat modifie");
    if (known) _current = a;
}

void FanLearner::recordPreference(const FanAction& a, float temp, float hum)
{
    int match = -1, weakest = 0;
    for (int i = 0; i < 16; i++) {
        StoredPreference& p = _preferences[i];
        if (p.votes < _preferences[weakest].votes) weakest = i;
        if (!p.votes || p.winter != uint8_t(_winter)
            || fabsf(p.temp - temp) > SIMILAR_TEMP_BAND
            || fabsf(p.hum - hum) > SIMILAR_HUM_BAND) continue;
        if (p.speed == a.speed && p.dir == a.dir) match = i;
        else p.votes = p.votes > 2 ? p.votes - 2 : 0;
    }
    if (match < 0) {
        match = weakest;
        _preferences[match] = {temp, hum, a.speed, uint8_t(a.dir), 0, uint8_t(_winter)};
    }
    StoredPreference& p = _preferences[match];
    p.temp = (p.temp + temp) * 0.5f;
    p.hum = (p.hum + hum) * 0.5f;
    p.votes = min(10, int(p.votes) + 1);
    _learnDirty = true;
}

bool FanLearner::preferred(float temp, float hum, FanAction& action) const
{
    float best = -1;
    for (const StoredPreference& p : _preferences) {
        if (!p.votes || banned(p.speed) || p.winter != uint8_t(_winter)
            || fabsf(p.temp - temp) > SIMILAR_TEMP_BAND
            || fabsf(p.hum - hum) > SIMILAR_HUM_BAND) continue;
        float score = p.votes / (1.0f + fabsf(p.temp - temp) + fabsf(p.hum - hum) / 12.0f);
        if (score > best) {
            best = score;
            action = {p.speed, FanDirection(p.dir)};
        }
    }
    return best >= 0;
}

void FanLearner::evaluate(float temp, float humidity)
{
    if (!isfinite(temp) || !isfinite(humidity)) {
        invalidatePending("mesure capteur invalide");
        return;
    }
    _lastApparent = apparentTemp(temp, humidity);
    if (_pending && millis() - _pendingTs >= EVAL_DELAY_MS) {
        if (fabsf(temp - _pendingTempBefore) > 1.5f || fabsf(humidity - _pendingHum) > 12.0f)
            invalidatePending("conditions trop differentes");
        else completePending(temp);
    }
}

void FanLearner::observeManual(const FanAction& a, float temp, float humidity)
{
    followManual(a);
    if (!isfinite(temp) || !isfinite(humidity)) return;
    recordPreference(a, temp, humidity);
    _pending = true;
    _pendingTs = millis();
    _pendingAction = a;
    _pendingTempBefore = temp;
    _pendingHum = humidity;
    _pendingDrift = isnan(_driftRate)
        ? 0.0f : _driftRate * (EVAL_DELAY_MS / 60000.0f);
    _current = a; // l'etat reel suit l'action manuelle
}

void FanLearner::completePending(float tempAfter)
{
    Observation& o = _buf[_head];
    o.timestamp  = _pendingTs;
    o.action     = _pendingAction;
    o.tempBefore = _pendingTempBefore;
    o.humBefore  = _pendingHum;
    o.tempAfter  = tempAfter;
    // Delta CORRIGE : on retranche la derive predite sans action.
    // Un coucher de soleil qui refroidit la piece de -0.4C n'est plus
    // attribue au ventilateur.
    float raw = tempAfter - _pendingTempBefore;
    o.expected = _pendingDrift;
    o.delta    = raw - _pendingDrift;

    _head = (_head + 1) % CAPACITY;
    if (_count < CAPACITY) _count++;
    _pending = false;
    _learnDirty = true;
    _event = true; // "compris" : pulse LED

    // Suspicion "ventilateur sans reponse" : un cas fige a haute
    // confiance predisait un effet net (|delta| > 0.25C) mais la mesure
    // est ~0. 3 d'affilee -> suspicion ; un effet mesure reel -> levee.
    // Pendant la suspicion, consolidate() gele les confiances pour ne
    // pas defigier l'apprentissage a cause d'une panne physique.
    const Knowledge* k = knowFor(o.action, o.tempBefore);
    if (k && k->conf >= 0.6f && fabsf(k->delta) > 0.25f) {
        if (fabsf(o.delta) < 0.15f) {
            _noResp++;
            if (_noResp >= 3 && !_suspect) {
                _suspect = true;
                Serial.println(">>> SUSPICION : le ventilateur ne semble "
                               "pas repondre (debranche ? RF perdu ?)");
            }
        } else {
            _noResp = 0;
            if (_suspect) {
                _suspect = false;
                Serial.println(">>> Ventilateur de nouveau reactif");
            }
        }
    }

    adaptThresholds(_pendingTempBefore);
    consolidate(o);

    Serial.print(">>> Observation #");
    Serial.print(_count);
    Serial.print(" : action v");
    Serial.print(o.action.speed);
    Serial.print(o.action.dir == DIR_NORMAL ? "N" : "R");
    Serial.print(" | ");
    Serial.print(o.tempBefore, 1);
    Serial.print(" -> ");
    Serial.print(tempAfter, 1);
    Serial.print(" C | delta = ");
    Serial.print(raw, 2);
    Serial.print(" - derive attendue ");
    Serial.print(o.expected, 2);
    Serial.print(" = effet ");
    Serial.println(o.delta, 2);
}

void FanLearner::consolidate(const Observation& o)
{
    // Met a jour une connaissance existante proche (meme action,
    // conditions similaires) : moyenne glissante.
    for (uint8_t i = 0; i < _knowCount; i++) {
        Knowledge& k = _know[i];
        if (k.speed == o.action.speed && k.dir == o.action.dir
            && fabsf(k.tempC - o.tempBefore) <= SIMILAR_TEMP_BAND
            && (isnan(k.humC) || isnan(o.humBefore) ||
                fabsf(k.humC - o.humBefore) <= SIMILAR_HUM_BAND)) {
            // n plafonne a 10 : une observation recente garde toujours
            // au moins ~10% de poids -> la moyenne reste adaptable.
            float w = k.n < 10 ? k.n : 10;
            k.delta = (k.delta * w + o.delta) / (w + 1);
            k.tempC = (k.tempC * w + o.tempBefore) / (w + 1);
            if (!isnan(o.humBefore))
                k.humC = isnan(k.humC) ? o.humBefore
                                       : (k.humC * w + o.humBefore) / (w + 1);
            if (k.n < 255) k.n++;
            // Confiance : renforcee si l'obs confirme, degradee si elle
            // contredit (ecart > 0.3C). Sous 0.3 -> entree defiegee.
            // Confiance gelee tant que le ventilateur est suspecte
            // hors tension : des effets ~0 ne sont pas des contradictions.
            if (!_suspect)
                k.conf += (fabsf(o.delta - k.delta) <= 0.3f) ? 0.10f : -0.15f;
            if (k.conf > 1.0f) k.conf = 1.0f;
            if (k.conf < 0.3f) {
                Serial.printf(">>> Connaissance v%u%s a %0.1fC DEFIGEE (confiance %0.2f)\n",
                              k.speed, k.dir == DIR_NORMAL ? "N" : "R",
                              k.tempC, k.conf);
                _know[i] = _know[--_knowCount]; // swap-remove
                return;
            }
            Serial.printf(">>> Connaissance v%u%s a %0.1fC : %u obs, delta %0.2f, conf %0.2f\n",
                          k.speed, k.dir == DIR_NORMAL ? "N" : "R",
                          k.tempC, k.n, k.delta, k.conf);
            return;
        }
    }
    // Fige une nouvelle connaissance quand assez d'observations
    // concordantes existent dans des conditions proches.
    uint8_t n = 0;
    for (uint8_t i = 0; i < _count; i++) {
        const Observation& x = _buf[i];
        if (x.action.speed == o.action.speed && x.action.dir == o.action.dir
            && fabsf(x.tempBefore - o.tempBefore) <= SIMILAR_TEMP_BAND)
            n++;
    }
    if (n >= KNOW_MIN_OBS && _knowCount < KNOW_CAP) {
        Knowledge& k = _know[_knowCount++];
        k.speed = o.action.speed;
        k.dir = o.action.dir;
        k.n = 1;
        k.tempC = o.tempBefore;
        k.humC = o.humBefore;
        k.delta = o.delta;
        k.conf = 0.6f; // confiance initiale prudente
        Serial.printf(">>> Nouvelle connaissance figee : v%u%s vers %0.1fC\n",
                      k.speed, k.dir == DIR_NORMAL ? "N" : "R", k.tempC);
    }

    // Deduplication : deux entrees figees qui se recouvrent (meme action,
    // conditions dans les bandes de similarite) n'apportent rien ->
    // fusion, moyenne ponderee par n, confiance = la meilleure des deux.
    for (uint8_t i = 0; i < _knowCount; i++) {
        for (uint8_t j = i + 1; j < _knowCount; j++) {
            Knowledge& a = _know[i];
            Knowledge& b = _know[j];
            if (a.speed != b.speed || a.dir != b.dir) continue;
            if (fabsf(a.tempC - b.tempC) > SIMILAR_TEMP_BAND) continue;
            if (!isnan(a.humC) && !isnan(b.humC)
                && fabsf(a.humC - b.humC) > SIMILAR_HUM_BAND) continue;
            float w = (float)a.n + b.n;
            a.delta = (a.delta * a.n + b.delta * b.n) / w;
            a.tempC = (a.tempC * a.n + b.tempC * b.n) / w;
            if (!isnan(b.humC))
                a.humC = isnan(a.humC) ? b.humC
                                       : (a.humC * a.n + b.humC * b.n) / w;
            a.n = (a.n + b.n > 255) ? 255 : a.n + b.n;
            if (b.conf > a.conf) a.conf = b.conf;
            _know[j] = _know[--_knowCount]; // swap-remove b
            Serial.printf(">>> Doublon fusionne : v%u%s vers %0.1fC\n",
                          a.speed, a.dir == DIR_NORMAL ? "N" : "R", a.tempC);
            j--; // revoir le nouvel index j
        }
    }
}

// Indice de confort type humidex : T + 0.5555*(e - 10),
// e = pression de vapeur (hPa) deduite de T et HR.
// 21-26C neutre, >29 lourd, >34 oppressant.
float FanLearner::apparentTemp(float t, float h) const
{
    float e = 6.112f * powf(10.0f, 7.5f * t / (237.7f + t)) * h / 100.0f;
    return t + 0.5555f * (e - 10.0f);
}

uint8_t FanLearner::similarObs(const FanAction& a, float temp) const
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < _count; i++) {
        const Observation& x = _buf[i];
        if (x.action.speed == a.speed && x.action.dir == a.dir
            && fabsf(x.tempBefore - temp) <= SIMILAR_TEMP_BAND)
            n++;
    }
    return n;
}

const FanLearner::Knowledge* FanLearner::knowFor(const FanAction& a,
                                                float temp) const
{
    for (uint8_t i = 0; i < _knowCount; i++) {
        const Knowledge& k = _know[i];
        if (k.speed == a.speed && k.dir == a.dir
            && fabsf(k.tempC - temp) <= SIMILAR_TEMP_BAND)
            return &k;
    }
    return nullptr;
}

int8_t FanLearner::confOf(const FanAction& a, float temp) const
{
    for (uint8_t i = 0; i < _knowCount; i++) {
        const Knowledge& k = _know[i];
        if (k.speed == a.speed && k.dir == a.dir
            && fabsf(k.tempC - temp) <= SIMILAR_TEMP_BAND)
            return (int8_t)(k.conf * 100.0f);
    }
    return -1;
}

void FanLearner::reset()
{
    _head = _count = 0;
    memset(_preferences, 0, sizeof(_preferences));
    _pending = false;
    _current = {0, DIR_NORMAL};
    _hasDecision = false;
    for (int s = 0; s < 2; s++) {
        _th[s][0] = 24.0f; _th[s][1] = 27.0f; _th[s][2] = 30.0f;
    }
    _knowCount = 0;
    _learnDirty = true;
    _lastReason = "reset";
    _hasLastCmd = false;
}

Decision FanLearner::update(float temp, float humidity)
{
    uint32_t now = millis();

    // 1) Cloture l'observation en cours si le delai est ecoule
    evaluate(temp, humidity);

    // 2) Delai minimum entre deux commandes
    if (_hasLastCmd && now - _lastCmdTs < MIN_CMD_MS) {
        _lastReason = "delai min 90s";
        return {_current, false, _lastReason};
    }

    // Humidex indicatif : les seuils utilisent la temperature de l'air.
    // L'humidite garde son ajustement explicite, sans double comptage.
    // Les observations et preferences gardent T/H brutes (mesures
    // physiques reproductibles).
    _lastApparent = apparentTemp(temp, humidity);

    bool learnedMode = _count >= MIN_SAMPLES;
    FanAction a = learnedMode ? learned(temp, humidity) : fallback(temp);
    const char* reason = learnedMode ? "appris" : "seuils fixes";

    // 2b) Exploration controlee (epsilon-greedy prudent) : ~1 fois / 4 h,
    // sans mesure en cours, on teste la vitesse la MOINS observee dans
    // les conditions courantes pour ne pas rester fige sur un choix
    // jamais remis en question. Sens plausible selon le contexte.
    if (learnedMode && !_pending && now - _lastExploreTs >= EXPLORE_MS) {
        FanDirection d = (_winter || (temp < 21.0f && humidity > 60.0f))
                         ? DIR_REVERSE : DIR_NORMAL;
        uint8_t best = 255, bestN = 255;
        for (uint8_t s = 1; s <= 6; s++) {
            if (banned(s)) continue;
            uint8_t n = similarObs({s, d}, temp);
            if (n < bestN) { bestN = n; best = s; }
        }
        if (best != 255 && bestN < 3 && best != a.speed) {
            a.speed = best;
            a.dir = d;
            reason = "exploration";
            _lastExploreTs = now;
        }
    }

    // 3) Facteur secondaire humidite : +1 vitesse si humide ET chaud
    if (humidity > HUMIDITY_FACTOR && temp >= _thMed() && a.speed < 6) {
        a.speed++;
        reason = learnedMode ? "appris+humidite" : "seuils+humidite";
    }

    // 3b) Anticipation solaire : baie vitree plein sud + fort ensoleillement
    // -> la temperature va monter, on anticipe le confort (+1 vitesse max)
    if (_sunGain && temp >= _thLow() - 0.5f && a.speed < 6) {
        a.speed++;
        reason = "anticipation soleil";
    }

    // 3a2) Estimation jour/nuit par tendance sur 30 min (sans RTC) :
    // jour typique -> T monte et H descend ; nuit -> inverse.
    // La meteo (is_day/radiation) ecrase cette estimation quand dispo.
    if (isnan(_trT0) || now - _trTs >= 30UL*60UL*1000UL) {
        if (!isnan(_trT0) && !isnan(_trH0)) {
            float dT = temp - _trT0, dH = humidity - _trH0;
            _dayEst = (dT > 0.20f) || (dT > 0.0f && dH < -1.0f);
        }
        _trT0 = temp; _trH0 = humidity; _trTs = now;
    }

    // 3bb) Plancher confort : quand il fait chaud, la brise rafraichit la
    // personne meme si la temperature de la piece ne baisse pas -> on
    // impose au moins v2 (le delta piece sous-estime l'effet ressenti).
    if (temp >= _thMed() && a.speed == 0) {
        a.speed = 2;
        a.dir = DIR_NORMAL;
        reason = "confort : brise minimum";
    }

    // 3bd) Aeration en cours : l'air exterieur domine la temperature.
    // Hors hiver on garde une circulation minimum (v1) pour renouveler
    // l'air de la piece ; en hiver on laisse faire (aeration breve).
    if (_airing && !_winter && a.speed == 0) {
        a.speed = banned(1) ? 2 : 1;
        a.dir = DIR_NORMAL;
        reason = "aeration : circulation";
    }

    // 3c) Logique saisonniere du sens de rotation :
    //  - hiver + frais/humide : REVERSE a basse vitesse = l'air chaud accumule
    //    au plafond (radiateurs) redescend SANS courant d'air direct
    //  - chaud : NORMAL (brise vers le bas)
    //  - frais + humide : on evite absolument le souffle vers le bas
    bool coldHumid = (temp < 21.0f && humidity > 60.0f);
    if (coldHumid) {
        a.dir = DIR_REVERSE;
        if (a.speed > 2) a.speed = 2;
        if (a.speed == 0 && temp < 20.0f) a.speed = 1; // destratification douce
        reason = "destratification (frais+humide)";
    } else if (_winter && temp < _thMed()) {
        a.dir = DIR_REVERSE;
        if (a.speed > 2) a.speed = 2;
        reason = "hiver : brassage plafond";
    } else if (temp >= _thMed()) {
        a.dir = DIR_NORMAL;
    }

    // 3d) Nuit estimee : silence prioritaire, on plafonne a v3 sauf
    // si vraiment chaud (ressenti au-dessus du seuil haut).
    if (!_dayEst && a.speed > 3 && temp < _thHigh()) {
        a.speed = 3;
        reason = "nuit : silence";
    }

    // 3e) Anti-oscillation pres des seuils : bande de doute +-0.4C
    // autour de chaque repere -> on conserve la vitesse actuelle pour
    // eviter le cycle on/off. Exception : si T monte vite (>1.2C/h),
    // on laisse passer une montee de vitesse (confort prioritaire).
    bool userPreference = preferred(temp, humidity, a);
    if (userPreference) reason = "preference utilisateur";
    if (!userPreference && a.speed != _current.speed) {
        bool nearTh = fabsf(temp - _thLow())  < 0.4f
                   || fabsf(temp - _thMed())  < 0.4f
                   || fabsf(temp - _thHigh()) < 0.4f;
        bool risingFast = !isnan(_driftRate) && _driftRate > 0.02f;
        if (nearTh && !(a.speed > _current.speed && risingFast)) {
            a.speed = _current.speed;
            reason = "hysteresis seuil";
        }
    }

    // 4) Delai minimum entre changements de sens
    if (a.dir != _current.dir && _hasLastCmd &&
        now - _lastDirTs < MIN_DIR_CHANGE_MS) {
        a.dir = _current.dir; // conserve le sens actuel
        reason = "sens bloque 3min";
    }

    if (a.speed == 0) a.dir = _current.dir;
    _lastDecision = a;
    _hasDecision = true;
    bool changed = (a.speed != _current.speed || a.dir != _current.dir);
    if (changed) {
        if (a.dir != _current.dir) _lastDirTs = now;
        _lastCmdTs = now;
        _hasLastCmd = true;

        // Nouvelle observation en attente d'evaluation
        _pending = true;
        _pendingTs = now;
        _pendingAction = a;
        _pendingTempBefore = temp;
        _pendingHum = humidity;
        _pendingDrift = isnan(_driftRate)
            ? 0.0f : _driftRate * (EVAL_DELAY_MS / 60000.0f);

        _current = a;
    }

    _lastReason = reason;
    return {_current, changed, reason};
}
