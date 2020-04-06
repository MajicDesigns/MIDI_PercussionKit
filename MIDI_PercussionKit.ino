/*
Arduino MIDI Percussion kit by MajicDesigns

Blog article: https://arduinoplusplus.wordpress.com/?p=10118

Outputs MIDI percussion messages based on the configured instruments for a 
specific digital or analog port. The instruments are defined in the 
Percussion Kit table (see below). No other changes to code should be necessary 
once the instrument parameters are defined.

If you like and use this code please consider making a small donation using
PayPal at https://paypal.me/MajicDesigns/4USD
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

type        Digital (INSTR_ANALOG) or analog (INSTR_DIGITAL) interface for the sensor. 
            - Digital signals are detected on a HIGH to LOW transition, so they
              need to be configured with PULL UP resistors.
            - Analog signals are standard analog 0-1024 value.

pin         The Arduino input pin to which the actuating sensor is connected.

activeTime  Time in ms that the note is active (ie, the time between NOTE_ON and 
            NOTE_OFF). Use 0 to forgo sending NOTE_OFF.

excludeTime Time in ms that the instrument is locked out after the note is played.
            This is used to mask out secondary signals (eg bounce) from the instrument
            once the initial signal is detected.

velocity    The default velocity of the MIDI note. This is required for digital 
            signals as it does not include a magnitude. If specified with an analog 
            input it overrides any velocity information derived from the real signal.

sensTrig    The analog input to read as the sensitivity threshold for the input value.
            This is ignored for digital sensors. For analog values only values greater
            than this setting will trigger the instrument.
*/

const uint8_t VAL_MAX = 127;    // max midi 7 bit value
const uint8_t NO_PIN = 0xff;    // no pin defined indicator 
typedef enum { INSTR_ANALOG, INSTR_DIGITAL } instr_t;
typedef enum { START, MEASURE, PLAY, EXCLUDE } state_t;

#define PIN(p) (p + VAL_MAX + 1)
#define VAL(v) (v)

typedef struct
{
  // Static data defining the instrument.
  uint8_t   kmValue;     // GM Percussion Key Map Value (eg, BASS_DRUM_1)
  instr_t   type;        // instrument Type (INSTR_ANALOG, INSTR_DIGITAL)
  uint8_t   pin;         // hardware pin number for the input, digital or analog
  uint16_t  activeTime;  // time between the NOTE_ON and NOTE_OFF messages in ms
  uint16_t  excludeTime; // time to exclude next activation for this instrument
  uint8_t   velocity;    // MIDI velocity setting. Setting required for INSTR_DIGITAL.
                         // Use 0 to infer velocity for analog instrument (INSTR_ANALOG) reading.
                         // Can be an analog pin or value. Use VAL() and PIN() macros to set the value.
  uint8_t   sensTrig;    // Sensor trigger threshold. Only valid for INSTR_ANALOG, ignored for INSTR_DIGITAL.
                         // Can be an analog pin or value. Use VAL() and PIN() macros to set the value.

  // Dynamic data created and used during run time.
  // This data does need to be statically initialized in the instrument table.
  bool      noteOn;      // true if NOTE_ON is current
  uint32_t  lastOnTime;  // last time the NOTE_ON was sent - saved millis() value
  uint16_t  lastValue;   // last value read for this instrument
  state_t   state;       // state for the FSM
} instrument_t;

// Global Data
instrument_t PT[] =  // Percussion Table
{
  // kmValue        Type          Pin Activ Excl      Vel    Sens
  { CRASH_CYMBAL_1, INSTR_DIGITAL,  7,    0,  50,  PIN(A4), VAL(0) },
  { COWBELL,        INSTR_DIGITAL,  6,    0,  50, VAL(127), VAL(0) },
  { LO_BONGO,       INSTR_DIGITAL,  4,    0,   0, VAL(127), VAL(0) },
  { HI_MID_TOM,     INSTR_ANALOG,  A0,  500,  50,   VAL(0), PIN(A5) },
};

midiComms midi(Serial);

// Control code follows --------------------------
uint16_t deRef(uint8_t v)
// unpack the values set by PIN() and VOL()
{
  uint16_t r;

  if (v > VAL_MAX)
  {
    r = analogRead(v - VAL_MAX - 1) >> 3;   // always needs to be within 0..127, divide by 8
    r = (r == 0 ? 1 : r);   // don't allow it to be 0
  }
  else
    r = v;

  return(r);
}

void handleDigital(instrument_t* p)
// Handle an instrument that has been designated as digital type.
// The value is either on or off and we need to detect a transition. 
{
  uint8_t value = digitalRead(p->pin);

  switch (p->state)
  {
  case START:
    // check for a HIGH to LOW transition
    if (value != p->lastValue)
    {
      PRINT("\nD Pin:", p->pin);
      PRINT(" Val:", value);
      if ((value == LOW) && (p->lastValue == HIGH))
        p->state = PLAY;
      p->lastValue = value;
    }
    break;

  case PLAY:
    if (p->noteOn) midi.noteOff(p->kmValue);  // switch it off if currently on
    midi.noteOn(p->kmValue, deRef(p->velocity));  // now turn the note on
    p->noteOn = (p->activeTime != 0);         // don't record if timer is zero
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

void handleAnalog(instrument_t *p)
// Handle an instrument that has been designated as an analog type.
// The value is first detected when it exceeds the threshold, the max 
// reading can then be used as the MIDI velocity, and finally the debounce 
// time is counted out before resetting for next hit detection.
{
  uint16_t v = analogRead(p->pin);

  switch(p->state)
  {
  case START: // waiting for an activation
    if (v > deRef(p->sensTrig))
    {
      uint16_t vel = deRef(p->velocity);

      PRINT("\nA P:", p->pin);
      PRINT(" Trig:", deRef(p->sensTrig));
      PRINT(" Lvl:", v);
      PRINT(" Vel:", vel);
      // if the velocity is 0, then we need to measure the height of the signal
      // to infer the velocity. Otherwise, use the value specified in the table.
      p->lastValue = (vel == 0 ? v : vel);
      p->state = (vel == 0 ? MEASURE : PLAY);    // next state
      if (p->state == PLAY) PRINTS(" - to Play") else PRINTS(" - to Meas")
    }
  break;
      
  case MEASURE: // capturing the maximum value of the activation
    if (v > p->lastValue)
      p->lastValue = v;
    else
    {
      PRINT(" velMeas:", p->lastValue);
      p->state = PLAY;
    }
    break;
      
  case PLAY: // play the instrument
    if (p->noteOn) midi.noteOff(p->kmValue);  // if it was on, turn it off
    midi.noteOn(p->kmValue, p->lastValue);    // play the note
    p->noteOn = (p->activeTime != 0);         // don't record if timer is zero
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

    // Set velocity and sensTrig pins only if they are pins.
    // These fields are packed using VAL() and PIN().
    if (PT[i].velocity <= VAL_MAX)
      pinMode(PT[i].velocity, INPUT);
    if (PT[i].sensTrig <= VAL_MAX)
      pinMode(PT[i].sensTrig, INPUT);
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
