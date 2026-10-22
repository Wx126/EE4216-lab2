#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "lab_page.h"


const bool USE_AP_MODE = true;

const char* AP_SSID     = "A0307433L";    
const char* AP_PASSWORD = "lab2pass123";  

//use sta as an alternative way
const char* STA_SSID     = "A0307433L";
const char* STA_PASSWORD = "lab2task3";
const unsigned long STA_TIMEOUT_MS = 15000;

//my pin configuration from lab 1
const int TRIG_PIN = 4;
const int ECHO_PIN = 5;   
const int LED_PIN  = 6;
const int NUM_LEDS = 8;

const unsigned long ECHO_TIMEOUT_US = 30000;  
const unsigned long MIN_PING_GAP_MS = 60;     

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

//each element in the array is 1 led respectively. if we want to change one only then we just need to modify only one element in the array.
struct LedState {
  uint8_t r, g, b, brightness;
};
LedState leds[NUM_LEDS];

unsigned long lastPingMs = 0;


void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

//task3.3 d, to include error message
void sendError(int code, const char* msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(code, doc);
}

void addLedState(JsonObject obj, int id) {
  obj["id"]         = id;
  obj["r"]          = leds[id].r;
  obj["g"]          = leds[id].g;
  obj["b"]          = leds[id].b;
  obj["brightness"] = leds[id].brightness;
}

void applyLeds() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t br = leds[i].brightness;
    strip.setPixelColor(i,leds[i].r * br / 255,leds[i].g * br / 255,leds[i].b * br / 255);
  }
  strip.show();
}

float measureDistanceCm() {
  unsigned long since = millis() - lastPingMs;
  if (since < MIN_PING_GAP_MS) delay(MIN_PING_GAP_MS - since);
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);   
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long us = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  lastPingMs = millis();

  if (us == 0) return -1;         
  return us * 0.0343f / 2.0f;      
}

bool readChannel(JsonDocument& body, const char* key, uint8_t& out) {
  JsonVariant v = body[key];
  if (!v.is<int>()) return false;  
  int n = v.as<int>();
  if (n < 0 || n > 255) return false;
  out = (uint8_t)n;
  return true;
}

void handleRoot() {
  server.send_P(200, "text/html", LAB_PAGE);
}

void handleDistance() {
  float d = measureDistanceCm();
  JsonDocument doc;
  if (d < 0) {
    doc["distance_cm"] = nullptr;
    doc["valid"] = false;
  } 
  else {
    doc["distance_cm"] = round(d * 10.0) / 10.0;  
    doc["valid"] = true;
  }
  sendJson(200, doc);
}

void handleGetLeds() {
  JsonDocument doc;
  JsonArray arr = doc["leds"].to<JsonArray>();
  for (int i = 0; i < NUM_LEDS; i++) {
    addLedState(arr.add<JsonObject>(), i);
  }
  sendJson(200, doc);
}

void handlePutLed() {
  String idStr = server.pathArg(0);
  if (idStr.length() != 1 || idStr[0] < '0' || idStr[0] > '7') {
    sendError(404, "LED id must be 0-7");
    return;
  }
  int id = idStr[0] - '0';

  if (!server.hasArg("plain")) {
    sendError(400, "Missing JSON body");
    return;
  }
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err || !body.is<JsonObject>()) {
    sendError(400, "Body must be a JSON object");
    return;
  }

  LedState next;
  if (!readChannel(body, "r", next.r) ||
      !readChannel(body, "g", next.g) ||
      !readChannel(body, "b", next.b) ||
      !readChannel(body, "brightness", next.brightness)) {
    sendError(400, "r, g, b and brightness must be integers from 0 to 255");
    return;
  }

  leds[id] = next;   
  applyLeds();      

  JsonDocument doc;
  addLedState(doc.to<JsonObject>(), id);
  sendJson(200, doc);
}

void handleNotFound() {
  sendError(404, "Not found");
}

//wifi
void startAP() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 1, false, 2)) {
    Serial.println("Failed to start AP (password must be at least 8 characters)");
    return;
  }
  Serial.print("AP started. SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Open http://");
  Serial.println(WiFi.softAPIP());
}

bool startSTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASSWORD);
  Serial.print("Connecting to ");
  Serial.print(STA_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection failed.");
    WiFi.disconnect();
    return false;
  }
  Serial.print("Connected. Open http://");
  Serial.println(WiFi.localIP());
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  //initialise with black first
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = {0, 0, 0, 128};
  }
  strip.begin();
  applyLeds();

  if (USE_AP_MODE) {
    startAP();
  } else if (!startSTA()) {
    Serial.println("Falling back to AP mode.");
    startAP();
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/distance", HTTP_GET, handleDistance);
  server.on("/api/leds", HTTP_GET, handleGetLeds);
  server.on(UriBraces("/api/leds/{}"), HTTP_PUT, handlePutLed);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();  
  delay(2);
}
