// candle for Adafruit NeoPixel
// 1 pixel version
// by Tim Bartlett, December 2013

/*

Modified for Particle Photon June 2016 by Jim Brower
1: can turn on/off with a Particle.function()
2: automatically turns on at Sunset and off at programmed time
3:Uses "sun_time" webhook for weatherunderground sunrise/sunset times:

{
	"event": "sun_time",
	"url": "http://api.wunderground.com/api/getYourOwnApiKey/astronomy/q/{{my-state}}/{{my-city}}.json",
	"requestType": "POST",
	"headers": null,
	"query": null,
	"responseTemplate": "{{#sun_phase}}{{sunrise.hour}}~{{sunrise.minute}}~{{sunset.hour}}~{{sunset.minute}}~{{#moon_phase}}{{current_time.hour}}~{{current_time.minute}}{{/moon_phase}}~{{/sun_phase}}",
	"responseTopic": "{{SPARK_CORE_ID}}_sun_time",
	"json": null,
	"auth": null,
	"coreid": null,
	"deviceid": null,
	"mydevices": true
}

*/

#include "neopixel.h"
//#include "application.h"

#define PIXEL_COUNT 1
#define PIXEL_PIN D2
#define PIXEL_TYPE WS2812B    // USING GRB WS2821B's here

SYSTEM_THREAD(ENABLED);

// color variables: mix RGB (0-255) for desired yellow
int redPx = 255;
int grnHigh = 100; //110-120 for 5v, 135 for 3.3v
int bluePx = 10; //10 for 5v, 15 for 3.3v

// animation time variables, with recommendations
int burnDepth = 10; //10 for 5v, 14 for 3.3v -- how much green dips below grnHigh for normal burn -
int flutterDepth = 25; //25 for 5v, 30 for 3.3v -- maximum dip for flutter
int cycleTime = 120; //120 -- duration of one dip in milliseconds

// pay no attention to that man behind the curtain
int fDelay;
int fRep;
int flickerDepth;
int burnDelay;
int burnLow;
int flickDelay;
int flickLow;
int flutDelay;
int flutLow;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, PIXEL_TYPE);

int powerState = -1;
int lastTimerState = -1;
int lastState = -1;

struct TimerTime{
  int theHour;
  int theMinute;
};

TimerTime sunrise = { 6, 15};
TimerTime sunset = {10, 30};
TimerTime onTime = {17, 30};
TimerTime offTime = { 2, 30};

const char* cityLocation = "Princeton";  //City for my Photon
const char* stateLocation = "NJ";     // State for my Photon
const char* room = "Living Room";
const int sunsetOffset = -2;

String responseTopic;
char sunriseBuffer[256] = "undefined";
char publishString[125] = "";
char currentTime[6] = "24:00";
void setup()
{
  flickerDepth = (burnDepth + flutterDepth) / 2.4;
  burnLow = grnHigh - burnDepth;
  burnDelay = (cycleTime / 2) / burnDepth;
  flickLow = grnHigh - flickerDepth;
  flickDelay = (cycleTime / 2) / flickerDepth;
  flutLow = grnHigh - flutterDepth;
  flutDelay = ((cycleTime / 2) / flutterDepth);
  //WiFi.setCredentials("OT325", "5E90B66FC6", WEP);
  strcpy(publishString, "{\"my-city\": \"");
  strcat(publishString, cityLocation);
  strcat(publishString, "\", \"my-state\": \"");
  strcat(publishString, stateLocation);
  strcat(publishString, "\" }");
  responseTopic = System.deviceID();
  Particle.subscribe(responseTopic, webhookHandler, MY_DEVICES);
  Particle.function("CrystalPower", powerFunction);
  Particle.function("SetTimes", setTimes);
  Particle.variable("Power", powerState);
  Particle.variable("currentTime", currentTime);
  Particle.variable("SunriseInfo", sunriseBuffer);
  strip.begin();
  strip.setPixelColor(0, 255, 0, 0);
  strip.show();
  int startBurn = millis();
  while(millis() - startBurn < 1000UL)
  {
    Particle.process();
  }
  Particle.publish("sun_time", publishString, 60, PRIVATE);
}

// In loop, call CANDLE STATES, with duration in seconds
// 1. on() = solid yellow
// 2. burn() = candle is burning normally, flickering slightly
// 3. flicker() = candle flickers noticably
// 4. flutter() = the candle needs air!

void loop()
{
  int timerState = (timerEvaluate()? 1 : 0);
  if(timerState != lastTimerState)
  {
    powerState = timerState;
    pushoverPowerMessage("Timer");
    char pushoverMessage[128] = "Timer set ";
    strcat(pushoverMessage, room);
    strcat(pushoverMessage, " crystal ");
    strcat(pushoverMessage, powerState? "ON" : "OFF");
    Particle.publish("pushover", pushoverMessage, 60, PRIVATE);
  }
  lastTimerState = timerState;
  static uint32_t flickMode = 0;
  if(powerState == 1)
  {
    switch (flickMode % 10)
    {
      case 0:
        burn(random(4, 10));
        break;
      case 1:
        flicker(random(4, 10));
        break;
      case 2:
        burn(random(4, 10));
        break;
      case 3:
        flutter(random(4, 10));
        break;
      case 4:
        burn(random(4, 10));
        break;
      case 5:
        on(random(4, 10));
        break;
      case 6:
        burn(random(4, 10));
        break;
      case 7:
        flicker(random(4, 10));
        break;
      case 8:
        on(random(4, 10));
        break;
      case 9:
        flutter(random(4, 10));
        break;
    }
    flickMode++;
  }
  else
  {
    if(lastState != powerState)
    {
      strip.setPixelColor(0, 0, 0, 0);
      strip.show();
    }
  }
  lastState = powerState;
  [](){static uint32_t webhookTimer = 0; if(millis() - webhookTimer > 60 * 60000UL){Particle.publish("sun_time", publishString, 60, PRIVATE); webhookTimer = millis();}}();
  [](){static uint32_t clockTimer = 0; if(millis() - clockTimer > 1000UL){char timeBuffer[6] = ""; sprintf(timeBuffer, "%02d:%02d", Time.hour(), Time.minute()); strcpy(currentTime, timeBuffer); clockTimer = millis();}}();
}

// basic fire function - not called in main loop
void fire(int grnLow) {
  for (int grnPx = grnHigh; grnPx > grnLow; grnPx--) {
    strip.setPixelColor(0, grnPx, redPx, bluePx);
    strip.show();
    delay(fDelay);
  }
  for (int grnPx = grnLow; grnPx < grnHigh; grnPx++) {
    strip.setPixelColor(0, grnPx, redPx, bluePx);
    strip.show();
    delay(fDelay);
  }
}

// fire animation
void on(int f) {
  fRep = f * 1000;
  int grnPx = grnHigh - 5;
  strip.setPixelColor(0, grnPx, redPx, bluePx);
  strip.show();
  delay(fRep);
}

void burn(int f) {
  fRep = f * 8;
  fDelay = burnDelay;
  for (int var = 0; var < fRep; var++) {
    fire(burnLow);
  }
}

void flicker(int f) {
  fRep = f * 8;
  fDelay = burnDelay;
  fire(burnLow);
  fDelay = flickDelay;
  for (int var = 0; var < fRep; var++) {
    fire(flickLow);
  }
  fDelay = burnDelay;
  fire(burnLow);
  fire(burnLow);
  fire(burnLow);
}

void flutter(int f) {
  fRep = f * 8;
  fDelay = burnDelay;
  fire(burnLow);
  fDelay = flickDelay;
  fire(flickLow);
  fDelay = flutDelay;
  for (int var = 0; var < fRep; var++) {
    fire(flutLow);
  }
  fDelay = flickDelay;
  fire(flickLow);
  fire(flickLow);
  fDelay = burnDelay;
  fire(burnLow);
  fire(burnLow);
}

void webhookHandler(const char *event, const char *data)
{
  if (strstr(event, "sun_time"))
  {
    gotSunTime(event, data);
  }
}

void gotSunTime(const char * event, const char * data)
{
  //sunriseBuffer = "";
  strcpy(sunriseBuffer, data);
  sunrise.theHour = atoi(strtok(sunriseBuffer, "\"~"));
  sunrise.theMinute = atoi(strtok(NULL, "~"));
  sunset.theHour = atoi(strtok(NULL, "~"));
  sunset.theMinute = atoi(strtok(NULL, "~"));
  int currentHour = atoi(strtok(NULL, "~"));
  int currentMinute = atoi(strtok(NULL, "~"));
  Time.zone(0);
  Time.zone(utcOffset(Time.hour(), currentHour));
  sprintf(sunriseBuffer, "%s, %s Sunrise: %02d:%02d, Sunset: %02d:%02d, Sunset Offset = %d, Off Time: %02d:%02d, Last Updated: %02d:%02d", cityLocation, stateLocation, sunrise.theHour, sunrise.theMinute, sunset.theHour, sunset.theMinute, sunsetOffset, offTime.theHour, offTime.theMinute, currentHour, currentMinute);
  //Particle.publish("pushover", sunriseBuffer, 60, PRIVATE);
  sunset.theHour += sunsetOffset; // let's come on before sunset. <<<<<<<<<<<<<<<<<<<<<<<< IMPORTANT
}

int utcOffset(int utcHour, int localHour)  // sorry Baker Island, this won't work for you (UTC-12)
{
  if (utcHour == localHour)
  {
    return 0;
  }
  else if (utcHour > localHour)
  {
    if (utcHour - localHour >= 12)
    {
      return 24 - utcHour + localHour;
    }
    else
    {
      return localHour - utcHour;
    }
  }
  else
  {
    if (localHour - utcHour > 12)
    {
      return  localHour - 24 - utcHour;
    }
    else
    {
      return localHour - utcHour;
    }
  }
}

int powerFunction(String command)
{
  int value = command.toInt();
  if (value == 0 || value == 1)
  {
    powerState = value;
    pushoverPowerMessage("User");
    return powerState;
  }
  else if (value == 2)
  {
    powerState = powerState? 0 : 1; //!powerState;
    powerState = value;
    pushoverPowerMessage("User");
    return powerState;
  }
  else return -1;
}
time_t tmConvert_t(int YYYY, byte MM, byte DD, byte hh, byte mm, byte ss)
{
  struct tm t;
  t.tm_year = YYYY-1900;
  t.tm_mon = MM - 1;
  t.tm_mday = DD;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  t.tm_isdst = 0;
  time_t t_of_day = mktime(&t);
  return t_of_day;
}
//
bool timerEvaluate()  // comparing time here is easier with Unix timestamps...
{
  int on_time = tmConvert_t(Time.year(), Time.month(), Time.day(), sunset.theHour, sunset.theMinute, 0);
  int off_time = tmConvert_t(Time.year(), Time.month(), Time.day(), offTime.theHour, offTime.theMinute, 0);
  int now_time = tmConvert_t(Time.year(), Time.month(), Time.day(), Time.hour(), Time.minute(), Time.second());
  //
  if (on_time < off_time)
  {
    return (now_time > on_time && now_time < off_time);
  }
  else if (off_time < on_time)
  {
    return (now_time > on_time || now_time < off_time);
  }
  else // if both on and off are set to the same time, I'm confused... so let's not lite up at all!
  {
    return false;
  }
}

int setTimes(String command)
{
  int gotTime = 0;
  Serial.println(command);
  char timeString[63] = "";
  command.toCharArray(timeString, sizeof(timeString));
  char* ptr;
  ptr = strstr(timeString,"ON");
  if(ptr)
  {
    ptr += 2;
    double on = atof(ptr);
    onTime.theHour = int(on);
    onTime.theMinute = (on - onTime.theHour) * 60.0;
    gotTime += 1;
  }
  ptr = strstr(timeString,"OFF");
  if(ptr)
  {
    ptr += 3;
    double off = atof(ptr);
    offTime.theHour = int(off);
    offTime.theMinute = (off - offTime.theHour) * 60.0;
    gotTime += 10;
  }
  sprintf(sunriseBuffer, "%s, %s Sunrise: %02d:%02d, Sunset: %02d:%02d, Sunset Offset = %d, Off Time: %02d:%02d, Last Updated: %02d:%02d", cityLocation, stateLocation, sunrise.theHour, sunrise.theMinute, sunset.theHour, sunset.theMinute, sunsetOffset, offTime.theHour, offTime.theMinute, Time.hour(), Time.minute());
  return gotTime;
}

void pushoverPowerMessage(const char* trigger)
{
  char pushoverMessage[128] = "";
  strcpy(pushoverMessage, trigger);
  strcat(pushoverMessage, " set ");
  strcat(pushoverMessage, room);
  strcat(pushoverMessage, " crystal ");
  strcat(pushoverMessage, powerState? "ON" : "OFF");
  Particle.publish("pushover", pushoverMessage, 60, PRIVATE);
}
