# LoRa_Test

Teensy 4.1 + RFM95W (SX1276) sendet per LoRa an einen **Waveshare USB-TO-LoRa-HF** (SX1262) am Rechner.

```
[Teensy 4.1] --SPI--> [RFM95W] ~~~ 868 MHz ~~~> [Waveshare USB-TO-LoRa-HF] --USB--> [Mac]
```

## Verkabelung

| RFM95W | Teensy 4.1 | Hinweis |
|---|---|---|
| 3.3V | 3.3V | **nicht** an 5V/VIN |
| GND | GND | |
| SCK | 13 | |
| MISO | 12 | |
| MOSI | 11 | |
| NSS / CS | 10 | |
| RESET | 9 | |
| DIO0 / G0 | 2 | Interrupt |
| ANT | Antenne | λ/4-Draht ≈ 8,2 cm für 868 MHz – **nie ohne Antenne senden** |

### GY-BME280 / GY-BMP280 (I2C)

| Sensor | Teensy 4.1 |
|---|---|
| VCC / VIN | 3.3V |
| GND | GND |
| SCL | 19 |
| SDA | 18 |

Adresse 0x76 oder 0x77 wird automatisch erkannt, ebenso BME280 (mit Feuchte) vs. BMP280 (ohne).

### Datenformat

Eine CSV-Zeile pro Funkpaket:

```
Zaehler,ms_seit_start,Temp_C,Druck_hPa,Feuchte_%,Hoehe_m
12,24000,21.53,1012.84,45.2,3.4
```

Beim BMP280 bleibt die Feuchte leer (`…,1012.84,,3.4`). Ohne Sensor: `12,24000,NOSENSOR`.

### Höhe und QNH

Höhe = `44330 · (1 − (p / QNH)^0,1903)` (barometrische Höhenformel). QNH ist der **relative Luftdruck (NHN)**
aus dem Wetterbericht – nicht der absolute. 1 hPa Fehler ≈ 8 m.

Der QNH kommt per Funk von der Bodenstation (`--qnh`), wird im EEPROM des Teensy gespeichert und
überlebt Neustarts und Neu-Flashen. Ohne gespeicherten Wert gilt `SEA_LEVEL_HPA` aus `config.h` (1013,25).

Rückkanal Bodenstation → Teensy: Der Teensy hört zwischen seinen Messungen zu.
Befehl `QNH=985.00` → Antwort `ACK,QNH,985.00` (400 ms verzögert, damit der Stick wieder empfangsbereit ist).

Der nackte RFM95W hat 2,0 mm Pad-Raster → Adapterplatine oder Drähte direkt anlöten.

## Firmware (PlatformIO)

```bash
pio run -t upload        # bauen + flashen
pio device monitor       # serieller Monitor (115200)
```

Alle Pins und Funkparameter stehen in [`include/config.h`](include/config.h).

Im seriellen Monitor umschalten:
- `t` – Sendemodus: alle 2 s eine Messwert-Zeile (siehe Datenformat)
- `r` – Empfangsmodus: zeigt empfangene Pakete als Text + Hex inkl. RSSI/SNR

## Waveshare-Stick (Mac)

Einmalig einrichten:

```bash
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
```

### Bodenstation (Messwerte speichern + Live-Diagramm)

```bash
.venv/bin/python tools/ground_station.py                 # Live-Diagramm, speichert nach data/
.venv/bin/python tools/ground_station.py --no-plot       # nur speichern + Terminalausgabe
.venv/bin/python tools/ground_station.py --qnh 985       # QNH (rel. Luftdruck NHN) an den Teensy schicken
.venv/bin/python tools/ground_station.py --replay data/flug_2026-11-19_16-25-08.csv
```

- Speichert jede Messung als `data/flug_<datum>_<uhrzeit>.csv` (Spalten: `empfangen,zaehler,ms,temp_c,druck_hpa,feuchte,hoehe_m`).
- Diagramme: Temperatur, Luftdruck, Höhe, Luftfeuchte; Statuszeile mit Paketzahl, verlorenen Paketen und Teensy-Neustarts.
- Beschädigte Funkzeilen werden per Plausibilitätscheck verworfen; alte, im Stick gepufferte Pakete beim Start ebenfalls.
- `--qnh` wird wiederholt (alle 2,5 s, max. 8×), bis der Teensy mit `ACK` bestätigt.
- Immer nur **ein** Programm gleichzeitig am Stick-Port (sonst teilen sich die Programme die Daten).

### Roh-Monitor / AT-Befehle

```bash
ls /dev/tty.usb*                          # Port finden (sonst Treiber laut Waveshare-Wiki installieren)
.venv/bin/python tools/lora_monitor.py    # Empfangen + AT-Befehle senden
```

Mit `AT` testen, ob der Stick antwortet. Befehlsliste: [Waveshare Wiki – USB-TO-LoRa-xF](https://www.waveshare.com/wiki/USB-TO-LoRa-xF).

## Parameter abgleichen

Beide Seiten müssen identisch sein:

| Parameter | Teensy (`config.h`) | Waveshare-Default |
|---|---|---|
| Frequenz | 868.0 MHz | Kanal 18 (`AT+TXCH`/`AT+RXCH`, Frequenz = 850 + Kanal MHz) |
| Bandbreite | 125 kHz | `AT+BW=0` (125 kHz) |
| Spreading Factor | 7 | `AT+SF=7` |
| Coding Rate | 4/5 | `AT+CR=1` (4/5) |
| Sync Word | 0x12 | funktioniert ✅ |
| Header | `00 00 00 11` | wird vom Stick hinzugefügt/entfernt |

**Getestet (FW 1.2):** Der Stick setzt im Stream-Modus (`AT+MODE=1`, NETID 0, ADDR 0) die 4 Bytes `00 00 00 11` vor die Nutzdaten und erwartet sie auch beim Empfang. Ohne diesen Header verwirft er Pakete vom Teensy. Bei anderer NETID/ADDR ändert sich der Header vermutlich → mit `r` neu mitschneiden.

**AT-Modus:** `+++` (mit CRLF) → Befehle → `AT+EXIT`. `AT+ALLP?` zeigt alle Einstellungen. Den KEY-Taster beim Einstecken nicht gedrückt halten (Firmware-Flash-Modus).

## Fehlersuche

Waveshare gibt an, dass der Stick nur mit eigenen Modulen kompatibel ist – seine Firmware hängt eigene Header-Bytes an.

1. Teensy mit `r` in den Empfangsmodus schalten.
2. Am Stick (`lora_monitor.py`) z. B. `ABC` senden.
3. Teensy-Ausgabe ansehen: erscheint `41 42 43` mit zusätzlichen Bytes davor, sind das die Header.
4. Diese Bytes in `config.h` bei `TX_HEADER_BYTES` eintragen, z. B. `{0x00, 0x01}`, dann mit `t` senden.
5. Kommt gar nichts an: Frequenz, SF, BW und Sync Word prüfen.
