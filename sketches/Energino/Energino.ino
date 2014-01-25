/*
 * Energino
 *
 * Energino is a energy consumption meter for DC loads
 *
 * This sketch connects an Arduino equipped with the Energino 
 * shield and with an Ethernet shield with a web service 
 * implementing the COSM (formely Pachube) REST API. It also
 * implements a serial interface.
 *
 * Circuit:
 *  Analog inputs attached to pins A0 (Current), A1 (Voltage)
 *  Digital output attached to pin D3 (Relay)
 *
 * Supported commands from the serial:
 *  #P<integer>, sets the period between two updates (in ms) [default is 2000]
 *  #S<0/1>, sets the relay configuration, 0 load on, 1 load off [default is 0]
 *  #A<integer>, sets the value in ohms of the R1 resistor [default is 100000]
 *  #B<integer>, sets the value in ohms of the R2 resistor [default is 10000]
 *  #C<integer>, sets the offset in mV of the current sensor [default is 2500]
 *  #D<integer>, sets the sensitivity in mV of the current sensor [default is 185]
 *  #R, resest the configuration to the defaults
 *
 * Serial putput:
 *   #Energino,0,<voltage>,<current>,<power>,<relay>,<period>,<samples>
 *
 * created 31 October 2012
 * by Roberto Riggio
 *
 * This code is released under the BSD Licence
 *
 */

#include <EEPROM.h>
#include <avr/eeprom.h>

#define RELAYPIN      2
#define CURRENTPIN    A0
#define VOLTAGEPIN    A1

// Energino parameters
int R1 = 100;
int R2 = 10;
int OFFSET = 2500;
int SENSITIVITY = 185;
int PERIOD = 2000;

// Control loop paramters
long sleep = 0;
long delta = 0;

// Last computed values
float VFinal = 0.0;
float IFinal = 0.0;

// magic string
const char MAGIC[] = "Energino";
const int REVISION = 1;

// Permanent configuration
struct settings_t {
  char magic[9];
  int r1;
  int r2;
  int offset;
  int sensitivity;
  int period;
} 
settings;

void reset() {
  strcpy (settings.magic,MAGIC);
  settings.period = PERIOD;
  settings.r1 = R1;
  settings.r2 = R2;
  settings.offset = OFFSET;
  settings.sensitivity = SENSITIVITY;
}

void setup() {
  // Set serial port
  Serial.begin(115200); 
  // Default on
  pinMode(RELAYPIN,OUTPUT);
  digitalWrite(RELAYPIN, LOW);
  // Loading setting 
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settings));
  if (strcmp(settings.magic, MAGIC) != 0) {
    reset();
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings));
  }
  // set sleep counter
  resetSleep(settings.period);
}

void loop() {

  // Start profiling
  long started = millis();

  // Parse incoming commands
  serParseCommand();

  // Instant values are too floating,
  // let's smooth them up
  long VRaw = 0;
  long IRaw = 0;

  for(long i = 0; i < sleep; i++) {
    VRaw += analogRead(VOLTAGEPIN);
    IRaw += analogRead(CURRENTPIN);
  }

  // Conversion
  VFinal = scaleVoltage((float)VRaw/sleep);
  IFinal = scaleCurrent((float)IRaw/sleep);

  // dump to serial
  dumpToSerial();

  // profiling
  delta = abs(millis() - started);

  // Control loop
  sleep -= 5 * (delta - settings.period);
  if (sleep < 5) {
    sleep = 5;
  }

}

void serParseCommand()
{
  // if serial is not available there is no point in continuing
  if (!Serial.available()) {
    return;
  }
  // working vars
  char cmd = '\0';
  int i, serAva;
  char inputBytes[60] = { 
    '\0'           };
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
    reset();
  }
  else {
    int value = atoi(inputBytesPtr);
    if (value < 0) {
      return;
    }
    if (cmd == 'P') {
      resetSleep(value);
    } 
    else if (cmd == 'A') {
      settings.r1 = value;
    } 
    else if (cmd == 'B') {
      settings.r2 = value;
    } 
    else if (cmd == 'C') {
      settings.offset = value;
    } 
    else if (cmd == 'D') {
      settings.sensitivity = value;
    }
    else if (cmd == 'S') {
      if (value > 0) {
        digitalWrite(RELAYPIN, HIGH);
      } 
      else {
        digitalWrite(RELAYPIN, LOW);
      }
    } 
  }
  eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings)); 
}

void dumpToSerial() {
  // Print data also on the serial
  Serial.print("#");
  Serial.print(MAGIC);
  Serial.print(",");
  Serial.print(REVISION);
  Serial.print(",");
  Serial.print(VFinal,3);
  Serial.print(",");
  Serial.print(IFinal,3);
  Serial.print(",");
  Serial.print(VFinal * IFinal,3);
  Serial.print(",");
  Serial.print(digitalRead(RELAYPIN));
  Serial.print(",");
  Serial.print(delta);
  Serial.print(",");
  Serial.print(sleep);
  Serial.print("\n");  
}

float scaleVoltage(float voltage) {
  return ( voltage * 5 * (settings.r1 + settings.r2)) / ( settings.r2 * 1024 );
}

float scaleCurrent(float current) {
  return ( current * 5000 / 1024  - settings.offset) / settings.sensitivity;
}

void resetSleep(long value) {
  sleep = (value * 4400) / 1000;
  settings.period = value;
}
