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

#define REFRESH_INTERVAL_MS 60000 // 10 minutes polling interval
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
uint32_t FiveVFiveMatches;
uint32_t ArenaGames;
uint32_t GeneratedCharacters;
uint32_t TotalAccounts;
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
  uint32_t &_fiveVFiveMatches;
  uint32_t &_arenaGames;
  uint32_t &_generatedCharacters;
  uint32_t &_totalAccounts;
  uint32_t &_timestamp;

  void PollValue()
  {
    _timestamp = millis();

    HTTPClient httpClient;
    httpClient.setTimeout(30000);
    httpClient.setReuse(false);

    if (!httpClient.begin("https://tomas-hzrqbqznnq-ez.a.run.app/"))
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

    // {"characters":{"characters":215201,"arena":8906,"five_vs_five":126,"one_vs_one":10355,"daily_active_users_game_today":50,"accounts_created":5690}}

    auto subJson = json["characters"];

    if(subJson == nullptr)
    {
      Serial.println("Failed to find object \"characters\"");
      return;
    }

    _oneVOneMatches = _oneVOneMatches = _fiveVFiveMatches = _arenaGames = _generatedCharacters = 0;

    long dailyUsersCount = subJson["daily_active_users_game_today"];
    Serial.printf("Daily active users today = %d", dailyUsersCount);
    Serial.println();
    _dailyUsersToday = dailyUsersCount;

    long totalAccountsCount = subJson["accounts_created"];
    Serial.printf("Total accounts = %d", totalAccountsCount);
    Serial.println();
    _totalAccounts = totalAccountsCount;

    long oneVOneCount = subJson["one_vs_one"];
    Serial.printf("1v1 games = %d", oneVOneCount);
    Serial.println();
    _oneVOneMatches = oneVOneCount;

    long fiveVFiveCount = subJson["five_vs_five"];
    Serial.printf("5v5 games = %d", fiveVFiveCount);
    Serial.println();
    _fiveVFiveMatches = fiveVFiveCount;

    long arenaCount = subJson["arena"];
    Serial.printf("Arena games = %d", arenaCount);
    Serial.println();
    _arenaGames = arenaCount;

    long charcterCount = subJson["characters"];
    Serial.printf("Generated characters = %d", charcterCount);
    Serial.println();
    _generatedCharacters = charcterCount;

    httpClient.end();

    return;
  }

public:
  PollingTask(Scheduler &scheduler, uint32_t &dailyUsersValue, uint32_t &oneVOneValue, uint32_t &fiveVFiveValue, uint32_t &arenaValue, uint32_t &charactersValue, uint32_t &totalAccounts, uint32_t &timestamp)
      : Task(
            REFRESH_INTERVAL_MS,
            TASK_FOREVER,
            [this]
            { PollValue(); },
            &scheduler, false),
        _dailyUsersToday(dailyUsersValue),
        _oneVOneMatches(oneVOneValue),
        _fiveVFiveMatches(fiveVFiveValue),
        _arenaGames(arenaValue),
        _generatedCharacters(charactersValue),
        _totalAccounts(totalAccounts),
        _timestamp(timestamp)
  {
  }
};

class CycleDisplayTask : public Task
{
private:
  NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> &_strip;
  uint32_t &_dailyUsersToday;
  uint32_t &_oneVOneMatches;
  uint32_t &_fiveVFiveMatches;
  uint32_t &_arenaGames;
  uint32_t &_generatedCharacters;
  uint32_t &_totalAccounts;

public:
  void DisplayValue()
  {
    auto seconds = millis() / 1000;

    // This looks a little daft since the result is the same as ( seconds % 3 ) but
    // that would make it flip every second, we want whatever is displayed to remain
    // for five seconds before switching to the next.
    int value = (seconds % 25) / 5;

    if (value < 0)
    {
      value = 0;
    }
    else if (value > 4)
    {
      value = 4;
    }

    _strip.ClearTo(Black);

    switch (value)
    {
    case 0:
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + ((_oneVOneMatches + _fiveVFiveMatches + _arenaGames) / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Red, _strip);
      }
      Serial.printf("Played games = %d", _oneVOneMatches + _fiveVFiveMatches + _arenaGames);
      Serial.println();
      break;
    case 1:
      DisplayAlphaNumberic('b', 5, GoalsOrange, _strip);
      DisplayAlphaNumberic('t', 4, GoalsOrange, _strip);
      DisplayAlphaNumberic('1', 3, GoalsOrange, _strip);
      DisplayAlphaNumberic('0', 2, GoalsOrange, _strip);
      DisplayAlphaNumberic('-', 1, GoalsOrange, _strip);
      DisplayAlphaNumberic('5', 0, GoalsOrange, _strip);
      Serial.println("Beat Display");
      break;
    case 2:
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + (_generatedCharacters / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Purple, _strip);
      }
      Serial.printf("Generated characters = %d", _generatedCharacters);
      Serial.println();
      break;
    case 3:
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + (_dailyUsersToday / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Green, _strip);
      }
      Serial.printf("Users today = %d", _dailyUsersToday);
      Serial.println();
      break;
    case 4:
      for (auto i = 0; i < DIGITS; i++)
      {
        auto digitValue = '0' + (_totalAccounts / (int)(pow(10, i) + 0.5)) % 10;
        DisplayAlphaNumberic(digitValue, i, Yellow, _strip);
      }
      Serial.printf("Total accounts = %d", _totalAccounts);
      Serial.println();
      break;
    default:
      DisplayAlphaNumberic('-', 0, Blue, _strip);
      break;
    }

    _strip.Show();

    delay(UPDATE_INTERVAL_MS);
  }

  CycleDisplayTask(Scheduler &scheduler, NeoPixelBus<MyPixelColorFeature, MyPixelColorMethod> &strip, uint32_t &dailyUsersValue, uint32_t &goValue, uint32_t &ffaValue, uint32_t &arenaValue, uint32_t &charactersValue, uint32_t &totalAccountsValue)
      : Task(
            TASK_IMMEDIATE,
            TASK_FOREVER,
            [this]
            { DisplayValue(); },
            &scheduler, false),
        _strip(strip),
        _dailyUsersToday(dailyUsersValue),
        _oneVOneMatches(goValue),
        _fiveVFiveMatches(ffaValue),
        _arenaGames(arenaValue),
        _generatedCharacters(charactersValue),
        _totalAccounts(totalAccountsValue)
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
CycleDisplayTask DisplayTask(TaskScheduler, PixelStrip, DailyUsersToday, OneVOneMatches, FiveVFiveMatches, ArenaGames, GeneratedCharacters, TotalAccounts);

// TempDummyClass DummyTask(TaskScheduler, PixelStrip);

PollingTask RefreshTask(TaskScheduler, DailyUsersToday, OneVOneMatches, FiveVFiveMatches, ArenaGames, GeneratedCharacters, TotalAccounts, Timestamp);

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
