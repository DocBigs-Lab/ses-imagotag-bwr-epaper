// Demo: Epd565-Treiber mit Adafruit GFX auf dem SES-imagotag 5,65" BWR
// (VUSION 6.0, Pervasive E2565JS0B1). Das Bild wird einmal beim Start gezeichnet.
// Board: Waveshare e-Paper ESP32 Driver Board (Schalter 1 = B, 2 = ON) oder,
// mit -DBOARD_SEENGREAT_C6, Seengreat ESP32-C6 E-Paper Driver Board (S1 = ON, S2 = ON).
#include <Arduino.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

#include <Epd565.h>

#include "logo.h"

#if defined(BOARD_SEENGREAT_C6)
static Epd565 epd(epdPinsSeengreatC6());
#else
static Epd565 epd;
#endif

// DocBigsLab-Logo: Haus-Icon rot, Schriftzug schwarz
static void drawLogo(int16_t x0, int16_t y0) {
    epd.drawBitmap(x0 + LOGO_ICON_X, y0 + LOGO_ICON_Y, LOGO_ICON_BITS, LOGO_ICON_W, LOGO_ICON_H, EPD_RED);
    epd.drawBitmap(x0 + LOGO_TEXT_X, y0 + LOGO_TEXT_Y, LOGO_TEXT_BITS, LOGO_TEXT_W, LOGO_TEXT_H, EPD_BLACK);
}

static void drawDemo() {
    epd.fillScreen(EPD_WHITE);

    // Kopfzeile
    epd.fillRect(0, 0, 600, 56, EPD_RED);
    epd.setFont(&FreeSansBold18pt7b);
    epd.setTextColor(EPD_WHITE);
    epd.setCursor(20, 40);
    epd.print("Epd565");

    // Text
    epd.setFont(&FreeSans12pt7b);
    epd.setTextColor(EPD_BLACK);
    epd.setCursor(20, 90);
    epd.print("SES-imagotag VUSION 6.0 BWR, 600 x 448");
    epd.setCursor(20, 118);
    epd.print("Pervasive E2565JS0B1, IST7136");
    epd.setTextColor(EPD_RED);
    epd.setCursor(20, 146);
    epd.print("Rot geht auch.");

    // Formen
    epd.fillCircle(80, 225, 50, EPD_BLACK);
    epd.fillCircle(80, 225, 28, EPD_RED);
    epd.drawRect(160, 175, 100, 100, EPD_BLACK);
    epd.fillRect(176, 191, 68, 68, EPD_RED);
    epd.fillTriangle(290, 275, 345, 175, 400, 275, EPD_BLACK);
    for (int i = 0; i < 10; i++) {
        epd.drawLine(440, 175 + i * 11, 580, 275 - i * 11, i % 2 ? EPD_RED : EPD_BLACK);
    }

    // Logo unten, mittig
    epd.drawFastHLine(20, 300, 560, EPD_BLACK);
    drawLogo((600 - LOGO_W) / 2, 318);

    epd.drawRect(0, 0, 600, 448, EPD_BLACK);
}

static void show(const char *what) {
    Serial.printf("%s ... ", what);
    bool ok = epd.display();
    Serial.printf("%s, %lu ms\n", ok ? "ok" : "Fehler", (unsigned long)epd.lastRefreshMs());
}

// Gegen Geisterbilder: mehrmals schwarz, rot, weiss (je ca. 35 s)
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
    Serial.println("\n=== Epd565 Demo ===");

    if (!epd.begin()) {
        Serial.println("Kein Speicher oder OTP des Panels nicht lesbar.");
        return;
    }

//    epd.setRotation(2);   // 180 Grad: Bild steht dann mit FPC-Kante oben

    uint32_t id = epd.readChipId();
    Serial.printf("Chip-ID: %06lX %s\n", (unsigned long)id, id == 0x71365A ? "(ok)" : "(unerwartet!)");

    epd.setTemperature(25);
    drawDemo();
    Serial.println("Sende Bild, Refresh dauert ca. 31 s ...");
    bool ok = epd.display();
    Serial.printf("%s, Refresh %lu ms\n", ok ? "Fertig" : "Fehler", (unsigned long)epd.lastRefreshMs());
    Serial.println("Befehle: c = Panel reinigen (2 Runden), d = Demo neu zeichnen");
}
void loop() {
    if (!Serial.available()) return;
    switch (Serial.read()) {
        case 'c': cleanPanel(2); Serial.println("Reinigung fertig."); break;
        case 'd': drawDemo(); show("Demo"); break;
        default: break;
    }
}
