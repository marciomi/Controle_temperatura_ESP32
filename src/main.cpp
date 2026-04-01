#if defined(ESP8266)
#include <ESP8266WiFi.h>
#define THINGSBOARD_ENABLE_PROGMEM 0
#elif defined(ESP32) || defined(RASPBERRYPI_PICO) || defined(RASPBERRYPI_PICO_W)
#include <WiFi.h>
#endif

#ifndef LED_BUILTIN
#define LED_BUILTIN 99
#endif

#include <Arduino_MQTT_Client.h>
#include <Server_Side_RPC.h>
#include <Attribute_Request.h>
#include <Shared_Attribute_Update.h>
#include <ThingsBoard.h>

#include <HTTPClient.h>
#include <UrlEncode.h>

// ------------------- WhatsApp -------------------
String phoneNumber = "+5511971702852";
String apiKey = "3552849";

void sendMessage(String message) {
  String url = "https://api.callmebot.com/whatsapp.php?phone=" + phoneNumber +
               "&apikey=" + apiKey + "&text=" + urlEncode(message);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpResponseCode = http.POST(url);

  if (httpResponseCode == 200) {
    Serial.println("Mensagem enviada com sucesso");
  } else {
    Serial.println("Erro no envio da mensagem");
    Serial.print("HTTP response code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

// ------------------- DHT22 -------------------
#include <DHT.h>
#define DHTPIN 4
#define DHTTYPE DHT22
#define LED_PIN 2
DHT dht(DHTPIN, DHTTYPE);

// ------------------- WiFi / ThingsBoard -------------------
constexpr char WIFI_SSID[] = "Wokwi-GUEST";
constexpr char WIFI_PASSWORD[] = "";
constexpr char TOKEN[] = "jclbdbg1ln3gdhdwi97b";
constexpr char THINGSBOARD_SERVER[] = "52.73.3.142";
constexpr uint16_t THINGSBOARD_PORT = 1883U;
constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;
constexpr size_t MAX_ATTRIBUTES = 3U;
constexpr uint64_t REQUEST_TIMEOUT_MICROSECONDS = 5000U * 1000U;

constexpr const char BLINKING_INTERVAL_ATTR[] = "blinkingInterval";
constexpr const char LED_MODE_ATTR[] = "ledMode";
constexpr const char LED_STATE_ATTR[] = "ledState";

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
Server_Side_RPC<3U, 5U> rpc;
Attribute_Request<2U, MAX_ATTRIBUTES> attr_request;
Shared_Attribute_Update<3U, MAX_ATTRIBUTES> shared_update;

const std::array<IAPI_Implementation*, 3U> apis = {
  &rpc,
  &attr_request,
  &shared_update
};

ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE, Default_Max_Stack_Size, apis);

volatile bool attributesChanged = false;
volatile int ledMode = 0;
volatile bool ledState = false;
volatile uint16_t blinkingInterval = 1000U;

uint32_t previousStateChange;
constexpr int16_t telemetrySendInterval = 2000U;
uint32_t previousDataSend;

// ------------------- WhatsApp Hysteresis -------------------
unsigned long lastWhatsAppSend = 0;
const unsigned long whatsappInterval = 20000; // 20s
bool alertActive = false;
const float TEMP_HIGH = 25.0;
const float TEMP_LOW  = 24.5;

// ------------------- RPC CALLBACK -------------------
void processSetLedMode(const JsonVariantConst &data, JsonDocument &response) {
  int new_mode = data;
  StaticJsonDocument<1> resp;
  if (new_mode != 0 && new_mode != 1) {
    resp["error"] = "Unknown mode!";
    response.set(resp);
    return;
  }
  ledMode = new_mode;
  attributesChanged = true;
  resp["newMode"] = (int)ledMode;
  response.set(resp);
}

const std::array<RPC_Callback, 1U> callbacks = {
  RPC_Callback{ "setLedMode", processSetLedMode }
};

// ------------------- WiFi -------------------
void InitWiFi() {
  Serial.println("Connecting to AP ...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to AP");
}

const bool reconnect() {
  if (WiFi.status() == WL_CONNECTED) return true;
  InitWiFi();
  return true;
}

// ------------------- SETUP -------------------
void setup() {
  Serial.begin(SERIAL_DEBUG_BAUD);
  pinMode(LED_PIN, OUTPUT);
  dht.begin();
  delay(1000);
  InitWiFi();
}

// ------------------- LOOP -------------------
void loop() {
  delay(10);

  if (!reconnect()) return;

  if (!tb.connected()) {
    if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) return;

    tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());

    Serial.println("Subscribing RPC...");
    rpc.RPC_Subscribe(callbacks.cbegin(), callbacks.cend());
  }

  if (millis() - previousDataSend > telemetrySendInterval) {
    previousDataSend = millis();

    float temperature = dht.readTemperature();

    if (!isnan(temperature)) {
      tb.sendTelemetryData("temperature", temperature);
    }

    // ------------------- ALERTA WHATSAPP COM HISTERESI -------------------
    if (!isnan(temperature)) {

      if (temperature > TEMP_HIGH && !alertActive) {
        alertActive = true;
        lastWhatsAppSend = 0;
      }

// --- NORMALIZAÇÃO ---
if (temperature < TEMP_LOW && alertActive) {

    sendMessage("Temperatura da sala do servidor dentro dos padroes.");
    delay(1500);  // evita erro 203 por envio duplo

    Serial.println("Mensagem de normalizacao enviada via WhatsApp!");

    alertActive = false;
}


// --- ALERTA ---
if (alertActive && millis() - lastWhatsAppSend >= whatsappInterval) {

    String msg = "Temperatura da sala do servidor acima do esperado. Temperatura atual ";
    msg += String(temperature, 1);
    msg += " C";

    sendMessage(msg);
    delay(1500);  // evita erro 203 por envio repetido

    lastWhatsAppSend = millis();

    Serial.println("Alerta de temperatura enviado via WhatsApp!");
}
    }
  }

  tb.loop();
}