// SPDX-License-Identifier: MIT
// Copyright (c) 2026 DocBigs-Lab
// Ablauf nach der Pervasive-Displays-Doku "Medium BW/BWR, Update procedure",
// Abweichungen der Treiber-Generation 06 abgeglichen mit PervasiveDisplays/Pervasive_Wide_Medium.
// Treiber fuer das 5,81" BWR aus SES-imagotag VUSION 5.9 BWR GU110 (EDG3-0590-A),
// Pervasive E2581JS061, 720 x 256. Alle Panelparameter kommen aus dem OTP des Panels.
// Details: doc/panel-5.81-zoll.md
#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <SPI.h>

#include "EpdCommon.h"

class Epd581 : public Adafruit_GFX {
public:
    static constexpr int16_t  WIDTH_PX  = 720;   // Querformat, Flexkabel rechts
    static constexpr int16_t  HEIGHT_PX = 256;
    static constexpr uint32_t PLANE_BYTES = (uint32_t)WIDTH_PX * HEIGHT_PX / 8;   // 23 040

    explicit Epd581(const EpdPins &pins = EpdPins());
    ~Epd581();

    // Speicher anlegen, Pins einrichten, OTP des Panels lesen, SPI starten.
    // false, wenn kein Speicher frei ist oder das OTP nicht passt.
    bool begin();

    // Chip-ID per Befehl 0x70 (24 Bit). Gelesen: 0x168705 (erstes Byte = Generation 06).
    uint32_t readChipId();

    // Temperatur fuer die Ansteuerung, -15 bis 60 Grad C (Standard 25).
    void setTemperature(float celsius);

    // Puffer an das Panel senden und Refresh ausfuehren (dauert ca. 52 s bei 25 Grad C).
    // false bei BUSY-Timeout oder ohne gueltiges OTP.
    bool display();

    // Dauer des letzten Refresh in ms.
    uint32_t lastRefreshMs() const { return _lastRefreshMs; }

    // Statt delay(10) beim Warten auf BUSY aufrufen, z. B. fuer Light Sleep
    // waehrend des Refresh. nullptr = wieder delay(10).
    void setBusyCallback(EpdBusyCallback cb, void *ctx = nullptr) {
        _busyCb = cb;
        _busyCtx = ctx;
    }

    // 128 Byte OTP des Panels (gueltig nach begin()).
    const uint8_t *otp() const { return _otp; }
    bool otpValid() const { return _otpValid; }

    // Adafruit_GFX
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;

private:
    void sendCommand(uint8_t index, const uint8_t *data, uint32_t len);
    void sendCommand(uint8_t index, std::initializer_list<uint8_t> data);
    bool waitWhileBusy(uint32_t timeoutMs);
    void hardwareReset();
    void bbWrite(uint8_t v);
    uint8_t bbRead();
    bool readOtp();
    void powerOnAndSoftStart();
    void powerOff();

    EpdPins _pins;
    SPIClass *_spi = &SPI;
    SPISettings _spiSettings{4000000, MSBFIRST, SPI_MODE0};
    uint8_t *_black = nullptr;   // 1 = schwarz, natives Format: 720 Zeilen x 32 Byte
    uint8_t *_red = nullptr;     // 1 = rot
    uint8_t _otp[128] = {};
    bool _otpValid = false;
    uint8_t _temperature = 0x41; // 25 Grad C
    uint32_t _lastRefreshMs = 0;
    EpdBusyCallback _busyCb = nullptr;
    void *_busyCtx = nullptr;
};
