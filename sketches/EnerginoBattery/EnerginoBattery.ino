/*
 * Energino
 *
 * Energino is a energy consumption meter for DC loads (win average).
 *
 * Circuit:
 *  Analog inputs attached to pins A0 (Current), A1 (Voltage)
 *  Digital output attached to pin D2 (Relay)
 *
 * Supported commands from the serial:
 *  #P<integer>, sets the period between two updates (in ms) [default is 2000]
 *  #S<0/1>, sets the relay configuration, 0 load on, 1 load off [default is 0]
 *  #A<integer>, sets the value in ohms of the R1 resistor [default is 100000]
 *  #B<integer>, sets the value in ohms of the R2 resistor [default is 10000]
 *  #C<integer>, sets the current sensor offset in mV [default is 2500]
 *  #D<integer>, sets the current sensor sensitivity in mV [default is 185]
 *  #R, reset the configuration to the defaults
 *  #T, self-tune the current sensor offset (use with no load attached)
 *  #Z, print settings
 *
 * Serial putput:
 *   #Energino,0,<voltage>,<current>,<power>,<relay>,<period>,<samples>,<voltage_error>,<current_error>
 *
 * created 31 October 2012
 * by Roberto Riggio
 *
 * This code is released under the BSD Licence
 *
 */

#include <energino.h>

#define RELAYPIN      2
#define CURRENTPIN    A0
#define VOLTAGEPIN    A1

// Energino parameters
int R1 = 351;
int R2 = 159;
int OFFSET = 2522;
int SENSITIVITY = 68;
int PERIOD = 2000;

// Running averages
long VRaw = 0;
long IRaw = 0;
long samples = 0;

// magic string
const char MAGIC[] = "Energino";
const int REVISION = 1;

void reset() {
  strcpy (settings.magic, MAGIC);
  settings.revision = REVISION;
  settings.period = PERIOD;
  settings.r1 = R1;
  settings.r2 = R2;
  settings.offset = OFFSET;
  settings.sensitivity = SENSITIVITY;
  settings.relaypin = RELAYPIN;
  settings.currentpin = CURRENTPIN;
  settings.voltagepin = VOLTAGEPIN;
}

const int SIZE = 60;
const double DISCARGE[SIZE] = { 11.80955, 11.73545, 11.68159, 11.63941, 11.60351, 11.57391, 11.54949, 11.52911, 11.50617, 11.4875, 11.46275, 11.43834, 11.41059, 11.38791, 11.36826, 11.34609, 11.3312, 11.31662, 11.29723, 11.27976, 11.2605, 11.23614, 11.21343, 11.18729, 11.16382, 11.13759, 11.11293, 11.08473, 11.05694, 11.02778, 11.00032, 10.96197, 10.94706, 10.91152, 10.88357, 10.85808, 10.82778, 10.79685, 10.76562, 10.73019, 10.69061, 10.64628, 10.58518, 10.61087, 10.53965, 10.47965, 10.41535, 10.34984, 10.30516, 10.23873, 10.17664, 10.11481, 10.04997, 9.98091, 9.90735, 9.81934, 9.73173, 9.53416, 9.07717, 8.5747 };

void setup() {
  // Set serial port
  Serial.begin(115200);
  // Loading setting
  loadSettings();
  if (strcmp(settings.magic, MAGIC) != 0) {
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

void loop() {
  // Parse incoming commands
  serParseCommand();
  // accumulate readings
  VRaw += analogRead(VOLTAGEPIN);
  IRaw += analogRead(CURRENTPIN);
  samples++;
  if (lastUpdated + settings.period <= millis()) {
    // Conversion
    VFinal = double(VRaw) / samples;
    IFinal = double(IRaw) / samples;
    lastSamples = samples;
    // dump to serial
    dumpToSerialBatt();
    // reset counters
    VRaw = 0;
    IRaw = 0;
    samples = 0;
    lastUpdated = millis();
  }
}

// coumpute battery percentage
double getBattery(double VFinal) {
    int i;
    for (i = 0; i < SIZE; i++) {
      if (DISCARGE[i] < getAvgVoltage(VFinal)) {
          break;
      }
    }
    if (i > 0) {
      return 1.0 - (i - 1.0) / SIZE;
    }
    return 1.0;
}

// dump current readings to serial
void dumpToSerialBatt() {
  // Print data also on the serial
  Serial.print("#");
  Serial.print(settings.magic);
  Serial.print(",");
  Serial.print(settings.revision);
  Serial.print(",");
  Serial.print(getAvgVoltage(VFinal), V_DIGITS);
  Serial.print(",");
  Serial.print(getAvgCurrent(IFinal), C_DIGITS);
  Serial.print(",");
  Serial.print(getAvgPower(VFinal, IFinal), P_DIGITS);
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
  Serial.print(",");
  Serial.print(getBattery(VFinal), 2);
  Serial.print("\n");
}

