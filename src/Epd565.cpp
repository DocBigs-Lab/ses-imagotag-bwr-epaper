// SPDX-License-Identifier: MIT
// Copyright (c) 2026 DocBigs-Lab
// Ablauf nach der Pervasive-Displays-Doku "Medium BW/BWR, Update procedure"
// und PervasiveDisplays/EPD_Driver_GU_large (IST7136, BSD-Lizenz, Hinweis in LICENSE).
#include "Epd565.h"

// Natives Format: 600 Zeilen zu je 448 Pixeln (56 Byte), MSB = erstes Pixel.
// Querformat (Flexkabel unten): Zeile = x, Pixel in der Zeile = 447 - y.
static constexpr uint32_t LINE_BYTES = 56;

// OTP-Adressen laut Pervasive-Doku "Medium BW/BWR"
enum : uint8_t {
    OTP_TCON = 0x0B, OTP_DRFW = 0x0C, OTP_DCTL = 0x10, OTP_VCOM = 0x11,
    OTP_RAM_RW = 0x12, OTP_DUW = 0x15, OTP_STV_DIR = 0x1B,
    OTP_MS_SYNC = 0x1C, OTP_BVSS = 0x1D, OTP_SOFTSTART = 0x28,
};

Epd565::Epd565(const EpdPins &pins) : Adafruit_GFX(WIDTH_PX, HEIGHT_PX), _pins(pins) {}

Epd565::~Epd565() {
    free(_black);
    free(_red);
}

bool Epd565::begin() {
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

    // OTP per Bit-Bang lesen, bevor SPI die Pins uebernimmt
    _otpValid = readOtp();
    _spi->begin(_pins.sck, -1, _pins.din, -1);
    return _otpValid;
}

void Epd565::setTemperature(float celsius) {
    // Skala laut Pervasive (DRIVER_B): T * 2 + 0x50, also 25 C = 0x82
    float v = 0x50 + celsius * 2.0f;
    if (v < 0) v = 0;
    if (v > 0xFE) v = 0xFE;
    _temperature = (uint8_t)(v + 0.5f);
}

// ---------- Zeichnen --------------------------------------------------------

void Epd565::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    int16_t t;
    switch (getRotation()) {
        case 1: t = x; x = WIDTH_PX - 1 - y; y = t; break;
        case 2: x = WIDTH_PX - 1 - x; y = HEIGHT_PX - 1 - y; break;
        case 3: t = x; x = y; y = HEIGHT_PX - 1 - t; break;
        default: break;
    }
    int16_t p = HEIGHT_PX - 1 - y;
    uint32_t i = (uint32_t)x * LINE_BYTES + (p >> 3);
    uint8_t m = 0x80 >> (p & 7);
    switch (color) {
        case EPD_BLACK: _black[i] |= m;  _red[i] &= ~m; break;
        case EPD_RED:   _black[i] &= ~m; _red[i] |= m;  break;
        default:        _black[i] &= ~m; _red[i] &= ~m; break;
    }
}

void Epd565::fillScreen(uint16_t color) {
    memset(_black, color == EPD_BLACK ? 0xFF : 0x00, PLANE_BYTES);
    memset(_red, color == EPD_RED ? 0xFF : 0x00, PLANE_BYTES);
}

// ---------- SPI ---------------------------------------------------------------

void Epd565::sendCommand(uint8_t index, const uint8_t *data, uint32_t len) {
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

void Epd565::sendCommand(uint8_t index, std::initializer_list<uint8_t> data) {
    sendCommand(index, data.begin(), data.size());
}

// Fertig erst, wenn BUSY EPD_BUSY_STABLE_MS lang ununterbrochen HIGH ist. Kurze
// HIGH-Pulse waehrend des Refresh beenden das Warten nicht (5,81" Generation 06).
bool Epd565::waitWhileBusy(uint32_t timeoutMs) {
    const uint32_t t0 = millis();
    uint32_t highSeit = 0;
    bool high = false;
    for (;;) {
        if (digitalRead(_pins.busy) == HIGH) {   // HIGH = frei, LOW = beschaeftigt
            if (!high) {
                high = true;
                highSeit = millis();
            }
            if (millis() - highSeit >= EPD_BUSY_STABLE_MS) return true;
            delay(1);
            continue;
        }
        high = false;
        if (millis() - t0 > timeoutMs) return false;
        if (_busyCb) _busyCb(_busyCtx);
        else delay(10);
    }
}

void Epd565::hardwareReset() {
    delay(200);
    digitalWrite(_pins.rst, HIGH); delay(20);
    digitalWrite(_pins.rst, LOW);  delay(200);
    digitalWrite(_pins.rst, HIGH); delay(50);
    digitalWrite(_pins.cs, HIGH);  delay(5);
}

// ---------- Lesen per Bit-Bang (bidirektionale Datenleitung) ------------------

void Epd565::bbWrite(uint8_t v) {
    for (int b = 7; b >= 0; b--) {
        digitalWrite(_pins.sck, LOW);
        digitalWrite(_pins.din, (v >> b) & 1);
        delayMicroseconds(1);
        digitalWrite(_pins.sck, HIGH);
        delayMicroseconds(1);
    }
    digitalWrite(_pins.sck, LOW);
}

uint8_t Epd565::bbRead() {
    uint8_t v = 0;
    for (int b = 0; b < 8; b++) {
        digitalWrite(_pins.sck, LOW);  delayMicroseconds(1);
        digitalWrite(_pins.sck, HIGH); delayMicroseconds(1);
        v = (uint8_t)((v << 1) | digitalRead(_pins.din));
    }
    digitalWrite(_pins.sck, LOW);
    return v;
}

// B9 liefert ein Leerbyte, danach 128 Byte OTP ab Adresse 0.
bool Epd565::readOtp() {
    pinMode(_pins.sck, OUTPUT);
    pinMode(_pins.din, OUTPUT);
    digitalWrite(_pins.sck, LOW);
    hardwareReset();

    digitalWrite(_pins.dc, LOW);
    digitalWrite(_pins.cs, LOW);
    bbWrite(0xB9);
    delay(5);
    digitalWrite(_pins.dc, HIGH);
    pinMode(_pins.din, INPUT_PULLUP);
    delayMicroseconds(2);
    (void)bbRead();
    for (uint8_t &b : _otp) b = bbRead();
    digitalWrite(_pins.cs, HIGH);
    pinMode(_pins.din, OUTPUT);

    // Plausibel, wenn DUW 56 Byte x 600 Zeilen beschreibt
    const uint8_t *d = &_otp[OTP_DUW];
    uint16_t lines = (uint16_t)((d[4] | (d[5] << 8)) - (d[2] | (d[3] << 8)) + 1);
    uint16_t bytes = (uint16_t)(d[1] - d[0] + 1);
    return lines == WIDTH_PX && bytes == LINE_BYTES;
}

uint32_t Epd565::readChipId() {
    _spi->end();
    pinMode(_pins.sck, OUTPUT);
    pinMode(_pins.din, OUTPUT);
    digitalWrite(_pins.sck, LOW);
    hardwareReset();

    digitalWrite(_pins.cs, LOW);
    digitalWrite(_pins.dc, LOW);
    bbWrite(0x70);
    digitalWrite(_pins.dc, HIGH);
    pinMode(_pins.din, INPUT_PULLUP);
    delayMicroseconds(2);
    uint32_t id = 0;
    for (int i = 0; i < 3; i++) id = (id << 8) | bbRead();
    digitalWrite(_pins.cs, HIGH);
    pinMode(_pins.din, OUTPUT);

    _spi->begin(_pins.sck, -1, _pins.din, -1);
    return id;
}

// ---------- Bildaufbau ----------------------------------------------------------

void Epd565::powerOnAndSoftStart() {
    sendCommand(0x05, {0x7d}); delay(200);
    sendCommand(0x05, {0x00}); delay(10);
    sendCommand(0xd8, &_otp[OTP_MS_SYNC], 1);
    sendCommand(0xd6, &_otp[OTP_BVSS], 1);
    sendCommand(0xa7, {0x10}); delay(100);
    sendCommand(0xa7, {0x00}); delay(100);
    sendCommand(0x44, {0x00});
    sendCommand(0x45, {0x80});
    sendCommand(0xa7, {0x10}); delay(100);
    sendCommand(0xa7, {0x00}); delay(100);
    sendCommand(0x44, {0x06});
    sendCommand(0x45, {_temperature});
    sendCommand(0xa7, {0x10}); delay(100);
    sendCommand(0xa7, {0x00}); delay(100);
    sendCommand(0x60, &_otp[OTP_TCON], 1);
    sendCommand(0x61, &_otp[OTP_STV_DIR], 1);
    sendCommand(0x02, &_otp[OTP_VCOM], 1);

    // Hochspannung in 4 Stufen hochfahren, Tabelle aus dem OTP (je 8 Byte)
    for (int s = 0; s < 4; s++) {
        const uint8_t *t = &_otp[OTP_SOFTSTART + s * 8];
        int repeat = t[0] & 0x7F;
        if (t[0] & 0x80) {   // Format 1: PHL/PHH schrittweise aendern
            uint8_t ph[2] = {t[1], t[2]};
            uint32_t dly = t[7] & 0x7F;
            for (int r = 0; r < repeat; r++) {
                sendCommand(0x09, &t[5], 1);
                ph[0] = (uint8_t)(ph[0] + t[3]);
                ph[1] = (uint8_t)(ph[1] + t[4]);
                sendCommand(0x51, ph, 2);
                sendCommand(0x09, &t[6], 1);
                if (t[7] & 0x80) delay(dly); else delayMicroseconds(10 * dly);
            }
        } else {             // Format 2: nur Schalter mit zwei Wartezeiten
            uint32_t da = t[3] & 0x7F, db = t[4] & 0x7F;
            for (int r = 0; r < repeat; r++) {
                sendCommand(0x09, &t[1], 1);
                if (t[3] & 0x80) delay(da); else delayMicroseconds(10 * da);
                sendCommand(0x09, &t[2], 1);
                if (t[4] & 0x80) delay(db); else delayMicroseconds(10 * db);
            }
        }
    }
}

void Epd565::powerOff() {
    waitWhileBusy(5000);
    sendCommand(0x09, {0x7f});
    sendCommand(0x05, {0x3d});
    sendCommand(0x09, {0x7e});
    delay(15);
    sendCommand(0x09, {0x00});
    digitalWrite(_pins.dc, LOW);
    digitalWrite(_pins.cs, LOW);
    digitalWrite(_pins.rst, LOW);
    delay(10);
    digitalWrite(_pins.cs, HIGH);
    digitalWrite(_pins.dc, HIGH);
    digitalWrite(_pins.rst, HIGH);
}

bool Epd565::display() {
    if (!_otpValid) return false;
    hardwareReset();
    sendCommand(0x01, &_otp[OTP_DCTL], 1);         // DCTL
    sendCommand(0x13, &_otp[OTP_DUW], 6);          // DUW
    sendCommand(0x90, &_otp[OTP_DRFW], 4);         // DRFW
    sendCommand(0x12, &_otp[OTP_RAM_RW], 3);       // RAM_RW
    sendCommand(0x10, _black, PLANE_BYTES);        // Ebene schwarz
    sendCommand(0x12, &_otp[OTP_RAM_RW], 3);       // RAM_RW
    sendCommand(0x11, _red, PLANE_BYTES);          // Ebene rot

    powerOnAndSoftStart();

    bool ok = waitWhileBusy(5000);
    sendCommand(0x15, {0x3c});                     // Refresh
    delay(5);
    uint32_t t0 = millis();
    ok = waitWhileBusy(90000) && ok;
    _lastRefreshMs = millis() - t0;

    powerOff();
    return ok;
}
