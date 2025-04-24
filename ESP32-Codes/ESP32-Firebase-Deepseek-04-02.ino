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
const char *ssid = "Albarracin";
const char *password = "Bel39337905";
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
int count = 0;

const long timerDelay = 5000;
String listenerPath = "buildings/Casa/comand/01";
String serialPath = "buildings/Casa/Serial";
String memoryPath = "buildings/Casa/Memory";

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
    WiFi.begin(ssid, password);
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

    if (!getLocalTime(&timeinfo))
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
}