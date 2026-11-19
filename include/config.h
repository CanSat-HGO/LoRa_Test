#pragma once

// ---------------------------------------------------------------------------
// Pins: Teensy 4.1 <-> RFM95W (Hardware-SPI)
//   RFM95W  -> Teensy
//   3.3V    -> 3.3V   (NICHT 5V/VIN)
//   GND     -> GND
//   SCK     -> 13
//   MISO    -> 12
//   MOSI    -> 11
//   NSS/CS  -> 10
//   RESET   -> 9
//   DIO0/G0 -> 2
// ---------------------------------------------------------------------------
#define PIN_LORA_CS    10
#define PIN_LORA_DIO0  2
#define PIN_LORA_RST   9

// ---------------------------------------------------------------------------
// Funkparameter: muessen EXAKT zum Waveshare USB-TO-LoRa-HF passen
// (Waveshare-Defaults: SF7, BW125, CR4/5)
// ---------------------------------------------------------------------------
// Waveshare: Frequenz = 850 + Kanal (MHz), Kanal 0-80 (AT+TXCH / AT+RXCH).
// Default-Kanal 18 -> 868.0 MHz
#define LORA_FREQ_MHZ   868.0f
#define LORA_BW_KHZ     125.0f
#define LORA_SF         7
#define LORA_CR         5        // 5 = 4/5
#define LORA_SYNC_WORD  0x12     // private LoRa (SX1262: 0x1424)
#define LORA_POWER_DBM  14       // DE 868 MHz: max. ~14 dBm (25 mW ERP)
#define LORA_PREAMBLE   8

// ---------------------------------------------------------------------------
// Betriebsart
//   MODE_TX : sendet alle TX_INTERVAL_MS eine Testnachricht
//   MODE_RX : empfaengt und gibt Bytes als Text + Hex aus
//             (zum "Abhoeren" des Waveshare-Sticks, um dessen Paketformat
//              bzw. zusaetzliche Header-Bytes zu sehen)
// Ueber den seriellen Monitor umschaltbar: 't' = TX, 'r' = RX
// ---------------------------------------------------------------------------
#define START_IN_RX_MODE  0
#define TX_INTERVAL_MS    2000   // Achtung 868-MHz-Duty-Cycle (1 %): nicht zu oft senden

// ---------------------------------------------------------------------------
// GY-BME280 / GY-BMP280 (I2C)
//   VCC -> 3.3V, GND -> GND, SCL -> 19, SDA -> 18
// Nachrichtenformat (CSV, eine Zeile pro Paket):
//   Zaehler,ms_seit_start,Temp_C,Druck_hPa,Feuchte_%,Hoehe_m
//   Feuchte ist leer beim BMP280; "NOSENSOR" wenn nichts gefunden.
// ---------------------------------------------------------------------------
// Standardwert fuer den Luftdruck auf Meereshoehe (QNH / "relativer Luftdruck NHN").
// Wird ueberschrieben, sobald die Bodenstation per Funk "QNH=<hPa>" schickt;
// der empfangene Wert bleibt im EEPROM gespeichert.
#define SEA_LEVEL_HPA     1013.25f

// Optionale Bytes, die vor jede Nachricht gesetzt werden (z.B. Adress-Header,
// falls der Waveshare-Stick so etwas erwartet). Leer lassen = rohes LoRa.
// Waveshare USB-TO-LoRa-HF (FW 1.2, NETID 0, ADDR 0, Stream-Modus) sendet
// selbst 00 00 00 11 vor die Nutzdaten (per RX-Mitschnitt ermittelt).
#define TX_HEADER_BYTES   {0x00, 0x00, 0x00, 0x11}
