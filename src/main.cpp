#include <TaskScheduler.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

#define UPDATE_INTERVAL_MS 100
#define PIXEL_COUNT 14
#define PIXEL_PIN 16

const static RgbColor Black(0, 0, 0);
const static RgbColor White(255, 255, 255);
const static RgbColor Purple(255, 0, 255);

typedef NeoGrbFeature MyPixelColorFeature;

#pragma region TASKS

class TickCounterClass : public Task
{
private:
  NeoPixelBus<MyPixelColorFeature, NeoWs2812Method> &_strip;
  uint32_t _counter;

public:
  void TickCounter()
  {
    for (auto i = 0; i < PIXEL_COUNT; ++i)
    {
      _strip.SetPixelColor(i, _counter & (1 << i) ? Purple : Black);
    }

    _strip.Show();

    _counter++;
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