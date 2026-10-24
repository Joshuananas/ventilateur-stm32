#pragma once
#include <Arduino.h>

class SafeSerial : public Print {
public:
    using Print::write;
    void begin(unsigned long baud) { Serial.begin(baud); }
    int available() { return Serial.available(); }
    int read() { return Serial.read(); }
    size_t write(uint8_t ch) override { return write(&ch, 1); }
    size_t write(const uint8_t* data, size_t size) override {
        if (!Serial) return size;
        uint32_t start = micros();
        size_t sent = 0;
        do {
            int free = Serial.availableForWrite();
            size_t count = free > 0 ? min(size - sent, size_t(free)) : 0;
            if (count) sent += Serial.write(data + sent, count);
        } while (sent < size && Serial && uint32_t(micros() - start) < 2000);
        return size;
    }
};

static SafeSerial safeSerial;
#undef Serial
#define Serial safeSerial
