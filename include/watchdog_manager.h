#pragma once
// ============================================================
//  watchdog_manager.h  –  Batch 3: Self-Healing Watchdog
//
//  Features:
//    - Hardware watchdog via ESP32 esp_task_wdt API
//    - Software watchdog: detects loop() stall (>WDT_LOOP_MAX_MS)
//    - Module health tracking: each module feeds its own timer
//    - Crash reason logging on boot (esp_reset_reason)
//    - Memory leak detection: alerts if heap drops below threshold
//    - Auto-save crash info to /crash_log.json
//
//  API:
//    GET /api/v1/watchdog/status  — health status of all modules
//    GET /api/v1/watchdog/crashes — crash history
//    POST /api/v1/watchdog/config — enable/disable, set thresholds
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <vector>

#define WDT_CFG_FILE        "/wdt_config.json"
#define WDT_CRASH_FILE      "/crash_log.json"
#define WDT_TAG             "[WDT]"
#define WDT_HW_TIMEOUT_S    30      // hardware watchdog timeout (seconds)
#define WDT_LOOP_MAX_MS     10000   // software: max ms between loop() calls
#define WDT_HEAP_MIN_BYTES  20000   // alert if free heap drops below this
#define WDT_MAX_CRASHES     20      // max crash entries to keep

struct WdtCrashEntry {
    uint32_t    id;
    uint32_t    timestamp;
    String      timeStr;
    String      reason;     // esp_reset_reason string
    uint32_t    heapAtCrash;
    uint32_t    uptimeSec;
};

struct WdtModuleHealth {
    String        name;
    unsigned long lastFeedMs;   // millis() of last feed
    uint32_t      timeoutMs;    // how long before considered stalled
    bool          stalled;

    bool isStalled() const {
        if (timeoutMs == 0) return false;
        return (millis() - lastFeedMs) > timeoutMs;
    }
};

class WatchdogManager {
public:
    WatchdogManager();

    void begin();
    void loop();        // must be called every loop() iteration

    // ── Hardware watchdog ─────────────────────────────────────
    void hwFeed();      // feed hardware watchdog
    bool hwEnabled() const { return _hwEnabled; }

    // ── Module health tracking ────────────────────────────────
    // Register a module — returns its index for fast feed()
    uint8_t registerModule(const String& name, uint32_t timeoutMs = 30000);
    void    feedModule(uint8_t idx);
    bool    isModuleStalled(uint8_t idx) const;

    // ── Memory monitoring ─────────────────────────────────────
    bool isHeapCritical() const { return ESP.getFreeHeap() < WDT_HEAP_MIN_BYTES; }
    uint32_t minHeapSeen() const { return _minHeapSeen; }

    // ── Status ────────────────────────────────────────────────
    String statusJson() const;
    String crashLogJson() const;

    // ── Config ────────────────────────────────────────────────
    void   setHwEnabled(bool en);
    void   setHeapThreshold(uint32_t bytes);
    bool   loadConfig();
    bool   saveConfig();

private:
    bool    _hwEnabled;
    bool    _hwStarted;
    uint32_t _heapThreshold;
    uint32_t _minHeapSeen;
    unsigned long _lastLoopMs;
    unsigned long _lastHeapCheck;

    std::vector<WdtModuleHealth> _modules;

    void _logCrashOnBoot();
    void _saveCrash(const WdtCrashEntry& entry);
    String _resetReasonStr(esp_reset_reason_t reason) const;
    String _buildTimeStr() const;
};

extern WatchdogManager wdtMgr;
