// ============================================================
//  web_server_modules.cpp  –  API routes for Tasks 7-11
//  NFC, RFID, Sub-GHz, NRF24, System modules
// ============================================================
#include "web_server.h"
#include "nfc_module.h"
#include "rfid_module.h"
#include "subghz_module.h"
#include "nrf24_module.h"
#include "system_module.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

// Re-use the same helpers from web_server.cpp (declared extern)
extern void sendJsonExt(AsyncWebServerRequest* req, int code, const String& json);

// Local helper matching the pattern in web_server.cpp
static void _sendJson(AsyncWebServerRequest* req, int code, const String& json) {
    AsyncWebServerResponse* r = req->beginResponse(code, "application/json", json);
    r->addHeader("Access-Control-Allow-Origin", "*");
    req->send(r);
}

static String* _getBodyBuf(AsyncWebServerRequest* req) {
    if (!req->_tempObject) {
        req->_tempObject = new String();
        req->onDisconnect([req]() {
            if (req->_tempObject) {
                delete reinterpret_cast<String*>(req->_tempObject);
                req->_tempObject = nullptr;
            }
        });
    }
    return reinterpret_cast<String*>(req->_tempObject);
}
static void _freeBodyBuf(AsyncWebServerRequest* req) {
    if (req->_tempObject) {
        delete reinterpret_cast<String*>(req->_tempObject);
        req->_tempObject = nullptr;
    }
}

#define POST_BODY_MOD(path, handler) \
    _server.on(path, HTTP_POST, \
        [](AsyncWebServerRequest* req){}, \
        nullptr, \
        [this](AsyncWebServerRequest* req, uint8_t* d, size_t l, size_t i, size_t t) { \
            if (_getBodyBuf(req)->length() + l > HTTP_MAX_BODY) { \
                _freeBodyBuf(req); \
                _sendJson(req, 413, "{\"error\":\"Request too large\"}"); return; \
            } \
            _getBodyBuf(req)->concat((char*)d, l); \
            bool lastChunk = (t > 0) ? (i + l >= t) : (i == 0); \
            if (lastChunk) { \
                String* buf = _getBodyBuf(req); \
                handler(req, (uint8_t*)buf->c_str(), buf->length()); \
                _freeBodyBuf(req); \
            } \
        })

// ============================================================
//  WebUI::setupModuleRoutes()  — called from WebUI::begin()
// ============================================================
void WebUI::setupModuleRoutes() {

    // ── NFC ─────────────────────────────────────────────────
    // GET /api/nfc/tags
    _server.on("/api/nfc/tags", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, nfcModule.tagsToJson());
    });

    // GET /api/nfc/read  — poll for result
    _server.on("/api/nfc/read", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (nfcModule.isReading()) {
            _sendJson(req, 200, "{\"ok\":true,\"reading\":true}");
            return;
        }
        if (nfcModule.tagAvailable()) {
            NfcTag t = nfcModule.lastTag();
            nfcModule.clearTag();
            String blocks = "[";
            for (size_t i = 0; i < t.blocks.size(); i++) {
                if (i) blocks += ",";
                blocks += "\"" + t.blocks[i] + "\"";
            }
            blocks += "]";
            String out = "{\"ok\":true,\"uid\":\"" + t.uid +
                         "\",\"type\":\"" + t.type +
                         "\",\"atqa\":\"" + t.atqa +
                         "\",\"sak\":\"" + t.sak +
                         "\",\"blocks\":" + blocks + "}";
            _sendJson(req, 200, out);
        } else {
            // Start a new read if nothing pending
            nfcModule.startRead();
            _sendJson(req, 200, "{\"ok\":true,\"reading\":true}");
        }
    });

    // POST /api/nfc/save
    POST_BODY_MOD("/api/nfc/save", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l)) {
            _sendJson(req, 400, "{\"error\":\"JSON parse failed\"}"); return;
        }
        NfcTag tag;
        tag.name = doc["name"] | "Tag";
        tag.uid  = doc["uid"]  | "";
        tag.type = doc["type"] | "Unknown";
        tag.atqa = doc["atqa"] | "";
        tag.sak  = doc["sak"]  | "";
        for (const char* b : doc["blocks"].as<JsonArray>())
            tag.blocks.push_back(String(b));
        uint32_t id = nfcModule.saveTag(tag);
        _sendJson(req, 200, id ? "{\"ok\":true,\"id\":" + String(id) + "}" :
                                  "{\"error\":\"Tag storage full\"}");
    });

    // POST /api/nfc/delete  body: {id:N}
    POST_BODY_MOD("/api/nfc/delete", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l)) { _sendJson(req, 400, "{\"error\":\"JSON\"}\n"); return; }
        uint32_t id = doc["id"] | 0u;
        bool ok = nfcModule.deleteTag(id);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Not found\"}");
    });

    // POST /api/nfc/emulate  body: {id:N}
    POST_BODY_MOD("/api/nfc/emulate", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        deserializeJson(doc, d, l);
        uint32_t id = doc["id"] | 0u;
        bool ok = nfcModule.startEmulate(id);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Tag not found\"}");
    });

    // POST /api/nfc/emulate/stop
    _server.on("/api/nfc/emulate/stop", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            nfcModule.stopEmulate();
            _sendJson(req, 200, "{\"ok\":true}");
        });

    // POST /api/nfc/clone  body: tag JSON
    POST_BODY_MOD("/api/nfc/clone", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        bool ok = nfcModule.writeClone();
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"No source\"}");
    });

    // POST /api/nfc/dict/start
    _server.on("/api/nfc/dict/start", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            nfcModule.startDictAttack();
            _sendJson(req, 200, "{\"ok\":true}");
        });

    // POST /api/nfc/dict/stop
    _server.on("/api/nfc/dict/stop", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            nfcModule.stopDictAttack();
            _sendJson(req, 200, "{\"ok\":true}");
        });

    // GET /api/nfc/dict/poll
    _server.on("/api/nfc/dict/poll", HTTP_GET, [](AsyncWebServerRequest* req) {
        String r = nfcModule.pollDictResult();
        bool done = r.endsWith("DONE");
        if (done) r = r.substring(0, r.length() - 4);
        _sendJson(req, 200, "{\"ok\":true,\"data\":\"" + r + "\",\"done\":" +
                  (done ? "true" : "false") + ",\"running\":" +
                  (nfcModule.isDictRunning() ? "true" : "false") + "}");
    });

    // GET /api/nfc/export?id=N  — download .nfc content
    _server.on("/api/nfc/export", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("id")) { _sendJson(req, 400, "{\"error\":\"Missing id\"}"); return; }
        uint32_t id = req->getParam("id")->value().toInt();
        String content = nfcModule.exportFlipperNfc(id);
        if (content.isEmpty()) { _sendJson(req, 404, "{\"error\":\"Not found\"}"); return; }
        AsyncWebServerResponse* r = req->beginResponse(200, "text/plain", content);
        r->addHeader("Content-Disposition", "attachment; filename=\"tag.nfc\"");
        req->send(r);
    });

    // POST /api/nfc/import  body: {name:"x", content:"..."}
    POST_BODY_MOD("/api/nfc/import", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l)) { _sendJson(req, 400, "{\"error\":\"JSON\"}"); return; }
        NfcTag tag;
        tag.name = doc["name"] | "Imported";
        String content = doc["content"] | "";
        if (!nfcModule.importFlipperNfc(content, tag)) {
            _sendJson(req, 400, "{\"error\":\"Parse failed\"}"); return;
        }
        tag.name = doc["name"] | "Imported";
        uint32_t id = nfcModule.saveTag(tag);
        _sendJson(req, 200, id ? "{\"ok\":true,\"id\":" + String(id) + "}" :
                                  "{\"error\":\"Storage full\"}");
    });

    // GET /api/nfc/parse?id=N
    _server.on("/api/nfc/parse", HTTP_GET, [](AsyncWebServerRequest* req) {
        NfcTag t = nfcModule.lastTag();
        String result = nfcModule.parseTag(t);
        _sendJson(req, 200, "{\"ok\":true,\"result\":\"" + result + "\"}");
    });

    // POST /api/chameleon/connect
    _server.on("/api/chameleon/connect", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Real HW: probe USB-serial for Chameleon Ultra/Mini
        _sendJson(req, 200, "{\"ok\":false,\"note\":\"Chameleon not connected\"}");
    });

    // POST /api/chameleon/upload  body: {slot:N, tag:{...}}
    POST_BODY_MOD("/api/chameleon/upload", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        _sendJson(req, 200, "{\"ok\":false,\"error\":\"Chameleon not connected\"}");
    });

    // ── RFID ────────────────────────────────────────────────
    _server.on("/api/rfid/cards", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, rfidModule.cardsToJson());
    });

    _server.on("/api/rfid/read", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (rfidModule.isReading()) {
            _sendJson(req, 200, "{\"ok\":true,\"reading\":true}"); return;
        }
        if (rfidModule.cardAvailable()) {
            RfidCard c = rfidModule.lastCard();
            rfidModule.clearCard();
            _sendJson(req, 200, "{\"ok\":true,\"uid\":\"" + c.uid +
                      "\",\"type\":\"" + c.type + "\"}");
        } else {
            rfidModule.startRead();
            _sendJson(req, 200, "{\"ok\":true,\"reading\":true}");
        }
    });

    POST_BODY_MOD("/api/rfid/save", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l)) { _sendJson(req, 400, "{\"error\":\"JSON\"}"); return; }
        RfidCard card;
        card.name = doc["name"] | "Card";
        card.uid  = doc["uid"]  | "";
        card.type = doc["type"] | "";
        uint32_t id = rfidModule.saveCard(card);
        _sendJson(req, 200, id ? "{\"ok\":true,\"id\":" + String(id) + "}" :
                                  "{\"error\":\"Storage full\"}");
    });

    POST_BODY_MOD("/api/rfid/delete", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        bool ok = rfidModule.deleteCard(doc["id"] | 0u);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Not found\"}");
    });

    POST_BODY_MOD("/api/rfid/write", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        String uid = doc["uid"] | "";
        bool ok = rfidModule.writeCard(uid);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Write failed\"}");
    });

    POST_BODY_MOD("/api/rfid/emulate", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        bool ok = rfidModule.startEmulate(doc["id"] | 0u);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Card not found\"}");
    });

    _server.on("/api/rfid/emulate/stop", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            rfidModule.stopEmulate();
            _sendJson(req, 200, "{\"ok\":true}");
        });


    // ── Sub-GHz ─────────────────────────────────────────────
    _server.on("/api/subghz/signals", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, subGhzModule.signalsToJson());
    });

    POST_BODY_MOD("/api/subghz/capture/start", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        float freq = doc["freq"] | 433.92f;
        subGhzModule.startCapture(freq);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    _server.on("/api/subghz/capture/stop", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            subGhzModule.stopCapture();
            _sendJson(req, 200, "{\"ok\":true}");
        });

    _server.on("/api/subghz/capture/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        String data = subGhzModule.pollCaptured();
        // Return as JSON packets array
        String out = "{\"ok\":true,\"packets\":[";
        int pos = 0; bool first = true;
        while (pos < (int)data.length()) {
            int nl = data.indexOf('\n', pos);
            if (nl < 0) nl = data.length();
            String line = data.substring(pos, nl);
            if (line.length()) {
                if (!first) out += ",";
                out += line;
                first = false;
            }
            pos = nl + 1;
        }
        out += "]}";
        _sendJson(req, 200, out);
    });

    POST_BODY_MOD("/api/subghz/save", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        SubGhzSignal sig;
        sig.name     = doc["name"]    | "Signal";
        sig.freqMhz  = doc["freq"]    | 433.92f;
        sig.protocol = doc["protocol"]| "";
        sig.captured = doc["captured"]| "";
        uint32_t id = subGhzModule.saveSignal(sig);
        _sendJson(req, 200, id ? "{\"ok\":true,\"id\":" + String(id) + "}" :
                                  "{\"error\":\"Storage full\"}");
    });

    POST_BODY_MOD("/api/subghz/replay", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        uint32_t id = doc["id"] | 0u;
        bool ok = subGhzModule.replaySignal(id);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Signal not found\"}");
    });

    POST_BODY_MOD("/api/subghz/delete", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        bool ok = subGhzModule.deleteSignal(doc["id"] | 0u);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Not found\"}");
    });

    POST_BODY_MOD("/api/subghz/rename", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        bool ok = subGhzModule.renameSignal(doc["id"] | 0u, doc["name"] | "");
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Not found\"}");
    });


    POST_BODY_MOD("/api/nrf24/rc", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        String dir = doc["direction"] | "S";
        uint8_t speed = doc["speed"] | 50;
        nrf24Module.sendRcCommand(dir[0], speed);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    POST_BODY_MOD("/api/nrf24/scan/start", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        nrf24Module.startScan();
        _sendJson(req, 200, "{\"ok\":true}");
    });

    _server.on("/api/nrf24/scan/stop", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            nrf24Module.stopScan();
            _sendJson(req, 200, "{\"ok\":true}");
        });

    _server.on("/api/nrf24/scan/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        const uint8_t* ch = nrf24Module.scanData();
        String out = "{\"ok\":true,\"channels\":[";
        for (int i = 0; i < NRF24_CHANNELS; i++) {
            if (i) out += ",";
            out += String(ch[i]);
        }
        out += "]}";
        _sendJson(req, 200, out);
    });

    POST_BODY_MOD("/api/nrf24/sniff/start", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        nrf24Module.startSniff();
        _sendJson(req, 200, "{\"ok\":true}");
    });

    _server.on("/api/nrf24/sniff/stop", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            nrf24Module.stopSniff();
            _sendJson(req, 200, "{\"ok\":true}");
        });

    _server.on("/api/nrf24/sniff/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        String data = nrf24Module.pollSniffPacket();
        String out = "{\"ok\":true,\"packets\":[";
        int pos = 0; bool first = true;
        while (pos < (int)data.length()) {
            int nl = data.indexOf('\n', pos);
            if (nl < 0) nl = data.length();
            String line = data.substring(pos, nl);
            if (line.length()) {
                if (!first) out += ",";
                out += line;
                first = false;
            }
            pos = nl + 1;
        }
        out += "]}";
        _sendJson(req, 200, out);
    });

    POST_BODY_MOD("/api/nrf24/replay", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        bool ok = nrf24Module.replayPackets();
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"No captured packets\"}");
    });

    POST_BODY_MOD("/api/nrf24/replay/start", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        nrf24Module.startReplayCapture();
        _sendJson(req, 200, "{\"ok\":true}");
    });

    _server.on("/api/nrf24/replay/stop", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            nrf24Module.stopReplayCapture();
            _sendJson(req, 200, "{\"ok\":true,\"count\":" +
                      String(nrf24Module.replayPacketCount()) + "}");
        });

    // ── System Module ────────────────────────────────────────
    _server.on("/api/system/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, sysModule.getStatusJson());
    });

    _server.on("/api/gps/info", HTTP_GET, [](AsyncWebServerRequest* req) {
        GpsInfo g = sysModule.getGpsInfo();
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"lat\":\"%.6f\",\"lon\":\"%.6f\","
            "\"speed\":\"%.1f\",\"sats\":%u,\"alt\":\"%.1f\",\"fix\":\"%s\"}",
            g.lat, g.lon, g.speed, (unsigned)g.sats, g.alt, g.fix.c_str());
        _sendJson(req, 200, String(buf));
    });


    POST_BODY_MOD("/api/system/led", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        LedConfig cfg;
        cfg.type    = (LedType)(doc["type"] | 0);
        cfg.mode    = (LedMode)(doc["mode"] | 0);
        cfg.dataPin = doc["dataPin"] | 13;
        cfg.clkPin  = doc["clkPin"]  | 14;
        sysModule.setLedMode(cfg);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    POST_BODY_MOD("/api/system/timezone", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        String tz = doc["timezone"] | "UTC";
        sysModule.setTimezone(tz);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    POST_BODY_MOD("/api/ghostlink", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        sysModule.setGhostLink(doc["enabled"] | false);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    _server.on("/api/system/gpio/overview", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, sysModule.gpioOverviewJson());
    });

    _server.on("/api/system/schedule", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, sysModule.scheduleTasksToJson());
    });

    POST_BODY_MOD("/api/system/schedule/add", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        SysScheduleTask task;
        task.name    = doc["name"]   | "Task";
        task.time    = doc["time"]   | "08:00";
        task.action  = doc["action"] | "reboot";
        task.enabled = doc["enabled"]| true;
        uint32_t id = sysModule.addScheduleTask(task);
        _sendJson(req, 200, "{\"ok\":true,\"id\":" + String(id) + "}");
    });

    POST_BODY_MOD("/api/system/schedule/delete", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        bool ok = sysModule.deleteScheduleTask(doc["id"] | 0u);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Not found\"}");
    });

    POST_BODY_MOD("/api/system/schedule/toggle", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc; deserializeJson(doc, d, l);
        bool ok = sysModule.toggleScheduleTask(doc["id"] | 0u, doc["enabled"] | true);
        _sendJson(req, 200, ok ? "{\"ok\":true}" : "{\"error\":\"Not found\"}");
    });

    POST_BODY_MOD("/api/system/reboot", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        _sendJson(req, 200, "{\"ok\":true}");
        delay(300);
        ESP.restart();
    });

    POST_BODY_MOD("/api/system/reset", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        _sendJson(req, 200, "{\"ok\":true}");
        delay(300);
        // Erase NVS + LittleFS then restart
        LittleFS.format();
        ESP.restart();
    });
    // ═══════════════════════════════════════════════════════════
    // Module Status + GPIO endpoints
    // ═══════════════════════════════════════════════════════════

    // ── NRF24 ────────────────────────────────────────────────
    _server.on("/api/nrf24/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, nrf24Module.statusJson());
    });
    _server.on("/api/nrf24/gpio", HTTP_GET, [](AsyncWebServerRequest* req) {
        Nrf24GpioConfig c = nrf24Module.loadGpioConfig();
        JsonDocument doc;
        doc["moduleType"] = (int)c.moduleType;
        doc["ce"]  = c.ce;  doc["csn"]  = c.csn;
        doc["sck"] = c.sck; doc["mosi"] = c.mosi;
        doc["miso"]= c.miso;doc["irq"]  = c.irq;
        doc["spiBus"]   = c.spiBus;
        doc["dataRate"] = (int)c.dataRate;
        doc["paLevel"]  = (int)c.paLevel;
        String out; serializeJson(doc, out);
        _sendJson(req, 200, out);
    });
    POST_BODY_MOD("/api/nrf24/gpio", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l) != DeserializationError::Ok) {
            _sendJson(req, 400, "{\"error\":\"JSON\"}"); return;
        }
        Nrf24GpioConfig cfg;
        cfg.moduleType = (Nrf24Module_t)(doc["moduleType"] | 0);
        cfg.ce   = doc["ce"]   | (uint8_t)4;
        cfg.csn  = doc["csn"]  | (uint8_t)5;
        cfg.sck  = doc["sck"]  | (uint8_t)18;
        cfg.mosi = doc["mosi"] | (uint8_t)23;
        cfg.miso = doc["miso"] | (uint8_t)19;
        cfg.irq  = doc["irq"]  | (uint8_t)0;
        cfg.spiBus   = doc["spiBus"]   | (uint8_t)0;
        cfg.dataRate = (Nrf24DataRate)(doc["dataRate"] | 1);
        cfg.paLevel  = (Nrf24PaLevel)(doc["paLevel"]   | 2);
        nrf24Module.reinit(cfg);
        _sendJson(req, 200, "{\"ok\":true}");
    });
    POST_BODY_MOD("/api/nrf24/config", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l) != DeserializationError::Ok) {
            _sendJson(req, 400, "{\"error\":\"JSON\"}"); return;
        }
        if (doc["channel"].is<uint8_t>())
            nrf24Module.setChannel(doc["channel"].as<uint8_t>());
        if (doc["dataRate"].is<int>())
            nrf24Module.setDataRate((Nrf24DataRate)doc["dataRate"].as<int>());
        _sendJson(req, 200, "{\"ok\":true}");
    });

    // ── SubGHz ───────────────────────────────────────────────
    _server.on("/api/subghz/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, subGhzModule.statusJson());
    });
    _server.on("/api/subghz/gpio", HTTP_GET, [](AsyncWebServerRequest* req) {
        SubGhzGpioConfig c = subGhzModule.loadGpioConfig();
        JsonDocument doc;
        doc["gdo0"] = c.gdo0; doc["gdo2"] = c.gdo2;
        doc["cs"]   = c.cs;   doc["sck"]  = c.sck;
        doc["mosi"] = c.mosi; doc["miso"] = c.miso;
        doc["spiBus"] = c.spiBus;
        String out; serializeJson(doc, out);
        _sendJson(req, 200, out);
    });
    POST_BODY_MOD("/api/subghz/gpio", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l) != DeserializationError::Ok) {
            _sendJson(req, 400, "{\"error\":\"JSON\"}"); return;
        }
        SubGhzGpioConfig cfg;
        cfg.gdo0   = doc["gdo0"]   | (uint8_t)34;
        cfg.gdo2   = doc["gdo2"]   | (uint8_t)35;
        cfg.cs     = doc["cs"]     | (uint8_t)5;
        cfg.sck    = doc["sck"]    | (uint8_t)18;
        cfg.mosi   = doc["mosi"]   | (uint8_t)23;
        cfg.miso   = doc["miso"]   | (uint8_t)19;
        cfg.spiBus = doc["spiBus"] | (uint8_t)0;
        subGhzModule.reinit(cfg);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    // ── RFID ─────────────────────────────────────────────────
    _server.on("/api/rfid/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, rfidModule.statusJson());
    });
    _server.on("/api/rfid/gpio", HTTP_GET, [](AsyncWebServerRequest* req) {
        RfidGpioConfig c = rfidModule.loadGpioConfig();
        JsonDocument doc;
        doc["dataPin"]  = c.dataPin;
        doc["clkPin"]   = c.clkPin;
        doc["powerPin"] = c.powerPin;
        String out; serializeJson(doc, out);
        _sendJson(req, 200, out);
    });
    POST_BODY_MOD("/api/rfid/gpio", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l) != DeserializationError::Ok) {
            _sendJson(req, 400, "{\"error\":\"JSON\"}"); return;
        }
        RfidGpioConfig cfg;
        cfg.dataPin  = doc["dataPin"]  | (uint8_t)14;
        cfg.clkPin   = doc["clkPin"]   | (uint8_t)13;
        cfg.powerPin = doc["powerPin"] | (uint8_t)12;
        rfidModule.reinit(cfg);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    // ── NFC ──────────────────────────────────────────────────
    _server.on("/api/nfc/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        _sendJson(req, 200, nfcModule.statusJson());
    });
    _server.on("/api/nfc/gpio", HTTP_GET, [](AsyncWebServerRequest* req) {
        NfcGpioConfig c = nfcModule.loadGpioConfig();
        JsonDocument doc;
        doc["iface"] = (int)c.iface;
        doc["sda"]   = c.sda;   doc["scl"]   = c.scl;
        doc["ss"]    = c.ss;    doc["irq"]   = c.irq;
        doc["reset"] = c.reset;
        String out; serializeJson(doc, out);
        _sendJson(req, 200, out);
    });
    POST_BODY_MOD("/api/nfc/gpio", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l) != DeserializationError::Ok) {
            _sendJson(req, 400, "{\"error\":\"JSON\"}"); return;
        }
        NfcGpioConfig cfg;
        cfg.iface = (NfcIface)(doc["iface"] | 0);
        cfg.sda   = doc["sda"]   | (uint8_t)21;
        cfg.scl   = doc["scl"]   | (uint8_t)22;
        cfg.ss    = doc["ss"]    | (uint8_t)15;
        cfg.irq   = doc["irq"]   | (uint8_t)16;
        cfg.reset = doc["reset"] | (uint8_t)17;
        nfcModule.reinit(cfg);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    // ── GPS ──────────────────────────────────────────────────
    _server.on("/api/gps/gpio", HTTP_GET, [](AsyncWebServerRequest* req) {
        GpsGpioConfig c = sysModule.loadGpsGpio();
        JsonDocument doc;
        doc["rxPin"]   = c.rxPin;
        doc["txPin"]   = c.txPin;
        doc["baud"]    = c.baud;
        doc["uartNum"] = c.uartNum;
        String out; serializeJson(doc, out);
        _sendJson(req, 200, out);
    });
    POST_BODY_MOD("/api/gps/gpio", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l) != DeserializationError::Ok) {
            _sendJson(req, 400, "{\"error\":\"JSON\"}"); return;
        }
        GpsGpioConfig cfg;
        cfg.rxPin   = doc["rxPin"]   | (uint8_t)16;
        cfg.txPin   = doc["txPin"]   | (uint8_t)17;
        cfg.baud    = doc["baud"]    | (uint32_t)9600;
        cfg.uartNum = doc["uartNum"] | (uint8_t)1;
        sysModule.reinitGps(cfg);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    // ── LED ──────────────────────────────────────────────────
    _server.on("/api/led/gpio", HTTP_GET, [](AsyncWebServerRequest* req) {
        LedConfig c = sysModule.loadLedConfig();
        JsonDocument doc;
        doc["type"]       = (int)c.type;
        doc["mode"]       = (int)c.mode;
        doc["dataPin"]    = c.dataPin;
        doc["numLeds"]    = c.numLeds;
        doc["r"]          = c.r;
        doc["g"]          = c.g;
        doc["b"]          = c.b;
        doc["brightness"] = c.brightness;
        String out; serializeJson(doc, out);
        _sendJson(req, 200, out);
    });
    POST_BODY_MOD("/api/led/gpio", [](AsyncWebServerRequest* req, uint8_t* d, size_t l) {
        JsonDocument doc;
        if (deserializeJson(doc, d, l) != DeserializationError::Ok) {
            _sendJson(req, 400, "{\"error\":\"JSON\"}"); return;
        }
        LedConfig cfg;
        cfg.type       = (LedType)(doc["type"]       | 0);
        cfg.mode       = (LedMode)(doc["mode"]       | 0);
        cfg.dataPin    = doc["dataPin"]    | (uint8_t)2;
        cfg.numLeds    = doc["numLeds"]    | (uint8_t)8;
        cfg.r          = doc["r"]          | (uint8_t)255;
        cfg.g          = doc["g"]          | (uint8_t)0;
        cfg.b          = doc["b"]          | (uint8_t)128;
        cfg.brightness = doc["brightness"] | (uint8_t)128;
        sysModule.setLedMode(cfg);
        _sendJson(req, 200, "{\"ok\":true}");
    });

    // ── Hardware summary ─────────────────────────────────────
    _server.on("/api/system/hardware", HTTP_GET, [](AsyncWebServerRequest* req) {
        char buf[200];
        snprintf(buf, sizeof(buf),
                 "{\"nrf24\":%s,\"subghz\":%s,\"rfid\":%s,"
                 "\"nfc\":%s,\"gps\":%s,\"led\":%s}",
                 nrf24Module.isConnected()  ? "true":"false",
                 subGhzModule.isConnected() ? "true":"false",
                 rfidModule.isConnected()   ? "true":"false",
                 nfcModule.isConnected()    ? "true":"false",
                 sysModule.isGpsConnected() ? "true":"false",
                 sysModule.isLedActive()    ? "true":"false");
        _sendJson(req, 200, String(buf));
    });
}
