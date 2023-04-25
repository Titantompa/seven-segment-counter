#include <TaskScheduler.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

#define UPDATE_INTERVAL_MS 1000
#define PIXEL_COUNT 14
#define PIXEL_PIN 16

#define SEGMENTS 7

const static RgbColor Black(0, 0, 0);
const static RgbColor White(255, 255, 255);
const static RgbColor Purple(255, 0, 255);
const static RgbColor Red(255, 0, 0);

typedef NeoGrbFeature MyPixelColorFeature;

const uint8_t PixelsPerSegment[7] = {2, 2, 2, 2, 2, 2, 2};

const uint8_t SevenSegDigit[10] =
{
/*  0     1     2     3     4   */
  0x77, 0x11, 0x6B, 0x3B, 0x1D, 
/*  5     6     7     8     9   */
  0x3E, 0x7E, 0x13, 0x7F, 0x3F
};


void DisplayNumber(int num, int decimal_place, RgbColor color, NeoPixelBus<MyPixelColorFeature, NeoWs2812Method> &_strip)
{
  
  uint8_t bitmask = SevenSegDigit[num];
  uint8_t start_pixel = 0;
  RgbColor segment_color;

  for (uint8_t seg = 0; seg < SEGMENTS; seg++)
  {

    start_pixel = seg * PixelsPerSegment[seg] + decimal_place * PIXEL_COUNT;
    segment_color = (bitmask & 0x01) ? color : Black;

    for (uint8_t pixel = start_pixel; pixel < start_pixel + PixelsPerSegment[seg]; pixel++)
    {
      _strip.SetPixelColor(pixel, segment_color);
    }
    
    bitmask >>= 1;
  }

  _strip.Show();

}


#pragma region TASKS

class TickCounterClass : public Task
{
private:
  NeoPixelBus<MyPixelColorFeature, NeoWs2812Method> &_strip;
  uint32_t _counter;

public:
  void TickCounter()
  {
    DisplayNumber(_counter,0, Red, _strip);

    _counter++;
    if(_counter > 9)
    {
      _counter = 0;
    }

    delay(UPDATE_INTERVAL_MS);
  }

  TickCounterClass(Scheduler &scheduler, NeoPixelBus<MyPixelColorFeature, NeoWs2812Method> &strip)
      : Task(
            TASK_IMMEDIATE,
            TASK_FOREVER,
            [this]
            { TickCounter(); },
            &scheduler, true),
        _strip(strip),
        _counter(0)
  {
  }
};

#pragma endregion

#pragma region GLOBALS

/* @brief The neopixel strip */
NeoPixelBus<MyPixelColorFeature, NeoWs2812Method> PixelStrip(PIXEL_COUNT, PIXEL_PIN);

/* @brief The task scheduler instance */
Scheduler TaskScheduler;

/* @brief The CounterTask instance controlling the led strip */
TickCounterClass CounterTask(TaskScheduler, PixelStrip);

#pragma endregion

#pragma region SETUP &LOOP

void setup()
{
  PixelStrip.Begin();
  PixelStrip.ClearTo(Black);
  PixelStrip.Show();

  TaskScheduler.enable();

  CounterTask.enable();
}

void loop()
{
  TaskScheduler.execute();
}

#pragma endregion