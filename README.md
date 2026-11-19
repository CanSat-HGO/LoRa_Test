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

Der nackte RFM95W hat 2,0 mm Pad-Raster → Adapterplatine oder Drähte direkt anlöten.

## Firmware (PlatformIO)

```bash
pio run -t upload        # bauen + flashen
pio device monitor       # serieller Monitor (115200)
```

Alle Pins und Funkparameter stehen in [`include/config.h`](include/config.h).

Im seriellen Monitor umschalten:
- `t` – Sendemodus: alle 2 s `Hallo vom Teensy #N`
- `r` – Empfangsmodus: zeigt empfangene Pakete als Text + Hex inkl. RSSI/SNR

## Waveshare-Stick (Mac)

```bash
ls /dev/tty.usb*                          # Port finden (sonst Treiber laut Waveshare-Wiki installieren)
pip3 install -r tools/requirements.txt
python3 tools/lora_monitor.py             # Empfangen + AT-Befehle senden
python3 tools/lora_monitor.py --log empfang.csv
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
