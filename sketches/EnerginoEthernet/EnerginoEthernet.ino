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
 *  #F<feed>, sets the feed id [default is 0]
 *  #K<key>, sets the COSM authentication key [default is -]
 *  #H, sets the remote host ip address [default is 216.52.233.121 (Cosm)]
 *  #O, sets the remote host port [default is 80]
 *
 * No updates are sent over the Ethernet interface if the feed id is set to 0
 *
 * Serial putput:
 *   #Energino,0,<voltage>,<current>,<power>,<relay>,<period>,<samples>,<ip>,<port>,<host>,<port>,<feed>,<key>
 *
 * RESTful interface: 
 *  This sketch accepts HTTP requests in the form GET /<cmd>/<param>/[value], where "cmd" 
 *  can be either "read" or "write", param is one of the following parameters:
 *   datastreams [read]
 *   switch [read|write]
 *  Examples: 
 *   GET http://<ipaddress>/read/datastreams
 *   GET http://<ipaddress>/read/switch
 *   GET http://<ipaddress>/write/switch/0
 *   GET http://<ipaddress>/write/switch/1
 *
 * created 31 October 2012
 * by Roberto Riggio
 *
 * This code is released under the BSD Licence
 *
 */

#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <avr/eeprom.h>

// comment/uncomment to disable/enable debug
//#define DEBUG

// comment/uncomment to disable/enable ethernet support
#define NOETH

#define RELAY_PIN 2

#ifdef DEBUG
#define DBG if(1) 
#else
#define DBG if(0) 
#endif

// Feed id
long FEED = 0;
// Cosm key
char KEY[] = "-";

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

#ifndef NOETH

// Cosm configuration
IPAddress HOST(216,52,233,121);
long PORT = 80;

// Server configuration parameters, energino will listen for
// incoming requests on this port
const long SERVER_PORT = 8180;

// HTML response
const char HTML_RESPONSE[43] PROGMEM = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n";
const char HTML_RESPONSE_404[25] PROGMEM = "HTTP/1.1 404 Not Found\n\n";

// JSON response header and footer
const char JSON_RESPONSE[49] PROGMEM = "HTTP/1.1 200 OK\nContent-Type: application/json\n\n";

// working vars
EthernetServer server(SERVER_PORT);

#endif

// magic string
const char MAGIC[] = "Energino";
const int REVISION = 1;

// Permanent configuration
struct settings_t {
  char magic[9];
  long period;
  int r1;
  int r2;
  int offset;
  int sensitivity;
  byte mac[6];
  char apikey[49];
  long feed;
  IPAddress host;
  long port;
} 
settings;

void reset() {
  strcpy (settings.magic,MAGIC);
  settings.period = PERIOD;
  settings.r1 = R1;
  settings.r2 = R2;
  settings.offset = OFFSET;
  settings.sensitivity = SENSITIVITY;
#ifndef NOETH
  settings.feed = FEED;
  settings.host = HOST;
  settings.port = PORT;
  strcpy (settings.apikey,KEY);  
  settings.mac[0] = 0x90;
  settings.mac[1] = 0xA2;
  settings.mac[2] = 0xDA;
  for (int i = 3; i < 6; i++) {
    randomSeed(analogRead(0));
    settings.mac[i] = random(0, 255);
  }
#endif
}

void setup() {
  // Set serial port
  Serial.begin(115200); 
  // Default on
  pinMode(RELAY_PIN,OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  // Loading setting 
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settings));
  if (strcmp(settings.magic, MAGIC) != 0) {
    reset();
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings));
  }
  // set sleep counter
  resetSleep(settings.period);
#ifndef NOETH
  // Try to configure the ethernet using DHCP
  Serial.println("@dhcp");
  if (Ethernet.begin(settings.mac) == 0) {
    Serial.println("@dhcp: fail");
    return;
  }
  Serial.println("@dhcp done");
  // Start server
  server.begin();
  // Init SD
  pinMode(10, OUTPUT);
  // see if the card is present and can be initialized:
  if (!SD.begin(4)) {
    Serial.println("@sd: fail");
    return;
  }
#endif
}

void loop() {

  // Make sure that update period is not too high
#ifndef NOETH
  if ((settings.feed != 0) && (settings.period < 2000)) {
    resetSleep(2000);
  }
#endif

  // Start profiling
  long started = millis();

  // Parse incoming commands
  serParseCommand();

  // Listen for incoming requests
#ifndef NOETH
  handleRequests();
#endif

  // Instant values are too floating,
  // let's smooth them up
  long VRaw = 0;
  long IRaw = 0;

  for(long i = 0; i < sleep; i++) {
    VRaw += analogRead(A1);
    IRaw += analogRead(A0);
  }

  // Conversion
  VFinal = scaleVoltage((float)VRaw/sleep);
  IFinal = scaleCurrent((float)IRaw/sleep);

  // send data to remote host
#ifndef NOETH
  sendData();
#endif

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
#ifndef NOETH
  else if (cmd == 'F') {
    long value = atol(inputBytesPtr);
    if (value >= 0) {
      settings.feed = value;
    }
  } 
  else if (cmd == 'K') {
    strncpy(settings.apikey, inputBytes,49);
    settings.apikey[48] = '\0';
  }
  else if (cmd == 'H') {
    byte host[4];
    char *p = strtok(inputBytes, ".");  
    for (int i = 0; i < 4; i++) {
      host[i] = atoi(p);
      p = strtok(NULL, ".");
    }
    settings.host=IPAddress(host);
  }  
  else if (cmd == 'O') {
    long value = atol(inputBytesPtr);
    if (value > 0) {
      settings.port = value;
    }
  }   
#endif
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
        digitalWrite(RELAY_PIN, HIGH);
      } 
      else {
        digitalWrite(RELAY_PIN, LOW);
      }
    } 
  }
  eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings)); 
}

#ifndef NOETH

// This method accepts HTTP requests in the form GET /<cmd>/<param>/[value]
void handleRequests() {
  EthernetClient request = server.available();
  if (request) {
    int idx = 0;
    char buffer[50];
    while (request.connected() && request.available()) {
      // Read char by char HTTP request
      char c = request.read();
      DBG Serial.print(c);
      // Store characters to string
      if (idx < 50) {
        buffer[idx] = c;
      }
      idx++;
      // If you've gotten to the end of the line (received a newline
      // character) the http request has ended, so you can send a reply
      if (c == '\n' || idx >= 50) {
        char * method;
        method = strtok (buffer, " ");
        if (strcmp(method, "GET") == 0) {
          char * toks;
          toks = strtok (NULL," ");
          toks = strtok (toks,"/");
          if (strcmp(toks, "read") == 0) {
            toks = strtok (NULL, "/");
            if (strcmp(toks, "datastreams") == 0) {
              writeHeader(request, JSON_RESPONSE);
              writeDataStream(request);
              break;
            } 
            else if (strcmp(toks, "switch") == 0) {
              writeHeader(request, JSON_RESPONSE);
              request.print("[");
              request.print(digitalRead(RELAY_PIN));
              request.print("]\n");
              break;
            } 
          } 
          else if (strcmp(toks, "write") == 0) {
            toks = strtok (NULL, "/");
            if (strcmp(toks, "switch") == 0) {
              toks = strtok (NULL, "/");
              if (atoi(toks) > 0) {
                digitalWrite(RELAY_PIN, HIGH);
              } 
              else { 
                digitalWrite(RELAY_PIN, LOW);
              }
              writeHeader(request, JSON_RESPONSE);
              request.print("[");
              request.print(digitalRead(RELAY_PIN));
              request.print("]\n");
              break;
            } 
          }
          else {
            File dataFile = SD.open(toks, FILE_READ);
            if (dataFile) {
              writeHeader(request, HTML_RESPONSE);
              while (dataFile.available()) {
                request.write(dataFile.read());
              }
              dataFile.close();
              request.write("\n");
              break;
            } 
          }
        }
        writeHeader(request, HTML_RESPONSE_404);
        request.write("\n");
        break;
      }
    }
    // give the web browser time to receive the data
    delay(10);
    // close the connection:
    request.stop();
  }
}

// this method makes a HTTP connection to the server
void sendData() {
  // check if the feed has been initialized
  if (settings.feed == 0) {
    return;
  }
  // try to connect
  EthernetClient client;    
  int ret = client.connect(settings.host, settings.port);
  if (ret == 1) {
    // build json document
    DBG Serial.print("PUT /v2/feeds/");
    DBG Serial.println(settings.feed);
    // send the HTTP PUT request. 
    client.print("PUT /v2/feeds/");
    client.print(settings.feed);
    client.print(" HTTP/1.1\n");
    client.print("X-PachubeApiKey: ");
    client.println(settings.apikey);
    client.print("User-Agent: ");
    client.println(MAGIC);    
    client.print("Content-Type: application/json\n");
    client.print("\n");
    // here's the actual content of the PUT request:
    writeDataStream(client);
    delay(1000);
    // if there are incoming bytes available 
    // from the server, read them and print them:
    while (client.available()) {
      char c = client.read();
      DBG Serial.print(c);
    }
    DBG Serial.print("\n");
    client.stop();
  }
}

void writeHeader(EthernetClient &request, const char * data) {
  while (pgm_read_byte(data) != 0x00) {
    request.write(pgm_read_byte(data++));             
  }
}

void writeDataStream(EthernetClient &request) {
  char r[10];
  request.print("{\"version\":\"1.0.0\",");
  request.print("\"datastreams\":[");
  request.print("{\"id\":\"current\",\"current_value\":");
  request.print(dtostrf (IFinal, 5, 3, r));
  request.print("},");
  request.print("{\"id\":\"voltage\",\"current_value\":");
  request.print(dtostrf (VFinal, 5, 3, r));
  request.print("},");
  request.print("{\"id\":\"watts\",\"current_value\":");
  request.print(dtostrf (VFinal * IFinal, 5, 3, r));
  request.print("},");
  request.print("{\"id\":\"switch\", \"current_value\":");
  request.print(itoa(digitalRead(RELAY_PIN),r,10));
  request.print("}");
  request.print("]}");
}

#endif

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
  Serial.print(digitalRead(RELAY_PIN));
  Serial.print(",");
  Serial.print(delta);
  Serial.print(",");
  Serial.print(sleep);
#ifdef NOETH
  Serial.print(",");
  Serial.print("-");
  Serial.print(",");
  Serial.print("-");
  Serial.print(",");
  Serial.print("-");
  Serial.print(",");
  Serial.print("-");
  Serial.print(",");
  Serial.print("-");
  Serial.print(",");
  Serial.print("-");
#else
  Serial.print(",");
  Serial.print(Ethernet.localIP());
  Serial.print(",");
  Serial.print(SERVER_PORT);
  Serial.print(",");
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(settings.host[thisByte], DEC);
    if (thisByte < 3) {
      Serial.print(".");
    }
  }
  Serial.print(",");
  Serial.print(settings.port);
  Serial.print(",");
  Serial.print(settings.feed);
  Serial.print(",");
  Serial.print(settings.apikey);
#endif
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
