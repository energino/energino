/*
  energinolive.h - Library for Energino devices (live services)
*/

#include <energino.h>

#ifndef ENERGINOPOETEST_H
#define ENERGINOPOETEST_H

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

#endif
