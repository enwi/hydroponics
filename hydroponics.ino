#ifdef ESP32
#include <ESPmDNS.h>
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>

ESP8266WiFiMulti wifiMulti;
#endif

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPUI.h>
#include <ESP_EEPROM.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <WiFiUdp.h>

#include "WiFiConfig.h" // ssid and password, not to be shown on stream :D

// Settings stored in eeprom
struct Settings
{
    uint16_t irrigationDuration;
    uint16_t drainDuration;
    bool irrigationEnabled;
} settings;
// Water pump
const uint8_t pump = D5;
// Did this irrigation controller start for the first time aka does not have a config
bool firstStart = false;
// LED pump indicator
const uint8_t pumpIndicator = D4;
// Are we currently irrigating?
bool irrigating = false;

// Our hostname
const char* hostname = "hydroponics";

// The last second we updated
uint8_t lastSecond = 0;

// The amount of seconds that have passed (will be reset freqeuntly)
uint16_t secondsPassed = 0;

// GUI
int timestampNI, infoNI, irrigationDurationNI, drainDurationNI, enableButton;

// Time update
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

time_t getNTPTime()
{
    return timeClient.getEpochTime();
}

/**
 * @brief Force syncing the time with a time server
 *
 * @return true Time was synced
 * @return false Time was not synced
 */
bool forceTimeSync()
{
    Serial.print("Forcing time sync... ");
    timeClient.forceUpdate();
    setSyncInterval(0);
    time_t time = now();
    setSyncInterval(300);
    const bool success = year(time) > 2020;
    Serial.println(success ? "Successful" : "Failed");
    return success;
}

/**
 * @brief Connects to any WiFi usig WifiMulti
 *
 * @return true If conected
 * @return false If not connected
 */
bool connectWiFi()
{
    return wifiMulti.run() == WL_CONNECTED;
}

void beginEEPROM()
{
    // The begin() call will find the data previously saved in EEPROM if the same size
    // as was previously committed. If the size is different then the EEEPROM data is cleared.
    // Note that this is not made permanent until you call commit();
    EEPROM.begin(sizeof(Settings));

    firstStart = EEPROM.percentUsed() < 0;

    if (!firstStart)
    {
        loadSettings();
    }

    // EEPROM.commitReset(); // "Factory defaults"
}

/**
 * @brief Load the settings
 *
 */
void loadSettings()
{
    EEPROM.get(0, settings);
}

/**
 * @brief Store the settings
 *
 * @return true Settings have been stored
 * @return false Settings could not be stored
 */
bool storeSettings()
{
    EEPROM.put(0, settings);
    return EEPROM.commit();
}

/**
 * @brief Enable the pump
 *
 * Will also turn on the pump indicator
 */
void enabledPump()
{
    Serial.println("Enabling pump");
    digitalWrite(pumpIndicator, LOW);
    digitalWrite(pump, HIGH);
}

/**
 * @brief Disable the pump
 *
 * Will also turn off the pump indicator
 */
void disabledPump()
{
    Serial.println("Disabling pump");
    digitalWrite(pumpIndicator, HIGH);
    digitalWrite(pump, LOW);
}

void updateIrrigationDuration(Control* sender, int)
{
    Serial.println("Updating IrrigationDuration to " + sender->value);
    settings.irrigationDuration = sender->value.toInt();
    storeSettings();
}

void updateDrainDuration(Control* sender, int)
{
    Serial.println("Updating DrainDuration to " + sender->value);
    settings.drainDuration = sender->value.toInt();
    storeSettings();
}

void updateEnableButton(Control* sender, int)
{
    Serial.println("Updating EnableButton to " + sender->value);
    settings.irrigationEnabled = sender->value.toInt();

    // Make sure pump does not stay on
    if (irrigating)
    {
        disabledPump();
    }
    storeSettings();
}

void updateTestButton(Control*, int value)
{
    if (value == B_DOWN)
    {
        enabledPump();
    }
    else
    {
        disabledPump();
    }
}

void setupGUI()
{
    timestampNI = ESPUI.label("Time", ControlColor::Turquoise, "Time is updating");
    infoNI = ESPUI.label("Info", ControlColor::Turquoise, "Starting...");
    irrigationDurationNI = ESPUI.number(
        "Irrigation duration", updateIrrigationDuration, ControlColor::Peterriver, settings.irrigationDuration, 0, 255);
    drainDurationNI = ESPUI.number(
        "Drainage duration", updateDrainDuration, ControlColor::Peterriver, settings.drainDuration, 0, 255);
    enableButton = ESPUI.switcher(
        "Irrigation enabled", updateEnableButton, ControlColor::Peterriver, settings.irrigationEnabled);
    ESPUI.button("Test pump", updateTestButton, ControlColor::Alizarin, "test");

    ESPUI.begin("Hydroponics controller");
}

void setupOTA()
{
    ArduinoOTA.onStart([]() {
        // Make sure pump does not continue to run
        disabledPump();

        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
        }
        else
        { // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
        {
            Serial.println("Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            Serial.println("Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            Serial.println("Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            Serial.println("Receive Failed");
        }
        else if (error == OTA_END_ERROR)
        {
            Serial.println("End Failed");
        }
    });
    ArduinoOTA.begin();
}

void setup()
{
    WiFi.hostname(hostname);

    Serial.begin(115200);

    // Setup pump pin
    pinMode(pumpIndicator, OUTPUT);
    pinMode(pump, OUTPUT);
    disabledPump();

    // setup eeprom and check if this is the first start
    beginEEPROM();

    if (firstStart)
    {
        settings.irrigationDuration = 20; // 20 seconds
        settings.drainDuration = 320; // 5 minutes
        settings.irrigationEnabled = false;
        storeSettings();
    }

    // Setup wifi
    WiFiConfig::addWifis(wifiMulti);
    bool connected = false;
    while (!connected)
    {
        connected = connectWiFi();
        // handle wifi or whatever the esp is doing
        yield();
    }

    // Setup DNS so we don't have to find and type the ip address
    MDNS.begin(hostname);

    // Setup OTA
    setupOTA();

    setupGUI();

    // Setup time client and force time sync
    timeClient.begin();
    setSyncProvider(getNTPTime);
    bool synced = false;
    while (!synced)
    {
        synced = forceTimeSync();
        delay(500);
    }
}

void logTime(const time_t currentTime, const uint8_t currentSecond)
{
    const uint8_t currentHour = hour(currentTime);

    char timeStamp[10];
    snprintf(timeStamp, 10, "%02d:%02d:%02d", currentHour, minute(currentTime), currentSecond);
    Serial.printf("Current time: %s\r\n", timeStamp);

    // Only update GUI if anyone is really looking at it
    if (ESPUI.ws->count() > 0)
    {
        ESPUI.updateLabel(timestampNI, timeStamp);
    }
}

void updateIrrigationInfo()
{
    // Only update GUI if anyone is really looking at it
    if (ESPUI.ws->count() > 0)
    {
        char info[20];
        snprintf(info, 20, "Irrigating for %ds", settings.irrigationDuration - secondsPassed);
        ESPUI.updateLabel(infoNI, info);
    }
}

void updateDrainingInfo()
{
    // Only update GUI if anyone is really looking at it
    if (ESPUI.ws->count() > 0)
    {
        char info[20];
        snprintf(info, 20, "Draining for %ds", settings.drainDuration - secondsPassed);
        ESPUI.updateLabel(infoNI, info);
    }
}

void irrigate()
{
    updateIrrigationInfo();
    if (secondsPassed >= settings.irrigationDuration)
    {
        secondsPassed = 0;
        irrigating = false;
        disabledPump();
    }
}

void drain()
{
    updateDrainingInfo();
    if (secondsPassed >= settings.drainDuration)
    {
        secondsPassed = 0;
        irrigating = true;
        enabledPump();
    }
}

void loop()
{
    const time_t currentTime = now();
    const uint8_t currentSecond = second(currentTime);

    if (settings.irrigationEnabled && currentSecond != lastSecond)
    {
        lastSecond = currentSecond;
        ++secondsPassed;

        logTime(currentTime, currentSecond);

        if (irrigating)
        {
            irrigate();
        }
        else
        {
            drain();
        }

        // put your main code here, to run repeatedly:
        timeClient.update(); // Handle NTP update
        // handle dns
        MDNS.update();
        // handle OTA
        ArduinoOTA.handle();
        // Check wifi
        connectWiFi();
    }

    // handle wifi or whatever the esp is doing
    yield();
}
