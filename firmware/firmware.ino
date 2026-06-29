// Waveshare ESP32-S3-Relay-6CH port. Separate project. Original working ESP32 project untouched.

#include <Arduino.h>
#include <DHT.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>
#include <stdarg.h>
#include "config.h"

// Clean two-zone test firmware for ESP32 greenhouse vent motors.
// No MQTT, no Blynk, no Raspberry integration.

static const bool ENABLE_STARTUP_CLOSE = false;

static const uint8_t PIN_DHT_1 = 4;
static const uint8_t PIN_OPEN_1 = 1;
static const uint8_t PIN_CLOSE_1 = 2;

static const uint8_t PIN_DHT_2 = 5;
static const uint8_t PIN_OPEN_2 = 41;
static const uint8_t PIN_CLOSE_2 = 42;

static const uint8_t PIN_SERVICE_OPEN = 45;
static const uint8_t PIN_SERVICE_CLOSE = 46;

// TODO Waveshare ESP32-S3: confirm ADC pins and voltage dividers before connecting analog sensors.
// Analog sensors are intentionally disabled in this port until the Waveshare header pins are verified.
// Wind sensor 0-5V must use voltage divider to 0-3.3V.
#define ANALOG_SENSOR_PINS_CONFIRMED 0
#if !ANALOG_SENSOR_PINS_CONFIRMED
#warning "Waveshare ESP32-S3 analog sensor pins are NOT confirmed. GPIO34/GPIO35/GPIO27 are legacy placeholders only; do not connect analog sensors until ADC pins and voltage dividers are verified."
#endif
static const uint8_t PIN_WIND_ADC = 34;     // Legacy placeholder: GPIO34 is not an ESP32-S3 ADC pin.
static const uint8_t PIN_RAIN_ADC = 35;     // Legacy placeholder: GPIO35 is not an ESP32-S3 ADC pin.
static const uint8_t PIN_WATER_LEVEL = 27;  // Legacy placeholder: not confirmed on Waveshare header.

static const int RELAY_ON = HIGH;
static const int RELAY_OFF = LOW;

static const uint8_t DHT_TYPE = DHT22;

static const float DEFAULT_OPEN_START_TEMP_C = 23.0f;
static const float DEFAULT_TEMP_STEP_C = 0.5f;
static const float DEFAULT_CLOSE_HYSTERESIS_C = 2.0f;
static const float DEFAULT_MAX_TEMP_C = 26.0f;

static const unsigned long DEFAULT_DHT_READ_MS = 2500UL;
static const unsigned long STATUS_PRINT_MS = 5000UL;
static const unsigned long ANALOG_READ_MS = 1000UL;
static const float DHT_MIN_VALID_TEMP_C = -20.0f;
static const float DHT_MAX_VALID_TEMP_C = 80.0f;
static const float DHT_MIN_VALID_HUM_PCT = 0.0f;
static const float DHT_MAX_VALID_HUM_PCT = 100.0f;
static const float DHT_BASE_MAX_TEMP_JUMP_C = 2.5f;
static const float DHT_TEMP_JUMP_C_PER_SEC = 0.08f;
static const float DHT_BASE_MAX_HUM_JUMP_PCT = 12.0f;
static const float DHT_HUM_JUMP_PCT_PER_SEC = 0.50f;

static const unsigned long DEFAULT_MOVE_MS = 2000UL;
static const unsigned long DEFAULT_PAUSE_MS = 10000UL;
static const unsigned long DEFAULT_SWITCH_MS = 1000UL;
static const unsigned long DEFAULT_STARTUP_CLOSE_MS = 10000UL;
static const unsigned long DEFAULT_EXTRA_CLOSE_MS = 5000UL;
static const unsigned long DEFAULT_FULL_TRAVEL_MS = 120000UL;
static const unsigned long DEFAULT_SERVICE_MOTOR_MS = 120000UL;

static const int DEFAULT_WIND_ALARM_THRESHOLD = 3000;
static const int DEFAULT_RAIN_ALARM_THRESHOLD = 3000;
static const int DEFAULT_WATER_LOW_ACTIVE_STATE = LOW;


struct WifiCredential {
  const char* ssid;
  const char* pass;
};

WifiCredential wifiList[] = {
  { WIFI_SSID, WIFI_PASS }
};

static const size_t WIFI_LIST_COUNT = sizeof(wifiList) / sizeof(wifiList[0]);
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000UL;
static const unsigned long WIFI_RETRY_INTERVAL_MS = 30000UL;
static const unsigned long WIFI_STATUS_PRINT_MS = 10000UL;
static const unsigned long WIFI_SCAN_INTERVAL_MS = 60000UL;

static const bool WAVESHARE_TEST_NETWORK_MODE = true;
static const bool USE_STATIC_IP = false;  // Public export uses DHCP by default.

static const char* FW_VERSION = "greenhouse_vents_clean_test-wifi-v3";
static const uint16_t EVENT_LOG_CAPACITY = 120;
static const size_t EVENT_LOG_TEXT_LEN = 112;
static const size_t EVENT_LOG_WEB_LIMIT = 25;

// Pороги wind/rain треба відкалібрувати по реальних значеннях Serial Monitor.

static const uint8_t CONFIRM_READS = 3;
static const uint8_t MAX_DHT_FAILS = 5;
static const bool ALLOW_MANUAL_DURING_EMERGENCY = true;

RTC_DATA_ATTR int bootCounter = 0;

enum Direction {
  DIR_NONE,
  DIR_OPEN,
  DIR_CLOSE
};

enum CommandSource {
  SRC_NONE,
  SRC_AUTO,
  SRC_MANUAL,
  SRC_STARTUP,
  SRC_EMERGENCY
};

enum ZoneState {
  IDLE,
  STARTUP_CLOSING,
  OPENING,
  CLOSING,
  EXTRA_CLOSING,
  WAIT_AFTER_MOVE,
  WAIT_SWITCH,
  ERROR,
  LOCKED
};

enum ZoneCommandKind {
  ZCMD_NONE,
  ZCMD_STEP_OPEN,
  ZCMD_STEP_CLOSE,
  ZCMD_FULL_OPEN,
  ZCMD_FULL_CLOSE,
  ZCMD_EXTRA_CLOSE,
  ZCMD_STARTUP_CLOSE
};

enum ServiceMotorState {
  SMS_IDLE,
  SMS_WAIT_SWITCH,
  SMS_OPENING,
  SMS_CLOSING
};

enum WifiState {
  WIFI_STATE_OFF,
  WIFI_STATE_IDLE,
  WIFI_STATE_SCANNING,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_RECONNECT_WAIT,
  WIFI_STATE_ERROR
};

struct ZoneConfig {
  float openStartTemp;
  float tempStep;
  float closeHysteresis;
  float maxTemp;
};

struct SystemConfig {
  unsigned long dhtReadMs;
  unsigned long moveMs;
  unsigned long pauseMs;
  unsigned long switchMs;
  unsigned long startupCloseMs;
  unsigned long extraCloseMs;
  unsigned long fullTravelMs;
  unsigned long serviceMotorMs;
  bool enableWindAlarm;
  bool enableRainAlarm;
  bool enableWaterMonitor;
  int windAlarmThreshold;
  int rainAlarmThreshold;
  int waterLowActiveState;
};

struct Zone {
  const char* name;
  uint8_t dhtPin;
  uint8_t openPin;
  uint8_t closePin;
  float currentTemp;
  float currentHum;
  bool dhtOk;
  uint8_t dhtFailCount;
  int currentStep;
  int targetStep;
  int maxStep;
  ZoneState state;
  unsigned long lastActionMs;
  unsigned long motorStartMs;
  unsigned long pauseStartMs;
  Direction lastDirection;
  bool calibrated;
  bool positionUncertain;
  bool locked;
  bool error;

  DHT* dht;
  Direction pendingDirection;
  ZoneState pendingState;
  CommandSource activeSource;
  CommandSource pendingSource;
  CommandSource lastCommandSource;
  unsigned long plannedMoveMs;
  int pendingStepDelta;
  uint8_t openConfirmCount;
  uint8_t closeConfirmCount;
  bool extraClosePending;
  bool emergencyManualOverride;
  ZoneCommandKind activeCommandKind;
  ZoneCommandKind pendingCommandKind;
  char lastEvent[96];
  unsigned long lastGoodDhtMs;
};

struct ServiceMotor {
  ServiceMotorState state;
  Direction activeDirection;
  Direction pendingDirection;
  unsigned long stateStartMs;
  unsigned long runUntilMs;
  char lastEvent[96];
};

struct EventLogEntry {
  uint32_t seq;
  uint32_t boot;
  uint32_t ms;
  char text[EVENT_LOG_TEXT_LEN];
};

DHT dht1(PIN_DHT_1, DHT_TYPE);
DHT dht2(PIN_DHT_2, DHT_TYPE);

Zone zone1;
Zone zone2;
Zone* zones[2] = { &zone1, &zone2 };
WebServer server(80);
ServiceMotor serviceMotor;
Preferences eventPrefs;
EventLogEntry eventLog[EVENT_LOG_CAPACITY];

ZoneConfig zoneConfigs[2] = {
  { DEFAULT_OPEN_START_TEMP_C, DEFAULT_TEMP_STEP_C, DEFAULT_CLOSE_HYSTERESIS_C, DEFAULT_MAX_TEMP_C },
  { DEFAULT_OPEN_START_TEMP_C, DEFAULT_TEMP_STEP_C, DEFAULT_CLOSE_HYSTERESIS_C, DEFAULT_MAX_TEMP_C }
};

SystemConfig cfg = {
  DEFAULT_DHT_READ_MS,
  DEFAULT_MOVE_MS,
  DEFAULT_PAUSE_MS,
  DEFAULT_SWITCH_MS,
  DEFAULT_STARTUP_CLOSE_MS,
  DEFAULT_EXTRA_CLOSE_MS,
  DEFAULT_FULL_TRAVEL_MS,
  DEFAULT_SERVICE_MOTOR_MS,
  false,
  false,
  false,
  DEFAULT_WIND_ALARM_THRESHOLD,
  DEFAULT_RAIN_ALARM_THRESHOLD,
  DEFAULT_WATER_LOW_ACTIVE_STATE
};

bool autoMode = true;
bool globalEmergency = false;
bool routerConnected = false;
bool fallbackApActive = false;
bool wifiEnabled = true;
bool wifiReconnectNeeded = true;
bool wifiScanRequested = true;
bool wifiScanForConnect = true;
bool wifiManualScanRequested = false;
bool wifiHasConnectedOnce = false;
bool wifiWeakSignalWarned = false;
bool wifiCriticalSignalWarned = false;
WifiState wifiState = WIFI_STATE_OFF;
int wifiCurrentCredentialIndex = -1;
int wifiAvailableCredentialIndices[WIFI_LIST_COUNT];
int wifiAvailableCredentialCount = 0;
int wifiAvailableCredentialCursor = 0;
uint32_t wifiDisconnectCount = 0;
uint32_t wifiReconnectSuccessCount = 0;
int32_t wifiLastRssi = 0;

char emergencyReason[32] = "none";
char serialBuffer[64];
size_t serialLen = 0;
char lastActionMessage[96] = "Очікування";

String routerSsid = WIFI_SSID;
String routerPass = WIFI_PASS;
char wifiConnectedSsid[33] = "";
char wifiTargetSsid[33] = "";
char wifiLastDisconnectReason[64] = "none";

int windRaw = 0;
int rainRaw = 0;
int waterState = HIGH;
bool waterLow = false;
bool waterKnown = false;

unsigned long lastDhtReadMs = 0;
unsigned long lastAnalogReadMs = 0;
unsigned long lastStatusPrintMs = 0;
unsigned long wifiConnectStartMs = 0;
unsigned long wifiLastRetryMs = 0;
unsigned long wifiLastStatusMs = 0;
unsigned long wifiLastScanStartMs = 0;
unsigned long wifiLastConnectAttemptMs = 0;
unsigned long wifiConnectedSinceMs = 0;
uint16_t eventLogCount = 0;
uint16_t eventLogHead = 0;
uint32_t eventLogNextSeq = 1;
bool eventLogReady = false;

void resetConfirmCounters(Zone& z);
int calculateMaxStep(const ZoneConfig& config);
float zoneClosedLevelTemp(const Zone& z);
float zoneOpenLevelTemp(const Zone& z);
float closeThresholdForStep(const Zone& z, int currentStep);
int computeTargetStep(const Zone& z, float tempC);
String formatElapsedMs(unsigned long sinceMs);
String formatFloatForLog(float value, uint8_t decimals = 1);
String zoneTelemetryForLog(const Zone& z);
const char* zoneCommandLabel(ZoneCommandKind commandKind);
unsigned long zoneActiveTimerMs(const Zone& z);
unsigned long zoneActiveElapsedMs(const Zone& z);
unsigned long zoneActiveRemainingMs(const Zone& z);
unsigned long commandDurationMs(ZoneCommandKind kind, unsigned long fallbackMs = 0);
void rejectZoneDhtRead(Zone& z, const char* reason);
void setLastActionMessage(const String& message);
void setZoneLastEvent(Zone& z, const String& message);
void setServiceMotorEvent(const String& message);
void setupEventLog();
void saveEventLog();
void appendEventLog(const String& message);
void appendEventLogf(const char* format, ...);
String eventLogText(size_t maxEntries = EVENT_LOG_CAPACITY);
void printEventLog(size_t maxEntries = EVENT_LOG_CAPACITY);
void clearEventLog();
String systemStatusText();
String statusJson();
String webPageHtml();
void setupWiFi();
void processWiFi(unsigned long now);
void setupAccessPoint();
void startWiFiConnect();
void handleWiFiConnected();
void handleWiFiDisconnected(int reasonCode);
void printWiFiStatus();
void scanWiFiNetworks(bool forConnect);
const char* wifiStateToString(WifiState state);
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
void setupWebServer();
void handleRoot();
void handleControlRoute();
void handleStatusRoute();
void handleStatusTextRoute();
void handleEventsTextRoute();
void handleConfigRoute();
void handleWiFiRoute();
void redirectHome();
void printSystemStatus();
void processCommand(const char* cmd);
void serviceSerialInput();
void updateZone(Zone& z, unsigned long now);
void updateServiceMotor(unsigned long now);
void readZoneDhts(unsigned long now);
void readAnalogInputs(unsigned long now);
void triggerEmergency(const char* reason);
void zoneRelaysOff(Zone& z);
void allRelaysOff();
void startOpen(Zone& z, CommandSource source);
void startClose(Zone& z, CommandSource source);
void startFullOpen(Zone& z);
void startFullClose(Zone& z);
void startManualExtraClose(Zone& z);
void stopMotor(Zone& z);
void stopAllMotion();
const char* sourceLabel(CommandSource source);
bool manualCommandAllowed(const Zone& z, bool isStop);
void onManualCommandAccepted(Zone& z, const char* action, bool affectsPosition);

const char* resetReasonLabel(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXTERNAL";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

const char* zoneStateLabel(ZoneState state) {
  switch (state) {
    case IDLE:             return "IDLE";
    case STARTUP_CLOSING:  return "STARTUP_CLOSING";
    case OPENING:          return "OPENING";
    case CLOSING:          return "CLOSING";
    case EXTRA_CLOSING:    return "EXTRA_CLOSING";
    case WAIT_AFTER_MOVE:  return "WAIT_AFTER_MOVE";
    case WAIT_SWITCH:      return "WAIT_SWITCH";
    case ERROR:            return "ERROR";
    case LOCKED:           return "LOCKED";
    default:               return "UNKNOWN";
  }
}

const char* directionLabel(Direction dir) {
  switch (dir) {
    case DIR_OPEN:  return "OPEN";
    case DIR_CLOSE: return "CLOSE";
    default:        return "NONE";
  }
}

const char* sourceLabel(CommandSource source) {
  switch (source) {
    case SRC_AUTO:      return "AUTO";
    case SRC_MANUAL:    return "MANUAL";
    case SRC_STARTUP:   return "STARTUP";
    case SRC_EMERGENCY: return "EMERGENCY";
    default:            return "NONE";
  }
}

const char* zoneCommandLabel(ZoneCommandKind commandKind) {
  switch (commandKind) {
    case ZCMD_STEP_OPEN:     return "STEP_OPEN";
    case ZCMD_STEP_CLOSE:    return "STEP_CLOSE";
    case ZCMD_FULL_OPEN:     return "FULL_OPEN";
    case ZCMD_FULL_CLOSE:    return "FULL_CLOSE";
    case ZCMD_EXTRA_CLOSE:   return "EXTRA_CLOSE";
    case ZCMD_STARTUP_CLOSE: return "STARTUP_CLOSE";
    default:                 return "NONE";
  }
}

const char* relayLabel(uint8_t pin) {
  return digitalRead(pin) == RELAY_ON ? "ON" : "OFF";
}

bool isMotorState(ZoneState state) {
  return state == STARTUP_CLOSING ||
         state == OPENING ||
         state == CLOSING ||
         state == EXTRA_CLOSING;
}

bool zoneIsBusy(const Zone& z) {
  return isMotorState(z.state) || z.state == WAIT_SWITCH;
}

bool manualMoveInProgress(const Zone& z) {
  return (isMotorState(z.state) && z.activeSource == SRC_MANUAL) ||
         (z.state == WAIT_SWITCH && z.pendingSource == SRC_MANUAL);
}

bool preserveAutoExtraCloseOnSensorError(const Zone& z) {
  return z.currentStep == 0 &&
         (z.extraClosePending || z.state == EXTRA_CLOSING);
}

int calculateMaxStep(const ZoneConfig& config) {
  if (config.tempStep <= 0.0f || config.maxTemp < config.openStartTemp) {
    return 1;
  }
  const int steps = static_cast<int>(floorf((config.maxTemp - config.openStartTemp) / config.tempStep));
  return steps < 1 ? 1 : steps;
}

float zoneClosedLevelTemp(const Zone& z) {
  const ZoneConfig& config = (&z == &zone1) ? zoneConfigs[0] : zoneConfigs[1];
  return config.openStartTemp - config.tempStep;
}

float zoneOpenLevelTemp(const Zone& z) {
  const ZoneConfig& config = (&z == &zone1) ? zoneConfigs[0] : zoneConfigs[1];
  if (z.currentStep <= 0) {
    return zoneClosedLevelTemp(z);
  }
  return config.openStartTemp + (z.currentStep - 1) * config.tempStep;
}

float closeThresholdForStep(const Zone& z, int currentStep) {
  const ZoneConfig& config = (&z == &zone1) ? zoneConfigs[0] : zoneConfigs[1];
  if (currentStep <= 0) {
    return config.openStartTemp - config.closeHysteresis;
  }
  const float stepTemp = config.openStartTemp + (currentStep - 1) * config.tempStep;
  return stepTemp - config.closeHysteresis;
}

int computeTargetStep(const Zone& z, float tempC) {
  const ZoneConfig& config = (&z == &zone1) ? zoneConfigs[0] : zoneConfigs[1];
  if (isnan(tempC) || tempC < config.openStartTemp) {
    return 0;
  }
  const float cappedTemp = tempC > config.maxTemp ? config.maxTemp : tempC;
  const int step = static_cast<int>(floorf((cappedTemp - config.openStartTemp) / config.tempStep)) + 1;
  return constrain(step, 0, calculateMaxStep(config));
}

void setLastActionMessage(const String& message) {
  strncpy(lastActionMessage, message.c_str(), sizeof(lastActionMessage) - 1);
  lastActionMessage[sizeof(lastActionMessage) - 1] = '\0';
}

void setZoneLastEvent(Zone& z, const String& message) {
  strncpy(z.lastEvent, message.c_str(), sizeof(z.lastEvent) - 1);
  z.lastEvent[sizeof(z.lastEvent) - 1] = '\0';
}

void setServiceMotorEvent(const String& message) {
  strncpy(serviceMotor.lastEvent, message.c_str(), sizeof(serviceMotor.lastEvent) - 1);
  serviceMotor.lastEvent[sizeof(serviceMotor.lastEvent) - 1] = '\0';
}

void setupEventLog() {
  memset(eventLog, 0, sizeof(eventLog));
  if (!eventPrefs.begin("ghvlog", false)) {
    Serial.println("WARNING: event log storage unavailable");
    return;
  }

  eventLogReady = true;
  const size_t bytesRead = eventPrefs.getBytes("items", eventLog, sizeof(eventLog));
  if (bytesRead != sizeof(eventLog)) {
    memset(eventLog, 0, sizeof(eventLog));
  }

  eventLogCount = static_cast<uint16_t>(eventPrefs.getUInt("count", 0));
  eventLogHead = static_cast<uint16_t>(eventPrefs.getUInt("head", 0));
  eventLogNextSeq = eventPrefs.getULong("seq", 1);

  if (eventLogCount > EVENT_LOG_CAPACITY) {
    eventLogCount = 0;
  }
  if (eventLogHead >= EVENT_LOG_CAPACITY) {
    eventLogHead = 0;
  }
  if (eventLogNextSeq == 0) {
    eventLogNextSeq = 1;
  }
}

void saveEventLog() {
  if (!eventLogReady) {
    return;
  }
  eventPrefs.putBytes("items", eventLog, sizeof(eventLog));
  eventPrefs.putUInt("count", eventLogCount);
  eventPrefs.putUInt("head", eventLogHead);
  eventPrefs.putULong("seq", eventLogNextSeq);
}

void appendEventLog(const String& message) {
  if (!eventLogReady) {
    return;
  }

  String sanitized = message;
  sanitized.replace("\r", " ");
  sanitized.replace("\n", " ");
  sanitized.trim();
  if (sanitized.length() == 0) {
    return;
  }

  EventLogEntry& entry = eventLog[eventLogHead];
  entry.seq = eventLogNextSeq++;
  entry.boot = static_cast<uint32_t>(bootCounter);
  entry.ms = millis();
  strncpy(entry.text, sanitized.c_str(), sizeof(entry.text) - 1);
  entry.text[sizeof(entry.text) - 1] = '\0';

  eventLogHead = (eventLogHead + 1) % EVENT_LOG_CAPACITY;
  if (eventLogCount < EVENT_LOG_CAPACITY) {
    eventLogCount++;
  }

  saveEventLog();
}

void appendEventLogf(const char* format, ...) {
  char buffer[EVENT_LOG_TEXT_LEN];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  appendEventLog(String(buffer));
}

String eventLogText(size_t maxEntries) {
  if (eventLogCount == 0) {
    return String("No events recorded.\r\n");
  }

  const size_t entriesToPrint = min(maxEntries, static_cast<size_t>(eventLogCount));
  const size_t start = (eventLogHead + EVENT_LOG_CAPACITY - entriesToPrint) % EVENT_LOG_CAPACITY;
  String text;
  text.reserve(entriesToPrint * 96);

  for (size_t i = 0; i < entriesToPrint; ++i) {
    const size_t idx = (start + i) % EVENT_LOG_CAPACITY;
    const EventLogEntry& entry = eventLog[idx];
    if (entry.seq == 0 || entry.text[0] == '\0') {
      continue;
    }

    text += "#";
    text += String(entry.seq);
    text += " | boot=";
    text += String(entry.boot);
    text += " | +";
    text += formatElapsedMs(entry.ms);
    text += " | ";
    text += entry.text;
    text += "\r\n";
  }

  return text;
}

void printEventLog(size_t maxEntries) {
  Serial.println("=== EVENT LOG ===");
  Serial.print(eventLogText(maxEntries));
}

void clearEventLog() {
  memset(eventLog, 0, sizeof(eventLog));
  eventLogCount = 0;
  eventLogHead = 0;
  eventLogNextSeq = 1;
  saveEventLog();
}

const char* wifiStateToString(WifiState state) {
  switch (state) {
    case WIFI_STATE_OFF:            return "WIFI_OFF";
    case WIFI_STATE_IDLE:           return "WIFI_IDLE";
    case WIFI_STATE_SCANNING:       return "WIFI_SCANNING";
    case WIFI_STATE_CONNECTING:     return "WIFI_CONNECTING";
    case WIFI_STATE_CONNECTED:      return "WIFI_CONNECTED";
    case WIFI_STATE_RECONNECT_WAIT: return "WIFI_RECONNECT_WAIT";
    case WIFI_STATE_ERROR:          return "WIFI_ERROR";
    default:                  return "WIFI_UNKNOWN";
  }
}

const char* wifiCredentialSsidAt(size_t index) {
  if (index == 0 && routerSsid.length() > 0) {
    return routerSsid.c_str();
  }
  return wifiList[index].ssid;
}

const char* wifiCredentialPassAt(size_t index) {
  if (index == 0 && routerPass.length() > 0) {
    return routerPass.c_str();
  }
  return wifiList[index].pass;
}

bool wifiCredentialConfigured(size_t index) {
  const char* ssid = wifiCredentialSsidAt(index);
  return ssid != nullptr && ssid[0] != '\0';
}

String wifiIpLabel() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("0.0.0.0");
}

String formatElapsedMs(unsigned long sinceMs) {
  if (sinceMs == 0) {
    return "0s";
  }
  const unsigned long totalSeconds = (millis() - sinceMs) / 1000UL;
  const unsigned long hours = totalSeconds / 3600UL;
  const unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
  const unsigned long seconds = totalSeconds % 60UL;
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%luh %lum %lus", hours, minutes, seconds);
  return String(buffer);
}

String formatFloatForLog(float value, uint8_t decimals) {
  return isnan(value) ? String("nan") : String(value, static_cast<unsigned int>(decimals));
}

String zoneTelemetryForLog(const Zone& z) {
  String text;
  text.reserve(96);
  text += "temp=";
  text += formatFloatForLog(z.currentTemp, 1);
  text += "C hum=";
  text += formatFloatForLog(z.currentHum, 1);
  text += "% step=";
  text += String(z.currentStep);
  text += "/";
  text += String(z.maxStep);
  text += " target=";
  text += String(z.targetStep);
  text += " close=";
  text += formatFloatForLog(closeThresholdForStep(z, z.currentStep), 1);
  text += "C";
  return text;
}

unsigned long zoneActiveTimerMs(const Zone& z) {
  if (isMotorState(z.state)) {
    return commandDurationMs(z.activeCommandKind, z.plannedMoveMs);
  }
  if (z.state == WAIT_SWITCH) {
    return commandDurationMs(z.pendingCommandKind, z.plannedMoveMs);
  }
  return 0UL;
}

unsigned long zoneActiveElapsedMs(const Zone& z) {
  if (isMotorState(z.state)) {
    return millis() - z.motorStartMs;
  }
  if (z.state == WAIT_SWITCH) {
    return millis() - z.pauseStartMs;
  }
  return 0UL;
}

unsigned long zoneActiveRemainingMs(const Zone& z) {
  const unsigned long total = zoneActiveTimerMs(z);
  const unsigned long elapsed = zoneActiveElapsedMs(z);
  return elapsed >= total ? 0UL : (total - elapsed);
}

unsigned long commandDurationMs(ZoneCommandKind kind, unsigned long fallbackMs) {
  switch (kind) {
    case ZCMD_STEP_OPEN:
    case ZCMD_STEP_CLOSE:
      return cfg.moveMs;
    case ZCMD_FULL_OPEN:
    case ZCMD_FULL_CLOSE:
      return cfg.fullTravelMs;
    case ZCMD_EXTRA_CLOSE:
      return cfg.extraCloseMs;
    case ZCMD_STARTUP_CLOSE:
      return cfg.startupCloseMs;
    case ZCMD_NONE:
    default:
      return fallbackMs;
  }
}

void resetConfirmCounters(Zone& z) {
  z.openConfirmCount = 0;
  z.closeConfirmCount = 0;
}

void initZone(Zone& z, const char* name, uint8_t dhtPin, uint8_t openPin, uint8_t closePin, DHT& dht) {
  z.name = name;
  z.dhtPin = dhtPin;
  z.openPin = openPin;
  z.closePin = closePin;
  z.currentTemp = NAN;
  z.currentHum = NAN;
  z.dhtOk = false;
  z.dhtFailCount = 0;
  z.currentStep = 0;
  z.targetStep = 0;
  z.maxStep = 1;
  z.state = IDLE;
  z.lastActionMs = 0;
  z.motorStartMs = 0;
  z.pauseStartMs = 0;
  z.lastDirection = DIR_NONE;
  z.calibrated = false;
  z.positionUncertain = false;
  z.locked = false;
  z.error = false;
  z.dht = &dht;
  z.pendingDirection = DIR_NONE;
  z.pendingState = IDLE;
  z.activeSource = SRC_NONE;
  z.pendingSource = SRC_NONE;
  z.lastCommandSource = SRC_NONE;
  z.plannedMoveMs = 0;
  z.pendingStepDelta = 0;
  z.openConfirmCount = 0;
  z.closeConfirmCount = 0;
  z.extraClosePending = false;
  z.emergencyManualOverride = false;
  z.activeCommandKind = ZCMD_NONE;
  z.pendingCommandKind = ZCMD_NONE;
  z.lastGoodDhtMs = 0;
  setZoneLastEvent(z, "Очікування");
}

void setupPins() {
  pinMode(PIN_OPEN_1, OUTPUT);
  pinMode(PIN_CLOSE_1, OUTPUT);
  pinMode(PIN_OPEN_2, OUTPUT);
  pinMode(PIN_CLOSE_2, OUTPUT);
  pinMode(PIN_SERVICE_OPEN, OUTPUT);
  pinMode(PIN_SERVICE_CLOSE, OUTPUT);

#if ANALOG_SENSOR_PINS_CONFIRMED
  pinMode(PIN_WATER_LEVEL, INPUT_PULLUP);
  pinMode(PIN_WIND_ADC, INPUT);
  pinMode(PIN_RAIN_ADC, INPUT);
#endif

  digitalWrite(PIN_OPEN_1, RELAY_OFF);
  digitalWrite(PIN_CLOSE_1, RELAY_OFF);
  digitalWrite(PIN_OPEN_2, RELAY_OFF);
  digitalWrite(PIN_CLOSE_2, RELAY_OFF);
  digitalWrite(PIN_SERVICE_OPEN, RELAY_OFF);
  digitalWrite(PIN_SERVICE_CLOSE, RELAY_OFF);

#if ANALOG_SENSOR_PINS_CONFIRMED
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_WIND_ADC, ADC_11db);
  analogSetPinAttenuation(PIN_RAIN_ADC, ADC_11db);
#endif
}

void zoneRelaysOff(Zone& z) {
  digitalWrite(z.openPin, RELAY_OFF);
  digitalWrite(z.closePin, RELAY_OFF);
}

void allRelaysOff() {
  zoneRelaysOff(zone1);
  zoneRelaysOff(zone2);
  digitalWrite(PIN_SERVICE_OPEN, RELAY_OFF);
  digitalWrite(PIN_SERVICE_CLOSE, RELAY_OFF);
}

void stopMotor(Zone& z) {
  zoneRelaysOff(z);
  z.lastActionMs = millis();
  Serial.printf("[%s] motor STOP, relays OPEN=%s CLOSE=%s\n",
                z.name,
                relayLabel(z.openPin),
                relayLabel(z.closePin));
}

void beginMoveNow(Zone& z) {
  zoneRelaysOff(z);

  if (z.pendingDirection == DIR_OPEN) {
    digitalWrite(z.openPin, RELAY_ON);
  } else if (z.pendingDirection == DIR_CLOSE) {
    digitalWrite(z.closePin, RELAY_ON);
  }

  z.state = z.pendingState;
  z.activeSource = z.pendingSource;
  z.activeCommandKind = z.pendingCommandKind;
  z.plannedMoveMs = commandDurationMs(z.activeCommandKind, z.plannedMoveMs);
  z.motorStartMs = millis();
  z.lastActionMs = z.motorStartMs;
  z.lastDirection = z.pendingDirection;

  if (z.pendingDirection == DIR_OPEN) {
    setZoneLastEvent(z, z.activeSource == SRC_MANUAL ? "Ручне відкривання" : "Крок відкривання");
  } else if (z.pendingDirection == DIR_CLOSE) {
    if (z.activeCommandKind == ZCMD_STARTUP_CLOSE) {
      setZoneLastEvent(z, "Початкове закриття");
    } else if (z.activeCommandKind == ZCMD_EXTRA_CLOSE) {
      setZoneLastEvent(z, "Дотяжка");
    } else {
      setZoneLastEvent(z, z.activeSource == SRC_MANUAL ? "Ручне закривання" : "Крок закривання");
    }
  }

  Serial.printf("[%s] START %s, source=%s, state=%s, duration=%lu ms, step=%d -> target=%d\n",
                z.name,
                directionLabel(z.pendingDirection),
                sourceLabel(z.activeSource),
                zoneStateLabel(z.state),
                z.plannedMoveMs,
                z.currentStep,
                z.targetStep);
  appendEventLog(String(z.name) + " start " +
                 String(directionLabel(z.pendingDirection)) +
                 " | src=" + String(sourceLabel(z.activeSource)) +
                 " | state=" + String(zoneStateLabel(z.state)) +
                 " | " + zoneTelemetryForLog(z) +
                 " | dur=" + String(z.plannedMoveMs) + "ms");
}

void queueMove(Zone& z,
               Direction dir,
               ZoneState motionState,
               unsigned long durationMs,
               int stepDelta,
               CommandSource source,
               ZoneCommandKind commandKind) {
  const bool manualBypass = (source == SRC_MANUAL);

  if (z.locked && !manualBypass) {
    Serial.printf("[%s] command denied: zone is LOCKED\n", z.name);
    return;
  }

  if (z.error && motionState != STARTUP_CLOSING && !manualBypass) {
    Serial.printf("[%s] command denied: zone is in ERROR\n", z.name);
    return;
  }

  if (manualBypass) {
    z.locked = false;
    if (globalEmergency) {
      z.emergencyManualOverride = true;
    }
  }

  zoneRelaysOff(z);
  z.pendingDirection = dir;
  z.pendingState = motionState;
  z.pendingSource = source;
  z.lastCommandSource = source;
  z.plannedMoveMs = durationMs;
  z.pendingStepDelta = stepDelta;
  z.pendingCommandKind = commandKind;
  z.lastActionMs = millis();
  resetConfirmCounters(z);

  const bool needDeadTime = (z.lastDirection != DIR_NONE && z.lastDirection != dir);
  if (needDeadTime) {
    z.state = WAIT_SWITCH;
    z.pauseStartMs = millis();
    Serial.printf("[%s] WAIT_SWITCH %lu ms before reverse from %s to %s\n",
                  z.name,
                  cfg.switchMs,
                  directionLabel(z.lastDirection),
                  directionLabel(dir));
    appendEventLog(String(z.name) + " wait switch " + String(cfg.switchMs) + "ms before " +
                   String(directionLabel(z.lastDirection)) + " -> " +
                   String(directionLabel(dir)) + " | " + zoneTelemetryForLog(z));
    return;
  }

  beginMoveNow(z);
}

void startOpen(Zone& z, CommandSource source) {
  queueMove(z,
            DIR_OPEN,
            OPENING,
            cfg.moveMs,
            +1,
            source,
            ZCMD_STEP_OPEN);
}

void startClose(Zone& z, CommandSource source) {
  queueMove(z,
            DIR_CLOSE,
            CLOSING,
            cfg.moveMs,
            -1,
            source,
            ZCMD_STEP_CLOSE);
}

void startFullOpen(Zone& z) {
  queueMove(z,
            DIR_OPEN,
            OPENING,
            cfg.fullTravelMs,
            z.maxStep,
            SRC_MANUAL,
            ZCMD_FULL_OPEN);
}

void startFullClose(Zone& z) {
  queueMove(z,
            DIR_CLOSE,
            CLOSING,
            cfg.fullTravelMs,
            -z.maxStep,
            SRC_MANUAL,
            ZCMD_FULL_CLOSE);
}

void startManualExtraClose(Zone& z) {
  queueMove(z,
            DIR_CLOSE,
            EXTRA_CLOSING,
            cfg.extraCloseMs,
            0,
            SRC_MANUAL,
            ZCMD_EXTRA_CLOSE);
}

void startExtraClose(Zone& z, CommandSource source) {
  queueMove(z, DIR_CLOSE, EXTRA_CLOSING, cfg.extraCloseMs, 0, source, ZCMD_EXTRA_CLOSE);
}

void startStartupClose(Zone& z) {
  z.calibrated = false;
  z.positionUncertain = false;
  z.locked = false;
  z.error = false;
  z.currentStep = 0;
  z.targetStep = 0;
  z.extraClosePending = false;
  z.emergencyManualOverride = false;
  z.lastDirection = DIR_NONE;
  resetConfirmCounters(z);
  queueMove(z,
            DIR_CLOSE,
            STARTUP_CLOSING,
            cfg.startupCloseMs,
            0,
            SRC_STARTUP,
            ZCMD_STARTUP_CLOSE);
}

void lockZone(Zone& z) {
  zoneRelaysOff(z);
  z.locked = true;
  z.state = LOCKED;
  z.pendingDirection = DIR_NONE;
  z.pendingState = LOCKED;
  z.activeSource = SRC_NONE;
  z.pendingSource = SRC_NONE;
  z.plannedMoveMs = 0;
  z.pendingStepDelta = 0;
  z.extraClosePending = false;
  z.emergencyManualOverride = false;
  z.currentStep = 0;
  z.targetStep = 0;
  z.lastActionMs = millis();
  resetConfirmCounters(z);
  Serial.printf("[%s] LOCKED after emergency close\n", z.name);
  appendEventLogf("%s locked after emergency close", z.name);
}

void enterZoneError(Zone& z, const char* reason) {
  if (manualMoveInProgress(z)) {
    z.error = true;
    resetConfirmCounters(z);
    Serial.printf("[%s] ERROR latched during MANUAL move: %s | manual pulse allowed to finish\n",
                  z.name,
                  reason);
    appendEventLogf("%s error latched during manual move: %s", z.name, reason);
    return;
  }

  if (preserveAutoExtraCloseOnSensorError(z)) {
    z.error = true;
    resetConfirmCounters(z);
    Serial.printf("[%s] ERROR latched while extra close pending: %s | extra close will continue\n",
                  z.name,
                  reason);
    appendEventLogf("%s error latched while extra close pending: %s | extra close continues",
                    z.name,
                    reason);
    return;
  }

  const bool wasMoving = isMotorState(z.state);
  stopMotor(z);
  z.error = true;
  z.state = ERROR;
  z.pendingDirection = DIR_NONE;
  z.pendingState = ERROR;
  z.activeSource = SRC_NONE;
  z.pendingSource = SRC_NONE;
  z.plannedMoveMs = 0;
  z.pendingStepDelta = 0;
  z.extraClosePending = false;
  if (wasMoving) {
    z.calibrated = false;
  }
  resetConfirmCounters(z);
  Serial.printf("[%s] ERROR: %s", z.name, reason);
  if (wasMoving) {
    Serial.print(" | calibration lost");
  }
  Serial.println();
  appendEventLogf("%s ERROR: %s%s",
                  z.name,
                  reason,
                  wasMoving ? " | calibration lost" : "");
}

void maybeClearZoneError(Zone& z) {
  if (!z.error) {
    return;
  }
  z.error = false;
  if (!z.locked && z.state == ERROR) {
    z.state = IDLE;
  }
  Serial.printf("[%s] DHT recovered, sensor error cleared", z.name);
  if (!z.calibrated) {
    Serial.print(" | zone still not calibrated, use reset if needed");
  }
  Serial.println();
  appendEventLogf("%s DHT recovered, sensor error cleared%s",
                  z.name,
                  z.calibrated ? "" : " | not calibrated");
}

void finishMove(Zone& z, unsigned long now) {
  const ZoneState finishedState = z.state;
  const CommandSource finishedSource = z.activeSource;
  const ZoneCommandKind finishedCommand = z.activeCommandKind;
  const int stepDelta = z.pendingStepDelta;
  const unsigned long finishedElapsedMs = now - z.motorStartMs;
  const unsigned long finishedPlannedMs = commandDurationMs(finishedCommand, z.plannedMoveMs);
  stopMotor(z);
  z.activeSource = SRC_NONE;
  z.activeCommandKind = ZCMD_NONE;
  z.pendingDirection = DIR_NONE;
  z.pendingSource = SRC_NONE;
  z.pendingCommandKind = ZCMD_NONE;
  z.plannedMoveMs = 0;
  z.pendingStepDelta = 0;
  z.lastActionMs = now;
  resetConfirmCounters(z);

  Serial.printf("[%s] FINISH state=%s cmd=%s elapsed=%lu ms planned=%lu ms\n",
                z.name,
                zoneStateLabel(finishedState),
                zoneCommandLabel(finishedCommand),
                finishedElapsedMs,
                finishedPlannedMs);
  appendEventLogf("%s finish %s | cmd=%s | elapsed=%lu | planned=%lu",
                  z.name,
                  zoneStateLabel(finishedState),
                  zoneCommandLabel(finishedCommand),
                  finishedElapsedMs,
                  finishedPlannedMs);

  if (finishedState == STARTUP_CLOSING) {
    z.currentStep = 0;
    z.targetStep = 0;
    z.calibrated = true;
    z.positionUncertain = false;
    z.state = globalEmergency ? LOCKED : IDLE;
    z.locked = globalEmergency;
    z.extraClosePending = false;
    z.pendingState = z.state;
    z.pauseStartMs = now;
    setZoneLastEvent(z, "Крок закривання завершено");
    Serial.printf("%s calibrated closed\n", z.name);
    appendEventLog(String(z.name) + " calibrated closed at startup | " + zoneTelemetryForLog(z));
    return;
  }

  if (finishedState == OPENING) {
    z.currentStep = constrain(z.currentStep + stepDelta, 0, z.maxStep);
    z.pendingState = IDLE;

    if (finishedCommand == ZCMD_FULL_OPEN) {
      z.currentStep = z.maxStep;
      z.positionUncertain = true;
      z.state = z.error ? ERROR : IDLE;
      z.pendingState = z.state;
      setZoneLastEvent(z, "Повне відкриття завершено");
      Serial.printf("[%s] MANUAL FULL OPEN finished, currentStep=%d (approximate)\n", z.name, z.currentStep);
      appendEventLog(String(z.name) + " manual full open finished | " + zoneTelemetryForLog(z));
    } else if (finishedSource == SRC_MANUAL) {
      z.positionUncertain = true;
      z.state = z.error ? ERROR : IDLE;
      z.pendingState = z.state;
      setZoneLastEvent(z, "Крок відкривання завершено");
      Serial.printf("[%s] MANUAL OPEN pulse finished, currentStep=%d (approximate)\n", z.name, z.currentStep);
      appendEventLog(String(z.name) + " manual open finished | " + zoneTelemetryForLog(z));
    } else {
      z.state = WAIT_AFTER_MOVE;
      z.pendingState = WAIT_AFTER_MOVE;
      z.pauseStartMs = now;
      setZoneLastEvent(z, "Крок відкривання завершено");
      Serial.printf("[%s] AUTO OPEN step finished, currentStep=%d\n", z.name, z.currentStep);
      appendEventLog(String(z.name) + " auto open step finished | " + zoneTelemetryForLog(z));
    }
    return;
  }

  if (finishedState == CLOSING) {
    z.currentStep = constrain(z.currentStep + stepDelta, 0, z.maxStep);
    if (z.currentStep == 0 && finishedSource != SRC_MANUAL) {
      z.extraClosePending = (cfg.extraCloseMs > 0);
    }
    z.pendingState = IDLE;

    if (finishedCommand == ZCMD_FULL_CLOSE) {
      z.currentStep = 0;
      z.targetStep = 0;
      z.calibrated = true;
      z.positionUncertain = false;
      z.extraClosePending = false;
      z.state = z.error ? ERROR : IDLE;
      z.pendingState = z.state;
      setZoneLastEvent(z, "Повне закриття завершено");
      Serial.printf("[%s] MANUAL FULL CLOSE finished, currentStep=%d\n", z.name, z.currentStep);
      appendEventLog(String(z.name) + " manual full close finished | " + zoneTelemetryForLog(z));
    } else if (finishedSource == SRC_MANUAL) {
      z.positionUncertain = true;
      z.extraClosePending = false;
      z.state = z.error ? ERROR : IDLE;
      z.pendingState = z.state;
      setZoneLastEvent(z, "Крок закривання завершено");
      Serial.printf("[%s] MANUAL CLOSE pulse finished, currentStep=%d (approximate)\n", z.name, z.currentStep);
      appendEventLog(String(z.name) + " manual close finished | " + zoneTelemetryForLog(z));
    } else if (globalEmergency) {
      z.state = IDLE;
      z.pendingState = IDLE;
      setZoneLastEvent(z, "Аварійне закривання завершено");
      Serial.printf("[%s] CLOSE step finished during emergency, currentStep=%d\n", z.name, z.currentStep);
      appendEventLog(String(z.name) + " emergency close step finished | " + zoneTelemetryForLog(z));
    } else {
      z.state = WAIT_AFTER_MOVE;
      z.pendingState = WAIT_AFTER_MOVE;
      z.pauseStartMs = now;
      setZoneLastEvent(z, "Крок закривання завершено");
      Serial.printf("[%s] AUTO CLOSE step finished, currentStep=%d\n", z.name, z.currentStep);
      appendEventLog(String(z.name) + " auto close step finished | " + zoneTelemetryForLog(z));
    }
    return;
  }

  if (finishedState == EXTRA_CLOSING) {
    z.currentStep = 0;
    z.targetStep = 0;
    z.extraClosePending = false;
    z.pendingState = IDLE;
    z.calibrated = true;
    z.positionUncertain = false;
    setZoneLastEvent(z, "Дотяжка завершена");

    if (globalEmergency) {
      lockZone(z);
    } else if (finishedSource == SRC_MANUAL) {
      z.state = z.error ? ERROR : IDLE;
      Serial.printf("[%s] MANUAL extra close finished\n", z.name);
      appendEventLog(String(z.name) + " manual extra close finished | " + zoneTelemetryForLog(z));
    } else {
      z.state = IDLE;
      Serial.printf("[%s] EXTRA close finished\n", z.name);
      appendEventLog(String(z.name) + " extra close finished | " + zoneTelemetryForLog(z));
    }
    return;
  }
}

void serviceMoveState(Zone& z, unsigned long now) {
  if (z.state == WAIT_SWITCH) {
    if (now - z.pauseStartMs >= cfg.switchMs) {
      beginMoveNow(z);
    }
    return;
  }

  if (!isMotorState(z.state)) {
    return;
  }

  if (now < z.motorStartMs) {
    return;
  }

  const unsigned long activeDurationMs = commandDurationMs(z.activeCommandKind, z.plannedMoveMs);
  if (now - z.motorStartMs >= activeDurationMs) {
    finishMove(z, now);
  }
}

void serviceWaitAfterMove(Zone& z, unsigned long now) {
  if (z.state != WAIT_AFTER_MOVE) {
    return;
  }

  if (globalEmergency) {
    z.state = IDLE;
    return;
  }

  if (now - z.pauseStartMs < cfg.pauseMs) {
    return;
  }

  if (z.extraClosePending) {
    startExtraClose(z, SRC_AUTO);
    return;
  }

  z.state = IDLE;
}

void rejectZoneDhtRead(Zone& z, const char* reason) {
  z.dhtOk = false;
  if (z.dhtFailCount < 255) {
    z.dhtFailCount++;
  }

  Serial.printf("[%s] DHT read rejected: %s (%u/%u)\n",
                z.name,
                reason,
                z.dhtFailCount,
                MAX_DHT_FAILS);

  if (z.dhtFailCount == 1) {
    appendEventLogf("%s DHT rejected: %s", z.name, reason);
  }

  if (z.dhtFailCount >= MAX_DHT_FAILS && !z.error) {
    enterZoneError(z, reason);
  }

  resetConfirmCounters(z);
}

void updateAutoConfirmCounters(Zone& z) {
  if (!autoMode || globalEmergency || z.locked || z.error || !z.dhtOk || z.state != IDLE) {
    resetConfirmCounters(z);
    return;
  }

  z.targetStep = computeTargetStep(z, z.currentTemp);

  const bool openCondition = z.targetStep > z.currentStep;
  const bool closeCondition = z.currentStep > 0 && z.currentTemp <= closeThresholdForStep(z, z.currentStep);

  if (openCondition) {
    if (z.openConfirmCount < 255) {
      z.openConfirmCount++;
    }
  } else {
    z.openConfirmCount = 0;
  }

  if (closeCondition) {
    if (z.closeConfirmCount < 255) {
      z.closeConfirmCount++;
    }
  } else {
    z.closeConfirmCount = 0;
  }
}

void serviceEmergencyZone(Zone& z) {
  if (!globalEmergency) {
    return;
  }

  if (z.emergencyManualOverride) {
    return;
  }

  if (z.state == WAIT_AFTER_MOVE) {
    z.state = IDLE;
  }

  if (z.state == ERROR) {
    z.state = IDLE;
  }

  if (z.state == LOCKED) {
    return;
  }

  if (zoneIsBusy(z)) {
    return;
  }

  if (z.currentStep > 0) {
    startClose(z, SRC_EMERGENCY);
    return;
  }

  if (z.extraClosePending) {
    startExtraClose(z, SRC_EMERGENCY);
    return;
  }

  lockZone(z);
}

void updateZone(Zone& z, unsigned long now) {
  serviceMoveState(z, now);
  serviceWaitAfterMove(z, now);

  if (globalEmergency) {
    serviceEmergencyZone(z);
    return;
  }

  if (z.locked || z.error || !autoMode || !z.dhtOk) {
    return;
  }

  if (z.state != IDLE) {
    return;
  }

  if (z.openConfirmCount >= CONFIRM_READS && z.targetStep > z.currentStep) {
    startOpen(z, SRC_AUTO);
    return;
  }

  if (z.closeConfirmCount >= CONFIRM_READS && z.currentStep > 0 && z.currentTemp <= closeThresholdForStep(z, z.currentStep)) {
    startClose(z, SRC_AUTO);
  }
}

void readZoneDht(Zone& z) {
  const float temp = z.dht->readTemperature();
  const float hum = z.dht->readHumidity();

  if (isnan(temp) || isnan(hum)) {
    rejectZoneDhtRead(z, "DHT returned NAN");
    return;
  }

  if (temp < DHT_MIN_VALID_TEMP_C || temp > DHT_MAX_VALID_TEMP_C) {
    char reason[96];
    snprintf(reason, sizeof(reason), "temperature out of range (%.1fC)", temp);
    rejectZoneDhtRead(z, reason);
    return;
  }

  if (hum < DHT_MIN_VALID_HUM_PCT || hum > DHT_MAX_VALID_HUM_PCT) {
    char reason[96];
    snprintf(reason, sizeof(reason), "humidity out of range (%.1f%%)", hum);
    rejectZoneDhtRead(z, reason);
    return;
  }

  if (!isnan(z.currentTemp) && !isnan(z.currentHum) && z.lastGoodDhtMs > 0) {
    const float elapsedSec = max(1.0f, static_cast<float>(millis() - z.lastGoodDhtMs) / 1000.0f);
    const float allowedTempJump = max(DHT_BASE_MAX_TEMP_JUMP_C, elapsedSec * DHT_TEMP_JUMP_C_PER_SEC);
    const float allowedHumJump = max(DHT_BASE_MAX_HUM_JUMP_PCT, elapsedSec * DHT_HUM_JUMP_PCT_PER_SEC);
    const float tempJump = fabsf(temp - z.currentTemp);
    const float humJump = fabsf(hum - z.currentHum);

    if (tempJump > allowedTempJump) {
      char reason[112];
      snprintf(reason,
               sizeof(reason),
               "temperature jump too large (%.1fC -> %.1fC, delta %.1fC, allowed %.1fC)",
               z.currentTemp,
               temp,
               tempJump,
               allowedTempJump);
      rejectZoneDhtRead(z, reason);
      return;
    }

    if (humJump > allowedHumJump) {
      char reason[112];
      snprintf(reason,
               sizeof(reason),
               "humidity jump too large (%.1f%% -> %.1f%%, delta %.1f%%, allowed %.1f%%)",
               z.currentHum,
               hum,
               humJump,
               allowedHumJump);
      rejectZoneDhtRead(z, reason);
      return;
    }
  }

  const bool hadSensorError = z.error;

  z.currentTemp = temp;
  z.currentHum = hum;
  z.dhtOk = true;
  z.dhtFailCount = 0;
  z.lastGoodDhtMs = millis();
  z.targetStep = computeTargetStep(z, z.currentTemp);

  if (hadSensorError) {
    maybeClearZoneError(z);
  }

  updateAutoConfirmCounters(z);

  Serial.printf("[%s] DHT ok: T=%.1f C, H=%.1f %%, targetStep=%d, openConfirm=%u, closeConfirm=%u\n",
                z.name,
                z.currentTemp,
                z.currentHum,
                z.targetStep,
                z.openConfirmCount,
                z.closeConfirmCount);
}

void readZoneDhts(unsigned long now) {
  if (now - lastDhtReadMs < cfg.dhtReadMs) {
    return;
  }

  lastDhtReadMs = now;
  readZoneDht(zone1);
  readZoneDht(zone2);
}

void readAnalogInputs(unsigned long now) {
  if (now - lastAnalogReadMs < ANALOG_READ_MS) {
    return;
  }

  lastAnalogReadMs = now;

#if !ANALOG_SENSOR_PINS_CONFIRMED
  windRaw = 0;
  rainRaw = 0;
  waterState = HIGH;
  waterLow = false;
  waterKnown = true;
  return;
#endif

  windRaw = analogRead(PIN_WIND_ADC);
  rainRaw = analogRead(PIN_RAIN_ADC);
  waterState = digitalRead(PIN_WATER_LEVEL);

  const bool nowWaterLow = cfg.enableWaterMonitor && (waterState == cfg.waterLowActiveState);
  if (!waterKnown || nowWaterLow != waterLow) {
    waterLow = nowWaterLow;
    waterKnown = true;
    Serial.printf("[WATER] state=%s (%s)\n",
                  waterState == LOW ? "LOW" : "HIGH",
                  waterLow ? "LOW LEVEL WARNING" : "NORMAL");
  }

  if (!globalEmergency) {
    const bool windAlarm = cfg.enableWindAlarm && windRaw > cfg.windAlarmThreshold;
    const bool rainAlarm = cfg.enableRainAlarm && rainRaw > cfg.rainAlarmThreshold;

    if (windAlarm && rainAlarm) {
      triggerEmergency("wind+rain");
    } else if (windAlarm) {
      triggerEmergency("wind");
    } else if (rainAlarm) {
      triggerEmergency("rain");
    }
  }
}

void printZoneStatus(const Zone& z) {
  const ZoneCommandKind commandKind = (z.state == WAIT_SWITCH) ? z.pendingCommandKind : z.activeCommandKind;
  Serial.printf("%s | temp=%.1fC hum=%.1f%% dht=%s fails=%u state=%s step=%d/%d target=%d calibrated=%s positionUncertain=%s locked=%s error=%s lastSource=%s lastDir=%s openRelay=%s closeRelay=%s openConfirm=%u closeConfirm=%u emergencyManualOverride=%s activeCmd=%s timerMs=%lu elapsedMs=%lu remainingMs=%lu\n",
                z.name,
                z.currentTemp,
                z.currentHum,
                z.dhtOk ? "OK" : "FAIL",
                z.dhtFailCount,
                zoneStateLabel(z.state),
                z.currentStep,
                z.maxStep,
                z.targetStep,
                z.calibrated ? "YES" : "NO",
                z.positionUncertain ? "YES" : "NO",
                z.locked ? "YES" : "NO",
                z.error ? "YES" : "NO",
                sourceLabel(z.lastCommandSource),
                directionLabel(z.lastDirection),
                relayLabel(z.openPin),
                relayLabel(z.closePin),
                z.openConfirmCount,
                z.closeConfirmCount,
                z.emergencyManualOverride ? "YES" : "NO",
                zoneCommandLabel(commandKind),
                zoneActiveTimerMs(z),
                zoneActiveElapsedMs(z),
                zoneActiveRemainingMs(z));

  if (z.currentStep > 0) {
    Serial.printf("%s | closeThreshold=%.2fC for currentStep=%d\n",
                  z.name,
                  closeThresholdForStep(z, z.currentStep),
                  z.currentStep);
  }
}

const char* serviceMotorStateLabel(ServiceMotorState state) {
  switch (state) {
    case SMS_WAIT_SWITCH: return "РЕВЕРС";
    case SMS_OPENING:     return "ВІДКРИВАЄ";
    case SMS_CLOSING:     return "ЗАКРИВАЄ";
    default:              return "ПАУЗА";
  }
}

const char* zoneStateUiLabel(const Zone& z) {
  switch (z.state) {
    case STARTUP_CLOSING: return "ІНІТ";
    case OPENING:         return "ВІДКРИВАЄ";
    case CLOSING:         return "ЗАКРИВАЄ";
    case EXTRA_CLOSING:   return "ДОТЯЖКА";
    case WAIT_SWITCH:     return "РЕВЕРС";
    case WAIT_AFTER_MOVE: return "ПАУЗА";
    case ERROR:           return "ПОМИЛКА";
    case LOCKED:          return "LOCKED";
    default:              return "ПАУЗА";
  }
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
  return out;
}

String secondsText(unsigned long ms) {
  return String(ms / 1000.0f, 1);
}

String formatTempCell(float value) {
  if (isnan(value)) {
    return "--";
  }
  return String(value, 1) + " °C";
}

String formatHumCell(float value) {
  if (isnan(value)) {
    return "--";
  }
  return String(value, 1) + " %";
}

String modeLabel() {
  return autoMode ? "АВТО" : "РУЧНИЙ";
}

String alarmStatusLabel() {
  if (!globalEmergency) {
    return "Норма";
  }
  if (strcmp(emergencyReason, "wind+rain") == 0) {
    return "Вітер + дощ";
  }
  if (strcmp(emergencyReason, "wind") == 0) {
    return "Вітер";
  }
  if (strcmp(emergencyReason, "rain") == 0) {
    return "Дощ";
  }
  return String("Аварія: ") + emergencyReason;
}

String windStatusLabel() {
  return cfg.enableWindAlarm ? String("Поріг ADC ") + cfg.windAlarmThreshold : "Контроль вимкнено";
}

String rainStatusLabel() {
  return cfg.enableRainAlarm ? String("Поріг ADC ") + cfg.rainAlarmThreshold : "Контроль вимкнено";
}

String waterStatusLabel() {
  if (!cfg.enableWaterMonitor) {
    return "Контроль вимкнено";
  }
  return waterLow ? "Низький рівень" : "Норма";
}

String waterSignalLabel() {
  return waterState == LOW ? "LOW" : "HIGH";
}

String routerStatusLabel() {
  if (!wifiEnabled) {
    return "вимкнено";
  }
  return routerConnected ? "підключено" : "не підключено";
}

String routerUrlLabel() {
  return routerConnected ? String("http://") + wifiIpLabel() : String("http://0.0.0.0");
}

String apUrlLabel() {
  return fallbackApActive ? String("http://") + WiFi.softAPIP().toString() : String("http://0.0.0.0");
}

String zoneStatusText(const Zone& z) {
  String text;
  text.reserve(360);
  text += z.name;
  text += " | temp=";
  text += isnan(z.currentTemp) ? "nan" : String(z.currentTemp, 1);
  text += "C hum=";
  text += isnan(z.currentHum) ? "nan" : String(z.currentHum, 1);
  text += "% dht=";
  text += z.dhtOk ? "OK" : "FAIL";
  text += " fails=";
  text += String(z.dhtFailCount);
  text += " state=";
  text += zoneStateLabel(z.state);
  text += " step=";
  text += String(z.currentStep);
  text += "/";
  text += String(z.maxStep);
  text += " target=";
  text += String(z.targetStep);
  text += " calibrated=";
  text += z.calibrated ? "YES" : "NO";
  text += " positionUncertain=";
  text += z.positionUncertain ? "YES" : "NO";
  text += " locked=";
  text += z.locked ? "YES" : "NO";
  text += " error=";
  text += z.error ? "YES" : "NO";
  text += " lastSource=";
  text += sourceLabel(z.lastCommandSource);
  text += " lastDir=";
  text += directionLabel(z.lastDirection);
  text += " openRelay=";
  text += relayLabel(z.openPin);
  text += " closeRelay=";
  text += relayLabel(z.closePin);
  text += " openConfirm=";
  text += String(z.openConfirmCount);
  text += " closeConfirm=";
  text += String(z.closeConfirmCount);
  text += " emergencyManualOverride=";
  text += z.emergencyManualOverride ? "YES" : "NO";
  text += "\n";

  if (z.currentStep > 0) {
    text += z.name;
    text += " | closeThreshold=";
    text += String(closeThresholdForStep(z, z.currentStep), 2);
    text += "C for currentStep=";
    text += String(z.currentStep);
    text += "\n";
  }

  return text;
}

String systemStatusText() {
  String text;
  text.reserve(2000);
  text += "========== SYSTEM STATUS ==========\n";
  text += "millis=";
  text += String(millis());
  text += " | bootCounter=";
  text += String(bootCounter);
  text += " | autoMode=";
  text += autoMode ? "ON" : "OFF";
  text += " | emergency=";
  text += globalEmergency ? "YES" : "NO";
  text += " | reason=";
  text += emergencyReason;
  text += " | allowManualDuringEmergency=";
  text += ALLOW_MANUAL_DURING_EMERGENCY ? "YES" : "NO";
  text += "\n";
  text += "windRaw=";
  text += String(windRaw);
  text += " | rainRaw=";
  text += String(rainRaw);
  text += " | waterState=";
  text += waterState == LOW ? "LOW" : "HIGH";
  text += " | waterLow=";
  text += waterLow ? "YES" : "NO";
  text += "\n";
  text += "serviceRelayOpen=";
  text += relayLabel(PIN_SERVICE_OPEN);
  text += " | serviceRelayClose=";
  text += relayLabel(PIN_SERVICE_CLOSE);
  text += "\n";
  text += "=== WIFI ===\n";
  text += "Enabled: ";
  text += wifiEnabled ? "YES" : "NO";
  text += "\nState: ";
  text += wifiStateToString(wifiState);
  text += "\nSSID: ";
  text += strlen(wifiConnectedSsid) > 0 ? wifiConnectedSsid : wifiCredentialSsidAt(0);
  text += "\nIP: ";
  text += wifiIpLabel();
  text += "\nRSSI: ";
  text += routerConnected ? String(wifiLastRssi) + " dBm" : String("n/a");
  text += "\nDisconnect count: ";
  text += String(wifiDisconnectCount);
  text += "\nReconnect success count: ";
  text += String(wifiReconnectSuccessCount);
  text += "\nLast disconnect reason: ";
  text += wifiLastDisconnectReason;
  text += "\nConnected for: ";
  text += routerConnected ? formatElapsedMs(wifiConnectedSinceMs) : String("0s");
  text += "\nLast reconnect attempt: ";
  text += wifiLastConnectAttemptMs ? formatElapsedMs(wifiLastConnectAttemptMs) + " ago" : String("never");
  text += "\nAP SSID: ";
  text += AP_SSID;
  text += "\nAP IP: ";
  text += apUrlLabel();
  text += "\nAP active: ";
  text += fallbackApActive ? "YES" : "NO";
  text += "\nEvent log count: ";
  text += String(eventLogCount);
  text += "/";
  text += String(EVENT_LOG_CAPACITY);
  text += "\n=== TIMERS ===";
  text += "\nmoveMs=";
  text += String(cfg.moveMs);
  text += " | pauseMs=";
  text += String(cfg.pauseMs);
  text += " | switchMs=";
  text += String(cfg.switchMs);
  text += " | extraCloseMs=";
  text += String(cfg.extraCloseMs);
  text += " | fullTravelMs=";
  text += String(cfg.fullTravelMs);
  text += "\n";
  text += zoneStatusText(zone1);
  text += zoneStatusText(zone2);
  text += "===================================\n";
  return text;
}

void printSystemStatus() {
  Serial.println();
  Serial.print(systemStatusText());
  Serial.println();
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  status");
  Serial.println("  auto on");
  Serial.println("  auto off");
  Serial.println("  reset_alarm");
  Serial.println("  z1 open");
  Serial.println("  z1 close");
  Serial.println("  z1 stop");
  Serial.println("  z1 reset");
  Serial.println("  z2 open");
  Serial.println("  z2 close");
  Serial.println("  z2 stop");
  Serial.println("  z2 reset");
  Serial.println("  wifi status");
  Serial.println("  wifi reconnect");
  Serial.println("  wifi off");
  Serial.println("  wifi on");
  Serial.println("  wifi scan");
  Serial.println("  log");
  Serial.println("  log clear");
  Serial.println("  help");
  Serial.println("Web:");
  Serial.printf("  router: %s\n", wifiCredentialSsidAt(0));
  Serial.printf("  open %s\n", routerUrlLabel().c_str());
  Serial.printf("  AP: %s -> %s\n", AP_SSID, apUrlLabel().c_str());
  Serial.println("  events: /events.txt");
}

String htmlHeader(const String& title) {
  String html;
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>";
  html += title;
  html += "</title>";
  html += "<style>";
  html += ":root{color-scheme:dark;}";
  html += "body{font-family:Arial,sans-serif;background:#111827;color:#e5e7eb;margin:0;padding:12px;}";
  html += ".wrap{max-width:1200px;margin:0 auto;display:grid;gap:12px;}";
  html += ".card{background:#1f2937;border:1px solid #374151;border-radius:14px;padding:14px;box-shadow:0 8px 18px rgba(0,0,0,.16);}";
  html += ".tab-pane>.card:first-child{margin-top:0;}.tab-pane .card{margin-top:12px;}";
  html += "h1,h2,h3{margin:0 0 10px 0;}h1{font-size:28px;}h2{font-size:20px;}h3{font-size:18px;}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;}";
  html += ".two{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px;}";
  html += ".three{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px;}";
  html += ".stat{background:#111827;border:1px solid #374151;border-radius:10px;padding:10px;min-height:68px;}";
  html += ".label{font-size:12px;color:#9ca3af;margin-bottom:4px;line-height:1.2;}.value{font-size:22px;font-weight:700;line-height:1.2;word-break:break-word;}";
  html += "form{display:grid;gap:10px;}label{display:grid;gap:6px;font-size:14px;color:#e5e7eb;}input,button,select{font-size:16px;padding:10px;border-radius:10px;border:1px solid #4b5563;box-sizing:border-box;}";
  html += "input,select{background:#0f172a;color:#e5e7eb;}button{background:#2563eb;color:#fff;border:none;cursor:pointer;font-weight:600;min-height:44px;}";
  html += ".warn{background:#c2410c;}.ok{background:#059669;}.violet{background:#7c3aed;}.stop{background:#dc2626;}";
  html += ".actions{display:flex;flex-wrap:wrap;gap:10px;}.actions form{display:inline-block;}";
  html += ".muted{color:#9ca3af;font-size:13px;line-height:1.45;}.small{font-size:12px;color:#9ca3af;line-height:1.45;}";
  html += ".zone-title,.header-line{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;}.zone-title h2{margin-bottom:4px;}";
  html += ".pill{display:inline-block;background:#0f172a;border:1px solid #374151;border-radius:999px;padding:6px 10px;font-size:12px;white-space:nowrap;}";
  html += ".header-pills{display:flex;flex-wrap:wrap;gap:8px;justify-content:flex-end;}.header-pill{display:inline-flex;align-items:center;gap:6px;background:#0f172a;border:1px solid #374151;border-radius:999px;padding:8px 12px;font-size:12px;font-weight:700;}";
  html += ".header-pill.ok{background:#0b3d2d;border-color:#14532d;color:#bbf7d0;}.header-pill.warn{background:#3f1d0a;border-color:#9a3412;color:#fdba74;}";
  html += ".sticky-tabs{position:sticky;top:0;z-index:20;padding:8px;background:rgba(17,24,39,.96);backdrop-filter:blur(10px);border-radius:14px;border:1px solid #374151;display:flex;gap:8px;overflow:auto;}";
  html += ".tab-btn{appearance:none;border:none;background:#0f172a;color:#cbd5e1;padding:10px 14px;border-radius:999px;white-space:nowrap;font-size:14px;font-weight:700;min-height:40px;}";
  html += ".tab-btn.active{background:#2563eb;color:#fff;}.tab-pane{display:none;}.tab-pane.active{display:block;}";
  html += ".status-strip{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;}.mini-stat{background:#111827;border:1px solid #374151;border-radius:10px;padding:9px 10px;display:grid;gap:3px;min-height:58px;}";
  html += ".mini-stat .label{margin:0;}.mini-stat .value{font-size:15px;font-weight:700;}";
  html += ".inline-note{margin-top:10px;padding:10px 12px;border-radius:10px;background:#111827;border:1px solid #374151;font-size:13px;color:#cbd5e1;line-height:1.45;}";
  html += ".zone-main{display:grid;gap:12px;}.zone-meta{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;}.zone-meta .stat:last-child .value{font-size:16px;}";
  html += ".control-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;}.control-grid form{display:block;}.control-grid button{width:100%;height:100%;}.span-all{grid-column:1/-1;}";
  html += ".section-stack{display:grid;gap:12px;}.diag-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:8px;}.diag-grid .value{font-size:18px;}";
  html += ".canvas-wrap{display:grid;gap:12px;}.canvas-card h3{margin-bottom:8px;}";
  html += "canvas{width:100%;height:180px;background:#111827;border:1px solid #374151;border-radius:10px;display:block;}";
  html += ".logbox{margin-top:12px;white-space:pre-wrap;max-height:360px;overflow:auto;background:#111827;border:1px solid #374151;border-radius:12px;padding:12px;font-size:12px;line-height:1.4;}";
  html += "details{background:#1f2937;}details summary{cursor:pointer;list-style:none;font-weight:700;font-size:18px;}details summary::-webkit-details-marker{display:none;}";
  html += "a{color:#93c5fd;}";
  html += "@media (max-width:720px){body{padding:10px;}.card{padding:12px;}.wrap{gap:10px;}h1{font-size:24px;}h2{font-size:19px;}.value{font-size:20px;}.zone-title,.header-line{flex-direction:column;align-items:flex-start;}.header-pills{justify-content:flex-start;}.control-grid{grid-template-columns:repeat(2,minmax(0,1fr));}.span-all{grid-column:1/-1;}.sticky-tabs{top:0;}}";
  html += "</style></head><body><div class='wrap'>";
  return html;
}

String selectYesNo(const String& name, bool value) {
  String html;
  html += "<select name='";
  html += name;
  html += "'>";
  html += "<option value='1'";
  if (value) {
    html += " selected";
  }
  html += ">Так</option>";
  html += "<option value='0'";
  if (!value) {
    html += " selected";
  }
  html += ">Ні</option></select>";
  return html;
}

String buildZoneStatusBlock(uint8_t idx) {
  const Zone& z = *zones[idx];
  const String key = idx == 0 ? "z1" : "z2";
  String html;
  html += "<div class='card'><div class='zone-title'><h2>Зона ";
  html += String(idx + 1);
  html += "</h2><span class='pill' id='";
  html += key;
  html += "_state'>";
  html += zoneStateUiLabel(z);
  html += "</span></div><div class='grid'>";
  html += "<div class='stat'><div class='label'>Температура</div><div class='value' id='";
  html += key;
  html += "_temp'>";
  html += formatTempCell(z.currentTemp);
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Вологість</div><div class='value' id='";
  html += key;
  html += "_hum'>";
  html += formatHumCell(z.currentHum);
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Рівень люка</div><div class='value' id='";
  html += key;
  html += "_level'>";
  html += String(zoneOpenLevelTemp(z), 1) + " °C";
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Поріг закриття</div><div class='value' id='";
  html += key;
  html += "_close'>";
  html += z.currentStep > 0 ? String(closeThresholdForStep(z, z.currentStep), 1) + " °C" : String("--");
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Помилок датчика</div><div class='value' id='";
  html += key;
  html += "_errors'>";
  html += String(z.dhtFailCount);
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Остання дія</div><div class='value' style='font-size:18px' id='";
  html += key;
  html += "_event'>";
  html += z.lastEvent;
  html += "</div></div></div></div>";
  return html;
}

String buildZoneControlsBlock(uint8_t idx) {
  const String key = idx == 0 ? "z1" : "z2";
  String html;
  html += "<div class='card'><h3>Керування Зона ";
  html += String(idx + 1);
  html += "</h3><div class='actions'>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_open'><button>Відкрити крок</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_close'><button>Закрити крок</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_fullopen'><button class='ok'>Повністю відкрити</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_fullclose'><button class='warn'>Повністю закрити</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_extra'><button class='violet'>Дотяжка</button></form>";
  html += "</div></div>";
  return html;
}

String buildZoneConfigBlock(uint8_t idx) {
  const String key = idx == 0 ? "z1" : "z2";
  const ZoneConfig& config = zoneConfigs[idx];
  String html;
  html += "<div class='card'><h3>Параметри Зона ";
  html += String(idx + 1);
  html += "</h3><form method='POST' action='/config'><input type='hidden' name='scope' value='";
  html += key;
  html += "'>";
  html += "<label>Початок відкриття, °C<input name='openStartTemp' type='number' step='0.1' value='" + String(config.openStartTemp, 1) + "'></label>";
  html += "<label>Крок, °C<input name='tempStep' type='number' step='0.1' value='" + String(config.tempStep, 1) + "'></label>";
  html += "<label>Гістерезис, °C<input name='closeHysteresis' type='number' step='0.1' value='" + String(config.closeHysteresis, 1) + "'></label>";
  html += "<label>Макс. температура, °C<input name='maxTemp' type='number' step='0.1' value='" + String(config.maxTemp, 1) + "'></label>";
  html += "<button type='submit'>Зберегти параметри Зона ";
  html += String(idx + 1);
  html += "</button></form></div>";
  return html;
}

String buildServiceMotorBlock() {
  String html;
  html += "<div class='card'><h2>Зашторення</h2><div class='grid'>";
  html += "<div class='stat'><div class='label'>Стан</div><div class='value' id='aux_state'>";
  html += serviceMotorStateLabel(serviceMotor.state);
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Таймер роботи</div><div class='value'>";
  html += secondsText(cfg.serviceMotorMs);
  html += " с</div></div>";
  html += "<div class='stat'><div class='label'>Остання дія</div><div class='value' style='font-size:18px' id='aux_event'>";
  html += serviceMotor.lastEvent;
  html += "</div></div></div><div class='actions'>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='aux_open'><button class='ok'>Відкрити</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='aux_close'><button class='warn'>Закрити</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='aux_stop'><button>Стоп</button></form>";
  html += "</div><div class='small' style='margin-top:10px'>При зміні напрямку мотор спочатку зупиняється, витримує паузу між напрямками і лише потім запускається в інший бік.</div></div>";
  return html;
}

String buildServiceMotorConfigBlock() {
  String html;
  html += "<div class='card'><h3>Зашторення: таймер</h3><form method='POST' action='/config'>";
  html += "<input type='hidden' name='scope' value='serviceMotor'>";
  html += "<label>Тривалість роботи, сек<input name='serviceMotorMs' type='number' step='0.1' value='" + secondsText(cfg.serviceMotorMs) + "'></label>";
  html += "<button type='submit'>Зберегти таймер сервісного мотора</button></form></div>";
  return html;
}

String buildSystemConfigBlock() {
  String html;
  html += "<div class='card'><h2>Параметри системи</h2><form method='POST' action='/config'>";
  html += "<input type='hidden' name='scope' value='global'><div class='three'>";
  html += "<label>Опитування датчиків, сек<input name='dhtReadMs' type='number' step='0.1' value='" + secondsText(cfg.dhtReadMs) + "'></label>";
  html += "<label>Час кроку, сек<input name='moveMs' type='number' step='0.1' value='" + secondsText(cfg.moveMs) + "'></label>";
  html += "<label>Пауза між кроками, сек<input name='pauseMs' type='number' step='0.1' value='" + secondsText(cfg.pauseMs) + "'></label>";
  html += "<label>Початкове закриття, сек<input name='startupCloseMs' type='number' step='0.1' value='" + secondsText(cfg.startupCloseMs) + "'></label>";
  html += "<label>Пауза між напрямками, сек<input name='switchMs' type='number' step='0.1' value='" + secondsText(cfg.switchMs) + "'></label>";
  html += "<label>Дотяжка, сек<input name='extraCloseMs' type='number' step='0.1' value='" + secondsText(cfg.extraCloseMs) + "'></label>";
  html += "<label>Повний хід вручну, сек<input name='fullTravelMs' type='number' step='0.1' value='" + secondsText(cfg.fullTravelMs) + "'></label>";
  html += "<label>Контроль вітру" + selectYesNo("enableWindAlarm", cfg.enableWindAlarm) + "</label>";
  html += "<label>Поріг вітру, ADC<input name='windAlarmThreshold' type='number' step='1' value='" + String(cfg.windAlarmThreshold) + "'></label>";
  html += "<label>Контроль дощу" + selectYesNo("enableRainAlarm", cfg.enableRainAlarm) + "</label>";
  html += "<label>Поріг дощу, ADC<input name='rainAlarmThreshold' type='number' step='1' value='" + String(cfg.rainAlarmThreshold) + "'></label>";
  html += "<label>Контроль води" + selectYesNo("enableWaterMonitor", cfg.enableWaterMonitor) + "</label>";
  html += "</div><button type='submit'>Зберегти системні параметри</button></form>";
  html += "<div class='small' style='margin-top:10px'>Пороги wind/rain треба відкалібрувати по реальних значеннях Serial Monitor.</div></div>";
  return html;
}

String buildChartsBlock() {
  String html;
  html += "<div class='card'><h2>Графіки температур</h2><div class='two'>";
  html += "<div><h3>Зона 1</h3><canvas id='z1Chart' width='520' height='220'></canvas></div>";
  html += "<div><h3>Зона 2</h3><canvas id='z2Chart' width='520' height='220'></canvas></div>";
  html += "</div><div class='small' style='margin-top:10px'>Останні 120 вимірів із моменту відкриття сторінки.</div></div>";
  return html;
}

String buildWiFiBlock() {
  String html;
  html += "<div class='card'><h2>Wi-Fi роутер</h2><form method='POST' action='/wifi'><div class='three'>";
  html += "<label>SSID роутера<input name='ssid' type='text' value='" + routerSsid + "'></label>";
  html += "<label>Пароль<input name='pass' type='password' value='' placeholder='enter password to update'></label>";
  html += "<div class='stat'><div class='label'>Статус роутера</div><div class='value' style='font-size:18px'>";
  html += routerStatusLabel();
  html += "<br><span class='small'>";
  html += routerUrlLabel();
  html += "<br>State: ";
  html += wifiStateToString(wifiState);
  html += "<br>RSSI: ";
  html += routerConnected ? String(wifiLastRssi) + " dBm" : String("n/a");
  html += "<br>Disconnects: ";
  html += String(wifiDisconnectCount);
  html += "<br>Reconnects: ";
  html += String(wifiReconnectSuccessCount);
  html += "</span></div></div>";
  html += "</div><button type='submit'>Зберегти Wi-Fi</button></form>";
  html += "<div class='actions' style='margin-top:10px'><form method='POST' action='/control'><input type='hidden' name='cmd' value='clearwifi'><button class='warn'>Очистити Wi-Fi</button></form></div></div>";
  return html;
}

String buildHeaderBlock() {
  String html;
  html += "<div class='card'><div class='header-line'><div><h1>Greenhouse 1 / 2 зони</h1>";
  html += "<div class='muted'>IP адреса: <b id='hdr_ip'>";
  html += wifiIpLabel();
  html += "</b></div>";
  html += "<div class='muted'>Роутер: <b id='hdr_ssid'>";
  html += wifiCredentialSsidAt(0);
  html += "</b> | версія: <b id='diag_fw'>";
  html += FW_VERSION;
  html += "</b></div></div><div class='header-pills'><span class='header-pill ";
  html += routerConnected ? "ok" : "warn";
  html += "' id='hdr_wifi'>";
  html += routerConnected ? "Wi-Fi OK" : "Wi-Fi offline";
  html += "</span><span class='header-pill'>AP: ";
  html += AP_SSID;
  html += "</span><span class='header-pill'>Waveshare S3 test port";
  html += "</span></div></div></div>";
  return html;
}

String buildTabsNav() {
  String html;
  html += "<div class='sticky-tabs' id='tabs'>";
  html += "<button class='tab-btn active' type='button' data-tab='home'>Головна</button>";
  html += "<button class='tab-btn' type='button' data-tab='settings'>Налаштування</button>";
  html += "<button class='tab-btn' type='button' data-tab='charts'>Графіки</button>";
  html += "<button class='tab-btn' type='button' data-tab='sensors'>Датчики</button>";
  html += "<button class='tab-btn' type='button' data-tab='wifi'>Wi-Fi</button>";
  html += "</div>";
  return html;
}

String buildCompactStatusBlock() {
  String html;
  html += "<div class='card'><div class='status-strip'>";
  html += "<div class='mini-stat'><div class='label'>Режим</div><div class='value' id='mode'>" + modeLabel() + "</div></div>";
  html += "<div class='mini-stat'><div class='label'>Аварія</div><div class='value' id='alarm'>" + alarmStatusLabel() + "</div></div>";
  html += "<div class='mini-stat'><div class='label'>Вітер</div><div class='value' id='wind'>" + windStatusLabel() + "</div></div>";
  html += "<div class='mini-stat'><div class='label'>Дощ</div><div class='value' id='rain'>" + rainStatusLabel() + "</div></div>";
  html += "<div class='mini-stat'><div class='label'>Вода</div><div class='value' id='water'>" + waterStatusLabel() + "</div></div>";
  html += "</div><div class='inline-note'>Останнє повідомлення: <span id='msg'>";
  html += String(lastActionMessage);
  html += "</span></div></div>";
  return html;
}

String buildZoneMainBlock(uint8_t idx) {
  const Zone& z = *zones[idx];
  const ZoneConfig& config = zoneConfigs[idx];
  const String key = idx == 0 ? "z1" : "z2";
  String html;
  html += "<div class='card zone-main'><div class='zone-title'><div><h2>Зона ";
  html += String(idx + 1);
  html += "</h2><div class='small'>Основне щоденне керування</div></div><span class='pill' id='";
  html += key;
  html += "_state'>";
  html += zoneStateUiLabel(z);
  html += "</span></div><div class='zone-meta'>";
  html += "<div class='stat'><div class='label'>Температура</div><div class='value' id='" + key + "_temp'>" + formatTempCell(z.currentTemp) + "</div></div>";
  html += "<div class='stat'><div class='label'>Вологість</div><div class='value' id='" + key + "_hum'>" + formatHumCell(z.currentHum) + "</div></div>";
  html += "<div class='stat'><div class='label'>Рівень люка</div><div class='value' id='" + key + "_level'>" + String(zoneOpenLevelTemp(z), 1) + " °C</div></div>";
  html += "<div class='stat'><div class='label'>Поріг відкриття</div><div class='value'>" + String(config.openStartTemp, 1) + " °C</div></div>";
  html += "<div class='stat'><div class='label'>Помилки датчика</div><div class='value' id='" + key + "_errors'>" + String(z.dhtFailCount) + "</div></div>";
  html += "<div class='stat'><div class='label'>Остання дія</div><div class='value' style='font-size:16px' id='" + key + "_event'>" + z.lastEvent + "</div></div>";
  html += "</div><div class='control-grid'>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_open'><button>Відкрити крок</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_close'><button>Закрити крок</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_fullopen'><button class='ok'>Повністю відкрити</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_fullclose'><button class='warn'>Повністю закрити</button></form>";
  html += "<form class='span-all' method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_extra'><button class='violet'>Дотяжка</button></form>";
  html += "<form class='span-all' method='POST' action='/control' onsubmit=\"return confirm('Скидати позицію тільки якщо люк фізично закритий. Продовжити?')\"><input type='hidden' name='cmd' value='" + key + "_reset'><button class='violet'>Скинути позицію</button></form>";
  html += "</div></div>";
  return html;
}

String buildChartsTabBlock() {
  String html;
  html += "<div class='card'><h2>Графіки температур</h2><div class='canvas-wrap'>";
  html += "<div class='canvas-card'><h3>Зона 1</h3><canvas id='z1Chart' width='520' height='180'></canvas></div>";
  html += "<div class='canvas-card'><h3>Зона 2</h3><canvas id='z2Chart' width='520' height='180'></canvas></div>";
  html += "</div><div class='small' style='margin-top:10px'>Останні 120 вимірів із моменту відкриття сторінки.</div></div>";
  return html;
}

String buildDiagnosticsBlock() {
  String html;
  html += "<div class='card'><h2>Датчики та діагностика</h2><div class='diag-grid'>";
  html += "<div class='stat'><div class='label'>Wind ADC</div><div class='value' id='wind_raw'>" + String(windRaw) + "</div></div>";
  html += "<div class='stat'><div class='label'>Rain ADC</div><div class='value' id='rain_raw'>" + String(rainRaw) + "</div></div>";
  html += "<div class='stat'><div class='label'>Water signal</div><div class='value' id='water_raw'>" + waterSignalLabel() + "</div></div>";
  html += "<div class='stat'><div class='label'>Помилки Зона 1</div><div class='value' id='diag_z1_errors'>" + String(zone1.dhtFailCount) + "</div></div>";
  html += "<div class='stat'><div class='label'>Помилки Зона 2</div><div class='value' id='diag_z2_errors'>" + String(zone2.dhtFailCount) + "</div></div>";
  html += "<div class='stat'><div class='label'>Last reboot reason</div><div class='value' style='font-size:16px' id='diag_reset'>" + String(resetReasonLabel(esp_reset_reason())) + "</div></div>";
  html += "<div class='stat'><div class='label'>Uptime ESP32</div><div class='value' style='font-size:16px' id='diag_uptime' data-ms='" + String(millis()) + "'>" + formatElapsedMs(millis()) + "</div></div>";
  html += "<div class='stat'><div class='label'>Last disconnect</div><div class='value' style='font-size:16px' id='diag_disconnect'>" + String(wifiLastDisconnectReason) + "</div></div>";
  html += "<div class='stat'><div class='label'>Wi-Fi RSSI</div><div class='value' style='font-size:16px' id='diag_rssi'>" + (routerConnected ? String(wifiLastRssi) + " dBm" : String("n/a")) + "</div></div>";
  html += "<div class='stat'><div class='label'>Wi-Fi state</div><div class='value' style='font-size:16px' id='diag_wifi_state'>" + String(wifiStateToString(wifiState)) + "</div></div>";
  html += "<div class='stat'><div class='label'>Boot counter</div><div class='value' style='font-size:16px' id='diag_boot'>" + String(bootCounter) + "</div></div>";
  html += "</div><div class='small' style='margin-top:10px'>Показання сирих ADC корисні для калібровки порогів вітру та дощу.</div></div>";
  html += "<div class='card'><h2>Журнал подій</h2><div class='small'>Останні важливі події збережені у flash. Після демонтажу ESP їх можна подивитися командою log.</div><pre id='elog' class='logbox'>";
  html += eventLogText(EVENT_LOG_WEB_LIMIT);
  html += "</pre><div class='small' style='margin-top:10px'><a href='/events.txt' target='_blank'>Відкрити весь журнал</a></div></div>";
  return html;
}

String selectYesNoUi(const String& name, bool value) {
  String html;
  html += "<select name='";
  html += name;
  html += "'>";
  html += "<option value='1'";
  if (value) {
    html += " selected";
  }
  html += ">Так</option>";
  html += "<option value='0'";
  if (!value) {
    html += " selected";
  }
  html += ">Ні</option></select>";
  return html;
}

String buildHeaderBlockUi() {
  String html;
  html += "<div class='card'><div class='header-line'><div><h1>Greenhouse 1 / 2 зони</h1>";
  html += "<div class='muted'>IP адреса: <b id='hdr_ip'>";
  html += wifiIpLabel();
  html += "</b></div>";
  html += "<div class='muted'>Роутер: <b id='hdr_ssid'>";
  html += strlen(wifiConnectedSsid) > 0 ? String(wifiConnectedSsid) : String(wifiCredentialSsidAt(0));
  html += "</b> | версія: <b id='diag_fw'>";
  html += FW_VERSION;
  html += "</b></div></div><div class='header-pills'><span class='header-pill ";
  html += routerConnected ? "ok" : "warn";
  html += "' id='hdr_wifi'>";
  html += routerConnected ? "Wi-Fi OK" : "Wi-Fi offline";
  html += "</span><span class='header-pill'>AP: ";
  html += AP_SSID;
  html += "</span><span class='header-pill'>Waveshare S3 test port";
  html += "</span></div></div></div>";
  return html;
}

String buildTabsNavUi() {
  String html;
  html += "<div class='sticky-tabs' id='tabs'>";
  html += "<button class='tab-btn active' type='button' data-tab='home'>Головна</button>";
  html += "<button class='tab-btn' type='button' data-tab='settings'>Налаштування</button>";
  html += "<button class='tab-btn' type='button' data-tab='charts'>Графіки</button>";
  html += "<button class='tab-btn' type='button' data-tab='sensors'>Датчики</button>";
  html += "<button class='tab-btn' type='button' data-tab='wifi'>Wi-Fi</button>";
  html += "</div>";
  return html;
}

String buildZoneMainBlockUi(uint8_t idx) {
  const Zone& z = *zones[idx];
  const ZoneConfig& config = zoneConfigs[idx];
  const String key = idx == 0 ? "z1" : "z2";
  String html;
  html += "<div class='card zone-main'><div class='zone-title'><div><h2>Зона ";
  html += String(idx + 1);
  html += "</h2><div class='small'>Щоденне керування зоною</div></div><span class='pill' id='";
  html += key;
  html += "_state'>";
  html += zoneStateUiLabel(z);
  html += "</span></div><div class='zone-meta'>";
  html += "<div class='stat'><div class='label'>Температура</div><div class='value' id='";
  html += key;
  html += "_temp'>";
  html += formatTempCell(z.currentTemp);
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Вологість</div><div class='value' id='";
  html += key;
  html += "_hum'>";
  html += formatHumCell(z.currentHum);
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Рівень люка</div><div class='value' id='";
  html += key;
  html += "_level'>";
  html += String(zoneOpenLevelTemp(z), 1) + " °C";
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Поріг відкриття</div><div class='value'>";
  html += String(config.openStartTemp, 1) + " °C";
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Помилки датчика</div><div class='value' id='";
  html += key;
  html += "_errors'>";
  html += String(z.dhtFailCount);
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Остання дія</div><div class='value' style='font-size:16px' id='";
  html += key;
  html += "_event'>";
  html += z.lastEvent;
  html += "</div></div></div><div class='control-grid'>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_open'><button>Відкрити крок</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_close'><button>Закрити крок</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_fullopen'><button class='ok'>Повністю відкрити</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_fullclose'><button class='warn'>Повністю закрити</button></form>";
  html += "<form class='span-all' method='POST' action='/control'><input type='hidden' name='cmd' value='" + key + "_extra'><button class='violet'>Дотяжка</button></form>";
  html += "<form class='span-all' method='POST' action='/control' onsubmit=\"return confirm('Скидати позицію тільки якщо люк фізично закритий. Продовжити?')\"><input type='hidden' name='cmd' value='" + key + "_reset'><button class='violet'>Скинути позицію</button></form>";
  html += "</div></div>";
  return html;
}

String buildGlobalControlsBlockUi() {
  String html;
  html += "<div class='card'><h2>Загальне керування</h2><div class='control-grid'>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='auto'><button class='ok'>Авто режим</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='manual'><button class='warn'>Ручний режим</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='stop'><button class='stop'>Стоп</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='resetalarm'><button class='violet'>Скинути аварію</button></form>";
  html += "</div><div class='small' style='margin-top:10px'>Стоп зупиняє обидві зони в будь-який момент. Скидання аварії розблоковує роботу після дощу або вітру.</div></div>";
  return html;
}

String buildServiceMotorBlockUi() {
  String html;
  html += "<div class='card'><h2>Зашторення</h2><div class='zone-meta'>";
  html += "<div class='stat'><div class='label'>Стан</div><div class='value' id='aux_state'>";
  html += serviceMotorStateLabel(serviceMotor.state);
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Таймер роботи</div><div class='value'>";
  html += secondsText(cfg.serviceMotorMs);
  html += " с</div></div>";
  html += "<div class='stat'><div class='label'>Остання дія</div><div class='value' style='font-size:16px' id='aux_event'>";
  html += serviceMotor.lastEvent;
  html += "</div></div></div><div class='control-grid'>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='aux_open'><button class='ok'>Відкрити</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='aux_close'><button class='warn'>Закрити</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='aux_stop'><button>Стоп</button></form>";
  html += "</div></div>";
  return html;
}

String buildServiceMotorConfigBlockUi() {
  String html;
  html += "<div class='card'><h2>Зашторення: таймер</h2><form method='POST' action='/config'>";
  html += "<input type='hidden' name='scope' value='serviceMotor'>";
  html += "<label>Тривалість роботи, сек<input name='serviceMotorMs' type='number' step='0.1' value='";
  html += secondsText(cfg.serviceMotorMs);
  html += "'></label><button type='submit'>Зберегти таймер сервісного мотора</button></form></div>";
  return html;
}

String buildSystemStatusAccordionUi() {
  String html;
  html += "<details class='card'><summary>Системний статус</summary><div class='diag-grid' style='margin-top:12px'>";
  html += "<div class='stat'><div class='label'>Режим</div><div class='value' style='font-size:16px' id='mode'>";
  html += modeLabel();
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Аварія</div><div class='value' style='font-size:16px' id='alarm'>";
  html += alarmStatusLabel();
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Вітер</div><div class='value' style='font-size:16px' id='wind'>";
  html += windStatusLabel();
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Дощ</div><div class='value' style='font-size:16px' id='rain'>";
  html += rainStatusLabel();
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Вода</div><div class='value' style='font-size:16px' id='water'>";
  html += waterStatusLabel();
  html += "</div></div>";
  html += "<div class='stat'><div class='label'>Останнє повідомлення</div><div class='value' style='font-size:16px' id='msg'>";
  html += String(lastActionMessage);
  html += "</div></div></div></details>";
  return html;
}

String buildZoneConfigBlockUi(uint8_t idx) {
  const String key = idx == 0 ? "z1" : "z2";
  const ZoneConfig& config = zoneConfigs[idx];
  String html;
  html += "<div class='card'><h2>Параметри Зона ";
  html += String(idx + 1);
  html += "</h2><form method='POST' action='/config'><input type='hidden' name='scope' value='";
  html += key;
  html += "'>";
  html += "<label>Початок відкриття, °C<input name='openStartTemp' type='number' step='0.1' value='" + String(config.openStartTemp, 1) + "'></label>";
  html += "<label>Крок, °C<input name='tempStep' type='number' step='0.1' value='" + String(config.tempStep, 1) + "'></label>";
  html += "<label>Гістерезис, °C<input name='closeHysteresis' type='number' step='0.1' value='" + String(config.closeHysteresis, 1) + "'></label>";
  html += "<label>Макс. температура, °C<input name='maxTemp' type='number' step='0.1' value='" + String(config.maxTemp, 1) + "'></label>";
  html += "<button type='submit'>Зберегти параметри Зона ";
  html += String(idx + 1);
  html += "</button></form></div>";
  return html;
}

String buildSystemConfigBlockUi() {
  String html;
  html += "<div class='card'><h2>Параметри системи</h2><form method='POST' action='/config'>";
  html += "<input type='hidden' name='scope' value='global'><div class='three'>";
  html += "<label>Опитування датчиків, сек<input name='dhtReadMs' type='number' step='0.1' value='" + secondsText(cfg.dhtReadMs) + "'></label>";
  html += "<label>Час кроку, сек<input name='moveMs' type='number' step='0.1' value='" + secondsText(cfg.moveMs) + "'></label>";
  html += "<label>Пауза між кроками, сек<input name='pauseMs' type='number' step='0.1' value='" + secondsText(cfg.pauseMs) + "'></label>";
  html += "<label>Початкове закриття, сек<input name='startupCloseMs' type='number' step='0.1' value='" + secondsText(cfg.startupCloseMs) + "'></label>";
  html += "<label>Пауза між напрямками, сек<input name='switchMs' type='number' step='0.1' value='" + secondsText(cfg.switchMs) + "'></label>";
  html += "<label>Дотяжка, сек<input name='extraCloseMs' type='number' step='0.1' value='" + secondsText(cfg.extraCloseMs) + "'></label>";
  html += "<label>Повний хід вручну, сек<input name='fullTravelMs' type='number' step='0.1' value='" + secondsText(cfg.fullTravelMs) + "'></label>";
  html += "<label>Контроль вітру" + selectYesNoUi("enableWindAlarm", cfg.enableWindAlarm) + "</label>";
  html += "<label>Поріг вітру, ADC<input name='windAlarmThreshold' type='number' step='1' value='" + String(cfg.windAlarmThreshold) + "'></label>";
  html += "<label>Контроль дощу" + selectYesNoUi("enableRainAlarm", cfg.enableRainAlarm) + "</label>";
  html += "<label>Поріг дощу, ADC<input name='rainAlarmThreshold' type='number' step='1' value='" + String(cfg.rainAlarmThreshold) + "'></label>";
  html += "<label>Контроль води" + selectYesNoUi("enableWaterMonitor", cfg.enableWaterMonitor) + "</label>";
  html += "</div><button type='submit'>Зберегти системні параметри</button></form>";
  html += "<div class='small' style='margin-top:10px'>Пороги wind/rain треба відкалібрувати по реальних значеннях Serial Monitor.</div></div>";
  return html;
}

String buildChartsTabBlockUi() {
  String html;
  html += "<div class='card'><h2>Графіки температур</h2><div class='canvas-wrap'>";
  html += "<div class='canvas-card'><h3>Зона 1</h3><canvas id='z1Chart' width='520' height='180'></canvas></div>";
  html += "<div class='canvas-card'><h3>Зона 2</h3><canvas id='z2Chart' width='520' height='180'></canvas></div>";
  html += "</div><div class='small' style='margin-top:10px'>Останні 120 вимірів із моменту відкриття сторінки.</div></div>";
  return html;
}

String buildDiagnosticsBlockUi() {
  String html;
  html += "<div class='card'><h2>Датчики та діагностика</h2><div class='diag-grid'>";
  html += "<div class='stat'><div class='label'>Wind ADC</div><div class='value' id='wind_raw'>" + String(windRaw) + "</div></div>";
  html += "<div class='stat'><div class='label'>Rain ADC</div><div class='value' id='rain_raw'>" + String(rainRaw) + "</div></div>";
  html += "<div class='stat'><div class='label'>Water signal</div><div class='value' id='water_raw'>" + waterSignalLabel() + "</div></div>";
  html += "<div class='stat'><div class='label'>Помилки Зона 1</div><div class='value' id='diag_z1_errors'>" + String(zone1.dhtFailCount) + "</div></div>";
  html += "<div class='stat'><div class='label'>Помилки Зона 2</div><div class='value' id='diag_z2_errors'>" + String(zone2.dhtFailCount) + "</div></div>";
  html += "<div class='stat'><div class='label'>Last reboot reason</div><div class='value' style='font-size:16px' id='diag_reset'>" + String(resetReasonLabel(esp_reset_reason())) + "</div></div>";
  html += "<div class='stat'><div class='label'>Uptime ESP32</div><div class='value' style='font-size:16px' id='diag_uptime'>" + formatElapsedMs(millis()) + "</div></div>";
  html += "<div class='stat'><div class='label'>Last disconnect</div><div class='value' style='font-size:16px' id='diag_disconnect'>" + String(wifiLastDisconnectReason) + "</div></div>";
  html += "<div class='stat'><div class='label'>Wi-Fi RSSI</div><div class='value' style='font-size:16px' id='diag_rssi'>" + (routerConnected ? String(wifiLastRssi) + " dBm" : String("n/a")) + "</div></div>";
  html += "<div class='stat'><div class='label'>Wi-Fi state</div><div class='value' style='font-size:16px' id='diag_wifi_state'>" + String(wifiStateToString(wifiState)) + "</div></div>";
  html += "<div class='stat'><div class='label'>Boot counter</div><div class='value' style='font-size:16px' id='diag_boot'>" + String(bootCounter) + "</div></div>";
  html += "</div><div class='small' style='margin-top:10px'>Показання сирих ADC корисні для калібровки порогів вітру та дощу.</div></div>";
  html += "<div class='card'><h2>Журнал подій</h2><div class='small'>Останні важливі події збережені у flash. Після демонтажу ESP їх можна подивитися командою log.</div><pre id='elog' class='logbox'>";
  html += eventLogText(EVENT_LOG_WEB_LIMIT);
  html += "</pre><div class='small' style='margin-top:10px'><a href='/events.txt' target='_blank'>Відкрити весь журнал</a></div></div>";
  return html;
}

String buildWiFiBlockUi() {
  String html;
  html += "<div class='card'><h2>Wi-Fi</h2><form method='POST' action='/wifi'><div class='three'>";
  html += "<label>SSID роутера<input name='ssid' type='text' value='" + routerSsid + "'></label>";
  html += "<label>Пароль<input name='pass' type='password' value='' placeholder='enter password to update'></label>";
  html += "<div class='stat'><div class='label'>Статус роутера</div><div class='value' style='font-size:18px'>";
  html += routerStatusLabel();
  html += "<br><span class='small'>";
  html += routerUrlLabel();
  html += "<br>State: ";
  html += wifiStateToString(wifiState);
  html += "<br>RSSI: ";
  html += routerConnected ? String(wifiLastRssi) + " dBm" : String("n/a");
  html += "<br>Disconnects: ";
  html += String(wifiDisconnectCount);
  html += "<br>Reconnects: ";
  html += String(wifiReconnectSuccessCount);
  html += "</span></div></div></div><button type='submit'>Зберегти Wi-Fi</button></form>";
  html += "<div class='actions' style='margin-top:10px'><form method='POST' action='/control'><input type='hidden' name='cmd' value='clearwifi'><button class='warn'>Очистити Wi-Fi</button></form></div></div>";
  return html;
}

String webPageHtml() {
  {
    String html = htmlHeader("Greenhouse 1 / 2 зони");
    html.reserve(26000);

    html += buildHeaderBlockUi();
    html += buildTabsNavUi();
    html += "<div class='tab-pane active' data-tab='home'>";
    html += buildZoneMainBlockUi(0);
    html += buildZoneMainBlockUi(1);
    html += buildGlobalControlsBlockUi();
    html += buildServiceMotorBlockUi();
    html += buildServiceMotorConfigBlockUi();
    html += buildSystemStatusAccordionUi();
    html += "</div>";
    html += "<div class='tab-pane' data-tab='settings'><div class='section-stack'>";
    html += buildZoneConfigBlockUi(0);
    html += buildZoneConfigBlockUi(1);
    html += buildSystemConfigBlockUi();
    html += "</div></div>";
    html += "<div class='tab-pane' data-tab='charts'>";
    html += buildChartsTabBlockUi();
    html += "</div>";
    html += "<div class='tab-pane' data-tab='sensors'><div class='section-stack'>";
    html += buildDiagnosticsBlockUi();
    html += "</div></div>";
    html += "<div class='tab-pane' data-tab='wifi'><div class='section-stack'>";
    html += buildWiFiBlockUi();
    html += "</div></div>";

    html += "<script>";
    html += "const z1Points=[];const z2Points=[];";
    html += "function activateTab(tab){document.querySelectorAll('.tab-btn').forEach(b=>b.classList.toggle('active',b.dataset.tab===tab));document.querySelectorAll('.tab-pane').forEach(p=>p.classList.toggle('active',p.dataset.tab===tab));localStorage.setItem('gh-tab',tab);if(history.replaceState){history.replaceState(null,'','#'+tab);}else{location.hash=tab;}}";
    html += "function initTabs(){const saved=(location.hash||'#'+(localStorage.getItem('gh-tab')||'home')).replace('#','');activateTab(saved||'home');document.querySelectorAll('.tab-btn').forEach(btn=>btn.addEventListener('click',()=>activateTab(btn.dataset.tab)));}";
    html += "function bindAjaxForms(){document.querySelectorAll(\"form[action='/control'],form[action='/config'],form[action='/wifi']\").forEach(form=>{form.addEventListener('submit',async ev=>{ev.preventDefault();const btn=form.querySelector(\"button[type='submit'],button:not([type])\");const prev=btn?btn.textContent:'';if(btn){btn.disabled=true;btn.textContent='...';}try{await fetch(form.action,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:new URLSearchParams(new FormData(form)).toString()});await poll();await pollLog();}catch(e){}finally{if(btn){btn.disabled=false;btn.textContent=prev;}}});});}";
    html += "function addPoint(arr,v){if(v===null||Number.isNaN(v))return;arr.push(v);if(arr.length>120)arr.shift();}";
    html += "function drawChart(id,points,color,emptyText){const c=document.getElementById(id);if(!c)return;const x=c.getContext('2d');const w=c.width,h=c.height;x.clearRect(0,0,w,h);x.fillStyle='#111827';x.fillRect(0,0,w,h);x.strokeStyle='#374151';for(let i=0;i<5;i++){const y=20+i*(h-40)/4;x.beginPath();x.moveTo(40,y);x.lineTo(w-10,y);x.stroke();}if(points.length===0){x.fillStyle='#9ca3af';x.font='14px Arial';x.fillText(emptyText,50,h/2);return;}let min=Math.min(...points);let max=Math.max(...points);if(max-min<1){min-=0.5;max+=0.5;}x.fillStyle='#9ca3af';x.font='12px Arial';x.fillText(max.toFixed(1)+'\\u00B0C',6,24);x.fillText(min.toFixed(1)+'\\u00B0C',6,h-12);x.strokeStyle=color;x.lineWidth=2;x.beginPath();points.forEach((p,i)=>{const px=40+(i*(w-55))/Math.max(points.length-1,1);const py=20+(max-p)*(h-40)/Math.max(max-min,0.1);if(i===0)x.moveTo(px,py);else x.lineTo(px,py);});x.stroke();}";
    html += "function renderCharts(){drawChart('z1Chart',z1Points,'#22c55e','\\u0429\\u0435 \\u043d\\u0435\\u043c\\u0430\\u0454 \\u0434\\u0430\\u043d\\u0438\\u0445');drawChart('z2Chart',z2Points,'#60a5fa','\\u0429\\u0435 \\u043d\\u0435\\u043c\\u0430\\u0454 \\u0434\\u0430\\u043d\\u0438\\u0445');}";
    html += "function setText(id,value){const el=document.getElementById(id);if(el)el.textContent=value;}";
    html += "async function poll(){try{const r=await fetch('/status');const s=await r.json();setText('mode',s.mode);setText('alarm',s.alarm);setText('wind',s.wind);setText('rain',s.rain);setText('water',s.water);setText('msg',s.message);setText('wind_raw',s.windRaw);setText('rain_raw',s.rainRaw);setText('water_raw',s.waterRaw);setText('aux_state',s.auxState);setText('aux_event',s.auxEvent);setText('z1_state',s.z1State);setText('z1_temp',s.z1Temp);setText('z1_hum',s.z1Hum);setText('z1_level',s.z1Level);setText('z1_errors',s.z1Errors);setText('z1_event',s.z1Event);setText('z2_state',s.z2State);setText('z2_temp',s.z2Temp);setText('z2_hum',s.z2Hum);setText('z2_level',s.z2Level);setText('z2_errors',s.z2Errors);setText('z2_event',s.z2Event);setText('hdr_ip',s.routerIp);setText('hdr_ssid',s.routerSsid);setText('hdr_wifi',s.routerBanner);const wifiBadge=document.getElementById('hdr_wifi');if(wifiBadge){wifiBadge.classList.toggle('ok',s.routerConnected);wifiBadge.classList.toggle('warn',!s.routerConnected);}setText('diag_z1_errors',s.z1Errors);setText('diag_z2_errors',s.z2Errors);setText('diag_disconnect',s.wifiLastDisconnect);setText('diag_rssi',s.wifiRssi);setText('diag_wifi_state',s.wifiState);setText('diag_boot',s.bootCounter);setText('diag_reset',s.resetReason);setText('diag_uptime',s.uptime);addPoint(z1Points,s.z1TempRaw);addPoint(z2Points,s.z2TempRaw);renderCharts();}catch(e){}}";
    html += "async function pollLog(){try{const r=await fetch('/events.txt?limit=12');const el=document.getElementById('elog');if(el)el.textContent=await r.text();}catch(e){}}";
    html += "initTabs();bindAjaxForms();poll();pollLog();setInterval(poll,2000);setInterval(pollLog,5000);";
    html += "</script></div></body></html>";
    return html;
  }
  String html = htmlHeader("Greenhouse 1 / 2 зони");
  html.reserve(26000);

  html += buildHeaderBlock();
  html += buildTabsNav();
  html += "<div class='tab-pane active' data-tab='home'>";
  html += buildCompactStatusBlock();
  html += buildZoneMainBlock(0);
  html += buildZoneMainBlock(1);
  html += "<div class='card'><h2>Загальне керування</h2><div class='control-grid'>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='auto'><button class='ok'>Авто режим</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='manual'><button class='warn'>Ручний режим</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='stop'><button class='stop'>Стоп</button></form>";
  html += "<form method='POST' action='/control'><input type='hidden' name='cmd' value='resetalarm'><button class='violet'>Скинути аварію</button></form>";
  html += "</div><div class='small' style='margin-top:10px'>Стоп зупиняє обидві зони в будь-який момент. Скидання аварії розблоковує роботу після дощу/вітру.</div></div>";
  html += buildServiceMotorBlock();
  html += buildServiceMotorConfigBlock();
  html += "</div>";
  html += "<div class='tab-pane' data-tab='settings'><div class='section-stack'>";
  html += buildZoneConfigBlock(0);
  html += buildZoneConfigBlock(1);
  html += buildSystemConfigBlock();
  html += "</div></div>";
  html += "<div class='tab-pane' data-tab='charts'>";
  html += buildChartsTabBlock();
  html += "</div>";
  html += "<div class='tab-pane' data-tab='sensors'><div class='section-stack'>";
  html += buildDiagnosticsBlock();
  html += "</div></div>";
  html += "<div class='tab-pane' data-tab='wifi'><div class='section-stack'>";
  html += buildWiFiBlock();
  html += "</div></div>";

  html += "<script>";
  html += "const z1Points=[];const z2Points=[];";
  html += "function activateTab(tab){document.querySelectorAll('.tab-btn').forEach(b=>b.classList.toggle('active',b.dataset.tab===tab));document.querySelectorAll('.tab-pane').forEach(p=>p.classList.toggle('active',p.dataset.tab===tab));localStorage.setItem('gh-tab',tab);if(history.replaceState){history.replaceState(null,'','#'+tab);}else{location.hash=tab;}}";
  html += "function initTabs(){const saved=(location.hash||'#'+(localStorage.getItem('gh-tab')||'home')).replace('#','');activateTab(saved||'home');document.querySelectorAll('.tab-btn').forEach(btn=>btn.addEventListener('click',()=>activateTab(btn.dataset.tab)));}";
  html += "function addPoint(arr,v){if(v===null||Number.isNaN(v))return;arr.push(v);if(arr.length>120)arr.shift();}";
  html += "function drawChart(id,points,color,emptyText){const c=document.getElementById(id);if(!c)return;const x=c.getContext('2d');const w=c.width,h=c.height;x.clearRect(0,0,w,h);x.fillStyle='#111827';x.fillRect(0,0,w,h);x.strokeStyle='#374151';for(let i=0;i<5;i++){const y=20+i*(h-40)/4;x.beginPath();x.moveTo(40,y);x.lineTo(w-10,y);x.stroke();}if(points.length===0){x.fillStyle='#9ca3af';x.font='14px Arial';x.fillText(emptyText,50,h/2);return;}let min=Math.min(...points);let max=Math.max(...points);if(max-min<1){min-=0.5;max+=0.5;}x.fillStyle='#9ca3af';x.font='12px Arial';x.fillText(max.toFixed(1)+'°C',6,24);x.fillText(min.toFixed(1)+'°C',6,h-12);x.strokeStyle=color;x.lineWidth=2;x.beginPath();points.forEach((p,i)=>{const px=40+(i*(w-55))/Math.max(points.length-1,1);const py=20+(max-p)*(h-40)/Math.max(max-min,0.1);if(i===0)x.moveTo(px,py);else x.lineTo(px,py);});x.stroke();}";
  html += "function renderCharts(){drawChart('z1Chart',z1Points,'#22c55e','Ще немає даних');drawChart('z2Chart',z2Points,'#60a5fa','Ще немає даних');}";
  html += "function setText(id,value){const el=document.getElementById(id);if(el)el.textContent=value;}";
  html += "async function poll(){try{const r=await fetch('/status');const s=await r.json();setText('mode',s.mode);setText('alarm',s.alarm);setText('wind',s.wind);setText('rain',s.rain);setText('water',s.water);setText('msg',s.message);setText('wind_raw',s.windRaw);setText('rain_raw',s.rainRaw);setText('water_raw',s.waterRaw);setText('aux_state',s.auxState);setText('aux_event',s.auxEvent);setText('z1_state',s.z1State);setText('z1_temp',s.z1Temp);setText('z1_hum',s.z1Hum);setText('z1_level',s.z1Level);setText('z1_errors',s.z1Errors);setText('z1_event',s.z1Event);setText('z2_state',s.z2State);setText('z2_temp',s.z2Temp);setText('z2_hum',s.z2Hum);setText('z2_level',s.z2Level);setText('z2_errors',s.z2Errors);setText('z2_event',s.z2Event);setText('hdr_ip',s.routerIp);setText('hdr_ssid',s.routerSsid);setText('hdr_wifi',s.routerBanner);const wifiBadge=document.getElementById('hdr_wifi');if(wifiBadge){wifiBadge.classList.toggle('ok',s.routerConnected);wifiBadge.classList.toggle('warn',!s.routerConnected);}setText('diag_z1_errors',s.z1Errors);setText('diag_z2_errors',s.z2Errors);setText('diag_disconnect',s.wifiLastDisconnect);setText('diag_rssi',s.wifiRssi);setText('diag_wifi_state',s.wifiState);setText('diag_boot',s.bootCounter);setText('diag_reset',s.resetReason);setText('diag_uptime',s.uptime);addPoint(z1Points,s.z1TempRaw);addPoint(z2Points,s.z2TempRaw);renderCharts();}catch(e){}}";
  html += "async function pollLog(){try{const r=await fetch('/events.txt?limit=12');document.getElementById('elog').textContent=await r.text();}catch(e){}}";
  html += "initTabs();poll();pollLog();setInterval(poll,2000);setInterval(pollLog,5000);";
  html += "</script></div></body></html>";
  return html;
}

String statusJson() {
  String json = "{";
  json += "\"mode\":\"" + jsonEscape(modeLabel()) + "\"";
  json += ",\"alarm\":\"" + jsonEscape(alarmStatusLabel()) + "\"";
  json += ",\"wind\":\"" + jsonEscape(windStatusLabel()) + "\"";
  json += ",\"rain\":\"" + jsonEscape(rainStatusLabel()) + "\"";
  json += ",\"water\":\"" + jsonEscape(waterStatusLabel()) + "\"";
  json += ",\"windRaw\":\"" + String(windRaw) + "\"";
  json += ",\"rainRaw\":\"" + String(rainRaw) + "\"";
  json += ",\"waterRaw\":\"" + jsonEscape(waterSignalLabel()) + "\"";
  json += ",\"message\":\"" + jsonEscape(String(lastActionMessage)) + "\"";
  json += ",\"auxState\":\"" + jsonEscape(serviceMotorStateLabel(serviceMotor.state)) + "\"";
  json += ",\"auxEvent\":\"" + jsonEscape(String(serviceMotor.lastEvent)) + "\"";
  json += ",\"routerSsid\":\"" + jsonEscape(String(strlen(wifiConnectedSsid) > 0 ? wifiConnectedSsid : wifiCredentialSsidAt(0))) + "\"";
  json += ",\"routerIp\":\"" + jsonEscape(wifiIpLabel()) + "\"";
  json += ",\"routerBanner\":\"" + jsonEscape(routerConnected ? String("Wi-Fi OK") : String("Wi-Fi offline")) + "\"";
  json += ",\"routerConnected\":";
  json += routerConnected ? "true" : "false";
  json += ",\"wifiState\":\"" + jsonEscape(String(wifiStateToString(wifiState))) + "\"";
  json += ",\"wifiRssi\":\"" + jsonEscape(routerConnected ? String(wifiLastRssi) + " dBm" : String("n/a")) + "\"";
  json += ",\"wifiLastDisconnect\":\"" + jsonEscape(String(wifiLastDisconnectReason)) + "\"";
  json += ",\"bootCounter\":\"" + String(bootCounter) + "\"";
  json += ",\"resetReason\":\"" + jsonEscape(String(resetReasonLabel(esp_reset_reason()))) + "\"";
  json += ",\"uptime\":\"" + jsonEscape(formatElapsedMs(millis())) + "\"";
  json += ",\"uptimeMs\":" + String(millis());
  json += ",\"cfgMoveMs\":" + String(cfg.moveMs);
  json += ",\"cfgPauseMs\":" + String(cfg.pauseMs);
  json += ",\"cfgSwitchMs\":" + String(cfg.switchMs);
  json += ",\"cfgExtraCloseMs\":" + String(cfg.extraCloseMs);
  json += ",\"cfgFullTravelMs\":" + String(cfg.fullTravelMs);
  json += ",\"z1State\":\"" + jsonEscape(zoneStateUiLabel(zone1)) + "\"";
  json += ",\"z1Temp\":\"" + jsonEscape(formatTempCell(zone1.currentTemp)) + "\"";
  json += ",\"z1Hum\":\"" + jsonEscape(formatHumCell(zone1.currentHum)) + "\"";
  json += ",\"z1Level\":\"" + jsonEscape(String(zoneOpenLevelTemp(zone1), 1) + " °C") + "\"";
  json += ",\"z1Close\":\"" + jsonEscape(zone1.currentStep > 0 ? String(closeThresholdForStep(zone1, zone1.currentStep), 1) + " °C" : String("--")) + "\"";
  json += ",\"z1Errors\":\"" + String(zone1.dhtFailCount) + "\"";
  json += ",\"z1Event\":\"" + jsonEscape(String(zone1.lastEvent)) + "\"";
  json += ",\"z1ActiveCmd\":\"" + jsonEscape(String(zoneCommandLabel(zone1.state == WAIT_SWITCH ? zone1.pendingCommandKind : zone1.activeCommandKind))) + "\"";
  json += ",\"z1TimerMs\":" + String(zoneActiveTimerMs(zone1));
  json += ",\"z1ElapsedMs\":" + String(zoneActiveElapsedMs(zone1));
  json += ",\"z1RemainingMs\":" + String(zoneActiveRemainingMs(zone1));
  json += ",\"z1TempRaw\":";
  json += isnan(zone1.currentTemp) ? String("null") : String(zone1.currentTemp, 1);
  json += ",\"z2State\":\"" + jsonEscape(zoneStateUiLabel(zone2)) + "\"";
  json += ",\"z2Temp\":\"" + jsonEscape(formatTempCell(zone2.currentTemp)) + "\"";
  json += ",\"z2Hum\":\"" + jsonEscape(formatHumCell(zone2.currentHum)) + "\"";
  json += ",\"z2Level\":\"" + jsonEscape(String(zoneOpenLevelTemp(zone2), 1) + " °C") + "\"";
  json += ",\"z2Close\":\"" + jsonEscape(zone2.currentStep > 0 ? String(closeThresholdForStep(zone2, zone2.currentStep), 1) + " °C" : String("--")) + "\"";
  json += ",\"z2Errors\":\"" + String(zone2.dhtFailCount) + "\"";
  json += ",\"z2Event\":\"" + jsonEscape(String(zone2.lastEvent)) + "\"";
  json += ",\"z2ActiveCmd\":\"" + jsonEscape(String(zoneCommandLabel(zone2.state == WAIT_SWITCH ? zone2.pendingCommandKind : zone2.activeCommandKind))) + "\"";
  json += ",\"z2TimerMs\":" + String(zoneActiveTimerMs(zone2));
  json += ",\"z2ElapsedMs\":" + String(zoneActiveElapsedMs(zone2));
  json += ",\"z2RemainingMs\":" + String(zoneActiveRemainingMs(zone2));
  json += ",\"z2TempRaw\":";
  json += isnan(zone2.currentTemp) ? String("null") : String(zone2.currentTemp, 1);
  json += "}";
  return json;
}

void redirectHome() {
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", webPageHtml());
}

void handleStatusRoute() {
  server.send(200, "application/json; charset=utf-8", statusJson());
}

void handleStatusTextRoute() {
  server.send(200, "text/plain; charset=utf-8", systemStatusText());
}

void handleEventsTextRoute() {
  size_t maxEntries = EVENT_LOG_CAPACITY;
  if (server.hasArg("limit")) {
    const int requested = server.arg("limit").toInt();
    if (requested > 0) {
      maxEntries = min(static_cast<size_t>(requested), static_cast<size_t>(EVENT_LOG_CAPACITY));
    }
  }
  server.send(200, "text/plain; charset=utf-8", eventLogText(maxEntries));
}

unsigned long secondsArgToMs(const String& value, unsigned long defaultValue) {
  if (value.length() == 0) {
    return defaultValue;
  }
  String normalized = value;
  normalized.trim();
  normalized.replace(',', '.');
  const float seconds = normalized.toFloat();
  if (seconds <= 0.0f) {
    return defaultValue;
  }
  return static_cast<unsigned long>(seconds * 1000.0f);
}

bool boolArgEnabled(const String& value) {
  return value == "1" || value == "true" || value == "on";
}

void updateZoneMaxStep(Zone& z, const ZoneConfig& config) {
  z.maxStep = calculateMaxStep(config);
  z.currentStep = constrain(z.currentStep, 0, z.maxStep);
  z.targetStep = constrain(z.targetStep, 0, z.maxStep);
}

void handleConfigRoute() {
  const String scope = server.arg("scope");

  if (scope == "z1" || scope == "z2") {
    const uint8_t idx = scope == "z1" ? 0 : 1;
    ZoneConfig& config = zoneConfigs[idx];
    if (server.hasArg("openStartTemp")) config.openStartTemp = server.arg("openStartTemp").toFloat();
    if (server.hasArg("tempStep")) config.tempStep = max(0.1f, server.arg("tempStep").toFloat());
    if (server.hasArg("closeHysteresis")) config.closeHysteresis = max(0.1f, server.arg("closeHysteresis").toFloat());
    if (server.hasArg("maxTemp")) config.maxTemp = max(config.openStartTemp, server.arg("maxTemp").toFloat());
    updateZoneMaxStep(*zones[idx], config);
    if ((*zones[idx]).dhtOk) {
      (*zones[idx]).targetStep = computeTargetStep(*zones[idx], (*zones[idx]).currentTemp);
    }
    setLastActionMessage(String("Зона ") + String(idx + 1) + ": параметри збережено");
    setZoneLastEvent(*zones[idx], "Параметри оновлено");
    Serial.printf("[Zone %u] config saved via web\n", idx + 1);
    redirectHome();
    return;
  }

  if (scope == "global") {
    if (server.hasArg("dhtReadMs")) cfg.dhtReadMs = max(1500UL, secondsArgToMs(server.arg("dhtReadMs"), cfg.dhtReadMs));
    if (server.hasArg("moveMs")) cfg.moveMs = max(500UL, secondsArgToMs(server.arg("moveMs"), cfg.moveMs));
    if (server.hasArg("pauseMs")) cfg.pauseMs = max(0UL, secondsArgToMs(server.arg("pauseMs"), cfg.pauseMs));
    if (server.hasArg("startupCloseMs")) cfg.startupCloseMs = max(0UL, secondsArgToMs(server.arg("startupCloseMs"), cfg.startupCloseMs));
    if (server.hasArg("switchMs")) cfg.switchMs = max(0UL, secondsArgToMs(server.arg("switchMs"), cfg.switchMs));
    if (server.hasArg("extraCloseMs")) cfg.extraCloseMs = max(0UL, secondsArgToMs(server.arg("extraCloseMs"), cfg.extraCloseMs));
    if (server.hasArg("fullTravelMs")) cfg.fullTravelMs = max(cfg.moveMs, secondsArgToMs(server.arg("fullTravelMs"), cfg.fullTravelMs));
    if (server.hasArg("enableWindAlarm")) cfg.enableWindAlarm = boolArgEnabled(server.arg("enableWindAlarm"));
    if (server.hasArg("windAlarmThreshold")) cfg.windAlarmThreshold = max(0L, server.arg("windAlarmThreshold").toInt());
    if (server.hasArg("enableRainAlarm")) cfg.enableRainAlarm = boolArgEnabled(server.arg("enableRainAlarm"));
    if (server.hasArg("rainAlarmThreshold")) cfg.rainAlarmThreshold = max(0L, server.arg("rainAlarmThreshold").toInt());
    if (server.hasArg("enableWaterMonitor")) cfg.enableWaterMonitor = boolArgEnabled(server.arg("enableWaterMonitor"));
    setLastActionMessage("Системні параметри збережено");
    appendEventLogf("Global config saved: move=%lu pause=%lu switch=%lu extra=%lu full=%lu",
                    cfg.moveMs,
                    cfg.pauseMs,
                    cfg.switchMs,
                    cfg.extraCloseMs,
                    cfg.fullTravelMs);
    Serial.println("[SYSTEM] config saved via web");
    redirectHome();
    return;
  }

  if (scope == "serviceMotor") {
    if (server.hasArg("serviceMotorMs")) {
      cfg.serviceMotorMs = max(500UL, secondsArgToMs(server.arg("serviceMotorMs"), cfg.serviceMotorMs));
    }
    setLastActionMessage("Таймер сервісного мотора збережено");
    setServiceMotorEvent("Таймер оновлено");
    Serial.println("[SERVICE] timer saved via web");
    redirectHome();
    return;
  }

  redirectHome();
}

void printWiFiStatus() {
  Serial.println("=== WIFI ===");
  Serial.printf("Enabled: %s\n", wifiEnabled ? "YES" : "NO");
  Serial.printf("State: %s\n", wifiStateToString(wifiState));
  Serial.printf("Status: %d\n", static_cast<int>(WiFi.status()));
  Serial.printf("SSID: %s\n", strlen(wifiConnectedSsid) > 0 ? wifiConnectedSsid : wifiCredentialSsidAt(0));
  Serial.printf("IP: %s\n", wifiIpLabel().c_str());
  if (routerConnected) {
    Serial.printf("RSSI: %ld dBm\n", static_cast<long>(wifiLastRssi));
  } else {
    Serial.println("RSSI: n/a");
  }
  Serial.printf("Disconnect count: %lu\n", static_cast<unsigned long>(wifiDisconnectCount));
  Serial.printf("Reconnect success count: %lu\n", static_cast<unsigned long>(wifiReconnectSuccessCount));
  Serial.printf("Last disconnect reason: %s\n", wifiLastDisconnectReason);
  Serial.printf("Connected for: %s\n", routerConnected ? formatElapsedMs(wifiConnectedSinceMs).c_str() : "0s");
  Serial.printf("Last reconnect attempt: %s\n", wifiLastConnectAttemptMs ? (formatElapsedMs(wifiLastConnectAttemptMs) + " ago").c_str() : "never");
}

void handleWiFiConnected() {
  routerConnected = true;
  wifiState = WIFI_STATE_CONNECTED;
  wifiReconnectNeeded = false;
  wifiConnectedSinceMs = millis();
  wifiLastRssi = WiFi.RSSI();
  strncpy(wifiConnectedSsid, WiFi.SSID().c_str(), sizeof(wifiConnectedSsid) - 1);
  wifiConnectedSsid[sizeof(wifiConnectedSsid) - 1] = '\0';
  if (wifiHasConnectedOnce) {
    wifiReconnectSuccessCount++;
  } else {
    wifiHasConnectedOnce = true;
  }
  wifiWeakSignalWarned = false;
  wifiCriticalSignalWarned = false;
  strncpy(wifiLastDisconnectReason, "none", sizeof(wifiLastDisconnectReason) - 1);
  wifiLastDisconnectReason[sizeof(wifiLastDisconnectReason) - 1] = '\0';
  Serial.printf("WiFi connected: SSID=%s IP=%s RSSI=%ld dBm\n",
                wifiConnectedSsid,
                wifiIpLabel().c_str(),
                static_cast<long>(wifiLastRssi));
  setLastActionMessage(String("Wi-Fi: підключено до ") + wifiConnectedSsid);
  appendEventLogf("WiFi connected: %s | IP=%s | RSSI=%ld dBm",
                  wifiConnectedSsid,
                  wifiIpLabel().c_str(),
                  static_cast<long>(wifiLastRssi));
}

void handleWiFiDisconnected(int reasonCode) {
  routerConnected = false;
  wifiLastRssi = 0;
  snprintf(wifiLastDisconnectReason, sizeof(wifiLastDisconnectReason), "reason=%d", reasonCode);

  if (!wifiEnabled) {
    wifiState = WIFI_STATE_OFF;
    return;
  }

  wifiDisconnectCount++;
  wifiReconnectNeeded = true;
  wifiState = WIFI_STATE_RECONNECT_WAIT;
  wifiLastRetryMs = millis();
  wifiCurrentCredentialIndex = -1;
  wifiAvailableCredentialCount = 0;
  wifiAvailableCredentialCursor = 0;
  wifiScanRequested = true;
  Serial.printf("WARNING: WiFi disconnected (%s). Reconnect scheduled.\n", wifiLastDisconnectReason);
  appendEventLogf("WiFi disconnected: %s", wifiLastDisconnectReason);
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      handleWiFiConnected();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      handleWiFiDisconnected(info.wifi_sta_disconnected.reason);
      break;
    default:
      break;
  }
}

void startWiFiConnect() {
  if (!wifiEnabled) {
    return;
  }

  if (wifiAvailableCredentialCursor >= wifiAvailableCredentialCount) {
    wifiState = WIFI_STATE_RECONNECT_WAIT;
    wifiReconnectNeeded = true;
    wifiLastRetryMs = millis();
    Serial.println("WARNING: all configured WiFi networks failed, retry later.");
    appendEventLog("WARNING: all configured WiFi networks failed, retry later.");
    return;
  }

  wifiCurrentCredentialIndex = wifiAvailableCredentialIndices[wifiAvailableCredentialCursor++];
  const char* ssid = wifiCredentialSsidAt(wifiCurrentCredentialIndex);
  const char* pass = wifiCredentialPassAt(wifiCurrentCredentialIndex);
  strncpy(wifiTargetSsid, ssid, sizeof(wifiTargetSsid) - 1);
  wifiTargetSsid[sizeof(wifiTargetSsid) - 1] = '\0';

  WiFi.disconnect(false, false);
  WiFi.begin(ssid, pass);
  wifiState = WIFI_STATE_CONNECTING;
  wifiConnectStartMs = millis();
  wifiLastConnectAttemptMs = wifiConnectStartMs;
  wifiReconnectNeeded = false;
  Serial.printf("Connecting to router: %s\n", ssid);
  appendEventLogf("WiFi connect attempt: %s", ssid);
}

void scanWiFiNetworks(bool forConnect) {
  if (!wifiEnabled) {
    return;
  }

  const int scanState = WiFi.scanComplete();
  if (scanState == WIFI_SCAN_RUNNING) {
    return;
  }
  if (scanState >= 0) {
    WiFi.scanDelete();
  }

  wifiScanForConnect = forConnect;
  wifiManualScanRequested = !forConnect;
  wifiScanRequested = false;
  wifiState = WIFI_STATE_SCANNING;
  wifiLastScanStartMs = millis();

  if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) {
    wifiState = WIFI_STATE_ERROR;
    wifiLastRetryMs = millis();
    Serial.println("WARNING: WiFi scan start failed.");
  } else {
    Serial.println("[WIFI] scan started");
  }
}

void setupAccessPoint() {
  WiFi.softAPdisconnect(true);
  if (WiFi.softAP(AP_SSID, AP_PASS)) {
    fallbackApActive = true;
    Serial.printf("WiFi AP started: %s\n", AP_SSID);
    Serial.printf("Open AP in browser: http://%s\n", WiFi.softAPIP().toString().c_str());
    appendEventLogf("WiFi AP started: %s | IP=%s", AP_SSID, WiFi.softAPIP().toString().c_str());
  } else {
    fallbackApActive = false;
    Serial.println("WARNING: failed to start WiFi AP");
    appendEventLog("WARNING: failed to start WiFi AP");
  }
}

void setupWiFi() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.onEvent(onWiFiEvent);
  routerConnected = false;
  setupAccessPoint();
  wifiEnabled = true;
  wifiReconnectNeeded = true;
  wifiScanRequested = true;
  wifiScanForConnect = true;
  wifiManualScanRequested = false;
  wifiState = WIFI_STATE_IDLE;
  wifiLastRetryMs = millis() - WIFI_RETRY_INTERVAL_MS;
  Serial.printf("WiFi hostname: %s\n", WIFI_HOSTNAME);
}

void processWiFi(unsigned long now) {
  if (!wifiEnabled) {
    return;
  }

  if (wifiState == WIFI_STATE_CONNECTED && WiFi.status() != WL_CONNECTED) {
    handleWiFiDisconnected(-1);
  }

  if (wifiState == WIFI_STATE_CONNECTING && now - wifiConnectStartMs >= WIFI_CONNECT_TIMEOUT_MS) {
    Serial.printf("WARNING: WiFi connect timeout for %s\n", wifiTargetSsid);
    appendEventLogf("WARNING: WiFi connect timeout for %s", wifiTargetSsid);
    WiFi.disconnect(false, false);
    startWiFiConnect();
    return;
  }

  if (!routerConnected && !wifiScanRequested && now - wifiLastScanStartMs >= WIFI_SCAN_INTERVAL_MS) {
    wifiScanRequested = true;
  }

  if (wifiState == WIFI_STATE_ERROR && now - wifiLastRetryMs >= WIFI_RETRY_INTERVAL_MS) {
    wifiState = WIFI_STATE_RECONNECT_WAIT;
  }

  if ((wifiState == WIFI_STATE_IDLE || wifiState == WIFI_STATE_RECONNECT_WAIT) &&
      (wifiScanRequested || (!routerConnected && now - wifiLastRetryMs >= WIFI_RETRY_INTERVAL_MS))) {
    scanWiFiNetworks(true);
    return;
  }

  if (wifiState == WIFI_STATE_SCANNING) {
    const int scanResult = WiFi.scanComplete();
    if (scanResult == WIFI_SCAN_RUNNING) {
      return;
    }

    if (scanResult >= 0) {
      wifiAvailableCredentialCount = 0;
      for (size_t credIdx = 0; credIdx < WIFI_LIST_COUNT; credIdx++) {
        if (!wifiCredentialConfigured(credIdx)) {
          continue;
        }
        const char* configuredSsid = wifiCredentialSsidAt(credIdx);
        for (int netIdx = 0; netIdx < scanResult; netIdx++) {
          if (WiFi.SSID(netIdx) == configuredSsid) {
            wifiAvailableCredentialIndices[wifiAvailableCredentialCount++] = static_cast<int>(credIdx);
            break;
          }
        }
      }

      if (wifiManualScanRequested) {
        Serial.printf("[WIFI] scan results: %d\n", scanResult);
        for (int netIdx = 0; netIdx < scanResult; netIdx++) {
          Serial.printf("  %s | RSSI=%d dBm | ENC=%d\n",
                        WiFi.SSID(netIdx).c_str(),
                        WiFi.RSSI(netIdx),
                        WiFi.encryptionType(netIdx));
        }
      }

      const bool restoreConnectedState = routerConnected && !wifiScanForConnect;
      WiFi.scanDelete();
      wifiManualScanRequested = false;
      wifiState = restoreConnectedState ? WIFI_STATE_CONNECTED : WIFI_STATE_IDLE;

      if (wifiScanForConnect) {
        if (wifiAvailableCredentialCount > 0) {
          wifiAvailableCredentialCursor = 0;
          startWiFiConnect();
        } else {
          wifiState = WIFI_STATE_RECONNECT_WAIT;
          wifiReconnectNeeded = true;
          wifiLastRetryMs = now;
          Serial.println("WARNING: no configured WiFi networks found. Will retry later.");
        }
      }
      wifiScanForConnect = false;
      return;
    }

    if (scanResult == WIFI_SCAN_FAILED) {
      wifiState = WIFI_STATE_ERROR;
      wifiLastRetryMs = now;
      Serial.println("WARNING: WiFi scan failed.");
    }
  }

  if (wifiState == WIFI_STATE_CONNECTED) {
    wifiLastRssi = WiFi.RSSI();
    if (wifiLastRssi < -85 && !wifiCriticalSignalWarned) {
      wifiCriticalSignalWarned = true;
      wifiWeakSignalWarned = true;
      Serial.printf("CRITICAL: very weak WiFi signal (%ld dBm)\n", static_cast<long>(wifiLastRssi));
    } else if (wifiLastRssi < -75 && !wifiWeakSignalWarned) {
      wifiWeakSignalWarned = true;
      Serial.printf("WARNING: weak WiFi signal (%ld dBm)\n", static_cast<long>(wifiLastRssi));
    } else if (wifiLastRssi >= -75) {
      wifiWeakSignalWarned = false;
      wifiCriticalSignalWarned = false;
    }
  }

  if (now - wifiLastStatusMs >= WIFI_STATUS_PRINT_MS) {
    wifiLastStatusMs = now;
    printWiFiStatus();
  }

  // Any external services must only run when WiFi.status() == WL_CONNECTED.
  // If Wi-Fi is unavailable, skip them and never block loop().
}

void handleWiFiRoute() {
  if (server.hasArg("ssid")) {
    routerSsid = server.arg("ssid");
  }
  if (server.hasArg("pass")) {
    routerPass = server.arg("pass");
  }
  wifiReconnectNeeded = true;
  wifiScanRequested = true;
  wifiLastRetryMs = millis() - WIFI_RETRY_INTERVAL_MS;
  WiFi.disconnect(false, false);
  wifiState = WIFI_STATE_IDLE;
  redirectHome();
}

void serviceMotorOutputsOff() {
  digitalWrite(PIN_SERVICE_OPEN, RELAY_OFF);
  digitalWrite(PIN_SERVICE_CLOSE, RELAY_OFF);
}

void stopServiceMotorWithEvent(const String& eventMessage) {
  serviceMotorOutputsOff();
  serviceMotor.state = SMS_IDLE;
  serviceMotor.activeDirection = DIR_NONE;
  serviceMotor.pendingDirection = DIR_NONE;
  serviceMotor.stateStartMs = millis();
  serviceMotor.runUntilMs = 0;
  setServiceMotorEvent(eventMessage);
  Serial.printf("[SERVICE] stop | event=%s\n", serviceMotor.lastEvent);
  appendEventLogf("Service motor stop | event=%s", serviceMotor.lastEvent);
}

void stopServiceMotor() {
  stopServiceMotorWithEvent("Очікування");
  setLastActionMessage("Зашторення: стоп");
}

void startServiceMotorNow(Direction dir) {
  serviceMotorOutputsOff();
  if (dir == DIR_OPEN) {
    digitalWrite(PIN_SERVICE_OPEN, RELAY_ON);
    serviceMotor.state = SMS_OPENING;
    setServiceMotorEvent("Відкривання");
    setLastActionMessage("Зашторення: відкривання");
  } else {
    digitalWrite(PIN_SERVICE_CLOSE, RELAY_ON);
    serviceMotor.state = SMS_CLOSING;
    setServiceMotorEvent("Закривання");
    setLastActionMessage("Зашторення: закривання");
  }
  serviceMotor.activeDirection = dir;
  serviceMotor.pendingDirection = DIR_NONE;
  serviceMotor.stateStartMs = millis();
  serviceMotor.runUntilMs = millis() + cfg.serviceMotorMs;
  Serial.printf("[SERVICE] start %s for %lu ms\n", directionLabel(dir), cfg.serviceMotorMs);
  appendEventLogf("Service motor start %s for %lu ms", directionLabel(dir), cfg.serviceMotorMs);
}

void commandServiceMotor(Direction dir) {
  autoMode = false;
  if (dir == DIR_NONE) {
    stopServiceMotor();
    return;
  }

  const bool reverseNeeded = serviceMotor.activeDirection != DIR_NONE &&
                             serviceMotor.activeDirection != dir &&
                             (serviceMotor.state == SMS_OPENING || serviceMotor.state == SMS_CLOSING);

  if (reverseNeeded) {
    serviceMotorOutputsOff();
    serviceMotor.state = SMS_WAIT_SWITCH;
    serviceMotor.pendingDirection = dir;
    serviceMotor.activeDirection = DIR_NONE;
    serviceMotor.stateStartMs = millis();
    setServiceMotorEvent(dir == DIR_OPEN ? "Пауза перед відкриванням" : "Пауза перед закриванням");
    setLastActionMessage("Зашторення: пауза перед реверсом");
    Serial.printf("[SERVICE] wait switch %lu ms before %s\n", cfg.switchMs, directionLabel(dir));
    appendEventLogf("Service motor wait switch %lu ms before %s", cfg.switchMs, directionLabel(dir));
    return;
  }

  startServiceMotorNow(dir);
}

void updateServiceMotor(unsigned long now) {
  if (serviceMotor.state == SMS_WAIT_SWITCH) {
    if (now - serviceMotor.stateStartMs >= cfg.switchMs) {
      startServiceMotorNow(serviceMotor.pendingDirection);
    }
    return;
  }

  if ((serviceMotor.state == SMS_OPENING || serviceMotor.state == SMS_CLOSING) &&
      now >= serviceMotor.runUntilMs) {
    stopServiceMotorWithEvent("Таймер завершено");
    setLastActionMessage("Зашторення: таймер завершено");
  }
}

void stopAllMotion() {
  autoMode = false;

  for (int i = 0; i < 2; i++) {
    Zone& z = *zones[i];
    const bool wasBusy = zoneIsBusy(z);
    stopMotor(z);
    z.pendingDirection = DIR_NONE;
    z.pendingState = IDLE;
    z.pendingSource = SRC_NONE;
    z.pendingCommandKind = ZCMD_NONE;
    z.activeSource = SRC_NONE;
    z.activeCommandKind = ZCMD_NONE;
    z.plannedMoveMs = 0;
    z.pendingStepDelta = 0;
    z.extraClosePending = false;
    if (wasBusy) {
      z.calibrated = false;
      z.positionUncertain = true;
      setZoneLastEvent(z, "Зупинено вручну");
    }
    z.state = z.error ? ERROR : IDLE;
    resetConfirmCounters(z);
  }

  stopServiceMotor();
  appendEventLog("Global stop: all motion stopped");
}

void handleControlRoute() {
  const String cmd = server.arg("cmd");

  if (cmd == "auto") {
    processCommand("auto on");
  } else if (cmd == "manual") {
    processCommand("auto off");
    setLastActionMessage("Ручний режим увімкнено");
  } else if (cmd == "stop") {
    stopAllMotion();
    autoMode = false;
    setLastActionMessage("Стоп: усі виходи зупинено");
  } else if (cmd == "resetalarm") {
    clearEmergency();
    setLastActionMessage("Аварію скинуто");
  } else if (cmd == "z1_open") {
    manualOpen(zone1);
    setLastActionMessage("Зона 1: ручне відкривання");
  } else if (cmd == "z1_close") {
    manualClose(zone1);
    setLastActionMessage("Зона 1: ручне закривання");
  } else if (cmd == "z1_fullopen") {
    manualFullOpen(zone1);
    setLastActionMessage("Зона 1: повне відкривання");
  } else if (cmd == "z1_fullclose") {
    manualFullClose(zone1);
    setLastActionMessage("Зона 1: повне закривання");
  } else if (cmd == "z1_extra") {
    manualExtraClose(zone1);
    setLastActionMessage("Зона 1: дотяжка");
  } else if (cmd == "z1_reset") {
    resetZonePosition(zone1);
    setLastActionMessage("Зона 1: позицію скинуто");
  } else if (cmd == "z2_open") {
    manualOpen(zone2);
    setLastActionMessage("Зона 2: ручне відкривання");
  } else if (cmd == "z2_close") {
    manualClose(zone2);
    setLastActionMessage("Зона 2: ручне закривання");
  } else if (cmd == "z2_fullopen") {
    manualFullOpen(zone2);
    setLastActionMessage("Зона 2: повне відкривання");
  } else if (cmd == "z2_fullclose") {
    manualFullClose(zone2);
    setLastActionMessage("Зона 2: повне закривання");
  } else if (cmd == "z2_extra") {
    manualExtraClose(zone2);
    setLastActionMessage("Зона 2: дотяжка");
  } else if (cmd == "z2_reset") {
    resetZonePosition(zone2);
    setLastActionMessage("Зона 2: позицію скинуто");
  } else if (cmd == "aux_open") {
    commandServiceMotor(DIR_OPEN);
  } else if (cmd == "aux_close") {
    commandServiceMotor(DIR_CLOSE);
  } else if (cmd == "aux_stop") {
    stopServiceMotor();
  } else if (cmd == "clearwifi") {
    routerSsid = WIFI_SSID;
    routerPass = WIFI_PASS;
    wifiReconnectNeeded = true;
    wifiScanRequested = true;
    wifiLastRetryMs = millis() - WIFI_RETRY_INTERVAL_MS;
    WiFi.disconnect(false, false);
    wifiState = WIFI_STATE_IDLE;
    setLastActionMessage("Wi-Fi очищено, відновлено базову конфігурацію");
  }

  redirectHome();
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatusRoute);
  server.on("/status.txt", HTTP_GET, handleStatusTextRoute);
  server.on("/events.txt", HTTP_GET, handleEventsTextRoute);
  server.on("/control", HTTP_POST, handleControlRoute);
  server.on("/config", HTTP_POST, handleConfigRoute);
  server.on("/wifi", HTTP_POST, handleWiFiRoute);
  server.onNotFound(handleRoot);
  server.begin();
  Serial.println("Web server started");
}

void beginStartupCalibration() {
  if (!ENABLE_STARTUP_CLOSE) {
    zone1.calibrated = true;
    zone2.calibrated = true;
    zone1.state = IDLE;
    zone2.state = IDLE;
    zone1.currentStep = 0;
    zone2.currentStep = 0;
    zone1.targetStep = 0;
    zone2.targetStep = 0;
    setZoneLastEvent(zone1, "Старт без закриття");
    setZoneLastEvent(zone2, "Старт без закриття");
    Serial.println("Startup close disabled: both zones assumed closed.");
    appendEventLog("Startup close disabled. Both zones assumed closed.");
    return;
  }

  Serial.printf("Startup close enabled: closing both zones for %lu ms\n", cfg.startupCloseMs);
  appendEventLogf("Startup close enabled for %lu ms", cfg.startupCloseMs);
  startStartupClose(zone1);
  startStartupClose(zone2);
}

void triggerEmergency(const char* reason) {
  if (globalEmergency) {
    return;
  }

  globalEmergency = true;
  autoMode = false;
  strncpy(emergencyReason, reason, sizeof(emergencyReason) - 1);
  emergencyReason[sizeof(emergencyReason) - 1] = '\0';

  Serial.println();
  Serial.printf("!!! GLOBAL EMERGENCY: %s !!!\n", emergencyReason);
  Serial.println("Auto mode disabled. Zones will close to step 0 and then LOCK.");
  Serial.println();
  appendEventLogf("GLOBAL EMERGENCY: %s", emergencyReason);

  zone1.locked = false;
  zone2.locked = false;
  zone1.emergencyManualOverride = false;
  zone2.emergencyManualOverride = false;
  resetConfirmCounters(zone1);
  resetConfirmCounters(zone2);
}

void clearEmergency() {
  if (!globalEmergency) {
    Serial.println("No active emergency to reset.");
    return;
  }

  globalEmergency = false;
  autoMode = true;
  strncpy(emergencyReason, "none", sizeof(emergencyReason) - 1);
  emergencyReason[sizeof(emergencyReason) - 1] = '\0';

  for (int i = 0; i < 2; i++) {
    Zone& z = *zones[i];
    z.locked = false;
    z.extraClosePending = false;
    z.emergencyManualOverride = false;
    z.pendingDirection = DIR_NONE;
    z.pendingState = IDLE;
    z.activeSource = SRC_NONE;
    z.pendingSource = SRC_NONE;
    z.plannedMoveMs = 0;
    z.pendingStepDelta = 0;
    if (!isMotorState(z.state) && z.state != WAIT_SWITCH) {
      z.state = z.error ? ERROR : IDLE;
    }
    resetConfirmCounters(z);
    zoneRelaysOff(z);
  }

  Serial.println("Emergency reset. autoMode=ON. Waiting for normal temperature evaluation.");
  appendEventLog("Emergency reset. Auto mode ON.");
}

void resetZonePosition(Zone& z) {
  if (!manualCommandAllowed(z, false)) {
    return;
  }
  onManualCommandAccepted(z, "RESET POSITION", false);
  zoneRelaysOff(z);
  z.currentStep = 0;
  z.targetStep = 0;
  z.calibrated = true;
  z.positionUncertain = false;
  z.locked = false;
  z.error = false;
  z.state = IDLE;
  z.pendingDirection = DIR_NONE;
  z.pendingState = IDLE;
  z.activeSource = SRC_NONE;
  z.pendingSource = SRC_NONE;
  z.lastCommandSource = SRC_MANUAL;
  z.plannedMoveMs = 0;
  z.pendingStepDelta = 0;
  z.lastDirection = DIR_NONE;
  z.extraClosePending = false;
  z.emergencyManualOverride = globalEmergency;
  z.dhtFailCount = 0;
  resetConfirmCounters(z);
  Serial.printf("[%s] manual reset: currentStep=0, calibrated=YES, position known closed\n", z.name);
  appendEventLogf("%s manual reset: position set closed", z.name);
}

bool manualCommandAllowed(const Zone& z, bool isStop) {
  if (isStop) {
    return true;
  }
  if (globalEmergency && !ALLOW_MANUAL_DURING_EMERGENCY) {
    Serial.printf("[%s] manual command denied: global emergency active\n", z.name);
    return false;
  }
  return true;
}

void onManualCommandAccepted(Zone& z, const char* action, bool affectsPosition) {
  if (globalEmergency) {
    Serial.printf("[%s] WARNING: manual command executed during emergency\n", z.name);
  }

  autoMode = false;
  z.locked = false;
  z.lastCommandSource = SRC_MANUAL;
  z.emergencyManualOverride = globalEmergency;
  if (affectsPosition) {
    z.positionUncertain = true;
  }
  resetConfirmCounters(z);
  setLastActionMessage(String(z.name) + ": " + action);

  Serial.printf("[%s] MANUAL command executed: %s. Auto mode disabled. Use 'auto on' to enable automatic control again.\n",
                z.name,
                action);
  appendEventLogf("%s manual command: %s | autoMode=OFF%s",
                  z.name,
                  action,
                  globalEmergency ? " | during emergency" : "");
}

void manualOpen(Zone& z) {
  if (!manualCommandAllowed(z, false)) {
    return;
  }
  if (z.state == WAIT_AFTER_MOVE) {
    z.state = IDLE;
  }
  onManualCommandAccepted(z, "OPEN pulse", true);
  z.targetStep = z.currentStep;
  startOpen(z, SRC_MANUAL);
}

void manualClose(Zone& z) {
  if (!manualCommandAllowed(z, false)) {
    return;
  }
  if (z.state == WAIT_AFTER_MOVE) {
    z.state = IDLE;
  }
  onManualCommandAccepted(z, "CLOSE pulse", true);
  z.targetStep = z.currentStep;
  startClose(z, SRC_MANUAL);
}

void manualFullOpen(Zone& z) {
  if (!manualCommandAllowed(z, false)) {
    return;
  }
  if (z.state == WAIT_AFTER_MOVE) {
    z.state = IDLE;
  }
  onManualCommandAccepted(z, "FULL OPEN", true);
  z.targetStep = z.maxStep;
  startFullOpen(z);
}

void manualFullClose(Zone& z) {
  if (!manualCommandAllowed(z, false)) {
    return;
  }
  if (z.state == WAIT_AFTER_MOVE) {
    z.state = IDLE;
  }
  onManualCommandAccepted(z, "FULL CLOSE", true);
  z.targetStep = 0;
  startFullClose(z);
}

void manualExtraClose(Zone& z) {
  if (!manualCommandAllowed(z, false)) {
    return;
  }
  if (z.state == WAIT_AFTER_MOVE) {
    z.state = IDLE;
  }
  onManualCommandAccepted(z, "EXTRA CLOSE", true);
  startManualExtraClose(z);
}

void manualStop(Zone& z) {
  if (!manualCommandAllowed(z, true)) {
    return;
  }
  onManualCommandAccepted(z, "STOP", true);
  const bool wasMoving = isMotorState(z.state);
  stopMotor(z);
  z.pendingDirection = DIR_NONE;
  z.pendingState = IDLE;
  z.activeSource = SRC_NONE;
  z.pendingSource = SRC_NONE;
  z.activeCommandKind = ZCMD_NONE;
  z.pendingCommandKind = ZCMD_NONE;
  z.plannedMoveMs = 0;
  z.pendingStepDelta = 0;
  z.extraClosePending = false;
  if (wasMoving) {
    z.calibrated = false;
    Serial.printf("[%s] manual STOP interrupted motion, calibration lost. Position may be inaccurate. Use %s reset when fully closed.\n",
                  z.name,
                  z.openPin == PIN_OPEN_1 ? "z1" : "z2");
    appendEventLogf("%s manual stop interrupted motion; calibration lost", z.name);
  }
  z.positionUncertain = true;
  setZoneLastEvent(z, "Зупинено вручну");
  z.state = z.error ? ERROR : IDLE;
  resetConfirmCounters(z);
  if (!wasMoving) {
    appendEventLogf("%s manual stop", z.name);
  }
}

char* trimSpaces(char* text) {
  while (*text == ' ' || *text == '\t') {
    text++;
  }

  char* end = text + strlen(text);
  while (end > text && (end[-1] == ' ' || end[-1] == '\t')) {
    end--;
  }
  *end = '\0';
  return text;
}

void toLowerInPlace(char* text) {
  for (size_t i = 0; text[i] != '\0'; i++) {
    text[i] = static_cast<char>(tolower(static_cast<unsigned char>(text[i])));
  }
}

void processCommand(const char* rawCmd) {
  char local[64];
  strncpy(local, rawCmd, sizeof(local) - 1);
  local[sizeof(local) - 1] = '\0';

  char* cmd = trimSpaces(local);
  toLowerInPlace(cmd);

  if (cmd[0] == '\0') {
    return;
  }

  Serial.printf("CMD> %s\n", cmd);

  if (strcmp(cmd, "status") == 0) {
    printSystemStatus();
    return;
  }

  if (strcmp(cmd, "wifi status") == 0) {
    printWiFiStatus();
    return;
  }

  if (strcmp(cmd, "log") == 0) {
    printEventLog();
    return;
  }

  if (strcmp(cmd, "log clear") == 0) {
    clearEventLog();
    appendEventLogf("Boot #%d | log cleared", bootCounter);
    Serial.println("Event log cleared.");
    return;
  }

  if (strcmp(cmd, "wifi reconnect") == 0) {
    if (!wifiEnabled) {
      Serial.println("wifi reconnect ignored: WiFi is OFF");
      return;
    }
    WiFi.disconnect(false, false);
    wifiReconnectNeeded = true;
    wifiScanRequested = true;
    wifiLastRetryMs = millis() - WIFI_RETRY_INTERVAL_MS;
    wifiState = WIFI_STATE_IDLE;
    Serial.println("WiFi reconnect requested");
    appendEventLog("WiFi reconnect requested");
    return;
  }

  if (strcmp(cmd, "wifi off") == 0) {
    wifiEnabled = false;
    wifiReconnectNeeded = false;
    wifiScanRequested = false;
    wifiManualScanRequested = false;
    routerConnected = false;
    fallbackApActive = false;
    wifiState = WIFI_STATE_OFF;
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    strncpy(wifiLastDisconnectReason, "wifi off", sizeof(wifiLastDisconnectReason) - 1);
    wifiLastDisconnectReason[sizeof(wifiLastDisconnectReason) - 1] = '\0';
    Serial.println("WiFi OFF. Vent control remains local.");
    appendEventLog("WiFi OFF. Vent control remains local.");
    return;
  }

  if (strcmp(cmd, "wifi on") == 0) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setHostname(WIFI_HOSTNAME);
    setupAccessPoint();
    wifiEnabled = true;
    wifiReconnectNeeded = true;
    wifiScanRequested = true;
    wifiScanForConnect = true;
    wifiState = WIFI_STATE_IDLE;
    wifiLastRetryMs = millis() - WIFI_RETRY_INTERVAL_MS;
    Serial.println("WiFi ON. Reconnect scheduled.");
    appendEventLog("WiFi ON. Reconnect scheduled.");
    return;
  }

  if (strcmp(cmd, "wifi scan") == 0) {
    if (!wifiEnabled) {
      Serial.println("wifi scan ignored: WiFi is OFF");
      return;
    }
    scanWiFiNetworks(false);
    appendEventLog("WiFi scan requested");
    return;
  }

  if (strcmp(cmd, "auto on") == 0) {
    if (globalEmergency) {
      Serial.println("auto on denied: global emergency is active");
      return;
    }
    autoMode = true;
    resetConfirmCounters(zone1);
    resetConfirmCounters(zone2);
    Serial.println("autoMode=ON");
    appendEventLog("Auto mode ON");
    return;
  }

  if (strcmp(cmd, "auto off") == 0) {
    autoMode = false;
    resetConfirmCounters(zone1);
    resetConfirmCounters(zone2);
    Serial.println("autoMode=OFF");
    appendEventLog("Auto mode OFF");
    return;
  }

  if (strcmp(cmd, "reset_alarm") == 0) {
    clearEmergency();
    return;
  }

  if (strcmp(cmd, "z1 open") == 0) {
    manualOpen(zone1);
    return;
  }

  if (strcmp(cmd, "z1 close") == 0) {
    manualClose(zone1);
    return;
  }

  if (strcmp(cmd, "z1 stop") == 0) {
    manualStop(zone1);
    return;
  }

  if (strcmp(cmd, "z1 reset") == 0) {
    resetZonePosition(zone1);
    return;
  }

  if (strcmp(cmd, "z2 open") == 0) {
    manualOpen(zone2);
    return;
  }

  if (strcmp(cmd, "z2 close") == 0) {
    manualClose(zone2);
    return;
  }

  if (strcmp(cmd, "z2 stop") == 0) {
    manualStop(zone2);
    return;
  }

  if (strcmp(cmd, "z2 reset") == 0) {
    resetZonePosition(zone2);
    return;
  }

  if (strcmp(cmd, "help") == 0) {
    printHelp();
    return;
  }

  Serial.println("Unknown command. Type 'help'.");
}

void serviceSerialInput() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      serialBuffer[serialLen] = '\0';
      if (serialLen > 0) {
        processCommand(serialBuffer);
      }
      serialLen = 0;
      continue;
    }

    if (serialLen < sizeof(serialBuffer) - 1) {
      serialBuffer[serialLen++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("greenhouse_vents_clean_test");
  Serial.println("WAVESHARE ESP32-S3 RELAY6CH TEST PORT");

  bootCounter++;
  const esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Reset reason: %s\n", resetReasonLabel(reason));
  Serial.printf("Boot counter: %d\n", bootCounter);
  setupEventLog();
  appendEventLogf("Boot #%d | reset=%s", bootCounter, resetReasonLabel(reason));

  setupPins();

  initZone(zone1, "Zone 1", PIN_DHT_1, PIN_OPEN_1, PIN_CLOSE_1, dht1);
  initZone(zone2, "Zone 2", PIN_DHT_2, PIN_OPEN_2, PIN_CLOSE_2, dht2);
  allRelaysOff();
  updateZoneMaxStep(zone1, zoneConfigs[0]);
  updateZoneMaxStep(zone2, zoneConfigs[1]);
  serviceMotor.state = SMS_IDLE;
  serviceMotor.activeDirection = DIR_NONE;
  serviceMotor.pendingDirection = DIR_NONE;
  serviceMotor.stateStartMs = 0;
  serviceMotor.runUntilMs = 0;
  setServiceMotorEvent("Очікування");

  dht1.begin();
  dht2.begin();

  setupWiFi();
  setupWebServer();
  readAnalogInputs(ANALOG_READ_MS);
  beginStartupCalibration();
  printHelp();
  printSystemStatus();
}

void loop() {
  server.handleClient();
  serviceSerialInput();
  const unsigned long now = millis();
  processWiFi(now);
  readAnalogInputs(now);
  readZoneDhts(now);
  updateServiceMotor(now);

  updateZone(zone1, now);
  updateZone(zone2, now);

  if (now - lastStatusPrintMs >= STATUS_PRINT_MS) {
    lastStatusPrintMs = now;
    printSystemStatus();
  }
}
