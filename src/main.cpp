#include <Arduino.h>
#include <RadioLib.h>
#include "config.h"
#include "sensor.h"

RFM95 radio = new Module(PIN_LORA_CS, PIN_LORA_DIO0, PIN_LORA_RST, RADIOLIB_NC);

static volatile bool irqFlag = false;
static bool rxMode = START_IN_RX_MODE;
static const uint8_t txHeader[] = TX_HEADER_BYTES;

static void onDio0() { irqFlag = true; }

static void printHex(const uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
    Serial.print(' ');
  }
}

static void printAscii(const uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    Serial.print(isPrintable(buf[i]) ? (char)buf[i] : '.');
  }
}

static void startRx() {
  rxMode = true;
  irqFlag = false;
  int state = radio.startReceive();
  Serial.printf("[RX] Empfangsmodus %s (Code %d)\n",
                state == RADIOLIB_ERR_NONE ? "aktiv" : "FEHLER", state);
}

static void startTxMode() {
  rxMode = false;
  irqFlag = false;
  radio.startReceive();  // zwischen den Messungen auf Befehle hoeren
  Serial.println("[TX] Sendemodus aktiv (hoert zwischendurch auf Befehle)");
}

static void handleSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' && !rxMode) startRx();
    else if (c == 't' && rxMode) startTxMode();
  }
}

// Sendet eine Textzeile (mit Waveshare-Header) und hoert danach wieder zu.
static bool sendLine(const char *text, size_t textLen) {
  uint8_t pkt[sizeof(txHeader) + 128];
  if (textLen > 128) textLen = 128;
  memcpy(pkt, txHeader, sizeof(txHeader));
  memcpy(pkt + sizeof(txHeader), text, textLen);
  size_t len = sizeof(txHeader) + textLen;

  int state = radio.transmit(pkt, len);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("[TX] %u Bytes: ", (unsigned)len);
    printAscii(pkt, len);
    Serial.print("  | ");
    printHex(pkt, len);
    Serial.println();
  } else {
    Serial.printf("[TX] Fehler %d\n", state);
  }
  // TX-Done-Interrupt verwerfen und zwischen den Messungen empfangsbereit sein
  irqFlag = false;
  radio.startReceive();
  return state == RADIOLIB_ERR_NONE;
}

// Bestaetigung wird verzoegert gesendet: der Waveshare-Stick braucht nach dem
// eigenen Senden etwas Zeit, bis er wieder empfangsbereit ist.
static const uint32_t ACK_DELAY_MS = 400;
static char pendingAck[40];
static int pendingAckLen = 0;
static uint32_t pendingAckAt = 0;

static void sendPendingAck() {
  if (pendingAckLen == 0 || (int32_t)(millis() - pendingAckAt) < 0) return;
  sendLine(pendingAck, pendingAckLen);
  pendingAckLen = 0;
}

// Befehle von der Bodenstation (Nutzdaten ohne Header), z.B. "QNH=985.0"
static void handleCommand(const char *cmd) {
  if (strncmp(cmd, "QNH=", 4) == 0) {
    float hpa = atof(cmd + 4);
    if (hpa >= 900.0f && hpa <= 1100.0f) {
      sensorSetSeaLevel(hpa);
      Serial.printf("[CMD] QNH gesetzt: %.2f hPa\n", hpa);
      pendingAckLen = snprintf(pendingAck, sizeof(pendingAck), "ACK,QNH,%.2f\r\n", hpa);
      pendingAckAt = millis() + ACK_DELAY_MS;
    } else {
      Serial.printf("[CMD] QNH ungueltig: %s\n", cmd + 4);
    }
  } else {
    Serial.printf("[CMD] unbekannt: %s\n", cmd);
  }
}

static void doTx() {
  static uint32_t lastTx = 0;
  static uint32_t counter = 0;
  if (millis() - lastTx < TX_INTERVAL_MS) return;
  lastTx = millis();

  char text[96];
  int textLen;
  SensorData sd = sensorRead();
  if (sd.valid) {
    char hum[12] = "";
    if (!isnan(sd.humidity)) snprintf(hum, sizeof(hum), "%.1f", sd.humidity);
    textLen = snprintf(text, sizeof(text), "%lu,%lu,%.2f,%.2f,%s,%.1f\r\n",
                       (unsigned long)counter++, (unsigned long)millis(),
                       sd.tempC, sd.pressureHpa, hum, sd.altitudeM);
  } else {
    textLen = snprintf(text, sizeof(text), "%lu,%lu,NOSENSOR\r\n",
                       (unsigned long)counter++, (unsigned long)millis());
  }

  sendLine(text, textLen);
}

static void pollRx(bool verbose) {
  if (!irqFlag) return;
  irqFlag = false;

  uint8_t buf[256];
  size_t len = radio.getPacketLength();
  if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
  int state = radio.readData(buf, len);

  if (state == RADIOLIB_ERR_NONE) {
    if (verbose) {
      Serial.printf("[RX] %u Bytes, RSSI %.1f dBm, SNR %.1f dB\n",
                    (unsigned)len, radio.getRSSI(), radio.getSNR());
      Serial.print("     ASCII: "); printAscii(buf, len); Serial.println();
      Serial.print("     HEX:   "); printHex(buf, len);   Serial.println();
    }
    // Waveshare-Header entfernen, Zeilenende abschneiden, Befehl auswerten
    size_t off = 0;
    if (len >= sizeof(txHeader) && memcmp(buf, txHeader, sizeof(txHeader)) == 0) off = sizeof(txHeader);
    buf[len] = 0;
    char *cmd = (char *)buf + off;
    cmd[strcspn(cmd, "\r\n")] = 0;
    if (*cmd) handleCommand(cmd);
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    Serial.println("[RX] CRC-Fehler");
  } else {
    Serial.printf("[RX] Fehler %d\n", state);
  }
  radio.startReceive();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                          LORA_SYNC_WORD, LORA_POWER_DBM, LORA_PREAMBLE);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("RFM95 Init fehlgeschlagen, Code %d (Verkabelung pruefen!)\n", state);
    while (true) { delay(1000); }
  }
  radio.setDio0Action(onDio0, RISING);

  if (sensorBegin()) Serial.printf("Sensor gefunden: %s\n", sensorName());
  else Serial.println("WARNUNG: kein BME280/BMP280 gefunden (SDA 18, SCL 19, 3.3V pruefen)");

  Serial.printf("RFM95 bereit: %.1f MHz, BW %.0f kHz, SF%d, CR4/%d, Sync 0x%02X, %d dBm\n",
                LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD, LORA_POWER_DBM);
  Serial.printf("QNH fuer Hoehe: %.2f hPa\n", sensorGetSeaLevel());
  Serial.println("Befehle: 't' = senden, 'r' = empfangen");

  if (rxMode) startRx(); else startTxMode();
}

void loop() {
  handleSerialCommands();
  pollRx(rxMode);
  sendPendingAck();
  if (!rxMode) doTx();
}
