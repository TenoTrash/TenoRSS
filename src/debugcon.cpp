#include "debugcon.h"

#include <Arduino.h>

#include "app.h"
#include "input.h"
#include "net.h"
#include "ui.h"

namespace dbg {

namespace {

char g_line[200];
size_t g_len = 0;

void sendKey(input::Kind kind, char ch, bool fn) {
  input::Event e;
  memset(&e, 0, sizeof(e));
  e.kind = kind;
  e.ch = ch;
  e.base = ch;
  e.fn = fn;
  input::inject(e);
}

void injectKeys(const char* s) {
  bool fn = false;
  for (; *s; s++) {
    if (*s != '\\') {
      sendKey(input::K_CHAR, *s, fn);
      fn = false;
      continue;
    }
    s++;
    switch (*s) {
      case 'n':
        sendKey(input::K_ENTER, 0, fn);
        break;
      case 'b':
        sendKey(input::K_BKSP, 0, fn);
        break;
      case 't':
        sendKey(input::K_TAB, 0, fn);
        break;
      case 'e':
        sendKey(input::K_CHAR, '`', fn);
        break;
      case 'g':
        sendKey(input::K_G0, 0, fn);
        break;
      case 'F':
        fn = true;
        continue;
      case '\\':
        sendKey(input::K_CHAR, '\\', fn);
        break;
      case 0:
        return;
      default:
        break;
    }
    fn = false;
  }
}

void printStatus() {
  net::Status st;
  net::getStatus(st);
  logf("#STAT pantalla=%s heap=%u min=%u bloque=%u pila_red=%u pila_ui=%u wifi=%d ssid=\"%s\" rssi=%d busy=%d fase=%d "
       "feed=%u/%u resultado=%d ok=%u err=%u nuevos=%u hora=%d",
       ui::screenName(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
       (unsigned)net::workerStackFree(), (unsigned)uxTaskGetStackHighWaterMark(nullptr), (int)st.wifi, st.ssid,
       (int)st.rssi, (int)st.busy, (int)st.phase, (unsigned)st.feedIdx, (unsigned)st.feedTotal, (int)st.outcome,
       (unsigned)st.okCount, (unsigned)st.failCount, (unsigned)st.newItems, (int)timeValid());
}

void runLine(const char* line) {
  if (line[0] != '#') return;
  if (!strncmp(line, "#k ", 3)) {
    injectKeys(line + 3);
  } else if (!strcmp(line, "#shot")) {
    ui::screenshot();
  } else if (!strcmp(line, "#stat")) {
    printStatus();
  } else if (!strcmp(line, "#splash")) {
    ui::debugSplash();
  } else if (!strcmp(line, "#reboot")) {
    logf("#REBOOT");
    delay(100);
    ESP.restart();
  }
}

}  // namespace

void poll() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (g_len > 0) {
        g_line[g_len] = 0;
        runLine(g_line);
        g_len = 0;
      }
    } else if (g_len < sizeof(g_line) - 1) {
      g_line[g_len++] = (char)c;
    }
  }
}

}  // namespace dbg
