#include <TaskScheduler.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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

#define REFRESH_INTERVAL_MS 600000 // Every ten minutes
#define UPDATE_INTERVAL_MS 1000
#define PIXEL_COUNT 28
#define PIXEL_PIN 33

#define SEGMENTS 7
#define DIGITS 6
#define SEGMENT_LEDS 4

const static RgbColor Black(0, 0, 0);
const static RgbColor White(255, 255, 255);
const static RgbColor Purple(255, 0, 255);
const static RgbColor Red(255, 0, 0);
const static RgbColor Green(0, 255, 0);
const static RgbColor Yellow(255, 255, 0);
const static RgbColor Blue(0, 0, 255);
const static RgbColor GoalsOrange(255, 30, 0);

typedef NeoGrbFeature MyPixelColorFeature;
typedef NeoEsp32I2s0Ws2812xMethod MyPixelColorMethod;

// This is the bit order of the segments:
//
//   + 1 +
//   2   0
//   + 3 +
//   6   4
//   + 5 +

const uint8_t SevenSegDigit[64] =
    {
        /*  0     1     2     3     4   */
        0x77, 0x11, 0x6B, 0x3B, 0x1D,
        /*  5     6     7     8     9   */
        0x3E, 0x7E, 0x13, 0x7F, 0x3F,
        /*  A     B     C     D     E   */
        0x5f, 0x7f, 0x66, 0x77, 0x63,
        /*  F     G     H     I     J  */
        0x4e, 0x76, 0x5d, 0x11, 0x73,
        /*  K     L     M     N     O */
        0x5d, 0x64, 0x57, 0x57, 0x77,
        /*  P     Q     R     S     T */
        0x4f, 0x77, 0x5f, 0x3e, 0x46,
        /*  U     V     W     X     Y */
        0x75, 0x75, 0x75, 0x5d, 0x1d,
        /*  Z */
        0x6b,
        /*  a     b     c     d     e   */
        0x7b, 0x7c, 0x68, 0x79, 0x6f,
        /*  f     g     h     i     j   */
        0x4e, 0x3f, 0x5c, 0x10, 0x71,
        /*  k     l     m     n     o   */
        0x5e, 0x11, 0x58, 0x58, 0x77,
        /*  p     q     r     s     t   */
        0x4f, 0x1f, 0x48, 0x3e, 0x6c,
        /*  u     v     w     x     y   */
        0x70, 0x70, 0x70, 0x5d, 0x3d,
        /*  z */
        0x6b,
        /*  -    ' ' */
        0x08, 0x00};

uint32_t DailyUsersToday;
uint32_t OneVOneMatches;
uint32_t SoloGames;
uint32_t GeneratedCharacters;
uint32_t OnlineUsers;
uint32_t Timestamp;

void DisplayAlphaNumberic(char alnum, int digit_offset, RgbColor color, NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> &_strip)
{
  int index;

  if (alnum >= '0' && alnum <= '9')
  {
    index = alnum - '0';
  }
  else if (alnum >= 'A' && alnum <= 'Z')
  {
    // Only have lower case for the moment
    index = (alnum - 'A') + 10;
  }
  else if (alnum >= 'a' && alnum <= 'z')
  {
    index = (alnum - 'a') + 36;
  }
  else if (alnum == '-')
  {
    index = 62;
  }
  else if (alnum == ' ')
  {
    index = 63;
  }
  else
  {
    return;
  }

  if (index < 0 || index > 63)
  {
    index = 63;
  }

  uint8_t bitmask = SevenSegDigit[index];
  uint8_t start_pixel = 0;
  RgbColor segment_color;

  for (uint8_t seg = 0; seg < SEGMENTS; seg++)
  {
    start_pixel = seg * SEGMENT_LEDS + digit_offset * PIXEL_COUNT;
    segment_color = ((bitmask >> seg) & 0x01) ? color : Black;

    for (uint8_t pixel = 0; pixel < SEGMENT_LEDS; pixel++)
    {
      _strip.SetPixelColor(pixel + start_pixel, segment_color);
    }
  }
}

#pragma region TASKS

class PollingTask : public Task
{
private:
  uint32_t &_dailyUsersToday;
  uint32_t &_oneVOneMatches;
  uint32_t &_soloGames;
  uint32_t &_generatedCharacters;
  uint32_t &_onlineUsers;
  uint32_t &_timestamp;

  void PollValue()
  {
    _timestamp = millis();

    HTTPClient httpClient;
    httpClient.setTimeout(30000);
    httpClient.setReuse(false);

    if (!httpClient.begin("https://europe-west1-prj-prod-public-g000.cloudfunctions.net/iot-stats"))
    {
      Serial.println("Failed to begin HTTPClient");
      return;
    }

    int code = httpClient.GET();

    if (code != HTTP_CODE_OK)
    {
      Serial.println("Failed on response from server: " + String(code));
      return;
    }

    auto body = httpClient.getString();

    Serial.println("Response from server: " + body);

    DynamicJsonDocument json(1024);

    auto error = deserializeJson(json, body);

    if (error != DeserializationError::Ok)
    {
      Serial.print("Failed to deserialize the response from the server: ");
      Serial.println(error.f_str());
    }

    _oneVOneMatches = _soloGames = _generatedCharacters = _onlineUsers = 0;

    // {
    //    "cache_age":0,
    //    "now":{
    //       "online_users":19
    //    },
    //    "timestamp":"2025-05-14T13:53:42.270334+00:00",
    //    "today":{
    //       "characters_generated":3838,
    //       "daily_active_users_app":133,
    //       "daily_active_users_game":234,
    //       "first_opens_game":32,
    //       "match_completed_1v1":156,
    //       "match_completed_solo":126
    //    },
    //    "total":{
    //       "characters_generated":1332072,
    //       "match_completed_1v1":84931,
    //       "match_completed_solo":79828
    //    }
    // }

    auto nowJson = json["now"];
    if(nowJson == nullptr)
    {
      Serial.println("Failed to find \"now\" object");
    }

    auto onlineUsersJson = nowJson["online_users"];
    if(onlineUsersJson == nullptr)
    {
      Serial.println("Failed to find \"now.online_users\" object");
    }
    _onlineUsers = onlineUsersJson;
    Serial.printf("online_users = %d", _onlineUsers);

    // -------

    auto todayJson = json["today"];
    if(todayJson == nullptr)
    {
      Serial.println("Failed to find \"today\" object");
    }

    auto dailyActiveUsersJson = todayJson["daily_active_users_game"];
    if(dailyActiveUsersJson == nullptr)
    {
      Serial.println("Failed to find \"today.daily_active_users_game\" object");
    }
    _dailyUsersToday = dailyActiveUsersJson;
    Serial.printf("daily_active_users_game = %d", _dailyUsersToday);

    // ------

    auto totalJson = json["total"];
    if(totalJson == nullptr)
    {
      Serial.println("Failed to find \"total\" object");
    }

    auto charactersGeneratedJson = totalJson["characters_generated"];
    if(charactersGeneratedJson == nullptr)
    {
      Serial.println("Failed to find \"total.characters_generated\" object");
    }
    _generatedCharacters = charactersGeneratedJson;
    Serial.printf("characters_generated = %d", _generatedCharacters);

    auto oneVOneCompletedJson = totalJson["match_completed_1v1"];
    if(oneVOneCompletedJson == nullptr)
    {
      Serial.println("Failed to find \"total.match_completed_1v1\" object");
    }
    _oneVOneMatches = oneVOneCompletedJson;
    Serial.printf("match_completed_1v1 = %d", _oneVOneMatches);

    auto soloCompletedJson = totalJson["match_completed_solo"];
    if(soloCompletedJson == nullptr)
    {
      Serial.println("Failed to find \"total.match_completed_solo\" object");
    }
    _soloGames = soloCompletedJson;
    Serial.printf("match_completed_solo = %d", _soloGames);

    httpClient.end();

    return;
  }

public:
  PollingTask(Scheduler &scheduler, uint32_t &dailyUsersValue, uint32_t &oneVOneValue, uint32_t &soloValue, uint32_t &charactersValue, uint32_t &onlineValue, uint32_t &timestamp)
      : Task(
            REFRESH_INTERVAL_MS,
            TASK_FOREVER,
            [this]
            { PollValue(); },
            &scheduler, false),
        _dailyUsersToday(dailyUsersValue),
        _oneVOneMatches(oneVOneValue),
        _soloGames(soloValue),
        _generatedCharacters(charactersValue),
        _timestamp(timestamp),
        _onlineUsers(onlineValue)
  {
  }
};

class CycleDisplayTask : public Task
{
private:
  NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> &_strip;
  uint32_t &_dailyUsersToday;
  uint32_t &_oneVOneMatches;
  uint32_t &_soloGames;
  uint32_t &_onlineUsers;
  uint32_t &_generatedCharacters;

public:
  void DisplayValue()
  {
    auto seconds = millis() / 1000;

    // This looks a little daft since the result is the same as ( seconds % 6 ) but
    // that would make it flip every second, we want whatever is displayed to remain
    // for five seconds before switching to the next.
    int value = (seconds % 36) / 6;

    if (value < 0)
    {
      value = 0;
    }
    else if (value > 5)
    {
      value = 5;
    }

    _strip.ClearTo(Black);

    switch (value)
    {
    case 0: // 1 v 1 matches
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + ((_oneVOneMatches) / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Red, _strip);
      }
      Serial.printf("One V One matches = %d", _oneVOneMatches);
      Serial.println();
      break;
    case 1: // Beat
      DisplayAlphaNumberic('b', 5, GoalsOrange, _strip);
      DisplayAlphaNumberic('t', 4, GoalsOrange, _strip);
      DisplayAlphaNumberic('1', 3, GoalsOrange, _strip);
      DisplayAlphaNumberic('2', 2, GoalsOrange, _strip);
      DisplayAlphaNumberic('-', 1, GoalsOrange, _strip);
      DisplayAlphaNumberic('3', 0, GoalsOrange, _strip);
      Serial.println("Beat Display");
      break;
    case 2: // Solo Games
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + (_soloGames / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Purple, _strip);
      }
      Serial.printf("Solo games = %d", _soloGames);
      Serial.println();
      break;
    case 3: // daily users
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + (_dailyUsersToday / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Green, _strip);
      }
      Serial.printf("Users today = %d", _dailyUsersToday);
      Serial.println();
      break;
    case 4: // Currently online
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + (_onlineUsers / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Yellow, _strip);
      }
      Serial.printf("Currently online = %d", _onlineUsers);
      Serial.println();
      break;
    case 5: // Total generated characters
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + (_generatedCharacters / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Blue, _strip);
      }
      Serial.printf("Total generated = %d", _generatedCharacters);
      Serial.println();
      break;
    default:
      DisplayAlphaNumberic('-', 0, Blue, _strip);
      break;
    }

    _strip.Show();

    delay(UPDATE_INTERVAL_MS);
  }

  CycleDisplayTask(Scheduler &scheduler, NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> &strip, uint32_t &dailyUsersValue, uint32_t &oneVOneValue, uint32_t &soloValue, uint32_t &charactersValue, uint32_t &onlineValue)
      : Task(
            TASK_IMMEDIATE,
            TASK_FOREVER,
            [this]
            { DisplayValue(); },
            &scheduler, false),
        _strip(strip),
        _dailyUsersToday(dailyUsersValue),
        _oneVOneMatches(oneVOneValue),
        _soloGames(soloValue),
        _generatedCharacters(charactersValue),
        _onlineUsers(onlineValue)
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
CycleDisplayTask DisplayTask(TaskScheduler, PixelStrip, DailyUsersToday, OneVOneMatches, SoloGames, GeneratedCharacters, OnlineUsers);

// TempDummyClass DummyTask(TaskScheduler, PixelStrip);

PollingTask RefreshTask(TaskScheduler, DailyUsersToday, OneVOneMatches, SoloGames, GeneratedCharacters, OnlineUsers, Timestamp);

#pragma endregion

#pragma region SETUP &LOOP

void initWifi()
{
  auto apSsid = String(soft_ap_ssid) + String(rand(), 0x16);

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
    delay(500);
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

  // NOTE: THIS MUST BE COMMENTED OUT WHEN NOT CONNECTED TO A COMPUTER!
  // while(!Serial && millis() < 10000)
  //{
  //  delay(100);
  //}

  initWifi();

  PixelStrip.Begin();
  PixelStrip.ClearTo(Black);
  PixelStrip.Show();

  TaskScheduler.enable();

  RefreshTask.enable();
  DisplayTask.enable();
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
