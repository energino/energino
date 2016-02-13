/*
 * EnerginoPOE
 *
 * This sketch is made for the Arduino Yun based POE Injector.
 *
 * Circuit:
 *  Analog inputs attached to pins A2 (Current), A1 (Voltage)
 *  Digital output attached to pin D4 (Relay)
 *  Green led attached to pin D3
 *  Yellow led attached to pin D2
 *  Red led attached to pin D5
 *
 * Supported commands from the serial:
 *  #P<integer>, sets the period between two updates (in ms) [default is 2000]
 *  #S<0/1>, sets the relay configuration, 0 load on, 1 load off [default is 0]
 *  #A<integer>, sets the value in ohms of the R1 resistor [default is 100000]
 *  #B<integer>, sets the value in ohms of the R2 resistor [default is 10000]
 *  #C<integer>, sets the current sensor offset in mV [default is 2500]
 *  #D<integer>, sets the current sensor sensitivity in mV [default is 185]
 *  #R, reset the configuration to the defaults
*   #H, run factory test for Energino, do not connect any load while running
 *  #T, self-tune the current sensor offset (use with no load attached)
 *  #Z, print settings
 *  #F<feed>, sets the feed id [default is 0]
 *  #K<key>, sets the Xively authentication key [default is -]
 *  #U<url>, sets the Xively URL [default is https://api.xively.com/v2/feeds/]
 *
 * No updates are sent over the Ethernet interface if the feed id is set to 0
 *
 * Serial putput:
 *   #EnerginoPOE,0,<voltage>,<current>,<power>,<relay>,<period>,<samples>,
 *   <voltage_error>,<current_error>,<feed>,<url>,<key>
 *
 * RESTful interface:
 *   Reading:
 *     GET http://<ipaddress>/arduino/datastreams
 *     GET http://<ipaddress>/arduino/datastreams/voltage
 *     GET http://<ipaddress>/arduino/datastreams/current
 *     GET http://<ipaddress>/arduino/datastreams/power
 *     GET http://<ipaddress>/arduino/datastreams/switch
 *   Switching on:
 *     GET http://<ipaddress>/arduino/datastreams/switch/0
 *   Switching off:
 *     GET http://<ipaddress>/arduino/datastreams/switch/1
 *
 * created 22 August 2014
 * by Roberto Riggio
 *
 * This code is released under the BSD Licence
 *
 */

#include <Bridge.h>
#include <YunServer.h>
#include <YunClient.h>
#include <energino.h>
#include <energinolive.h>
#include <sma.h>

#define APIKEY        "foo"
#define FEEDID        0
#define FEEDURL      "http://192.168.100.158:5533/v2/feeds/"
//#define FEEDURL      "https://api.xively.com/v2/feeds/"

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
const int SMAPOINTS = 21;
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

// Instantiate a server enabling the the Yun to listen for connected clients.
YunServer server;

void setup() {
  // Set serial port
  Serial.begin(115200);
  // Set external reference
  analogReference(EXTERNAL);
  // Loading setting
  loadSettings();
  if (strcmp(settings.magic, MAGIC) != 0) {
    reset();
    saveSettings();
  }
  // Default on
  pinMode(settings.relaypin, OUTPUT);
  digitalWrite(settings.relaypin, LOW);
  // Init bridge library
  Bridge.begin();
  // listen on port
  server.listenOnLocalhost();
  server.begin();
  // Use the led 13 and GREEN to notify that the
  // setup completed
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  pinMode(GREENLED, OUTPUT);
  digitalWrite(GREENLED, LOW);
  // Set defaults
  pinMode(YELLOWLED, OUTPUT);
  digitalWrite(YELLOWLED, HIGH);
  pinMode(REDLED, OUTPUT);
  digitalWrite(REDLED, HIGH);
  // Set last update to now
  lastUpdated = millis();
}

void factoryCheck() {

  // Resetting board
  Serial.println("Resetting board...");
  reset();
  saveSettings();

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

  Serial.println("************************************************");
  Serial.println("Testing green led, blinking 3 times...");
  testBlink(GREENLED);
  Serial.println("Testing yellow led, blinking 3 times...");
  testBlink(YELLOWLED);
  Serial.println("Testing red led, blinking 3 times...");
  testBlink(REDLED);
  Serial.println("************************************************");
  Serial.println("Testing switch...");
  testSwitch();
  Serial.println("************************************************");
  Serial.println("Setting offset...");
  tuneOffset(AREF);
  Serial.println("************************************************");
  Serial.println("Setting voltage divider...");
  tuneDividerGain(AREF);
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

  // Set green led back on
  pinMode(GREENLED, OUTPUT);
  digitalWrite(GREENLED, LOW);

}

void loop() {
  // Make sure that update period is not too high
  // when pushing data to Xively
  if ((settings.feedid != 0) && (settings.period < PERIOD)) {
    settings.period = PERIOD;
  }
  // Get clients coming from server
  YunClient client = server.accept();
  // There is a new client?
  if (client) {
    // Process request
    process(client);
    // Close connection and free resources.
    client.stop();
  }
  // Parse incoming commands
  serParseCommand(AREF);
  // if feed is set, the turn yellow led on
  if (settings.feedid > 0)
    digitalWrite(YELLOWLED, LOW);
  else
    digitalWrite(YELLOWLED, HIGH);
  // set poe led
  if (digitalRead(settings.relaypin) == 0)
    digitalWrite(REDLED, LOW);
  else
    digitalWrite(REDLED, HIGH);
  // Instant values are too floating,
  // let's smooth them up
  int v = analogRead(settings.voltagepin);
  int i = analogRead(settings.currentpin);
  v_sma.add(v);
  i_sma.add(i);
  if (lastUpdated + settings.period <= millis()) {
    // Conversion
    VFinal = v_sma.avg();
    IFinal = i_sma.avg();
    lastSamples = SMAPOINTS;
    // dump to serial
    dumpToSerial(AREF);
    // send data to remote host
    sendData(AREF);
    // set last update
    lastUpdated = millis();
  }

}

void testBlink(int pin) {
  for (int i = 0; i < 3; i++) {
    digitalWrite(pin, LOW);
    delay(1000);
    digitalWrite(pin, HIGH);
    delay(1000);
  }
}

void testSwitch() {
  digitalWrite(settings.relaypin, HIGH);
  delay(2000);
  digitalWrite(settings.relaypin, LOW);
  delay(2000);
  digitalWrite(settings.relaypin, HIGH);
  delay(2000);
  digitalWrite(settings.relaypin, LOW);
  delay(2000);
}

void tuneOffset(int aref) {

  long value = 0;
  long count = 10000;
  for(long i = 0; i < count; i++) {
    value += analogRead(settings.currentpin);
  }

  long v_out = (value * res(aref)) / count;
  Serial.print("Offset set to (mV): ");
  Serial.println(v_out);
  settings.offset = v_out;

}

void tuneSensitivity(int aref) {

  double known = 1;
  long value = 0;
  long count = 10000;
  for(long i = 0; i < count; i++) {
    value += analogRead(settings.currentpin);
  }

  long v_out = (value * res(aref)) / count;

  double delta = double(v_out - settings.offset);

  long sensitivity = delta / known;

  Serial.print("Sensitivity set to (mV/A): ");
  Serial.println(sensitivity);
  settings.sensitivity = sensitivity;

}

void tuneDividerGain(int aref) {

  double known = 18000;
  long value = 0;
  long count = 10000;
  for(long i = 0; i < count; i++) {
    value += analogRead(settings.voltagepin);
  }

  long v_out = (value / count) * res(aref);
  double gain = known / v_out;

  int new_r1 = gain * settings.r2 - settings.r2;

  Serial.print("Setting R1 to (Mohm): ");
  Serial.println(new_r1);
  settings.r1 = new_r1;

}
