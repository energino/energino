/*
 * Energino
 *
 * Energino Yun is a energy consumption meter for DC loads
 *
 * Circuit:
 *  Analog inputs attached to pins A2 (Current), A1 (Voltage)
 *  Digital output attached to pin D4 (Relay)
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
 *  #F<feed>, sets the feed id [default is 0]
 *  #K<key>, sets the Xively authentication key [default is -]
 *  #U<url>, sets the Xively URL [default is https://api.xively.com/v2/feeds/]
 *
 * No updates are sent over the Ethernet interface if the feed id is set to 0
 *
 * Serial putput:
 *   #EnerginoPOE,0,<voltage>,<current>,<power>,<relay>,<period>,<samples>,<voltage_error>,<current_error>,<feed>,<url>,<key>
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

#include <energino.h>
#include <sma.h>
#include <Process.h>
#include <Bridge.h>
#include <YunServer.h>
#include <YunClient.h>

#define APIKEY        "foo"
#define FEEDID        0
#define FEEDSURL      "https://api.xively.com/v2/feeds/"

#define RELAYPIN      4
#define CURRENTPIN    A2
#define VOLTAGEPIN    A1

// Energino parameters
int R1 = 390;
int R2 = 100;
int OFFSET = 2500;
int SENSITIVITY = 185;
int PERIOD = 2000;

// magic string
const char MAGIC[] = "EnerginoPOE";
const int REVISION = 1;

// Moving averages
const int MAPOINTS = 101;

SMA v_sma(MAPOINTS);
SMA i_sma(MAPOINTS);

// Last update
long lastUpdated;

void reset() {
  strcpy (settings.magic,MAGIC);
  settings.period = PERIOD;
  settings.r1 = R1;
  settings.r2 = R2;
  settings.offset = OFFSET;
  settings.sensitivity = SENSITIVITY;
  settings.relaypin = RELAYPIN;
  settings.currentpin = CURRENTPIN;
  settings.voltagepin = VOLTAGEPIN;
  strcpy (settings.apikey,APIKEY);
  settings.feedid = FEEDID;
  strcpy (settings.feedsurl,FEEDSURL);
}

// rest server
YunServer server;

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
  // Init bridge library
  Bridge.begin();
  // listen on port
  server.listenOnLocalhost();
  server.begin();
  // Use the led 13 to notify that the
  // setup completed
  pinMode(13,OUTPUT);
  digitalWrite(13, HIGH);
  // Set last update to now
  lastUpdated = millis();
}

void loop() {

  // Make sure that update period is not too high
  // when pushing data to Xively (one sample every
  // 2 seconds should be a reasonable lower boud)
  if ((settings.feedid != 0) && (settings.period < 2000)) {
    settings.period = 2000;
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
  serParseCommand();

  // Instant values are too floating,
  // let's smooth them up
  int v = analogRead(VOLTAGEPIN);
  int i = analogRead(CURRENTPIN);

  v_sma.add(v);
  i_sma.add(i);

  if (lastUpdated + settings.period <= millis()) {
    // dump to serial
    dumpToSerial();
    // send data to remote host
    sendData();
    // set last update
    lastUpdated = millis();
  }

}

// this method makes a HTTP connection to the server:
void sendData() {

  if (settings.feedid == 0) {
    return;
  }

  // form the string for the API header parameter:
  String apiString = "X-ApiKey: ";
  apiString += settings.apikey;

  // form the string for the URL parameter:
  String url = settings.feedsurl;
  url += settings.feedid;
  url += ".csv";

  // form the string for the payload
  String dataString = "current,";
  dataString += getAvgCurrent(i_sma.avg());
  dataString += "\nvoltage,";
  dataString += getAvgVoltage(v_sma.avg());
  dataString += "\npower,";
  dataString += getAvgPower(v_sma.avg(), i_sma.avg());
  dataString += "\nswitch,";
  dataString += digitalRead(settings.relaypin);

  // Send the HTTP PUT request

  // Is better to declare the Process here, so when the
  // sendData function finishes the resources are immediately
  // released. Declaring it global works too, BTW.
  Process xively;
  Serial.print("@sending data... ");
  xively.begin("curl");
  xively.addParameter("-k");
  xively.addParameter("--request");
  xively.addParameter("PUT");
  xively.addParameter("--data");
  xively.addParameter(dataString);
  xively.addParameter("--header");
  xively.addParameter(apiString);
  xively.addParameter(url);
  xively.run();
  Serial.println("done!");

}

void dumpToSerial() {
  // Print data also on the serial
  Serial.print("#");
  Serial.print(MAGIC);
  Serial.print(",");
  Serial.print(REVISION);
  Serial.print(",");
  Serial.print(getAvgVoltage(v_sma.avg()), 2);
  Serial.print(",");
  Serial.print(getAvgCurrent(i_sma.avg()), 2);
  Serial.print(",");
  Serial.print(getAvgPower(v_sma.avg(), i_sma.avg()), 1);
  Serial.print(",");
  Serial.print(digitalRead(settings.relaypin));
  Serial.print(",");
  Serial.print(settings.period);
  Serial.print(",");
  Serial.print(MAPOINTS);
  Serial.print(",");
  Serial.print(getVError());
  Serial.print(",");
  Serial.print(getIError());
  Serial.print(",");
  Serial.print(settings.feedid);
  Serial.print(",");
  Serial.print(settings.feedsurl);
  Serial.print(",");
  Serial.println(settings.apikey);
}

void process(YunClient client) {

  String command = client.readStringUntil('/');
  command.trim();

  if (command == "datastreams") {

    String subCommand = client.readStringUntil('/');
    subCommand.trim();

    if (subCommand == "current") {
      sendReply(client,subCommand,getAvgCurrent(i_sma.avg()));
      return;
    }

    if (subCommand == "voltage") {
      sendReply(client,subCommand,getAvgVoltage(v_sma.avg()));
      return;
    }

    if (subCommand == "power") {
      sendReply(client,subCommand,getAvgPower(v_sma.avg(), i_sma.avg()));
      return;
    }

    if (subCommand == "switch") {
      char c = client.read();
      if (c == '0') {
        digitalWrite(settings.relaypin, LOW);
      }
      if (c == '1') {
        digitalWrite(settings.relaypin, HIGH);
      }
      sendReply(client,subCommand,digitalRead(RELAYPIN));
      return;
    }

    client.print(F("{\"version\":\"1.0.0\","));
    client.print(F("\"datastreams\":["));
    client.print(F("{\"id\":\"voltage\",\"current_value\":"));
    client.print(getAvgVoltage(v_sma.avg()));
    client.println(F("},"));
    client.print(F("{\"id\":\"current\",\"current_value\":"));
    client.print(getAvgCurrent(i_sma.avg()));
    client.println(F("},"));
    client.print(F("{\"id\":\"power\",\"current_value\":"));
    client.print(getAvgPower(v_sma.avg(), i_sma.avg()));
    client.println(F("},"));
    client.print(F("{\"id\":\"switch\",\"current_value\":"));
    client.print(digitalRead(settings.relaypin));
    client.println(F("}"));
    client.println(F("]"));
    client.println(F("}"));
  }

}

void sendReply(YunClient client, String cmd, float value) {
  client.print(F("{\"version\":\"1.0.0\","));
  client.print(F("\"id\":\""));
  client.print(cmd);
  client.print(F("\",\"current_value\":"));
  client.print(value);
  client.println(F("}"));
}
