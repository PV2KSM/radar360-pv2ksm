// Radar360 PV2KSM - modo de recuperacao (particao "recovery", tipo factory)
// Sistema minimo e separado: conecta no Wi-Fi salvo (ou cria uma rede), mostra o endereco na tela e
// oferece uma pagina para enviar o firmware e o pacote visual. Nao e atualizado pelo OTA, so pelo cabo.
// Board.h, Display.h e Panel.cpp sao copiados do firmware principal pelo build.sh.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include "Display.h"

#define REC_VERSION "1.0"
static const uint32_t MAGIC = 0x52454332;
static RTC_NOINIT_ATTR uint32_t hopMagic, hops;   // tentativas de voltar ao firmware normal que nao deram certo

LGFX lcd;
static WebServer server(80);
static char wuser[17] = "admin", wpass[33] = "radar360", host[32] = "radar";
static String why, retLabel = "app0", ipText, netText;
static bool fwOk = false, pkOk = false;
static String upErr;
static uint32_t pkDone = 0, pkErased = 0;
static uint8_t pkHead[20];

// ---------- tela ----------
// As fontes da tela nao tem acentos: troca as letras acentuadas mais comuns
static String plain(const String &in)
{
  static const char *from[] = {"á","à","â","ã","é","ê","í","ó","ô","õ","ú","ç","Á","É","Í","Ó","Ú","Ç","Ã","Õ"};
  static const char *to[]   = {"a","a","a","a","e","e","i","o","o","o","u","c","A","E","I","O","U","C","A","O"};
  String s = in;
  for(size_t i = 0; i < sizeof(from) / sizeof(from[0]); i++) s.replace(from[i], to[i]);
  return s;
}

static void screen(const char *status = nullptr)
{
  lcd.fillScreen(TFT_BLACK);
  lcd.drawCircle(180, 180, 176, 0x2A49);
  lcd.setTextDatum(middle_center);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(0xFE60);
  lcd.drawString("Modo recuperacao", 180, 80);
  lcd.setFont(&fonts::FreeSans9pt7b);
  lcd.setTextColor(0x9CF3);
  String w = plain(why.length() ? why : String("toque o botao para sair"));
  if(w.length() > 34) w = w.substring(0, 34);
  lcd.drawString(w, 180, 112);
  lcd.setTextColor(TFT_WHITE);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  if(netText.length()) lcd.drawString(plain(netText), 180, 165);
  lcd.setFont(&fonts::FreeSansBold18pt7b);
  lcd.setTextColor(0x5FFA);
  if(ipText.length()) lcd.drawString(ipText, 180, 205);
  lcd.setFont(&fonts::FreeSans9pt7b);
  lcd.setTextColor(0x9CF3);
  if(status) lcd.drawString(plain(status), 180, 255);
  else lcd.drawString("Abra o endereco no navegador", 180, 255);
  lcd.drawString("Botao: iniciar firmware normal", 180, 285);
}

static void progress(const char *what, int pct)
{
  static int last = -1;
  if(pct == last) return;
  last = pct;
  lcd.fillRect(60, 240, 240, 60, TFT_BLACK);
  lcd.setFont(&fonts::FreeSans9pt7b);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(plain(what), 180, 252);
  lcd.drawRoundRect(80, 270, 200, 14, 7, 0x6B4D);
  if(pct > 0) lcd.fillRoundRect(82, 272, 196 * pct / 100, 10, 5, 0x5FFA);
}

// ---------- decidir: ficar aqui ou voltar ao firmware normal ----------
static const esp_partition_t *appPart(const char *label)
{
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
}

static bool appValid(const esp_partition_t *p, esp_app_desc_t *d = nullptr)
{
  esp_app_desc_t tmp;
  return p && esp_ota_get_partition_description(p, d ? d : &tmp) == ESP_OK;
}

static bool bootNormal()
{
  const esp_partition_t *p = appPart(retLabel.c_str());
  if(!appValid(p)) p = appPart("app0");
  if(!appValid(p)) p = appPart("app1");
  if(!appValid(p)) return false;
  Preferences pr;
  if(pr.begin("r360boot", false)) { pr.remove("why"); pr.end(); }
  if(esp_ota_set_boot_partition(p) != ESP_OK) return false;   // confere a imagem antes
  hopMagic = MAGIC; hops++;
  delay(50);
  esp_restart();
  return true;
}

// otadata em branco = primeira vez depois da gravacao pelo cabo (ou a recuperacao foi escolhida e o
// motivo ficou gravado). Com otadata preenchido e sem motivo, o bootloader caiu aqui porque o firmware
// normal nao e valido (ou nao confirmou depois de uma atualizacao): fica na recuperacao.
static bool otadataBlank()
{
  const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if(!p) return true;
  uint8_t b[64];
  if(esp_partition_read(p, 0, b, sizeof(b)) != ESP_OK) return false;
  for(uint8_t c : b) if(c != 0xFF) return false;
  if(esp_partition_read(p, 0x1000, b, sizeof(b)) != ESP_OK) return false;
  for(uint8_t c : b) if(c != 0xFF) return false;
  return true;
}

// ---------- pagina ----------
static bool auth()
{
  if(server.authenticate(wuser, wpass)) return true;
  server.requestAuthentication(BASIC_AUTH, "Radar360");
  return false;
}

static String appLine(const char *label)
{
  esp_app_desc_t d;
  const esp_partition_t *p = appPart(label);
  if(!appValid(p, &d)) return String(label) + ": vazio";
  return String(label) + ": " + d.project_name + " " + d.version;
}

static String packLine()
{
  const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "assets");
  if(!p) return "sem particao assets";
  uint8_t h[20];
  if(esp_partition_read(p, 0, h, 20) != ESP_OK || memcmp(h, "R360PK01", 8)) return "nenhum";
  uint32_t v; memcpy(&v, h + 8, 4);
  return "v" + String(v);
}

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Radar 360 · Recuperação</title>
<style>body{margin:0;background:#100c05;color:#fff2d6;font:15px system-ui,sans-serif}main{max-width:560px;margin:auto;padding:18px}
h1{font-size:20px;color:#ffcf40;margin:6px 0 2px}.c{background:#1c160b;border:1px solid #3d311a;border-radius:14px;padding:16px;margin:14px 0}
h2{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:#ffcf40;margin:0 0 10px}p{color:#c9b892;margin:6px 0}
input[type=file]{width:100%;margin:8px 0;color:#fff2d6}button{background:#ffcf40;color:#1c1300;border:0;border-radius:10px;padding:11px 16px;font:inherit;font-weight:600;cursor:pointer}
button.s{background:none;color:#ffcf40;border:1px solid #6b5626}progress{width:100%;height:10px;margin-top:10px}code{color:#ffcf40}</style></head><body><main>
<h1>⚠ Modo recuperação</h1>)HTML";

static const char PAGE_TAIL[] PROGMEM = R"HTML(
<div class="c"><h2>Firmware</h2><p>Arquivo <code>radar360-vX-ota.bin</code>.</p><input type="file" id="fw" accept=".bin"><button onclick="up('fw','/fw')">Enviar firmware</button><progress id="fwP" value="0" max="100"></progress><p id="fwM"></p></div>
<div class="c"><h2>Pacote visual</h2><p>Arquivo <code>radar360-pack-vX.bin</code>.</p><input type="file" id="pk" accept=".bin"><button onclick="up('pk','/pack')">Enviar pacote</button><progress id="pkP" value="0" max="100"></progress><p id="pkM"></p></div>
<div class="c"><h2>Sair</h2><button onclick="go('/boot')">Iniciar firmware normal</button> <button class="s" onclick="go('/reboot')">Reiniciar</button><p id="gM"></p></div>
<script>
function up(id,url){const f=document.getElementById(id).files[0];if(!f)return;const fd=new FormData();fd.append('f',f,f.name);const x=new XMLHttpRequest();x.open('POST',url);
x.upload.onprogress=e=>{if(e.lengthComputable)document.getElementById(id+'P').value=e.loaded/e.total*100};
x.onload=()=>{document.getElementById(id+'M').textContent=x.responseText};x.onerror=()=>{document.getElementById(id+'M').textContent='Falha na conexão'};x.send(fd)}
function go(u){fetch(u,{method:'POST'}).then(r=>r.text()).then(t=>document.getElementById('gM').textContent=t)}
</script></main></body></html>)HTML";

static void handleRoot()
{
  if(!auth()) return;
  String h = FPSTR(PAGE_HEAD);
  h += "<p>" + (why.length() ? why : String("Aberto pelo usuário")) + "</p>";
  h += "<div class=\"c\"><h2>Instalado</h2><p>" + appLine("app0") + "<br>" + appLine("app1") + "<br>Pacote visual: " + packLine() + "<br>Recuperação " REC_VERSION "</p></div>";
  h += FPSTR(PAGE_TAIL);
  server.send(200, "text/html; charset=utf-8", h);
}

// Firmware: Update grava na primeira particao de app (app0) e marca para iniciar por ela
static void fwUpload()
{
  HTTPUpload &u = server.upload();
  if(u.status == UPLOAD_FILE_START)
  {
    fwOk = false; upErr = "";
    if(!server.authenticate(wuser, wpass)) { upErr = "Sem login"; return; }
    if(!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) upErr = String("Não iniciou: ") + Update.errorString();
    progress("Gravando firmware", 0);
  }
  else if(u.status == UPLOAD_FILE_WRITE)
  {
    if(upErr.length()) return;
    if(u.totalSize == 0 && (u.currentSize < 16 || u.buf[0] != 0xE9)) { upErr = "Não é um firmware ESP32"; Update.abort(); return; }
    if(Update.write(u.buf, u.currentSize) != u.currentSize) { upErr = String("Erro ao gravar: ") + Update.errorString(); Update.abort(); return; }
    progress("Gravando firmware", min(99, (int)((u.totalSize + u.currentSize) / 21000)));
  }
  else if(u.status == UPLOAD_FILE_END)
  {
    if(upErr.length()) return;
    if(!Update.end(true)) { upErr = String("Verificação falhou: ") + Update.errorString(); return; }
    fwOk = true;
    progress("Firmware gravado", 100);
  }
  else if(u.status == UPLOAD_FILE_ABORTED) { Update.abort(); upErr = "Envio interrompido"; }
}

static void fwDone()
{
  if(!auth()) return;
  if(!fwOk) { server.send(400, "text/plain; charset=utf-8", upErr.length() ? upErr : String("Falhou")); screen(); return; }
  server.send(200, "text/plain; charset=utf-8", "Firmware gravado. Reiniciando nele…");
  Preferences pr; if(pr.begin("r360boot", false)) { pr.remove("why"); pr.end(); }
  delay(600);
  esp_restart();
}

// Pacote: mesma regra do firmware principal (marcador gravado por ultimo, depois do CRC)
static void pkUpload()
{
  HTTPUpload &u = server.upload();
  const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "assets");
  if(u.status == UPLOAD_FILE_START)
  {
    pkOk = false; upErr = ""; pkDone = 0; pkErased = 0;
    if(!server.authenticate(wuser, wpass)) { upErr = "Sem login"; return; }
    if(!p) upErr = "Sem partição assets";
    progress("Gravando pacote", 0);
    return;
  }
  if(upErr.length() || !p) return;
  if(u.status == UPLOAD_FILE_WRITE)
  {
    const uint8_t *src = u.buf; size_t n = u.currentSize; uint32_t at = pkDone;
    if(pkDone == 0)
    {
      if(n < 20 || memcmp(src, "R360PK01", 8)) { upErr = "Não é um pacote visual do Radar 360"; return; }
      memcpy(pkHead, src, 20);
    }
    if(pkDone + n > p->size) { upErr = "Pacote maior que a partição"; return; }
    while(pkErased < pkDone + n)
    {
      if(esp_partition_erase_range(p, pkErased, 0x10000) != ESP_OK) { upErr = "Erro ao apagar"; return; }
      pkErased += 0x10000;
    }
    if(at < 8) { const size_t k = min((size_t)(8 - at), n); src += k; n -= k; at += k; }
    if(n && esp_partition_write(p, at, src, n) != ESP_OK) { upErr = "Erro ao gravar"; return; }
    pkDone += u.currentSize;
    progress("Gravando pacote", min(99, (int)(pkDone / 20000)));
  }
  else if(u.status == UPLOAD_FILE_END)
  {
    uint32_t cnt, crc; memcpy(&cnt, pkHead + 12, 4); memcpy(&crc, pkHead + 16, 4);
    const uint32_t head = 20 + cnt * 32;
    if(cnt == 0 || cnt > 256 || head >= pkDone) { upErr = "Pacote incompleto"; return; }
    static uint8_t buf[4096];
    uint32_t c = 0;
    for(uint32_t o = head; o < pkDone; )
    {
      const uint32_t k = min((uint32_t)sizeof(buf), pkDone - o);
      esp_partition_read(p, o, buf, k);
      c = esp_rom_crc32_le(c, buf, k);
      o += k;
    }
    if(c != crc) { upErr = "Pacote corrompido (CRC): envie de novo"; return; }
    if(esp_partition_write(p, 0, pkHead, 8) != ESP_OK) { upErr = "Erro ao gravar o cabeçalho"; return; }
    pkOk = true;
    progress("Pacote gravado", 100);
  }
}

static void pkDoneH()
{
  if(!auth()) return;
  server.send(pkOk ? 200 : 400, "text/plain; charset=utf-8", pkOk ? String("Pacote gravado.") : (upErr.length() ? upErr : String("Falhou")));
  delay(1500);
  screen();
}

// ---------- rede ----------
static bool tryWifi()
{
  Preferences pr;
  if(!pr.begin("r360", true)) return false;
  String ss[20], pw[20]; int n = 0;
  for(int i = 0; i < 20; i++)
  {
    char k[8];
    snprintf(k, sizeof(k), "ws%d", i); String s = pr.isKey(k) ? pr.getString(k) : String();
    snprintf(k, sizeof(k), "wp%d", i); String p = pr.isKey(k) ? pr.getString(k) : String();
    if(s.length()) { ss[n] = s; pw[n] = p; n++; }
  }
  if(pr.isKey("wuser")) pr.getString("wuser", wuser, sizeof(wuser));
  if(pr.isKey("wpass")) pr.getString("wpass", wpass, sizeof(wpass));
  if(pr.isKey("host")) pr.getString("host", host, sizeof(host));
  pr.end();
  if(!n) return false;
  WiFi.mode(WIFI_STA);
  const int found = WiFi.scanNetworks();
  // primeiro as redes salvas que apareceram, da mais forte para a mais fraca
  for(int pass = 0; pass < 2; pass++)
    for(int i = 0; i < n; i++)
    {
      bool seen = false;
      for(int k = 0; k < found; k++) if(WiFi.SSID(k).equalsIgnoreCase(ss[i])) seen = true;
      if(seen != (pass == 0)) continue;
      netText = ss[i]; ipText = ""; screen("Conectando…");
      WiFi.begin(ss[i].c_str(), pw[i].c_str());
      const uint32_t t0 = millis();
      while(millis() - t0 < 12000 && WiFi.status() != WL_CONNECTED) { delay(100); if(digitalRead(PIN_BUTTON) == LOW) bootNormal(); }
      if(WiFi.status() == WL_CONNECTED) return true;
      WiFi.disconnect();
    }
  return false;
}

void setup()
{
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  {
    Preferences pr;
    if(pr.begin("r360boot", true)) { why = pr.isKey("why") ? pr.getString("why") : String(); if(pr.isKey("ret")) retLabel = pr.getString("ret"); pr.end(); }
  }
  if(hopMagic != MAGIC || esp_reset_reason() == ESP_RST_POWERON) { hopMagic = MAGIC; hops = 0; }
  Serial.printf("[rec] recuperacao %s, motivo: '%s', voltas: %lu\n", REC_VERSION, why.c_str(), (unsigned long)hops);

  // Sem pedido (primeira gravacao pelo cabo, ou o bootloader caiu aqui): tenta o firmware normal uma vez
  if(!why.length() && hops == 0 && otadataBlank() && digitalRead(PIN_BUTTON) != LOW && bootNormal()) return;
  if(!why.length()) why = otadataBlank() ? "nenhum firmware válido" : "o firmware normal não iniciou";
  hops = 0;

  lcd.init();
  lcd.setBrightness(150);
  screen("Procurando Wi-Fi…");

  if(tryWifi()) ipText = WiFi.localIP().toString();
  else
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Radar360-Recuperacao");
    netText = "Rede: Radar360-Recuperacao";
    ipText = WiFi.softAPIP().toString();
  }
  if(MDNS.begin(host)) MDNS.addService("http", "tcp", 80);
  screen();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/fw", HTTP_POST, fwDone, fwUpload);
  server.on("/pack", HTTP_POST, pkDoneH, pkUpload);
  server.on("/boot", HTTP_POST, []() { if(!auth()) return; server.send(200, "text/plain; charset=utf-8", "Iniciando o firmware normal…"); delay(500); if(!bootNormal()) screen("Nenhum firmware valido"); });
  server.on("/reboot", HTTP_POST, []() { if(!auth()) return; server.send(200, "text/plain; charset=utf-8", "Reiniciando…"); delay(500); esp_restart(); });
  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302); });
  server.begin();
}

void loop()
{
  server.handleClient();
  // Toque no botao: volta ao firmware normal
  static uint32_t downAt = 0;
  if(digitalRead(PIN_BUTTON) == LOW) { if(!downAt) downAt = millis(); }
  else if(downAt) { if(millis() - downAt > 50) { screen("Iniciando o firmware normal…"); if(!bootNormal()) screen("Nenhum firmware valido"); } downAt = 0; }
  delay(2);
}
