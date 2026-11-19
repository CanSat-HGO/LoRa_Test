#include "sensor.h"
#include "config.h"
#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>

static Adafruit_BME280 bme;
static Adafruit_BMP280 bmp;
static enum { NONE, BME, BMP } type = NONE;
static uint8_t addr = 0;
static float seaLevelHpa = SEA_LEVEL_HPA;

static const uint32_t EE_MAGIC = 0x514E4831;  // "QNH1"
struct EeQnh { uint32_t magic; float hpa; };

static bool qnhPlausible(float h) { return h >= 900.0f && h <= 1100.0f; }

static void loadSeaLevel() {
  EeQnh e;
  EEPROM.get(0, e);
  if (e.magic == EE_MAGIC && qnhPlausible(e.hpa)) seaLevelHpa = e.hpa;
}

void sensorSetSeaLevel(float hpa) {
  if (!qnhPlausible(hpa) || hpa == seaLevelHpa) return;  // EEPROM nur bei Aenderung schreiben
  seaLevelHpa = hpa;
  EeQnh e{EE_MAGIC, hpa};
  EEPROM.put(0, e);
}

float sensorGetSeaLevel() { return seaLevelHpa; }

bool sensorBegin() {
  loadSeaLevel();
  Wire.begin();  // Teensy 4.1: SDA = 18, SCL = 19
  for (uint8_t a : {0x76, 0x77}) {
    if (bme.begin(a, &Wire)) { type = BME; addr = a; break; }
    if (bmp.begin(a))        { type = BMP; addr = a; break; }
  }
  if (type == BMP) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_63);
  }
  if (type == NONE) {
    Serial.print("I2C-Scan: ");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) { Serial.printf("0x%02X ", a); found++; }
    }
    Serial.println(found ? "" : "nichts gefunden");
  }
  return type != NONE;
}

const char *sensorName() {
  static char buf[24];
  if (type == NONE) return "kein Sensor";
  snprintf(buf, sizeof(buf), "%s @ 0x%02X", type == BME ? "BME280" : "BMP280", addr);
  return buf;
}

SensorData sensorRead() {
  SensorData d{false, NAN, NAN, NAN, NAN};
  if (type == BME) {
    d.tempC       = bme.readTemperature();
    d.pressureHpa = bme.readPressure() / 100.0f;
    d.humidity    = bme.readHumidity();
    d.altitudeM   = bme.readAltitude(seaLevelHpa);
  } else if (type == BMP) {
    d.tempC       = bmp.readTemperature();
    d.pressureHpa = bmp.readPressure() / 100.0f;
    d.altitudeM   = bmp.readAltitude(seaLevelHpa);
  }
  d.valid = type != NONE && !isnan(d.tempC) && d.pressureHpa > 300.0f;
  return d;
}
