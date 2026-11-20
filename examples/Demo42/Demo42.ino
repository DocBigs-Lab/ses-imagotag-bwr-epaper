// Demo: Epd42-Treiber mit Adafruit GFX auf dem SES-imagotag 4,2" BWR
// (Pervasive E2417JS0D1). Das Bild wird einmal beim Start gezeichnet.
// Board: Waveshare e-Paper ESP32 Driver Board (Schalter 1 = B, 2 = ON) oder,
// mit -DBOARD_SEENGREAT_C6, Seengreat ESP32-C6 E-Paper Driver Board (S1 = ON, S2 = ON).
#include <Arduino.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include <Epd42.h>

#include "logo.h"

#if defined(BOARD_SEENGREAT_C6)
static Epd42 epd(epdPinsSeengreatC6());
#else
static Epd42 epd;
#endif

static float g_temp = 25;   // Grad C, mit 't' umschaltbar

// DocBigsLab-Logo: Haus-Icon rot, Schriftzug schwarz
static void drawLogo(int16_t x0, int16_t y0) {
    epd.drawBitmap(x0 + LOGO_ICON_X, y0 + LOGO_ICON_Y, LOGO_ICON_BITS, LOGO_ICON_W, LOGO_ICON_H, EPD_RED);
    epd.drawBitmap(x0 + LOGO_TEXT_X, y0 + LOGO_TEXT_Y, LOGO_TEXT_BITS, LOGO_TEXT_W, LOGO_TEXT_H, EPD_BLACK);
}

static void drawDemo() {
    epd.fillScreen(EPD_WHITE);

    // Kopfzeile
    epd.fillRect(0, 0, 400, 40, EPD_RED);
    epd.setFont(&FreeSansBold12pt7b);
    epd.setTextColor(EPD_WHITE);
    epd.setCursor(14, 29);
    epd.print("Epd42");

    // Text
    epd.setFont(&FreeSans9pt7b);
    epd.setTextColor(EPD_BLACK);
    epd.setCursor(14, 62);
    epd.print("SES-imagotag, Pervasive E2417JS0D1");
    epd.setCursor(14, 82);
    epd.print("UC8176-kompatibel, 400 x 300");
    epd.setTextColor(EPD_RED);
    epd.setCursor(14, 102);
    epd.print("Rot geht auch.");

    // Formen
    epd.fillCircle(52, 156, 38, EPD_BLACK);
    epd.fillCircle(52, 156, 22, EPD_RED);
    epd.drawRect(108, 116, 80, 80, EPD_BLACK);
    epd.fillRect(120, 128, 56, 56, EPD_RED);
    epd.fillTriangle(208, 196, 250, 116, 292, 196, EPD_BLACK);
    for (int i = 0; i < 9; i++) {
        epd.drawLine(306, 116 + i * 10, 388, 196 - i * 10, i % 2 ? EPD_RED : EPD_BLACK);
    }

    // Logo unten, mittig
    epd.drawFastHLine(14, 206, 372, EPD_BLACK);
    drawLogo((400 - LOGO_W) / 2, 214);

    epd.drawRect(0, 0, 400, 300, EPD_BLACK);
}

static void show(const char *what) {
    Serial.printf("%s bei %.0f C ... ", what, g_temp);
    bool ok = epd.display();
    Serial.printf("%s, %lu ms\n", ok ? "ok" : "BUSY-Timeout", (unsigned long)epd.lastRefreshMs());
}

// Gegen Geisterbilder: mehrmals schwarz, rot, weiss
static void cleanPanel(int rounds) {
    for (int r = 1; r <= rounds; r++) {
        Serial.printf("Reinigung Runde %d/%d\n", r, rounds);
        epd.fillScreen(EPD_BLACK); show("  schwarz");
        epd.fillScreen(EPD_RED);   show("  rot");
        epd.fillScreen(EPD_WHITE); show("  weiss");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Epd42 Demo ===");

    if (!epd.begin()) {
        Serial.println("Kein Speicher fuer die Bildpuffer.");
        return;
    }
    uint32_t id = epd.readChipId();
    Serial.printf("Antwort auf 0x70: %06lX %s\n", (unsigned long)id,
                  (id >> 8) == 0x0007 ? "(ok)" : "(unerwartet!)");

    epd.setTemperature(g_temp);
    drawDemo();
    show("Demo");
    Serial.println("Befehle: c = reinigen (2 Runden), d = Demo neu zeichnen,");
    Serial.println("         t = Temperatur umschalten (25, 15, 5 C) und neu zeichnen");
}

void loop() {
    if (!Serial.available()) return;
    switch (Serial.read()) {
        case 'c': cleanPanel(2); Serial.println("Reinigung fertig."); break;
        case 'd': drawDemo(); show("Demo"); break;
        case 't':
            g_temp = g_temp > 20 ? 15 : g_temp > 10 ? 5 : 25;
            epd.setTemperature(g_temp);
            drawDemo();
            show("Demo");
            break;
        default: break;
    }
}
