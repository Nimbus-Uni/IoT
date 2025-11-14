#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "base64.h"
#include <ArduinoJson.h>
#include <time.h>

#define CAMERA_MODEL_AI_THINKER

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define FLASH_LED_PIN 4

#define REED_PIN 13
#define PIR_PIN 12

const char* ssid = "Kelly";
const char* password = "28111981";
const char* firebaseURL = "https://esp32-a5949-default-rtdb.firebaseio.com";

char* state = "close";
String lastLog = "";
unsigned long lastMovement = 0;
unsigned long lastPhoto = 0;
int totalPhotos = 0;
bool isOpen = false;
bool hasAccess = true;
bool isMovement = false;
int lastPir = LOW;

void setup() {
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  pinMode(REED_PIN, INPUT_PULLUP);

  // Start Monitor Serial
  Serial.begin(115200);
  Serial.println("Iniciando ESP32-CAM...");

  // Configuration of the ESP32
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SXGA;
  config.jpeg_quality = 10;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Erro ao inicializar a câmera: 0x%x", err);
    return;
  }

  Serial.println("Câmera iniciada!");
  WiFi.begin(ssid, password);
  Serial.println("Conectando na Internet!");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("Wifi conectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Aguardando sincronização de tempo");
  while (time(nullptr) < 1000000000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nTempo sincronizado!");
}

void loop() {
  unsigned long time = millis();
  int reedState = digitalRead(REED_PIN);
  int pir = digitalRead(PIR_PIN);

  if (pir == HIGH && lastPir == LOW && hasAccess && state == "close") {
    Serial.println("Movimento detectado!");
    hasAccess = false;
    lastMovement = time;
    takePhoto("movement");
  }

  lastPir = pir;

  if (time - lastMovement >= 10000 && !hasAccess) {
    hasAccess = true;
  }

  if (reedState == HIGH && state == "close") {
    state = "open";
    isOpen = true;
    totalPhotos = 0;
  }

  if (reedState == LOW && isOpen) {
    state = "close";
    lastLog = "";
    totalPhotos = 0;
    isOpen = false;
    closeLog();
  }

  if (state == "open" && time - lastPhoto >= 5000) {
    lastPhoto = time;
    takePhoto("open");
  }

  delay(100);
}

void takePhoto(String type) {
  Serial.println("Tirando foto!");
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(200);
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Erro ao tirar foto!");
    return;
  }
  digitalWrite(FLASH_LED_PIN, LOW);
  Serial.println("Foto capturada com sucesso!");
  Serial.printf("Tamanho: %d bytes\n", fb->len);
  Serial.printf("Resolução: %dx%d pixels\n", fb->width, fb->height);
  Serial.printf("Formato: %s\n", (fb->format == PIXFORMAT_JPEG) ? "JPEG" : "Outro");
  sendFirebase(fb, type);
  esp_camera_fb_return(fb);
  Serial.println("Foto concluída!");
}

bool getLocation(float& lat, float& lon) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin("http://ip-api.com/json");
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      lat = doc["lat"];
      lon = doc["lon"];
      http.end();
      return true;
    }
  }
  http.end();
  return false;
}

String getDateAndTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Erro ao obter tempo";
  }

  char buffer[64];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void sendFirebase(camera_fb_t* fb, String type) {
  Serial.println("Enviando para o Firebase!");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi não conectado!");
    return;
  }

  float latitude = 0.0;
  float longitude = 0.0;
  bool hasLocation = getLocation(latitude, longitude);

  HTTPClient http;

  int httpCode = 0;

  if (type == "movement") {
    String url = String(firebaseURL) + "/logs.json";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    Serial.println("Convertendo foto para base64!");
    String imagemBase64 = base64::encode(fb->buf, fb->len);

    String dateTime = getDateAndTime();

    String json = "{";
    json += "\"timestamp\":" + String(millis()) + ",";
    json += "\"dataHora\":\"" + dateTime + "\",";
    json += "\"image\":\"data:image/jpeg;base64," + imagemBase64 + "\",";
    json += "\"status\":\"movement\"";

    if (hasLocation) {
      json += ",\"latitude\":" + String(latitude, 6);
      json += ",\"longitude\":" + String(longitude, 6);
    } else {
      json += ",\"latitude\":null,\"longitude\":null";
    }

    json += "}";
    httpCode = http.POST(json);

    if (httpCode > 0) {
      Serial.println("Foto enviada ao Firebase!");
      String response = http.getString();

      Serial.println("Resposta do Firebase: ");
      Serial.println(response);
    } else {
      Serial.println("Erro ao enviar foto para o Firebase!");
    }
  }

  if (type == "open" && totalPhotos == 0) {
    String url = String(firebaseURL) + "/logs.json";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    Serial.println("Convertendo foto para base64!");
    String imagemBase64 = base64::encode(fb->buf, fb->len);

    String dateTime = getDateAndTime();

    String json = "{";
    json += "\"timestamp\":" + String(millis()) + ",";
    json += "\"dataHora\":\"" + dateTime + "\",";
    json += "\"images\":[\"data:image/jpeg;base64," + imagemBase64 + "\"],";
    json += "\"status\":\"open\"";

    if (hasLocation) {
      json += ",\"latitude\":" + String(latitude, 6);
      json += ",\"longitude\":" + String(longitude, 6);
    } else {
      json += ",\"latitude\":null,\"longitude\":null";
    }

    json += "}";

    httpCode = http.POST(json);

    if (httpCode > 0) {
      Serial.println("Foto enviada ao Firebase!");
      String response = http.getString();

      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, response);

      if (!error) {
        const char* nameChar = doc["name"];
        lastLog = String(nameChar);
      } else {
        Serial.println("Erro ao parsear JSON!");
      }

      Serial.println("Resposta do Firebase: ");
      Serial.println(lastLog);
    } else {
      Serial.println("Erro ao enviar foto para o Firebase!");
    }

    http.end();
    totalPhotos++;
    return;
  }

  if (type == "open" && totalPhotos > 0) {
    String url = String(firebaseURL) + "/logs/" + lastLog + "/images.json";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    Serial.println("Convertendo foto para base64!");
    String imagemBase64 = base64::encode(fb->buf, fb->len);

    String json = "\"data:image/jpeg;base64," + imagemBase64 + "\"";

    httpCode = http.POST(json);

    if (httpCode > 0) {
      Serial.println("Foto enviada ao Firebase!");
      Serial.println(lastLog);
      if (httpCode == 200) {
        String response = http.getString();
        Serial.println("Resposta do Firebase: ");
        Serial.println(response);
      }
    } else {
      Serial.println("Erro ao enviar foto para o Firebase!");
    }
  }

  http.end();
  totalPhotos++;
  Serial.println("Envio 100% concluído!");
}

void closeLog() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi não conectado!");
    return;
  }

  HTTPClient http;

  int httpCode = 0;

  String url = String(firebaseURL) + "/logs.json";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String dateTime = getDateAndTime();

  float latitude = 0.0;
  float longitude = 0.0;
  bool hasLocation = getLocation(latitude, longitude);

  String json = "{";
  json += "\"timestamp\":" + String(millis()) + ",";
  json += "\"dataHora\":\"" + dateTime + "\",";
  json += "\"status\":\"close\"";

  if (hasLocation) {
    json += ",\"latitude\":" + String(latitude, 6);
    json += ",\"longitude\":" + String(longitude, 6);
  } else {
    json += ",\"latitude\":null,\"longitude\":null";
  }

  json += "}";

  httpCode = http.POST(json);


  if (httpCode > 0) {
    Serial.println("Foto enviada ao Firebase!");
    String response = http.getString();

    Serial.println("Resposta do Firebase: ");
    Serial.println(response);
  } else {
    Serial.println("Erro ao enviar foto para o Firebase!");
  }
}