/*************************************************************************
 *  LinkX Weather Station — DHT22 Live Temp/Humidity Dashboard
 *  Board   : Gbro STEM AI Robotics ESP32-S3 (LinkX Dev Board)
 *
 *  FEATURES
 *   - AP dashboard @ 192.168.4.1, WebSocket live readings (~every 3s)
 *   - Self-contained canvas line chart (no external CDN — works fully
 *     offline in AP-only mode, unlike a Chart.js CDN import would)
 *   - Data logged to LittleFS every 5 minutes, downloadable as CSV
 *   - RGB "comfort zone" indicator (cold/comfortable/hot) + buzzer
 *     alarm on extreme readings
 *   - Sensor error handling (NaN/read-failure retry + dashboard badge)
 *   - OTA firmware updates
 *
 *  LIBRARIES REQUIRED: ESPAsyncWebServer, AsyncTCP, ArduinoJson, LittleFS,
 *                       ArduinoOTA, DHT sensor library (by Adafruit) +
 *                       Adafruit Unified Sensor (dependency)
 *  SETUP: Upload /data to LittleFS BEFORE flashing this sketch.
 *
 *  WIRING (DHT22):
 *   VCC -> 3.3V   GND -> GND   DATA -> GPIO35
 *   (add a 10k pull-up resistor between DATA and VCC if your module
 *   doesn't already have one built in)
 *************************************************************************/

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <DHT.h>

// ================= WIFI CONFIG =================
const char* AP_SSID     = "LinkX-WeatherStation";
const char* AP_PASSWORD = "12345678";
const char* STA_SSID     = "";
const char* STA_PASSWORD = "";

// ================= RGB + BUZZER =================
#define RED_PIN   45
#define GREEN_PIN 48
#define BLUE_PIN  47
#define BUZZER    14

// ================= DHT22 =================
#define DHT_PIN 35
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

const int pwmFreq = 5000;
const int pwmResolution = 8;

// Comfort-zone thresholds (Celsius) - adjust to your climate/preference
const float COLD_BELOW_C = 15.0;
const float HOT_ABOVE_C  = 28.0;
const float ALARM_TEMP_C = 38.0;    // extreme heat alarm
const float ALARM_HUMIDITY = 88.0;  // extreme humidity alarm

// ================= STATE =================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

float currentTemp = NAN, currentHum = NAN;
int consecutiveFailures = 0;
bool sensorError = false;

unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL_MS = 3000;        // DHT22 needs >=2s between reads
unsigned long lastLogTime = 0;
const unsigned long LOG_INTERVAL_MS = 5UL * 60UL * 1000UL;   // 5 minutes

// ================= RGB / BUZZER =================
void setRGB(int r,int g,int b){ ledcWrite(RED_PIN,r); ledcWrite(GREEN_PIN,g); ledcWrite(BLUE_PIN,b); }
void rgbCold(){ setRGB(0,20,90); }
void rgbComfortable(){ setRGB(0,60,0); }
void rgbHot(){ setRGB(120,20,0); }
void rgbError(){ setRGB(150,90,0); }
void rgbBoot(){ setRGB(80,0,80); }
void beep(int times,int onMs,int offMs){
  for(int i=0;i<times;i++){ digitalWrite(BUZZER,HIGH); delay(onMs); digitalWrite(BUZZER,LOW); if(i<times-1) delay(offMs); }
}

// ================= LOGGING =================
void logEvent(const String &text){
  File f = LittleFS.open("/log.csv","a");
  if(!f) return;
  if(f.size() > 200000){
    f.close();
    LittleFS.remove("/log_old.csv");
    LittleFS.rename("/log.csv","/log_old.csv");
    f = LittleFS.open("/log.csv","a");
  }
  f.printf("%lu,%s\n", millis(), text.c_str());
  f.close();
}
void broadcastEvent(const String &text){
  logEvent(text);
  StaticJsonDocument<128> doc;
  doc["event"] = text;
  String out; serializeJson(doc, out);
  ws.textAll(out);
}
void logWeatherReading(float t, float h){
  File f = LittleFS.open("/weather_log.csv","a");
  if(!f) return;
  if(f.size() > 500000){   // rotate at 500KB (this file grows slowly - one row per 5 min)
    f.close();
    LittleFS.remove("/weather_log_old.csv");
    LittleFS.rename("/weather_log.csv","/weather_log_old.csv");
    f = LittleFS.open("/weather_log.csv","a");
  }
  f.printf("%lu,%.1f,%.1f\n", millis(), t, h);
  f.close();
}

// ================= SENSOR READ + COMFORT/ALARM LOGIC =================
void readSensor(){
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if(isnan(t) || isnan(h)){
    consecutiveFailures++;
    if(consecutiveFailures == 3){    // only alert after repeated failures, avoids false alarms on one glitch
      sensorError = true;
      rgbError();
      broadcastEvent("DHT22 read failed 3x in a row - check wiring");
    }
    return;   // keep last good reading displayed rather than showing garbage
  }

  consecutiveFailures = 0;
  if(sensorError){
    sensorError = false;
    broadcastEvent("DHT22 recovered - readings normal again");
  }

  currentTemp = t;
  currentHum = h;

  // Comfort-zone RGB
  if(t < COLD_BELOW_C) rgbCold();
  else if(t > HOT_ABOVE_C) rgbHot();
  else rgbComfortable();

  // Extreme-condition alarm
  if(t > ALARM_TEMP_C || h > ALARM_HUMIDITY){
    beep(3,100,100);
    broadcastEvent("ALERT: extreme reading " + String(t,1) + "C / " + String(h,0) + "%");
  }
}

// ================= WEBSOCKET =================
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
  // This dashboard is read-only (no drive/motor commands), so no message handler needed beyond connect logging
  if(type == WS_EVT_CONNECT){
    Serial.printf("[WS] client #%u connected\n", client->id());
  }
}

// ================= SETUP =================
void setup(){
  Serial.begin(115200);
  delay(200);

  pinMode(BUZZER,OUTPUT);
  ledcAttach(RED_PIN,pwmFreq,pwmResolution);
  ledcAttach(GREEN_PIN,pwmFreq,pwmResolution);
  ledcAttach(BLUE_PIN,pwmFreq,pwmResolution);
  rgbBoot();

  dht.begin();

  if(!LittleFS.begin(true)){
    Serial.println("[ERR] LittleFS mount failed");
  } else {
    if(!LittleFS.exists("/log.csv")){ File f=LittleFS.open("/log.csv","w"); if(f){f.println("millis,event"); f.close();} }
    if(!LittleFS.exists("/weather_log.csv")){ File f=LittleFS.open("/weather_log.csv","w"); if(f){f.println("millis,temp_c,humidity_pct"); f.close();} }
  }

  WiFi.mode(strlen(STA_SSID) ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("[AP] IP: "); Serial.println(WiFi.softAPIP());
  if(strlen(STA_SSID)){
    WiFi.begin(STA_SSID, STA_PASSWORD);
    unsigned long start = millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-start<8000) delay(300);
  }

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.serveStatic("/log.csv", LittleFS, "/log.csv");
  server.serveStatic("/weather_log.csv", LittleFS, "/weather_log.csv");
  server.onNotFound([](AsyncWebServerRequest *r){ r->send(404,"text/plain","Not found"); });
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();

  ArduinoOTA.setHostname("linkx-weatherstation");
  ArduinoOTA.onStart([](){ rgbBoot(); });
  ArduinoOTA.begin();

  beep(2,60,80);
  logEvent("Boot complete");
  Serial.println("Connect to WiFi \"LinkX-WeatherStation\" and open http://192.168.4.1");

  // Take one reading immediately so the dashboard isn't empty on first load
  delay(2000);   // DHT22 needs a moment after power-up before its first valid read
  readSensor();
  lastReadTime = millis();
  lastLogTime = millis();
}

// ================= LOOP =================
void loop(){
  ArduinoOTA.handle();
  ws.cleanupClients();
  unsigned long now = millis();

  if(now - lastReadTime >= READ_INTERVAL_MS){
    lastReadTime = now;
    readSensor();

    if(!isnan(currentTemp)){
      StaticJsonDocument<128> doc;
      doc["temp"] = currentTemp;
      doc["hum"] = currentHum;
      String out; serializeJson(doc, out);
      ws.textAll(out);
    } else {
      StaticJsonDocument<32> doc;
      doc["error"] = true;
      String out; serializeJson(doc, out);
      ws.textAll(out);
    }
  }

  if(now - lastLogTime >= LOG_INTERVAL_MS){
    lastLogTime = now;
    if(!isnan(currentTemp)){
      logWeatherReading(currentTemp, currentHum);
      broadcastEvent("Logged reading: " + String(currentTemp,1) + "C / " + String(currentHum,0) + "%");
    }
  }
}
