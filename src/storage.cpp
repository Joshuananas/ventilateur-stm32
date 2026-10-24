#include "storage.h"
#include "persistence/flash_layout.h"
#include <string.h>
#include <stm32f4xx_hal_flash.h>
#include <stm32f4xx_hal_flash_ex.h>
#include "safe_serial.h"

// Taille des donnees avant le CRC : magic+version(8) + fan(8) + 16 slots*1026
static const uint32_t HEADER_BYTES = 16;
static const uint32_t SLOT_BYTES   = sizeof(StoredSlot);          // 1026
static const uint32_t DATA_BYTES   = HEADER_BYTES + CC1101::MAX_SLOTS * SLOT_BYTES; // 16432
static const uint32_t TOTAL_WORDS  = (DATA_BYTES + 4) / 4;        // 4109

static_assert(sizeof(StoredLearn) % 4 == 0, "StoredLearn doit etre multiple de 4");

static uint32_t crc32Update(uint32_t crc, uint8_t b)
{
    crc ^= b;
    for (uint8_t i = 0; i < 8; i++)
        crc = (crc & 1) ? (crc >> 1) ^ CRC32_POLYNOMIAL : (crc >> 1);
    return crc;
}

// Octet i du blob tel qu'il doit etre ecrit (genere a la volee, sans buffer)
static uint8_t blobByte(const CC1101& radio, const FanState& fan, uint32_t i)
{
    if (i < 4)  return (uint8_t)(STORAGE_MAGIC >> (8 * i));
    if (i < 8)  return (uint8_t)(STORAGE_VER >> (8 * (i - 4)));
    if (i < 16) {
        switch (i - 8) {
            case 0: return fan.known;
            case 1: return fan.powerOn;
            case 2: return fan.speed;
            case 3: return fan.reverse;
            case 4: return (uint8_t)fan.light;
            default: return 0;
        }
    }
    i -= HEADER_BYTES;
    uint32_t s = i / SLOT_BYTES;
    uint32_t o = i % SLOT_BYTES;
    if (s >= CC1101::MAX_SLOTS) return 0;
    uint16_t n = radio.slotPulses((uint8_t)s);
    const uint16_t* p = radio.slotData((uint8_t)s);
    if (o == 0) return (uint8_t)(n & 0xFF);
    if (o == 1) return (uint8_t)(n >> 8);
    o -= 2;
    uint16_t v = (o / 2 < CC1101::MAX_PULSES) ? p[o / 2] : 0;
    return (o & 1) ? (uint8_t)(v >> 8) : (uint8_t)(v & 0xFF);
}

static uint32_t blobWord(const CC1101& radio, const FanState& fan, uint32_t w)
{
    uint32_t v = 0;
    for (uint8_t j = 0; j < 4; j++)
        v |= (uint32_t)blobByte(radio, fan, w * 4 + j) << (8 * j);
    return v;
}

// ---------- Redondance A/B ----------
// Chaque copie = en-tete {COPY_MAGIC, seq} + StorageBlob inchange.
// Le format historique (blob seul a STORAGE_ADDR, sans en-tete) est
// reconnu comme une copie de seq=0 : migration automatique a la
// prochaine sauvegarde, sans effacer la copie valide.
static bool blobCheck(const uint8_t* p)
{
    const StorageBlob* b = (const StorageBlob*)p;
    if (b->magic != STORAGE_MAGIC || b->version != STORAGE_VER) return false;
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < DATA_BYTES; i++) crc = crc32Update(crc, p[i]);
    return (crc ^ 0xFFFFFFFFUL) == b->crc32;
}

// Renvoie le pointeur du blob de la copie a 'base', ou nullptr, et son seq.
static const uint8_t* copyBlob(uint32_t base, uint32_t* seq)
{
    const uint32_t* h = (const uint32_t*)base;
    if (h[0] == COPY_MAGIC && blobCheck((const uint8_t*)(base + 8))) {
        if (seq) *seq = h[1];
        return (const uint8_t*)(base + 8);
    }
    if (base == STORAGE_ADDR && blobCheck((const uint8_t*)base)) {
        if (seq) *seq = 0;
        return (const uint8_t*)base;
    }
    return nullptr;
}

// Copie la plus recente valide (comparaison signee : wraparound 32 bits).
static const uint8_t* bestBlob(uint32_t* seq)
{
    uint32_t sa = 0, sb = 0;
    const uint8_t* a = copyBlob(STORAGE_ADDR, &sa);
    const uint8_t* b = copyBlob(STORAGE_B_ADDR, &sb);
    if (!a && !b) return nullptr;
    bool useA = !b || (a && (int32_t)(sa - sb) >= 0);
    if (seq) *seq = useA ? sa : sb;
    return useA ? a : b;
}

bool Storage::valid()
{
    return bestBlob(nullptr) != nullptr;
}

bool Storage::load(CC1101& radio, FanState& fan, FanLearner& learner)
{
    const uint8_t* blob = bestBlob(nullptr);
    if (!blob) return false;
    const StorageBlob* b = (const StorageBlob*)blob;
    fan.known   = b->fan.known != 0;
    fan.powerOn = b->fan.powerOn != 0;
    fan.speed   = b->fan.speed;
    fan.reverse = b->fan.reverse != 0;
    fan.light   = (LightMode)b->fan.light;
    for (uint8_t s = 0; s < CC1101::MAX_SLOTS; s++) {
        // pointeur flash vers les pulses (offset pair -> aligne pour uint16)
        const uint16_t* p = (const uint16_t*)(blob
            + offsetof(StorageBlob, slots) + s * sizeof(StoredSlot)
            + offsetof(StoredSlot, pulses));
        radio.setSlot(s, p, b->slots[s].count);
    }
    // Bloc apprentissage juste apres le blob (optionnel)
    if (learner.deserialize(blob + TOTAL_WORDS * 4, sizeof(StoredLearn)))
        Serial.printf("[FLASH] apprentissage restaure : %u obs, %u figees\n",
                      learner.count(), learner.knowCount());
    return true;
}

static bool eraseSector(uint8_t sector)
{
    FLASH_EraseInitTypeDef e = {};
    e.TypeErase = FLASH_TYPEERASE_SECTORS;
    e.Sector = sector;
    e.NbSectors = 1;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    uint32_t err = 0;
    return HAL_FLASHEx_Erase(&e, &err) == HAL_OK;
}

void Storage::erase()
{
    noInterrupts();
    HAL_FLASH_Unlock();
    eraseSector(FLASH_SECTOR_7);
    eraseSector(FLASH_SECTOR_5);
    HAL_FLASH_Lock();
    interrupts();
}

bool Storage::save(const CC1101& radio, const FanState& fan, const FanLearner& learner)
{
    // Serialise le learner dans un buffer (taille fixe ~900 o)
    static uint8_t learnBuf[sizeof(StoredLearn)];
    memset(learnBuf, 0, sizeof(learnBuf));
    static_assert(offsetof(StoredLearn, preferenceMagic) == 8 + 24 + 4 + FanLearner::CAPACITY * 28 + FanLearner::KNOW_CAP * 20 + 4, "Learning layout mismatch");
    if (learner.serializedSize() != sizeof(learnBuf)) return false;
    if (learner.serialize(learnBuf) != sizeof(learnBuf)) return false;
    // 1) CRC32 sur les octets a programmer (meme source que l'ecriture)
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < DATA_BYTES; i++)
        crc = crc32Update(crc, blobByte(radio, fan, i));
    crc ^= 0xFFFFFFFFUL;

    // 2) Cible : l'autre secteur que la meilleure copie (elle reste
    //    intacte pendant l'effacement et la programmation).
    uint32_t oldSeq = 0;
    const uint8_t* best = bestBlob(&oldSeq);
    uint32_t target = (best == (const uint8_t*)STORAGE_ADDR) ? STORAGE_B_ADDR : STORAGE_ADDR;
    uint8_t sector = (target == STORAGE_ADDR) ? FLASH_SECTOR_7 : FLASH_SECTOR_5;
    uint32_t newSeq = oldSeq + 1;

    // 3) Efface puis programme mot par mot (interruptions coupees)
    noInterrupts();
    HAL_FLASH_Unlock();
    if (!eraseSector(sector)) {
        HAL_FLASH_Lock();
        interrupts();
        return false;
    }
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, target, COPY_MAGIC) != HAL_OK
        || HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, target + 4, newSeq) != HAL_OK) {
        HAL_FLASH_Lock();
        interrupts();
        return false;
    }
    for (uint32_t w = 0; w < TOTAL_WORDS; w++) {
        uint32_t v = (w == TOTAL_WORDS - 1) ? crc : blobWord(radio, fan, w);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, target + 8 + w * 4, v) != HAL_OK) {
            HAL_FLASH_Lock();
            interrupts();
            return false;
        }
    }
    // Bloc apprentissage juste apres le blob (sizeof(StoredLearn) multiple de 4)
    for (uint32_t w = 0; w < sizeof(StoredLearn) / 4; w++) {
        uint32_t v; memcpy(&v, learnBuf + w * 4, 4);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              target + 8 + TOTAL_WORDS * 4 + w * 4, v) != HAL_OK) {
            HAL_FLASH_Lock();
            interrupts();
            return false;
        }
    }
    HAL_FLASH_Lock();
    interrupts();

    // 4) Verification par relecture de la copie fraiche
    uint32_t writtenSeq = 0;
    const uint8_t* written = copyBlob(target, &writtenSeq);
    return written && writtenSeq == newSeq
        && memcmp(written + TOTAL_WORDS * 4, learnBuf, sizeof(learnBuf)) == 0;
}

// ---------- Journal de boot persistant : voir bootlog.cpp ----------
