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
#include <Ethernet.h>
#include <EEPROM.h>
#include <avr/eeprom.h>

// comment/uncomment to disable/enable debug
#define DEBUG

#define RELAY_PIN 3
#define STRING_BUFFER_SIZE 300

#ifdef DEBUG
#define DBG if(1) 
#else
#define DBG if(0) 
#endif

// Feed id
const long FEED = 0;
// Cosm key
const char KEY[] = "-";

// Cosm configuration
IPAddress HOST(216,52,233,121);
const long PORT = 80;

// This configuration is used when DHCP fails
IPAddress IP(172,25,18,219);
IPAddress MASK(255,255,255,0);
IPAddress GW(172,25,18,254);

// Energino parameters
int R1 = 100;
int R2 = 10;
int OFFSET = 2500;
int SENSITIVITY = 850;
int PERIOD = 2000;

// Buffers used for parsing HTTP request lines
char buffer[STRING_BUFFER_SIZE];
char r[10];

// Server configuration parameters, energino will listen for
// incoming requests on this port
const long SERVER_PORT = 8180;

// Accumulators
long VRaw;
long IRaw;

// Control loop paramters
long sleep = 0;
long delta = 0;

// Last computed values
float VFinal = 0.0;
float IFinal = 0.0;

// Not found page
const char CONTENT_404[] = "HTTP/1.1 404 Not Found\n\n";

// Not implemented page
const char CONTENT_501[] = "HTTP/1.1 501 Not Implemented\n\n";

// JSON response header and footer
const char JSON_RESPONSE_BEGIN[] = "HTTP/1.1 200 OK\nContent-Type: application/json\n\n[";
const char JSON_RESPONSE_END[] = "]\n";

// magic string
const char MAGIC[] = "Energino";
const int REVISION = 0;

// Minimum period for Ethernet updates
const long MIN_ETHERNET_PERIOD = 2000;

// Permanent configuration
struct settings_t {
  char magic[17];
  byte mac[6];
  char apikey[49];
  long period;
  int r1;
  int r2;
  int offset;
  int sensitivity;
  long feed;
  IPAddress host;
  long port;
} 
settings;

// working vars
EthernetServer server(SERVER_PORT);

void reset() {
  DBG Serial.println("Writing defaults.");  
  strcpy (settings.magic,MAGIC);
  settings.period = PERIOD;
  settings.r1 = R1;
  settings.r2 = R2;
  settings.offset = OFFSET;
  settings.sensitivity = SENSITIVITY;
  settings.feed = FEED;
  settings.host = HOST;
  settings.port = PORT;
  strcpy (settings.apikey,KEY);  
  randomSeed(analogRead(0));
  settings.mac[0] = 0x90;
  settings.mac[1] = 0xA2;
  settings.mac[2] = 0xDA;
  for (int i = 3; i < 6; i++) {
    settings.mac[i] = random(0, 255);
  }
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
  // set sleep counter
  resetSleep(settings.period);
  // Print hw address
  char macstr[18];
  snprintf(macstr, 18, "%02x:%02x:%02x:%02x:%02x:%02x", settings.mac[0], settings.mac[1], settings.mac[2], settings.mac[3], settings.mac[4], settings.mac[5]);
  DBG Serial.print("HWAddr: ");
  DBG Serial.println(macstr);
  // Try to configure the ethernet using DHCP
  DBG Serial.println("Running DHCP...");
  if (Ethernet.begin(settings.mac) == 0) {
    DBG Serial.println("DHCP failed, using static IP.");
    // Use static IP
    Ethernet.begin(settings.mac, IP, MASK, GW);
  }
  // Give the Ethernet shield a second to initialize:
  delay(1000);
  // Start server
  server.begin();
}

void loop() {

  // Make sure that update period is not too high
  if ((settings.feed != 0) && (settings.period < MIN_ETHERNET_PERIOD)) {
    DBG Serial.println("Update period is too high, resetting.");
    resetSleep(MIN_ETHERNET_PERIOD);
  }

  // Start profiling
  long started = millis();

  // Parse incoming commands
  serParseCommand();

  // Listen for incoming requests
  handleRequests();

  // Instant values are too floating,
  // let's smooth them up
  VRaw = 0;
  IRaw = 0;

  int skipCount = 0;

  for(long i = 0; i < sleep; i++) {
    VRaw += analogRead(A1);
    IRaw += analogRead(A0);
  }

  // Conversion
  VFinal = scaleVoltage((float)VRaw/sleep);
  IFinal = scaleCurrent((float)IRaw/sleep);

  // send data to remote host
  sendData();

  // Control loop
  delta = abs(millis() - started);
  sleep -= 5 * (delta - settings.period);
  if (sleep < 5) {
    sleep = 5;
  }

}

void serParseCommand()
{
  char cmd = '\0';
  int i, serAva;
  char inputBytes[60] = { 
    '\0'       };
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
    int value = atoi(inputBytesPtr);
    if (value > 0) {
      resetSleep(value);
    }
  } 
  else if (cmd == 'A') {
    int value = atoi(inputBytesPtr);
    if (value >= 0) {
      settings.r1 = value;
    }
  } 
  else if (cmd == 'B') {
    int value = atoi(inputBytesPtr);
    if (value >= 0) {
      settings.r2 = value;
    }
  } 
  else if (cmd == 'C') {
    int value = atoi(inputBytesPtr);
    if (value >= 0) {
      settings.offset = value;
    }
  } 
  else if (cmd == 'D') {
    int value = atoi(inputBytesPtr);
    if (value >= 0) {
      settings.sensitivity = value;
    }
  } 
  else if (cmd == 'S') {
    int value = atoi(inputBytesPtr);
    if (value >= 0) {
      digitalWrite(RELAY_PIN, HIGH);
    } 
    else {
      digitalWrite(RELAY_PIN, LOW);
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
  else if (cmd == 'O') {
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
    DBG Serial.println("Got request: ");
    int idx = 0;
    while (request.connected() && request.available()) {
      // Read char by char HTTP request
      char c = request.read();
      DBG Serial.print(c);
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
  // check if the feed has been initialized
  if (settings.feed == 0) {
    return;
  }
  // try to connect
  DBG Serial.println("Connecting.");
  EthernetClient client;    
  if (client.connect(settings.host, settings.port)) {
    DBG Serial.println("connected.");
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
  }
}

char * getDatastreams() {
  strcpy (buffer,"{\"version\":\"1.0.0\",");
  strcat (buffer,"\"datastreams\":[");
  strcat (buffer,"{\"id\":\"current\",\"current_value\":");
  strcat (buffer,dtostrf (IFinal, 5, 3, r));
  strcat (buffer,"},");
  strcat (buffer,"{\"id\":\"voltage\",\"current_value\":");
  strcat (buffer,dtostrf (VFinal, 5, 3, r));
  strcat (buffer,"},");
  strcat (buffer,"{\"id\":\"watts\",\"current_value\":");
  strcat (buffer,dtostrf (VFinal * IFinal, 5, 3, r));
  strcat (buffer,"},");
  strcat (buffer,"{\"id\":\"switch\", \"current_value\":");
  strcat (buffer,itoa(digitalRead(RELAY_PIN),r,10));
  strcat (buffer,"}");
  strcat (buffer,"]}");
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

float scaleVoltage(float VRaw) {
  return ( VRaw * 5 * (settings.r1 + settings.r2)) / ( settings.r2 * 1024 );
}

float scaleCurrent(float IRaw) {
  return ( IRaw * 5 / ( 1024 ) - settings.offset) / settings.sensitivity;
}

void resetSleep(long value) {
  sleep = value / 1000 * 5000;
  settings.period = value;
}

