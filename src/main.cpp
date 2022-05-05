#include <Arduino.h>

// LED
//#define FASTLED_ESP8266_RAW_PIN_ORDER
#include <Wire.h>
#include "FastLED.h"
// TouchSensor
#include "MPR121.h"



// #define  __WIFI__

#ifdef __WIFI__

  #include <ESP8266WiFi.h>
  #include <ArduinoOTA.h>

  const char* ssid     = "Lentilka";
  const char* password = "Kvetinka42";

#endif

// WIFI
// JSON
// MQTT...

// todo

// integrate OTA
// integrate WifiManager
// integrate some HomeAssistant MQTT device
// by this - set up on color/ off color
//         - speed of animation
//         - sensitivity of touch sensor


////////////////////
//
// HARDWARE SETTINGS 
//
/////////////////////

#define TOUCH_CHIPS_COUNT 3
#define MAX_TOUCH_CHIPS_COUNT 4
#define TOUCH_PER_CHIPS_COUNT 12

#define LED_COUNT   35

#define LED_DATA_PIN D5

byte irq_pins[MAX_TOUCH_CHIPS_COUNT] = {
  D7,
  D4,
  D3,
  D7
};

/*
chip 1 - 5A
chip 2 - shorted and connected to SCL
chip 3 - shorted and connected to 3.3V
*/

byte addresses[MAX_TOUCH_CHIPS_COUNT] = {
  0x5A, // ADD=GND (default) 5A
  0x5D, // ADD=VCC 5B 
  0x5B, // ADD=SDA 5C
  0x5C, // ADD=SCL 5D
};

////////////////////
//
// SOFTWARE SETTINGS
//
/////////////////////

// initial colors, can be store somewhere in EPROM
unsigned long updateSpeed = 10; // how often to update in ms
unsigned long upSpeed = 500;
unsigned long downSpeed = 3000;

 // initial colors, can be store somewhere in EPROM
CRGB onColor(0,20,255); 
CRGB offColor(0,0,0);

char tresholdTouch = 3;  //15
char tresholdRelease = 6;  //4

bool demo;
unsigned long LEDSwitchSpeed = 1000;
long ledIndex;

MPR121 sensors[TOUCH_CHIPS_COUNT];

CRGB leds[LED_COUNT];
float percValueArr[LED_COUNT];          // percent value of final color.
bool directionValueArr[LED_COUNT];    // bool - direction if we are going to 100% or to 0%

// some mapping what touch chip and index correspond to what LED
int ledIndexforTouch[TOUCH_CHIPS_COUNT][TOUCH_PER_CHIPS_COUNT];


//
//
//  gamma table
//
//

const uint8_t gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };


//////////////////////////////////////////////////////////////////////////////
//
//   timer - loop goes and throught this setup, run some task in interval.
//
//////////////////////////////////////////////////////////////////////////////

typedef struct
{
  unsigned long timeout;
  unsigned long intervalTime;
  unsigned long timeFromLastRun;
  bool run;
} intervalTimer;

void SetupInterval(intervalTimer& timer, unsigned long intervaltime, bool imediateRun = false)
{
  timer.run = false;
  timer.timeout = imediateRun ? -intervaltime : 0;
  timer.intervalTime = intervaltime;
}

bool processInterval(intervalTimer &timer, unsigned long ms)
{
  if (ms - timer.timeout >= timer.intervalTime)
  {
    timer.timeFromLastRun = ms - timer.timeout;
    timer.timeout =  ms;
    if (timer.run == false)
    {
      timer.run = true;   return true;
    }
  }

  timer.run = false;  return false;
}

void resetInterval(intervalTimer &timer, unsigned long ms)
{
	timer.timeout = ms;
}

// variable
intervalTimer updateTimer;
intervalTimer LEDDemoTimer;

//////////////////////////////////////////////////////////////////////////////
//
//   Initialize all arrays, values and more.
//
//////////////////////////////////////////////////////////////////////////////

void initArrays()
{
  for (int i=0; i<LED_COUNT; i++)
  {
    percValueArr[i] = 0;
    directionValueArr[i] = false;
  }

// can be set up by fixed array (if you mix touch on table differently)
  int ledIndex=0;
  for (int i=0; i<TOUCH_CHIPS_COUNT; i++)
  {
    for (int j=0; j<TOUCH_PER_CHIPS_COUNT; j++)
    {
      ledIndexforTouch[i][j] = ledIndex;
      ledIndex++;
    }
  }
}

void updateLEDS(unsigned long updateTime)
{
  // somehow read, and depending on speed - update perc array
  for (int i=0; i<LED_COUNT; i++)
  {

    if (directionValueArr[i])
    {
        // update up  
        percValueArr[i] = percValueArr[i] + ((float)updateTime/ (float)upSpeed *100.0f);
        if (percValueArr[i] > 100)
            percValueArr[i] = 100;
    } else
    {
        // update down
        percValueArr[i] = percValueArr[i] - ((float)updateTime/(float)downSpeed * 100.0f);
        if (percValueArr[i] < 0)
            percValueArr[i] = 0;
    }

    int R = offColor.r - (offColor.r - onColor.r)/100.0f * percValueArr[i];
    int G = offColor.g - (offColor.g - onColor.g)/100.0f * percValueArr[i];
    int B = offColor.b - (offColor.b - onColor.b)/100.0f * percValueArr[i];

    CRGB color =  CRGB(gamma8[G],gamma8[B],gamma8[B]);
    leds[i] = color;
  }

}

void initTouch()
{  
 Serial.println(" init wire: ");
 Wire.begin();  
 Serial.println(" init touch chips: ");

 for (byte sensor = 0; sensor < TOUCH_CHIPS_COUNT; sensor++) 
 {
   Serial.print(" init chips: ");
   Serial.println(sensor);
    sensors[sensor] = MPR121(
                             irq_pins[sensor],  // triggered/interupt pin
                             false,             // interrupt mode?
                             addresses[sensor], // START_ADDRESS = 0x5A
                             true,              // use touch times
                             false              // use auto config and reconfig
                             );
    //
    //  Set default touch and release thresholds
    //
    Serial.println(" init chips set up Tresholds");
    for (byte i = 0; i < MPR121::MAX_SENSORS; i++) 
    {
      sensors[sensor].setThreshold(i, tresholdTouch, tresholdRelease);
    }  
  }
  Serial.println(" init chips DONE");
}


void updateTouch()
{
  
  for (int i=0; i<TOUCH_CHIPS_COUNT; i++)
  {
    if (sensors[i].readTouchInputs()) 
    {
      Serial.print(" readed touch: ");
      Serial.print(i);
      for (int j = 0; j < TOUCH_PER_CHIPS_COUNT; j++) 
      {
        if (sensors[i].changed(j)) 
        {
          Serial.print(" changed: ");
          Serial.print(j);

          long led = ledIndexforTouch[i][j];

          Serial.print(" LED: ");
          Serial.println(led);

          if (led < LED_COUNT)
          {
            if (sensors[i].touched(j)) 
            {            
            
              directionValueArr[led] = true;
            } else 
            {
              directionValueArr[led] = false;
            }
          }
        }
      }
    }
  }
}

void setOTA() {
  #ifdef __WIFI__
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname("LEDTable");
    ArduinoOTA.setPassword("OTAPASSWORD");
    ArduinoOTA.begin();
  #endif
}

/*****************************************************************************/
/****************** SETUP  ***************************************************/
/*****************************************************************************/

void setup() 
{

  Serial.begin(9600);
  Serial.println("Init Arrays");
  initArrays();

  ledIndex = 0;
  demo = false; 

  if (!demo)
  {
	  Serial.println("Init Touch");
   initTouch();
  }

  SetupInterval(updateTimer, updateSpeed);
  SetupInterval(LEDDemoTimer, LEDSwitchSpeed);

 // SetupInterval(StatusTimer, LEDSwitchSpeed);

  Serial.println("Inited LEDS");
  FastLED.setDither(0);
  FastLED.addLeds<WS2812B, LED_DATA_PIN>(leds, LED_COUNT);  

#ifdef __WIFI__
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

#endif

  Serial.println("Set OTA");
  setOTA();
  
  Serial.println("Setup Done");
}

/*****************************************************************************/
/****************** LOOP  ****************************************************/
/*****************************************************************************/
long firstLoop = 10;
void loop() {
 if (firstLoop > 0)
 {
    firstLoop--;
    Serial.print("Status -< ");
    Serial.println(firstLoop);
 }

  unsigned long currentMillis = millis();
 
  if (processInterval(updateTimer, currentMillis))
  {
    // count time from last run - to ensure animation flowless ( update only for a exact time that passed from last run. )
    // but still, can be set to run 1 step of animation for every 100ms ( for example ), can be set to 0 - go every time off course.

    if (demo)
    {
      if (processInterval(LEDDemoTimer, currentMillis))
      {
        directionValueArr[ledIndex == 0? LED_COUNT-1 : ledIndex] = false;        
        ledIndex++;
        if (ledIndex > LED_COUNT)
        {          
          ledIndex = 0;
        }
        directionValueArr[ledIndex] = true;
        Serial.print(" LED : ");
        Serial.println(ledIndex);
      }        
    } else
    {
      updateTouch();      
    }

    {
      updateLEDS(updateTimer.timeFromLastRun);
      FastLED.show();
    }
  }

  // magic with wifi ? can it be runned in different thread ?

  #ifdef __WIFI__
    if (WiFi.status() == WL_CONNECTED) 
    {
      ArduinoOTA.handle();
    }
  #endif
}



