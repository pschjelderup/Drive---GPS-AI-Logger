#include "customers.h"

#include <SD_MMC.h>

#include "config.h"
#include "sensors.h"

namespace {

// Sextio kunder ar fler an nagon valjer mellan pa en pekskarm i en bil, och
// listan far kosta minne motsvarande det - inte mer. Radas det upp fler i filen
// lases de forsta sextio in, sa att en for lang fil ger en kortare lista i
// stallet for ett kort som inte startar.
const uint8_t kMaxCustomers = 60;
const uint8_t kNameLen = 40;

char g_names[kMaxCustomers][kNameLen];
uint8_t g_count = 0;

void trim(char *s) {
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' ||
                     s[len - 1] == ' ' || s[len - 1] == '\t')) {
    s[--len] = '\0';
  }
  size_t start = 0;
  while (s[start] == ' ' || s[start] == '\t') start++;
  if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
}

}  // namespace

namespace customers {

void begin() { reload(); }

void reload() {
  g_count = 0;
  if (!sensors::sdMounted()) return;

  File f = SD_MMC.open(CUSTOMERS_FILE, FILE_READ);
  if (!f) return;

  char line[96];
  while (f.available() && g_count < kMaxCustomers) {
    const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    trim(line);
    if (line[0] == '\0') continue;
    if (line[0] == '#') continue;  // kommentarsrad

    // Star det ett id forst ar namnet det som kommer efter semikolonet.
    const char *name = line;
    char *sep = strchr(line, ';');
    if (sep) {
      *sep = '\0';
      name = sep + 1;
      while (*name == ' ') name++;
      if (*name == '\0') continue;
    }

    strncpy(g_names[g_count], name, kNameLen - 1);
    g_names[g_count][kNameLen - 1] = '\0';
    g_count++;
  }
  f.close();
}

uint8_t count() { return g_count; }

const char *name(uint8_t i) {
  if (i >= g_count) return "";
  return g_names[i];
}

}  // namespace customers
