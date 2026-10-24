// Journal de boot persistant : enregistre la cause de chaque reset dans
// le secteur 6 (BOOTLOG_* de persistence/flash_layout.h), sans effacer.
#include "storage.h"
#include "persistence/flash_layout.h"
#include <stm32f4xx_hal_flash.h>
#include <stm32f4xx_hal_flash_ex.h>
#include "safe_serial.h"

// ---------- Journal de boot persistant ----------
static void eraseLogSector()
{
    FLASH_EraseInitTypeDef e = {};
    e.TypeErase = FLASH_TYPEERASE_SECTORS;
    e.Sector = FLASH_SECTOR_6;
    e.NbSectors = 1;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    uint32_t err = 0;
    HAL_FLASHEx_Erase(&e, &err);
}

static void logInit()
{
    uint32_t hdr = *(volatile const uint32_t*)BOOTLOG_BASE;
    if (hdr == BOOTLOG_HDR) return;                 // deja initialise
    if (hdr == 0xFFFFFFFFUL) {                      // secteur vierge
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOTLOG_BASE, BOOTLOG_HDR);
        return;
    }
    // Contenu inconnu : on repart proprement
    return;
}

static uint32_t bootLogFree()
{
    for (uint32_t a = BOOTLOG_ADDR; a + BOOTREC_SIZE <= BOOTLOG_END;
         a += BOOTREC_SIZE) {
        if (*(volatile const uint32_t*)a == 0xFFFFFFFFUL) return a;
    }
    return 0; // zone pleine
}

uint32_t Storage::bootLogCount()
{
    uint32_t n = 0;
    for (uint32_t a = BOOTLOG_ADDR; a + BOOTREC_SIZE <= BOOTLOG_END;
         a += BOOTREC_SIZE) {
        if (*(volatile const uint32_t*)a != BOOTREC_MAGIC) break;
        n++;
    }
    return n;
}

void Storage::logBoot(uint32_t rccFlags, uint32_t stage)
{
    noInterrupts();
    HAL_FLASH_Unlock();
    logInit();
    uint32_t a = *(volatile const uint32_t*)BOOTLOG_BASE == BOOTLOG_HDR ? bootLogFree() : 0;
    if (a) {
        uint32_t seq = bootLogCount();
        for (uint8_t i = 0; i < 4; i++) {
            uint32_t w = (i == 0) ? BOOTREC_MAGIC : (i == 1) ? seq
                       : (i == 2) ? rccFlags : stage;
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, a + i * 4, w);
        }
    }
    HAL_FLASH_Lock();
    interrupts();
}

// Effacement EXPLICITE du journal : commande 'logclear'.
void Storage::logClear()
{
    noInterrupts();
    HAL_FLASH_Unlock();
    eraseLogSector();
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOTLOG_BASE, BOOTLOG_HDR);
    HAL_FLASH_Lock();
    interrupts();
}

// Cause dominante du reset d'apres RCC->CSR (flags STM32F4)
const char* Storage::resetCause(uint32_t rcc)
{
    if (rcc & (1UL << 29)) return "watchdog IWDG (gel > timeout)";
    if (rcc & (1UL << 30)) return "watchdog fenetre WWDG";
    if (rcc & (1UL << 28)) return "reset logiciel";
    if (rcc & (1UL << 26)) return "broche NRST";
    if (rcc & (1UL << 27)) return "mise sous tension";
    if (rcc & (1UL << 25)) return "brown-out (alim faible)";
    if (rcc & (1UL << 31)) return "basse consommation";
    return "inconnu";
}

void Storage::dumpBootLog()
{
    uint32_t n = bootLogCount();
    Serial.printf("[BOOTLOG] %lu enregistrement(s)\n", (unsigned long)n);
    if (n > 20) {
        Serial.printf("[BOOTLOG] ... %lu anciens masques\n",
                      (unsigned long)(n - 20));
    }
    for (uint32_t i = (n > 20 ? n - 20 : 0); i < n; i++) {
        volatile const uint32_t* r =
            (volatile const uint32_t*)(BOOTLOG_ADDR + i * BOOTREC_SIZE);
        Serial.printf("[BOOTLOG] #%lu rcc=0x%08lX -> %s stage=%lu\n",
                      (unsigned long)r[1], (unsigned long)r[2],
                      resetCause(r[2]), (unsigned long)r[3]);
    }
}
