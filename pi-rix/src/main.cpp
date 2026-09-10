#include <Arduino.h>
#include <MIDIUSB.h>

#define MIDI_CHANNEL 0 //maybe??

extern volatile unsigned long timer0_millis;
unsigned int last_change = 0;
byte last_status = HIGH;

char presets[][20] = {
  "Preset 1",
  "Preset 2",
  "Preset 3",
  "Preset 4",
  "Preset 5",
  "Preset 6",
  "Preset 7",
  "Preset 8"
};

void sendProgramChange(byte program){
  midiEventPacket_t event;
  
  event.header = 0x0C;
  event.byte1 = 0xC0 | MIDI_CHANNEL;
  event.byte2 = program;
  event.byte3 = 0;

  MidiUSB.sendMIDI(event);
  MidiUSB.flush();
}

void setup() {
  pinMode(2, INPUT_PULLUP); //not sure leonardo has internal pullup resistor
  pinMode(3, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
}

void loop() {
  //need debouncing..
  if(digitalRead(2) == LOW){
    if(last_status == HIGH && millis() - last_change > 100){
      last_status = LOW;
      last_change = millis();
      sendProgramChange(0);
    }
  }
  if(digitalRead(2) == HIGH){
    if(last_status == LOW && millis() - last_change > 100){
      last_status = HIGH;
      last_change = millis();
    }
  }
  if(digitalRead(3) == LOW){
    if(last_status == HIGH && millis() - last_change > 100){
      last_status = LOW;
      last_change = millis();
      sendProgramChange(1);
    }
  }
  if(digitalRead(3) == HIGH){
    if(last_status == LOW && millis() - last_change > 100){
      last_status = HIGH;
      last_change = millis();
    }
  }
}