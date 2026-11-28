// Demo: Epd581-Treiber mit Adafruit GFX auf dem SES-imagotag VUSION 5.9 BWR (5,81", E2581JS061).
// Das Bild wird einmal beim Start gezeichnet; Reset-Taste (EN) zeichnet neu.
// Board: Waveshare e-Paper ESP32 Driver Board (Schalter 1 = B, 2 = ON) oder,
// mit -DBOARD_SEENGREAT_C6, Seengreat ESP32-C6 E-Paper Driver Board (S1 = ON, S2 = ON).
// "OL" und "UR" markieren die Ecken oben links und unten rechts (Flexkabel rechts).
#include <Arduino.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

#include <Epd581.h>

#include "logo.h"
#include "qr.h"

#if defined(BOARD_SEENGREAT_C6)
static Epd581 epd(epdPinsSeengreatC6());
#else
static Epd581 epd;
#endif

static constexpr int16_t W = Epd581::WIDTH_PX;    // 720
static constexpr int16_t H = Epd581::HEIGHT_PX;   // 256

static void drawLogo(int16_t x0, int16_t y0) {
    epd.drawBitmap(x0 + LOGO_ICON_X, y0 + LOGO_ICON_Y, LOGO_ICON_BITS, LOGO_ICON_W, LOGO_ICON_H, EPD_RED);
    epd.drawBitmap(x0 + LOGO_TEXT_X, y0 + LOGO_TEXT_Y, LOGO_TEXT_BITS, LOGO_TEXT_W, LOGO_TEXT_H, EPD_BLACK);
}

// QR-Code aus qr.h, jedes Modul als scale x scale Quadrat
static void drawQr(int16_t x0, int16_t y0, int16_t scale) {
    for (int16_t r = 0; r < QR_MODULES; r++) {
        for (int16_t c = 0; c < QR_MODULES; c++) {
            uint8_t b = pgm_read_byte(&QR_BITS[r * QR_BYTES_PER_ROW + c / 8]);
            if (b & (0x80 >> (c & 7))) epd.fillRect(x0 + c * scale, y0 + r * scale, scale, scale, EPD_BLACK);
        }
    }
}

static void drawDemo() {
    epd.fillScreen(EPD_WHITE);

    // Kopfzeile
    epd.fillRect(0, 0, W, 48, EPD_RED);
    epd.setFont(&FreeSansBold18pt7b);
    epd.setTextColor(EPD_WHITE);
    epd.setCursor(70, 36);
    epd.print("Epd581  -  720 x 256");

    // Lagemarken
    epd.setFont(&FreeSans12pt7b);
    epd.setTextColor(EPD_WHITE);
    epd.setCursor(6, 32);
    epd.print("OL");
    epd.fillTriangle(48, 18, 48, 30, 60, 24, EPD_WHITE);
    epd.fillRect(W - 40, H - 30, 40, 30, EPD_BLACK);
    epd.setCursor(W - 36, H - 8);
    epd.print("UR");

    // Logo links, Formen rechts
    drawLogo(10, 70);
    epd.fillCircle(530, 140, 50, EPD_BLACK);
    epd.fillCircle(530, 140, 28, EPD_RED);
    drawQr(600, 88, 3);   // QR-Code zum Repo

    epd.setTextColor(EPD_BLACK);
    epd.setCursor(10, 220);
    epd.print("Schwarz, ");
    epd.setTextColor(EPD_RED);
    epd.print("Rot");
    epd.setTextColor(EPD_BLACK);
    epd.print(" und Weiss");
    epd.setFont(&FreeSans9pt7b);
    epd.setTextColor(EPD_RED);
    epd.setCursor(10, 244);
    epd.print("github.com/DocBigs-Lab/ses-imagotag-bwr-epaper");

    epd.drawRect(0, 0, W, H, EPD_BLACK);
}

static void show(const char *what) {
    Serial.printf("%s ... ", what);
    bool ok = epd.display();
    Serial.printf("%s, %lu ms\n", ok ? "ok" : "BUSY-Timeout", (unsigned long)epd.lastRefreshMs());
}

static void cleanPanel(int rounds) {
    for (int r = 1; r <= rounds; r++) {
        Serial.printf("Reinigung Runde %d/%d\n", r, rounds);
        epd.fillScreen(EPD_BLACK); show("  schwarz");
        epd.fillScreen(EPD_RED);   show("  rot");
        epd.fillScreen(EPD_WHITE); show("  weiss");
    }
}

static void printHelp() {
    Serial.println("Befehle: c = Panel reinigen (2 Runden), d = Demo neu zeichnen, o = OTP ausgeben");
}

static void dumpOtp() {
    const uint8_t *o = epd.otp();
    Serial.println("OTP (128 Byte):");
    for (int i = 0; i < 128; i++) {
        if (i % 16 == 0) Serial.printf("  %02X:", i);
        Serial.printf(" %02X", o[i]);
        if (i % 16 == 15) Serial.println();
    }
    const uint8_t *u = &o[0x15];
    Serial.printf("DUW: Byte %u..%u, Zeile %u..%u\n", u[0], u[1],
                  u[2] | (u[3] << 8), u[4] | (u[5] << 8));
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Epd581 Demo ===");

    bool ok = epd.begin();
    uint32_t id = epd.readChipId();
    Serial.printf("Chip-ID: %06lX\n", (unsigned long)id);
    dumpOtp();
    if (!ok) {
        Serial.println("OTP passt nicht (Kennung 0x16, 32 Byte x 720 Zeilen erwartet) oder kein Speicher.");
        Serial.println("Bitte die OTP-Ausgabe oben schicken.");
        return;
    }

    epd.setTemperature(25);
    drawDemo();
    Serial.println("Sende Bild ...");
    show("Demo");
    printHelp();
}

void loop() {
    if (!Serial.available()) return;
    switch (Serial.read()) {
        case 'c': cleanPanel(2); Serial.println("Reinigung fertig."); break;
        case 'd': drawDemo(); show("Demo"); break;
        case 'o': dumpOtp(); break;
        case 'h': printHelp(); break;
        default: break;
    }
}
