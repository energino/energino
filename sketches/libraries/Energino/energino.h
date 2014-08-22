/*
  energino.h - Library for Energino devices
  */

#ifndef ENERGINO_H
#define ENERGINO_H

#include "Arduino.h"
#include <avr/eeprom.h>

// default reference voltage
int DEFAULT_AREF = 5000;

// Last computed values
double VFinal = 0.0;
double IFinal = 0.0;
long lastSamples = 0;

// Last update
long lastUpdated;

// Permanent configuration
struct settings_t {
  char magic[12];
  int revision;
  long period;
  int r1;
  int r2;
  int offset;
  int sensitivity;
  int relaypin;
  int currentpin;
  int voltagepin;
  char apikey[49];
  long feedid;
  char feedsurl[60];
} settings;

void reset();

void dumpToSerial();

void setPeriod(long value);

double res(int aref) {
  return aref / 1024.0;
}

long getVError(int aref) {
  return (res(aref) * (settings.r1 + settings.r2)) / settings.r2;
}

long getVError() {
  return getVError(DEFAULT_AREF);
}

long getIError(int aref) {
  return (res(aref) / settings.sensitivity) * 1000.0;
}

long getIError() {
  return getIError(DEFAULT_AREF);
}

double getAvgVoltage(double value, int aref) {
  long v_out = value * res(aref);
  long output = (v_out * double(settings.r1 + settings.r2)) / settings.r2;
  return (output > 0) ? output / 1000.0 : 0;
}

double getAvgVoltage(double value) {
  return getAvgVoltage(value, DEFAULT_AREF);
}

double getAvgCurrent(double value, int aref) {
  long v_out = value * res(aref);
  double output = double(v_out - settings.offset) / settings.sensitivity;
  return (output > 0) ? output : 0;
}

double getAvgCurrent(double value) {
  return getAvgCurrent(value, DEFAULT_AREF);
}

double getAvgPower(double voltage, double current, long aref) {
  return getAvgVoltage(voltage, aref) * getAvgCurrent(current, aref);
}

double getAvgPower(double voltage, double current) {
  return getAvgPower(voltage, current, DEFAULT_AREF);
}

void saveSettings() {
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings));
}

void loadSettings() {
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settings));
}

void dumpSettings() {
  Serial.print("@magic: ");
  Serial.println(settings.magic);
  Serial.print("@revision: ");
  Serial.println(settings.revision);
  Serial.print("@period: ");
  Serial.print(settings.period);
  Serial.println(" ms");
  Serial.print("@r1: ");
  Serial.print(settings.r1);
  Serial.println(" Kohm");
  Serial.print("@r2: ");
  Serial.print(settings.r2);
  Serial.println(" Kohm");
  Serial.print("@offset: ");
  Serial.print(settings.offset);
  Serial.println(" mV");
  Serial.print("@sensitivity: ");
  Serial.print(settings.sensitivity);
  Serial.println(" mV/A");
  Serial.print("@relaypin: ");
  Serial.println(settings.relaypin);
  Serial.print("@currentpin: ");
  Serial.println(settings.currentpin);
  Serial.print("@voltagepin: ");
  Serial.println(settings.voltagepin);
}

void serParseCommand(int aref)
{
  // if serial is not available there is no point in continuing
  if (!Serial.available()) {
    return;
  }
  // working vars
  char cmd = '\0';
  int i, serAva;
  char inputBytes[60] = {'\0'};
  char * inputBytesPtr = &inputBytes[0];
  // read command from serial
  serAva = Serial.available();
  for (i = 0; i < serAva; i++) {
    char chr = Serial.read();
    if (i == 0) {
      if (chr != '#') {
        return;
      }
      else {
        continue;
      }
    }
    else if (i == 1) {
      cmd = chr;
    }
    else{
      inputBytes[i-2] = chr;
    }
  }
  // null-terminate input buffer
  inputBytes[i] = '\0';
  // execute command
  if (cmd == 'R') {
    Serial.println("@reset");
    reset();
  }
  else if (cmd == 'Z') {
    dumpSettings();
  }
  else if (cmd == 'T') {
    long value = 0;
    for(long i = 0; i < 1000; i++) {
      value += analogRead(settings.currentpin);
    }
    long v_out = (value * res(aref)) / 1000;
    Serial.print("@offset: ");
    Serial.println(v_out);
    settings.offset = v_out;
  }
  else if (cmd == 'F') {
    long value = atol(inputBytesPtr);
    if (value >= 0) {
      settings.feedid = value;
    }
  }
  else if (cmd == 'K') {
    strncpy(settings.apikey, inputBytes,49);
    settings.apikey[48] = '\0';
  }
  else if (cmd == 'U') {
    strncpy(settings.feedsurl, inputBytes,60);
    settings.feedsurl[59] = '\0';
  }
  else {
    int value = atoi(inputBytesPtr);
    if (value < 0) {
      return;
    }
    if (cmd == 'P') {
      settings.period = value;
      Serial.print("@period: ");
      Serial.print(settings.period);
      Serial.println("ms");
    }
    else if (cmd == 'A') {
      settings.r1 = value;
      Serial.print("R1: ");
      Serial.print(settings.r1);
      Serial.println(" Kohm");
    }
    else if (cmd == 'B') {
      settings.r2 = value;
      Serial.print("R2: ");
      Serial.print(settings.r2);
      Serial.println(" Kohm");
    }
    else if (cmd == 'C') {
      settings.offset = value;
      Serial.print("Offeset: ");
      Serial.print(settings.offset);
      Serial.println(" mV");
    }
    else if (cmd == 'D') {
      settings.sensitivity = value;
      Serial.print("Sensitivity: ");
      Serial.print(settings.sensitivity);
      Serial.println(" mV/A");
    }
    else if (cmd == 'S') {
      if (value > 0) {
        Serial.println("@switch: high");
        digitalWrite(settings.relaypin, HIGH);
      }
      else {
        Serial.println("@switch: low");
        digitalWrite(settings.relaypin, LOW);
      }
    }
  }
  saveSettings();
}

void serParseCommand() {
  serParseCommand(DEFAULT_AREF);
}

void dumpToSerial() {
  // Print data also on the serial
  Serial.print("#");
  Serial.print(settings.magic);
  Serial.print(",");
  Serial.print(settings.revision);
  Serial.print(",");
  Serial.print(getAvgVoltage(VFinal), 2);
  Serial.print(",");
  Serial.print(getAvgCurrent(IFinal), 2);
  Serial.print(",");
  Serial.print(getAvgPower(VFinal, IFinal), 1);
  Serial.print(",");
  Serial.print(digitalRead(settings.relaypin));
  Serial.print(",");
  Serial.print(settings.period);
  Serial.print(",");
  Serial.print(lastSamples);
  Serial.print(",");
  Serial.print(getVError());
  Serial.print(",");
  Serial.print(getIError());
  Serial.print("\n");
}

void init(const char * magic) {
  // Set serial port
  Serial.begin(115200);
  // Loading setting
  loadSettings();
  if (strcmp(settings.magic, magic) != 0) {
    reset();
    saveSettings();
  }
  // Default on
  pinMode(settings.relaypin,OUTPUT);
  digitalWrite(settings.relaypin, LOW);
  // Use the led 13 to notify that the
  // setup completed
  pinMode(13,OUTPUT);
  digitalWrite(13, HIGH);
  // Set last update to now
  lastUpdated = millis();
}

#endif
