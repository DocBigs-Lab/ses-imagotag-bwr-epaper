// SPDX-License-Identifier: MIT
// Copyright (c) 2026 DocBigs-Lab
// Treiber fuer das 4,2" BWR aus SES-imagotag-Preisschildern, Pervasive E2417JS0D1
// (UC8176-kompatibler Controller). Ablauf nach der Pervasive-Doku fuer kleine
// Panels: Soft-Reset, Temperatur, Panel-Einstellung, Bild, Refresh.
// Details: doc/panel-4.2-zoll.md
#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <SPI.h>

#include "EpdCommon.h"

class Epd42 : public Adafruit_GFX {
public:
    static constexpr int16_t  WIDTH_PX  = 400;   // Querformat, Flexkabel unten
    static constexpr int16_t  HEIGHT_PX = 300;
    static constexpr uint32_t PLANE_BYTES = (uint32_t)WIDTH_PX * HEIGHT_PX / 8;   // 15 000

    explicit Epd42(const EpdPins &pins = EpdPins());
    ~Epd42();

    // Speicher anlegen, Pins und SPI einrichten. false, wenn kein Speicher frei ist.
    bool begin();

    // Antwort auf Befehl 0x70 (24 Bit, beim UC8176 die Revision). Erwartet: 0x0007FF.
    uint32_t readChipId();

    // Temperatur fuer die Ansteuerkurve, 0 bis 50 Grad C (Standard 25).
    // Bleibt Schwarz grau, hilft ein niedrigerer Wert (laengere Kurve).
    void setTemperature(float celsius);

    // Puffer an das Panel senden und Refresh ausfuehren (ca. 23 s bei 25 Grad C,
    // ca. 55 s bei 5 Grad C). false bei BUSY-Timeout.
    bool display();

    // Dauer des letzten Refresh in ms.
    uint32_t lastRefreshMs() const { return _lastRefreshMs; }

    // Adafruit_GFX
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;

private:
    void sendCommand(uint8_t index, const uint8_t *data, uint32_t len);
    void sendCommand(uint8_t index, std::initializer_list<uint8_t> data);
    bool waitWhileBusy(uint32_t timeoutMs);
    void hardwareReset();

    EpdPins _pins;
    SPIClass *_spi = &SPI;
    SPISettings _spiSettings{4000000, MSBFIRST, SPI_MODE0};
    uint8_t *_black = nullptr;   // 1 = schwarz, Zeile fuer Zeile, 50 Byte pro Zeile
    uint8_t *_red = nullptr;     // 1 = rot
    uint8_t _temperature = 25;   // Grad C
    uint32_t _lastRefreshMs = 0;
};
