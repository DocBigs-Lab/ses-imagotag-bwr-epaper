/* =========================================================================
 * Diagnosefirmware fuer E-Paper-Panels aus SES-imagotag-Preisschildern
 * (Pervasive Displays mit internem Timing-Controller).
 *
 *   7,4"  E2741ES083  IST7x32  Chip-ID 71 32 5A  -> Tasten v, V, W, X
 *   5,65" E2565JS0B1  IST7136  Chip-ID 71 36 5A  -> Tasten n, N, R, K, C
 *   5,81" E2581JS061  Gen. 06  Chip-ID 16 87 05  -> nur i, o (Bild: examples/Demo581)
 *   4,2"  E2417JS0D1  UC8176   0x70 -> 00 07 FF  -> Taste g
 *
 * Boards: Waveshare e-Paper ESP32 Driver Board (Umgebung esp32dev) und
 * Seengreat ESP32-C6 E-Paper Driver Board (Umgebung esp32c6).
 *
 * Eigenstaendig, nutzt die Bibliothek nicht. Bedienung ueber den seriellen
 * Monitor (115200 Baud), h zeigt das Menue. Unbekanntes Panel: zuerst nur
 * i, o und b senden, die lesen nur. z und q senden auch unbekannte Befehle.
 * Anleitung: doc/diagnose.md
 * ========================================================================= */
#include <Arduino.h>

#if defined(BOARD_SEENGREAT_C6)
// Seengreat ESP32-C6 E-Paper Driver Board, S1 = ON (4-Draht), S2 = ON (0,47 Ohm)
#define PIN_SCK   5
#define PIN_DIN   4
#define PIN_CS    7
#define PIN_DC    3
#define PIN_RST   6
#define PIN_BUSY  2
#else
// Waveshare e-Paper ESP32 Driver Board, Schalter 1 = B, 2 = ON
#define PIN_SCK   13
#define PIN_DIN   14
#define PIN_CS    15
#define PIN_DC    27
#define PIN_RST   26
#define PIN_BUSY  25
#endif

static int g_w = 800, g_h = 480;
static bool g_fixedWait = false;   // 'f': BUSY ignorieren, feste Wartezeiten

// ---------- Bit-Bang SPI ----------------------------------------------------
static void bbByte(uint8_t v) {
    for (int i = 7; i >= 0; i--) {
        digitalWrite(PIN_SCK, LOW);
        digitalWrite(PIN_DIN, (v >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(PIN_SCK, HIGH);
        delayMicroseconds(1);
    }
    digitalWrite(PIN_SCK, LOW);
}
static uint8_t bbRead(void) {
    uint8_t v = 0;
    pinMode(PIN_DIN, INPUT_PULLUP);
    delayMicroseconds(2);
    for (int i = 7; i >= 0; i--) {
        digitalWrite(PIN_SCK, LOW);  delayMicroseconds(1);
        digitalWrite(PIN_SCK, HIGH); delayMicroseconds(1);
        v = (uint8_t)((v << 1) | digitalRead(PIN_DIN));
    }
    digitalWrite(PIN_SCK, LOW);
    pinMode(PIN_DIN, OUTPUT);
    return v;
}

// Pervasive-Stil: Index mit DC=Low, Daten mit DC=High, CS dazwischen hoch
static void sendIndex(uint8_t idx) {
    digitalWrite(PIN_DC, LOW);
    digitalWrite(PIN_CS, LOW);
    bbByte(idx);
    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, HIGH);
}
static void sendIndexData(uint8_t idx, const uint8_t *d, int n) {
    sendIndex(idx);
    digitalWrite(PIN_CS, LOW);
    for (int i = 0; i < n; i++) bbByte(d[i]);
    digitalWrite(PIN_CS, HIGH);
}
static void sendIndex1(uint8_t idx, uint8_t v) { sendIndexData(idx, &v, 1); }

// BUSY low-aktiv: warten bis HIGH. Gibt Dauer aus, false bei Timeout.
static bool waitBusy(const char *what, uint32_t timeoutMs) {
    uint32_t t0 = millis();
    delay(1);
    while (digitalRead(PIN_BUSY) == LOW) {
        if (millis() - t0 > timeoutMs) {
            Serial.printf("  %s: TIMEOUT nach %lu ms (BUSY bleibt LOW)\n", what, (unsigned long)timeoutMs);
            return false;
        }
        delay(5);
    }
    Serial.printf("  %s: %lu ms\n", what, (unsigned long)(millis() - t0));
    return true;
}

static void hwReset(void) {
    digitalWrite(PIN_RST, HIGH); delay(5);
    digitalWrite(PIN_RST, LOW);  delay(10);
    digitalWrite(PIN_RST, HIGH); delay(10);
}

static void readId(void) {
    hwReset();
    digitalWrite(PIN_CS, LOW);
    digitalWrite(PIN_DC, LOW); bbByte(0x70);
    digitalWrite(PIN_DC, HIGH);
    uint8_t a = bbRead(), b = bbRead(), c = bbRead();
    digitalWrite(PIN_CS, HIGH);
    const char *panel = (a == 0x71 && b == 0x32 && c == 0x5A) ? "7,4\" E2741ES083 (IST7x32)"
                      : (a == 0x71 && b == 0x36 && c == 0x5A) ? "5,65\" E2565JS0B1 (IST7136)"
                      : (a == 0x16) ? "5,81\" E2581JS061 (Generation 06), Test mit examples/Demo581"
                      : (a == 0x00 && b == 0x07) ? "4,2\" E2417JS0D1 (UC8176, Revision), Test mit g"
                      : "unbekannt";
    Serial.printf("ID 0x70: %02X %02X %02X -> %s\n", a, b, c, panel);
}

// ---------- Testbild ----------------------------------------------------------
// pattern: 0 = Balken, 1 = alles 0 (beide Ebenen leer), 2 = alles rot, 3 = alles schwarz
static bool plane1Bit(int x, int y, int pattern) {
    if (pattern == 1 || pattern == 2) return false;
    if (pattern == 3) return true;
    if (x < 64 && y < 64) return false;
    int bar = x * 4 / g_w;
    return bar == 0 || bar == 3;
}
static bool plane2Bit(int x, int y, int pattern) {
    if (pattern == 1 || pattern == 3) return false;
    if (pattern == 2) return true;
    if (x < 64 && y < 64) return true;
    int bar = x * 4 / g_w;
    return bar == 2 || bar == 3;
}

static bool g_native = false;   // Pervasive mid: 800 Zeilen x 480 Pixel (60 Byte)

static void sendPlane(uint8_t idx, int plane, int pattern) {
    sendIndex(idx);
    digitalWrite(PIN_CS, LOW);
    if (g_native) {
        // native Zeile L = x im Querformat (0..g_w-1), Pixel p in der Zeile = y (0..g_h-1)
        for (int L = 0; L < g_w; L++) {
            for (int pb = 0; pb < g_h / 8; pb++) {
                uint8_t v = 0;
                for (int k = 0; k < 8; k++) {
                    int y = pb * 8 + k;
                    int x = g_w - 1 - L;   // Panel-Zeile 0 liegt rechts (Flexkabel unten)
                    bool bit = plane == 1 ? plane1Bit(x, y, pattern) : plane2Bit(x, y, pattern);
                    if (bit) v |= (uint8_t)(0x80 >> k);
                }
                bbByte(v);
            }
        }
        digitalWrite(PIN_CS, HIGH);
        return;
    }
    for (int y = 0; y < g_h; y++) {
        for (int xb = 0; xb < g_w / 8; xb++) {
            uint8_t v = 0;
            for (int k = 0; k < 8; k++) {
                int x = xb * 8 + k;
                bool bit = plane == 1 ? plane1Bit(x, y, pattern) : plane2Bit(x, y, pattern);
                if (bit) v |= (uint8_t)(0x80 >> k);
            }
            bbByte(v);
        }
    }
    digitalWrite(PIN_CS, HIGH);
}

// psrMode: 0 = kein PSR senden, 1 = CF 8D, 2 = CF 89
static uint8_t g_smallTemp = 25;   // Grad C fuer runTest (E5), umschaltbar mit 'Y'

static void runTest(int psrMode, int pattern) {
    Serial.printf("\n--- Test: %dx%d, PSR-Modus %d, Muster %d ---\n", g_w, g_h, psrMode, pattern);
    hwReset();
    if (!waitBusy("nach HW-Reset", 2000)) return;
    sendIndex1(0x00, 0x0E);                       // Soft-Reset
    if (!waitBusy("Soft-Reset", 2000)) return;
    sendIndex1(0xE5, g_smallTemp);                // Temperatur (Standard 25 C)
    Serial.printf("  Temperatur: %u C\n", g_smallTemp);
    sendIndex1(0xE0, 0x02);                       // Temperatur aktivieren
    if (psrMode == 1) { const uint8_t p[2] = {0xCF, 0x8D}; sendIndexData(0x00, p, 2); }
    if (psrMode == 2) { const uint8_t p[2] = {0xCF, 0x89}; sendIndexData(0x00, p, 2); }

    uint32_t t0 = millis();
    sendPlane(0x10, 1, pattern);
    sendPlane(0x13, 2, pattern);
    Serial.printf("  Daten gesendet: %lu ms\n", (unsigned long)(millis() - t0));

    sendIndex1(0x04, 0x00);                       // DC/DC Power ON
    if (!waitBusy("Power ON", 5000)) { sendIndex1(0x02, 0x00); return; }
    if (g_fixedWait) { Serial.println("  feste Wartezeit Power ON: 300 ms"); delay(300); }
    sendIndex(0x12);                              // Refresh
    bool ok = waitBusy("Refresh", 60000);
    if (g_fixedWait) {
        Serial.println("  feste Wartezeit Refresh: 30 s - Display beobachten!");
        for (int i = 0; i < 30; i++) {            // BUSY trotzdem mitloggen
            Serial.print(digitalRead(PIN_BUSY)); delay(1000);
        }
        Serial.println();
    }
    sendIndex1(0x02, 0x00);                       // DC/DC Power OFF
    waitBusy("Power OFF", 5000);
    Serial.println(ok ? "Fertig. Was zeigen die 4 Balken (links->rechts) und das Quadrat oben links?"
                      : "Refresh nicht sauber beendet - Power OFF trotzdem gesendet.");
}

// BUSY-Diagnose: Pull-up vs. Pull-down (floatet der Pin?) und Verlauf nach Soft-Reset
static void busyDiag(void) {
    Serial.println("\n--- BUSY-Diagnose ---");
    hwReset();
    delay(200);   // Reset-Phase abwarten, sonst faellt die Pruefung in den BUSY-Wechsel
    pinMode(PIN_BUSY, INPUT_PULLUP);   delay(5); int up = digitalRead(PIN_BUSY);
    pinMode(PIN_BUSY, INPUT_PULLDOWN); delay(5); int dn = digitalRead(PIN_BUSY);
    Serial.printf("  Ruhe: Pull-up=%d Pull-down=%d -> %s\n", up, dn,
                  up == dn ? "getrieben" : "OFFEN (Pin nicht verbunden?)");
    // Verlauf waehrend HW-Reset + Soft-Reset mit Pull-down abtasten
    digitalWrite(PIN_RST, LOW);  delay(10);
    digitalWrite(PIN_RST, HIGH);
    String s;
    for (int i = 0; i < 50; i++) { s += digitalRead(PIN_BUSY) ? '1' : '0'; delayMicroseconds(200); }
    sendIndex1(0x00, 0x0E);
    s += '|';
    for (int i = 0; i < 200; i++) { s += digitalRead(PIN_BUSY) ? '1' : '0'; delayMicroseconds(500); }
    Serial.printf("  Verlauf (Pull-down, Reset 10ms | Soft-Reset 100ms):\n  %s\n", s.c_str());
    pinMode(PIN_BUSY, INPUT);
}

// Liest n Bytes nach cmd mit gegebenem Pull; BUSY-Minimum waehrend 50 ms danach
static void rd(uint8_t cmd, uint8_t pull, uint8_t *buf, int n, int *busyLow) {
    hwReset();
    digitalWrite(PIN_CS, LOW);
    digitalWrite(PIN_DC, LOW); bbByte(cmd);
    digitalWrite(PIN_DC, HIGH);
    for (int i = 0; i < n; i++) {
        pinMode(PIN_DIN, pull); delayMicroseconds(2);
        uint8_t v = 0;
        for (int b = 7; b >= 0; b--) {
            digitalWrite(PIN_SCK, LOW);  delayMicroseconds(1);
            digitalWrite(PIN_SCK, HIGH); delayMicroseconds(1);
            v = (uint8_t)((v << 1) | digitalRead(PIN_DIN));
        }
        digitalWrite(PIN_SCK, LOW);
        buf[i] = v;
    }
    pinMode(PIN_DIN, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
    int low = 0;
    for (int i = 0; i < 500; i++) { if (!digitalRead(PIN_BUSY)) low++; delayMicroseconds(100); }
    if (busyLow) *busyLow = low;
}

// Nur Lese-/Abfragebefehle. Ausgelassen: 0x04/0x05 (Power ON), 0x12 (Refresh),
// 0x80 (VCOM-Messung), 0x07 (Deep Sleep), OTP-Schreibbefehle.
static void scanReads(void) {
    const uint8_t cmds[] = {0x40, 0x43, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
                            0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x81, 0x82,
                            0x92, 0xB9, 0x1B, 0x2D, 0x2E, 0x2F, 0xF0, 0xF1};
    Serial.println("\n--- Lese-Scan (nur getriebene Antworten, 8 Byte) ---");
    uint8_t up[8], dn[8];
    for (uint8_t c : cmds) {
        int bl1, bl2;
        rd(c, INPUT_PULLUP, up, 8, &bl1);
        rd(c, INPUT_PULLDOWN, dn, 8, &bl2);
        int drv = 0;
        for (int i = 0; i < 8; i++) if (up[i] == dn[i]) drv++;
        if (drv == 0 && bl1 == 0) continue;
        Serial.printf("  %02X: ", c);
        for (int i = 0; i < 8; i++) {
            if (up[i] == dn[i]) Serial.printf("%02X ", up[i]); else Serial.print("-- ");
        }
        Serial.printf(" BUSY-low %d/500\n", bl1);
    }
    Serial.println("--- Scan fertig ---");
}

// ---------- 3-Draht-SPI (9 Bit: D/C-Bit + 8 Datenbits, DC-Pin ungenutzt) ----
static void bb9(bool isData, uint8_t v) {
    digitalWrite(PIN_SCK, LOW);
    digitalWrite(PIN_DIN, isData ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(PIN_SCK, HIGH); delayMicroseconds(1);
    bbByte(v);
}
static void cmd9(uint8_t c, const uint8_t *d, int n) {
    digitalWrite(PIN_CS, LOW);
    bb9(false, c);
    for (int i = 0; i < n; i++) bb9(true, d[i]);
    digitalWrite(PIN_CS, HIGH);
    delayMicroseconds(10);
}
static void read9(uint8_t c, uint8_t pull, uint8_t *buf, int n) {
    digitalWrite(PIN_CS, LOW);
    bb9(false, c);
    for (int i = 0; i < n; i++) {
        pinMode(PIN_DIN, pull); delayMicroseconds(2);
        uint8_t v = 0;
        for (int b = 7; b >= 0; b--) {
            digitalWrite(PIN_SCK, LOW);  delayMicroseconds(1);
            digitalWrite(PIN_SCK, HIGH); delayMicroseconds(1);
            v = (uint8_t)((v << 1) | digitalRead(PIN_DIN));
        }
        digitalWrite(PIN_SCK, LOW);
        buf[i] = v;
    }
    pinMode(PIN_DIN, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
}

static void wire3Test(void) {
    Serial.println("\n--- 3-Draht-SPI-Test (9 Bit) ---");
    digitalWrite(PIN_DC, HIGH);
    const uint8_t rc[] = {0x70, 0x71, 0x40, 0x2F};
    uint8_t up[6], dn[6];
    for (uint8_t c : rc) {
        hwReset(); read9(c, INPUT_PULLUP, up, 6);
        hwReset(); read9(c, INPUT_PULLDOWN, dn, 6);
        Serial.printf("  %02X: ", c);
        for (int i = 0; i < 6; i++) {
            if (up[i] == dn[i]) Serial.printf("%02X ", up[i]); else Serial.print("-- ");
        }
        Serial.println();
    }
    // Soft-Reset + Power ON im 9-Bit-Format, BUSY beobachten
    hwReset();
    String s;
    const uint8_t sr = 0x0E;
    cmd9(0x00, &sr, 1);
    for (int i = 0; i < 50; i++) { s += digitalRead(PIN_BUSY) ? '1' : '0'; delay(1); }
    s += '|';
    cmd9(0x04, nullptr, 0);
    uint32_t t0 = millis(), firstLow = 0, lastLow = 0;
    while (millis() - t0 < 1000) {
        bool b = digitalRead(PIN_BUSY);
        if (!b) { if (!firstLow) firstLow = millis() - t0 + 1; lastLow = millis() - t0; }
        if ((millis() - t0) % 10 == 0) { s += b ? '1' : '0'; delay(1); }
    }
    cmd9(0x02, nullptr, 0);
    delay(100);
    Serial.printf("  BUSY (Soft-Reset 50ms | PON alle 10ms, 1s):\n  %s\n", s.c_str());
    Serial.printf("  BUSY low ab %lu bis %lu ms nach PON\n", (unsigned long)firstLow, (unsigned long)lastLow);
    hwReset(); read9(0x71, INPUT_PULLUP, up, 1);
    Serial.printf("  0x71 (9 Bit) danach: %02X\n", up[0]);
}

// Gegentest: 4,2" SES-imagotag BWR, UC8176, 400x300 (Sequenz wie GxEPD2_420c)
static void uc8176Test(void) {
    int w = g_w, h = g_h;
    g_w = 400; g_h = 300;
    Serial.println("\n--- UC8176-Test 4,2\" 400x300 (GxEPD2_420c) ---");
    hwReset();
    if (!waitBusy("nach HW-Reset", 2000)) { g_w = w; g_h = h; return; }
    const uint8_t bst[3] = {0x17, 0x17, 0x17};
    sendIndexData(0x06, bst, 3);                  // Booster Soft Start
    sendIndex(0x04);                              // Power ON
    bool pon = waitBusy("Power ON", 5000);
    sendIndex1(0x00, 0x0F);                       // PSR: LUT aus OTP, KWR
    uint32_t t0 = millis();
    sendPlane(0x10, 1, 0);
    sendPlane(0x13, 2, 0);
    Serial.printf("  Daten gesendet: %lu ms\n", (unsigned long)(millis() - t0));
    sendIndex(0x12);                              // Refresh
    bool ok = waitBusy("Refresh", 60000);
    sendIndex(0x02);                              // Power OFF
    waitBusy("Power OFF", 5000);
    Serial.println(pon && ok ? "Fertig - was zeigt das Panel?" : "Ablauf nicht sauber (siehe Zeiten).");
    g_w = w; g_h = h;
}

// Ein Schritt: Kommando (+Daten) senden, BUSY-LOW-Dauer messen
static void step(uint8_t c, const uint8_t *d, int n, uint32_t timeoutMs) {
    if (n > 0) sendIndexData(c, d, n); else sendIndex(c);
    uint32_t t0 = millis(), lowStart = 0, lowEnd = 0;
    while (millis() - t0 < 30) { if (!digitalRead(PIN_BUSY)) { lowStart = millis(); break; } }
    if (lowStart) {
        while (!digitalRead(PIN_BUSY) && millis() - lowStart < timeoutMs) delay(1);
        lowEnd = millis();
    }
    Serial.printf("    %02X", c);
    for (int i = 0; i < n; i++) Serial.printf(" %02X", d[i]);
    if (!lowStart) Serial.println("  -> BUSY bleibt HIGH");
    else if (!digitalRead(PIN_BUSY)) Serial.printf("  -> BUSY LOW > %lu ms (Timeout)\n", (unsigned long)timeoutMs);
    else Serial.printf("  -> BUSY LOW %lu ms\n", (unsigned long)(lowEnd - lowStart));
}

static void seqTests(void) {
    const uint8_t t19 = 0x19, e02 = 0x02, x0e = 0x0E, ff = 0xFF, z = 0x00;
    const uint8_t fa5 = 0xA5, fe3 = 0xE3;
    Serial.println("\n--- Sequenz-Tests ---");

    Serial.println("  A) Pervasive BWRY-Stil: E6 19, E0 02, A5, 04, 02");
    hwReset();
    step(0xE6, &t19, 1, 2000); step(0xE0, &e02, 1, 2000); step(0xA5, nullptr, 0, 3000);
    step(0x04, nullptr, 0, 3000); step(0x02, &z, 1, 3000);

    Serial.println("  B) wie A mit E5: E5 19, E0 02, A5, 04, 02");
    hwReset();
    step(0xE5, &t19, 1, 2000); step(0xE0, &e02, 1, 2000); step(0xA5, nullptr, 0, 3000);
    step(0x04, nullptr, 0, 3000); step(0x02, &z, 1, 3000);

    Serial.println("  C) A1 mit FF bzw. 00");
    hwReset(); step(0xA1, &ff, 1, 3000);
    hwReset(); step(0xA1, &z, 1, 3000);
    hwReset(); step(0xA1, nullptr, 0, 3000);

    Serial.println("  D) Freischalten FF A5, dann 04, 02, FF E3");
    hwReset();
    step(0xFF, &fa5, 1, 2000); step(0x04, nullptr, 0, 3000); step(0x02, &z, 1, 3000);
    step(0xFF, &fe3, 1, 2000);

    Serial.println("  E) Soft-Reset 00 0E, E5 19, E0 02, A1 FF, 04, 02");
    hwReset();
    step(0x00, &x0e, 1, 2000); step(0xE5, &t19, 1, 2000); step(0xE0, &e02, 1, 2000);
    step(0xA1, &ff, 1, 3000); step(0x04, nullptr, 0, 3000); step(0x02, &z, 1, 3000);

    hwReset();
    Serial.println("--- Sequenz-Tests fertig ---");
}

// ---------- Pervasive "mid-size" iTC (IST7232/IST7236), 7.41" 800x480 BWR -------
// Exakt nach PervasiveDisplays/EPD_Driver_GU_mid (EPD_Driver.cpp, globalUpdate)
static void sd(uint8_t idx, std::initializer_list<uint8_t> d) {
    uint8_t buf[8]; int n = 0;
    for (uint8_t v : d) buf[n++] = v;
    sendIndexData(idx, buf, n);
}

// Bildfenster je Panelgroesse. Zeilen = Gate-Zeilen (Querformat-Breite),
// Pixel pro Zeile = Querformat-Hoehe.
struct MidPanel {
    const char *name;
    int lines, pixels;
    uint8_t duw[6], drfw[4], ramrw[3];
};
// 7,41" E2741ES083 (IST7x32): Werte aus EPD_Driver_GU_mid
static const MidPanel PANEL_741 = {"7.41\" 800x480", 800, 480,
    {0x00, 0x3b, 0x00, 0x00, 0x1f, 0x03}, {0x00, 0x3b, 0x00, 0xc9}, {0x3b, 0x00, 0x14}};
// 5,65" E2565JS0B1 (IST7136): DUW und DRFW stehen so im OTP (B9 ab 0x0D bzw. 0x15),
// 56 Byte x 600 Zeilen. RAM_RW nach dem Muster von GU_mid (x-Ende, y-Start, 14).
static const MidPanel PANEL_565 = {"5.65\" 600x448", 600, 448,
    {0x00, 0x37, 0x00, 0x00, 0x57, 0x02}, {0x00, 0x37, 0x00, 0x96}, {0x37, 0x00, 0x14}};

static void pdiMidUpdate(uint8_t dctl, int pattern = 0, const MidPanel &p = PANEL_741) {
    Serial.printf("\n--- Pervasive mid-size %s (DCTL=0x%02X, Muster %d) ---\n", p.name, dctl, pattern);
    g_w = p.lines; g_h = p.pixels;
    // _reset(200, 20, 200, 50, 5)
    delay(200); digitalWrite(PIN_RST, HIGH); delay(20);
    digitalWrite(PIN_RST, LOW); delay(200);
    digitalWrite(PIN_RST, HIGH); delay(50);
    digitalWrite(PIN_CS, HIGH); delay(5);

    sd(0x01, {dctl});                                   // DCTL
    sendIndexData(0x13, p.duw, 6);                      // DUW
    sendIndexData(0x90, p.drfw, 4);                     // DRFW
    sendIndexData(0x12, p.ramrw, 3);                    // RAM_RW
    uint32_t t0 = millis();
    g_native = true;
    sendPlane(0x10, 1, pattern);                        // 1. Frame (schwarz)
    sendIndexData(0x12, p.ramrw, 3);                    // RAM_RW
    sendPlane(0x11, 2, pattern);                        // 2. Frame (rot)
    g_native = false;
    Serial.printf("  Daten gesendet: %lu ms\n", (unsigned long)(millis() - t0));

    // _DCDC_softStart_Mid()
    sd(0x05, {0x7d}); delay(200);
    sd(0x05, {0x00}); delay(10);
    sd(0xc2, {0x3f}); delay(1);
    sd(0xd8, {0x00});                                   // MS_SYNC
    sd(0xd6, {0x00});                                   // BVSS
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    sd(0x03, {0x00, 0x01});                             // OSC
    sd(0x44, {0x00});
    sd(0x45, {0x80});
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    sd(0x44, {0x06});
    sd(0x45, {0x82});                                   // Temperatur 25 C
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    sd(0x60, {0x25});                                   // TCON
    sd(0x61, {0x00});                                   // STV_DIR
    sd(0x01, {0x00});                                   // DCTL
    sd(0x02, {0x00});                                   // VCOM
    uint8_t i51[] = {0x50, 0x01, 0x0a, 0x01};
    sendIndexData(0x51, &i51[0], 2);
    const uint8_t i09[] = {0x1f, 0x9f, 0x7f, 0xff};
    for (int v = 1; v <= 4; v++)  { sendIndexData(0x09, &i09[0], 1); i51[1] = v; sendIndexData(0x51, &i51[0], 2); sendIndexData(0x09, &i09[1], 1); delay(2); }
    for (int v = 1; v <= 10; v++) { sendIndexData(0x09, &i09[0], 1); i51[3] = v; sendIndexData(0x51, &i51[2], 2); sendIndexData(0x09, &i09[1], 1); delay(2); }
    for (int v = 3; v <= 10; v++) { sendIndexData(0x09, &i09[2], 1); i51[3] = v; sendIndexData(0x51, &i51[2], 2); sendIndexData(0x09, &i09[3], 1); delay(2); }
    for (int v = 9; v >= 2; v--)  { sendIndexData(0x09, &i09[2], 1); i51[2] = v; sendIndexData(0x51, &i51[2], 2); sendIndexData(0x09, &i09[3], 1); delay(2); }
    sendIndexData(0x09, &i09[3], 1); delay(10);
    Serial.println("  DC/DC Soft-Start gesendet");

    // _displayRefresh()
    if (!waitBusy("vor Refresh", 5000)) Serial.println("  (weiter trotz BUSY)");
    sd(0x15, {0x3c}); delay(5);
    uint32_t tr = millis();
    while (digitalRead(PIN_BUSY) == LOW && millis() - tr < 60000) delay(10);
    Serial.printf("  Refresh: BUSY LOW %lu ms%s\n", (unsigned long)(millis() - tr),
                  digitalRead(PIN_BUSY) ? "" : " (Timeout!)");

    // _DCDC_softShutdown_Mid()
    sd(0x09, {0x7f});
    sd(0x05, {0x7d});
    sd(0x09, {0x00});
    delay(200);
    waitBusy("Shutdown", 5000);
    digitalWrite(PIN_DC, LOW);
    digitalWrite(PIN_CS, LOW);
    digitalWrite(PIN_RST, LOW);
    digitalWrite(PIN_CS, HIGH);
    delay(10);
    digitalWrite(PIN_DC, HIGH);
    digitalWrite(PIN_RST, HIGH);
    Serial.println("Fertig - was zeigt das Panel?");
}

// ---------- Pervasive mid-size mit Werten aus dem Panel-OTP (IST7136, 5,65") ------
// B9 liefert ein Leerbyte, danach OTP ab Adresse 0 (otp[a] = Antwort[a + 1]).
// Adressen laut Pervasive-Doku "Medium BW/BWR, Update procedure":
//   0x0B TCON, 0x0C-0F DRFW, 0x10 DCTL, 0x11 VCOM, 0x12-14 RAM_RW, 0x15-1A DUW,
//   0x1B STV_DIR, 0x1C MS_SYNC, 0x1D BVSS, ab 0x28 DC/DC-Soft-Start (4 x 8 Byte).
// Soft-Start-Stufe (aus dem Dump abgeleitet, passt zu GU_mid):
//   [0] 0x80|Wiederholungen, [1] PHL_INI, [2] PHH_INI, [3] PHL_VAR, [4] PHH_VAR,
//   [5] BST-SW-a, [6] BST-SW-b, [7] 0x80|Wartezeit
static uint8_t g_otp[128];
// Temperaturcode fuer 'n'/'N'/'R'/'K', umschaltbar mit 'T'.
// Medium DRIVER_B laut PDLS: T * 2 + 0x50 (0x82 = 25 C).
static uint8_t g_tempCode = 0x82;
// Oszillator (Befehl 03) fuer 'n'/'N'/'R'/'K', umschaltbar mit 'O'.
// PDLS DRIVER_B sendet ihn nicht; GU_mid sendet 00 01, GU_large (IST7136) 00 11.
static int g_osc = -1;   // -1 = nicht senden, sonst zweites Datenbyte

static bool readOtp(void) {
    static uint8_t buf[129];
    rd(0xB9, INPUT_PULLUP, buf, sizeof(buf), nullptr);
    memcpy(g_otp, buf + 1, sizeof(g_otp));
    bool ok = g_otp[0x0B] != 0xFF && g_otp[0x15] != 0xFF;
    Serial.printf("  OTP: TCON %02X  DRFW %02X %02X %02X %02X  DCTL %02X  VCOM %02X\n",
                  g_otp[0x0B], g_otp[0x0C], g_otp[0x0D], g_otp[0x0E], g_otp[0x0F],
                  g_otp[0x10], g_otp[0x11]);
    Serial.printf("       RAM_RW %02X %02X %02X  DUW %02X %02X %02X %02X %02X %02X\n",
                  g_otp[0x12], g_otp[0x13], g_otp[0x14], g_otp[0x15], g_otp[0x16],
                  g_otp[0x17], g_otp[0x18], g_otp[0x19], g_otp[0x1A]);
    Serial.printf("       STV_DIR %02X  MS_SYNC %02X  BVSS %02X\n",
                  g_otp[0x1B], g_otp[0x1C], g_otp[0x1D]);
    for (int s = 0; s < 4; s++) {
        const uint8_t *t = &g_otp[0x28 + s * 8];
        Serial.printf("       Stufe %d: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                      s + 1, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]);
    }
    if (!ok) Serial.println("  OTP leer oder nicht lesbar - Abbruch.");
    return ok;
}

// Ablauf nach PervasiveDisplays/PDLS_EXT3_Basic_Global, "Medium screens with C or J
// film", DRIVER_B (COG_MediumCJ_*). Gilt fuer xE2565JS0Bx, alle Werte aus dem OTP.
static void pdiOtpUpdate(int pattern) {
    Serial.printf("\n--- IST7136, Werte aus OTP (Muster %d, Temperaturcode %02X, OSC %s) ---\n",
                  pattern, g_tempCode, g_osc < 0 ? "aus" : g_osc == 0x01 ? "00 01" : g_osc == 0x11 ? "00 11" : "00 21");
    if (!readOtp()) return;
    const uint8_t *o = g_otp;
    // Bildgroesse aus DUW: Bytes pro Zeile und Anzahl Zeilen
    g_w = (int)((o[0x19] | (o[0x1A] << 8)) - (o[0x17] | (o[0x18] << 8)) + 1);
    g_h = (o[0x16] - o[0x15] + 1) * 8;
    Serial.printf("  Bildgroesse laut DUW: %d Zeilen x %d Pixel\n", g_w, g_h);

    // COG_MediumCJ_reset: b_reset(200, 20, 200, 50, 5)
    delay(200); digitalWrite(PIN_RST, HIGH); delay(20);
    digitalWrite(PIN_RST, LOW);  delay(200);
    digitalWrite(PIN_RST, HIGH); delay(50);
    digitalWrite(PIN_CS, HIGH);  delay(5);

    // COG_MediumCJ_initial
    sendIndexData(0x01, &o[0x10], 1);                   // DCTL

    // COG_MediumCJ_sendImageData
    sendIndexData(0x13, &o[0x15], 6);                   // DUW
    sendIndexData(0x90, &o[0x0C], 4);                   // DRFW
    uint32_t t0 = millis();
    g_native = true;
    sendIndexData(0x12, &o[0x12], 3);                   // RAM_RW
    sendPlane(0x10, 1, pattern);                        // 1. Frame (schwarz)
    sendIndexData(0x12, &o[0x12], 3);                   // RAM_RW
    sendPlane(0x11, 2, pattern);                        // 2. Frame (rot)
    g_native = false;
    Serial.printf("  Daten gesendet: %lu ms\n", (unsigned long)(millis() - t0));

    // COG_MediumCJ_update
    sd(0x05, {0x7d}); delay(200);
    sd(0x05, {0x00}); delay(10);
    sendIndexData(0xd8, &o[0x1C], 1);                   // MS_SYNC
    sendIndexData(0xd6, &o[0x1D], 1);                   // BVSS
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    if (g_osc >= 0) {                                   // OSC (optional, Test)
        const uint8_t osc[2] = {0x00, (uint8_t)g_osc};
        sendIndexData(0x03, osc, 2);
    }
    sd(0x44, {0x00});
    sd(0x45, {0x80});
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    sd(0x44, {0x06});
    sendIndexData(0x45, &g_tempCode, 1);                // Temperatur
    sd(0xa7, {0x10}); delay(100);
    sd(0xa7, {0x00}); delay(100);
    sendIndexData(0x60, &o[0x0B], 1);                   // TCON
    sendIndexData(0x61, &o[0x1B], 1);                   // STV_DIR
    sendIndexData(0x02, &o[0x11], 1);                   // VCOM (kein DCTL hier)

    // DC/DC-Soft-Start nach OTP-Tabelle
    for (int s = 0; s < 4; s++) {
        const uint8_t *t = &o[0x28 + s * 8];
        int reps = t[0] & 0x7F;
        uint8_t ph[2] = {t[1], t[2]};
        bool ms = t[7] & 0x80;                          // 1 = ms, 0 = 10 us
        int dly = t[7] & 0x7F;
        if (!(t[0] & 0x80)) { Serial.printf("  Stufe %d: Format 2 nicht umgesetzt!\n", s + 1); continue; }
        for (int r = 0; r < reps; r++) {
            sendIndexData(0x09, &t[5], 1);
            ph[0] = (uint8_t)(ph[0] + t[3]);
            ph[1] = (uint8_t)(ph[1] + t[4]);
            sendIndexData(0x51, ph, 2);
            sendIndexData(0x09, &t[6], 1);
            if (ms) delay(dly); else delayMicroseconds(10 * dly);
        }
        Serial.printf("  Stufe %d: %d x, Ende PHL %02X PHH %02X\n", s + 1, reps, ph[0], ph[1]);
    }

    if (!waitBusy("vor Refresh", 5000)) Serial.println("  (weiter trotz BUSY)");
    sd(0x15, {0x3c});
    delay(5);
    uint32_t tr = millis();
    while (digitalRead(PIN_BUSY) == LOW && millis() - tr < 90000) delay(10);
    Serial.printf("  Refresh: BUSY LOW %lu ms%s\n", (unsigned long)(millis() - tr),
                  digitalRead(PIN_BUSY) ? "" : " (Timeout!)");

    // COG_MediumCJ_powerOff (Variante xxx_JS_0B)
    waitBusy("vor Abschalten", 5000);
    sd(0x09, {0x7f});
    sd(0x05, {0x3d});
    sd(0x09, {0x7e});
    delay(15);
    sd(0x09, {0x00});
    digitalWrite(PIN_DC, LOW);
    digitalWrite(PIN_CS, LOW);
    digitalWrite(PIN_RST, LOW);
    delay(10);
    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, HIGH);
    digitalWrite(PIN_RST, HIGH);
    Serial.println("Fertig - was zeigt das Panel?");
}

// OTP/MTP-Inhalt lesen: B9 (Panel-OTP) und 72, je 256 Byte als Hex + ASCII.
// Nur lesend, aendert nichts am Panel.
static void dumpOtp(void) {
    static uint8_t buf[256];
    const uint8_t cmds[] = {0xB9, 0x72};
    for (uint8_t c : cmds) {
        rd(c, INPUT_PULLUP, buf, sizeof(buf), nullptr);
        Serial.printf("\n--- Dump %02X (256 Byte) ---\n", c);
        for (int row = 0; row < 256; row += 16) {
            Serial.printf("  %03X: ", row);
            for (int i = 0; i < 16; i++) Serial.printf("%02X ", buf[row + i]);
            Serial.print(" |");
            for (int i = 0; i < 16; i++) {
                uint8_t v = buf[row + i];
                Serial.print((v >= 0x20 && v < 0x7F) ? (char)v : '.');
            }
            Serial.println("|");
        }
    }
    Serial.println("--- Dump fertig ---");
}

static void menu(void) {
    Serial.println();
    Serial.println("--- Panel erkennen (nur lesend) ---");
    Serial.println("i = Chip-ID (0x70): 71 32 5A = 7,4\", 71 36 5A = 5,65\", 16 .. = 5,81\", 00 07 = 4,2\"");
    Serial.println("o = OTP-Dump (B9, 72)");
    Serial.println("b = BUSY-Diagnose");
    Serial.println("x = Lesescan ausgewaehlter Befehle");
    Serial.println("--- 7,4\" E2741ES083 (IST7x32) ---");
    Serial.println("v = Testbild (DCTL 08), V = dito DCTL 00");
    Serial.println("W = alles weiss (DCTL 08), X = dito DCTL 00");
    Serial.println("--- 5,65\" E2565JS0B1 (IST7136), Werte aus dem OTP ---");
    Serial.println("n = Testbild, N = alles weiss, R = alles rot, K = alles schwarz");
    Serial.println("C = Reinigen: 3 Runden schwarz, rot, weiss (ca. 5 Minuten)");
    Serial.println("T = Temperatur umschalten (25, 20, 15, 30 C)");
    Serial.println("--- 4,2\" E2417JS0D1 (UC8176-kompatibel) ---");
    Serial.println("g = Testbild wie GxEPD2_420c (PSR 0F, ohne Temperatur)");
    Serial.println("P = Testbild mit Pervasive-Ablauf (Temperatur, PSR CF 8D), B = dito alles schwarz");
    Serial.println("Y = Temperatur fuer P/B umschalten (25, 20, 15, 5, 35 C)");
    Serial.println("--- Sonstiges ---");
    Serial.println("h = dieses Menue");
    Serial.println("Fehlversuche aus der Fehlersuche: 1 2 3 c s w u p k f r q z m M O");
}

void setup(void) {
    Serial.begin(115200);
    pinMode(PIN_CS, OUTPUT);   digitalWrite(PIN_CS, HIGH);
    pinMode(PIN_DC, OUTPUT);   digitalWrite(PIN_DC, HIGH);
    pinMode(PIN_RST, OUTPUT);  digitalWrite(PIN_RST, HIGH);
    pinMode(PIN_BUSY, INPUT);
    pinMode(PIN_DIN, OUTPUT);  digitalWrite(PIN_DIN, LOW);
    pinMode(PIN_SCK, OUTPUT);  digitalWrite(PIN_SCK, LOW);
    delay(1000);
    Serial.println("\n=== Diagnose SES-imagotag E-Paper (Pervasive iTC) ===");
    readId();
    menu();
}

void loop(void) {
    if (!Serial.available()) return;
    char c = (char)Serial.read();
    switch (c) {
        case '1': runTest(1, 0); menu(); break;
        case '2': runTest(2, 0); menu(); break;
        case '3': runTest(0, 0); menu(); break;
        case 'c': runTest(1, 1); menu(); break;
        case 's': { int t = g_w; g_w = g_h; g_h = t; menu(); break; }
        case 'i': readId(); break;
        case 'h': case '?': menu(); break;
        case 'o': dumpOtp(); break;
        case 'm': pdiMidUpdate(0x08, 0, PANEL_565); menu(); break;    // 5,65" Testbild
        case 'M': pdiMidUpdate(0x08, 1, PANEL_565); menu(); break;    // 5,65" alles weiss
        case 'n': pdiOtpUpdate(0); menu(); break;                     // Testbild, Werte aus OTP
        case 'N': pdiOtpUpdate(1); menu(); break;                     // alles weiss, Werte aus OTP
        case 'R': pdiOtpUpdate(2); menu(); break;                     // alles rot, Werte aus OTP
        case 'K': pdiOtpUpdate(3); menu(); break;                     // alles schwarz, Werte aus OTP
        case 'O': {   // OSC fuer n/N/R/K: aus -> 00 01 -> 00 11 -> 00 21 -> aus
            g_osc = g_osc < 0 ? 0x01 : g_osc == 0x01 ? 0x11 : g_osc == 0x11 ? 0x21 : -1;
            if (g_osc < 0) Serial.println("OSC fuer n/N/R/K: aus (wie PDLS)");
            else Serial.printf("OSC fuer n/N/R/K: 03 <- 00 %02X\n", g_osc);
            break;
        }
        case 'C': {   // Reinigen 5,65": 3 Runden schwarz, rot, weiss (ca. 5 Minuten)
            for (int r = 1; r <= 3; r++) {
                Serial.printf("\n=== Reinigungsrunde %d von 3 ===\n", r);
                pdiOtpUpdate(3);
                pdiOtpUpdate(2);
                pdiOtpUpdate(1);
            }
            menu();
            break;
        }
        case 'T': {   // Temperatur umschalten: 25 -> 20 -> 15 -> 30 -> 25 C (Code = T * 2 + 0x50)
            g_tempCode = g_tempCode == 0x82 ? 0x78 : g_tempCode == 0x78 ? 0x6E : g_tempCode == 0x6E ? 0x8C : 0x82;
            Serial.printf("Temperatur fuer n/N/R/K: %d C (Code %02X)\n", (g_tempCode - 0x50) / 2, g_tempCode);
            break;
        }
        case 'b': busyDiag(); break;
        case 'x': scanReads(); break;
        case 'w': wire3Test(); break;
        case 'v': pdiMidUpdate(0x08); menu(); break;
        case 'V': pdiMidUpdate(0x00); menu(); break;
        case 'W': pdiMidUpdate(0x08, 1); menu(); break;   // alles weiss
        case 'X': pdiMidUpdate(0x00, 1); menu(); break;   // alles weiss, DCTL 00
        case 'q': seqTests(); break;
        case 'z': {   // Vollscan aller 256 Kommandos: Antwort + BUSY-Aktivitaet
            Serial.println("\n--- Vollscan 00..FF (getrieben / BUSY-low) ---");
            uint8_t up[4], dn[4];
            for (int c = 0; c < 256; c++) {
                int bl, bl2;
                rd((uint8_t)c, INPUT_PULLUP, up, 4, &bl);
                rd((uint8_t)c, INPUT_PULLDOWN, dn, 4, &bl2);
                bool drv = false;
                for (int i = 0; i < 4; i++) if (up[i] == dn[i]) drv = true;
                if (!drv && bl == 0 && bl2 == 0) continue;
                Serial.printf("  %02X: ", c);
                for (int i = 0; i < 4; i++) {
                    if (up[i] == dn[i]) Serial.printf("%02X ", up[i]); else Serial.print("-- ");
                }
                Serial.printf(" BUSY-low %d/%d\n", bl, bl2);
            }
            hwReset();
            Serial.println("--- Vollscan fertig ---");
            break;
        }
        case 'g': uc8176Test(); menu(); break;
        case 'Y':     // Temperatur fuer P/B: 25 -> 20 -> 15 -> 5 -> 35 -> 25 C
            g_smallTemp = g_smallTemp == 25 ? 20 : g_smallTemp == 20 ? 15 : g_smallTemp == 15 ? 5
                        : g_smallTemp == 5 ? 35 : 25;
            Serial.printf("Temperatur fuer P/B: %u C\n", g_smallTemp);
            break;
        case 'P': case 'B': {   // 4,2": Pervasive-Ablauf fuer kleine Panels (Temperatur, PSR CF 8D)
            int w = g_w, h = g_h;
            g_w = 400; g_h = 300;
            runTest(1, c == 'P' ? 0 : 3);
            g_w = w; g_h = h;
            menu();
            break;
        }
        case 'u': {   // UC8179-Stil: PWR + PON, BUSY beobachten, sofort POF
            Serial.println("\n--- UC8179-Test: PWR, PON, POF ---");
            hwReset();
            String s;
            for (int i = 0; i < 20; i++) { s += digitalRead(PIN_BUSY) ? '1' : '0'; delay(1); }
            const uint8_t pwr[4] = {0x07, 0x07, 0x3F, 0x3F};   // GxEPD2 GDEW075Z08
            sendIndexData(0x01, pwr, 4);
            sendIndex(0x04);                                   // PON
            s += '|';
            uint32_t t0 = millis(); uint32_t firstLow = 0, lastLow = 0;
            while (millis() - t0 < 1000) {
                bool b = digitalRead(PIN_BUSY);
                if (!b) { if (!firstLow) firstLow = millis() - t0 + 1; lastLow = millis() - t0; }
                if ((millis() - t0) % 10 == 0) { s += b ? '1' : '0'; delay(1); }
            }
            sendIndex(0x02);                                   // POF
            delay(100);
            Serial.printf("  BUSY (Reset 20ms | nach PON alle 10ms, 1s):\n  %s\n", s.c_str());
            Serial.printf("  BUSY low ab %lu ms bis %lu ms nach PON\n", (unsigned long)firstLow, (unsigned long)lastLow);
            // FLG nach Power ON lesen (UC: 0x71)
            digitalWrite(PIN_CS, LOW); digitalWrite(PIN_DC, LOW); bbByte(0x71);
            digitalWrite(PIN_DC, HIGH); uint8_t f = bbRead(); digitalWrite(PIN_CS, HIGH);
            Serial.printf("  0x71 nach POF: %02X\n", f);
            break;
        }
        case 'k': {   // Kopplung GPIO25 <-> GPIO26 in beiden Richtungen, je nur kurz treiben
            Serial.println("\n--- Kopplung GPIO26(RST) -> GPIO25(BUSY) ---");
            for (int lvl = 0; lvl < 2; lvl++) {
                pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, lvl);
                pinMode(PIN_BUSY, INPUT_PULLUP);   delay(2); int u = digitalRead(PIN_BUSY);
                pinMode(PIN_BUSY, INPUT_PULLDOWN); delay(2); int d = digitalRead(PIN_BUSY);
                Serial.printf("  RST=%d: BUSY up=%d down=%d\n", lvl, u, d);
            }
            pinMode(PIN_BUSY, INPUT);
            pinMode(PIN_RST, INPUT);
            Serial.println("--- Kopplung GPIO25(BUSY) -> GPIO26(RST) ---");
            for (int lvl = 0; lvl < 2; lvl++) {
                pinMode(PIN_BUSY, OUTPUT); digitalWrite(PIN_BUSY, lvl);
                pinMode(PIN_RST, INPUT_PULLUP);   delay(2); int u = digitalRead(PIN_RST);
                pinMode(PIN_RST, INPUT_PULLDOWN); delay(2); int d = digitalRead(PIN_RST);
                pinMode(PIN_BUSY, INPUT);
                Serial.printf("  BUSY=%d: RST up=%d down=%d\n", lvl, u, d);
            }
            pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, HIGH);
            break;
        }
        case 'p': {   // GPIO25/26 nur als Eingang: wer verhaelt sich wie BUSY?
            Serial.println("\n--- Pin-Diagnose GPIO25 (BUSY) / GPIO26 (RST), beide nur Eingang ---");
            pinMode(PIN_RST, INPUT_PULLUP);  pinMode(PIN_BUSY, INPUT_PULLUP);  delay(5);
            int u26 = digitalRead(PIN_RST), u25 = digitalRead(PIN_BUSY);
            pinMode(PIN_RST, INPUT_PULLDOWN); pinMode(PIN_BUSY, INPUT_PULLDOWN); delay(5);
            int d26 = digitalRead(PIN_RST), d25 = digitalRead(PIN_BUSY);
            Serial.printf("  GPIO26: up=%d down=%d  GPIO25: up=%d down=%d\n", u26, d26, u25, d25);
            String s25, s26;
            sendIndex1(0x00, 0x0E);                         // Soft-Reset
            for (int i = 0; i < 100; i++) { s25 += digitalRead(PIN_BUSY) ? '1':'0'; s26 += digitalRead(PIN_RST) ? '1':'0'; delay(1); }
            s25 += '|'; s26 += '|';
            sendIndex1(0x04, 0x00);                         // Power ON
            for (int i = 0; i < 200; i++) { s25 += digitalRead(PIN_BUSY) ? '1':'0'; s26 += digitalRead(PIN_RST) ? '1':'0'; delay(2); }
            sendIndex1(0x02, 0x00);                         // Power OFF
            Serial.println("  Verlauf (Pull-down; Soft-Reset 100ms | Power ON 400ms):");
            Serial.printf("  GPIO25: %s\n  GPIO26: %s\n", s25.c_str(), s26.c_str());
            pinMode(PIN_BUSY, INPUT);
            pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, HIGH);
            break;
        }
        case 'r': {   // Antwortet der Chip auch bei gehaltenem Reset?
            for (int k = 0; k < 2; k++) {
                hwReset();
                if (k == 1) { digitalWrite(PIN_RST, LOW); delay(20); }
                digitalWrite(PIN_CS, LOW);
                digitalWrite(PIN_DC, LOW); bbByte(0x70);
                digitalWrite(PIN_DC, HIGH);
                uint8_t a = bbRead(), b2 = bbRead(), c2 = bbRead();
                digitalWrite(PIN_CS, HIGH);
                Serial.printf("RST %s: 0x70 -> %02X %02X %02X\n", k ? "LOW gehalten" : "HIGH (normal)", a, b2, c2);
                digitalWrite(PIN_RST, HIGH); delay(20);
            }
            break;
        }
        case 'f': g_fixedWait = true; runTest(1, 0); g_fixedWait = false; menu(); break;
        default: break;
    }
}
