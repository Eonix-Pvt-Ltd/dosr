#include "settings.h"
#include "config.h"
#include <Preferences.h>

static const uint16_t SETTINGS_MAGIC   = 0x4D46;  // 'MF'
static const uint8_t  SETTINGS_VERSION = 2;

// Version 1 ended at deviceName; version 2 appended phBufLo/phBufHi. With
// 4-byte struct alignment and a float as the first new field, the v1 blob
// length is exactly the offset of that field.
static_assert(alignof(Settings) == 4, "v1 migration assumes 4-byte alignment");
static const size_t SETTINGS_V1_SIZE = offsetof(Settings, phBufLo);
static const uint32_t SAVE_DEBOUNCE_MS = 3000;

const char* const PUMP_MODE_NAMES[4]   = { "Off", "Manual", "Scheduled", "Auto" };
const char* const AUTO_TARGET_NAMES[3] = { "pH down", "pH up", "EC up" };

Settings cfg;

static Preferences prefs;
static bool     dirty      = false;
static uint32_t dirtySince = 0;

void settingsDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic   = SETTINGS_MAGIC;
  cfg.version = SETTINGS_VERSION;

  for (uint8_t i = 0; i < NUM_PUMPS; i++) {
    snprintf(cfg.pump[i].name, sizeof(cfg.pump[i].name), "Pump %u", i + 1);
    cfg.pump[i].mlPerSec        = 1.0f;   // placeholder until the user calibrates
    cfg.pump[i].doseMl          = 5.0f;
    cfg.pump[i].intervalMin     = 720;    // twice a day
    cfg.pump[i].lockoutMin      = 15;
    cfg.pump[i].mode            = MODE_MANUAL;
    cfg.pump[i].target          = TGT_PH_DOWN;
    cfg.pump[i].maxDosesPerHour = 4;
  }

  cfg.phSetpoint  = 6.0f;
  cfg.phDeadband  = 0.3f;
  cfg.ecSetpoint  = 1400.0f;
  cfg.ecDeadband  = 100.0f;

  // Neutral starting point for a PH4502C-style board. Recalibrate before use.
  cfg.phVolt4     = 3.05f;
  cfg.phVolt7     = 2.50f;
  cfg.phBufLo     = 4.00f;
  cfg.phBufHi     = 7.00f;
  cfg.ecCalFactor = 1000.0f;
  cfg.tempC       = 25.0f;

  cfg.settleSec   = 5;
  cfg.mixWaitSec  = 60;

  cfg.floatEnabled   = false;
  cfg.floatActiveLow = true;

  cfg.mqttPort = 1883;
  strlcpy(cfg.deviceName, "dosr", sizeof(cfg.deviceName));
}

void settingsBegin() {
  prefs.begin("minifeeder", false);
  size_t len = prefs.getBytesLength("cfg");
  bool ok = false;
  bool migrated = false;

  if (len == sizeof(Settings) || len == SETTINGS_V1_SIZE) {
    // Start from defaults so fields an older blob lacks keep sane values;
    // getBytes() then overwrites only the first `len` bytes.
    settingsDefaults();
    Settings tmp = cfg;
    prefs.getBytes("cfg", &tmp, sizeof(tmp));
    if (tmp.magic == SETTINGS_MAGIC) {
      if (len == sizeof(Settings) && tmp.version == SETTINGS_VERSION) {
        ok = true;
      } else if (len == SETTINGS_V1_SIZE && tmp.version == 1) {
        ok = migrated = true;
      }
    }
    if (ok) cfg = tmp;
  }

  if (!ok) {
    Serial.println(F("[cfg] no valid settings found, writing defaults"));
    settingsDefaults();
    settingsSaveNow();
  } else if (migrated) {
    Serial.println(F("[cfg] settings migrated v1 -> v2"));
    settingsSaveNow();
  } else {
    Serial.println(F("[cfg] settings loaded"));
  }
}

void settingsSaveNow() {
  cfg.magic   = SETTINGS_MAGIC;
  cfg.version = SETTINGS_VERSION;
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  dirty = false;
  Serial.println(F("[cfg] saved"));
}

void settingsMarkDirty() {
  dirty = true;
  dirtySince = millis();
}

bool settingsPendingSave() { return dirty; }

void settingsTick() {
  if (dirty && millis() - dirtySince > SAVE_DEBOUNCE_MS) {
    settingsSaveNow();
  }
}
