/*
 * Energino
 *
 * Energino Yun is a energy consumption meter for DC loads
 *
 * This sketch connects an Arduino equipped with the Energino 
 * shield and with an Ethernet shield with a web service 
 * implementing the COSM (formely Pachube) REST API. It also
 * implements a serial interface.
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
 *  #C<integer>, sets the offset in mV of the current sensor [default is 2500]
 *  #D<integer>, sets the sensitivity in mV of the current sensor [default is 185]
 *  #R, resest the configuration to the defaults
 *  #F<feed>, sets the feed id [default is 0]
 *  #K<key>, sets the Xively authentication key [default is foo]
 *  #U<url>, sets the Xively URL [default is https://api.xively.com/v2/feeds/]
 *
 * No updates are sent over the Ethernet interface if the feed id is set to 0
 *
 * Serial putput:
 *   #Energino,0,<voltage>,<current>,<power>,<relay>,<period>,<samples>,<feed>,<key>
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
 * created 9 November 2013
 * by Roberto Riggio
 *
 * This code is released under the BSD Licence
 *
 */

#include <Process.h>
#include <Bridge.h>
#include <YunServer.h>
#include <YunClient.h>
#include <avr/eeprom.h>

#define APIKEY        "foo"                               // replace your Xively api key here
#define FEEDID        0                                   // replace your feed ID
#define FEEDSURL      "https://api.xively.com/v2/feeds/"  // replace your remote service IP address

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
const char MAGIC[] = "EnerginoYun";
const int REVISION = 1;

// Permanent configuration
struct settings_t {
  char magic[12];
  long period;
  int r1;
  int r2;
  int offset;
  int sensitivity;
  char apikey[49];
  long feedid;
  char feedsurl[60];
} 
settings;

void reset() {
  strcpy (settings.magic,MAGIC);
  settings.period = PERIOD;
  settings.r1 = R1;
  settings.r2 = R2;
  settings.offset = OFFSET;
  settings.sensitivity = SENSITIVITY;
  strcpy (settings.apikey,APIKEY);  
  settings.feedid = FEEDID;
  strcpy (settings.feedsurl,FEEDSURL);  
}

// rest server
YunServer server;

void setup() {
  // Init bridge library
  Bridge.begin();
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
  // listen on port
  server.listenOnLocalhost();
  server.begin();
}

void loop() {

  // Make sure that update period is not too high
  // when pushing data to Xively (one sample every 
  // 5 seconds should be a reasonable lower boud)
  if ((settings.feedid != 0) && (settings.period < 5000)) {
    resetSleep(5000);
  }

  // Start profiling
  long started = millis();

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
  long VRaw = 0;
  long IRaw = 0;

  for(long i = 0; i < sleep; i++) {
    VRaw += analogRead(VOLTAGEPIN);
    IRaw += analogRead(CURRENTPIN);
  }

  // Conversion
  VFinal = scaleVoltage((float)VRaw/sleep);
  IFinal = scaleCurrent((float)IRaw/sleep);

  // send data to remote host
  sendData();

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

  // Send the HTTP PUT request

  // Is better to declare the Process here, so when the
  // sendData function finishes the resources are immediately
  // released. Declaring it global works too, BTW.
  Process xively;

  xively.begin("curl");
  xively.addParameter("-k");
  xively.addParameter("--silent");
  xively.addParameter("--request");
  xively.addParameter("PUT");
  xively.addParameter("--data");

  String dataString = "current,";
  dataString += IFinal;
  dataString += "\nvoltage,";
  dataString += VFinal;
  dataString += "\npower,";
  dataString += VFinal*IFinal;
  dataString += "\nswitch,";
  dataString += digitalRead(RELAYPIN);

  xively.addParameter(dataString);
  xively.addParameter("--header");
  xively.addParameter(apiString); 
  xively.addParameter(url);
  xively.run();

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
    '\0'                             };
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
  Serial.print(",");
  Serial.print(settings.feedid);
  Serial.print(",");
  Serial.print(settings.feedsurl);
  Serial.print(",");
  Serial.println(settings.apikey);
}

float scaleVoltage(float voltage) {
  float tmp = ( voltage * 5 * (settings.r1 + settings.r2)) / ( settings.r2 * 1024 );
  return (tmp > 0) ? tmp : 0.0;
}

float scaleCurrent(float current) {
  float tmp = ( current * 5000 / 1024  - settings.offset) / settings.sensitivity;
  return (tmp > 0) ? tmp : 0.0;
}

void resetSleep(long value) {
  sleep = (value * 4400) / 1000;
  settings.period = value;
}

void process(YunClient client) {

  String command = client.readStringUntil('/');
  command.trim();

  if (command == "datastreams") {

    String subCommand = client.readStringUntil('/');
    subCommand.trim();

    if (subCommand == "current") {
      sendReply(client,subCommand,IFinal);
      return;
    }

    if (subCommand == "voltage") {
      sendReply(client,subCommand,VFinal);
      return;
    }

    if (subCommand == "power") {
      sendReply(client,subCommand,VFinal*IFinal);
      return;
    }

    if (subCommand == "switch") {
      char c = client.read();
      if (c == '0') {
        digitalWrite(RELAYPIN, 0);
      } 
      if (c == '1') {
        digitalWrite(RELAYPIN, 1);
      } 
      sendReply(client,subCommand,digitalRead(RELAYPIN));
      return;
    }

    client.print(F("{\"version\":\"1.0.0\","));
    client.print(F("\"datastreams\":["));
    client.print(F("{\"id\":\"voltage\",\"current_value\":"));
    client.print(VFinal);
    client.println(F("},"));
    client.print(F("{\"id\":\"current\",\"current_value\":"));
    client.print(IFinal);
    client.println(F("},"));
    client.print(F("{\"id\":\"power\",\"current_value\":"));
    client.print(VFinal * IFinal);
    client.println(F("},"));
    client.print(F("{\"id\":\"switch\",\"current_value\":"));
    client.print(digitalRead(RELAYPIN));
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


