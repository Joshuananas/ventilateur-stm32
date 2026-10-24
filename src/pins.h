#pragma once
#include <Arduino.h>

// ---- LED embarquee (active LOW) ----
#define LED_PIN     PC13

// ---- BME280 (I2C1) ----
#define I2C_SDA     PB7
#define I2C_SCL     PB6
#define BME280_ADDR 0x76 // ou 0x77 selon le module
// VIN -> 3V3, GND -> G

// ---- CC1101 (SPI1, 433 MHz) ----
#define SPI_SCK     PA5
#define SPI_MISO    PA6
#define SPI_MOSI    PA7
#define CC1101_CSN  PA4
#define CC1101_GDO0 PA0 // signal demodule (async serial) -> interruption
// VCC -> 3V3, GND -> G
