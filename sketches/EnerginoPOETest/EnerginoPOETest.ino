/*
 * EnerginoPOE Board Unit Test
 *
 * created 22 August 2014
 * by Roberto Riggio
 *
 * This code is released under the BSD Licence
 *
 */

#include <energino.h>
#include <sma.h>
#include <MemoryFree.h>

#define APIKEY        "foo"
#define FEEDID        0
#define FEEDURL      "https://192.168.9.80:5533/v2/feeds/"

#define RELAYPIN      4
#define CURRENTPIN    A2
#define VOLTAGEPIN    A1

#define GREENLED      3
#define YELLOWLED     2
#define REDLED        5

// Energino parameters
int R1 = 390;
int R2 = 100;
int OFFSET = 2500;
int SENSITIVITY = 185;
int PERIOD = 5000;

// magic string
const char MAGIC[] = "EnerginoPOE";
const int REVISION = 1;

// Moving averages
const int SMAPOINTS = 101;
SMA v_sma(SMAPOINTS);
SMA i_sma(SMAPOINTS);

// External AREF in mV
int AREF = 4096;

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
  strcpy (settings.apikey, APIKEY);
  settings.feedid = FEEDID;
  strcpy (settings.feedurl, FEEDURL);
}

void setup() {
  // Set serial port
  Serial.begin(115200);
  delay(5000);      
  // Resetting board
  Serial.println("Resetting board...");
  reset();
  saveSettings();
  // Set external reference
  Serial.println("Setting external reference...");
  analogReference(EXTERNAL);
  // Loading setting
  Serial.println("Loading settings...");
  loadSettings();
  // Default on
  Serial.println("Setting relay pin as output...");
  pinMode(settings.relaypin, OUTPUT);
  digitalWrite(settings.relaypin, LOW);
  // Set defaults
  Serial.println("Setting defaults...");
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  pinMode(GREENLED, OUTPUT);
  digitalWrite(GREENLED, HIGH);
  pinMode(YELLOWLED, OUTPUT);
  digitalWrite(YELLOWLED, HIGH);
  pinMode(REDLED, OUTPUT);
  digitalWrite(REDLED, HIGH);
  // Dump settings
  Serial.println("Dump settings...");
  dumpSettings();
}

void test_blink(int pin) {
  for (int i = 0; i < 3; i++) {
    digitalWrite(pin, LOW); 
    delay(1000);      
    digitalWrite(pin, HIGH); 
    delay(1000);   
  }
}

void test_switch() {
  digitalWrite(settings.relaypin, HIGH); 
  delay(2000);      
  digitalWrite(settings.relaypin, LOW); 
  delay(2000);   
  digitalWrite(settings.relaypin, HIGH); 
  delay(2000);      
  digitalWrite(settings.relaypin, LOW); 
  delay(2000);   
}

void tuneOffset() {

  long value = 0;
  long count = 10000;
  for(long i = 0; i < count; i++) {
    value += analogRead(settings.currentpin);
  }
  
  long v_out = (value * res(AREF)) / count;
  Serial.print("Offset set to (mV): ");
  Serial.println(v_out);
  settings.offset = v_out;

}

void error() {
 while (true) {
  digitalWrite(GREENLED, HIGH);
  digitalWrite(YELLOWLED, HIGH);
  digitalWrite(REDLED, HIGH);
  delay(500);
  digitalWrite(GREENLED, LOW);
  digitalWrite(YELLOWLED, LOW);
  digitalWrite(REDLED, LOW);
  delay(500);
 } 
}

void tuneSensitivity() {

  double known = 1;
  long value = 0;
  long count = 10000;
  for(long i = 0; i < count; i++) {
    value += analogRead(settings.currentpin);
  }
  
  long v_out = (value * res(AREF)) / count;
  
  double delta = double(v_out - settings.offset);

  long sensitivity = delta / known;

  Serial.print("Sensitivity set to (mV/A): ");
  Serial.println(sensitivity);
  settings.sensitivity = sensitivity;

}

void tuneDividerGain() {

  double known = 18000;
  long value = 0;
  long count = 10000;
  for(long i = 0; i < count; i++) {
    value += analogRead(settings.voltagepin);
  }
  
  long v_out = (value / count) * res(AREF);
  double gain = known / v_out;

  int new_r1 = gain * settings.r2 - settings.r2;

  Serial.print("Setting R1 to (Mohm): ");
  Serial.println(new_r1);
  settings.r1 = new_r1;
  
}

void loop() {
  Serial.println("************************************************");
  Serial.println("Testing green led, blinking 3 times...");
  test_blink(GREENLED);
  Serial.println("Testing yellow led, blinking 3 times...");
  test_blink(YELLOWLED);
  Serial.println("Testing red led, blinking 3 times...");
  test_blink(REDLED);
  Serial.println("************************************************");
  Serial.println("Testing switch...");
  test_switch();
  Serial.println("************************************************");
  Serial.println("Setting offset...");
  tuneOffset();
  Serial.println("************************************************");
  Serial.println("Setting voltage divider...");
  tuneDividerGain();
  Serial.println("************************************************");
  Serial.println("Configuration string:");
  Serial.print(settings.r1);
  Serial.print(",");
  Serial.print(settings.r2);
  Serial.print(",");
  Serial.print(settings.offset);
  Serial.print(",");
  Serial.println(settings.sensitivity);
  Serial.println("************************************************");
  saveSettings();
  Serial.println("Done!");
  delay(1000);
  digitalWrite(GREENLED, LOW);
  digitalWrite(YELLOWLED, LOW);
  digitalWrite(REDLED, LOW);
  delay(10000); 
}

