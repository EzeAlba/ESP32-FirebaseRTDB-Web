// Conexion de ESP32 a NFCPN532
//             3v3 <--> vcc
//             gnd <--> gnd
//             p21 <--> sda/txd
//             p22 <--> scl/rxd// We have the server running in Async always in the background working with Internet or without internet by creating a local AP.
// The handle functions from myserver.local/cards are no working completely with just the ESP32. What it does is sent the info to Firebase RTDB and then
// ESP32 realizes of the changes and executes the corresponding option. We need to change that to work completely local creating new functions to work with
// the local server. Also we have the time with the internal RTC that is syncronizing everytime it gets the time from the internet, and we can use that one
// when no internet is available.
// ezealba2@gmail.com
// ESP32-2025
// OIPn5PDwGAXrgJv0vT8SC5Ay7jz1
#include <ArduinoJson.h>
#include <AsyncFsWebServer.h> // https://github.com/cotestatnt/esp-fs-webserver
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <NfcAdapter.h> //ESPC3
#include <Preferences.h>
#include <Firebase_ESP_Client.h>
#include "time.h"
#include <FS.h>
#include <LittleFS.h>
#include "LittleFS.h"
#include <ESP32Time.h> //Libreria de tiempo con RTC

// Proporcionar la informacion del proceso de generacion del token.
#include "addons/TokenHelper.h"

// Proporcionar la informacion de impresion de la carga util de RTDB y otras funciones de ayuda.
#include "addons/RTDBHelper.h"

// Configuration
#define FILESYSTEM LittleFS
#define LED 2
#define LOCK 23
#define SENSOR 4
#define BUZZER 5
#define API_KEY "AIzaSyDKv1wkflxPSIm-3GBX5__qyY5US3WFI9o"
#define DATABASE_URL "https://esp32-basico-lectura-c3a63-default-rtdb.firebaseio.com/"
#define USER_EMAIL "ezealba2@gmail.com"
#define USER_PASSWORD "ESP32-2025"
const char *ssid = "Albarracin";
const char *password = "Bel39337905";
String building = "Casa";
// Global Objects
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);
// NfcAdapter nfc1 = NfcAdapter(pn532_i2c);////ESPC3 objeto nfc de libreria adaptada
Preferences prefs;
ESP32Time rtc; // creamos una estructura del tipo ESP32Time
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
// FSWebServer myWebServer(FILESYSTEM, 80);
AsyncFsWebServer server(80, LittleFS, "myServer");
AsyncEventSource events("/events");

// Variables
bool signupOK = false;
unsigned long sendDataPrevMillis = 0;
int count = 0;
uint8_t ledPin = LED;
uint8_t sensor = SENSOR;
uint8_t lock = LOCK;
uint8_t buzz = BUZZER;

bool apMode = false;
int testInt = 150;
float testFloat = 123.456;

// int sda_pin = 20; // GPIO20 as I2C SDA //ESPC3
// int scl_pin = 21; // GPIO21 as I2C SCL//ESPC3

bool hasInternet = false;
unsigned long lastConnectionCheck = 0;
const long connectionCheckInterval = 30000; // Check every 30 seconds

const long timerDelay = 5000;
String listenerPath = "buildings/Casa/comand/01";
String serialPath = "buildings/Casa/Serial";
String memoryPath = "buildings/Casa/Memory";
String lastSerialMessage = "";

void setup()
{
    Serial.begin(115200);
    pinMode(LED, OUTPUT);
    pinMode(lock, OUTPUT);
    digitalWrite(lock, HIGH);
    pinMode(sensor, INPUT);
    pinMode(buzz, OUTPUT);

    // Initialize NFC
    if (!initNFC())
        while (1)
            ;

    // Initialize WiFi
    if (!initWiFi())
        Serial.println("Continuing without WiFi");

    // Initialize Firebase
    if (WiFi.status() == WL_CONNECTED)
        initFirebase();
}

bool initNFC()
{
    // Wire.setPins(sda_pin, scl_pin);//ESPC3
    Wire.begin();
    nfc.begin();
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata)
    {
        Serial.println("No NFC reader found");
        return false;
    }
    nfc.SAMConfig();
    Serial.println("Ready to read RFID cards");
    return true;
}

bool initWiFi()
{
    WiFi.mode(WIFI_STA);
    // FILESYSTEM INIT including AP mode or Wifi using SSID, Pass saved
    serverFS();
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
        checkInternetConnection();
        return true;
    }
    else
    {
        // Start AP mode if can't connect to WiFi
        Serial.println("\nWiFi not connected! Starting AP mode...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ESP_NFC_Controller", "123456789");
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
        hasInternet = false;
        return false;
    }
}

void initFirebase()
{
    Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    if (Firebase.signUp(&config, &auth, "", ""))
    {
        Serial.println("FB-OK");
        signupOK = true;
    }
    else
    {
        Serial.println(config.signer.signupError.message.c_str());
    }
    config.token_status_callback = tokenStatusCallback;
    Firebase.reconnectWiFi(true);
    Firebase.reconnectNetwork(true);
    Firebase.begin(&config, &auth);
    fbdo.setBSSLBufferSize(2048 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);
    sendSerial("Firebase ready and NFC Ready");
}

void checkInternetConnection()
{
    bool currentStatus = (WiFi.status() == WL_CONNECTED);

    if (currentStatus && !hasInternet)
    {
        // Conexión recién establecida
        hasInternet = true;
        initFirebase();
        onInternetRestored();
    }
    else if (!currentStatus && hasInternet)
    {
        // Pérdida de conexión
        hasInternet = false;
    }
}

void loop()
{
    static uint8_t uid[7] = {0};
    uint8_t uidLength = sizeof(uid);

    if (digitalRead(sensor) == HIGH)
    {
        digitalWrite(buzz, LOW);
    }
    else
    {
        digitalWrite(buzz, HIGH);
        sendSerial("Puerta Abierta!");
    }

    if (millis() - lastConnectionCheck > connectionCheckInterval)
    {
        checkInternetConnection();
        lastConnectionCheck = millis();
        if (hasInternet && !signupOK)
        {
            initFirebase();
        }
    }

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength))
    {
        processCard(uid, uidLength);
        delay(1000);
    }

    if (Firebase.ready() && (millis() - sendDataPrevMillis > 1000 || sendDataPrevMillis == 0))
    {
        sendDataPrevMillis = millis();

        int addMode = 0;
        if (Firebase.RTDB.getInt(&fbdo, listenerPath, &addMode))
        {
            Serial.print("Add mode:");
            Serial.println(addMode);

            if (addMode == 1)
            {
                String userData;
                if (Firebase.RTDB.getString(&fbdo, "buildings/Casa/comand/newUserName", &userData))
                {
                    if (userData.length() > 0 && userData != "0")
                    {
                        addNewCardWS(userData); // Usar la función local
                        // Resetear valores en Firebase
                        Firebase.RTDB.setInt(&fbdo, listenerPath, 0);
                        Firebase.RTDB.setString(&fbdo, "buildings/Casa/comand/newUserName", "0");
                    }
                }
            }
            else if (addMode == 2)
            {
                viewMemory(); // Esta función ya escribe en Firebase si está conectado
                Firebase.RTDB.setInt(&fbdo, listenerPath, 0);
            }
            else if (addMode == 3)
            {
                String userToDelete;
                if (Firebase.RTDB.getString(&fbdo, "buildings/Casa/comand/deleteUser", &userToDelete))
                {
                    if (userToDelete.length() > 0 && userToDelete != "0")
                    {
                        deleteUserCardWS(userToDelete); // Usar la función local
                        // Resetear valores en Firebase
                        Firebase.RTDB.setInt(&fbdo, listenerPath, 0);
                        Firebase.RTDB.setString(&fbdo, "buildings/Casa/comand/deleteUser", "0");
                    }
                }
            }
        }
        else
        {
            sendSerial("Error reading command: " + fbdo.errorReason());
        }
    }
}

void processCard(uint8_t *uid, uint8_t uidLength)
{
    char uidStr[15];
    snprintf(uidStr, sizeof(uidStr), "%02X%02X%02X%02X%02X%02X%02X",
             uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);

    String timeStr = getTime();
    sendSerial("Card Read: " + timeStr + "| UID: " + uidStr);

    String rtcTime = getRTCTime();
    agregarArchivo("/registros.txt", "\nCard Read: " + rtcTime + "| UID: " + uidStr);

    prefs.begin("rfid", true);
    String userName = prefs.getString(uidStr, ""); // Get the user name associated with this UID
    bool accessGranted = userName.length() > 0;
    prefs.end();

    if (accessGranted)
    {
        sendSerial("Access Granted for: " + userName);
        digitalWrite(LED, HIGH);
        digitalWrite(lock, LOW);
        digitalWrite(buzz, HIGH);
        delay(2000);
        digitalWrite(LED, LOW);
        digitalWrite(lock, HIGH);
        digitalWrite(buzz, LOW);
    }
    if (signupOK)
    {
        sendToFirebase(timeStr, uidStr, userName, accessGranted ? "Granted" : "Denied");
    }
}

String getTime()
{
    struct tm timeinfo;
    if (hasInternet)
    {
        configTime(-10800, 0, "pool.ntp.org");
        if (getLocalTime(&timeinfo))
        {
            rtc.setTimeStruct(timeinfo);
        }
    }

    // Fall back to RTC if internet time fails
    if (!getLocalTime(&timeinfo))
    {
        timeinfo = rtc.getTimeStruct();
    }

    char buffer[30];
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M", &timeinfo);
    return String(buffer);
}

String getRTCTime()
{
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        rtc.setTimeStruct(timeinfo);
    }
    else if (!getLocalTime(&timeinfo))
    {
        return "Time Error";
    }
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M", &timeinfo);
    return String(buffer);
}

void sendToFirebase(const String &timeStr, const String &uidStr, const String &userName, const String &access)
{
    if (Firebase.ready() && signupOK)
    {
        FirebaseJson json;
        String path = "buildings/" + building + "/cards/" + uidStr + "/";
        json.set("uid", uidStr);
        json.set("user", userName);
        json.set("time", timeStr);
        json.set("access", access);

        if (Firebase.RTDB.setJSON(&fbdo, path, &json))
        {
            Serial.println("All data updated in Firebase");
        }
        else
        {
            Serial.printf("Firebase error: %s\n", fbdo.errorReason().c_str());
        }
    }
}

String waitInput()
{
    while (!Serial.available())
        ;
    return Serial.readStringUntil('\n');
}

void addNewCard()
{
    Serial.println("Entering user data from RTDB:");
    String userData;
    if (Firebase.RTDB.getString(&fbdo, "buildings/Casa/comand/newUserName", &userData))
    {
        if (userData.length() > 0 && userData != "0")
        {
            Serial.printf("Registering card for user: %s\n", userData.c_str());

            sendSerial("Scan card to register:");
            uint8_t uid[7] = {0};
            uint8_t uidLength = sizeof(uid);

            unsigned long start = millis();
            while (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength) && millis() - start < 10000)
            {
                delay(100);
            }

            if (uidLength > 0)
            {
                char uidStr[15];
                snprintf(uidStr, sizeof(uidStr), "%02X%02X%02X%02X%02X%02X%02X",
                         uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);

                prefs.begin("rfid", false);
                prefs.putString(uidStr, userData);

                int counter = prefs.getInt("counter", 0);
                prefs.putString(String(counter).c_str(), "Card: " + String(uidStr) + ", User: " + userData);
                prefs.putInt("counter", counter + 1);
                prefs.end();

                sendSerial("Card registered to user: " + userData);
            }
            else
            {
                sendSerial("No card detected");
            }
        }
        else
        {
            sendSerial("No valid user data provided");
        }
        if (Firebase.ready() && (millis() - sendDataPrevMillis > 1000 || sendDataPrevMillis == 0))
        {
            sendDataPrevMillis = millis();
            if (Firebase.RTDB.setInt(&fbdo, listenerPath, 0))
            {
                Serial.println("Comand restored");
            }
            delay(100);
            Firebase.RTDB.setString(&fbdo, "buildings/Casa/comand/newUserName", "0");
        }
    }
    else
    {
        Serial.printf("Error reading user data: %s\n", fbdo.errorReason().c_str());
    }
}

void addNewCardWS(String user)
{
    uint8_t uid[7] = {0};
    uint8_t uidLength = sizeof(uid);
    sendSerial("Ready to Scan card:");

    unsigned long start = millis();
    while (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength) &&
           millis() - start < 10000)
    {
        delay(100);
    }

    if (uidLength > 0)
    {
        char uidStr[15];
        snprintf(uidStr, sizeof(uidStr), "%02X%02X%02X%02X%02X%02X%02X",
                 uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);

        // Guardar en preferencias
        prefs.begin("rfid", false);
        prefs.putString(uidStr, user);
        int counter = prefs.getInt("counter", 0);
        prefs.putString(String(counter).c_str(), "Card: " + String(uidStr) + ", User: " + user);
        prefs.putInt("counter", counter + 1);
        prefs.end();

        // Actualizar Firebase si hay conexión
        if (hasInternet && Firebase.ready())
        {
            String path = "buildings/" + building + "/cards/" + uidStr + "/";
            FirebaseJson json;
            json.set("uid", uidStr);
            json.set("user", user);
            json.set("time", getTime());
            Firebase.RTDB.setJSON(&fbdo, path, &json);
        }

        sendSerial("Card registered to user: " + user);
    }
    else
    {
        sendSerial("No card detected");
    }
}

void viewMemory()
{
    prefs.begin("rfid", true);
    int counter = prefs.getInt("counter", 0);

    FirebaseJson memoryData;

    Serial.println("\nRegistered Cards:");
    for (int i = 0; i < counter; i++)
    {
        String key = String(i);
        String entry = prefs.getString(key.c_str(), "");
        Serial.printf("%d: %s\n", i, entry.c_str());

        // Agregar directamente al JSON con clave única
        memoryData.set("card_" + String(i), entry);
    }

    memoryData.set("totalRegistered", counter);
    memoryData.set("lastUpdated", getTime());

    prefs.end();

    if (Firebase.ready() && signupOK)
    {
        Firebase.RTDB.setJSON(&fbdo, "buildings/Casa/Memory", &memoryData);
    }

    if (Firebase.RTDB.setInt(&fbdo, listenerPath, 0))
    {
        Serial.println("Command restored");
    }
    sendSerial("Memoria actualizada en Base de Datos");
}

void deleteUserCard()
{
    Serial.println("Preparing to delete user...");

    String userToDelete;
    if (Firebase.RTDB.getString(&fbdo, "buildings/Casa/comand/deleteUser", &userToDelete))
    {
        if (userToDelete.length() > 0 && userToDelete != "0")
        {
            sendSerial("Attempting to delete user: " + userToDelete);

            prefs.begin("rfid", false);

            // 1. Buscar y eliminar el UID asociado al usuario
            bool found = false;
            int counter = prefs.getInt("counter", 0);

            // Buscar en todas las entradas
            for (int i = 0; i < counter; i++)
            {
                String key = String(i);
                String entry = prefs.getString(key.c_str(), "");

                // El formato es "Card: UID, User: NOMBRE"
                int userPos = entry.indexOf("User: ");
                if (userPos != -1)
                {
                    String userName = entry.substring(userPos + 6); // +6 para saltar "User: "

                    if (userName == userToDelete)
                    {
                        // Extraer UID (entre "Card: " y ", User:")
                        int cardPos = entry.indexOf("Card: ");
                        int commaPos = entry.indexOf(", User:");
                        if (cardPos != -1 && commaPos != -1)
                        {
                            String uid = entry.substring(cardPos + 6, commaPos);

                            // Eliminar ambas entradas
                            prefs.remove(uid.c_str());
                            prefs.remove(key.c_str());
                            found = true;
                            sendSerial("Deleted card: " + uid + " for user: " + userToDelete);
                            // Reorganizar las entradas restantes
                            for (int j = i; j < counter - 1; j++)
                            {
                                String nextKey = String(j + 1);
                                String nextEntry = prefs.getString(nextKey.c_str(), "");
                                prefs.putString(key.c_str(), nextEntry);
                                key = nextKey;
                            }

                            // Eliminar la última entrada duplicada
                            prefs.remove(String(counter - 1).c_str());

                            // Actualizar el contador
                            prefs.putInt("counter", counter - 1);
                            break;
                        }
                    }
                }
            }

            if (!found)
            {
                sendSerial("User not found in registry");
            }

            // Limpiar el campo en Firebase
            Firebase.RTDB.setString(&fbdo, "buildings/Casa/comand/deleteUser", "0");
            Firebase.RTDB.setInt(&fbdo, "buildings/Casa/comand/01", 0);
            prefs.end();
        }
        else
        {
            sendSerial("No valid user specified for deletion");
        }
    }
    else
    {
        sendSerial("Error reading user to delete: " + fbdo.errorReason());
    }
}

void deleteUserCardWS(String user)
{
    // String userToDelete;
    sendSerial("Attempting to delete user: " + user);
    prefs.begin("rfid", false);
    // 1. Buscar y eliminar el UID asociado al usuario
    bool found = false;
    int counter = prefs.getInt("counter", 0);

    // Buscar en todas las entradas
    for (int i = 0; i < counter; i++)
    {
        String key = String(i);
        String entry = prefs.getString(key.c_str(), "");

        // El formato es "Card: UID, User: NOMBRE"
        int userPos = entry.indexOf("User: ");
        if (userPos != -1)
        {
            String userName = entry.substring(userPos + 6); // +6 para saltar "User: "
            if (userName == user)
            {
                // Extraer UID (entre "Card: " y ", User:")
                int cardPos = entry.indexOf("Card: ");
                int commaPos = entry.indexOf(", User:");
                if (cardPos != -1 && commaPos != -1)
                {
                    String uid = entry.substring(cardPos + 6, commaPos);

                    // Eliminar ambas entradas
                    prefs.remove(uid.c_str());
                    prefs.remove(key.c_str());
                    found = true;
                    sendSerial("Deleted card: " + uid + " for user: " + user);
                    // Reorganizar las entradas restantes
                    for (int j = i; j < counter - 1; j++)
                    {
                        String nextKey = String(j + 1);
                        String nextEntry = prefs.getString(nextKey.c_str(), "");
                        prefs.putString(key.c_str(), nextEntry);
                        key = nextKey;
                    }
                    // Eliminar la última entrada duplicada
                    prefs.remove(String(counter - 1).c_str());

                    // Actualizar el contador
                    prefs.putInt("counter", counter - 1);
                    break;
                }
            }
        }
    }
    if (!found)
    {
        sendSerial("User not found in registry");
    }
    prefs.end();
}

void sendSerial(const String &message)
{
    if (Firebase.ready() && signupOK)
    {
        FirebaseJson json;
        json.set(getTime(), message);
        Firebase.RTDB.setJSON(&fbdo, serialPath, &json);
    }
    // Also send to web clients
    lastSerialMessage = message;
    events.send(message.c_str(), "message");
    Serial.println(message); // Keep original serial output
}

////////////////////////////////  Filesystem  /////////////////////////////////////////
bool startFilesystem()
{
    if (LittleFS.begin())
    {
        File root = LittleFS.open("/", "r");
        File file = root.openNextFile();
        while (file)
        {
            Serial.printf("FS File: %s, size: %d\n", file.name(), file.size());
            file = root.openNextFile();
        }
        return true;
    }
    else
    {
        Serial.println("ERROR on mounting filesystem. It will be reformatted!");
        LittleFS.format();
        ESP.restart();
    }
    return false;
}

void getFsInfo(fsInfo_t *fsInfo)
{
    fsInfo->fsName = "LittleFS";
    fsInfo->totalBytes = LittleFS.totalBytes();
    fsInfo->usedBytes = LittleFS.usedBytes();
}

////////////////////////////  HTTP Request Handlers  ////////////////////////////////////
void handleLed(AsyncWebServerRequest *request)
{
    static int value = false;
    // http://xxx.xxx.xxx.xxx/led?val=1
    if (request->hasParam("val"))
    {
        value = request->arg("val").toInt();
        digitalWrite(ledPin, value);
    }
    String reply = "LED is now ";
    reply += value ? "ON" : "OFF";
    request->send(200, "text/plain", reply);
}

void serverFS()
{
    if (startFilesystem())
    {
        Serial.println("LittleFS filesystem ready!");
        File config = server.getConfigFile("r");
        if (config)
        {
            DynamicJsonDocument doc(config.size() * 1.33);
            deserializeJson(doc, config);
            testInt = doc["Test int variable"];
            testFloat = doc["Test float variable"];
        }
        Serial.printf("Stored \"testInt\" value: %d\n", testInt);
        Serial.printf("Stored \"testFloat\" value: %3.2f\n", testFloat);
    }
    else
        Serial.println("LittleFS error!");

    // Try to connect to WiFi (will start AP if not connected after timeout)
    if (!server.startWiFi(10000))
    {
        Serial.println("\nWiFi not connected! Starting AP mode...");
        server.startCaptivePortal("ESP_AP", "123456789", "/setup");
    }

    server.addOptionBox("Custom options");
    server.addOption("Test int variable", testInt);
    server.addOption("Test float variable", testFloat);
    server.setSetupPageTitle("Simple Async ESP FS WebServer");

    // Enable ACE FS file web editor and add FS info callback function
    server.enableFsCodeEditor();
#ifdef ESP32
    server.setFsInfoCallback(getFsInfo);
#endif

    server.addHandler(&events);
    server.serveStatic("/cards", LittleFS, "/www/card_management.html");
    server.on("/addCard", HTTP_GET, handleAddCard);
    server.on("/viewCards", HTTP_GET, handleViewCards);
    server.on("/deleteUser", HTTP_GET, handleDeleteUser);
    server.on("/led", HTTP_GET, handleLed);

    // Login handler
    server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    if(request->hasParam("user") && request->hasParam("pass")) {
        String user = request->getParam("user")->value();
        String pass = request->getParam("pass")->value();
        
        // Simple hardcoded auth (replace with your own logic)
        if(user == "admin" && pass == "admin123") {
            DynamicJsonDocument doc(128);
            doc["success"] = true;
            doc["hasInternet"] = hasInternet;
            
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
            return;
        }
    }
    request->send(401, "text/plain", "Authentication failed"); });

    // Check authentication status
    server.on("/checkAuth", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    DynamicJsonDocument doc(128);
    doc["authenticated"] = true; // In a real app, check session
    doc["hasInternet"] = hasInternet;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });

    // Get access logs
    server.on("/getLogs", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    if(LittleFS.exists("/registros.txt")) {
        request->send(LittleFS, "/registros.txt", "text/plain");
    } else {
        request->send(404, "text/plain", "No logs found");
    } });

    // Toggle WiFi
    server.on("/toggleWifi", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    if(request->hasParam("state")) {
        String state = request->getParam("state")->value();
        if(state == "off") {
            WiFi.disconnect(true);  // Disconnect WiFi and disable auto-reconnect
            request->send(200, "text/plain", "WiFi turned off");
            hasInternet = false;
        } else {
            WiFi.begin(ssid, password);
            request->send(200, "text/plain", "WiFi turned on - reconnecting...");
        }
    } else {
        request->send(400, "text/plain", "Missing state parameter");
    } });

    // System information
    server.on("/systemInfo", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    DynamicJsonDocument doc(256);
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });

    // Start server
    server.init();
    Serial.print(F("Async ESP Web Server started on IP Address: "));
    Serial.println(server.getServerIP());
    Serial.println(F(
        "This is \"simpleServer.ino\" example.\n"
        "Open /setup page to configure optional parameters.\n"
        "Open /edit page to view, edit or upload example or your custom webserver source files."));
}

void agregarArchivo(String ruta, String texto)
{
    Serial.println("Agregando " + texto + " en archivo: " + ruta);
    File archivo = LittleFS.open(ruta, "a");
    if (!archivo)
    {
        Serial.println("Archivo no se puede abrir");
        return;
    }
    if (archivo.print(texto))
    {
        Serial.println("Mensaje agregado");
    }
    else
    {
        Serial.println("Agregado fallo");
    }
    archivo.close();
}

// Add these new request handlers
void handleAddCard(AsyncWebServerRequest *request)
{
    if (request->hasParam("user"))
    {
        String user = request->getParam("user")->value();

        if (hasInternet)
        {
            // If we have internet, use the Firebase workflow
            Firebase.RTDB.setString(&fbdo, "buildings/Casa/comand/newUserName", user);
            Firebase.RTDB.setInt(&fbdo, listenerPath, 1);
            request->send(200, "text/plain", "Using cloud mode. Ready to scan card for user: " + user);
        }
        else
        {
            // Local-only mode
            addNewCardWS(user);
            request->send(200, "text/plain", "Using local mode. Card registered for user: " + user);
        }
    }
    else
    {
        request->send(400, "text/plain", "Missing user parameter");
    }
}

void handleViewCards(AsyncWebServerRequest *request)
{
    prefs.begin("rfid", true);
    int counter = prefs.getInt("counter", 0);

    DynamicJsonDocument doc(1024);
    JsonArray cards = doc.createNestedArray("cards");

    for (int i = 0; i < counter; i++)
    {
        String key = String(i);
        String entry = prefs.getString(key.c_str(), "");
        if (entry.length() > 0)
        {
            cards.add(entry);
        }
    }

    prefs.end();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleDeleteUser(AsyncWebServerRequest *request)
{
    if (request->hasParam("user"))
    {
        String user = request->getParam("user")->value();

        if (hasInternet)
        {
            // Cloud mode
            Firebase.RTDB.setString(&fbdo, "buildings/Casa/comand/deleteUser", user);
            Firebase.RTDB.setInt(&fbdo, listenerPath, 3);
            request->send(200, "text/plain", "Using cloud mode. Attempting to delete user: " + user);
        }
        else
        {
            // Local mode
            deleteUserCardWS(user);
            request->send(200, "text/plain", "Using local mode. Attempted to delete user: " + user);
        }
    }
    else
    {
        request->send(400, "text/plain", "Missing user parameter");
    }
}

void syncLocalDataWithFirebase()
{
    if (!hasInternet)
        return;

    prefs.begin("rfid", true);
    int counter = prefs.getInt("counter", 0);

    // Sync all cards to Firebase
    for (int i = 0; i < counter; i++)
    {
        String key = String(i);
        String entry = prefs.getString(key.c_str(), "");

        if (entry.length() > 0)
        {
            // Parse the entry "Card: UID, User: NAME"
            int cardPos = entry.indexOf("Card: ");
            int userPos = entry.indexOf("User: ");
            if (cardPos != -1 && userPos != -1)
            {
                String uid = entry.substring(cardPos + 6, userPos - 2);
                String user = entry.substring(userPos + 6);

                // Update Firebase
                String path = "buildings/" + building + "/cards/" + uid + "/";
                FirebaseJson json;
                json.set("uid", uid);
                json.set("user", user);
                json.set("time", getTime());
                Firebase.RTDB.setJSON(&fbdo, path, &json);
            }
        }
    }

    prefs.end();
    sendSerial("Local data synchronized with Firebase");
}

// Call this when internet connection is restored
void onInternetRestored()
{
    syncLocalDataWithFirebase();
    // Sync any other pending data...
}
