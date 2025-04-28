// ESP32 DATA LOGGER with Modbus RTU, SD Card, and Web Interface
// For ESP32-WROOM-32E; 20250428
/*
다음 내용으로 DATA LOGGER 8MB, 아두이노 코드와 웹앱 프로그램 작성해줘
CPU : ESP32-WROOM-32E, NTP TIME 동기화
Modbus RTU interface : RS485, 9600 BPS, (RW-GPIO 4, RX-GPIO 16, TX-17), Delta DTK Temperature controller(ID:1, REGISTER:0x1000)
MicroSD Card Reader : MicroSD Card Reader SPI interface(CS-GPIO 5, SCK-GPIO 18, MISO-GPIO 19, MOSI-GPIO23, CD-GPIO 8) date, time, temperature 로그 기록
microSD카드에 데이터 저장(날자,시간,온도) 파일명 : LOG%04d%02d.CSV (LOGyyyymm.CSV) 월별로 파일 생성, header 텍스트는( Date, Time, Temperature ) 새 파일명 작성시 생성
LED-GPIO 2, BUZZER-GPIO 7
Web page1에서 WiFi ID/PASSWORD 로 인터넷 네트워크에 접속하도록 로그인 관리
Web page2에서 저장 주기 설정(1~60분): 디폴트 10분, 알람 온도 설정(예:-150도 이상이면 알람), 알람 발생시 DISCORD와 EMAIL로 푸시 알람,
Web page3에서 2시간~1일(저장주기에 따라 변동) 온도 그래프(-200~50도) 실시간으로 웹페이지에 표시, 로그 데이터 월별로 csv파일로 다운로드 기능, 알람 발생 내역(날자,시간,온도) 리스트 관리, 선택 삭제 가능
*/
/*
 * ESP32 Cryogenic Temperature Data Logger
 * 
 * Hardware:
 * - ESP32-WROOM-32E
 * - RS485 interface connected to Delta DTK Temperature controller
 * - MicroSD Card Reader (SPI)
 * - Status LED on GPIO 2
 * - Buzzer on GPIO 7
 * 
 * Features:
 * - Reads temperature data via Modbus RTU from Delta DTK controller
 * - Logs data to microSD card in CSV format (monthly files)
 * - Web interface for configuration and monitoring
 * - NTP time synchronization (Seoul timezone)
 * - Alarm notifications via Discord webhook and/or email
 */

#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ModbusRTU.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <ArduinoJson.h>

// Pin definitions
#define RS485_RW_PIN 4
#define RS485_RX_PIN 16
#define RS485_TX_PIN 17
#define SD_CS_PIN 5
#define SD_SCK_PIN 18
#define SD_MISO_PIN 19
#define SD_MOSI_PIN 23
#define SD_CD_PIN 8
#define LED_PIN 2
#define BUZZER_PIN 7

// Constants
#define MODBUS_ID 1
#define TEMPERATURE_REGISTER 0x1000
#define LOG_INTERVAL_DEFAULT 10 // Minutes
#define ALARM_TEMP_DEFAULT -150.0 // Celsius

// Global variables
AsyncWebServer server(80);
ModbusRTU modbus;
HardwareSerial modbusSerial(1);

struct {
  int logInterval = LOG_INTERVAL_DEFAULT;
  float alarmTemp = ALARM_TEMP_DEFAULT;
  String discordWebhook = "";
  String emailAddress = "";
  String emailPassword = "";
  String smtpServer = "smtp.gmail.com";
  int smtpPort = 465;
  String wifiSSID = "";
  String wifiPassword = "";
} settings;

struct {
  float temperature = -196.0;
  bool alarm = false;
  float temperatureHistory[120]; // 2 hours of data at 1-minute resolution
  int historyIndex = 0;
} sensorData;

struct AlarmEvent {
  String date;
  String time;
  float temperature;
};

std::vector<AlarmEvent> alarmEvents;

// Function declarations
void setupWiFi();
void setupModbus();
void setupSD();
void setupSPIFFS();
void setupServer();
void setupNTP();
bool readTemperature();
void checkAlarm();
void logData();
void updateTemperatureHistory();
void saveSettings();
void loadSettings();
String getFormattedDate();
String getFormattedTime();
String getCurrentMonthFileName();
bool createLogFileIfNeeded(const String& filename);
void notifyAlarm();

void setup() {
  Serial.begin(115200);
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  setupSD();
  setupSPIFFS();
  loadSettings();
  setupModbus();
  setupWiFi();
  setupNTP();
  setupServer();
  
  // Initialize temperature history with current temp
  for (int i = 0; i < 120; i++) {
    sensorData.temperatureHistory[i] = -196.0;
  }
  
  // Initial temperature reading
  readTemperature();
}

void loop() {
  static unsigned long lastReadTime = 0;
  static unsigned long lastLogTime = 0;
  static unsigned long lastHistoryUpdateTime = 0;
  unsigned long currentMillis = millis();
  
  // Read temperature every 5 seconds
  if (currentMillis - lastReadTime >= 5000) {
    lastReadTime = currentMillis;
    
    if (readTemperature()) {
      checkAlarm();
    }
  }
  
  // Update temperature history every minute
  if (currentMillis - lastHistoryUpdateTime >= 60000) {
    lastHistoryUpdateTime = currentMillis;
    updateTemperatureHistory();
  }
  
  // Log data based on set interval
  if (currentMillis - lastLogTime >= settings.logInterval * 60000UL) {
    lastLogTime = currentMillis;
    logData();
  }
  
  // Poll for modbus messages
  modbus.task();
  yield();
}

void setupWiFi() {
  if (settings.wifiSSID.length() > 0) {
    WiFi.begin(settings.wifiSSID.c_str(), settings.wifiPassword.c_str());
    
    int attempts = 0;
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nFailed to connect to WiFi");
      WiFi.disconnect();
    }
  } else {
    Serial.println("No WiFi credentials set");
  }
}

void setupModbus() {
  modbusSerial.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  modbus.begin(&modbusSerial, RS485_RW_PIN);
  modbus.master();
}

void setupSD() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed!");
    return;
  }
  
  Serial.println("SD Card initialized successfully");
}

void setupSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed!");
    return;
  }
  
  // Check if our web app file exists and copy from index.html.txt if not
  if (!SPIFFS.exists("/index.html")) {
    Serial.println("Copying web app to SPIFFS...");
    
    File sourceFile = SD.open("/index.html.txt", FILE_READ);
    if (sourceFile) {
      File destFile = SPIFFS.open("/index.html", FILE_WRITE);
      if (destFile) {
        while (sourceFile.available()) {
          destFile.write(sourceFile.read());
        }
        destFile.close();
      }
      sourceFile.close();
    }
  }
  
  Serial.println("SPIFFS initialized successfully");
}

void setupNTP() {
  configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // UTC+9 for Seoul time
  
  Serial.println("Waiting for NTP time sync");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synchronized");
}

void setupServer() {
  // Serve static files from SPIFFS
  server.serveStatic("/", SPIFFS, "/index.html");
  
  // API endpoints
  server.on("/save_wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      settings.wifiSSID = request->getParam("ssid", true)->value();
      settings.wifiPassword = request->getParam("password", true)->value();
      saveSettings();
      
      request->send(200, "text/plain", "WiFi settings saved");
      
      // Restart ESP to apply new WiFi settings
      delay(1000);
      ESP.restart();
    } else {
      request->send(400, "text/plain", "Missing required parameters");
    }
  });
  
  server.on("/save_settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("logInterval", true) && request->hasParam("alarmTemp", true)) {
      settings.logInterval = request->getParam("logInterval", true)->value().toInt();
      settings.alarmTemp = request->getParam("alarmTemp", true)->value().toFloat();
      
      if (request->hasParam("discordWebhook", true)) {
        settings.discordWebhook = request->getParam("discordWebhook", true)->value();
      }
      
      if (request->hasParam("emailAddress", true)) {
        settings.emailAddress = request->getParam("emailAddress", true)->value();
      }
      
      if (request->hasParam("emailPassword", true) && request->getParam("emailPassword", true)->value() != "") {
        settings.emailPassword = request->getParam("emailPassword", true)->value();
      }
      
      if (request->hasParam("smtpServer", true)) {
        settings.smtpServer = request->getParam("smtpServer", true)->value();
      }
      
      if (request->hasParam("smtpPort", true)) {
        settings.smtpPort = request->getParam("smtpPort", true)->value().toInt();
      }
      
      saveSettings();
      request->send(200, "text/plain", "Settings saved");
    } else {
      request->send(400, "text/plain", "Missing required parameters");
    }
  });
  
  server.on("/get_temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(128);
    doc["temperature"] = sensorData.temperature;
    doc["alarm"] = sensorData.alarm;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  server.on("/get_temperature_history", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    JsonArray array = doc.to<JsonArray>();
    
    // Return the temperature history in chronological order (oldest to newest)
    for (int i = 0; i < 120; i++) {
      int index = (sensorData.historyIndex + i) % 120;
      array.add(sensorData.temperatureHistory[index]);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  server.on("/get_alarm_events", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    JsonArray array = doc.to<JsonArray>();
    
    for (const auto& event : alarmEvents) {
      JsonObject obj = array.createNestedObject();
      obj["date"] = event.date;
      obj["time"] = event.time;
      obj["temperature"] = event.temperature;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  server.on("/delete_alarm", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("index", true)) {
      int index = request->getParam("index", true)->value().toInt();
      
      if (index >= 0 && index < alarmEvents.size()) {
        alarmEvents.erase(alarmEvents.begin() + index);
        request->send(200, "text/plain", "Alarm deleted");
      } else {
        request->send(400, "text/plain", "Invalid alarm index");
      }
    } else {
      request->send(400, "text/plain", "Missing index parameter");
    }
  });
  
  server.on("/wifi_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256);
    doc["connected"] = WiFi.status() == WL_CONNECTED;
    doc["ssid"] = settings.wifiSSID;
    doc["ip"] = WiFi.localIP().toString();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  server.on("/get_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    doc["logInterval"] = settings.logInterval;
    doc["alarmTemp"] = settings.alarmTemp;
    doc["discordWebhook"] = settings.discordWebhook;
    doc["emailAddress"] = settings.emailAddress;
    doc["smtpServer"] = settings.smtpServer;
    doc["smtpPort"] = settings.smtpPort;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // Start server
  server.begin();
  Serial.println("Web server started");
}

bool readTemperature() {
  uint16_t result;
  bool success = modbus.readHreg(MODBUS_ID, TEMPERATURE_REGISTER, &result, 1);
  
  if (success) {
    // Scale the value based on Delta DTK Temperature controller's format
    // Assuming 10x multiplier in the controller (e.g., 1234 = 123.4°C)
    sensorData.temperature = result / 10.0;
    return true;
  } else {
    Serial.println("Failed to read temperature from Modbus");
    return false;
  }
}

void checkAlarm() {
  bool previousAlarmState = sensorData.alarm;
  
  // Check if temperature is above threshold (alarm condition)
  sensorData.alarm = sensorData.temperature > settings.alarmTemp;
  
  // If alarm state just changed to active
  if (!previousAlarmState && sensorData.alarm) {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    
    // Record alarm event
    AlarmEvent event;
    event.date = getFormattedDate();
    event.time = getFormattedTime();
    event.temperature = sensorData.temperature;
    alarmEvents.push_back(event);
    
    // Send notifications
    notifyAlarm();
    
    // Log the alarm event
    String filename = getCurrentMonthFileName();
    if (SD.exists(filename)) {
      File logFile = SD.open(filename, FILE_APPEND);
      if (logFile) {
        logFile.print(event.date);
        logFile.print(",");
        logFile.print(event.time);
        logFile.print(",");
        logFile.print(event.temperature);
        logFile.print(",ALARM");
        logFile.println();
        logFile.close();
      }
    }
  } 
  // If alarm is cleared
  else if (previousAlarmState && !sensorData.alarm) {
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void updateTemperatureHistory() {
  // Store current temperature in the circular buffer
  sensorData.temperatureHistory[sensorData.historyIndex] = sensorData.temperature;
  sensorData.historyIndex = (sensorData.historyIndex + 1) % 120; // Wrap around at 120 entries
}

void logData() {
  String filename = getCurrentMonthFileName();
  
  // Create file with header if it doesn't exist
  if (createLogFileIfNeeded(filename)) {
    // Append data to file
    File logFile = SD.open(filename, FILE_APPEND);
    if (logFile) {
      logFile.print(getFormattedDate());
      logFile.print(",");
      logFile.print(getFormattedTime());
      logFile.print(",");
      logFile.println(sensorData.temperature);
      logFile.close();
      
      // Indicate successful logging with LED flash
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
    } else {
      Serial.println("Error opening log file for writing");
    }
  }
}

void saveSettings() {
  DynamicJsonDocument doc(1024);
  
  doc["logInterval"] = settings.logInterval;
  doc["alarmTemp"] = settings.alarmTemp;
  doc["discordWebhook"] = settings.discordWebhook;
  doc["emailAddress"] = settings.emailAddress;
  doc["emailPassword"] = settings.emailPassword;
  doc["smtpServer"] = settings.smtpServer;
  doc["smtpPort"] = settings.smtpPort;
  doc["wifiSSID"] = settings.wifiSSID;
  doc["wifiPassword"] = settings.wifiPassword;
  
  File configFile = SPIFFS.open("/config.json", FILE_WRITE);
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
    Serial.println("Settings saved to SPIFFS");
  } else {
    Serial.println("Failed to open config file for writing");
  }
}

void loadSettings() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", FILE_READ);
    if (configFile) {
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, configFile);
      
      if (!error) {
        settings.logInterval = doc["logInterval"] | LOG_INTERVAL_DEFAULT;
        settings.alarmTemp = doc["alarmTemp"] | ALARM_TEMP_DEFAULT;
        settings.discordWebhook = doc["discordWebhook"] | "";
        settings.emailAddress = doc["emailAddress"] | "";
        settings.emailPassword = doc["emailPassword"] | "";
        settings.smtpServer = doc["smtpServer"] | "smtp.gmail.com";
        settings.smtpPort = doc["smtpPort"] | 465;
        settings.wifiSSID = doc["wifiSSID"] | "";
        settings.wifiPassword = doc["wifiPassword"] | "";
        
        Serial.println("Settings loaded from SPIFFS");
      } else {
        Serial.println("Failed to parse config file");
      }
      
      configFile.close();
    }
  } else {
    Serial.println("No config file found, using defaults");
  }
}

String getFormattedDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "0000-00-00";
  }
  
  char buffer[11];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", 
           1900 + timeinfo.tm_year, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  return String(buffer);
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "00:00:00";
  }
  
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", 
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buffer);
}

String getCurrentMonthFileName() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "LOG000000.CSV";
  }
  
  char buffer[13];
  snprintf(buffer, sizeof(buffer), "LOG%04d%02d.CSV", 
           1900 + timeinfo.tm_year, timeinfo.tm_mon + 1);
  return String(buffer);
}

bool createLogFileIfNeeded(const String& filename) {
  if (!SD.exists(filename)) {
    File logFile = SD.open(filename, FILE_WRITE);
    if (logFile) {
      logFile.println("Date,Time,Temperature");
      logFile.close();
      Serial.print("Created new log file: ");
      Serial.println(filename);
      return true;
    } else {
      Serial.print("Failed to create log file: ");
      Serial.println(filename);
      return false;
    }
  }
  return true;
}

void notifyAlarm() {
  // Simple logic for now - would need appropriate libraries for full implementation
  
  // Send Discord webhook notification
  if (settings.discordWebhook.length() > 0 && WiFi.status() == WL_CONNECTED) {
    Serial.println("Would send Discord notification (not implemented)");
    // Would use HTTPClient to send a POST request to the Discord webhook URL
  }
  
  // Send email notification
  if (settings.emailAddress.length() > 0 && settings.emailPassword.length() > 0 && 
      WiFi.status() == WL_CONNECTED) {
    Serial.println("Would send email notification (not implemented)");
    // Would use ESP_Mail_Client or similar library to send email
  }
}