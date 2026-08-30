#include "websync.h"

#include <DNSServer.h>
#include "storage.h"
#include <WebServer.h>
#include <WiFi.h>

#include "cams.h"
#include "cloudsync.h"
#include "config.h"
#include "customers.h"
#include "gnss.h"
#include "sensors.h"
#include "trip.h"

namespace {

WebServer g_server(80);
DNSServer g_dns;

bool g_up = false;
uint32_t g_tripEndedMs = 0;
bool g_sawTrip = false;

// Uppladdningen skrivs forst till en tillfallig fil och flyttas nar den ar
// hel. En halv kamerafil som redan ligger pa sin riktiga plats ar varre an
// ingen alls.
const char *kUploadTmp = "/DRIVE/UPP.TMP";
File g_upload;
bool g_uploadOk = false;

// ------------------------------------------------------------------ sidan --
// Hela sidan bor i flashminnet och ar fri fran beroenden: inga typsnitt, inga
// bibliotek, ingenting som ska hamtas fran ett internet som inte finns har.
// Telefonen ar ju ansluten till bilen, inte till varlden.
const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="sv">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Hikaya</title>
<style>
:root{--bg:#f6f5f1;--panel:#ffffff;--line:#d9d7d0;--text:#181c24;--dim:#666d78;
--accent:#1a58d2;--green:#088852;--warn:#ac7400;--red:#cb2a2a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
font-family:-apple-system,system-ui,sans-serif;line-height:1.5;
padding:1rem 1rem 3rem}
main{max-width:34rem;margin:0 auto}
h1{font-size:1.5rem;margin:.25rem 0 0;letter-spacing:-.02em}
.sub{color:var(--dim);font-size:.85rem;margin:0 0 1rem}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;
padding:1rem;margin-bottom:1rem}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:.5rem}
.stat{background:#f0efe9;border-radius:10px;padding:.5rem .75rem}
.stat b{display:block;font-size:1.1rem}
.stat span{color:var(--dim);font-size:.75rem}
h2{font-size:.95rem;margin:0 0 .6rem;color:var(--dim);text-transform:uppercase;
letter-spacing:.05em}
.resa{display:flex;align-items:center;gap:.75rem;padding:.6rem 0;
border-bottom:1px solid var(--line)}
.resa:last-child{border-bottom:none}
.chip{flex:none;width:.65rem;height:2.4rem;border-radius:4px}
.chip.privat{background:var(--accent)}.chip.foretag{background:var(--green)}
.chip.diffust{background:var(--warn)}.chip.omarkt{background:var(--line)}
.resa .info{flex:1;min-width:0}
.resa .rad1{font-weight:600}
.resa .rad2{color:var(--dim);font-size:.8rem;white-space:nowrap;
overflow:hidden;text-overflow:ellipsis}
.knapp{background:var(--accent);color:#ffffff;border:none;border-radius:9px;
padding:.5rem .8rem;font-weight:600;font-size:.85rem}
.knapp.grå{background:#e6e4dd;color:var(--dim)}
.knapp:active{opacity:.7}
a.knapp{text-decoration:none;display:inline-block}
.uppl label{display:block;margin-bottom:.75rem}
.uppl span{display:block;color:var(--dim);font-size:.8rem;margin-bottom:.25rem}
input[type=file]{width:100%;color:var(--dim);font-size:.85rem}
.natrad{display:flex;gap:.5rem;margin-bottom:.5rem}
.natrad input{flex:1;min-width:0;padding:.4rem .5rem;border:1px solid var(--line);
border-radius:8px;font-size:.9rem}
label>input{width:100%;padding:.4rem .5rem;border:1px solid var(--line);
border-radius:8px;font-size:.9rem}
#status{color:var(--dim);font-size:.85rem;min-height:1.2rem}
.tom{color:var(--dim);text-align:center;padding:1rem 0}
</style>
</head>
<body>
<main>
<h1>Hikaya</h1>
<p class="sub" id="version">ansluten till bilen</p>

<div class="card"><div class="stats" id="stats"></div></div>

<div class="card">
<h2>Resor</h2>
<div id="resor"><p class="tom">hämtar …</p></div>
<p style="margin:.75rem 0 0">
<a class="knapp" href="/csv">Hämta RESOR.CSV</a>
</p>
</div>

<div class="card uppl">
<h2>Ladda upp till enheten</h2>
<label><span>Fartkameror (KAMEROR.BIN)</span>
<input type="file" id="fkam"></label>
<label><span>Hastighetsgränser (HASTIGHET.BIN)</span>
<input type="file" id="fhast"></label>
<label><span>Kundlista (KUNDER.CSV)</span>
<input type="file" id="fkund"></label>
<p id="status"></p>
</div>

<div class="card uppl">
<h2>Molnsynk</h2>
<p style="color:var(--dim);font-size:.85rem;margin-top:0">
Spara upp till fyra nät – företagets, hemmets, telefonens hotspot.
Enheten tar det som finns där bilen står och synkar själv när ingen
resa pågår. Lösenord och token visas aldrig här – lämna fälten tomma
så behålls det som redan är sparat.</p>
<p style="color:var(--dim);font-size:.85rem">
Tänk på: näten måste sända på 2,4&nbsp;GHz (slå på
<b>Maximera kompatibilitet</b> för iPhone-hotspot). Och är hotspoten
samma telefon som du surfar härifrån just nu kan den inte dela ut nät
samtidigt som den är ansluten hit – koppla ner från Hikaya-wifit
och slå på hotspoten, så synkar enheten själv.</p>
<div id="mnat"></div>
<label><span>Enhetens token (från webbappens inställningar)</span>
<input type="password" id="mtoken" autocapitalize="off"></label>
<p>
<button class="knapp" id="mspara">Spara</button>
<button class="knapp grå" id="msynka">Synka nu</button>
</p>
<p id="mstatus" style="color:var(--dim);font-size:.85rem"></p>
</div>
</main>
<script>
const el=id=>document.getElementById(id);
function stat(b,s){return `<div class="stat"><b>${b}</b><span>${s}</span></div>`}

fetch('/api/status').then(r=>r.json()).then(d=>{
  el('version').textContent='version '+d.version+' · '+d.klocka;
  el('stats').innerHTML=
    stat(d.kameror||'–','fartkameror')+
    stat(d.granser?'ja':'nej','hastighetsdata')+
    stat(d.ledigt_mb+' MB','ledigt på kortet')+
    stat(d.kunder,'kunder i listan');
}).catch(()=>{el('version').textContent='kunde inte nå enheten'});

function ladda(){
fetch('/api/resor').then(r=>r.text()).then(t=>{
  const rader=t.split('\n').filter(x=>x.trim()).map(x=>{try{return JSON.parse(x)}catch(e){return null}}).filter(Boolean);
  if(!rader.length){el('resor').innerHTML='<p class="tom">Inga resor på kortet ännu.</p>';return}
  rader.reverse();
  el('resor').innerHTML=rader.map(r=>{
    const km=(r.meter/1000).toFixed(1).replace('.',',');
    const start=(r.start||'').replace('T',' ').slice(0,16);
    const kund=r.kund?' · '+r.kund:'';
    return `<div class="resa">
      <div class="chip ${r.syfte}"></div>
      <div class="info">
        <div class="rad1">Resa ${r.resa} · ${km} km</div>
        <div class="rad2">${start} · ${r.syfte}${kund}</div>
      </div>
      <a class="knapp" href="/gpx?fil=${r.gpx}">GPX</a>
      <button class="knapp grå" onclick="arkivera('${r.gpx}',this)">Klar</button>
    </div>`}).join('');
})}
ladda();

function arkivera(fil,btn){
  btn.disabled=true;
  fetch('/api/arkivera?fil='+fil,{method:'POST'})
    .then(r=>{if(!r.ok)throw 0;btn.closest('.resa').style.opacity=.35;btn.textContent='Arkiverad'})
    .catch(()=>{btn.disabled=false;el('status').textContent='kunde inte arkivera '+fil});
}

function koppla(id,namn){
  el(id).addEventListener('change',ev=>{
    const f=ev.target.files[0];if(!f)return;
    el('status').textContent='laddar upp '+namn+' …';
    const fd=new FormData();fd.append('fil',f,namn);
    fetch('/upp?namn='+namn,{method:'POST',body:fd})
      .then(r=>r.text().then(t=>{if(!r.ok)throw t;
        el('status').textContent=t;ev.target.value='';}))
      .catch(t=>{el('status').textContent='fel: '+t});
  });
}
koppla('fkam','KAMEROR.BIN');
koppla('fhast','HASTIGHET.BIN');
koppla('fkund','KUNDER.CSV');

let nh='';
for(let i=0;i<4;i++){nh+=`<div class="natrad">
<input type="text" id="mssid${i}" placeholder="nätets namn" autocapitalize="off">
<input type="password" id="mlosen${i}" placeholder="lösenord"></div>`}
el('mnat').innerHTML=nh;

let mfyllt=false;
function molnlage(){
  fetch('/api/moln').then(r=>r.json()).then(d=>{
    (d.nat||[]).forEach((n,i)=>{
      if(!mfyllt&&n.ssid)el('mssid'+i).value=n.ssid;
      el('mlosen'+i).placeholder=n.harLosen?'••••  sparat – tomt behåller':'lösenord';
    });
    mfyllt=true;
    el('mtoken').placeholder=d.harToken?'••••  sparat – tomt behåller':'';
    el('mstatus').textContent=d.konfigurerad
      ? `${d.lage}${d.besked?' · '+d.besked:''} · upp: ${d.resor} resor, ${d.gpx} gpx · ned: ${d.filer} filer`
      : 'inte konfigurerad';
  }).catch(()=>{});
}
molnlage(); setInterval(molnlage, 5000);

el('mspara').addEventListener('click',()=>{
  const fd=new URLSearchParams();
  for(let i=0;i<4;i++){
    fd.set('ssid'+i,el('mssid'+i).value.trim());
    fd.set('losen'+i,el('mlosen'+i).value);
  }
  fd.set('token',el('mtoken').value.trim());
  fetch('/moln',{method:'POST',body:fd})
    .then(r=>r.text()).then(t=>{
      el('mstatus').textContent=t;
      for(let i=0;i<4;i++)el('mlosen'+i).value='';
      el('mtoken').value='';
      molnlage();
    });
});
el('msynka').addEventListener('click',()=>{
  fetch('/moln/synka',{method:'POST'})
    .then(r=>r.text()).then(t=>{el('mstatus').textContent=t;});
});
</script>
</body>
</html>
)HTML";

// -------------------------------------------------------------- hjalpare ---

// Bara resefiler med ratt namnform far lamna kortet den har vagen. En adress
// ar indata, och indata pekar dit avsandaren vill - inte dit vi tanker.
bool validGpxName(const String &name) {
  if (name.length() < 6 || name.length() > 12) return false;
  if (!name.startsWith("R")) return false;
  if (!name.endsWith(".GPX")) return false;
  for (size_t i = 1; i < name.length() - 4; i++) {
    if (!isDigit(name[i])) return false;
  }
  return true;
}

void sendJsonStatus() {
  const uint64_t freeMb = sensors::freeBytes() / (1024ULL * 1024ULL);
  const GnssDebug d = gnss::debug();

  char clock[24] = "";
  sensors::localStamp(sensors::unixUtc(), clock, sizeof(clock));

  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"version\":\"%s\",\"klocka\":\"%s\",\"kameror\":%lu,"
           "\"granser\":%s,\"ledigt_mb\":%lu,\"kunder\":%u,"
           "\"gps\":{\"finns\":%s,\"satelliter\":%u}}",
           fwVersionFull(), clock, (unsigned long)cams::count(),
           cams::limitsLoaded() ? "true" : "false", (unsigned long)freeMb,
           (unsigned)customers::count(), d.present ? "true" : "false",
           (unsigned)d.sats);
  g_server.send(200, "application/json", buf);
}

void handleResor() {
  File f = SDCARD.open(TRIPS_JSONL, FILE_READ);
  if (!f) {
    g_server.send(200, "application/x-ndjson", "");
    return;
  }
  g_server.streamFile(f, "application/x-ndjson");
  f.close();
}

void handleGpx() {
  const String name = g_server.arg("fil");
  if (!validGpxName(name)) {
    g_server.send(400, "text/plain", "ogiltigt filnamn");
    return;
  }
  // Under en pagaende resa ar natet nere, sa filen som skrivs just nu kan
  // aldrig hamtas halvfardig har.
  String path = String(GPX_DIR) + "/" + name;
  File f = SDCARD.open(path, FILE_READ);
  if (!f) {
    // Redan arkiverad? Da finns den i UPPLADDAT och ska ga att hamta igen.
    path = String(GPX_SYNCED_DIR) + "/" + name;
    f = SDCARD.open(path, FILE_READ);
  }
  if (!f) {
    g_server.send(404, "text/plain", "filen finns inte");
    return;
  }
  g_server.sendHeader("Content-Disposition",
                      "attachment; filename=" + name);
  g_server.streamFile(f, "application/gpx+xml");
  f.close();
}

void handleCsv() {
  File f = SDCARD.open(TRIPS_CSV, FILE_READ);
  if (!f) {
    g_server.send(404, "text/plain", "ingen dagbok annu");
    return;
  }
  g_server.sendHeader("Content-Disposition", "attachment; filename=RESOR.CSV");
  // text/csv med teckenupplysning, sa att Excel inte gissar fel pa ao och a.
  g_server.streamFile(f, "text/csv; charset=utf-8");
  f.close();
}

void handleArkivera() {
  const String name = g_server.arg("fil");
  if (!validGpxName(name)) {
    g_server.send(400, "text/plain", "ogiltigt filnamn");
    return;
  }
  const String from = String(GPX_DIR) + "/" + name;
  const String to = String(GPX_SYNCED_DIR) + "/" + name;
  if (!SDCARD.exists(from)) {
    g_server.send(404, "text/plain", "filen finns inte");
    return;
  }
  // Flyttas, raderas inte. Kortet ar den enda kopian tills nagot annat
  // bevisats, och UPPLADDAT ar just det - en markering, inte en soptunna.
  if (SDCARD.exists(to)) SDCARD.remove(to);
  if (!SDCARD.rename(from, to)) {
    g_server.send(500, "text/plain", "kunde inte flytta");
    return;
  }
  g_server.send(200, "text/plain", "arkiverad");
}

// Uppladdningen. Namnet vitlistas: sidan far fylla pa enhetens datafiler, inte
// skriva var som helst pa kortet.
const char *uploadTarget(const String &name) {
  if (name == "KAMEROR.BIN") return CAMS_FILE;
  if (name == "HASTIGHET.BIN") return LIMITS_FILE;
  if (name == "KUNDER.CSV") return CUSTOMERS_FILE;
  return nullptr;
}

// De binara filerna borjar med sin magi. En fil utan den ar fel fil, och den
// ska avvisas har - inte upptackas av en bil som slutat varna.
bool uploadContentOk(const String &name) {
  if (name == "KUNDER.CSV") return true;
  File f = SDCARD.open(kUploadTmp, FILE_READ);
  if (!f) return false;
  uint8_t magic[4] = {0};
  const bool got = f.read(magic, 4) == 4;
  f.close();
  if (!got) return false;
  if (name == "KAMEROR.BIN") return memcmp(magic, "DLC1", 4) == 0;
  if (name == "HASTIGHET.BIN") return memcmp(magic, "DLH1", 4) == 0;
  return false;
}

void handleUploadData() {
  HTTPUpload &up = g_server.upload();

  if (up.status == UPLOAD_FILE_START) {
    g_uploadOk = false;
    if (!SDCARD.exists(DRIVE_DIR)) SDCARD.mkdir(DRIVE_DIR);
    if (SDCARD.exists(kUploadTmp)) SDCARD.remove(kUploadTmp);
    g_upload = SDCARD.open(kUploadTmp, FILE_WRITE);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_upload) g_upload.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_upload) {
      g_upload.flush();
      g_uploadOk = (g_upload.size() == up.totalSize) && up.totalSize > 0;
      g_upload.close();
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (g_upload) g_upload.close();
    SDCARD.remove(kUploadTmp);
  }
}

void handleUploadDone() {
  const String name = g_server.arg("namn");
  const char *target = uploadTarget(name);

  if (!target) {
    SDCARD.remove(kUploadTmp);
    g_server.send(400, "text/plain", "okant filnamn");
    return;
  }
  if (!g_uploadOk) {
    SDCARD.remove(kUploadTmp);
    g_server.send(500, "text/plain", "uppladdningen kom inte fram hel");
    return;
  }
  if (!uploadContentOk(name)) {
    SDCARD.remove(kUploadTmp);
    g_server.send(400, "text/plain",
                  "filen ser inte ut som en " + name);
    return;
  }

  // Kamerafilerna kan vara oppna i avlasningstraden. Den far slappa dem innan
  // filen byts, och lasa om efterat - allt via cams egna handslag, sa att
  // traden aldrig soker i en lista som haller pa att bytas ut.
  const bool camsFile = (name == "KAMEROR.BIN" || name == "HASTIGHET.BIN");
  if (camsFile) cams::beginUpdate();

  if (SDCARD.exists(target)) SDCARD.remove(target);
  const bool ok = SDCARD.rename(kUploadTmp, target);

  if (camsFile) cams::endUpdate();
  if (name == "KUNDER.CSV") customers::reload();

  if (!ok) {
    g_server.send(500, "text/plain", "kunde inte skriva filen");
    return;
  }

  char msg[80];
  if (name == "KAMEROR.BIN") {
    // Antalet ar kvittot. "2771 kameror inlasta" sager att allt fungerade;
    // "klart" sager ingenting.
    snprintf(msg, sizeof(msg), "%lu kameror inlästa",
             (unsigned long)cams::count());
  } else if (name == "HASTIGHET.BIN") {
    snprintf(msg, sizeof(msg), "hastighetsgränserna är på plats");
  } else {
    snprintf(msg, sizeof(msg), "%u kunder inlästa",
             (unsigned)customers::count());
  }
  g_server.send(200, "text/plain; charset=utf-8", msg);
}

// ---- molnsynken: uppgifterna skrivs in har en gang och bor sedan i enhetens
// flashminne. Token star i webbappens installningar.

// Ett natnamn kan innehalla vad som helst - aven citattecken. In i json gar
// det bara i skyddad form.
String jsonEsc(const String &v) {
  String out;
  out.reserve(v.length() + 4);
  for (size_t i = 0; i < v.length(); i++) {
    const char c = v[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

void handleMolnStatus() {
  const CloudStatus c = cloudsync::status();
  const char *stateName[] = {"av", "vilar", "ansluter", "synkar", "klar", "fel"};
  String out = "{\"konfigurerad\":";
  out += cloudsync::configured() ? "true" : "false";
  out += ",\"ssid\":\"" + jsonEsc(cloudsync::ssid()) + "\"";
  out += ",\"lage\":\"";
  out += stateName[c.state <= CLOUD_ERROR ? c.state : 0];
  out += "\",\"besked\":\"" + jsonEsc(String(c.detail)) + "\"";
  out += ",\"resor\":" + String((unsigned long)c.tripsUploaded);
  out += ",\"gpx\":" + String((unsigned long)c.gpxUploaded);
  out += ",\"filer\":" + String((unsigned long)c.filesDownloaded);
  out += ",\"harToken\":";
  out += cloudsync::hasToken() ? "true" : "false";
  out += ",\"nat\":[";
  for (uint8_t i = 0; i < cloudsync::kNetMax; i++) {
    if (i) out += ",";
    out += "{\"ssid\":\"" + jsonEsc(cloudsync::netSsid(i)) + "\",\"harLosen\":";
    out += cloudsync::netHasPassword(i) ? "true" : "false";
    out += "}";
  }
  out += "]}";
  g_server.send(200, "application/json", out);
}

void handleMolnSave() {
  String s[cloudsync::kNetMax], p[cloudsync::kNetMax];
  const char *ssids[cloudsync::kNetMax];
  const char *passes[cloudsync::kNetMax];
  char key[10];
  for (uint8_t i = 0; i < cloudsync::kNetMax; i++) {
    snprintf(key, sizeof(key), "ssid%u", i);
    s[i] = g_server.arg(key);
    s[i].trim();
    snprintf(key, sizeof(key), "losen%u", i);
    p[i] = g_server.arg(key);
    ssids[i] = s[i].c_str();
    passes[i] = p[i].c_str();
  }
  cloudsync::configureNets(ssids, passes, g_server.arg("token").c_str());

  if (!cloudsync::configured()) {
    g_server.send(200, "text/plain; charset=utf-8", "molnsynken avstängd");
    return;
  }
  // Kvittot sager vad som faktiskt lagrades - att "sparat" i sjalva verket
  // betydde "raderat" ar precis det missforstand som ska bort.
  uint8_t n = 0;
  for (uint8_t i = 0; i < cloudsync::kNetMax; i++) {
    if (cloudsync::netSsid(i).length()) n++;
  }
  String msg = "sparat – " + String(n) + " nät";
  if (cloudsync::hasToken()) msg += " · synkar så fort ett nät nås";
  else msg += " · men token saknas fortfarande";
  g_server.send(200, "text/plain; charset=utf-8", msg);
}

// Allt som inte kanns igen skickas till startsidan. Det ar det som gor
// fangstportalen: telefonen provar en kand adress, far en omdirigering i
// stallet for det vantade svaret, och drar slutsatsen att har finns en sida
// att visa. Precis som pa hotellet.
void handleNotFound() {
  g_server.sendHeader("Location",
                      "http://" + WiFi.softAPIP().toString() + "/");
  g_server.send(302, "text/plain", "");
}

void startAp() {
  // enableAP i stallet for mode(WIFI_AP): stationssidan ags av molnsynken och
  // far inte slackas har. Radion klarar bada rollerna samtidigt.
  WiFi.enableAP(true);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);

  // Alla dns-fragor besvaras med var egen adress. Telefonen hittar alltsa
  // "hit" vilket namn den an slar upp - det ar sa portalen fangar den.
  g_dns.start(53, "*", WiFi.softAPIP());

  g_server.on("/", HTTP_GET, []() {
    g_server.send_P(200, "text/html; charset=utf-8", kIndexHtml);
  });
  g_server.on("/api/status", HTTP_GET, sendJsonStatus);
  g_server.on("/api/resor", HTTP_GET, handleResor);
  g_server.on("/gpx", HTTP_GET, handleGpx);
  g_server.on("/csv", HTTP_GET, handleCsv);
  g_server.on("/api/arkivera", HTTP_POST, handleArkivera);
  g_server.on("/upp", HTTP_POST, handleUploadDone, handleUploadData);
  g_server.on("/api/moln", HTTP_GET, handleMolnStatus);
  g_server.on("/moln", HTTP_POST, handleMolnSave);
  g_server.on("/moln/synka", HTTP_POST, []() {
    cloudsync::requestSync();
    g_server.send(200, "text/plain; charset=utf-8", "synk begärd");
  });
  g_server.onNotFound(handleNotFound);
  g_server.begin();

  g_up = true;
}

volatile bool g_suspend = false;

void stopAp() {
  g_server.stop();
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  // Bara accesspunkten slacks. Stationssidan ags av molnsynken, som slacker
  // sig sjalv nar resan borjar.
  WiFi.enableAP(false);
  g_up = false;
}

}  // namespace

namespace websync {

void suspend(bool on) { g_suspend = on; }

void begin() {
  // Natet startas fran tick() nar tillstandet ar kant. Har finns inget att
  // gora - men funktionen finns, sa att uppstartsordningen syns i .ino-filen.
}

void tick() {
  const bool tripActive = trip::status().active;

  // Molnsynken har foretrade framfor konfigsidan: tls behover det
  // internminne som accesspunkten annars haller.
  if (g_suspend) {
    if (g_up) stopAp();
    return;
  }

  if (tripActive) {
    g_sawTrip = true;
    g_tripEndedMs = 0;
    // Under fard ar natet nere. Det drar strom, och en webbsida ar inget man
    // ska titta pa nar man kor.
    if (g_up) stopAp();
    return;
  }

  if (!g_up) {
    // Efter en resa vantar natet nagra sekunder, sa att en snabb stopp-och-
    // korning inte hinner dra igang wifi i onodan. Vid start finns ingen
    // fordrojning - da har bilen statt still lange nog anda.
    if (g_sawTrip) {
      if (g_tripEndedMs == 0) g_tripEndedMs = millis();
      if (millis() - g_tripEndedMs < WIFI_START_DELAY_S * 1000UL) return;
    }
    startAp();
    return;
  }

  g_dns.processNextRequest();
  g_server.handleClient();
}

bool isUp() { return g_up; }

const char *ssid() { return WIFI_AP_SSID; }

String ipString() { return g_up ? WiFi.softAPIP().toString() : String(""); }

uint8_t clientCount() { return g_up ? WiFi.softAPgetStationNum() : 0; }

}  // namespace websync
