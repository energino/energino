/*
 * EnerginoEthernet
 *
 * This sketch connects an Arduino equipped with the Energino 
 * shield and with an Ethernet shield with a web service 
 * implementing the COSM (formely Pachube) REST API.
 * Energino is a energy consumption meter for DC loads
 *
 * Circuit:
 *  Analog inputs attached to pins A0 (Current), A1 (Voltage)
 *  Digital output attached to pin D3 (Relay)
 *
 * Supported commands from the serial:
 *  #F<feed>, sets the feed id [default is 0]
 *  #K<key>, sets the COSM authentication key [default is -]
 *  #P<integer>, sets the period between two updates (in ms) [default is 2000]
 *  #R, resest the configuration to the defaults
 * No updates are sent if the feed id is set to 0
 *
 * Serial putput:
 *   #EnerginoEthernet,0,<voltage>,<current>,<power>,<relay>,<period>,<samples>,<ip>,<port>,<host>,<port>,<feed>,<key>
 *
 * RESTful interface: 
 *  This sketch accepts HTTP requests in the form GET /<cmd>/<param>/[value], where "cmd" 
 *  can be either "read" or "write", param is one of the following parameters:
 *   Datastreams [read]
 *   Switch [read|write]
 *  Example: GET http://<ipaddress>/read/Datastreams
 *
 * created 24 May 2012
 * by Roberto Riggio
 *
 * This code is release under the BSD Licence
 *
 */

#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <avr/eeprom.h>

// comment/uncomment to disable/enable debug
//#define DEBUG

#define RELAY_PIN 2

#define STRING_BUFFER_SIZE 400
#define SAMPLES 100

#ifdef DEBUG
#define DBG if(1) 
#else
#define DBG if(0) 
#endif

// Feed id
const long FEED = 0;
const char KEY[] = "-";

// Cosm configuration
IPAddress HOST(216,52,233,121);
const long PORT = 80;

// Assign a MAC address for the ethernet controller.
byte MAC[] = { 
  0x90, 0xA2, 0xDA, 0x0D, 0x1D, 0x95 };
  
// This configuration is used when DHCP fails
IPAddress IP(172,25,18,219);
IPAddress MASK(255,255,255,0);
IPAddress GW(172,25,18,254);

// Energino parameters
long R1 = 100000;
long R2 = 10000;
float OFFSET = 2.69;
float SENSITIVITY = 0.850;
long PERIOD = 2000;

// Coordinates
float LAT=51.5235375648154;
float LON=-0.0807666778564453;

// Buffers used for parsing HTTP request lines
char buffer[STRING_BUFFER_SIZE];
char r[10];

// Server configuration parameters, energino will listen for
// incoming requests on this port
const long SERVER_PORT = 8180;

// Not found page
const char CONTENT_404[] = "HTTP/1.1 404 Not Found\n\n";

// Not implemented page
const char CONTENT_501[] = "HTTP/1.1 501 Not Implemented\n\n";

// JSON response header and footer
const char JSON_RESPONSE_BEGIN[] = "HTTP/1.1 200 OK\nContent-Type: application/json\n\n[";
const char JSON_RESPONSE_END[] = "]\n";

// magic string
const char MAGIC[] = "EnerginoEthernet";
const int REVISION = 0;

// Permanent configuration
struct settings_t {
  char magic[17];
  char apikey[49];
  long period;
  long feed;
  IPAddress host;
  long port;
} 
settings;

// working vars
EthernetClient client;
EthernetServer server(SERVER_PORT);

long lastConnectionTime = 0; 

void reset() {
  DBG Serial.println("Writing defaults.");  
  strcpy (settings.magic,MAGIC);
  settings.period = PERIOD;
  settings.feed = FEED;
  settings.host = HOST;
  settings.port = PORT;
  strcpy (settings.apikey,KEY);
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
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settings));
  if (strcmp(settings.magic, MAGIC) != 0) {
    reset();
    eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings));
  }
  // Try to configure the ethernet using DHCP
  DBG Serial.println("Running DHCP.");
  if (Ethernet.begin(MAC) == 0) {
    DBG Serial.println("DHCP failed, using static IP.");
    // Use static IP
    Ethernet.begin(MAC, IP, MASK, GW);
  }
  // Give the Ethernet shield a second to initialize:
  delay(1000);
  // Start server
  server.begin();
}

void loop() {
  // If we're not connected, and a full period has passed since
  // our connection, then connect again and send data.
  if(!client.connected() && (millis() - lastConnectionTime > settings.period)) {
    sendData();
  } else {
    client.stop();
  }
  // Parse incoming commands
  serParseCommand();
  // Listen for incoming requests
  handleRequests();
}

void serParseCommand()
{
  char cmd = '\0';
  int i, serAva;
  char inputBytes[60] = { '\0' };
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
      else if (i == 1) {
        cmd = chr;
      }
      else{
        inputBytes[i-2] = chr;
      }
    }
    inputBytes[i] = '\0';
  } 
  if (cmd == 'F') {
    long value = atol(inputBytesPtr);
    if (value >= 0) {
      settings.feed = value;
    }
  } 
  else if (cmd == 'P') {
    long value = atol(inputBytesPtr);
    if (value > 0) {
      settings.period = value;
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
    IPAddress hostIP = IPAddress(host);
    DBG Serial.print("New host ip address: ");
    DBG Serial.println(hostIP);
    settings.host=IPAddress(host);
  }  
  else if (cmd == 'S') {
    long value = atol(inputBytesPtr);
    if (value > 0) {
      DBG Serial.print("New host port: ");
      DBG Serial.println(value);
      settings.port = value;
    }
    
  }   
  else if (cmd == 'R') {
    reset();
  }
  eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings)); 
}

// This method accepts HTTP requests in the form GET /<cmd>/<param>/[value]
void handleRequests() {
  EthernetClient request = server.available();
  if (request) {
    DBG Serial.println("Got request");
    int idx = 0;
    while (request.connected() && request.available()) {
      // Read char by char HTTP request
      char c = request.read();
      // Store characters to string
      if (idx < STRING_BUFFER_SIZE) {
        buffer[idx] = c;
      }
      idx++;
      // If you've gotten to the end of the line (received a newline
      // character) the http request has ended, so you can send a reply
      if (c == '\n' || idx >= STRING_BUFFER_SIZE) {
        char method [5];
        char url [20];
        sscanf (buffer,"%s /%s/%s/%d",method,url);
        if (strcmp(method, "GET") == 0) {
          char * toks;
          toks = strtok (url,"/");
          if (strcmp(toks, "read") == 0) {
            toks = strtok (NULL, "/");
            if (strcmp(toks, "datastreams") == 0) {
              DBG Serial.println("Sending full datastream");
              sendContent(request,getDatastreams());
              break;
            } 
            else if (strcmp(toks, "switch") == 0) {
              DBG Serial.println("Sending switch status");
              sendContent(request, digitalRead(RELAY_PIN));
              break;
            } 
          } 
          else if (strcmp(toks, "write") == 0) {
            toks = strtok (NULL, "/");
            if (strcmp(toks, "switch") == 0) {
              toks = strtok (NULL, "/");
              if (atoi(toks) > 0) {
                DBG Serial.println("Setting switch status to HIGH");
                digitalWrite(RELAY_PIN, HIGH);
              } 
              else { 
                DBG Serial.println("Setting switch status to LOW");
                digitalWrite(RELAY_PIN, LOW);
              }
              sendContent(request, digitalRead(RELAY_PIN));
              break;
            } 
          } 
          // send a not found response header
          request.println(CONTENT_404);
          request.println();
          break;
        }
        request.println(CONTENT_501);
        request.println();
        break;
      }
    }
    // give the web browser time to receive the data
    delay(10);
    // close the connection:
    request.stop();
  }
}

void sendContent(EthernetClient &request, long value) {
  request.println(JSON_RESPONSE_BEGIN);
  request.print(value);
  request.print(JSON_RESPONSE_END);
}

void sendContent(EthernetClient &request, char *value) {
  request.println(JSON_RESPONSE_BEGIN);
  request.print(value);
  request.print(JSON_RESPONSE_END);
}

// this method makes a HTTP connection to the server
void sendData() {
  // poll energino
  char *json = getDatastreams();
  // if there's a successful connection:
  DBG Serial.println("Connecting.");
  if ((settings.feed != 0) && client.connect(settings.host, settings.port)) {
    DBG Serial.println("connected.");
    DBG Serial.print("PUT /v2/feeds/");
    DBG Serial.println(settings.feed);
    // send the HTTP PUT request. 
    client.print("PUT /v2/feeds/");
    client.print(settings.feed);
    client.print(" HTTP/1.1\n");
    client.println("Host: api.cosm.com");
    client.print("X-PachubeApiKey: ");
    client.println(settings.apikey);
    client.print("User-Agent: ");
    client.println(MAGIC);    
    client.print("Content-Length: ");
    client.print(strlen(json));
    client.print("\n");
    client.print("Content-Type: application/json\n");
    client.print("\n");
    // here's the actual content of the PUT request:
    client.print(json);
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
  else {
    // if you couldn't make a connection:
    DBG Serial.println("Unable to connect.");
    client.stop();
  }
  lastConnectionTime = millis();
}

char * getDatastreams() {
  float i = getCurrent(A0, OFFSET, SENSITIVITY);
  float v = getVoltage(A1, R1, R2);
  strcpy (buffer,"{\"version\":\"1.0.0\",");
  strcat (buffer,"\"datastreams\":[");
  strcat (buffer,"{\"id\":\"current\",\"current_value\":\"");
  strcat (buffer,dtostrf (i, 5, 3, r));
  strcat (buffer,"\"},");
  strcat (buffer,"{\"id\":\"voltage\",\"current_value\":\"");
  strcat (buffer,dtostrf (v, 5, 3, r));
  strcat (buffer,"\"},");
  strcat (buffer,"{\"id\":\"watts\",\"current_value\":\"");
  strcat (buffer,dtostrf (v * i, 5, 3, r));
  strcat (buffer,"\"},");
  strcat (buffer,"{\"id\":\"switch\", \"current_value\":\"");
  strcat (buffer,itoa(digitalRead(RELAY_PIN),r,10));
  strcat (buffer,"\"}");
  strcat (buffer,"],\"location\":{\"lat\":");
  strcat (buffer,dtostrf (LAT, 8, 5, r));
  strcat (buffer,",\"lon\":");
  strcat (buffer,dtostrf (LON, 8, 5, r));
  strcat (buffer,"}}");
  // Print data also on the serial
  Serial.print("#");
  Serial.print(MAGIC);
  Serial.print(",");
  Serial.print(REVISION);
  Serial.print(",");
  Serial.print(v,3);
  Serial.print(",");
  Serial.print(i,3);
  Serial.print(",");
  Serial.print(v * i,3);
  Serial.print(",");
  Serial.print(digitalRead(RELAY_PIN));
  Serial.print(",");
  Serial.print(settings.period);
  Serial.print(",");
  Serial.print(SAMPLES);
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
  Serial.print("\n");  
  return buffer;
}

float getVoltage(int pin, long r1, long r2) {
  long readings = 0;
  for(int i = 0; i < SAMPLES; i++) {
    readings += analogRead(pin);
  }
  long VRaw = readings / SAMPLES;
  return ((float) VRaw * 5 * (r1 + r2)) / ( r2 * 1024 );
}

float getCurrent(int pin, float offset, float sensitivity) {
  long readings = 0;
  for(int i = 0; i < SAMPLES; i++) {
    readings += analogRead(pin);
  }
  long IRaw = readings / SAMPLES;
  return ((float) IRaw * 5 / ( 1024 ) - offset) / sensitivity;
}

