// Arduino MIDI comms class
// Encapsulate all the midi message sending functions and control values
//
#pragma once

#include <Arduino.h>
#include "debug.h"

class midiComms
{
public:
  midiComms(Stream& s) : _S(s) {}

  void begin(void) {}

  void midiSend(uint8_t message, uint8_t channel, uint8_t d1, uint8_t d2 = 0)
    // Send a MIDI message out the serial port
  {
    uint8_t msg[3], l = 0;

    msg[l++] = message + channel;
    msg[l++] = d1;
    if (message <= 0xb0)
      msg[l++] = d2;

#if  USE_MIDI  
    _S.write(msg, l);
#endif

    PRINTX("\nCh: ", msg[0]);
    PRINT(" d1: ", msg[1]);
    PRINT(" d2: ", msg[2]);
  }

  // Define functions for each MIDI message type
  //
  // System messages
  inline void noteOff(uint8_t n)               { midiSend(0x80, PERCUSSION_CHANNEL, n, 0); } // note, velocity
  inline void noteOn(uint8_t n, uint8_t v)     { midiSend(0x90, PERCUSSION_CHANNEL, n, v); } // note, velocity  
  inline void keyPress(uint8_t n, uint8_t p)   { midiSend(0xa0, PERCUSSION_CHANNEL, n, p); } // note, pressure
  inline void ctrlChange(uint8_t c, uint8_t v) { midiSend(0xb0, PERCUSSION_CHANNEL, c, v); } // controller, value
  inline void progChange(uint8_t n)            { midiSend(0xc0, PERCUSSION_CHANNEL, n); }    // program number
  inline void chanPress(uint8_t p)             { midiSend(0xd0, PERCUSSION_CHANNEL, p); }    // pressure
  inline void pitchBlend(uint8_t m, uint8_t l) { midiSend(0xe0, PERCUSSION_CHANNEL, m, l); } // LSB, MSB

  // Channel Mode Messages
  inline void chanSoundOff(void)       { midiSend(0xb0, PERCUSSION_CHANNEL, 0x78); }    // all sound is muted on channel
  inline void chanReset(void)          { midiSend(0xb0, PERCUSSION_CHANNEL, 0x79); }    // reset all controllers
  inline void chanLocalCtrl(uint8_t v) { midiSend(0xb0, PERCUSSION_CHANNEL, 0x7a, v); } // local control (0=OFF, 127=ON); }
  inline void chanNotesOff(void)       { midiSend(0xb0, PERCUSSION_CHANNEL, 0x7b); }    // all notes on channel turn off
  inline void chanOmniOff(void)        { midiSend(0xb0, PERCUSSION_CHANNEL, 0x7c); }    // omni mode on
  inline void chanOmniOn(void)         { midiSend(0xb0, PERCUSSION_CHANNEL, 0x7d); }    // omni mode off
  inline void chanMonoOn(uint8_t n)    { midiSend(0xb0, PERCUSSION_CHANNEL, 0x7e, n); } // mono mode on. n=#channels (omni off); } or 0 (omni on); }
  inline void chanPolyOn(void)         { midiSend(0xb0, PERCUSSION_CHANNEL, 0x7f); }    // poly mode on

  // Control Changes
  inline void ctlBankMSB(uint8_t b)    { midiSend(0xb0, PERCUSSION_CHANNEL, 0x00, b); } // select the bank MSB (0-127); }
  inline void ctlBankLSB(uint8_t b)    { midiSend(0xb0, PERCUSSION_CHANNEL, 0x20, b); } // select the bank LSB (0-127); }
  inline void ctlModMSB(uint8_t m)     { midiSend(0xb0, PERCUSSION_CHANNEL, 0x01, m); } // set modulation MSB (0-127); }
  inline void ctlModLSB(uint8_t m)     { midiSend(0xb0, PERCUSSION_CHANNEL, 0x21, m); } // set modulation LSB (0-127); }
  inline void ctlVolMSB(uint8_t v)     { midiSend(0xb0, PERCUSSION_CHANNEL, 0x07, v); } // volume setting MSB (0-127); }
  inline void ctlVolLSB(uint8_t v)     { midiSend(0xb0, PERCUSSION_CHANNEL, 0x27, v); } // volume setting LSB (0-127); }

private:
  const uint8_t PERCUSSION_CHANNEL = 9; // zero based channel number for percussion (Ch 10)
  Stream& _S;   // output stream
};
