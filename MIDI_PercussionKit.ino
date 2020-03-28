/*
Arduino MIDI Percussion kit by MajicDesigns

Blog article: https://arduinoplusplus.wordpress.com

Outputs MIDI percussion messages based on the configured instruments for a 
specific digital or analog AVR port. The instruments are defined in the 
Percussion Kit table (see below). No other changes to code should be necessary 
once the data is defined.
*/

#include "debug.h"
#include "MIDI_Comms.h"
#include "GM_Percussion.h"

// Miscellaneous defines
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

/*
Structure for percussion instrument definition and control
----------------------------------------------------------
Each table entry represents an element of the percussion kit with the following fields:

KMValue     Name of the instrument (from GM_Percussion.h)

Type        Digital (INSTR_ANALOG) or analog (INSTR_DIGITAL) interface for the sensor. 
            - Digital signals are detected on a HIGH to LOW transition, so they
              need to be configured with PULL UP resistors.
            - Analog signals are standard analog 0-1024 value.

Pin         The Arduino input pin to which the actuating sensor is connected.

Active      Time in ms that the note is active (ie, the time between NOTE_ON and 
            NOTE_OFF). Use 0 to forgo sending NOTE_OFF.

Exclusion   Time in ms that the instrument is locked out after the note is played.
            This is used to mask out secondary signals (eg bounce) from the instrument
            once the initial signal is detected. Only for analog interfaces, unused
            for digital.

Velocity    The default velocity of the MIDI note. This is required for digital 
            signals as it does not include a magnitude. If specified with an analog 
            input it overrides any velocity information derived from the real signal.

Sensitivity The analog input to read as the sensitivity threshold for the input value.
            This is ignored for digital sensors. For analog values only values greater
            than this setting will trigger the instrument. Use 255 or 0xff to disable
            the sensitivity setting (NO_PIN).
*/

const uint8_t NO_PIN = 0xff; 
typedef enum { INSTR_ANALOG, INSTR_DIGITAL } instr_t;
typedef enum { START, MEASURE, PLAY, EXCLUDE } state_t;

typedef struct
{
  // Static data defining the instrument.
  uint8_t   kmValue;     // GM Percussion Key Map Value (eg, BASS_DRUM_1)
  instr_t   type;        // instrument Type (INSTR_ANALOG, INSTR_DIGITAL)
  uint8_t   pin;         // hardware pin number for the input, digital or analog
  uint16_t  activeTime;  // time between the NOTE_ON and NOTE_OFF messages in ms
  uint16_t  excludeTime; // time to exclude next activation for this instrument
  uint8_t   defVelocity; // preset MIDI velocity for this (0 for adjustable). Value required for INSTR_DIGITAL
  uint8_t   sensPin;     // sensitivity adjustment analog input pin (0xff or 255 to disable)

  // Dynamic data created and used during run time.
  // This data does need to be statically initialized in the instrument table.
  bool      noteOn;      // true if NOTE_ON is current
  uint32_t  lastOnTime;  // last time the NOTE_ON was sent - saved millis() value
  uint16_t  lastStatus;  // last value read for this instrument
  state_t   state;       // state for the FSM
} instrument_t;

// Global Data
instrument_t PT[] =  // Percussion Table
{
  // kmValue        Type          Pin Activ Excl  Vel Sens
  { CRASH_CYMBAL_1, INSTR_DIGITAL,  7,    0,   0,  92, NO_PIN },
  { COWBELL,        INSTR_DIGITAL,  6, 1000,   0, 127, NO_PIN },
  { LO_BONGO,       INSTR_DIGITAL,  4,    0,   0, 127, NO_PIN },
  { HI_MID_TOM,     INSTR_ANALOG,  A0,  500,  50,   0,  A5 },
};

midiComms midi(Serial);

// Control code follows --------------------------

uint16_t getSensThreshold(uint8_t pin)
// Read the sensitivity input
{
  if (pin == NO_PIN)
    return(1);
  else
    return(analogRead(pin));
}

void handleDigital(instrument_t *p)
// Handle an instrument that has been designated as digital type.
// The value is either on or off and we need to detect a transition. 
{
  uint8_t status = digitalRead(p->pin);

  // check for a HIGH to LOW transition
  if (status != p->lastStatus)
  {
    PRINT("\nD", p->pin);
    PRINT(" ", status);
    if ((status == LOW) && (p->lastStatus == HIGH))
    {
      if (p->noteOn) midi.noteOff(p->kmValue);

      midi.noteOn(p->kmValue, p->defVelocity);
      p->noteOn = true;
      p->lastOnTime = millis();
    }
    p->lastStatus = status;
  }
}

void handleAnalog(instrument_t *p)
// Handle an instrument that has been designated as an analog type.
// The value is first detected when it exceeds the threshold, the max 
// reading is then used as the MIDI velocity, and finally the debounce 
// time is counted out before the next hit is allowed to be detected.
{
  uint16_t v = analogRead(p->pin);

  switch(p->state)
  {
  case START: // waiting for an activation
    if (v > getSensThreshold(p->sensPin))
    {
      PRINT("\nA", p->instrPort);
      PRINT(" ", v);
      p->lastStatus = (p->defVelocity ? p->defVelocity : v);
      p->state = (p->defVelocity ? PLAY : MEASURE);    // next state
    }
  break;
      
  case MEASURE: // capturing the maximum value of the activation
    if (v > p->lastStatus) p->lastStatus = v;
    if (v > getSensThreshold(p->sensPin))
      break;
    // otherwise fall through
      
  case PLAY: // play the instrument
    p->lastStatus >>= 3; // bring within range 0-127 (divide by 8)
    if (p->noteOn) midi.noteOff(p->kmValue);
    midi.noteOn(p->kmValue, p->lastStatus);
    p->noteOn = (p->activeTime != 0);   // don't record if timer is zero
    p->lastOnTime = millis();
    p->state = EXCLUDE;
  break;

  case EXCLUDE: // ignore signals during the exclusion time
    if (millis() - p->lastOnTime >= p->excludeTime)
      p->state = START;
  break;
      
  default:
    midi.noteOff(p->kmValue);
    p->state = START;
  break;
  }
}

void checkNoteOff(void)
// check if we have to send a note off message when time has expired
{
  uint32_t t = millis();

  for (uint8_t i=0; i<ARRAY_SIZE(PT); i++)
  {
    if ((PT[i].noteOn) && (t - PT[i].lastOnTime >= PT[i].activeTime))
    {
      midi.noteOff(PT[i].kmValue);
      PT[i].noteOn = false;
    }
  }
}

void setup(void)
{
  Serial.begin(SERIAL_RATE);
  PRINTS("\n[MIDI Percussion]");

  // initialize MIDI interface and devices
  midi.begin();
  midi.chanSoundOff();
  midi.chanNotesOff();
  midi.chanPolyOn();
  midi.ctlVolMSB(127);
  midi.progChange(0);  // GM1 standard drum kit

  // Set up the percussion hardware interface
  for (uint8_t i = 0; i < ARRAY_SIZE(PT); i++)
  {
    if (PT[i].type == INSTR_DIGITAL)
      pinMode(PT[i].pin, INPUT_PULLUP);
    else
      pinMode(PT[i].pin, INPUT);
    if (PT[i].sensPin != NO_PIN)
      pinMode(PT[i].sensPin, INPUT);
  }
}

void loop(void)
{
  // Check if any notes need to be turned off
  checkNoteOff();

  // Process each instrument
  for (uint8_t i = 0; i < ARRAY_SIZE(PT); i++)
  {
    switch(PT[i].type)
    {
    case INSTR_DIGITAL: handleDigital(&PT[i]); break;
    case INSTR_ANALOG:  handleAnalog(&PT[i]);  break;
    }
  }
}
