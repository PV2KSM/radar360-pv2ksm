#include "Config.h"
#include "Lang.h"
#include <Preferences.h>

Config cfg;
static Preferences prefs;
static const char *NS = "r360";
static const char *DEFAULT_PASS = "radar360";

static void defaults()
{
  memset(&cfg, 0, sizeof(cfg));
  strlcpy(cfg.host, "radar", sizeof(cfg.host));
  strlcpy(cfg.webUser, "admin", sizeof(cfg.webUser));
  strlcpy(cfg.webPass, DEFAULT_PASS, sizeof(cfg.webPass));
  strlcpy(cfg.tz, "GMT0", sizeof(cfg.tz));
  strlcpy(cfg.ntp1, "pool.ntp.org", sizeof(cfg.ntp1));
  strlcpy(cfg.ntp2, "time.google.com", sizeof(cfg.ntp2));
  cfg.bright = 80;
  cfg.nightBright = 15;
  cfg.nightOn = false;
  cfg.nightStart = 22 * 60;
  cfg.nightEnd = 7 * 60;
  cfg.rotation = 0;
  cfg.cycleSecs = 0;
  cfg.showSeconds = true;
  cfg.cpuMhz = 240;
  cfg.cpuAuto = false;
  cfg.lcdTurbo = 0;
  cfg.lang = LANG_EN;
  uiLang = cfg.lang;
}

static void getStr(const char *key, char *dst, size_t n)
{
  if(prefs.isKey(key)) prefs.getString(key, dst, n);
}

void configLoad()
{
  defaults();
  if(!prefs.begin(NS, true)) return;   // primeira vez: namespace ainda nao existe
  char key[8];
  for(int i = 0; i < WIFI_SLOTS; i++)
  {
    snprintf(key, sizeof(key), "ws%d", i); getStr(key, cfg.wifi[i].ssid, sizeof(cfg.wifi[i].ssid));
    snprintf(key, sizeof(key), "wp%d", i); getStr(key, cfg.wifi[i].pass, sizeof(cfg.wifi[i].pass));
  }
  configCompactWifi();
  getStr("host", cfg.host, sizeof(cfg.host));
  getStr("wuser", cfg.webUser, sizeof(cfg.webUser));
  getStr("carto", cfg.cartoKey, sizeof(cfg.cartoKey));
  getStr("wpass", cfg.webPass, sizeof(cfg.webPass));
  getStr("tz", cfg.tz, sizeof(cfg.tz));
  getStr("ntp1", cfg.ntp1, sizeof(cfg.ntp1));
  getStr("ntp2", cfg.ntp2, sizeof(cfg.ntp2));
  cfg.bright = prefs.getUChar("br", cfg.bright);
  cfg.nightBright = prefs.getUChar("nbr", cfg.nightBright);
  cfg.nightOn = prefs.getBool("non", cfg.nightOn);
  cfg.nightStart = prefs.getUShort("ns", cfg.nightStart);
  cfg.nightEnd = prefs.getUShort("ne", cfg.nightEnd);
  cfg.rotation = prefs.getUChar("rot", cfg.rotation) & 3;
  cfg.cycleSecs = prefs.getUShort("cyc", cfg.cycleSecs);
  cfg.showSeconds = prefs.getBool("sec", cfg.showSeconds);
  cfg.cpuMhz = prefs.getUShort("cpu", cfg.cpuMhz);
  if(cfg.cpuMhz != 80 && cfg.cpuMhz != 160) cfg.cpuMhz = 240;
  cfg.cpuAuto = prefs.getBool("cpuA", cfg.cpuAuto);
  cfg.lcdTurbo = prefs.getUChar("lcdT", 0) == 1 ? 1 : 0;   // um teste que nao foi confirmado volta para 40 MHz
  cfg.lang = prefs.getUChar("lang", LANG_EN);
  if(cfg.lang > LANG_ES) cfg.lang = LANG_EN;
  uiLang = cfg.lang;
  prefs.end();
}

void configSave()
{
  if(!prefs.begin(NS, false)) return;
  char key[8];
  for(int i = 0; i < WIFI_SLOTS; i++)
  {
    // Espaco vazio nao ocupa a NVS: apaga a chave em vez de gravar texto vazio
    snprintf(key, sizeof(key), "ws%d", i);
    if(cfg.wifi[i].ssid[0]) prefs.putString(key, cfg.wifi[i].ssid); else if(prefs.isKey(key)) prefs.remove(key);
    snprintf(key, sizeof(key), "wp%d", i);
    if(cfg.wifi[i].ssid[0] && cfg.wifi[i].pass[0]) prefs.putString(key, cfg.wifi[i].pass); else if(prefs.isKey(key)) prefs.remove(key);
  }
  prefs.putString("host", cfg.host);
  prefs.putString("wuser", cfg.webUser);
  prefs.putString("carto", cfg.cartoKey);
  prefs.putString("wpass", cfg.webPass);
  prefs.putString("tz", cfg.tz);
  prefs.putString("ntp1", cfg.ntp1);
  prefs.putString("ntp2", cfg.ntp2);
  prefs.putUChar("br", cfg.bright);
  prefs.putUChar("nbr", cfg.nightBright);
  prefs.putBool("non", cfg.nightOn);
  prefs.putUShort("ns", cfg.nightStart);
  prefs.putUShort("ne", cfg.nightEnd);
  prefs.putUChar("rot", cfg.rotation);
  prefs.putUShort("cyc", cfg.cycleSecs);
  prefs.putBool("sec", cfg.showSeconds);
  prefs.putUShort("cpu", cfg.cpuMhz);
  prefs.putBool("cpuA", cfg.cpuAuto);
  prefs.putUChar("lcdT", cfg.lcdTurbo == 1 ? 1 : 0);
  prefs.putUChar("lang", cfg.lang);
  uiLang = cfg.lang;
  prefs.end();
}

void configFactoryReset()
{
  if(prefs.begin(NS, false)) { prefs.clear(); prefs.end(); }
  defaults();
}

void configCompactWifi()
{
  int n = 0;
  for(int i = 0; i < WIFI_SLOTS; i++)
    if(cfg.wifi[i].ssid[0]) { if(n != i) cfg.wifi[n] = cfg.wifi[i]; n++; }
  for(int i = n; i < WIFI_SLOTS; i++) memset(&cfg.wifi[i], 0, sizeof(cfg.wifi[i]));
}

bool configHasWifi()
{
  for(int i = 0; i < WIFI_SLOTS; i++) if(cfg.wifi[i].ssid[0]) return true;
  return false;
}

bool configDefaultPassword()
{
  return !strcmp(cfg.webPass, DEFAULT_PASS);
}
