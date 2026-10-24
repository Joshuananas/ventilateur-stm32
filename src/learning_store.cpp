// Persistance du FanLearner : (de)serialisation du modele appris.
// StoredLearn et les identifiants sont dans persistence/storage_format.h.
#include "learning.h"
#include "persistence/storage_format.h"
#include <string.h>

static void wrF(uint8_t*& p, float v)   { memcpy(p, &v, 4); p += 4; }
static void wrU8(uint8_t*& p, uint8_t v){ *p++ = v; }
static void wrU32(uint8_t*& p, uint32_t v){ memcpy(p, &v, 4); p += 4; }
static float rdF(const uint8_t*& p)     { float v; memcpy(&v, p, 4); p += 4; return v; }
static uint8_t rdU8(const uint8_t*& p)  { return *p++; }
static uint32_t rdU32(const uint8_t*& p){ uint32_t v; memcpy(&v, p, 4); p += 4; return v; }

static uint32_t learnCrc(const uint8_t* p, uint16_t n)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint16_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ CRC32_POLYNOMIAL : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFUL;
}

// Taille : magic+ver (8) + 3 floats (12) + 4 octets + obs (30*24)
//          + know (8*16) + crc (4)
uint16_t FanLearner::serializedSize() const
{
    return sizeof(StoredLearn);
}

uint16_t FanLearner::serialize(uint8_t* out) const
{
    uint8_t* p = out;
    wrU32(p, LEARN_MAGIC);
    wrU32(p, LEARN_VER);
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < 3; i++) wrF(p, _th[s][i]);
    wrU8(p, _count); wrU8(p, _head); wrU8(p, _knowCount); wrU8(p, _banned);
    for (uint8_t i = 0; i < CAPACITY; i++) {
        const Observation& o = _buf[i];
        wrU32(p, o.timestamp);
        wrU8(p, o.action.speed); wrU8(p, (uint8_t)o.action.dir);
        wrU8(p, 0); wrU8(p, 0);
        wrF(p, o.tempBefore); wrF(p, o.tempAfter);
        wrF(p, o.humBefore);  wrF(p, o.delta); wrF(p, o.expected);
    }
    for (uint8_t i = 0; i < KNOW_CAP; i++) {
        const Knowledge& k = _know[i];
        wrU8(p, k.speed); wrU8(p, k.dir); wrU8(p, k.n); wrU8(p, 0);
        wrF(p, k.tempC); wrF(p, k.humC); wrF(p, k.delta); wrF(p, k.conf);
    }
    uint16_t used = (uint16_t)(p - out);
    wrU32(p, learnCrc(out, used));
    uint8_t* preferenceStart = p;
    wrU32(p, LEARN_PREFERENCE_MAGIC);
    for (const StoredPreference& pref : _preferences) {
        wrF(p, pref.temp); wrF(p, pref.hum);
        wrU8(p, pref.speed); wrU8(p, pref.dir);
        wrU8(p, pref.votes); wrU8(p, pref.winter);
    }
    wrU32(p, learnCrc(preferenceStart, p - preferenceStart));
    return p - out;
}

bool FanLearner::deserialize(const uint8_t* in, uint16_t len)
{
    const uint16_t baseSize = offsetof(StoredLearn, preferenceMagic);
    if (len < baseSize) return false;
    const uint8_t* p = in;
    if (rdU32(p) != LEARN_MAGIC || rdU32(p) != LEARN_VER) return false;
    uint16_t crcOff = baseSize - 4;
    uint32_t expect; memcpy(&expect, in + crcOff, 4);
    if (learnCrc(in, crcOff) != expect) return false;

    for (int s = 0; s < 2; s++)
        for (int i = 0; i < 3; i++) _th[s][i] = rdF(p);
    _count = rdU8(p); _head = rdU8(p); _knowCount = rdU8(p); _banned = rdU8(p);
    if (_count > CAPACITY || _head >= CAPACITY || _knowCount > KNOW_CAP)
        return false;
    for (uint8_t i = 0; i < CAPACITY; i++) {
        Observation& o = _buf[i];
        o.timestamp = rdU32(p);
        o.action.speed = rdU8(p); o.action.dir = (FanDirection)rdU8(p);
        rdU8(p); rdU8(p);
        o.tempBefore = rdF(p); o.tempAfter = rdF(p);
        o.humBefore  = rdF(p); o.delta     = rdF(p); o.expected = rdF(p);
    }
    for (uint8_t i = 0; i < KNOW_CAP; i++) {
        Knowledge& k = _know[i];
        k.speed = rdU8(p); k.dir = rdU8(p); k.n = rdU8(p); rdU8(p);
        k.tempC = rdF(p); k.humC = rdF(p); k.delta = rdF(p); k.conf = rdF(p);
    }
    memset(_preferences, 0, sizeof(_preferences));
    if (len >= serializedSize()) {
        const uint8_t* start = in + baseSize;
        p = start;
        uint32_t magic = rdU32(p);
        uint32_t storedCrc;
        memcpy(&storedCrc, in + serializedSize() - 4, 4);
        if (magic == LEARN_PREFERENCE_MAGIC && learnCrc(start, serializedSize() - baseSize - 4) == storedCrc) {
            for (StoredPreference& pref : _preferences) {
                pref.temp = rdF(p); pref.hum = rdF(p);
                pref.speed = rdU8(p); pref.dir = rdU8(p);
                pref.votes = rdU8(p); pref.winter = rdU8(p);
                if (pref.speed > 6 || pref.dir > 1 || pref.winter > 1
                    || !isfinite(pref.temp) || !isfinite(pref.hum)) pref.votes = 0;
            }
        }
    }
    _pending = false;
    _hasDecision = false;
    return true;
}
