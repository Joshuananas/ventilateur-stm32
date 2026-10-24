#pragma once
#include <cstddef>
#include <cstdint>
#include "../domain/limits.h"
#include "../domain/learning_types.h"

// ---------- Format persistant (flash) ----------
// Les timestamps sont en millis() : informatifs seulement, jamais
// reinterpretes apres un reboot. _pending n'est pas persisté.
#define LEARN_MAGIC 0x4E524C31UL  // 'LRN1'
#define LEARN_VER   5UL
#define LEARN_PREFERENCE_MAGIC 0x50524631UL
#define STORAGE_MAGIC  0x46414E31UL  // 'FAN1'
#define STORAGE_VER    1UL
// En-tete de copie A/B (8 o devant le blob) : magic + numero de sequence.
// Le blob lui-meme garde exactement le meme format (compatible v1).
#define COPY_MAGIC     0x43505931UL  // 'CPY1'
#define CRC32_POLYNOMIAL 0xEDB88320UL

struct __attribute__((packed)) StoredObs {
    uint32_t timestamp;
    uint8_t speed, dir, pad[2];
    float tempBefore, tempAfter, humBefore, delta, expected;
};                                          // 28 o

struct __attribute__((packed)) StoredKnow {
    uint8_t speed, dir, n, pad;
    float tempC, humC, delta, conf;
};                                          // 20 o

struct __attribute__((packed)) StoredLearn {
    uint32_t magic;                         // LEARN_MAGIC
    uint32_t version;                       // LEARN_VER
    float thresholds[2][3];
    uint8_t count, head, knowCount, pad;
    StoredObs  obs[LearningLimits::CAPACITY];   // 720 o
    StoredKnow know[LearningLimits::KNOW_CAP];  // 128 o
    uint32_t crc32;                         // sur tout ce qui precede
    uint32_t preferenceMagic;
    StoredPreference preferences[16];
    uint32_t preferenceCrc;
};

struct __attribute__((packed)) StoredSlot {
    uint16_t count;
    uint16_t pulses[RadioLimits::MAX_PULSES];
};

struct __attribute__((packed)) StoredFanState {
    uint8_t known, powerOn, speed, reverse, light, pad[3];
};

struct __attribute__((packed)) StorageBlob {
    uint32_t magic;        // 0x46414E31 'FAN1'
    uint32_t version;      // 1
    StoredFanState fan;
    StoredSlot slots[RadioLimits::MAX_SLOTS];
    uint32_t crc32;        // CRC32 (poly 0xEDB88320) de tout ce qui precede
    // Bloc apprentissage : ajoute APRES le CRC pour rester compatible
    // avec les blobs deja en flash (zone absente -> simplement ignoree).
    StoredLearn learn;
};

static_assert(sizeof(float) == 4);
static_assert(sizeof(StoredPreference) == 12);
static_assert(sizeof(StoredObs) == 28);
static_assert(offsetof(StoredObs, expected) == 24);
static_assert(sizeof(StoredKnow) == 20);
static_assert(sizeof(StoredLearn) == 2352);
static_assert(offsetof(StoredLearn, thresholds) == 8);
static_assert(offsetof(StoredLearn, obs) == 36);
static_assert(offsetof(StoredLearn, know) == 1828);
static_assert(offsetof(StoredLearn, crc32) == 2148);
static_assert(offsetof(StoredLearn, preferenceMagic) == 2152);
static_assert(offsetof(StoredLearn, preferences) == 2156);
static_assert(offsetof(StoredLearn, preferenceCrc) == 2348);
static_assert(sizeof(StoredSlot) == 1026);
static_assert(sizeof(StoredFanState) == 8);
static_assert(offsetof(StorageBlob, slots) == 16);
static_assert(offsetof(StorageBlob, crc32) == 16432);
static_assert(offsetof(StorageBlob, learn) == 16436);
static_assert(sizeof(StorageBlob) == 18788);
