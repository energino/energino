/*
  energinolive.h - Library for Energino devices (live services)
*/

#include <YunServer.h>
#include <YunClient.h>

#ifndef ENERGINOLIVE_H
#define ENERGINOLIVE_H

// rest server
YunServer server;

void dumpToSerialLive(int aref) {
  // Print data also on the serial
  Serial.print("#");
  Serial.print(settings.magic);
  Serial.print(",");
  Serial.print(settings.revision);
  Serial.print(",");
  Serial.print(getAvgVoltage(VFinal, aref), 2);
  Serial.print(",");
  Serial.print(getAvgCurrent(IFinal, aref), 2);
  Serial.print(",");
  Serial.print(getAvgPower(VFinal, IFinal, aref), 1);
  Serial.print(",");
  Serial.print(digitalRead(settings.relaypin));
  Serial.print(",");
  Serial.print(settings.period);
  Serial.print(",");
  Serial.print(lastSamples);
  Serial.print(",");
  Serial.print(getVError(aref));
  Serial.print(",");
  Serial.print(getIError(aref));
  Serial.print(",");
  Serial.print(settings.feedid);
  Serial.print(",");
  Serial.print(settings.feedsurl);
  Serial.print(",");
  Serial.println(settings.apikey);
}

void dumpToSerialLive() {
  dumpToSerialLive(DEFAULT_AREF);
}

// this method makes a HTTP connection to the server:
void sendData(int aref) {

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
  dataString += getAvgCurrent(IFinal, aref);
  dataString += "\nvoltage,";
  dataString += getAvgVoltage(VFinal, aref);
  dataString += "\npower,";
  dataString += getAvgPower(VFinal, IFinal, aref);
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

void sendData() {
    sendData(DEFAULT_AREF);
}

void sendReply(YunClient client, String cmd, float value) {
  client.print(F("{\"version\":\"1.0.0\","));
  client.print(F("\"id\":\""));
  client.print(cmd);
  client.print(F("\",\"current_value\":"));
  client.print(value);
  client.println(F("}"));
}

void process(YunClient client, int aref) {

  String command = client.readStringUntil('/');
  command.trim();

  if (command == "datastreams") {

    String subCommand = client.readStringUntil('/');
    subCommand.trim();

    if (subCommand == "current") {
      sendReply(client,subCommand,getAvgCurrent(IFinal, aref));
      return;
    }

    if (subCommand == "voltage") {
      sendReply(client,subCommand,getAvgVoltage(VFinal, aref));
      return;
    }

    if (subCommand == "power") {
      sendReply(client,subCommand,getAvgPower(VFinal, IFinal, aref));
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
      sendReply(client, subCommand, digitalRead(settings.relaypin));
      return;
    }

    client.print(F("{\"version\":\"1.0.0\","));
    client.print(F("\"datastreams\":["));
    client.print(F("{\"id\":\"voltage\",\"current_value\":"));
    client.print(getAvgVoltage(VFinal, aref));
    client.println(F("},"));
    client.print(F("{\"id\":\"current\",\"current_value\":"));
    client.print(getAvgCurrent(IFinal, aref));
    client.println(F("},"));
    client.print(F("{\"id\":\"power\",\"current_value\":"));
    client.print(getAvgPower(VFinal, IFinal, aref));
    client.println(F("},"));
    client.print(F("{\"id\":\"switch\",\"current_value\":"));
    client.print(digitalRead(settings.relaypin));
    client.println(F("}"));
    client.println(F("]"));
    client.println(F("}"));
  }

}

void process(YunClient client) {
    process(client, DEFAULT_AREF);
}

#endif
