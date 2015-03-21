/*
 * energino.h - Library for Energino devices
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
unsigned int lastSamples = 0;

// digits to be printed on the serial
int V_DIGITS = 3;
int C_DIGITS = 3;
int P_DIGITS = 2;

// Last update
unsigned long lastUpdated;

// Permanent configuration
struct settings_t {
  char magic[12];
  int revision;
  int period;
  int r1;
  int r2;
  int offset;
  int sensitivity;
  int relaypin;
  int currentpin;
  int voltagepin;
  char apikey[49];
  unsigned long feedid;
  char feedurl[60];
} settings;

void reset();

void factoryCheck();

double res(int aref) {
  return aref / 1024.0;
}

// max voltage quantization error in mV
int getVError(int aref) {
  return (res(aref) * (settings.r1 + settings.r2)) / settings.r2;
}

int getVError() {
  return getVError(DEFAULT_AREF);
}

// max current quantization error in mA
int getIError(int aref) {
  return (res(aref) / settings.sensitivity) * 1000.0;
}

int getIError() {
  return getIError(DEFAULT_AREF);
}

// covert 10-bits DAC reading into V
double getAvgVoltage(double value, int aref) {
  double v_out = value * res(aref);
  double output = (v_out * double(settings.r1 + settings.r2)) / settings.r2;
  return (output > 0) ? output / 1000.0 : 0;
}

double getAvgVoltage(double value) {
  return getAvgVoltage(value, DEFAULT_AREF);
}

// covert 10-bits DAC reading into A
double getAvgCurrent(double value, int aref) {
  double v_out = value * res(aref);
  double output = double(v_out - settings.offset) / settings.sensitivity;
  return (output > 0) ? output : 0;
}

double getAvgCurrent(double value) {
  return getAvgCurrent(value, DEFAULT_AREF);
}

// covert 10-bits DAC reading into W
double getAvgPower(double voltage, double current, int aref) {
  return getAvgVoltage(voltage, aref) * getAvgCurrent(current, aref);
}

double getAvgPower(double voltage, double current) {
  return getAvgPower(voltage, current, DEFAULT_AREF);
}

// save/read settings to eeprom
void saveSettings() {
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings));
}

void loadSettings() {
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settings));
}

// dump settings to serial
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
  Serial.print("@apikey: ");
  Serial.println(settings.apikey);
  Serial.print("@feedid: ");
  Serial.println(settings.feedid);
  Serial.print("@feedurl: ");
  Serial.println(settings.feedurl);
}

// parse serial CLI command
void serParseCommand(int aref)
{
  // if serial is not available there is no point in continuing
  if (!Serial.available()) {
    return;
  }
  // working vars
  char cmd = '\0';
  char buf[60] = {'\0'};
  char valueBuf[58] = {'\0'};
  char * valueBufPtr = &valueBuf[0];
  int i;
  // read command from serial
  String str = Serial.readStringUntil('\n');
  str.toCharArray(buf, 60);
  // parse incoming string char by char
  for (i = 0; i < 58; i++) {
    char chr = buf[i];
    if (chr == '\0') {
      break;
    }
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
      valueBuf[i-2] = chr;
    }
  }
  // null-terminate input buffer
  valueBuf[i] = '\0';
  // execute command
  if (cmd == 'R') {
    Serial.println("@reset");
    reset();
  }
  // execute command
  if (cmd == 'H') {
    Serial.println("@Factory check");
    factoryCheck();
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
    long value = atol(valueBufPtr);
    if (value >= 0) {
      settings.feedid = value;
    }
  }
  else if (cmd == 'K') {
    strncpy(settings.apikey, valueBuf,49);
    settings.apikey[48] = '\0';
  }
  else if (cmd == 'U') {
    strncpy(settings.feedurl, valueBuf,60);
    settings.feedurl[59] = '\0';
  }
  else if (cmd == 'P') {
    int value = atoi(valueBufPtr);
    if (value < 0) {
      return;
    }
    settings.period = value;
    Serial.print("@period: ");
    Serial.print(settings.period);
    Serial.println("ms");
  }
  else if (cmd == 'A') {
    int value = atoi(valueBufPtr);
    if (value < 0) {
      return;
    }
    settings.r1 = value;
    Serial.print("R1: ");
    Serial.print(settings.r1);
    Serial.println(" Kohm");
  }
  else if (cmd == 'B') {
    int value = atoi(valueBufPtr);
    if (value < 0) {
      return;
    }
    settings.r2 = value;
    Serial.print("R2: ");
    Serial.print(settings.r2);
    Serial.println(" Kohm");
  }
  else if (cmd == 'C') {
    int value = atoi(valueBufPtr);
    if (value < 0) {
      return;
    }
    settings.offset = value;
    Serial.print("Offeset: ");
    Serial.print(settings.offset);
    Serial.println(" mV");
  }
  else if (cmd == 'D') {
    int value = atoi(valueBufPtr);
    if (value < 0) {
      return;
    }
    settings.sensitivity = value;
    Serial.print("Sensitivity: ");
    Serial.print(settings.sensitivity);
    Serial.println(" mV/A");
  }
  else if (cmd == 'S') {
    int value = atoi(valueBufPtr);
    if (value < 0) {
      return;
    }
    if (value > 0) {
      Serial.println("@switch: high");
      digitalWrite(settings.relaypin, HIGH);
    }
    else {
      Serial.println("@switch: low");
      digitalWrite(settings.relaypin, LOW);
    }
  }
  saveSettings();
}

void serParseCommand() {
  serParseCommand(DEFAULT_AREF);
}

// dump current readings to serial
void dumpToSerial(int aref) {
  // Print data also on the serial
  Serial.print("#");
  Serial.print(settings.magic);
  Serial.print(",");
  Serial.print(settings.revision);
  Serial.print(",");
  Serial.print(getAvgVoltage(VFinal, aref), V_DIGITS);
  Serial.print(",");
  Serial.print(getAvgCurrent(IFinal, aref), C_DIGITS);
  Serial.print(",");
  Serial.print(getAvgPower(VFinal, IFinal, aref), P_DIGITS);
  Serial.print(",");
  Serial.print(digitalRead(settings.relaypin));
  Serial.print(",");
  Serial.print(settings.period);
  Serial.print(",");
  Serial.print(lastSamples);
  Serial.print(",");
  Serial.print(getVError(aref));
  Serial.print(",");
  Serial.print(getIError(aref));
  Serial.print("\n");
}

void dumpToSerial() {
  dumpToSerial(DEFAULT_AREF);
}

#endif
