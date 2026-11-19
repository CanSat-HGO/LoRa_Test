#pragma once
#include <Arduino.h>

struct SensorData {
  bool  valid;
  float tempC;
  float pressureHpa;
  float humidity;   // NAN beim BMP280 (kein Feuchtesensor)
  float altitudeM;
};

// Sucht BME280 bzw. BMP280 auf 0x76/0x77. Gibt true zurueck, wenn gefunden.
bool sensorBegin();
SensorData sensorRead();
const char *sensorName();

// Luftdruck auf Meereshoehe (QNH, "relativer Luftdruck NHN") fuer die Hoehe.
// Wird im EEPROM gespeichert und ueberlebt Neustarts.
void  sensorSetSeaLevel(float hpa);
float sensorGetSeaLevel();
