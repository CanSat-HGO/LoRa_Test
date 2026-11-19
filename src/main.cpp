#include <Arduino.h>
#include <RadioLib.h>
#include "config.h"

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
  radio.standby();
  Serial.println("[TX] Sendemodus aktiv");
}

static void handleSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' && !rxMode) startRx();
    else if (c == 't' && rxMode) startTxMode();
  }
}

static void doTx() {
  static uint32_t lastTx = 0;
  static uint32_t counter = 0;
  if (millis() - lastTx < TX_INTERVAL_MS) return;
  lastTx = millis();

  char text[64];
  int textLen = snprintf(text, sizeof(text), "Hallo vom Teensy #%lu\r\n",(unsigned long)counter++);

  uint8_t pkt[sizeof(txHeader) + sizeof(text)];
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
}

static void doRx() {
  if (!irqFlag) return;
  irqFlag = false;

  uint8_t buf[256];
  size_t len = radio.getPacketLength();
  if (len > sizeof(buf)) len = sizeof(buf);
  int state = radio.readData(buf, len);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("[RX] %u Bytes, RSSI %.1f dBm, SNR %.1f dB\n",
                  (unsigned)len, radio.getRSSI(), radio.getSNR());
    Serial.print("     ASCII: "); printAscii(buf, len); Serial.println();
    Serial.print("     HEX:   "); printHex(buf, len);   Serial.println();
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

  Serial.printf("RFM95 bereit: %.1f MHz, BW %.0f kHz, SF%d, CR4/%d, Sync 0x%02X, %d dBm\n",
                LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD, LORA_POWER_DBM);
  Serial.println("Befehle: 't' = senden, 'r' = empfangen");

  if (rxMode) startRx(); else startTxMode();
}

void loop() {
  handleSerialCommands();
  if (rxMode) doRx(); else doTx();
}
