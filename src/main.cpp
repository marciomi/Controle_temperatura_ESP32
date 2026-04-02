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

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
Attribute_Request<2U, MAX_ATTRIBUTES> attr_request;
Shared_Attribute_Update<3U, MAX_ATTRIBUTES> shared_update;

const std::array<IAPI_Implementation*, 2U> apis = {
  &attr_request,
  &shared_update
};

ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE, Default_Max_Stack_Size, apis);

constexpr int16_t telemetrySendInterval = 2000U;
uint32_t previousDataSend;

// ------------------- WhatsApp Hysteresis -------------------
unsigned long lastWhatsAppSend = 0;
const unsigned long whatsappInterval = 600000; // 10 minutos (10 * 60 * 1000 ms)
bool alertActive = false;
const float TEMP_HIGH = 25.0;
const float TEMP_LOW  = 24.5;

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
  }

  if (millis() - previousDataSend > telemetrySendInterval) {
    previousDataSend = millis();

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (!isnan(temperature)) {
      tb.sendTelemetryData("temperature", temperature);

      // Controle do LED com base na temperatura
      if (temperature > TEMP_HIGH) {
        digitalWrite(LED_PIN, HIGH); // Acende o LED
      } else {
        digitalWrite(LED_PIN, LOW);  // Apaga o LED
      }
    }

    if (!isnan(humidity)) {
      tb.sendTelemetryData("humidity", humidity);
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