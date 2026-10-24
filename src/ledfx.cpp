#include "ledfx.h"
#include "pins.h"

static uint32_t ledAlertUntil = 0;
static uint32_t ledAlertStart = 0;
static uint32_t ledThinkUntil = 0;
static uint32_t ledThinkStart = 0;

// Pulsation exponentielle : montee en attackMs puis retombee en tauMs.
// Retourne 0 (LED eteinte) quand la luminosite devient imperceptible.
static int expPulse(uint32_t t, uint32_t attackMs, uint32_t tauMs, float amp)
{
    float x = (t < attackMs)
        ? (float)t / (float)attackMs
        : expf(-(float)(t - attackMs) / (float)tauMs);
    int d = (int)(x * amp * 255.0f);
    return d < 15 ? 0 : (d > 255 ? 255 : d); // extinction franche <6%
}

// Respiration de sommeil : ~7 s. Inspiration 2.6 s en courbe douce,
// expiration en exponentielle, puis apnee noire ~2 s.
static int breathPulse(uint32_t t)
{
    if (t < 2600) {
        float s = t / 2600.0f;
        float x = s * s * (3.0f - 2.0f * s); // smoothstep ease-in-out
        return (int)(x * 200.0f);            // pic ~78% : reste discret
    }
    if (t < 5400) {
        int d = (int)(expf(-(float)(t - 2600) / 900.0f) * 200.0f);
        return d < 15 ? 0 : d;
    }
    return 0; // apnee : noir complet
}

void ledFxInit()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // LED eteinte (active LOW)
}

void ledFxTick(bool fault, bool manual)
{
    uint32_t t = millis();
    int duty;
    if (t < ledAlertUntil) {
        duty = expPulse(t - ledAlertStart, 20, 200, 1.0f);   // tir laser
    } else if (t < ledThinkUntil) {
        duty = expPulse(t - ledThinkStart, 40, 350, 1.0f);   // "compris"
    } else if (fault) {
        duty = (t % 250 < 60) ? 255 : 0;                     // faute : 4 Hz
    } else if (manual) {
        uint32_t ph = t % 1400;                              // "lub-dub"
        duty = ph < 260 ? expPulse(ph, 60, 130, 1.0f)
                        : expPulse(ph - 260, 50, 110, 0.7f);
    } else {
        duty = breathPulse(t % 7000);                        // sommeil
    }
    bool on = (micros() % 2000) < (uint32_t)(duty * 2000 / 255);
    digitalWrite(LED_PIN, on ? LOW : HIGH);
}

void ledFxRfPulse(uint32_t durationMs)
{
    ledAlertStart = millis();
    ledAlertUntil = ledAlertStart + durationMs;
}

void ledFxThinkPulse()
{
    ledThinkStart = millis();
    ledThinkUntil = ledThinkStart + 1600;
}
