// SPDX-License-Identifier: MIT
// Copyright (c) 2026 DocBigs-Lab
// Ablauf nach der Pervasive-Doku fuer kleine Panels mit internem Timing-Controller.
#include "Epd42.h"

// Natives Format: 300 Zeilen zu je 400 Pixeln (50 Byte), MSB = erstes Pixel.
// Querformat (Flexkabel unten): (0,0) oben links, keine Umrechnung noetig.
static constexpr uint32_t LINE_BYTES = 50;

Epd42::Epd42(const EpdPins &pins) : Adafruit_GFX(WIDTH_PX, HEIGHT_PX), _pins(pins) {}

Epd42::~Epd42() {
    free(_black);
    free(_red);
}

bool Epd42::begin() {
    if (!_black) _black = (uint8_t *)malloc(PLANE_BYTES);
    if (!_red) _red = (uint8_t *)malloc(PLANE_BYTES);
    if (!_black || !_red) return false;
    fillScreen(EPD_WHITE);

    pinMode(_pins.busy, _pins.busyPullup ? INPUT_PULLUP : INPUT);
    pinMode(_pins.rst, OUTPUT);
    digitalWrite(_pins.rst, HIGH);
    pinMode(_pins.dc, OUTPUT);
    digitalWrite(_pins.dc, HIGH);
    pinMode(_pins.cs, OUTPUT);
    digitalWrite(_pins.cs, HIGH);
    _spi->begin(_pins.sck, -1, _pins.din, -1);
    return true;
}

void Epd42::setTemperature(float celsius) {
    // Befehl E5 erwartet die Temperatur direkt in Grad C
    if (celsius < 0) celsius = 0;
    if (celsius > 50) celsius = 50;
    _temperature = (uint8_t)(celsius + 0.5f);
}

// ---------- Zeichnen --------------------------------------------------------

void Epd42::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    int16_t t;
    switch (getRotation()) {
        case 1: t = x; x = WIDTH_PX - 1 - y; y = t; break;
        case 2: x = WIDTH_PX - 1 - x; y = HEIGHT_PX - 1 - y; break;
        case 3: t = x; x = y; y = HEIGHT_PX - 1 - t; break;
        default: break;
    }
    uint32_t i = (uint32_t)y * LINE_BYTES + (x >> 3);
    uint8_t m = 0x80 >> (x & 7);
    switch (color) {
        case EPD_BLACK: _black[i] |= m;  _red[i] &= ~m; break;
        case EPD_RED:   _black[i] &= ~m; _red[i] |= m;  break;
        default:        _black[i] &= ~m; _red[i] &= ~m; break;
    }
}

void Epd42::fillScreen(uint16_t color) {
    memset(_black, color == EPD_BLACK ? 0xFF : 0x00, PLANE_BYTES);
    memset(_red, color == EPD_RED ? 0xFF : 0x00, PLANE_BYTES);
}

// ---------- SPI ---------------------------------------------------------------

void Epd42::sendCommand(uint8_t index, const uint8_t *data, uint32_t len) {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_pins.dc, LOW);
    digitalWrite(_pins.cs, LOW);
    _spi->transfer(index);
    digitalWrite(_pins.cs, HIGH);
    digitalWrite(_pins.dc, HIGH);
    if (len) {
        digitalWrite(_pins.cs, LOW);
        _spi->writeBytes(data, len);
        digitalWrite(_pins.cs, HIGH);
    }
    _spi->endTransaction();
}

void Epd42::sendCommand(uint8_t index, std::initializer_list<uint8_t> data) {
    sendCommand(index, data.begin(), data.size());
}

bool Epd42::waitWhileBusy(uint32_t timeoutMs) {
    uint32_t t0 = millis();
    while (digitalRead(_pins.busy) == LOW) {   // LOW = beschaeftigt
        if (millis() - t0 > timeoutMs) return false;
        if (_busyCb) _busyCb(_busyCtx);
        else delay(10);
    }
    return true;
}

void Epd42::hardwareReset() {
    digitalWrite(_pins.rst, HIGH); delay(5);
    digitalWrite(_pins.rst, LOW);  delay(10);
    digitalWrite(_pins.rst, HIGH); delay(10);
}

// Lesen ueber die bidirektionale Datenleitung geht nur per Bit-Bang.
uint32_t Epd42::readChipId() {
    _spi->end();
    pinMode(_pins.sck, OUTPUT);
    pinMode(_pins.din, OUTPUT);
    digitalWrite(_pins.sck, LOW);
    hardwareReset();

    digitalWrite(_pins.cs, LOW);
    digitalWrite(_pins.dc, LOW);
    for (int b = 7; b >= 0; b--) {
        digitalWrite(_pins.sck, LOW);
        digitalWrite(_pins.din, (0x70 >> b) & 1);
        delayMicroseconds(1);
        digitalWrite(_pins.sck, HIGH);
        delayMicroseconds(1);
    }
    digitalWrite(_pins.sck, LOW);
    digitalWrite(_pins.dc, HIGH);
    pinMode(_pins.din, INPUT_PULLUP);
    delayMicroseconds(2);
    uint32_t id = 0;
    for (int i = 0; i < 24; i++) {
        digitalWrite(_pins.sck, LOW);  delayMicroseconds(1);
        digitalWrite(_pins.sck, HIGH); delayMicroseconds(1);
        id = (id << 1) | (uint32_t)digitalRead(_pins.din);
    }
    digitalWrite(_pins.sck, LOW);
    digitalWrite(_pins.cs, HIGH);
    pinMode(_pins.din, OUTPUT);

    _spi->begin(_pins.sck, -1, _pins.din, -1);
    return id;
}

// ---------- Bildaufbau ----------------------------------------------------------

bool Epd42::display() {
    hardwareReset();
    bool ok = waitWhileBusy(2000);
    sendCommand(0x00, {0x0E});                     // Soft-Reset
    ok = waitWhileBusy(2000) && ok;
    sendCommand(0xE5, {_temperature});             // Temperatur in Grad C
    sendCommand(0xE0, {0x02});                     // Temperatur aktivieren
    sendCommand(0x00, {0xCF, 0x8D});               // Panel-Einstellung
    sendCommand(0x10, _black, PLANE_BYTES);        // Ebene schwarz
    sendCommand(0x13, _red, PLANE_BYTES);          // Ebene rot

    sendCommand(0x04, {0x00});                     // Power ON
    ok = waitWhileBusy(5000) && ok;
    sendCommand(0x12, nullptr, 0);                 // Refresh
    delay(5);
    uint32_t t0 = millis();
    ok = waitWhileBusy(120000) && ok;              // bei 5 Grad C ca. 55 s
    _lastRefreshMs = millis() - t0;

    sendCommand(0x02, {0x00});                     // Power OFF
    waitWhileBusy(5000);
    return ok;
}
