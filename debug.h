// Debug settings
//
#pragma once

// MIDI vs debugging mode
#define USE_MIDI 1

#if USE_MIDI
#define PRINT(s, v)
#define PRINTX(s, v)
#define PRINTS(s)

#define SERIAL_RATE 31250
#else
#define PRINT(s, v)   { Serial.print(F(s)); Serial.print(v); }
#define PRINTX(s, v)  { Serial.print(F(s)); Serial.print(F("0x")); Serial.print(v, HEX); }
#define PRINTS(s)     { Serial.print(F(s)); }

#define SERIAL_RATE 57600
#endif // USE MIDI
