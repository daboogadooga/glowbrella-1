/*--------------------------------------------------------------------------
 For IRIS, the glowbrella
  --------------------------------------------------------------------------*/

#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_GFX.h>

// NEOPIXEL STUFF ----------------------------------------------------------

// Some nice fixed values
#define KNOB_MIN      0
#define KNOB_MAX      1023
#define MAX_BRIGHTNESS  230

#define NEO_PIN        6 // Arduino pin to NeoPixel data input
#define NEO_WIDTH     40 // 19 // this one is test strip//40 // Hat circumference in pixels
#define NEO_HEIGHT    8  // Number of pixel rows (round up if not equal)
#define NEO_OFFSET  (((NEO_WIDTH * NEO_HEIGHT) - 240) / 2)
#define TOP_BUTTON_PIN 2 // pin for the first/top button
#define BOT_BUTTON_PIN 4 // pin for the second/bottom button
#define TOP_KNOB_PIN  A0 // pin for first/top knob
#define BOT_KNOB_PIN  A1 // pin for second/bottom knob
#define SOUND_SENSOR_PIN A2  // pin for sound sensor

Adafruit_NeoMatrix matrix(NEO_WIDTH, NEO_HEIGHT, NEO_PIN,
  NEO_MATRIX_TOP  + NEO_MATRIX_LEFT +
  NEO_MATRIX_ROWS + NEO_MATRIX_PROGRESSIVE,
  NEO_GRB         + NEO_KHZ800);

unsigned long prevFrameTime = 0L;             // For animation timing



///////////
// Type Declarations
///////////
typedef int (*runFn)(int delta); // Some runner function

// Buttons and knobs state variables
int topButtonState = 0;
int botButtonState = 0;
int topKnobState = 0;
int botKnobState = 0;
int soundSensorValue = 0;

// Deltas per-tick
int topKnobDelta = 0;
int botKnobDelta = 0;


// UTILITY FUNCTIONS -------------------------------------------------------

int EFFECT_STYLE = 0;       // zero means blank screen
uint16_t color = 0xF800;    // Red by default
static int brightness = 31; // batteries have limited sauce
static int soundThreshold = 50; // To adjust the sensitivity of the sound sensor

// Getter for top button pressed
bool topButtonDown()
{
	topButtonState = digitalRead(TOP_BUTTON_PIN);
	return topButtonState == LOW;
}

bool botButtonDown()
{
  return digitalRead(BOT_BUTTON_PIN) == HIGH;
}

int getSoundSensor()
{
  return analogRead(SOUND_SENSOR_PIN);
}

int getTopKnob()
{
  return analogRead(TOP_KNOB_PIN);
}

int getBotKnob()
{
  return analogRead(BOT_KNOB_PIN);
}

void clearPartialCol(int startIndex, int length, int row)
{
  for (int i = startIndex; i < length; i++)
  {
    if (i >= matrix.width()) break; // Done early if we go bigger than we can
    matrix.drawPixel(i, row, 0);
  }
}

// MEAT, POTATOES ----------------------------------------------------------

void setup() {
  randomSeed(1337); // because fuck you that's why
  Serial.begin(9600);
  matrix.begin();
  matrix.setBrightness(brightness);    // Batteries have limited sauce

  // Gettin' pinny with it
  pinMode(TOP_BUTTON_PIN, INPUT);
  pinMode(TOP_KNOB_PIN, INPUT);
  pinMode(BOT_BUTTON_PIN, INPUT);
  pinMode(BOT_KNOB_PIN, INPUT);
  pinMode(SOUND_SENSOR_PIN, INPUT);
}


// effects is an array of run functions, each can be stateful
// the & is a reference to the function -- remember, runFn is a function pointer
runFn effects[14] = {
  	&clear,
        &fill,
        &music,
        &music2,
        &pulse,
        &party,
        &partySolids,
        &hart,
        &dante,
	&theaterChase,
	&theaterChaseRainbow,
	&rainbow,
        &spiral,
	&chase
};


// Precompute the total number of effects & keep track of current effect
int numEffects = sizeof(effects) / sizeof(runFn);
int currentEffect = 0;

// Helper to update button deltas
void updateInputDeltas()
{
  static int lastBotKnob = 0;
  static int lastTopKnob = 0;
  
  topKnobDelta = getTopKnob() - lastTopKnob;
  botKnobDelta = getBotKnob() - lastBotKnob;

  lastTopKnob = getTopKnob();
  lastBotKnob = getBotKnob();
}

// Step the current effect + respond to input
void loop()
{
  static int nextEffectTick = 0;
  unsigned long t = millis(); // Current elapsed time, milliseconds.
  int tickDelta = (int) (t - prevFrameTime);
  updateInputDeltas();
  color = Wheel(int(getTopKnob()/4));
  // Check if the button is pressed, if so advance current effect with loop
  if (botButtonDown())
  {
    currentEffect = (currentEffect + 1) % numEffects;
    nextEffectTick = 0;
    clear(10);
    delay(300);
  }
  int b = checkTopButton();

  // Get button event and act accordingly
   if (b == 1) clickEvent();
   if (b == 2) doubleClickEvent();
   if (b == 3) holdEvent();

  
  // Dereference + grab current run function and tick it
  runFn current = *effects[currentEffect];
  if (nextEffectTick > 0)
  {
    nextEffectTick -= tickDelta;
  }
  
  if (nextEffectTick <= 0)
  {
    nextEffectTick = current(tickDelta); // Do something on failure if you like
  }
  
  // Save t for next time --> @todo: should be passing time delta to each function
  prevFrameTime = t;
}

//=================================================
// Top Button Events to trigger

// single click adjusts the brightness
void clickEvent() {
   brightness = int(getBotKnob()/4);
   matrix.setBrightness(brightness);
}
// double click sets a new sound threshold
void doubleClickEvent() {
   soundThreshold = int(getBotKnob()/4);
}
//@TO-DO press and hold adjusts the speed of the effects
void holdEvent() {

}

//=================================================
//  MULTI-CLICK:  One Button, Multiple Events

// Button timing variables
int debounce = 20;          // ms debounce period to prevent flickering when pressing or releasing the button
int DCgap = 250;            // max ms between clicks for a double click event
int holdTime = 1000;        // ms hold period: how long to wait for press+hold event

// Button variables
boolean buttonVal = HIGH;   // value read from button
boolean buttonLast = HIGH;  // buffered value of the button's previous state
boolean DCwaiting = false;  // whether we're waiting for a double click (down)
boolean DConUp = false;     // whether to register a double click on next release, or whether to wait and click
boolean singleOK = true;    // whether it's OK to do a single click
long downTime = -1;         // time the button was pressed down
long upTime = -1;           // time the button was released
boolean ignoreUp = false;   // whether to ignore the button release because the click+hold was triggered
boolean waitForUp = false;        // when held, whether to wait for the up event
boolean holdEventPast = false;    // whether or not the hold event happened already

int checkTopButton() {    
   int event = 0;
   buttonVal = topButtonDown();
   // Button pressed down
   if (buttonVal == LOW && buttonLast == HIGH && (millis() - upTime) > debounce)
   {
       downTime = millis();
       ignoreUp = false;
       waitForUp = false;
       singleOK = true;
       holdEventPast = false;
       if ((millis()-upTime) < DCgap && DConUp == false && DCwaiting == true)  DConUp = true;
       else  DConUp = false;
       DCwaiting = false;
   }
   // Button released
   else if (buttonVal == HIGH && buttonLast == LOW && (millis() - downTime) > debounce)
   {        
       if (not ignoreUp)
       {
           upTime = millis();
           if (DConUp == false) DCwaiting = true;
           else
           {
               event = 2;
               DConUp = false;
               DCwaiting = false;
               singleOK = false;
           }
       }
   }
   // Test for normal click event: DCgap expired
   if ( buttonVal == HIGH && (millis()-upTime) >= DCgap && DCwaiting == true && DConUp == false && singleOK == true && event != 2)
   {
       event = 1;
       DCwaiting = false;
   }
   // Test for hold
   if (buttonVal == LOW && (millis() - downTime) >= holdTime) {
       // Trigger "normal" hold
       if (not holdEventPast)
       {
           event = 3;
           waitForUp = true;
           ignoreUp = true;
           DConUp = false;
           DCwaiting = false;
           //downTime = millis();
           holdEventPast = true;
       }
   }
   buttonLast = buttonVal;
   return event;
}

//=====================================================
// Effects to cycle through

//Theatre-style crawling lights with rainbow effect
int theaterChaseRainbow(int delta)
{
	static int wait = 50;
	static int j = 0;
	static int q = 0;

	// Per tick logic
	for (int i = 0; i < matrix.numPixels(); i += 3)
	{
		// Toggle every third pixel
		matrix.setPixelColor(i + q, Wheel(j));
	}

	matrix.show(); 

	for (int i = 0; i < matrix.numPixels(); i += 3)
	{
		// Toggle every third pixel
		matrix.setPixelColor(i + q, 0);
	}

	// Pulling the for loop into a recursion section
	q++;
	if (q >= 3)
	{
		j = (j + 1) % 256;
		q = 0;
	}

	return wait;
}

//Theatre-style crawling lights.
int theaterChase(int delta)
{
  // State
  static uint16_t state = 0;
  static int wait = 50;
  static int j = 0;
  static int q = 0;

  // Tick
  if (state == 0)
  {
    for (int i = 0; i < matrix.width(); i += 3) {
      matrix.drawFastVLine(i + q, 0, matrix.height(), color);
    }

    state = 1;
  } else if (state == 1) {
    state = 0;

    for (int i = 0; i < matrix.width(); i += 3) {
      matrix.drawFastVLine(i + q, 0, matrix.height(), 0);
    }
  }
  matrix.show();

  // Advance
  q++;
  if (q >= 3)
  {
    j = (j + 1) % 8;
    q = 0;
  }

  return wait;
}

//int x[4] = {1, 5, 3, 2};

/**
* Let's make some rainbows, h-rat
* @param {int} delta - The time delta from the last global tick
* @return How long the main loop should delay
*/
int hart(int delta)
{
  // State variables for each raindrop -- row, column, and color -- max drops = NEO_WIDTH
  static int dropRow[NEO_WIDTH] = { -1 };
  static int dropCol[NEO_WIDTH] = { -1 };
  static int dropColor[NEO_WIDTH] = { -1 };
  // variable to track the next index I should add a drop
  static int head = 0;
  // Am I waiting for longer?
  static int wait = 0;
  // How much I move back in the color wheel
  int step = 10;
  
  // Trigger on sound
  int sound = getSoundSensor();
  if (sound > soundThreshold)
  {
    // Event captured, add the drop!!
    // @todo: if any of those are NOT -1, then I should clear their state, because I just stole their index!!!
    dropCol[head] = random(NEO_WIDTH);
    dropRow[head] = random(NEO_HEIGHT);
    dropColor[head] = random(255);
    head = (head + 1) % NEO_WIDTH;
  }
  
  // Delay <= 0? TICK THAT SHIT
  wait -= delta;
  if (wait <= 0)
  {
    // Do logic
    for (int i = 0; i < NEO_WIDTH; i++)
    {
      // Oh. Not that drop? SKIP IT
      if (dropCol[i] == -1 || dropRow[i] == -1) continue;

      // DO DAT FADE
      if (dropCol[i] > 3)
        matrix.drawPixel(dropCol[i] - 4, dropRow[i], 0);
      if (dropCol[i] > 2)
        matrix.drawPixel(dropCol[i] - 3, dropRow[i], Wheel(((byte) (dropColor[i] - 3 * step))));
      if (dropCol[i] > 1)
        matrix.drawPixel(dropCol[i] - 2, dropRow[i], Wheel(((byte) (dropColor[i] - 2 * step))));
      if (dropCol[i] > 0)
        matrix.drawPixel(dropCol[i] - 1, dropRow[i], Wheel(((byte) (dropColor[i] - step))));

      // Set the color of this drop
      matrix.drawPixel(dropCol[i], dropRow[i], Wheel((byte) dropColor[i]));
      
      // ADVANCE
      dropCol[i]++;

      // Oh, should I remove?
//      if (dropCol[i] >= NEO_WIDTH)
//      {
//        // @todo: clear pixel range fn
//        clearPartialCol(i, length, row);
//      }
    }
    
    wait = 25;
  }

  matrix.show();
  return 0;
}

// Just some colors with sound
int dante(int delta)
{
  static int nextTick = 0;
  static int points[NEO_WIDTH] = { -1 };
  static uint32_t colors[NEO_WIDTH] = { 0 };
  static int head = 0;
  
  int sound = getSoundSensor();
  bool triggered = sound > soundThreshold;
  
  if (triggered)
  {
    points[head] = 0;
    colors[head] = Wheel((byte) random(255));
    head = (head + 1) % NEO_WIDTH;
  }
  
  if (nextTick <= 0)
  {
    for (int i = 0; i < NEO_WIDTH; i++)
    {
      if (points[i] == -1) continue;

      if (points[i] >= NEO_WIDTH)
      {
        matrix.drawFastVLine(points[i] - 1, 0, matrix.height(), 0);
        points[i] = -1;
      }

      matrix.drawFastVLine(points[i], 0, matrix.height(), colors[i]);

      if (points[i] > 0)
      {
        matrix.drawFastVLine(points[i] - 1, 0, matrix.height(), 0);
      }

      points[i]++;
    }
    nextTick = 25;
  }
  nextTick -= delta;
  
  matrix.show();
  
  return 0;
}

int party(int delta)
{
  static int delay = 0;
  
  if (delay >= 0)
  {
    delay -= delta;
  }

  if (delay <= 0)
  {
    for (int x = 0; x < NEO_WIDTH; x++)
    {
      for (int y = 0; y < NEO_HEIGHT; y++)
      {
        matrix.drawPixel(x, y, Wheel(random(255)));
      }
    }
    delay = 10;
  }
  
  matrix.show();

  return 30;
}
int partySolids(int delta)
{
  static int delay = 0;
  static int randomColor = 0;
  if (delay >= 0)
  {
    delay -= delta;
  }
  randomColor = Wheel(random(255));
  if (delay <= 0)
  {
    for (int x = 0; x < NEO_WIDTH; x++)
    {
      for (int y = 0; y < NEO_HEIGHT; y++)
      {
        matrix.drawPixel(x, y, randomColor);
      }
    }
    delay = 10;
  }
  
  matrix.show();

  return 30;
}

// Effect to clear the screen
int clear(int delta)
{
	matrix.fillScreen(0);
	matrix.show();

	return 0;
}

int bored(int delta)
{
  static int next = 0;
  
}

// Fill the screen with the current color
int fill(int delta)
{
	matrix.fillScreen(color);
	matrix.show();

	return 0;
}

// Rainbow effect
int rainbow(int delta)
{
	static int wait = 50;
	static uint16_t j = 0;

	for (int i = 0; i < matrix.width(); i++)
	{
		matrix.drawFastVLine(i, 0, matrix.height(), Wheel((i+j) & 255));
	}

	matrix.show();

	j = (j + 1) % 256;


	return wait;
}

// Chase effect
int chase(int delta)
{
	static uint16_t j = 0;
	static uint16_t i = 0;

	matrix.drawPixel(i, j, color);
	matrix.drawPixel(i - 5, j, 0);
	matrix.show();

	i++;
	if (i >= matrix.width() + 5)
	{
		i = 0;
		j = (j + 1) % matrix.height();
	}

	return 25;
}

int pulse(int delta)
{
  static int brightnessMod = 0;
  static boolean brighter = true;
  if(brighter){ 
  if (brightnessMod <=100){
    brightnessMod +=1;
    matrix.setBrightness(brightnessMod);
  } else {
      brighter = false;
    }
  } else {
    brightnessMod -=1;
    matrix.setBrightness(brightnessMod);
    if (brightnessMod<1)
    {
      brighter = true;
    }
  }
  matrix.fillScreen(color);
  matrix.show();
  
  return 5;
}

// Respond to some music my nigga'
int music(int delta)
{
  static int brightnessMod = 0;

  int sound = getSoundSensor();

  if (sound > soundThreshold)
  {
    brightnessMod = 100;
  }
  
  if (brightnessMod > 0)
  {
    matrix.fillScreen(color);
    matrix.setBrightness(brightnessMod);
    matrix.show();
    brightnessMod-=3;
    
    if (brightnessMod <=0)
    {
      brightnessMod = 0;
      clear(10);
    }
  }
  return 0;
}

int music2(int delta)
{
  int sound = getSoundSensor();
  static int i = matrix.width();
  if (sound > soundThreshold)
  {
    i = 0;
  }
  if(i < matrix.width())
  {
    matrix.drawFastVLine(i, 0, matrix.height(), color);
    matrix.drawFastVLine(i-1, 0, matrix.height(), 0);
    matrix.show();
    i++;
  }
  
  return 25;
}

int spiral(int delta)
{
  static int offset = 0;
  
//  NEO_HEIGHT // rotation
//  NEO_WIDTH // distance from center

  int i = 0;
  int r = 0;
  int c = offset;
  while (true)
  {
    matrix.drawPixel(r, c, Wheel(random(255)));
    c = (c + 1) % NEO_HEIGHT; 
    r++;
    if (r > NEO_WIDTH) break;
  }
  
  offset %= NEO_HEIGHT;
  return 0;
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
   return matrix.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return matrix.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
   WheelPos -= 170;
   return matrix.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}
