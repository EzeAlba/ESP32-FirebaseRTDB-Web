// We have the server running in Async always in the background working with Internet or without internet by creating a local AP.
// The handle functions from myserver.local/cards are no working completely with just the ESP32. What it does is sent the info to Firebase RTDB and then
// ESP32 realizes of the changes and executes the corresponding option. We need to change that to work completely local creating new functions to work with
// the local server. Also we have the time with the internal RTC that is syncronizing everytime it gets the time from the internet, and we can use that one
// when no internet is available.
#include <ArduinoJson.h>
#include <AsyncFsWebServer.h> // https://github.com/cotestatnt/esp-fs-webserver
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
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
#define API_KEY "AIzaSyDKv1wkflxPSIm-3GBX5__qyY5US3WFI9o"
#define DATABASE_URL "https://esp32-basico-lectura-c3a63-default-rtdb.firebaseio.com/"
const char *ssid = "Albarracin";
const char *password = "Bel39337905";
String building = "Casa";
// Global Objects
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);
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
bool apMode = false;
int testInt = 150;
float testFloat = 123.456;

const long timerDelay = 5000;
String listenerPath = "buildings/Casa/comand/01";
String serialPath = "buildings/Casa/Serial";
String memoryPath = "buildings/Casa/Memory";
String lastSerialMessage = "";

void setup()
{
    Serial.begin(115200);
    pinMode(LED, OUTPUT);

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
        return true;
    }
    return false;
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

void loop()
{
    static uint8_t uid[7] = {0};
    uint8_t uidLength = sizeof(uid);
    // myWebServer.run(); // Server is running everytime the loops starts. So, is good because checks the WiFi but is pretty slow. Lets try the Async in a new Skecth.
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength))
    {
        processCard(uid, uidLength);
        delay(1000); // Debounce
    }
    // Firebase.ready() should be called repeatedly to handle authentication tasks.
    if (Firebase.ready() && (millis() - sendDataPrevMillis > 1000 || sendDataPrevMillis == 0))
    {
        sendDataPrevMillis = millis();

        int addMode;
        if (Firebase.RTDB.getInt(&fbdo, listenerPath, &addMode))
        {
            // digitalWrite(ledPin, ledState);
            Serial.print("Add mode:");
            Serial.println(addMode);
            if (addMode == 1)
            {
                addNewCard();
            }
            else if (addMode == 2)
            {
                viewMemory();
            }
            else if (addMode == 3)
            {
                deleteUserCard();
            }
        }
        else
        {
            sendSerial(fbdo.errorReason());
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
        delay(1000);
        digitalWrite(LED, LOW);
    }
    if (signupOK)
    {
        sendToFirebase(timeStr, uidStr, userName, accessGranted ? "Granted" : "Denied");
    }
}

String getTime()
{
    configTime(-10800, 0, "pool.ntp.org");
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
        Firebase.RTDB.setString(&fbdo, "buildings/Casa/comand/newUserName", user);
        Firebase.RTDB.setInt(&fbdo, listenerPath, 1);
        request->send(200, "text/plain", "Ready to scan card for user: " + user);
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
        Firebase.RTDB.setString(&fbdo, "buildings/Casa/comand/deleteUser", user);
        Firebase.RTDB.setInt(&fbdo, listenerPath, 3);
        request->send(200, "text/plain", "Attempting to delete user: " + user);
    }
    else
    {
        request->send(400, "text/plain", "Missing user parameter");
    }
}
