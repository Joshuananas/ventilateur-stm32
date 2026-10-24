#include "resume.h"

static const uint32_t RESUME_MAGIC = 0x46525331;

void Resume::initBackupDomain()
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();
}

bool Resume::valid(uint32_t rccCsr)
{
    return !(rccCsr & (RCC_CSR_PORRSTF | RCC_CSR_BORRSTF))
        && RTC->BKP0R == RESUME_MAGIC && RTC->BKP4R == ~RTC->BKP2R;
}

uint32_t Resume::previousStage()
{
    return RTC->BKP0R == RESUME_MAGIC ? RTC->BKP1R : 0xFFFFFFFFUL;
}

Resume::Snapshot Resume::snapshot()
{
    uint32_t c = RTC->BKP2R;
    Snapshot s;
    s.manual = c & 1;
    s.known = c & 2;
    s.powerOn = c & 4;
    s.speed = (c >> 3) & 7;
    s.reverse = c & 64;
    s.light = (c >> 7) & 3;
    s.manualRemainingMs = RTC->BKP3R;
    return s;
}

void Resume::checkpoint(uint32_t stage)
{
    RTC->BKP1R = stage;
}

void Resume::retain(const Snapshot& s)
{
    uint32_t state = (s.manual ? 1UL : 0UL)
        | (uint32_t(s.known) << 1) | (uint32_t(s.powerOn) << 2)
        | (uint32_t(s.speed) << 3) | (uint32_t(s.reverse) << 6)
        | (uint32_t(s.light) << 7);
    RTC->BKP0R = 0;
    RTC->BKP2R = state;
    RTC->BKP3R = s.manualRemainingMs;
    RTC->BKP4R = ~state;
    RTC->BKP0R = RESUME_MAGIC;
}
