#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <Preferences.h>
#include <Firebase_ESP_Client.h>
#include "time.h"

// Proporcionar la informacion del proceso de generacion del token.
#include "addons/TokenHelper.h"

// Proporcionar la informacion de impresion de la carga util de RTDB y otras funciones de ayuda.
#include "addons/RTDBHelper.h"

// Configuration
#define LED 2
#define API_KEY "AIzaSyDKv1wkflxPSIm-3GBX5__qyY5US3WFI9o"
#define DATABASE_URL "https://esp32-basico-lectura-c3a63-default-rtdb.firebaseio.com/"
const char* ssid = "Albarracin";
const char* password = "Bel39337905";
String building = "Casa";
// Global Objects
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);
Preferences prefs;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Variables
bool signupOK = false;
unsigned long sendDataPrevMillis = 0;
const long timerDelay = 5000;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  
  // Initialize NFC
  if(!initNFC()) while(1);
  
  // Initialize WiFi
  if(!initWiFi()) Serial.println("Continuing without WiFi");
  
  // Initialize Firebase
  if(WiFi.status() == WL_CONNECTED) initFirebase();
}

bool initNFC() {
  Wire.begin();
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("No NFC reader found");
    return false;
  }
  
  Serial.printf("NFC Reader found. Version: %X\n", versiondata);
  nfc.SAMConfig();
  Serial.println("Ready to read RFID cards");
  return true;
}

bool initWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  return false;
}

void initFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  
  Serial.println("\n--Registering----");
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("OK");
    signupOK = true;
  } else {
    Serial.println(config.signer.signupError.message.c_str());
  }
  
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  static uint8_t uid[7] = {0};
  uint8_t uidLength = sizeof(uid);
  
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    processCard(uid, uidLength);
    delay(1000); // Debounce
  }
  
  if (Serial.available()) handleSerialCommand();
}

void processCard(uint8_t* uid, uint8_t uidLength) {
  char uidStr[15];
  snprintf(uidStr, sizeof(uidStr), "%02X%02X%02X%02X%02X%02X%02X", 
           uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
  
  String timeStr = getTime();
  Serial.printf("\nCard Read - %s\nUID: %s\n", timeStr.c_str(), uidStr);
  
  prefs.begin("rfid", true);
  String userName = prefs.getString(uidStr, ""); // Get the user name associated with this UID
  bool accessGranted = userName.length() > 0;
  prefs.end();
  
  if (accessGranted) {
    Serial.println("Access Granted");
    digitalWrite(LED, HIGH);
    delay(1000);
    digitalWrite(LED, LOW);
  }
  if (signupOK) {
    sendToFirebase(timeStr, uidStr, userName, accessGranted ? "Granted" : "Denied");
  }
   
}

String getTime() {
  configTime(-10800, 0, "pool.ntp.org");
  struct tm timeinfo;
  
  if(!getLocalTime(&timeinfo)) {
    return "Time Error";
  }
  
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M", &timeinfo);
  return String(buffer);
}

void sendToFirebase(const String& timeStr, const String& uidStr, const String& userName, const String& access) {
  if (Firebase.ready() && signupOK) {
    FirebaseJson json;
    String path = "buildings/" + building + "/cards/" + uidStr + "/";
    json.set("uid", uidStr);
    json.set("user", userName);
    json.set("time", timeStr);
    json.set("access", access);

    if (Firebase.RTDB.setJSON(&fbdo, path, &json)) {
      Serial.println("All data updated in Firebase");
    } else {
      Serial.printf("Firebase error: %s\n", fbdo.errorReason().c_str());
    }
  }
}

void handleSerialCommand() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  
  if (cmd == "menu") {
    while (true) {
      Serial.println("\n1. Add New Card\n2. View Memory\n3. Exit");
      String choice = waitInput();
      
      if (choice == "1") addNewCard();
      else if (choice == "2") viewMemory();
      else if (choice == "3") break;
    }
  }
}

String waitInput() {
  while (!Serial.available());
  return Serial.readStringUntil('\n');
}

void addNewCard() {
  Serial.println("Enter user data:");
  String userData = waitInput();
  
  Serial.println("Scan card to register:");
  uint8_t uid[7] = {0};
  uint8_t uidLength = sizeof(uid);
  
  unsigned long start = millis();
  while (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength) && millis() - start < 10000) {
    delay(100);
  }
  
  if (uidLength > 0) {
    char uidStr[15];
    snprintf(uidStr, sizeof(uidStr), "%02X%02X%02X%02X%02X%02X%02X", 
             uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
    
    prefs.begin("rfid", false);
    prefs.putString(uidStr, userData);
    
    int counter = prefs.getInt("counter", 0);
    prefs.putString(String(counter).c_str(), "Card: " + String(uidStr) + ", User: " + userData);
    prefs.putInt("counter", counter + 1);
    prefs.end();
    
    Serial.printf("Card registered to user: %s\n", userData.c_str());
  } else {
    Serial.println("No card detected");
  }
}

void viewMemory() {
  prefs.begin("rfid", true);
  int counter = prefs.getInt("counter", 0);
  
  Serial.println("\nRegistered Cards:");
  for (int i = 0; i < counter; i++) {
    String key = String(i);
    Serial.printf("%d: %s\n", i, prefs.getString(key.c_str(), "").c_str());
  }
  
  Serial.printf("\nTotal registered: %d\n", counter);
  prefs.end();
}