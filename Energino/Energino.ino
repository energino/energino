/*
 * EnerginoEthernet
 *
 * Energino is a energy consumption meter for DC loads
 *
 * Circuit:
 *  Analog inputs attached to pins A0 (Current), A1 (Voltage)
 *  Digital output attached to pin D3 (Relay)
 *
 * Supported commands from the serial:
 *  #P<integer>, sets the period between two updates (in ms) [default is 2000]
 *  #S<0/1>, sets the relay configuration, 0 load on, 1 load off [default is 0]
 *  #V<0/1>, samples voltage pin [default is 1]
 *  #I<0/1>, samples current pin [default is 1]
 *  #A<integer>, sets the value in ohms of the R1 resistor [default is 100000]
 *  #B<integer>, sets the value in ohms of the R2 resistor [default is 10000]
 *  #C<integer>, sets the offset in mV of the current sensor [default is 2500]
 *  #D<integer>, sets the sensitivity in mV of the current sensor [default is 185]
 *  #R, resest the configuration to the defaults
 *
 * Serial putput:
 *   #Energino,0,<voltage>,<current>,<power>,<switch>,<samples>,<period>
 *
 * created 24 May 2012
 * by Roberto Riggio
 *
 * This code is release under the BSD Licence
 *
 */
 
#include <avr/eeprom.h>

// comment/uncomment to enable debug
//#define DEBUG

#define RELAY_PIN 2

#ifdef DEBUG
#define DBG if(1) 
#else
#define DBG if(0) 
#endif

// Energino parameters
boolean readVoltage = true;
boolean readCurrent = true;
long R1 = 100000;
long R2 = 10000;
float OFFSET = 2.5;
float SENSITIVITY = 0.185;
long VRaw;
long IRaw;
int targetSleep = 1000;
long sleep = targetSleep / 1000 * 5000;

// magic string
const char MAGIC[] = "Energino";
const int REVISION = 0;

// Permanent configuration
struct settings_t {
  char magic[17];
  int revision;
  long r1;
  long r2;
  float sensitivity;
  float offset;
} 
settings;

void reset() {
    DBG Serial.println("Writing defaults");
    strcpy (settings.magic,MAGIC);
    settings.r1 = R1;
    settings.r2 = R2;
    settings.sensitivity = SENSITIVITY;
    settings.offset = OFFSET;
}

void setup() {
  // Default on
  pinMode(RELAY_PIN,OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(13,OUTPUT);
  digitalWrite(13, LOW);
  // configuring serial
  Serial.begin(115200);
  // Loading setting 
  DBG Serial.println("Loading settings");
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settings));
  if (strcmp(settings.magic, MAGIC) != 0) {
    DBG Serial.println("Revision mismatch");
    reset();
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings));
  }
  DBG Serial.print("R1: ");
  DBG Serial.println(settings.r1);
  DBG Serial.print("R2: ");
  DBG Serial.println(settings.r2);
  DBG Serial.print("Offset: ");
  DBG Serial.println(settings.offset,3);
  DBG Serial.print("Sensitivity: ");
  DBG Serial.println(settings.sensitivity,3);
}

void serParseCommand()
{
  char cmd = '\0';
  long value;
  int i, serAva;
  char inputBytes[12];
  char * inputBytesPtr = &inputBytes[0];
  if (Serial.available() > 0) {
    delay(5);
    serAva = Serial.available();
    for (i=0; i<serAva; i++) {
      char chr = Serial.read();
      if (i == 0) {
        if (chr != '#') {
          return;
        } 
        else {
          continue;
        }
      }  
      if (i == 1)
        cmd = chr;
      else
        inputBytes[i-2] = chr;
    }
    inputBytes[i] = '\0';
    value = atol(inputBytesPtr);
  } 
  if (cmd == 'P' && value > 0) {
    resetSleep(value);
  } else if (cmd == 'S' && value > 0) {
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(13, HIGH);
  } else if (cmd == 'S' && value == 0) {
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(13, LOW);
  } else if (cmd == 'V' && value > 0) {
    resetSleep(targetSleep);
    readVoltage = true;
  } else if (cmd == 'V' && value == 0) {
    resetSleep(targetSleep);
    readVoltage = false;
  } else if (cmd == 'I' && value > 0) {
    resetSleep(targetSleep);
    readCurrent = true;
  } else if (cmd == 'I' && value == 0) {
    resetSleep(targetSleep);
    readCurrent = false;
  } else if (cmd == 'A' && value > 0) {
    settings.r1 = value;
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings)); 
  } else if (cmd == 'B' && value > 0) {
    settings.r2 = value;
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings)); 
  } else if (cmd == 'C' && value > 0) {
    settings.offset = (float) value / 1000;
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings)); 
  } else if (cmd == 'D' && value > 0) {
    settings.sensitivity = (float) value / 1000;
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings)); 
  } else if (cmd == 'R') {
    reset();
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings)); 
  } 
}

float scaleVoltage(float VRaw) {
  return ( VRaw * 5 * (settings.r1 + settings.r2)) / ( settings.r2 * 1024 );
}

float scaleCurrent(float IRaw) {
  return ( IRaw * 5 / ( 1024 ) - settings.offset) / settings.sensitivity;
}

void resetSleep(long value) {
    sleep = value / 1000 * 5000;
    targetSleep = value;
}

void loop() {

  // Start profiling
  long started = millis();

  // Parse incoming commands
  serParseCommand();

  // Instant values are too floating,
  // let's smooth them up
  VRaw = 0;
  IRaw = 0;

  for(long i = 0; i < sleep; i++) {
    if (readVoltage) 
      VRaw += analogRead(A1);
    if (readCurrent) 
      IRaw += analogRead(A0);
    else
      IRaw += 512;
  }

  // Conversion
  float VFinal = scaleVoltage((float)VRaw/sleep);
  float IFinal = scaleCurrent((float)IRaw/sleep);

  // Control loop
  long delta = abs(millis() - started);
  sleep -= 5 * (delta - targetSleep);
  if (sleep < 5) {
    sleep = 5;
  }

  // Output
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
  Serial.print(digitalRead(RELAY_PIN));
  Serial.print(",");
  Serial.print(sleep);
  Serial.print(",");
  Serial.print(delta);
  Serial.print("\n");

}
