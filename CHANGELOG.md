# Changelog

Änderungen an der Bibliothek `SesEpaper` (bis 1.x: `ses-imagotag-7in4-bwr-epaper`). Versionen nach [Semantic Versioning](https://semver.org/lang/de/).

## 2.1.0 – 2026-11-25

### Neu

- **`Epd581`** für das 5,81"-Panel aus dem Preisschild SES-imagotag VUSION 5.9 BWR GU110 (`EDG3-0590-A`), Pervasive E2581JS061, 720 × 256. Neuere Treiber-Generation 06 (Chip-ID `16 87 05`), alle Parameter aus dem OTP des Panels. Getestet am Waveshare ESP32 und am Seengreat ESP32-C6. Details: [doc/panel-5.81-zoll.md](doc/panel-5.81-zoll.md)
- `examples/Demo581` mit Logo, Reinigung (`c`) und OTP-Ausgabe (`o`)
- `EpdPins::busyPullup`: interner Pull-up an BUSY für Boards ohne eigenen Pull-up, z. B. Seengreat C6 mit ausgelötetem R2 für geringen Schlafstrom ([doc/boards.md](doc/boards.md#schlafstrom-akkubetrieb))
- `setBusyCallback(fn, ctx)` in allen Klassen: ersetzt beim Warten auf BUSY das `delay(10)`, z. B. für Light Sleep während des Refresh im Akkubetrieb

### Geändert

- Diagnosefirmware erkennt das 5,81"-Panel an der Chip-ID. Die BUSY-Prüfung wartet die Reset-Phase ab und meldet nicht mehr fälschlich „OFFEN“. Fertige `.bin` für beide Boards neu gebaut.
- README: Installation über PlatformIO-Registry und Arduino Library Manager, Hinweis auf den Adafruit-Blog
- Neue Fotos aller vier Panels

Bestehender Code läuft unverändert weiter. Ohne die neuen Einstellungen verhalten sich alle Klassen wie in 2.0.0.

## 2.0.0 – 2026-11-20

- Bibliothek heißt `SesEpaper`, Repo `ses-imagotag-bwr-epaper`. Klassen und Header sind unverändert, anzupassen ist nur `lib_deps`.
- **`Epd42`** für das 4,2"-Panel (Pervasive E2417JS0D1, UC8176-kompatibel, 400 × 300) mit einstellbarer Temperatur
- Demos als eigene PlatformIO-Projekte (`examples/Demo74`, `Demo565`, `Demo42`) mit Umgebungen für Waveshare ESP32 und Seengreat ESP32-C6
- `doc/boards.md`: Flexkabel, Schalter und Pins beider Boards
- Diagnosefirmware mit Anleitung ([doc/diagnose.md](doc/diagnose.md)) und fertigen `.bin` zum Flashen im Browser

## 1.1.0 – 2026-11-19

- **`Epd565`** für das 5,65"-Panel aus VUSION 6.0 (Pervasive E2565JS0B1, IST7136, 600 × 448), Parameter aus dem OTP des Panels
- `EpdCommon.h` mit gemeinsamen Farben, `EpdPins` und `epdPinsSeengreatC6()`. `Epd74Pins` bleibt als Alias erhalten.

## 1.0.0 – 2026-11-12

- **`Epd74`** für das 7,4"-Panel (Pervasive E2741ES083, IST7x32, 800 × 480), Adafruit GFX
