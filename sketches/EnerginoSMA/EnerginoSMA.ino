/*
 * Energino
 *
 * Energino is a energy consumption meter for DC loads (moving average)
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
 * created 22 August 2014
 * by Roberto Riggio
 *
 * This code is released under the BSD Licence
 *
 */

#include <energino.h>
#include <sma.h>

#define RELAYPIN      2
#define CURRENTPIN    A0
#define VOLTAGEPIN    A1

// Energino parameters
int R1 = 100;
int R2 = 10;
int OFFSET = 2500;
int SENSITIVITY = 185;
int PERIOD = 2000;

// Moving averages
const int MAPOINTS = 101;

SMA v_sma(MAPOINTS);
SMA i_sma(MAPOINTS);

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
  int v = analogRead(VOLTAGEPIN);
  int i = analogRead(CURRENTPIN);
  v_sma.add(v);
  i_sma.add(i);
  if (lastUpdated + settings.period <= millis()) {
    // Conversion
    VFinal = v_sma.avg();
    IFinal = i_sma.avg();
    lastSamples = MAPOINTS;
    // dump to serial
    dumpToSerial();
    // reset counters
    lastUpdated = millis();
  }
}

