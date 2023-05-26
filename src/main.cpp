#include <TaskScheduler.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <WiFi.h>
#include <HTTPClient.h>

#pragma region WiFi Settings

#define Q(x) (#x)
#define QuoteMacro(x) (Q(x))

const char * hostname = "GoalsCounter"; 
const char * soft_ap_ssid = QuoteMacro(SOFT_AP_SSID); 
const char * soft_ap_pwd = QuoteMacro(SOFT_AP_PWD); 
const char * wifi_ssid = QuoteMacro(WIFI_SSID); 
const char * wifi_pwd = QuoteMacro(WIFI_PWD);

IPAddress soft_ap_address(172, 17, 49, 1);
IPAddress soft_ap_mask(255, 255, 255, 128);

#pragma endregion

#define REFRESH_INTERVAL_MS 60000
#define UPDATE_INTERVAL_MS 1000
#define PIXEL_COUNT 28
#define PIXEL_PIN 33

#define SEGMENTS 7
#define DIGITS 5
#define SEGMENT_LEDS 4

const static RgbColor Black(0, 0, 0);
const static RgbColor White(255, 255, 255);
const static RgbColor Purple(255, 0, 255);
const static RgbColor Red(255, 0, 0);

typedef NeoGrbFeature MyPixelColorFeature;
typedef NeoEsp32I2s0Ws2812xMethod MyPixelColorMethod;

const uint8_t SevenSegDigit[10] =
{
/*  0     1     2     3     4   */
  0x77, 0x11, 0x6B, 0x3B, 0x1D, 
/*  5     6     7     8     9   */
  0x3E, 0x7E, 0x13, 0x7F, 0x3F
};

uint32_t CurrentValue;
uint32_t Timestamp;

void DisplayNumber(int num, int digit_offset, RgbColor color, NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> &_strip)
{
  
  uint8_t bitmask = SevenSegDigit[num];
  uint8_t start_pixel = 0;
  RgbColor segment_color;

  for (uint8_t seg = 0; seg < SEGMENTS; seg++)
  {
    start_pixel = seg * SEGMENT_LEDS + digit_offset * PIXEL_COUNT;
    segment_color = ((bitmask>>seg) & 0x01) ? color : Black;

    for (uint8_t pixel = 0; pixel < SEGMENT_LEDS; pixel++)
    {
      _strip.SetPixelColor(pixel+start_pixel, segment_color);
    }
  }
}

#pragma region TASKS

class PollingTask : public Task
{
  private:
  uint32_t& _value;
  uint32_t& _timestamp;

  void PollValue()
  {
    _timestamp = millis();

    HTTPClient httpClient;
    httpClient.setTimeout(30000);
    httpClient.setReuse(false);
    
    if(!httpClient.begin("https://tomas-hzrqbqznnq-ez.a.run.app/"))
    {
      Serial.println("Failed to begin HTTPClient");
      return;
    }

    int code = httpClient.GET();

    if(code != HTTP_CODE_OK)
    {
      Serial.println("Failed on response from server: "+String(code));
      return;
    }

    auto body = httpClient.getString();

    Serial.println("Response from server: "+body);

    int start = body.indexOf("counter");
    if(start== -1)
    {
      Serial.println("Failed, since body doesn't seem to contain 'counter'");
      return;
    }
    start += 8;

    while( body[start] == ' ' || body[start]==':' && start < body.length())
      start++;

    if(start == body.length())
    {
      Serial.println("Failed, unable to find start of numeric value");
      return;
    }

    auto end = start;
    while(isdigit(body[end]) && end < body.length())
      end++;

    if(end == body.length())
    {
      Serial.println("Failed, reached end of payload looking for end of numeric value");
      return;
    }

    auto numstr = body.substring(start,end);

    Serial.println("Found counter value: "+numstr);

    _value = atoi(numstr.c_str());

    httpClient.end();

    return;
  }

  public:
  PollingTask(Scheduler &scheduler, uint32_t& value, uint32_t& timestamp)
      : Task(REFRESH_INTERVAL_MS,
            TASK_FOREVER,
            [this]
            { PollValue(); },
            &scheduler, true),
        _value(value),
        _timestamp(timestamp)
  {
  }
};

class TickCounterClass : public Task
{
private:
  NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> &_strip;
  uint32_t& _counter;

public:
  void TickCounter()
  {
    for(auto i = 0; i < DIGITS; i++)
    {
      auto digitValue = (_counter/(int)(pow(10, i)+0.5))%10; // todo - calculate the single digit value
      DisplayNumber(digitValue, i, Red, _strip);
    }

    _strip.Show();

    delay(UPDATE_INTERVAL_MS);
  }

  TickCounterClass(Scheduler &scheduler, NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> &strip, uint32_t& counter)
      : Task(
            TASK_IMMEDIATE,
            TASK_FOREVER,
            [this]
            { TickCounter(); },
            &scheduler, true),
        _strip(strip),
        _counter(counter)
  {
  }
};

#pragma endregion

#pragma region GLOBALS

/* @brief The neopixel strip */
NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> PixelStrip(PIXEL_COUNT*DIGITS, PIXEL_PIN);

/* @brief The task scheduler instance */
Scheduler TaskScheduler;

/* @brief The CounterTask instance controlling the led strip */
TickCounterClass CounterTask(TaskScheduler, PixelStrip, CurrentValue);

PollingTask RefreshTask(TaskScheduler, CurrentValue, Timestamp);

#pragma endregion

#pragma region SETUP &LOOP


void initWifi()
{
  auto apSsid = String(soft_ap_ssid)+String(rand(), 0x16);

  Serial.println("Soft AP SSID: " + apSsid);

  // WiFi.mode(WIFI_STA);
  WiFi.mode(WIFI_AP_STA);

  if (!WiFi.softAPConfig(soft_ap_address, INADDR_NONE, soft_ap_mask))
  {
    Serial.println("WiFi.softAPConfig() failed!");
  }

  if (!WiFi.softAP(apSsid.c_str(), soft_ap_pwd))
  {
    Serial.println("WiFi.softAP() failed!");
  }

  Serial.print("Soft AP IP: ");
  Serial.println(WiFi.softAPIP());

  WiFi.begin(wifi_ssid, wifi_pwd);

  pinMode(LED_BUILTIN, OUTPUT);

  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  digitalWrite(LED_BUILTIN, false);

  Serial.print("Wifi Client IP: ");
  Serial.println(WiFi.localIP());
}

void setup()
{
  Serial.begin(115200);

  while(!Serial && millis() < 10000)
  {
    delay(100);
  }

  initWifi();

  PixelStrip.Begin();
  PixelStrip.ClearTo(Black);
  PixelStrip.Show();

  TaskScheduler.enable();

  CounterTask.enable();
}

void loop()
{
  TaskScheduler.execute();

  // Handle WiFi reconnects
  if(WiFi.status() != WL_CONNECTED)
  {
    WiFi.reconnect();
      
    while (WiFi.status() != WL_CONNECTED)
    {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(250);
      Serial.print(".");
    }

    Serial.print("Wifi Client IP: ");
    Serial.println(WiFi.localIP());
  }

}

#pragma endregion