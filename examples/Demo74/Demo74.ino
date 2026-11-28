// Demo: Epd74-Treiber mit Adafruit GFX auf dem SES-imagotag 7,4" BWR.
// Das Bild wird einmal beim Start gezeichnet; Reset-Taste (EN) zeichnet neu.
// Board: Waveshare e-Paper ESP32 Driver Board (Schalter 1 = B, 2 = ON) oder,
// mit -DBOARD_SEENGREAT_C6, Seengreat ESP32-C6 E-Paper Driver Board (S1 = ON, S2 = ON).
#include <Arduino.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include <Epd74.h>

#include "logo.h"
#include "qr.h"

#if defined(BOARD_SEENGREAT_C6)
static Epd74 epd(epdPinsSeengreatC6());
#else
static Epd74 epd;
#endif

// DocBigsLab-Logo: Haus-Icon rot, Schriftzug schwarz
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
    epd.fillRect(0, 0, 800, 64, EPD_RED);
    epd.setFont(&FreeSansBold24pt7b);
    epd.setTextColor(EPD_WHITE);
    epd.setCursor(24, 48);
    epd.print("Epd74");

    // Text
    epd.setFont(&FreeSans12pt7b);
    epd.setTextColor(EPD_BLACK);
    epd.setCursor(24, 100);
    epd.print("SES-imagotag 7,4\" BWR  -  Pervasive E2741ES083, 800 x 480");
    epd.setCursor(24, 130);
    epd.print("Adafruit GFX: Text, Linien, Kreise, Flaechen");
    epd.setTextColor(EPD_RED);
    epd.setCursor(24, 160);
    epd.print("Rot geht auch:  github.com/DocBigs-Lab/ses-imagotag-bwr-epaper");

    // Formen
    epd.fillCircle(100, 240, 56, EPD_BLACK);
    epd.fillCircle(100, 240, 32, EPD_RED);
    epd.drawRect(210, 184, 112, 112, EPD_BLACK);
    epd.fillRect(228, 202, 76, 76, EPD_RED);
    epd.fillTriangle(380, 296, 450, 184, 520, 296, EPD_BLACK);
    for (int i = 0; i < 12; i++) {
        epd.drawLine(580, 184 + i * 10, 770, 296 - i * 10, i % 2 ? EPD_RED : EPD_BLACK);
    }

    // Logo unten links, QR-Code zum Repo rechts
    epd.drawFastHLine(24, 322, 752, EPD_BLACK);
    drawLogo(24, 340);
    drawQr(677, 350, 3);

    epd.drawRect(0, 0, 800, 480, EPD_BLACK);
}

static void show(const char *what) {
    Serial.printf("%s ... ", what);
    bool ok = epd.display();
    Serial.printf("%s, %lu ms\n", ok ? "ok" : "BUSY-Timeout", (unsigned long)epd.lastRefreshMs());
}

// Gegen Geisterbilder: mehrmals schwarz, rot, weiss (je ca. 28 s)
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
    Serial.println("\n=== Epd74 Demo ===");

    if (!epd.begin()) {
        Serial.println("Kein Speicher fuer die Bildpuffer.");
        return;
    }
    uint32_t id = epd.readChipId();
    Serial.printf("Chip-ID: %06lX %s\n", (unsigned long)id, id == 0x71325A ? "(ok)" : "(unerwartet!)");

    epd.setTemperature(25);
    drawDemo();
    Serial.println("Sende Bild, Refresh dauert ca. 28 s ...");
    bool ok = epd.display();
    Serial.printf("%s, Refresh %lu ms\n", ok ? "Fertig" : "BUSY-Timeout", (unsigned long)epd.lastRefreshMs());
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
