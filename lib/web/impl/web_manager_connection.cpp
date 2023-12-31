/**
 * @file web_manager_connection.cpp
 * @author Akari (masiro.to.akari@gmail.com)
 * @brief
 * @version 0.23.1
 * @date 2023-03-12
 *
 * @copyright Copyright (c) 2023 / MaSiRo Project.
 *
 */
#include "web_manager_connection.hpp"

#include "../web_setting.hpp"

namespace MaSiRoProject
{
namespace Web
{
WebManagerConnection::WebManagerConnection()
{
}

////////////////////////////////////////////////////
// config function
////////////////////////////////////////////////////
void WebManagerConnection::config_address_ap(IPAddress ip, IPAddress subnet, IPAddress gateway)
{
    if (INADDR_NONE != ip) {
        if (INADDR_NONE != subnet) {
            this->_config_ap.flag_set = true;
            this->_config_ap.local_ip = ip;
            this->_config_ap.subnet   = subnet;
            this->_config_ap.gateway  = gateway;
        }
    }
}
void WebManagerConnection::config_address_sta(IPAddress ip, IPAddress subnet, IPAddress gateway)
{
    if (INADDR_NONE != ip) {
        if (INADDR_NONE != subnet) {
            this->_config_sta.flag_set = true;
            this->_config_sta.local_ip = ip;
            this->_config_sta.subnet   = subnet;
            this->_config_sta.gateway  = gateway;
        }
    }
}

////////////////////////////////////////////////////
// information function
////////////////////////////////////////////////////
std::vector<WebManagerConnection::NetworkList> WebManagerConnection::get_wifi_list()
{
    std::vector<NetworkList> list;
    int num = WiFi.scanNetworks();
    if (0 < num) {
        for (int i = 0; i < num; i++) {
            NetworkList item;
            String name = WiFi.SSID(i);
            if (0 != name.length()) {
                item.name       = name;
                item.encryption = WiFi.encryptionType(i);
                item.rssi       = WiFi.RSSI(i);
                item.rssi       = this->_get_rssi_as_quality(item.rssi);
                list.push_back(item);
            }
        }
        std::sort(list.begin(), list.end(), NetworkList::compar_rssi);
    }
    return list;
}
IPAddress WebManagerConnection::get_ip()
{
    switch (this->_mode) {
        case MODE_CONNECTION::MODE_CONNECTION_AP:
            return WiFi.softAPIP();
        case MODE_CONNECTION::MODE_CONNECTION_STA:
            return WiFi.localIP();
            break;
        case MODE_CONNECTION::MODE_CONNECTION_NONE:
        default:
            return INADDR_NONE;
    }
}
const char *WebManagerConnection::get_ssid()
{
    switch (this->_mode) {
        case MODE_CONNECTION::MODE_CONNECTION_AP:
            return WiFi.softAPSSID().c_str();
        case MODE_CONNECTION::MODE_CONNECTION_STA:
            return WiFi.SSID().c_str();
        case MODE_CONNECTION::MODE_CONNECTION_NONE:
        default:
            return "";
    }
}
bool WebManagerConnection::is_ap_mode()
{
    bool result = false;
    switch (this->_mode) {
        case MODE_CONNECTION::MODE_CONNECTION_AP:
            result = true;
            break;
        case MODE_CONNECTION::MODE_CONNECTION_STA:
        case MODE_CONNECTION::MODE_CONNECTION_NONE:
        default:
            break;
    }
    return result;
}

bool WebManagerConnection::is_connected(bool immediate)
{
    static unsigned long next_time = 0;
    static bool result             = false;
    if ((true == immediate) || (next_time < millis())) {
        next_time          = millis() + this->CONNECTION_CHECK_INTERVAL;
        wl_status_t status = WiFi.status();
        switch (status) {
            case wl_status_t::WL_CONNECTED:
            case wl_status_t::WL_SCAN_COMPLETED:
            case wl_status_t::WL_IDLE_STATUS:
                result = true;
                break;
            case wl_status_t::WL_CONNECTION_LOST:
            case wl_status_t::WL_CONNECT_FAILED:
            case wl_status_t::WL_NO_SSID_AVAIL:
            case wl_status_t::WL_NO_SHIELD:
            case wl_status_t::WL_DISCONNECTED:
            default:
                result = false;
                break;
        }
    }
    return result;
}

////////////////////////////////////////////////////
// connection function
////////////////////////////////////////////////////
bool WebManagerConnection::begin()
{
#ifdef SETTING_WIFI_HOSTNAME
    this->set_hostname(SETTING_WIFI_HOSTNAME);
#endif
    return this->_setup();
}

bool WebManagerConnection::reconnect(bool save)
{
    return this->reconnect(this->_ssid, this->_pass, this->_mode_ap, this->_auto_default_setting, save);
}
bool WebManagerConnection::reconnect_default(bool save)
{
    return this->reconnect(SETTING_WIFI_SSID_DEFAULT, SETTING_WIFI_PASS_DEFAULT, SETTING_WIFI_MODE_AP, SETTING_WIFI_MODE_AUTO_TRANSITIONS, save);
}

bool WebManagerConnection::reconnect(std::string ssid, std::string pass, bool ap_mode, bool auto_default, bool save)
{
    if ("" == ssid) {
        ssid = this->_ssid;
    }
    if ("" == pass) {
        pass = this->_pass;
    }
    bool result = this->_reconnect(ssid, pass, ap_mode, auto_default, save);
    if (false == result) {
        if ((this->_ssid != ssid) || (this->_pass != pass) || (this->_mode_ap != ap_mode) || (this->_auto_default_setting != auto_default)) {
            result = this->_reconnect(this->_ssid, this->_pass, this->_mode_ap, this->_auto_default_setting, false);
        }
    }
    return result;
}

bool WebManagerConnection::disconnect()
{
    bool result = true;
    switch (this->_mode) {
        case MODE_CONNECTION::MODE_CONNECTION_AP:
            result = WiFi.softAPdisconnect();
            break;
        case MODE_CONNECTION::MODE_CONNECTION_STA:
            if (true == WiFi.isConnected()) {
                result = WiFi.disconnect();
            }
            break;

        case MODE_CONNECTION::MODE_CONNECTION_NONE:
        default:
            break;
    }
    if (false == result) {
        int countdown = this->CONNECTION_TIMEOUT_MS;
        while (true == this->is_connected(true)) {
            delay(this->CONNECTION_TIMEOUT_INTERVAL);
            countdown -= this->CONNECTION_TIMEOUT_INTERVAL;
            if (0 >= countdown) {
                result = false;
                break;
            }
        }
    }
    result = !this->is_connected(true);
    if (true == result) {
        this->_mode = MODE_CONNECTION::MODE_CONNECTION_NONE;
    }
    return result;
}

////////////////////////////////////////////////////
// private function
////////////////////////////////////////////////////
int WebManagerConnection::_get_rssi_as_quality(int rssi)
{
    int quality = 0;

    if (rssi <= -100) {
        quality = 0;
    } else if (rssi >= -50) {
        quality = 100;
    } else {
        quality = 2 * (rssi + 100);
    }
    return quality;
}

bool WebManagerConnection::_reconnect(std::string ssid, std::string pass, bool ap_mode, bool auto_default, bool save)
{
    bool result = true;
    (void)this->disconnect();
    if (MODE_CONNECTION::MODE_CONNECTION_NONE == this->_mode) {
        if ("" != this->_hostname) {
            WiFi.setHostname(this->_hostname.c_str());
        }
        if (true == ap_mode) {
            if (true == this->_config_ap.flag_set) {
                result = WiFi.softAPConfig(this->_config_ap.local_ip, this->_config_ap.gateway, this->_config_ap.subnet);
            }
            if (true == result) {
                result = WiFi.softAP(ssid.c_str(), pass.c_str());
                if (true == result) {
                    this->_mode = MODE_CONNECTION::MODE_CONNECTION_AP;
                }
            }
        } else {
            if (true == this->_config_sta.flag_set) {
                result = WiFi.config(this->_config_sta.local_ip, this->_config_sta.gateway, this->_config_sta.subnet);
            }
            if (true == result) {
                WiFi.begin(ssid.c_str(), pass.c_str());
                int countdown = this->CONNECTION_TIMEOUT_MS;
                while (false == this->is_connected(true)) {
                    delay(this->CONNECTION_TIMEOUT_INTERVAL);
                    countdown -= this->CONNECTION_TIMEOUT_INTERVAL;
                    if (0 >= countdown) {
                        break;
                    }
                }
                result = this->is_connected(true);
                if (true == result) {
                    this->_mode = MODE_CONNECTION::MODE_CONNECTION_STA;
                }
            }
        }
        if (true == result) {
            if (true == save) {
                (void)this->save_information(ssid, pass, ap_mode, auto_default);
            } else {
                (void)this->set_information(ssid, pass, ap_mode, auto_default);
            }
        }
    }
    return result;
}

} // namespace Web
} // namespace MaSiRoProject
