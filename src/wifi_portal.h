#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "config.h"
#include "settings.h"
#include "display_ui.h"
#include "power.h"

// ---- Phone hotspot -------------------------------------------------------------------
// With "Phone hotspot" ON (Settings page, WIFI) the CYD runs its own WiFi network,
// WIFI_AP_SSID / WIFI_AP_PASSWORD. Join it and open http://192.168.4.1 for:
//   Live    gauges (same units as the screen, alarms in red), stored and pending codes
//           with their descriptions; refreshes every second
//   Logs    the SD card's drive logs (and, in the CAN version, CAN recordings): download
//           or delete
//   Update  upload a new firmware .bin (from .pio/build/<env>/firmware.bin)
// It only shows what the display already reads: no extra requests go to the truck.
// Every page or data request counts as activity, so the display stays awake while in use.

// Both versions have the drive log; the CAN version also has the CAN frame recorder
// (can_logger.h, which defines CAN_LOGGER_H)

static WebServer webServer(80);
static bool wifiRunning = false;

static const char PORTAL_STYLE[] PROGMEM = R"CSS(
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#101214;--card:#1b1f23;--line:#2c3238;--text:#e8eaed;--dim:#9aa0a6;--red:#e5484d;--yellow:#f5c400;--green:#3fb950;--cyan:#39c5cf}
body{margin:0;background:var(--bg);color:var(--text);font:16px system-ui,sans-serif}
header{display:flex;gap:16px;align-items:center;padding:12px 16px;border-bottom:1px solid var(--line)}
header b{flex:1}header a{color:var(--cyan);text-decoration:none}
main{padding:16px;max-width:720px;margin:auto}
.status{padding:8px 12px;border-radius:8px;background:var(--card);margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.g{background:var(--card);border:2px solid var(--line);border-radius:10px;padding:10px}
.g .l{color:var(--dim);font-size:13px}.g .v{font-size:30px;font-weight:600}.g .u{color:var(--dim);font-size:14px}
.g.alarm{border-color:var(--red);background:#3a1416}
h2{font-size:18px;margin:22px 0 8px}
.code{background:var(--card);border-left:4px solid var(--red);border-radius:6px;padding:8px 12px;margin:6px 0}
.code.p{border-color:var(--yellow)}.code b{margin-right:10px}.dim{color:var(--dim)}
table{width:100%;border-collapse:collapse}td{padding:8px;border-bottom:1px solid var(--line)}
button{background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:6px;padding:6px 12px;font-size:15px}
a.btn{color:var(--cyan)}
</style>
)CSS";

static const char PORTAL_NAV[] PROGMEM = R"HTML(
<header><b>CYD-OBDII</b><a href="/">Live</a><a href="/logs">Logs</a><a href="/update">Update</a></header>
)HTML";

static const char PORTAL_LIVE[] PROGMEM = R"HTML(
<main>
<div class="status" id="st">Connecting...</div>
<div class="grid" id="gauges"></div>
<h2>Trouble codes</h2><div id="codes" class="dim">Loading...</div>
</main>
<script>
function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
function code(c,p){return '<div class="code'+(p?' p':'')+'"><b>'+esc(c.c)+'</b>'+(p?'<span class="dim">pending</span> ':'')+
  esc(c.d||'No description stored')+'</div>'}
async function tick(){
  try{
    const d=await (await fetch('/api/live')).json();
    document.getElementById('st').textContent=d.status;
    document.getElementById('gauges').innerHTML=d.gauges.map(g=>
      '<div class="g'+(g.a?' alarm':'')+'"><div class="l">'+esc(g.l)+'</div><span class="v">'+esc(g.v)+
      '</span> <span class="u">'+esc(g.u)+'</span></div>').join('');
    let h=d.stored.map(c=>code(c,false)).join('')+d.pending.map(c=>code(c,true)).join('');
    if(d.storedTotal<0)h='Not read yet';else if(!h)h='No trouble codes';
    if(d.abs>0)h+='<div class="code"><b>ABS</b>'+d.abs+' ABS code(s) stored</div>';
    document.getElementById('codes').innerHTML=h;
  }catch(e){document.getElementById('st').textContent='Lost connection to the CYD...'}
  setTimeout(tick,1000);
}
tick();
</script>
)HTML";

static const char PORTAL_UPDATE[] PROGMEM = R"HTML(
<main>
<h2>Firmware update</h2>
<p class="dim">Build in PlatformIO, then choose <b>.pio/build/esp32dev/firmware.bin</b> (or the sniffer /
selftest build). The CYD restarts when the upload is done. Keep the phone close and don't power off.</p>
<input type="file" id="f" accept=".bin"> <button onclick="up()">Upload</button>
<p id="m"></p>
<script>
function up(){
  const f=document.getElementById('f').files[0],m=document.getElementById('m');
  if(!f){m.textContent='Choose a firmware .bin first';return}
  const x=new XMLHttpRequest(),fd=new FormData();fd.append('firmware',f);
  x.upload.onprogress=e=>m.textContent='Uploading '+Math.round(e.loaded*100/e.total)+'%';
  x.onload=()=>m.textContent=x.responseText;x.onerror=()=>m.textContent='Upload failed';
  x.open('POST','/update');x.send(fd);
}
</script>
</main>
)HTML";

inline String jsonEscape(const String& s) {
    String out;
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c >= 32) out += c;
    }
    return out;
}

inline void sendPage(const char* body) {
    noteActivity();
    String html = "<!doctype html><html><head><title>CYD-OBDII</title>";
    html += FPSTR(PORTAL_STYLE);
    html += "</head><body>";
    html += FPSTR(PORTAL_NAV);
    html += FPSTR(body);
    html += "</body></html>";
    webServer.send(200, "text/html", html);
}

// Same status idea as the screen's status bar, in words
inline String portalStatus() {
#ifdef CAN_LOGGER_H
    if (canListenOnly) return "Sniffer page (listen-only): gauges are not being read";
#endif
    if (!isConnected) return "Truck not answering: showing DEMO values";
    int alarm = firstAlarmMetric();
    if (alarm >= 0) return "ALARM: " + String(metrics[alarm].label) + " " + shownValue(alarm) + " " + shownUnit(alarm);
    return "Live data from the truck";
}

inline void handleLive() {
    noteActivity();
    String j = "{\"status\":\"" + jsonEscape(portalStatus()) + "\",\"gauges\":[";
    for (int i = 0; i < METRIC_COUNT; i++) {
        if (i) j += ',';
        j += "{\"l\":\"" + jsonEscape(metrics[i].label) + "\",\"v\":\"" + jsonEscape(shownValue(i)) +
             "\",\"u\":\"" + jsonEscape(shownUnit(i)) + "\",\"a\":" + String(metricAlarmState(i) != 0 ? 1 : 0) + "}";
    }
    j += "],\"storedTotal\":" + String(dtcTotal) + ",\"stored\":[";
    for (int i = 0; i < min(max(dtcTotal, 0), MAX_DTCS); i++) {
        if (i) j += ',';
        j += "{\"c\":\"" + dtcCodes[i] + "\",\"d\":\"" + jsonEscape(describeDTC(dtcCodes[i])) + "\"}";
    }
    j += "],\"pending\":[";
    for (int i = 0; i < min(max(pendingTotal, 0), MAX_DTCS); i++) {
        if (i) j += ',';
        j += "{\"c\":\"" + pendingCodes[i] + "\",\"d\":\"" + jsonEscape(describeDTC(pendingCodes[i])) + "\"}";
    }
    j += "],\"abs\":" + String(absDtcTotal) + "}";
    webServer.send(200, "application/json", j);
}

// Only names the loggers write are accepted (DRIVE001.CSV, CAN0001.LOG / .TXT), so a
// request can't reach any other file
inline bool validLogName(const String& n) {
    if (n.length() == 12 && n.startsWith("DRIVE") && n.endsWith(".CSV")) {
        for (int i = 5; i < 8; i++) if (!isDigit(n[i])) return false;
        return true;
    }
    if (n.length() != 11 || !n.startsWith("CAN")) return false;
    for (int i = 3; i < 7; i++) if (!isDigit(n[i])) return false;
    return n.endsWith(".LOG") || n.endsWith(".TXT");
}

// The file a logger is writing right now: listed, but not downloadable until STOP
inline bool isRecordingFile(const String& n) {
#ifdef CAN_LOGGER_H
    if (logRecording && n.startsWith(logName)) return true;
#endif
    return driveLogging && n == driveLogName;
}

inline void handleLogs() {
    noteActivity();
    String body = "<main><h2>SD card recordings</h2>";
    if (!sdBegin()) {
        body += "<p class='dim'>No SD card found.</p></main>";
    } else {
        body += "<p class='dim'>.CSV = drive logs (gauges once a second, for a spreadsheet)"
#ifdef CAN_LOGGER_H
                ", .LOG = all CAN frames (candump format, for SavvyCAN or tools/can_compare.py),"
                " .TXT = START and MARK times"
#endif
                ".</p><table>";
        File dir = SD.open("/");
        int n = 0;
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            String name = String(f.name());
            if (name.startsWith("/")) name = name.substring(1);
            if (f.isDirectory() || !validLogName(name)) continue;
            n++;
            body += "<tr><td>" + name + "</td><td class='dim'>" + String((uint32_t)(f.size() / 1024)) + " KB</td><td>";
            if (isRecordingFile(name)) {
                body += "<span class='dim'>recording...</span>";
            } else {
                body += "<a class='btn' href='/download?f=" + name + "'>Download</a> "
                        "<button onclick=\"if(confirm('Delete " + name + "?'))fetch('/delete?f=" + name +
                        "',{method:'POST'}).then(()=>location.reload())\">Delete</button>";
            }
            body += "</td></tr>";
        }
        dir.close();
        body += "</table>";
        if (!n) body += "<p class='dim'>No recordings yet. Use REC on the CYD's LOG page"
#ifdef CAN_LOGGER_H
                        " (or the sniffer page for CAN frames)"
#endif
                        ".</p>";
        body += "<p class='dim'>" + String((uint32_t)((SD.totalBytes() - SD.usedBytes()) / (1024 * 1024))) +
                " MB free</p></main>";
    }
    sendPage(body.c_str());
}

inline void handleDownload() {
    noteActivity();
    String name = webServer.arg("f");
    if (!validLogName(name) || isRecordingFile(name) || !sdBegin()) { webServer.send(404, "text/plain", "Not found"); return; }
    File f = SD.open("/" + name, FILE_READ);
    if (!f) { webServer.send(404, "text/plain", "Not found"); return; }
    webServer.sendHeader("Content-Disposition", "attachment; filename=" + name);
    webServer.streamFile(f, "text/plain");
    f.close();
    logDebug("WIFI", "Downloaded " + name);
}

inline void handleDelete() {
    noteActivity();
    String name = webServer.arg("f");
    if (!validLogName(name) || isRecordingFile(name) || !sdBegin() || !SD.remove("/" + name)) {
        webServer.send(400, "text/plain", "Could not delete");
        return;
    }
    logDebug("WIFI", "Deleted " + name);
    webServer.send(200, "text/plain", "Deleted");
}

// Firmware upload: the .bin is written to the spare app partition as it arrives, and
// the CYD restarts into it once it has been checked
inline void handleUpdateUpload() {
    HTTPUpload& up = webServer.upload();
    noteActivity();
    if (up.status == UPLOAD_FILE_START) {
        logDebug("WIFI", "Firmware upload started: " + up.filename);
#ifdef CAN_LOGGER_H
        logStop();   // close any SD recording first
#endif
        driveLogStop();
        tft.fillRect(0, 0, 320, STATUS_BAR_H, TFT_BLUE);
        tft.setTextColor(TFT_WHITE, TFT_BLUE);
        tft.drawCentreString("UPDATING FIRMWARE - DON'T POWER OFF", 160, 4, 2);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) logDebug("WIFI", "Update.begin failed: " + String(Update.errorString()));
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            logDebug("WIFI", "Update.write failed: " + String(Update.errorString()));
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) logDebug("WIFI", "Firmware upload done: " + String(up.totalSize) + " bytes");
        else                  logDebug("WIFI", "Update.end failed: " + String(Update.errorString()));
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        logDebug("WIFI", "Firmware upload aborted");
    }
}

inline void handleUpdateDone() {
    bool ok = !Update.hasError() && Update.isFinished();
    webServer.send(200, "text/plain", ok ? "Done. The CYD is restarting with the new firmware."
                                         : "Update failed: " + String(Update.errorString()));
    if (ok) {
        if (settingsDirty) settingsSave();
        delay(500);
        ESP.restart();
    }
    updateStatusBar(true);
}

inline void wifiStart() {
    logDebug("WIFI", "Starting hotspot (free heap " + String(ESP.getFreeHeap() / 1024) + " KB)");
    WiFi.persistent(false);   // don't keep WiFi settings in flash (saves memory and flash writes)
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD)) {
        // Not retried every pass: switch the hotspot off and on again to retry
        logDebug("WIFI", "Could not start the hotspot (free heap " + String(ESP.getFreeHeap() / 1024) +
                         " KB). The Settings page shows FAILED; switch it off and on to retry.");
        WiFi.mode(WIFI_OFF);
        wifiStartFailed = true;
        return;
    }
    static bool routesAdded = false;   // the hotspot can be switched off and on again
    if (!routesAdded) {
        routesAdded = true;
        webServer.on("/", HTTP_GET, [] { sendPage(PORTAL_LIVE); });
        webServer.on("/api/live", HTTP_GET, handleLive);
        webServer.on("/logs", HTTP_GET, handleLogs);
        webServer.on("/download", HTTP_GET, handleDownload);
        webServer.on("/delete", HTTP_POST, handleDelete);
        webServer.on("/update", HTTP_GET, [] { sendPage(PORTAL_UPDATE); });
        webServer.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
        webServer.onNotFound([] { webServer.send(404, "text/plain", "Not found"); });
    }
    webServer.begin();
    wifiRunning = true;
    logDebug("WIFI", "Hotspot on: join '" + String(WIFI_AP_SSID) + "', then open http://" + WiFi.softAPIP().toString() +
                     " (free heap " + String(ESP.getFreeHeap() / 1024) + " KB)");
}

inline void wifiStop() {
    webServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiRunning = false;
    logDebug("WIFI", "Hotspot off");
}

// Call on every loop() pass: follows the Settings page switch and answers requests
inline void wifiTick() {
    if (!settings.wifiOn) wifiStartFailed = false;   // switching it off clears a failure
    if (settings.wifiOn && !wifiRunning && !wifiStartFailed) wifiStart();
    else if (!settings.wifiOn && wifiRunning) wifiStop();
    if (wifiRunning) webServer.handleClient();
}

#endif
