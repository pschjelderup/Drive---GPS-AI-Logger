#include "sound.h"

#include "config.h"

namespace {

// Ett besked ar en lista med toner. Noll hertz betyder tystnad, vilket ar hur
// man far ett pipmonster i stallet for ett langt tut.
struct Step {
  uint16_t hz;
  uint16_t ms;
};

// Piezon later hogst kring sin egen resonansfrekvens, sa tonerna ligger dar och
// skiljs at pa langd och antal i stallet for pa tonhojd. Ett hogre piep later
// inte hogre - det later bara svagare.
const uint16_t kHi = BUZZER_RESONANCE_HZ;
const uint16_t kLo = BUZZER_RESONANCE_HZ * 3 / 4;

// Angelagenheten avgor vad som far avbryta vad. Kameran narmar sig monotont, sa
// varje ring later mer patrangande an den forra.
const Step kCamFar[] = {{kLo, 120}, {0, 0}};
const Step kCamMid[] = {{kHi, 110}, {0, 90}, {kHi, 110}, {0, 0}};
const Step kCamNear[] = {{kHi, 90}, {0, 70}, {kHi, 90}, {0, 70},
                         {kHi, 90}, {0, 70}, {kHi, 220}, {0, 0}};

// Overhastighet later med flit annorlunda an kameran - tva korta, lagt. Man ska
// kunna hora skillnad pa "du kor for fort" och "det star en kamera dar framme"
// utan att titta pa skarmen.
const Step kOver[] = {{kLo, 70}, {0, 60}, {kLo, 70}, {0, 0}};

const Step kStart[] = {{kLo, 80}, {kHi, 120}, {0, 0}};
const Step kEnd[] = {{kHi, 80}, {kLo, 160}, {0, 0}};
const Step kTap[] = {{kHi, 25}, {0, 0}};
const Step kError[] = {{kLo, 300}, {0, 120}, {kLo, 300}, {0, 0}};

struct Cue {
  const Step *steps;
  uint8_t priority;
};

Cue cueFor(SoundCue c) {
  switch (c) {
    case CUE_CAM_NEAR: return {kCamNear, 5};
    case CUE_CAM_MID: return {kCamMid, 4};
    case CUE_CAM_FAR: return {kCamFar, 3};
    case CUE_OVER_LIMIT: return {kOver, 2};
    case CUE_ERROR: return {kError, 4};
    case CUE_TRIP_START: return {kStart, 1};
    case CUE_TRIP_END: return {kEnd, 1};
    case CUE_TAP: return {kTap, 0};
    default: return {nullptr, 0};
  }
}

bool g_enabled = DEFAULT_SOUND_ON;
bool g_attached = false;

const Step *g_steps = nullptr;
uint8_t g_priority = 0;
uint8_t g_at = 0;
uint32_t g_stepStartMs = 0;

void output(uint16_t hz) {
  if (!g_attached) return;
  if (hz == 0) {
    // Nolldutycykel i stallet for att koppla loss stiftet. Loskopplat stift
    // flyter, och ett flytande stift pa en piezo kan ge ett svagt sorrande.
    ledcWrite(PIN_BUZZER, 0);
    return;
  }
  ledcWriteTone(PIN_BUZZER, hz);
  // Halva dutycykeln ger den hogsta ljudnivan ur en piezo: den drivs da av en
  // ren fyrkantsvag i stallet for korta pulser.
  ledcWrite(PIN_BUZZER, 128);
}

}  // namespace

namespace sound {

void begin() {
  // Atta bitars upplosning racker: dutycykeln behover bara vara halv, och en
  // hogre upplosning skulle bara begransa vilka frekvenser som gar att stalla.
  g_attached = ledcAttach(PIN_BUZZER, BUZZER_RESONANCE_HZ, 8);
  if (g_attached) output(0);
}

void setEnabled(bool on) {
  g_enabled = on;
  if (!on) silence();
}

bool enabled() { return g_enabled; }

void play(SoundCue cue) {
  if (!g_enabled || !g_attached) return;
  const Cue c = cueFor(cue);
  if (!c.steps) return;

  // Ett pagaende besked med hogre angelagenhet far inte avbrytas av ett lagre.
  if (g_steps && c.priority < g_priority) return;

  g_steps = c.steps;
  g_priority = c.priority;
  g_at = 0;
  g_stepStartMs = millis();
  output(g_steps[0].hz);
}

void tick() {
  if (!g_steps) return;

  const Step &now = g_steps[g_at];
  if (now.ms == 0) {
    // Listan ar slut.
    g_steps = nullptr;
    g_priority = 0;
    output(0);
    return;
  }

  if (millis() - g_stepStartMs < now.ms) return;

  g_at++;
  g_stepStartMs = millis();
  output(g_steps[g_at].hz);
}

void silence() {
  g_steps = nullptr;
  g_priority = 0;
  output(0);
}

}  // namespace sound
