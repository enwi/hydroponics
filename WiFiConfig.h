#include <ESP8266WiFiMulti.h>

namespace WiFiConfig
{
    /**
     * @brief Add a set of wifis (SSID and password) to the given WiFiMulti
     *
     * @param wifiMulti WiFi manager
     */
    void addWifis(ESP8266WiFiMulti& wifiMulti)
    {
        wifiMulti.addAP("ssid_from_AP_1", "your_password_for_AP_1");
        // wifiMulti.addAP("ssid_from_AP_2", "your_password_for_AP_2");
        // wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");
    }
} // namespace WiFiConfig