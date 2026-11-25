// SPDX-License-Identifier: MIT
// Copyright (c) 2026 DocBigs-Lab
// Gemeinsame Typen fuer Epd74 und Epd565.
#pragma once

#include <stdint.h>

enum : uint16_t {
    EPD_WHITE = 0,
    EPD_BLACK = 1,
    EPD_RED   = 2,
};

// Pinbelegung. Standard: Waveshare e-Paper ESP32 Driver Board (SKU 15823).
struct EpdPins {
    int8_t sck  = 13;
    int8_t din  = 14;
    int8_t cs   = 15;
    int8_t dc   = 27;
    int8_t rst  = 26;
    int8_t busy = 25;
    // Interner Pull-up an BUSY. Noetig, wenn der Pull-up auf dem Board fehlt,
    // z. B. Seengreat ohne R2 (Schlafstrom, siehe doc/boards.md).
    bool busyPullup = false;
};

// Wird aufgerufen, solange das Panel BUSY meldet (statt delay(10)). Die Funktion
// darf kurz warten oder den ESP in den Light Sleep schicken, z. B. bis BUSY auf
// HIGH geht, hoechstens aber etwa 1 s. Danach prueft der Treiber BUSY und seinen
// Timeout selbst. ctx ist der Zeiger aus setBusyCallback().
using EpdBusyCallback = void (*)(void *ctx);

// Seengreat ESP32-C6 E-Paper Driver Board (S1 = ON, S2 = ON).
inline EpdPins epdPinsSeengreatC6() {
    EpdPins p;
    p.sck = 5; p.din = 4; p.cs = 7; p.dc = 3; p.rst = 6; p.busy = 2;
    return p;
}
