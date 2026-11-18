#include "Button.h"
#include "Board.h"

// Toque curto: solto antes de LONG_MS e sem segundo toque em DOUBLE_GAP_MS.
// Toque longo: dispara ao atingir LONG_MS (sem esperar soltar).
// Toque duplo: dois toques curtos com intervalo menor que DOUBLE_GAP_MS.
static const uint32_t DEBOUNCE_MS = 25;
static const uint32_t LONG_MS = 900;
static const uint32_t DOUBLE_GAP_MS = 300;

static bool stable = false;        // true = pressionado
static bool lastRaw = false;
static uint32_t rawChanged = 0;
static uint32_t pressedAt = 0;
static uint32_t releasedAt = 0;
static bool longFired = false;
static uint8_t clicks = 0;

void buttonBegin()
{
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

bool buttonHeldAtBoot(uint32_t ms)
{
  const uint32_t t0 = millis();
  while(millis() - t0 < ms)
  {
    if(digitalRead(PIN_BUTTON) != LOW) return false;
    delay(10);
  }
  return true;
}

ButtonEvent buttonPoll()
{
  const uint32_t now = millis();
  const bool raw = digitalRead(PIN_BUTTON) == LOW;
  if(raw != lastRaw) { lastRaw = raw; rawChanged = now; }

  if(raw != stable && now - rawChanged >= DEBOUNCE_MS)
  {
    stable = raw;
    if(stable)
    {
      pressedAt = now;
      longFired = false;
    }
    else
    {
      releasedAt = now;
      if(!longFired)
      {
        clicks++;
        if(clicks >= 2) { clicks = 0; return BTN_DOUBLE; }
      }
    }
  }

  if(stable && !longFired && now - pressedAt >= LONG_MS)
  {
    longFired = true;
    clicks = 0;
    return BTN_LONG;
  }

  if(!stable && clicks == 1 && now - releasedAt >= DOUBLE_GAP_MS)
  {
    clicks = 0;
    return BTN_SHORT;
  }
  return BTN_NONE;
}
