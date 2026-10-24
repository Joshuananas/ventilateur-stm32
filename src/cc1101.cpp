#include "cc1101.h"
#include "pins.h"
#include <SPI.h>
#include "safe_serial.h"

// Registres / strobes CC1101
#define REG_IOCFG2   0x00
#define REG_IOCFG0   0x02
#define REG_FIFOTHR  0x03
#define REG_PKTLEN   0x06
#define REG_PKTCTRL0 0x08
#define REG_FSCTRL1  0x0B
#define REG_FREQ2    0x0D
#define REG_FREQ1    0x0E
#define REG_FREQ0    0x0F
#define REG_MDMCFG4  0x10
#define REG_MDMCFG3  0x11
#define REG_MDMCFG2  0x12
#define REG_MDMCFG1  0x13
#define REG_DEVIATN  0x15
#define REG_MCSM0    0x18
#define REG_FOCCFG   0x19
#define REG_AGCCTRL2 0x1B
#define REG_AGCCTRL1 0x1C
#define REG_AGCCTRL0 0x1D
#define REG_FREND1   0x21
#define REG_FREND0   0x22
#define REG_FSCAL3   0x23
#define REG_FSCAL1   0x25
#define REG_TEST2    0x2C
#define REG_TEST1    0x2D
#define REG_TEST0    0x2E
#define REG_PATABLE  0x3E

#define STROBE_SRES  0x30
#define STROBE_SRX   0x34
#define STROBE_STX   0x35
#define STROBE_SIDLE 0x36
#define STROBE_SFTX  0x3B
#define STROBE_SFRX  0x3A

#define STATUS_PARTNUM 0xF0
#define STATUS_VERSION 0xF1
#define STATUS_MARCSTATE 0xF5

#define CC_READ_BIT  0x80
#define CC_BURST_BIT 0x40

static const SPISettings CC_SPI(1000000, MSBFIRST, SPI_MODE0);

// Capture RX : polling de GDO0 dans poll() — l'EXTI comme l'input capture
// TIM2 perdaient des fronts en plein paquet ; le polling (comme sniff())
// suit le flux continu (~52 ms par paquet, syncs tous les ~100 pulses).

CC1101* CC1101::_instance = nullptr;

// Attente SO (MISO) bas avec timeout — evite tout blocage si le chip est absent
static void waitSo(uint8_t misoPin)
{
    uint32_t t0 = micros();
    while (digitalRead(misoPin) == HIGH && micros() - t0 < 50000) {}
}

void CC1101::strobe(uint8_t cmd)
{
    SPI.beginTransaction(CC_SPI);
    digitalWrite(_csn, LOW);
    waitSo(SPI_MISO);
    SPI.transfer(cmd);
    digitalWrite(_csn, HIGH);
    SPI.endTransaction();
}

void CC1101::spiWrite(uint8_t addr, uint8_t val)
{
    SPI.beginTransaction(CC_SPI);
    digitalWrite(_csn, LOW);
    waitSo(SPI_MISO);
    SPI.transfer(addr);
    SPI.transfer(val);
    digitalWrite(_csn, HIGH);
    SPI.endTransaction();
}

void CC1101::spiWriteBurst(uint8_t addr, const uint8_t* vals, uint8_t n)
{
    SPI.beginTransaction(CC_SPI);
    digitalWrite(_csn, LOW);
    waitSo(SPI_MISO);
    SPI.transfer(addr | CC_BURST_BIT);
    for (uint8_t i = 0; i < n; i++) SPI.transfer(vals[i]);
    digitalWrite(_csn, HIGH);
    SPI.endTransaction();
}

uint8_t CC1101::spiRead(uint8_t addr)
{
    SPI.beginTransaction(CC_SPI);
    digitalWrite(_csn, LOW);
    waitSo(SPI_MISO);
    SPI.transfer(addr | CC_READ_BIT);
    uint8_t v = SPI.transfer(0);
    digitalWrite(_csn, HIGH);
    SPI.endTransaction();
    return v;
}

uint8_t CC1101::spiReadStatus(uint8_t addr)
{
    SPI.beginTransaction(CC_SPI);
    digitalWrite(_csn, LOW);
    waitSo(SPI_MISO);
    SPI.transfer(addr | CC_READ_BIT | CC_BURST_BIT);
    uint8_t v = SPI.transfer(0);
    digitalWrite(_csn, HIGH);
    SPI.endTransaction();
    return v;
}

void CC1101::reset()
{
    digitalWrite(_csn, HIGH); delayMicroseconds(50);
    digitalWrite(_csn, LOW);  delayMicroseconds(50);
    digitalWrite(_csn, HIGH); delayMicroseconds(50);
    SPI.beginTransaction(CC_SPI);
    digitalWrite(_csn, LOW);
    waitSo(SPI_MISO);
    SPI.transfer(STROBE_SRES);
    digitalWrite(_csn, HIGH);
    SPI.endTransaction();
    delay(5);
}

void CC1101::configRxOok()
{
    // Config 433.92 MHz, OOK/ASK, sortie serie asynchrone sur GDO0.
    // Base : reglages SmartRF typiques pour reception OOK ~2.4 kBaud.
    spiWrite(REG_IOCFG2,   0x0D); // GDO2 = sortie serie (non utilise)
    spiWrite(REG_IOCFG0,   0x0D); // GDO0 = sortie serie asynchrone
    spiWrite(REG_FIFOTHR,  0x47);
    spiWrite(REG_PKTLEN,   0xFF);
    spiWrite(REG_PKTCTRL0, 0x32); // mode serie asynchrone
    spiWrite(REG_FSCTRL1,  0x06);
    spiWrite(REG_FREQ2,    0x10); // 433.92 MHz
    spiWrite(REG_FREQ1,    0xB0);
    spiWrite(REG_FREQ0,    0x71);
    spiWrite(REG_MDMCFG4,  0x68); // bande passante RX ~270 kHz (540k = bruit
                                 // permanent -> plus aucun sync detecte)
    spiWrite(REG_MDMCFG3,  0x93);
    spiWrite(REG_MDMCFG2,  0xB0); // OOK + DEM_DCFILT_OFF : le filtre DC derivait
                                 // pendant le sync 7.3 ms et faussait le seuil
    spiWrite(REG_MDMCFG1,  0x22);
    spiWrite(REG_DEVIATN,  0x15);
    spiWrite(REG_MCSM0,    0x10); // cal auto idle->RX/TX, PO_TIMEOUT desactive
    spiWrite(REG_FOCCFG,   0x14);
    spiWrite(REG_AGCCTRL2, 0x87); // AGC, gain LNA reduit : la telecommande proche
                                 // saturait le RX a gain max (0xC7) -> trames
                                 // tronquees a ~12-15 bits
    spiWrite(REG_AGCCTRL1, 0x40); // AGC autour du sync
    spiWrite(REG_AGCCTRL0, 0x92); // filtre AGC plus reactif
    spiWrite(REG_FREND1,   0xB6);
    spiWrite(REG_FREND0,   0x11);
    spiWrite(REG_FSCAL3,   0xE9);
    spiWrite(REG_FSCAL1,   0x2A);
    spiWrite(REG_TEST2,    0x81);
    spiWrite(REG_TEST1,    0x35);
    spiWrite(REG_TEST0,    0x09);
}

void CC1101::configTxOok()
{
    configRxOok();
    spiWrite(REG_IOCFG0, 0x2E); // GDO0 en entree : signal TX serie asynchrone
    spiWrite(REG_MDMCFG2, 0x30);
    // OOK : le '1' utilise PATABLE[FREND0.PATABLE] (=index 1 car FREND0=0x11).
    // Il faut donc ecrire l'index 1, pas seulement l'index 0.
    // PATABLE[0]=0x00 ('0' = pas de porteuse), PATABLE[1]=0xC0 ('1' = +10 dBm)
    static const uint8_t pa[8] = { 0x00, 0xC0, 0, 0, 0, 0, 0, 0 };
    spiWriteBurst(REG_PATABLE, pa, 8);
}

bool CC1101::begin(uint8_t csn, uint8_t gdo0)
{
    _csn = csn; _gdo0 = gdo0;
    memset(_counts, 0, sizeof(_counts));
    SPI.begin();
    reset();
    uint8_t pn = spiReadStatus(STATUS_PARTNUM);
    uint8_t ver = spiReadStatus(STATUS_VERSION);
    Serial.print("CC1101 PARTNUM=0x"); Serial.print(pn, HEX);
    Serial.print(" VERSION=0x"); Serial.print(ver, HEX);
    // Test ecriture/lecture : ecrire FSCTRL1 puis relire
    spiWrite(REG_FSCTRL1, 0x06);
    uint8_t rb = spiRead(REG_FSCTRL1);
    Serial.print(" FSCTRL1_rw=0x"); Serial.println(rb, HEX);
    return (pn == 0x00 && ver != 0x00 && ver != 0xFF);
}

void CC1101::setFreq(float mhz)
{
    uint32_t f = (uint32_t)(mhz * 1e6 / 26e6 * 65536.0 + 0.5);
    spiWrite(REG_FREQ2, (f >> 16) & 0xFF);
    spiWrite(REG_FREQ1, (f >> 8) & 0xFF);
    spiWrite(REG_FREQ0, f & 0xFF);
}

void CC1101::rxOn()
{
    configRxOok();
    strobe(STROBE_SFRX);
    strobe(STROBE_SRX);
}

void CC1101::idle()
{
    strobe(STROBE_SIDLE);
}

// ---------------- Ecoute en arriere-plan ----------------

// Capture par polling dans poll() : ni l'EXTI logiciel ni l'input capture
// TIM2 ne voyaient que ~8 pulses par paquet (le mode BOTHEDGE de la core
// lit un mauvais registre -> deltas bidons). Le polling en boucle — comme
// sniff(), qui capture le flux complet — est le seul fiable.
void CC1101::onEdge(uint32_t now, int cur)
{
    // Run-lengths reconstruits par niveau (le niveau du pulse finissant est
    // lu juste apres le front). La trame ne commence qu'au sync : un HIGH
    // > 3000 us termine par un front descendant. Le bruit de fond du CC1101
    // (AGC gain max) produit des fronts epars mais jamais de pulse > 3 ms.
    _edgesTotal++;
    uint32_t d = now - _lastEdgeUs;
    _lastEdgeUs = now;
    if (_frameReady) return; // trame precedente pas encore consommee

    // Front descendant apres un HIGH long = nouveau sync. La telecommande
    // emet des paquets dos a dos (gap ~3 ms seulement) : le sync precedent
    // marque donc la FIN du paquet en cours — on le commit tout de suite,
    // sans attendre un silence de 300 ms qui n'arrive jamais en appui continu.
    if (d > 3000 && cur == LOW) {
        if (_rxActive && _rxCount >= 8 && !_frameReady) {
            _rxLen = _rxCount;
            memcpy((void*)_rxLast, (const void*)_rx, _rxLen * sizeof(uint16_t));
            _rxLastLen = _rxLen;
            _frameReady = true;
        }
        _rx[0] = (uint16_t)min(d, (uint32_t)65535);
        _rxCount = 1;
        _rxLevel = 1;            // dernier niveau stocke = HIGH (le sync)
        _rxStartMs = millis();
        _rxActive = true;
        return;
    }
    if (!_rxActive) return;

    if (d < 30) return; // glitch : absorbe (le pulse en cours garde sa parite)
    uint8_t lvl = cur ? 0 : 1; // niveau du pulse qui vient de finir
    if (lvl == _rxLevel) {
        _rx[_rxCount - 1] = (uint16_t)min((uint32_t)_rx[_rxCount - 1] + d, (uint32_t)65535);
    } else if (_rxCount < MAX_PULSES) {
        _rx[_rxCount++] = (uint16_t)min(d, (uint32_t)65535);
        _rxLevel = lvl;
    }
}

void CC1101::startListening()
{
    if (_listening) return;
    _instance = this;
    configRxOok();
    strobe(STROBE_SFRX);
    strobe(STROBE_SRX);
    _rxCount = 0;
    _rxLen = 0;
    _rxActive = false;
    _frameReady = false;
    pinMode(_gdo0, INPUT);
    _tmrPrev = (uint8_t)digitalRead(_gdo0);
    _lastEdgeUs = micros();
    _listening = true;
}

void CC1101::stopListening()
{
    if (!_listening) return;
    _listening = false;
    strobe(STROBE_SIDLE);
}

void CC1101::poll()
{
    if (!_listening) return;
    // Echantillonnage de GDO0 a chaque appel : la loop tourne assez vite
    // pour suivre les fronts ~350 us (meme principe que sniff()).
    int l = digitalRead(_gdo0);
    if (l != (int)_tmrPrev) {
        _tmrPrev = (uint8_t)l;
        onEdge(micros(), l);
    }
    if (_frameReady) return;
    // Fin de trame : capture commencee au sync et silence > 300 ms
    // (les emissions contiennent parfois des gaps longs ; le decodage
    // n'utilise que la premiere trame complete)
    if (_rxActive && _rxCount >= 8 && (micros() - _lastEdgeUs) > 300000) {
        // Trame commencant dans la fenetre d'auto-ecoute apres un replay : jeter
        if (_rxStartMs < _ignoreUntil) {
            _rxCount = 0;
            _rxActive = false;
            return;
        }
        _rxLen = _rxCount;
        memcpy(_rxLast, (const void*)_rx, _rxLen * sizeof(uint16_t));
        _rxLastLen = _rxLen;
        _rxCount = 0;
        _rxActive = false;
        _frameReady = true;
    }
    // Debordement : trame trop longue, on fige ce qu'on a
    if (_rxActive && _rxCount >= MAX_PULSES) {
        _rxLen = MAX_PULSES;
        memcpy(_rxLast, (const void*)_rx, _rxLen * sizeof(uint16_t));
        _rxLastLen = _rxLen;
        _rxCount = 0;
        _rxActive = false;
        _frameReady = true;
    }
}

void CC1101::clearFrame()
{
    _frameReady = false;
    _rxLen = 0;
    _rxCount = 0;
    _rxActive = false;
    _lastEdgeUs = micros();
}

// ---------------- Decodage / reconnaissance ----------------

uint16_t CC1101::decodePacket(const uint16_t* pulses, uint16_t n,
                              uint8_t* bitsOut, uint16_t maxBits)
{
    // Cherche le sync : un pulse HIGH (index pair) > 3000 us.
    // Si rien en position paire, on tente les indices impairs (capture decalée).
    int syncIdx = -1;
    for (uint16_t i = 0; i + 1 < n; i += 2)
        if (pulses[i] > 3000) { syncIdx = i; break; }
    if (syncIdx < 0)
        for (uint16_t i = 1; i + 1 < n; i += 2)
            if (pulses[i] > 3000) { syncIdx = i; break; }
    if (syncIdx < 0) return 0;

    uint16_t nb = 0;
    for (uint16_t i = syncIdx + 2; i + 1 < n && nb < maxBits; i += 2) {
        if (pulses[i] > 3000) break; // sync suivant = fin de trame
        bitsOut[nb++] = (pulses[i] > 550) ? 1 : 0;
    }
    return (nb >= 8) ? nb : 0;
}

void CC1101::printBits(const uint8_t* bits, uint16_t nb)
{
    // Impression hexa MSB-first, dernier octet complete par des zeros
    for (uint16_t i = 0; i < nb; i += 8) {
        uint8_t b = 0;
        for (uint8_t j = 0; j < 8; j++) {
            b <<= 1;
            if (i + j < nb && bits[i + j]) b |= 1;
        }
        if (b < 0x10) Serial.print('0');
        Serial.print(b, HEX);
        Serial.print(' ');
    }
    Serial.print('(');
    Serial.print(nb);
    Serial.print(" bits)");
}

void CC1101::ensureCache()
{
    if (_cacheValid) return;
    for (uint8_t s = 0; s < MAX_SLOTS; s++)
        _slotBitLen[s] = decodePacket(_buf[s], _counts[s], _slotBits[s], MAX_BITS);
    _cacheValid = true;
}

const uint8_t* CC1101::slotBits(uint8_t s)
{
    ensureCache();
    return (s < MAX_SLOTS) ? _slotBits[s] : nullptr;
}

uint16_t CC1101::slotBitLen(uint8_t s)
{
    ensureCache();
    return (s < MAX_SLOTS) ? _slotBitLen[s] : 0;
}

uint16_t CC1101::decodeFrame(uint8_t* bitsOut, uint16_t maxBits) const
{
    return decodePacket((const uint16_t*)_rxLast, _rxLen, bitsOut, maxBits);
}

void CC1101::clearSlot(uint8_t s)
{
    if (s >= MAX_SLOTS) return;
    _counts[s] = 0;
    _slotBitLen[s] = 0;
    _cacheValid = false;
}

void CC1101::setSlot(uint8_t s, const uint16_t* pulses, uint16_t n)
{
    if (s >= MAX_SLOTS || !pulses) return;
    if (n > MAX_PULSES) n = MAX_PULSES;
    memcpy(_buf[s], pulses, n * sizeof(uint16_t));
    _counts[s] = n;
    // Normalisation parite : le premier sync (pulse > 3000) doit etre a un
    // index PAIR (niveau HIGH). S'il est a un index impair, la capture
    // commencait par un LOW : on decale d'un pulse.
    uint16_t cnt = _counts[s];
    for (uint16_t i = 0; i + 1 < cnt; i++) {
        if (_buf[s][i] > 3000) {
            if (i & 1) {
                memmove(&_buf[s][0], &_buf[s][1], (cnt - 1) * sizeof(uint16_t));
                _counts[s] = cnt - 1;
                Serial.println("[RF] Slot normalise (decale d'un pulse)");
            }
            break;
        }
    }
    _cacheValid = false;
}

// Match tolérant : les trames reçues comportent parfois des bits erronés
// (bruit, rebonds du demodulateur) et les codes boutons peuvent n'etre
// distants que de 2 bits. Strategie : compter les bits differents sur la
// longueur commune (>=36 bits) ; un slot gagne s'il est strictement le
// meilleur ET avec <=4 erreurs ET au moins 2 bits mieux que le second.
//   gagnant clair         -> return slot
//   prefixe commun mais
//   pas de gagnant clair  -> -2 (activite telecommande, bouton incertain)
//   prefixe etranger      -> -1 (inconnu)
int8_t CC1101::matchBits(const uint8_t* bits, uint16_t nb)
{
    if (nb < 8) return -1;
    ensureCache();

    // Famille de la telecommande : les 24 premiers bits sont quasi fixes
    // (2F 0F 04). On tolere 3 erreurs dessus pour classer "des notres".
    static const uint8_t FAMILY[24] = {
        0,0,1,0,1,1,1,1, 0,0,0,0,1,1,1,1, 0,0,0,0,0,1,0,0
    };
    uint16_t famErr = 0;
    for (uint16_t i = 0; i < 24 && i < nb; i++)
        if (bits[i] != FAMILY[i]) famErr++;
    bool ours = (famErr <= 3);

    int8_t best = -1; uint16_t bestErr = 0xFFFF, secondErr = 0xFFFF;
    for (uint8_t s = 0; s < MAX_SLOTS; s++) {
        uint16_t nref = _slotBitLen[s];
        if (!slotHasSignal(s) || nref < 8) continue;
        uint16_t ncmp = min(nb, nref);
        if (ncmp > 1) ncmp--; // le dernier bit d'un fragment tronque est incertain
        if (ncmp < 36) continue; // pas assez de bits pour distinguer les boutons
        uint16_t err = 0;
        for (uint16_t i = 0; i < ncmp; i++)
            if (_slotBits[s][i] != bits[i]) err++;
        if (err < bestErr) { secondErr = bestErr; bestErr = err; best = (int8_t)s; }
        else if (err < secondErr) secondErr = err;
    }
    if (best >= 0 && bestErr <= 4 && (secondErr == 0xFFFF || secondErr >= bestErr + 2))
        return best;
    return ours ? (int8_t)-2 : (int8_t)-1;
}

int8_t CC1101::matchSlot(const uint16_t* pulses, uint16_t n)
{
    uint8_t bits[MAX_BITS];
    uint16_t nb = decodePacket(pulses, n, bits, MAX_BITS);
    return matchBits(bits, nb);
}

uint8_t CC1101::gdoLevel() const
{
    return digitalRead(_gdo0);
}

int8_t CC1101::rssi()
{
    uint8_t r = spiReadStatus(0x34); // RSSI status reg
    int16_t d = (r >= 128) ? (int16_t)r - 256 : (int16_t)r;
    return (int8_t)(d / 2 - 74);
}

void CC1101::selectSlot(uint8_t s)
{
    if (s < MAX_SLOTS) _slot = s;
}

uint16_t CC1101::sniff(uint32_t durationMs)
{
    bool wasListening = _listening;
    if (wasListening) stopListening();
    configRxOok();
    strobe(STROBE_SFRX);
    strobe(STROBE_SRX);
    _counts[_slot] = 0;
    pinMode(_gdo0, INPUT);

    // 1) Attente d'activite : compte les fronts sur 200 ms glissants.
    //    Le bruit de fond donne des fronts epars ; une telecommande en donne en rafale.
    Serial.println("[RF] En attente d'activite... appuie sur la telecommande");
    uint32_t t0 = millis();
    int last = digitalRead(_gdo0);
    uint16_t recent = 0;
    uint32_t winStart = millis();
    bool triggered = false;
    while (millis() - t0 < durationMs) {
        int l = digitalRead(_gdo0);
        if (l != last) { last = l; recent++; }
        if (millis() - winStart >= 200) {
            if (recent >= 8) { triggered = true; break; } // rafale detectee
            recent = 0; winStart = millis();
        }
    }
    if (!triggered) {
        strobe(STROBE_SIDLE);
        if (wasListening) startListening();
        return 0;
    }

    Serial.println("[RF] Activite detectee ! Capture en cours...");

    // 2) Capture des timings jusqu'a silence de 300 ms ou buffer plein.
    //    Le premier pulse peut etre un LOW si la capture demarre pendant
    //    un silence : on normalise ensuite pour que _buf commence par HIGH.
    uint16_t& count = _counts[_slot];
    int firstLevel = last; // niveau pendant le premier pulse capture
    uint32_t lastEdge = micros();
    uint32_t quietStart = millis();
    while (count < MAX_PULSES) {
        int l = digitalRead(_gdo0);
        if (l != last) {
            uint32_t now = micros();
            uint32_t d = now - lastEdge;
            lastEdge = now;
            last = l;
            quietStart = millis();
            if (d >= 30)
                _buf[_slot][count++] = (uint16_t)min(d, (uint32_t)65535); // gaps longs conserves (satures)
        }
        if (millis() - quietStart > 300 && count > 0) break; // trame finie
    }
    strobe(STROBE_SIDLE);
    if (firstLevel == LOW && count > 1) {
        // Jeter le premier pulse (fragment LOW) : le buffer commence par HIGH
        memmove(&_buf[_slot][0], &_buf[_slot][1], (count - 1) * sizeof(uint16_t));
        count--;
        Serial.println("[RF] Normalisation : capture commencait par LOW, decalee");
    }
    _cacheValid = false; // slot courant mis a jour -> recalcul des bits
    if (wasListening) startListening();
    return count;
}

void CC1101::replay(uint8_t repeats, bool inverted, bool singleFrame)
{
    uint16_t count = _counts[_slot];
    if (count < 8) return;

    uint16_t start = 0, end = count;
    if (singleFrame) {
        // Cherche le 1er sync (HIGH > 3000 us), la trame va jusqu'au sync suivant
        for (uint16_t i = 0; i + 1 < count; i += 2) {
            if (_buf[_slot][i] > 3000) { start = i; break; }
        }
        end = count;
        for (uint16_t i = start + 2; i + 1 < count; i += 2) {
            if (_buf[_slot][i] > 3000) { end = i; break; }
        }
    }

    bool wasListening = _listening;
    if (wasListening) stopListening();
    configTxOok();
    if (_txFreq > 0.0f) setFreq(_txFreq); // surcharge pour txScan
    pinMode(_gdo0, OUTPUT);
    strobe(STROBE_SFTX);
    strobe(STROBE_STX);

    bool level = !inverted; // premier pulse = HIGH (ou LOW si inverse)
    for (uint8_t r = 0; r < repeats; r++) {
        for (uint16_t i = start; i < end; i++) {
            digitalWrite(_gdo0, level ? HIGH : LOW);
            delayMicroseconds(_buf[_slot][i]);
            level = !level;
        }
        digitalWrite(_gdo0, LOW);
        delayMicroseconds(15000); // silence entre trames
        level = !inverted;
    }
    digitalWrite(_gdo0, LOW);
    strobe(STROBE_SIDLE);
    configRxOok();
    pinMode(_gdo0, INPUT);
    // Anti auto-ecoute : ignorer les trames qui debutent < 300 ms apres le TX
    _ignoreUntil = millis() + 300;
    clearFrame();
    if (wasListening) startListening();
}

void CC1101::txScan()
{
    // Rejoue le slot courant sur plusieurs frequences autour de 433.92.
    // L'utilisateur regarde si le ventilateur reagit a une frequence.
    bool wasListening = _listening;
    if (wasListening) stopListening();
    for (float f = 433.30f; f <= 434.50f; f += 0.10f) {
        _txFreq = f;
        Serial.printf("[TXSCAN] %lu.%02lu MHz ...\n",
                      (unsigned long)f,
                      (unsigned long)((f - (unsigned long)f) * 100.0f + 0.5f));
        replay(3);
        stopListening(); // replay() relance l'ecoute -> stop avant la suite
        delay(800);      // fenetre pour observer le ventilateur
    }
    _txFreq = 0.0f;
    setFreq(433.92f);
    Serial.println("[TXSCAN] termine");
    if (wasListening) startListening();
}

void CC1101::txTest()
{
    // Emet une porteuse continue ~2 s et verifie MARCSTATE (attendu ~0x13-0x16 = TX)
    bool wasListening = _listening;
    if (wasListening) stopListening();
    configTxOok();
    pinMode(_gdo0, OUTPUT);
    strobe(STROBE_SFTX);
    strobe(STROBE_STX);
    digitalWrite(_gdo0, HIGH);
    for (uint8_t i = 0; i < 4; i++) {
        delay(500);
        Serial.printf("[TX] MARCSTATE=0x%02X\n", spiReadStatus(STATUS_MARCSTATE));
    }
    digitalWrite(_gdo0, LOW);
    strobe(STROBE_SIDLE);
    configRxOok();
    pinMode(_gdo0, INPUT);
    _ignoreUntil = millis() + 300;
    clearFrame();
    if (wasListening) startListening();
    Serial.println("[TX] porteuse 2 s emise");
}

void CC1101::printSignal() const
{
    uint16_t count = _counts[_slot];
    Serial.print("[RF] slot=");
    Serial.print(_slot);
    Serial.print(" pulses=");
    Serial.println(count);
    uint16_t lo = 0xFFFF, hi = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (_buf[_slot][i] < lo) lo = _buf[_slot][i];
        if (_buf[_slot][i] > hi) hi = _buf[_slot][i];
    }
    if (count) {
        Serial.print("[RF] min="); Serial.print(lo);
        Serial.print("us max="); Serial.print(hi); Serial.println("us");
    }
    for (uint16_t i = 0; i < count && i < 128; i++) {
        Serial.print(_buf[_slot][i]);
        Serial.print(i % 2 ? 'L' : 'H');
        Serial.print(' ');
        if (i % 16 == 15) Serial.println();
    }
    if (count > 128) Serial.print("...");
    Serial.println();
    uint8_t bits[MAX_BITS];
    uint16_t nb = decodePacket(_buf[_slot], count, bits, MAX_BITS);
    if (nb) {
        Serial.print("[RF] bits=");
        printBits(bits, nb);
        Serial.println();
    }
}

void CC1101::dumpFrame() const
{
    // Dump brut de la derniere trame ecoutee (run-lengths, H en premier)
    Serial.print("[RX] frame pulses=");
    Serial.println(_rxLastLen);
    for (uint16_t i = 0; i < _rxLastLen; i++) {
        Serial.print(_rxLast[i]);
        Serial.print(i % 2 ? 'L' : 'H');
        Serial.print(' ');
        if (i % 16 == 15) Serial.println();
    }
    Serial.println();
}

void CC1101::dumpSlot(uint8_t s) const
{
    if (s >= MAX_SLOTS) return;
    Serial.print("[RF] DUMPBEGIN ");
    Serial.println(s);
    for (uint16_t i = 0; i < _counts[s]; i++) {
        Serial.print(_buf[s][i]);
        Serial.print(',');
        if (i % 24 == 23) Serial.println();
    }
    Serial.println();
    Serial.println("[RF] DUMPEND");
}

bool CC1101::loadSlot(uint8_t s, const char* csv)
{
    if (s >= MAX_SLOTS || !csv) return false;
    uint16_t n = 0;
    while (*csv && n < MAX_PULSES) {
        while (*csv == ' ' || *csv == ',') csv++;
        if (!*csv) break;
        uint32_t v = 0;
        bool digit = false;
        while (*csv >= '0' && *csv <= '9') {
            v = v * 10 + (*csv - '0');
            csv++;
            digit = true;
        }
        if (!digit) break;
        _buf[s][n++] = (uint16_t)min(v, (uint32_t)65535);
        while (*csv == ' ' || *csv == ',') csv++;
    }
    _counts[s] = n;
    _cacheValid = false;
    return n > 0;
}
