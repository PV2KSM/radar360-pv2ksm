// ---- Estresse: todas as telas, estilos, cenas, horas e dados estranhos ----
#include <random>
static std::mt19937 rng(1234);
static int rnd(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

static std::string weatherJson(int code, float temp, int hours, bool empty)
{
  const long now = (long)simNow, h0 = now - now % 3600, d0 = now - now % 86400 + 3 * 3600;
  char b[256]; std::string j = "{\"utc_offset_seconds\":-10800,";
  snprintf(b, sizeof(b), "\"current\":{\"temperature_2m\":%.1f,\"relative_humidity_2m\":%d,\"apparent_temperature\":%.1f,\"is_day\":%d,\"precipitation\":%.1f,\"weather_code\":%d,\"cloud_cover\":50,\"pressure_msl\":1013,\"wind_speed_10m\":%d,\"wind_direction_10m\":%d,\"wind_gusts_10m\":60},",
           temp, rnd(0, 100), temp + 2, rnd(0, 1), rnd(0, 1) ? 2.5 : 0.0, code, rnd(0, 140), rnd(0, 360));
  j += b;
  if(empty) { j += "\"hourly\":{},\"daily\":{},\"minutely_15\":{}}"; return j; }
  j += "\"hourly\":{\"time\":[";
  for(int i = 0; i < hours; i++) { j += std::to_string(h0 + i * 3600); if(i < hours - 1) j += ","; }
  j += "],\"temperature_2m\":[";
  for(int i = 0; i < hours; i++) { j += std::to_string(temp + rnd(-15, 15)); if(i < hours - 1) j += ","; }
  j += "],\"precipitation_probability\":[";
  for(int i = 0; i < hours; i++) { j += std::to_string(rnd(0, 100)); if(i < hours - 1) j += ","; }
  j += "],\"precipitation\":[";
  for(int i = 0; i < hours; i++) { j += std::to_string(rnd(0, 30) / 3.0); if(i < hours - 1) j += ","; }
  j += "],\"weather_code\":[";
  for(int i = 0; i < hours; i++) { j += std::to_string(code); if(i < hours - 1) j += ","; }
  j += "],\"is_day\":[";
  for(int i = 0; i < hours; i++) { j += std::to_string(rnd(0, 1)); if(i < hours - 1) j += ","; }
  j += "]},\"daily\":{\"time\":[";
  for(int i = 0; i < 7; i++) { j += std::to_string(d0 + i * 86400); if(i < 6) j += ","; }
  j += "],\"weather_code\":[";
  for(int i = 0; i < 7; i++) { j += std::to_string(code); if(i < 6) j += ","; }
  j += "],\"temperature_2m_max\":[";
  for(int i = 0; i < 7; i++) { j += std::to_string(temp + rnd(0, 20)); if(i < 6) j += ","; }
  j += "],\"temperature_2m_min\":[";
  for(int i = 0; i < 7; i++) { j += std::to_string(temp - rnd(0, 20)); if(i < 6) j += ","; }
  j += "],\"precipitation_probability_max\":[";
  for(int i = 0; i < 7; i++) { j += std::to_string(rnd(0, 100)); if(i < 6) j += ","; }
  j += "],\"precipitation_sum\":[1,2,3,4,5,6,7],\"sunrise\":[";
  for(int i = 0; i < 7; i++) { j += std::to_string(d0 + 5 * 3600 + i * 86400); if(i < 6) j += ","; }
  j += "],\"sunset\":[";
  for(int i = 0; i < 7; i++) { j += std::to_string(d0 + 18 * 3600 + i * 86400); if(i < 6) j += ","; }
  j += "]},\"minutely_15\":{\"time\":[";
  for(int i = 0; i < 8; i++) { j += std::to_string(now - now % 900 + i * 900); if(i < 7) j += ","; }
  j += "],\"precipitation\":[0,0,0.5,1,0,0,2,0]}}";
  return j;
}

static std::string flightsJson(int n)
{
  static const char *T[] = {"A20N", "B38M", "B789", "AT76", "AS50", "C172", "C68A", "E195", "ZZZZ", "", "A388", "R44", "PC12", "GLF6"};
  static const char *C[] = {"A1", "A2", "A3", "A4", "A5", "A7", "B1", "C2", "", "X9"};
  std::string j = "{\"ac\":[";
  for(int i = 0; i < n; i++)
  {
    char b[400];
    const double lat = -23.229 + rnd(-200, 200) / 100.0, lon = -45.871 + rnd(-200, 200) / 100.0;
    const char *call = (i % 7 == 0) ? "ABCDEFGHIJKLMNOP" : (i % 5 == 0) ? "" : "TAM3311 ";
    snprintf(b, sizeof(b), "{\"hex\":\"e4%04x\",\"flight\":\"%s\",\"r\":\"PR-%c%c%c\",\"t\":\"%s\",\"category\":\"%s\",\"lat\":%.5f,\"lon\":%.5f,%s\"gs\":%d,\"track\":%.1f,\"baro_rate\":%d,\"squawk\":\"7700\"}%s",
             i, call, 'A' + i % 26, 'B', 'C', T[rnd(0, 13)], C[rnd(0, 9)], lat, lon,
             (i % 6 == 0) ? "\"alt_baro\":\"ground\"," : (std::string("\"alt_baro\":") + std::to_string(rnd(-1000, 60000)) + ",").c_str(),
             rnd(0, 650), rnd(0, 3599) / 10.0, rnd(-6000, 6000), i < n - 1 ? "," : "");
    j += b;
  }
  return j + "]}";
}

static std::string newsXml(int n, bool atom)
{
  std::string x = atom ? "<feed>" : "<rss><channel>";
  for(int i = 0; i < n; i++)
  {
    std::string t;
    if(i % 4 == 0) for(int k = 0; k < 40; k++) t += "Manchete enorme com acentuação çãõ &amp; &#233; &#x1F600; 😀 ";
    else if(i % 4 == 1) t = "Curta";
    else if(i % 4 == 2) t = "&lt;b&gt;Tag&lt;/b&gt; Supercalifragilisticexpialidocioussupercalifragilisticexpialidocious palavra gigante sem espaço";
    else t = "Notícia normal sobre o tempo no Vale do Paraíba - Fonte Muito Comprida Ltda";
    x += atom ? "<entry><title>" + t + "</title><link href=\"https://x/" + std::to_string(i) + "\"/><updated>2026-11-13T12:00:00Z</updated></entry>"
              : "<item><title>" + t + "</title><link>https://news.google.com/rss/articles/" + std::string(300, 'a') + "</link><pubDate>Fri, 13 Nov 2026 13:20:00 GMT</pubDate>" +
                (i % 3 ? "<source url=\"x\">Fonte Muito Comprida Ltda</source>" : "") + "</item>";
  }
  return x + (atom ? "</feed>" : "</channel></rss>");
}

static std::string dmrHtml(int n)
{
  std::string h = "<table>";
  for(int i = 0; i < n; i++)
  {
    h += "<tr><td>10:" + std::to_string(10 + i) + ":01</td><td>DMR Slot " + std::to_string(1 + i % 2) + "</td><td>" +
         (i % 3 == 0 ? std::string("PY2ABCDEFGHIJKLMNOP/MOBILE") : std::string("PU2XYZ")) + "</td><td>TG " + std::to_string(rnd(1, 9999999)) +
         "</td><td>Net</td><td>" + (i == 0 ? "TX" : "12.4") + "</td><td>0%</td></tr><tr><td></td></tr><tr>";
  }
  return h + "</table>";
}

static void drawAll(int &count)
{
  {
    // detalhes: trocando de aviao e fechando no meio
    simScreen = SCR_FLIGHTS; flightsOpen(); flightsWork();
    for(int k = 0; k < 24; k++)
    {
      if(k % 6 == 0) flightsLongPress();
      flightsStep(k % 2 ? 1 : -1); flightsWork(); flightsDraw(); count++;
      if(k % 7 == 6) flightsClose();
    }
    flightsClose();
  }
  {
    feat.scrPc = true; strlcpy(feat.pcHost, "pc.local", sizeof(feat.pcHost)); simScreen = SCR_PC; pcOpen();
    const char *bad[] = {"", "{", "[]", "null", "{\"Children\":5}", "{\"Children\":[{\"Text\":7,\"Children\":[{\"SensorId\":\"/amdcpu/0/load/0\",\"Type\":\"Load\",\"Text\":\"CPU Total\",\"Value\":\"abc %\"}]}]}",
                         "{\"Children\":[{\"Children\":[{\"SensorId\":\"/nic/x\",\"Type\":\"Throughput\",\"Text\":\"Download Speed\",\"Value\":\"1e30 GB/s\"}]}]}"};
    for(int k = 0; k < 60; k++)
    {
      overrideData.clear(); overrideFail.clear();
      if(k % 5 == 4) overrideFail["data.json"] = -1;
      else if(k % 7 == 3) overrideData["data.json"] = bad[(k / 7) % 7];
      else overrideData["data.json"] = lhmJson(k, k % 3 != 0);
      simMillis += 2001; pcWork();
      feat.pcStyle = k % 3; feat.pcSecs = 1 + k % 7; pcDraw(); count++;
    }
    std::string deep; for(int d = 0; d < 60; d++) deep += "{\"Children\":["; deep += "{}"; for(int d = 0; d < 60; d++) deep += "]}";
    overrideData["data.json"] = deep; simMillis += 2001; pcWork(); pcDraw(); count++;
    overrideData.clear(); overrideFail.clear(); pcClose(); feat.scrPc = false; feat.pcStyle = 0;
  }
  {
    // Cotacoes: respostas estranhas, pares faltando, historico quebrado, lista mudando
    feat.scrQuotes = true; quotesBegin(); simScreen = SCR_QUOTES; quotesOpen();
    const char *last[] = {"", "{", "[]", "null", "{\"USDBRL\":5}", "{\"USDBRL\":{\"bid\":\"abc\",\"pctChange\":\"1e40\"}}",
                          "{\"USDBRL\":{\"bid\":\"-3\",\"pctChange\":\"-99\",\"high\":\"1e30\",\"low\":\"0\",\"name\":\"Uma moeda com um nome muito longo mesmo/Real\"}}",
                          "{\"USDBRL\":{\"bid\":\"5.1\",\"pctChange\":\"0.3\"},\"BTCBRL\":{\"bid\":\"999999999\",\"pctChange\":\"-0.001\"}}"};
    const char *daily[] = {"", "{}", "[]", "[{\"bid\":\"5\"}]", "[{\"bid\":\"5\"},{\"bid\":\"5\"},{\"bid\":\"5\"}]", "[{\"x\":1},{\"bid\":\"nan\"},{\"bid\":\"2\"}]"};
    const char *codes[] = {"USD", "BTC", "EUR", "XYZ12", "BRL", "DOGE", "A", "ZZZZZZZ"};
    for(int k = 0; k < 80; k++)
    {
      overrideData.clear(); overrideFail.clear();
      if(k % 9 == 8) overrideFail["json/last"] = 404;
      else overrideData["json/last"] = last[k % 8];
      if(k % 5 == 4) overrideFail["json/daily"] = -1; else overrideData["json/daily"] = daily[k % 6];
      if(k % 10 == 0) { feat.qCount = k % 11; for(int i = 0; i < feat.qCount; i++) strlcpy(feat.qItems[i], codes[(i + k) % 8], 8); featGen++; }
      feat.qStyle = k % 2; feat.qSecs = 2 + k % 5;
      for(int w = 0; w < 3; w++) { simMillis += 61000; quotesWork(); }
      quotesTick(); quotesDraw(); count++;
    }
    overrideData.clear(); overrideFail.clear(); quotesClose(); feat.scrQuotes = false; feat.qStyle = 0;
  }
  for(int st = 0; st < 3; st++) for(int d = 0; d < 2; d++) { feat.wHoursStyle = st; feat.wDaysStyle = d; simScreen = SCR_WX_HOURS; weatherDrawHours(); simScreen = SCR_WX_DAYS; weatherDrawDays(); count += 2; }
  feat.wHoursStyle = 0; feat.wDaysStyle = 0;
  for(int st = 0; st < CLK_COUNT; st++) { feat.cStyle = st; simScreen = SCR_CLOCK; clockDraw(); count++; }
  simScreen = SCR_WX_NOW;
  for(int s = 0; s < 2; s++) { feat.wNowStyle = s; weatherOpen(); simMillis += 300; weatherDrawNow(); simMillis += 900; weatherDrawNow(); count += 2; }
  weatherOpen(); simMillis += 200; weatherDrawHours(); simMillis += 900; weatherDrawHours();
  weatherOpen(); simMillis += 200; weatherDrawDays(); simMillis += 900; weatherDrawDays(); count += 4;
  simScreen = SCR_WX_RADAR; weatherDrawRadar(); count++;
  for(int light = 0; light < 2; light++)
  {
    fakeRadar(light);
    for(int st = 0; st <= 10; st++) for(int k = 0; k < 3; k++) { feat.wCardStyle = st; simMillis += 333; weatherDrawRadar(); weatherRadarLongPress(); count++; }
  }
  feat.wCardStyle = 0;
  simScreen = SCR_MOON;
  for(int d = 0; d < 60; d++) { simNow += 86400 / 2 + 3333; moonDraw(); count++; }
  for(double la : {-89.0, -60.0, 0.0, 70.0, 89.9}) { feat.lat = la; feat.lon = la * 2; moonDraw(); count++; }
  feat.lat = 51.5074; feat.lon = -0.1278;
  for(int st = 0; st < 4; st++) { feat.wStyle = st; feat.fStyle = st; featIsDaylight(feat.lat, feat.lon); featViewNow(st, feat.lat, feat.lon); }
}

static void stress()
{
  int count = 0;
  const time_t base = local(2026, 11, 13, 0, 7, 0);
  static const int CODES[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99, 7, -3, 1000};
  // nomes de lugar longos
  strlcpy(feat.place, "Aeroporto Internacional de Cidade Exemplo Nome Bem Comprido", sizeof(feat.place));
  strlcpy(feat.wPlace, feat.place, sizeof(feat.wPlace));
  for(int ci = 0; ci < (int)(sizeof(CODES) / sizeof(CODES[0])); ci++)
  {
    for(int k = 0; k < 6; k++)
    {
      simNow = base + ci * 4111 + k * 14423;   // horas variadas, dia e noite
      simMillis += rnd(50, 50000);
      const float temp = rnd(-40, 55);
      overrideData["open-meteo"] = weatherJson(CODES[ci], temp, rnd(0, 1) ? 24 : rnd(0, 5), rnd(0, 9) == 0);
      featGen = featGen + 1;
      weatherWork();
      drawAll(count);
    }
  }
  // sem dados do clima / falha
  overrideData.erase("open-meteo"); overrideFail["open-meteo"] = -1;
  featGen = featGen + 1; weatherWork(); drawAll(count);
  overrideFail.clear();

  // Voos
  for(int n : {0, 1, 3, 40, 80, 200})
  {
    overrideData["adsb"] = flightsJson(n);
    for(int style = 0; style < 3; style++)
      for(int ic = 0; ic < 2; ic++)
        for(int cb = 0; cb < 2; cb++)
        {
          feat.fStyle = style; feat.fIcon = ic; feat.fColorBy = cb; feat.fTypes = rnd(0, 127);
          simScreen = SCR_FLIGHTS; flightsOpen(); flightsWork(); flightsDraw(); count++;
        }
    flightsLongPress();
    for(int k = 0; k < 6; k++) { flightsWork(); flightsDraw(); flightsShortPress(); count++; }
    flightsClose();
  }
  overrideFail["adsb"] = -5; flightsOpen(); flightsWork(); flightsDraw(); count++; overrideFail.clear();
  // as duas fontes: listas diferentes em cada uma, uma falhando, trocando de tamanho
  feat.fSource = 2;
  for(int round = 0; round < 30; round++)
  {
    overrideData.clear();
    overrideData["adsb.fi"] = flightsJson(rnd(0, 120));
    overrideData["adsb.lol"] = flightsJson(rnd(0, 120));
    if(round % 7 == 3) overrideFail["adsb.lol"] = -106;
    if(round % 11 == 5) overrideFail["adsb.fi"] = -3;
    if(round % 9 == 0) featGen++;
    simScreen = SCR_FLIGHTS; flightsOpen(); flightsWork(); flightsDraw(); count++;
    char fs[96]; flightsSources(fs, sizeof(fs));
    overrideFail.clear();
  }
  overrideData.clear(); feat.fSource = 0;

  // Noticias
  for(int atom = 0; atom < 2; atom++)
    for(int n : {0, 1, 7, 45})
    {
      overrideData["news.google"] = newsXml(n, atom);
      featGen = featGen + 1; simScreen = SCR_NEWS; newsOpen(); newsWork();
      for(int k = 0; k < 6; k++)
      {
        for(int s = 0; s < 6; s++) for(int u = 0; u < 2; u++) { feat.nStyle = s; feat.nUrgent = u; simMillis += 333; newsDraw(); count++; }
        simMillis += 60000; newsTick();
        if(k == 3) { newsLongPress(); newsDraw(); newsShortPress(); }
      }
    }

  // DMR
  for(int n : {0, 1, 3, 12})
  {
    overrideData["lh.php"] = dmrHtml(n);
    overrideData["radioid"] = "{\"results\":[{\"fname\":\"Alexander Maximilian Ferdinand Montgomery Wellington\",\"city\":\"Newcastle upon Tyne under Lyme Extra Long Name\",\"state\":\"Greater Manchester Metropolitan Area\",\"country\":\"Brazil\",\"id\":3141592}]}";
    simScreen = SCR_DMR;
    for(int k = 0; k < 4; k++) { dmrOpen(); dmrWork(); simMillis += 5000; }
    for(int s = 0; s < 6; s++) { feat.dStyle = s; dmrDraw(); dmrLongPress(); dmrDraw(); dmrLongPress(); count += 2; }
  }

  // Horoscopo
  simScreen = SCR_HORO;
  for(int th = 0; th < 3; th++)
    for(int sg = 0; sg < 12; sg++)
      for(time_t t : {local(2026, 11, 13, 10, 0, 0), local(2028, 2, 29, 23, 59, 0), local(2026, 12, 31, 23, 59, 59), local(2027, 1, 1, 0, 0, 1)})
      {
        simNow = t; feat.hTheme = th; feat.hSigns = (1 << sg) | (rnd(0, 1) ? 0xFFF : 0); feat.hScores = rnd(0, 1);
        horoOpen(); horoDraw(); horoTick(); horoLongPress(); horoDraw(); count += 2;
      }
  feat.hSigns = 0; horoOpen(); horoDraw(); count++;

  // Porta-retrato (com e sem fotos)
  simScreen = SCR_PHOTOS;
  for(int st = 0; st < PCLK_COUNT; st++)
    for(int pop = 0; pop < 2; pop++)
    {
      feat.pStyle = st; feat.pWxPop = pop; feat.pFade = rnd(0, 1);
      photosOpen(); for(int k = 0; k < 5; k++) { photosDraw(); simMillis += 400; count++; }
    }
  printf("estresse ok: %d telas desenhadas\n", count);
}
