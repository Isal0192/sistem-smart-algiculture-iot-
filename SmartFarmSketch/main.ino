#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <WebServer.h>
#include <Preferences.h>

// ======================================================
// CONFIG & STORAGE
// ======================================================
Preferences preferences;
WebServer server(80);

String ssid = "";
String password = "";
bool isAPMode = false;

// ======================================================
// HIVEMQ CLOUD
// ======================================================
const char* mqtt_server = "";
const int mqtt_port = ;

const char* mqtt_user = "";
const char* mqtt_password = "";

// ======================================================
// PIN
// ======================================================
#define SDA_PIN      21
#define SCL_PIN      22

#define MQ_PIN       19
#define BUZZER_PIN   17

// PIN RELAY 8 CHANNEL
const int RELAY_PINS[8] = {23, 4, 2, 15, 27, 14, 12, 13};

// ======================================================
// OLED
// ======================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ======================================================
// SENSOR
// ======================================================
Adafruit_AHTX0 aht;

// ======================================================
// MQTT
// ======================================================
WiFiClientSecure espClient;
PubSubClient client(espClient);

// ======================================================
// VARIABLE
// ======================================================
float temperature = 0;
float humidity = 0;

bool fireDetected = false;
bool relayStates[8] = {false, false, false, false, false, false, false, false};

unsigned long lastPublish = 0;

// ======================================================
// HTTP HANDLERS FOR WEB SERVER
// ======================================================
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial; text-align:center; margin-top:50px;} input{padding:10px; width:80%; margin:10px 0;} button{padding:10px 20px; background-color:#4CAF50; color:white; border:none; cursor:pointer;}</style></head>";
  html += "<body><h2>UAS IoT - Wi-Fi Setup</h2>";
  html += "<form action='/save' method='POST'>";
  html += "<input type='text' name='ssid' placeholder='SSID / Nama WiFi' required><br>";
  html += "<input type='password' name='password' placeholder='Password'><br>";
  html += "<button type='submit'>Save & Connect</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid")) {
    String newSsid = server.arg("ssid");
    String newPass = server.arg("password");

    preferences.begin("wifi-config", false);
    preferences.putString("ssid", newSsid);
    preferences.putString("password", newPass);
    preferences.end();

    String html = "<html><body><h2>Konfigurasi Tersimpan!</h2><p>ESP32 sedang restart untuk mencoba koneksi baru...</p></body></html>";
    server.send(200, "text/html", html);
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// ======================================================
// WIFI MANAGEMENT WITH 20s TIMEOUT
// ======================================================
void setupWiFiManager() {
  preferences.begin("wifi-config", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();

  if (ssid == "") {
    Serial.println("Belum ada konfigurasi Wi-Fi. Masuk ke mode AP.");
    startAPMode();
    return;
  }

  Serial.println("\nMencoba tersambung ke Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Connecting WiFi...");
    display.print("Time: "); display.print((millis() - startAttemptTime)/1000); display.println("s / 20s");
    display.display();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Terkoneksi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    isAPMode = false;
  } else {
    Serial.println("\nGagal konek dalam 20 detik! Mengaktifkan Hotspot...");
    startAPMode();
  }
}

void startAPMode() {
  isAPMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("UAS IoT", ""); 
  
  Serial.println("Hotspot Berhasil Dibuat!");
  Serial.print("SSID: UAS IoT\nIP Portal: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Web Server Configuration Ready.");
}

// ======================================================
// MQTT CALLBACK (Logika Active LOW)
// ======================================================
void callback(char* topic, byte* payload, unsigned int length)
{
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.println("\n===== MQTT MESSAGE =====");
  Serial.print("Topic : "); Serial.println(topic);
  Serial.print("Data  : "); Serial.println(message);

  String topicStr = String(topic);
  
  for (int i = 0; i < 8; i++) {
    String targetTopic = "smartfarm/relay" + String(i + 1);
    if (topicStr == targetTopic) {
      relayStates[i] = (message == "ON");
      
      // LOGIKA ACTIVE LOW: Jika status true (ON), kirim LOW untuk menyalakan relay fisiknya.
      digitalWrite(RELAY_PINS[i], relayStates[i] ? LOW : HIGH);
      break;
    }
  }
}

// ======================================================
// MQTT CONNECTION
// ======================================================
void connectMQTT()
{
  if (isAPMode) return; 

  while (!client.connected()) {
    if (WiFi.status() != WL_CONNECTED) return;

    Serial.println("\nConnecting MQTT...");
    String clientId = "ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("MQTT CONNECTED");
      
      for (int i = 1; i <= 8; i++) {
        String topic = "smartfarm/relay" + String(i);
        client.subscribe(topic.c_str());
      }
    } else {
      Serial.print("MQTT FAILED : ");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// ======================================================
// CONTROL VIA SERIAL MONITOR
// ======================================================
void checkSerialControl() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim(); // Menghapus spasi atau karakter newline tambahan
    input.toUpperCase(); // Membuat karakter menjadi huruf besar semua

    // Format perintah diharapkan: R1ON, R1OFF, R2ON, dst.
    if (input.startsWith("R") && input.length() >= 3) {
      int relayNum = input.substring(1, 2).toInt(); // Mengambil info nomor relay (1-8)
      String command = input.substring(2);         // Mengambil info perintah (ON/OFF)

      if (relayNum >= 1 && relayNum <= 8) {
        int index = relayNum - 1;
        if (command == "ON") {
          relayStates[index] = true;
          digitalWrite(RELAY_PINS[index], LOW); // Active LOW -> LOW = ON
          Serial.printf("Serial Command: Relay %d diubah ke ON\n", relayNum);
        } else if (command == "OFF") {
          relayStates[index] = false;
          digitalWrite(RELAY_PINS[index], HIGH); // Active LOW -> HIGH = OFF
          Serial.printf("Serial Command: Relay %d diubah ke OFF\n", relayNum);
        } else {
          Serial.println("Perintah tidak dikenal! Gunakan ON atau OFF (Contoh: R1ON / R1OFF)");
        }
      } else {
        Serial.println("Nomor Relay salah! Masukkan rentang R1 sampai R8.");
      }
    }
  }
}

// ======================================================
// OLED DISPLAY
// ======================================================
void drawOLED()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  if (isAPMode) {
    display.setCursor(0, 0);
    display.println("=== AP AP_MODE ===");
    display.setCursor(0, 16);
    display.println("Connect to WiFi:");
    display.println("SSID: UAS IoT");
    display.setCursor(0, 40);
    display.println("Buka Browser ke IP:");
    display.println("192.168.4.1");
  } else {
    display.setCursor(0, 0);
    display.print("T:"); display.print(temperature, 1); display.print("C");
    display.setCursor(64, 0);
    display.print("H:"); display.print(humidity, 0); display.print("%");

    // Menampilkan 1 jika status ON, dan 0 jika status OFF di layar
    display.setCursor(0, 14);
    display.printf("R1:%d R2:%d R3:%d R4:%d", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
    display.setCursor(0, 26);
    display.printf("R5:%d R6:%d R7:%d R8:%d", relayStates[4], relayStates[5], relayStates[6], relayStates[7]);

    display.setCursor(0, 40);
    display.print("Fire Alert: "); display.print(fireDetected ? "YES" : "NO");

    display.setCursor(0, 54);
    display.print("WF:"); display.print(WiFi.status() == WL_CONNECTED ? "OK" : "ERR");
    display.setCursor(64, 54);
    display.print("MQ:"); display.print(client.connected() ? "OK" : "ERR");
  }
  display.display();
}

// ======================================================
// SETUP
// ======================================================
void setup()
{
  Serial.begin(115200);
  Serial.println("\n===== SMART FARM =====");

  pinMode(MQ_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Inisialisasi seluruh Pin Relay sebagai OUTPUT
  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    // LOGIKA ACTIVE LOW: Saat pertama kali nyala, berikan HIGH agar semua relay mati dahulu
    digitalWrite(RELAY_PINS[i], HIGH); 
  }

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAILED");
    while (1);
  }

  if (!aht.begin()) {
    Serial.println("AHT10 FAILED");
    while (1);
  }

  setupWiFiManager();

  if (!isAPMode) {
    espClient.setInsecure();
    client.setBufferSize(1024);
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    connectMQTT();
  }

  Serial.println("SYSTEM READY");
}

// ======================================================
// LOOP
// ======================================================
void loop()
{
  checkSerialControl();
 
  if (isAPMode) {
    server.handleClient();
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      startAPMode();
      return;
    }

    if (!client.connected()) {
      connectMQTT();
    }

    client.loop();

    sensors_event_t hum;
    sensors_event_t temp;
    aht.getEvent(&hum, &temp);
    temperature = temp.temperature;
    humidity = hum.relative_humidity;

    fireDetected = (digitalRead(MQ_PIN) == LOW);
    digitalWrite(BUZZER_PIN, fireDetected ? HIGH : LOW);

    if (millis() - lastPublish > 3000) {
      lastPublish = millis();

      char tempStr[10];
      char humStr[10];
      dtostrf(temperature, 4, 1, tempStr);
      dtostrf(humidity, 4, 1, humStr);

      client.publish("smartfarm/temperature", tempStr, true);
      client.publish("smartfarm/humidity", humStr, true);
      client.publish("smartfarm/fire", fireDetected ? "ON" : "OFF", true);
    }
  }

  drawOLED();
  delay(100);
}
