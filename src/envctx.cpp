#include "envctx.h"

EnvContext env;

uint8_t EnvContext::noteSample(float temp, float hum, uint32_t nowMs)
{
    uint8_t ev = EV_NONE;
    // moyenne lente ~6 h : saison estimee sans horloge
    seasonEma = isnan(seasonEma) ? temp
                : seasonEma + (temp - seasonEma) * 0.00023f;
    // pente sur ~10 min : signature apport solaire (rapide + jour)
    if (isnan(solarRefT) || nowMs - solarRefTs >= 10UL*60UL*1000UL) {
        if (!isnan(solarRefT)) {
            solarSlope = temp - solarRefT;
            humidSlope = isnan(hum) || isnan(solarRefH)
                         ? 0 : hum - solarRefH;
            bool wasCooking = cookingNow;
            cookingNow = solarSlope > 0.30f && humidSlope > 0.5f;
            if (cookingNow && !wasCooking) ev |= EV_COOKING;
            bool wasAiring = airingNow;
            airingNow = solarSlope < -0.5f;
            if (airingNow && !wasAiring) ev |= EV_AIRING;
        }
        solarRefT = temp;
        solarRefH = hum;
        solarRefTs = nowMs;
    }
    return ev;
}
