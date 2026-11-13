#include "input.h"

#include <M5Cardputer.h>

#include <algorithm>
#include <vector>

namespace input {

namespace {

const size_t kQueueSize = 16;
Event g_queue[kQueueSize];
size_t g_head = 0;
size_t g_tail = 0;

std::vector<Point2D_t> g_prev;  // teclas que estaban apretadas en la pasada anterior
bool g_hasRepeat = false;
Point2D_t g_repeatKey;
uint32_t g_repeatAt = 0;

const uint32_t kRepeatDelayMs = 420;
const uint32_t kRepeatRateMs = 70;

void push(const Event& e) {
  const size_t n = (g_tail + 1) % kQueueSize;
  if (n == g_head) return;  // cola llena: se descarta
  g_queue[g_tail] = e;
  g_tail = n;
}

bool isModifier(uint8_t code) {
  return code == KEY_FN || code == KEY_OPT || code == KEY_LEFT_CTRL || code == KEY_LEFT_SHIFT ||
         code == KEY_LEFT_ALT;
}

bool makeEvent(const Point2D_t& pos, bool repeat, Event& e) {
  Keyboard_Class& kb = M5Cardputer.Keyboard;
  const KeyValue_t kv = kb.getKeyValue(pos);
  const uint8_t code = (uint8_t)kv.value_first;
  if (isModifier(code)) return false;
  const Keyboard_Class::KeysState& st = kb.keysState();
  memset(&e, 0, sizeof(e));
  e.fn = st.fn;
  e.shift = st.shift;
  e.ctrl = st.ctrl;
  e.opt = st.opt;
  e.alt = st.alt;
  e.repeat = repeat;
  // Las teclas especiales se reconocen por su valor sin Shift: los codigos de
  // Enter/Backspace/Tab coinciden con los ASCII de ( * + que solo salen con Shift.
  if (code == KEY_ENTER) {
    e.kind = K_ENTER;
  } else if (code == KEY_BACKSPACE) {
    e.kind = K_BKSP;
  } else if (code == KEY_TAB) {
    e.kind = K_TAB;
  } else {
    e.kind = K_CHAR;
    e.base = kv.value_first;
    e.ch = st.shift ? kv.value_second : kv.value_first;
  }
  return true;
}

}  // namespace

void poll() {
  M5Cardputer.update();
  const std::vector<Point2D_t>& keys = M5Cardputer.Keyboard.keyList();

  for (size_t i = 0; i < keys.size(); i++) {
    if (std::find(g_prev.begin(), g_prev.end(), keys[i]) != g_prev.end()) continue;
    Event e;
    if (makeEvent(keys[i], false, e)) {
      push(e);
      g_hasRepeat = true;
      g_repeatKey = keys[i];
      g_repeatAt = millis() + kRepeatDelayMs;
    }
  }
  if (g_hasRepeat) {
    if (std::find(keys.begin(), keys.end(), g_repeatKey) == keys.end()) {
      g_hasRepeat = false;
    } else if ((int32_t)(millis() - g_repeatAt) >= 0) {
      Event e;
      if (makeEvent(g_repeatKey, true, e)) push(e);
      g_repeatAt = millis() + kRepeatRateMs;
    }
  }
  g_prev = keys;

  if (M5Cardputer.BtnA.wasPressed()) {
    Event e;
    memset(&e, 0, sizeof(e));
    e.kind = K_G0;
    push(e);
  }
}

bool next(Event& ev) {
  if (g_head == g_tail) return false;
  ev = g_queue[g_head];
  g_head = (g_head + 1) % kQueueSize;
  return true;
}

void inject(const Event& ev) { push(ev); }

Nav navOf(const Event& ev) {
  Nav n = N_NONE;
  switch (ev.kind) {
    case K_ENTER:
      n = N_OK;
      break;
    case K_BKSP:
      n = N_DEL;
      break;
    case K_G0:
      n = N_BACK;
      break;
    case K_CHAR:
      if (ev.base == ';') n = N_UP;
      else if (ev.base == '.') n = N_DOWN;
      else if (ev.base == ',') n = N_LEFT;
      else if (ev.base == '/') n = N_RIGHT;
      else if (ev.base == '`') n = N_BACK;
      break;
    default:
      break;
  }
  // mantener apretado solo repite el movimiento, nunca aceptar/volver/borrar
  if (ev.repeat && (n == N_OK || n == N_BACK || n == N_DEL)) return N_NONE;
  return n;
}

}  // namespace input
