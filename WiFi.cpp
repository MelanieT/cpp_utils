/*
 * WiFi.cpp
 *
 *  Created on: Feb 25, 2017
 *      Author: kolban
 */

//#define _GLIBCXX_USE_C99
#include <string>
#include <vector>
#include <algorithm>
#include "sdkconfig.h"
#include "WiFi.h"
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include "GeneralUtils.h"
#include <nvs_flash.h>
#include <lwip/dns.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <cstring>
#include <utility>
#include <esp_phy_init.h>


static const char *LOG_TAG = "WiFi";

using namespace std;

/*
static void setDNSServer(char *ip) {
    ip_addr_t dnsserver;
    ESP_LOGD(tag, "Setting DNS[%d] to %s", 0, ip);
    inet_pton(AF_INET, ip, &dnsserver);
    ESP_LOGD(tag, "ip of DNS is %.8x", *(uint32_t *)&dnsserver);
    dns_setserver(0, &dnsserver);
}
*/


/**
 * @brief Creates and uses a default event handler
 */
WiFi::WiFi()
    : ip(0), gw(0), netmask(0), m_pWifiEventHandler(nullptr), staInterface(nullptr), apInterface(nullptr)

{
    m_eventLoopStarted = false;
    m_initCalled = false;
    //m_pWifiEventHandler = new WiFiEventHandler();
    m_apConnectionStatus = UINT8_MAX;    // Are we connected to an access point?
} // WiFi


/**
 * @brief Deletes the event handler that was used by the class
 */
WiFi::~WiFi()
{
    if (m_pWifiEventHandler != nullptr)
    {
        delete m_pWifiEventHandler;
        m_pWifiEventHandler = nullptr;
    }
} // ~WiFi


/**
 * @brief Add a reference to a DNS server.
 *
 * Here we define a server that will act as a DNS server.  We can add two DNS
 * servers in total.  The first will be the primary, the second will be the backup.
 * The public Google DNS servers are "8.8.8.8" and "8.8.4.4".
 *
 * For example:
 *
 * @code{.cpp}
 * wifi.addDNSServer("8.8.8.8");
 * wifi.addDNSServer("8.8.4.4");
 * @endcode
 *
 * @param [in] ip The IP address of the DNS Server.
 * @return N/A.
 */
void WiFi::addDNSServer(const std::string &ip)
{
    addDNSServer(ip.c_str());
} // addDNSServer


void WiFi::addDNSServer(const char *ip)
{
    ip_addr_t dns_server;
    if (inet_pton(AF_INET, ip, &dns_server))
    {
        addDNSServer(dns_server);
    }
} // addDNSServer


void WiFi::addDNSServer(ip_addr_t ip)
{
    ESP_LOGD(LOG_TAG, "Setting DNS[%d] to %d.%d.%d.%d", m_dnsCount, ((uint8_t *) (&ip))[0], ((uint8_t *) (&ip))[1],
             ((uint8_t *) (&ip))[2], ((uint8_t *) (&ip))[3]);
    init(WIFI_MODE_STA);
    ::dns_setserver(m_dnsCount, &ip);
    m_dnsCount++;
    m_dnsCount %= 2;
} // addDNSServer


/**
 * @brief Set a reference to a DNS server.
 *
 * Here we define a server that will act as a DNS server.  We use numdns to specify which DNS server to set
 *
 * For example:
 *
 * @code{.cpp}
 * wifi.setDNSServer(0, "8.8.8.8");
 * wifi.setDNSServer(1, "8.8.4.4");
 * @endcode
 *
 * @param [in] numdns The DNS number we wish to set
 * @param [in] ip The IP address of the DNS Server.
 * @return N/A.
 */
void WiFi::setDNSServer(int numdns, const std::string &ip)
{
    setDNSServer(numdns, ip.c_str());
} // setDNSServer


void WiFi::setDNSServer(int numdns, const char *ip)
{
    ip_addr_t dns_server;
    if (inet_pton(AF_INET, ip, &dns_server))
    {
        setDNSServer(numdns, dns_server);
    }
} // setDNSServer


void WiFi::setDNSServer(int numdns, ip_addr_t ip)
{
    ESP_LOGD(LOG_TAG, "Setting DNS[%d] to %d.%d.%d.%d", m_dnsCount, ((uint8_t *) (&ip))[0], ((uint8_t *) (&ip))[1],
             ((uint8_t *) (&ip))[2], ((uint8_t *) (&ip))[3]);
    init(WIFI_MODE_STA);
    ::dns_setserver(numdns, &ip);
} // setDNSServer


/**
 * @brief Connect to an external access point.
 *
 * The event handler will be called back with the outcome of the connection.
 *
 * @param [in] ssid The network SSID of the access point to which we wish to connect.
 * @param [in] password The password of the access point to which we wish to connect.
 * @param [in] waitForConnection Block until the connection has an outcome.
 * @param [in] mode WIFI_MODE_AP for normal or WIFI_MODE_APSTA if you want to keep an Access Point running while you connect
 * @return ESP_OK if we are now connected and wifi_err_reason_t if not.
 */
esp_err_t WiFi::connectSTA(const std::string &ssid, const std::string &password, bool waitForConnection, bool testConnection)
{
    init((wifi_mode_t )(m_wifiMode | WIFI_MODE_STA));

    ESP_LOGD(LOG_TAG, ">> connectSTA");

    if (m_wifiMode & WIFI_MODE_STA)
    {
        ESP_LOGI(LOG_TAG, "Station is active, shutting it down");
        esp_wifi_set_mode((wifi_mode_t) (m_wifiMode & (~WIFI_MODE_STA)));
    }

    // If we don't do this, changes in network topology or availability could make the device unconnectable.
    esp_phy_erase_cal_data_in_nvs();

    m_apConnectionStatus = UINT8_MAX;

    m_wifiMode = (wifi_mode_t )(m_wifiMode | WIFI_MODE_STA);
    esp_err_t errRc = ::esp_wifi_set_mode(m_wifiMode);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "esp_wifi_set_mode: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }

    if (ip != 0 && gw != 0 && netmask != 0)
    {
        esp_netif_dhcpc_stop(staInterface); // Don't run a DHCP client

        esp_netif_ip_info_t ipInfo;
        ipInfo.ip.addr = ip;
        ipInfo.gw.addr = gw;
        ipInfo.netmask.addr = netmask;

        esp_netif_set_ip_info(staInterface, &ipInfo);
    }

    wifi_config_t sta_config;
    ::memset(&sta_config, 0, sizeof(sta_config));
    ::strcpy((char *)sta_config.sta.ssid, ssid.c_str());
    ::strcpy((char *)sta_config.sta.password, password.c_str());
    sta_config.sta.bssid_set = false;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
//    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    errRc = ::esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "esp_wifi_set_config: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }

    if (!waitForConnection)
    {
        errRc = ::esp_wifi_connect();
        if (errRc != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "esp_wifi_connect: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
            abort();
        }

        return m_apConnectionStatus; // Somewhat meaningless here
    }

    m_testConnection = testConnection;
    m_connectFinished.take("connectAP");   // Take the semaphore to wait for a connection.
    do
    {
        ESP_LOGD(LOG_TAG, "esp_wifi_connect");
        errRc = ::esp_wifi_connect();
        if (errRc != ESP_OK)
        {
            // There will be no connected event. Give the semaphore back.
            m_connectFinished.give();
            m_testConnection = false;
            ESP_LOGE(LOG_TAG, "esp_wifi_connect: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
            return errRc;
        }

        // Here we try to take the semaphore again. This will only succeed when the
        // wifi event handler has given it.
        if (m_connectFinished.take(5000, "connectAP"))
            break;

    } while (!m_testConnection); // retry if not connected within 5s and not just testing
    // Have to give it again for next time.

    if (m_testConnection)
    {
        esp_err_t ret = m_apConnectionStatus;
        // We still hold the semaphore, disconnect the test connection
        disconnectSTA();
        // Wait for it to happen
        while(m_connectFinished.take(5000, "connectAP"))
            ;

        m_testConnection = false;

        m_connectFinished.give();

        m_apConnectionStatus = UINT8_MAX;

        return ret;
    }
    m_connectFinished.give();

    ESP_LOGD(LOG_TAG, "<< connectAP");
    return m_apConnectionStatus;  // Return ESP_OK if we are now connected and wifi_err_reason_t if not.
} // connectSTA

esp_err_t WiFi::disconnectSTA()
{
    if (!(m_wifiMode & WIFI_MODE_STA))
        return ESP_OK;

    esp_err_t errRc  = esp_wifi_disconnect();

    m_wifiMode = (wifi_mode_t)(m_wifiMode & (~WIFI_MODE_STA));

    if (m_wifiMode != WIFI_MODE_NULL)
    {
        errRc = ::esp_wifi_set_mode(m_wifiMode);
        if (errRc != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "esp_wifi_set_mode: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
            abort();
        }
    }

    return errRc;
}

/**
 * @brief Dump diagnostics to the log.
 */
void WiFi::dump()
{
    ESP_LOGD(LOG_TAG, "WiFi Dump");
    ESP_LOGD(LOG_TAG, "---------");
    char ipAddrStr[30];
    const ip_addr *ip = ::dns_getserver(0);
    inet_ntop(AF_INET, &ip, ipAddrStr, sizeof(ipAddrStr));
    ESP_LOGD(LOG_TAG, "DNS Server[0]: %s", ipAddrStr);
} // dump


/**
 * @brief Returns whether wifi is connected to an access point
 */
bool WiFi::isConnectedToAP() const
{
    return m_apConnectionStatus;
} // isConnected


/**
 * @brief Primary event handler interface.
 */
/* STATIC */ void WiFi::eventHandler(void *ctx, esp_event_base_t base, long event_id, void *event_data)
{
    // This is the common event handler that we have provided for all event processing.  It is called for every event
    // that is received by the WiFi subsystem.  The "ctx" parameter is an instance of the current WiFi object that we are
    // processing.  We can then retrieve the specific/custom event handler from within it and invoke that.  This then makes this
    // an indirection vector to the real caller.

    WiFi *pWiFi = (WiFi *) ctx;   // retrieve the WiFi object from the passed in context.

    // Invoke the event handler.
    if (pWiFi->m_pWifiEventHandler != nullptr)
    {
        pWiFi->m_pWifiEventHandler->getEventHandler()(pWiFi->m_pWifiEventHandler, base, event_id, event_data);
    }

    // If the event we received indicates that we now have an IP address or that a connection was disconnected then unlock the mutex that
    // indicates we are waiting for a connection complete.
    if (base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            auto info = (wifi_event_sta_disconnected_t *) event_data;
            pWiFi->m_apConnectionStatus = info->reason;
            pWiFi->m_connectFinished.give();
        }
    }
    else if (base == IP_EVENT)
    {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            pWiFi->m_apConnectionStatus = ESP_OK;
            pWiFi->m_connectFinished.give();
        }
    }
} // eventHandler


/**
 * @brief Get the AP IP Info.
 * @return The AP IP Info.
 */
esp_netif_ip_info_t WiFi::getApIpInfo()
{
    //init();
    esp_netif_ip_info_t ipInfo;
    esp_netif_get_ip_info(apInterface, &ipInfo);
    return ipInfo;
} // getApIpInfo


/**
 * @brief Get the MAC address of the AP interface.
 * @return The MAC address of the AP interface.
 */
std::string WiFi::getApMac()
{
    uint8_t mac[6];
    //init();
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    char mac_str[10];
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return {mac_str};
} // getApMac


/**
 * @brief Get the AP SSID.
 * @return The AP SSID.
 */
std::string WiFi::getApSSID()
{
    wifi_config_t conf;
    //init();
    esp_wifi_get_config(WIFI_IF_AP, &conf);
    return {(char *) conf.sta.ssid};
} // getApSSID


/**
 * @brief Get the current ESP32 IP form AP.
 * @return The ESP32 IP.
 */
std::string WiFi::getApIp()
{
    esp_netif_ip_info_t ipInfo = getApIpInfo();
    char ipAddrStr[30];
    inet_ntop(AF_INET, &ipInfo.ip.addr, ipAddrStr, sizeof(ipAddrStr));
    return {ipAddrStr};
} // getStaIp

/**
 * @brief Get the current AP netmask.
 * @return The Netmask IP.
 */
std::string WiFi::getApNetmask()
{
    esp_netif_ip_info_t ipInfo = getApIpInfo();
    char ipAddrStr[30];
    inet_ntop(AF_INET, &ipInfo.netmask.addr, ipAddrStr, sizeof(ipAddrStr));
    return {ipAddrStr};
} // getStaNetmask

/**
 * @brief Get the current AP Gateway IP.
 * @return The Gateway IP.
 */
std::string WiFi::getApGateway()
{
    esp_netif_ip_info_t ipInfo = getApIpInfo();
    char ipAddrStr[30];
    inet_ntop(AF_INET, &ipInfo.gw.addr, ipAddrStr, sizeof(ipAddrStr));
    return {ipAddrStr};
} // getStaGateway

/**
 * @brief Lookup an IP address by host name.
 *
 * @param [in] hostName The hostname to resolve.
 *
 * @return The IP address of the host or 0.0.0.0 if not found.
 */
struct in_addr WiFi::getHostByName(const std::string &hostName)
{
    return getHostByName(hostName.c_str());
} // getHostByName


struct in_addr WiFi::getHostByName(const char *hostName)
{
    struct in_addr retAddr = {};
    struct hostent *he = gethostbyname(hostName);
    if (he == nullptr)
    {
        retAddr.s_addr = 0;
        ESP_LOGD(LOG_TAG, "Unable to resolve %s - %d", hostName, h_errno);
    }
    else
    {
        retAddr = *(struct in_addr *) (he->h_addr_list[0]);
        ESP_LOGD(LOG_TAG, "resolved %s to %.8x", hostName, *(uint32_t *) &retAddr);
    }
    return retAddr;
} // getHostByName


/**
 * @brief Get the WiFi Mode.
 * @return The WiFi Mode.
 */
std::string WiFi::getMode()
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    switch (mode)
    {
    case WIFI_MODE_NULL:
        return "WIFI_MODE_NULL";
    case WIFI_MODE_STA:
        return "WIFI_MODE_STA";
    case WIFI_MODE_AP:
        return "WIFI_MODE_AP";
    case WIFI_MODE_APSTA:
        return "WIFI_MODE_APSTA";
    default:
        return "unknown";
    }
} // getMode


/**
 * @brief Get the STA IP Info.
 * @return The STA IP Info.
 */
esp_netif_ip_info_t WiFi::getStaIpInfo()
{
    esp_netif_ip_info_t ipInfo;
    esp_netif_get_ip_info(staInterface, &ipInfo);
    return ipInfo;
} // getStaIpInfo


/**
 * @brief Get the current ESP32 IP form STA.
 * @return The ESP32 IP.
 */
std::string WiFi::getStaIp()
{
    esp_netif_ip_info_t ipInfo = getStaIpInfo();
    char ipAddrStr[30];
    inet_ntop(AF_INET, &ipInfo.ip.addr, ipAddrStr, sizeof(ipAddrStr));
    return {ipAddrStr};
} // getStaIp


/**
 * @brief Get the current STA netmask.
 * @return The Netmask IP.
 */
std::string WiFi::getStaNetmask()
{
    esp_netif_ip_info_t ipInfo = getStaIpInfo();
    char ipAddrStr[30];
    inet_ntop(AF_INET, &ipInfo.netmask.addr, ipAddrStr, sizeof(ipAddrStr));
    return {ipAddrStr};
} // getStaNetmask


/**
 * @brief Get the current STA Gateway IP.
 * @return The Gateway IP.
 */
std::string WiFi::getStaGateway()
{
    esp_netif_ip_info_t ipInfo = getStaIpInfo();
    char ipAddrStr[30];
    inet_ntop(AF_INET, &ipInfo.gw.addr, ipAddrStr, sizeof(ipAddrStr));
    return {ipAddrStr};
} // getStaGateway


/**
 * @brief Get the MAC address of the STA interface.
 * @return The MAC address of the STA interface.
 */
std::string WiFi::getStaMac()
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_str[10];
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return {mac_str};
} // getStaMac


/**
 * @brief Get the STA SSID.
 * @return The STA SSID.
 */
std::string WiFi::getStaSSID()
{
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    return {(char *) conf.ap.ssid};
} // getStaSSID


/**
 * @brief Initialize WiFi.
 */
/* PRIVATE */ void WiFi::init(wifi_mode_t mode)
{
    // The new architecture of the SDK features multiple handlers. Re-setting them can cause
    // a silent hang, and isn't useful anyway.
    if (!m_eventLoopStarted)
    {
        esp_event_loop_create_default();
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, WiFi::eventHandler, this);
        esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, WiFi::eventHandler, this);
        m_eventLoopStarted = true;
    }
    // Now, one way or another, the event handler is WiFi::eventHandler.

    if (!m_initCalled)
    {
        m_initCalled = true;

        ::nvs_flash_init();
        ::esp_netif_init();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t errRc = ::esp_wifi_init(&cfg);
        if (errRc != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "esp_wifi_init: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
            abort();
        }

        errRc = ::esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (errRc != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "esp_wifi_set_storage: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
            abort();
        }

        staInterface = esp_netif_create_default_wifi_sta();
        assert(staInterface);
        if (!m_stationHostname.empty())
            esp_netif_set_hostname(staInterface, m_stationHostname.c_str());

        apInterface = esp_netif_create_default_wifi_ap();
        assert(apInterface);

        esp_wifi_set_mode(mode);

        errRc = ::esp_wifi_start();
        if (errRc != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "esp_wifi_start: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
            abort();
        }
    }
} // init

/**
 * @brief Perform a WiFi scan looking for access points.
 *
 * An access point scan is performed and a vector of WiFi access point records
 * is built and returned with one record per found scan instance.  The scan is
 * performed in a blocking fashion and will not return until the set of scanned
 * access points has been built.
 *
 * @return A vector of WiFiAPRecord instances.
 */
std::vector<WiFiAPRecord> WiFi::scan()
{
    ESP_LOGD(LOG_TAG, ">> scan");
    std::vector<WiFiAPRecord> apRecords;

    init((wifi_mode_t )(m_wifiMode | WIFI_MODE_STA));

    esp_err_t errRc = ::esp_wifi_set_mode((wifi_mode_t )(m_wifiMode | WIFI_MODE_STA));
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "esp_wifi_set_mode: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }

    wifi_scan_config_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.show_hidden = true;

    esp_err_t rc = ::esp_wifi_scan_start(&conf, true);
    if (rc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "esp_wifi_scan_start: %d", rc);

        esp_wifi_set_mode(m_wifiMode);

        return apRecords;
    }

    uint16_t apCount;  // Number of access points available.
    rc = ::esp_wifi_scan_get_ap_num(&apCount);
    if (rc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "esp_wifi_scan_get_ap_num: %d", rc);
        esp_wifi_set_mode(m_wifiMode);
        return apRecords;
    }
    else
    {
        ESP_LOGD(LOG_TAG, "Count of found access points: %d", apCount);
    }

    if (!apCount)
    {
        esp_wifi_set_mode(m_wifiMode);
        return vector<WiFiAPRecord>();
    }
    auto *list = (wifi_ap_record_t *) malloc(sizeof(wifi_ap_record_t) * apCount);
    if (list == nullptr)
    {
        ESP_LOGE(LOG_TAG, "Failed to allocate memory");
        esp_wifi_set_mode(m_wifiMode);
        return apRecords;
    }

    errRc = ::esp_wifi_scan_get_ap_records(&apCount, list);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "esp_wifi_scan_get_ap_records: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }

    for (uint16_t i = 0; i < apCount; i++)
    {
        WiFiAPRecord wifiAPRecord;
        memcpy(wifiAPRecord.m_bssid, list[i].bssid, 6);
        wifiAPRecord.m_ssid = std::string((char *) list[i].ssid);
        wifiAPRecord.m_authMode = list[i].authmode;
        wifiAPRecord.m_rssi = list[i].rssi;
        apRecords.push_back(wifiAPRecord);
    }
    free(list);   // Release the storage allocated to hold the records.
    std::sort(apRecords.begin(),
              apRecords.end(),
              [](const WiFiAPRecord &lhs, const WiFiAPRecord &rhs) { return lhs.m_rssi > rhs.m_rssi; });
    esp_wifi_set_mode(m_wifiMode);
    return apRecords;
} // scan


/**
 * @brief Start being an access point.
 *
 * @param[in] ssid The SSID to use to advertize for stations.
 * @param[in] password The password to use for station connections.
 * @param[in] auth The authorization mode for access to this access point.  Options are:
 * * WIFI_AUTH_OPEN
 * * WIFI_AUTH_WPA_PSK
 * * WIFI_AUTH_WPA2_PSK
 * * WIFI_AUTH_WPA_WPA2_PSK
 * * WIFI_AUTH_WPA2_ENTERPRISE
 * * WIFI_AUTH_WEP
 * @return N/A.
 */
void WiFi::startAP(const std::string &ssid, const std::string &password, wifi_auth_mode_t auth)
{
    startAP(ssid, password, auth, 0, false, 4);
} // startAP


/**
 * @brief Start being an access point.
 *
 * @param[in] ssid The SSID to use to advertize for stations.
 * @param[in] password The password to use for station connections.
 * @param[in] auth The authorization mode for access to this access point.  Options are:
 * * WIFI_AUTH_OPEN
 * * WIFI_AUTH_WPA_PSK
 * * WIFI_AUTH_WPA2_PSK
 * * WIFI_AUTH_WPA_WPA2_PSK
 * * WIFI_AUTH_WPA2_ENTERPRISE
 * * WIFI_AUTH_WEP
 * @param[in] channel from the access point.
 * @param[in] is the ssid hidden, ore not.
 * @param[in] limiting number of clients.
 * @return N/A.
 */
void WiFi::startAP(const std::string &ssid, const std::string &password, wifi_auth_mode_t auth, uint8_t channel,
                   bool ssid_hidden, uint8_t max_connection)
{
    ESP_LOGI(LOG_TAG, ">> startAP: ssid: %s", ssid.c_str());

    init((wifi_mode_t)(m_wifiMode | WIFI_MODE_AP));

    if (m_wifiMode & WIFI_MODE_AP)
        stopAP();

    m_wifiMode = (wifi_mode_t)(m_wifiMode | WIFI_MODE_AP);

    esp_err_t errRc = ::esp_wifi_set_mode(m_wifiMode);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "esp_wifi_set_mode: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }

    // Build the apConfig structure.
    wifi_config_t apConfig;
    ::memset(&apConfig, 0, sizeof(apConfig));
    ::memcpy(apConfig.ap.ssid, ssid.data(), ssid.size());
    apConfig.ap.ssid_len = ssid.size();
    ::memcpy(apConfig.ap.password, password.data(), password.size());
    apConfig.ap.channel = channel;
    apConfig.ap.authmode = auth;
    apConfig.ap.ssid_hidden = (uint8_t) ssid_hidden;
    apConfig.ap.max_connection = max_connection;
    apConfig.ap.beacon_interval = 100;

    errRc = ::esp_wifi_set_config(WIFI_IF_AP, &apConfig);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "esp_wifi_set_config: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }

//    errRc = esp_netif_dhcps_start(apInterface);
//    if (errRc != ESP_OK)
//    {
//        ESP_LOGE(LOG_TAG, "esp_netif_dhcps_start: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
//    }
//
    ESP_LOGI(LOG_TAG, "<< startAP");
} // startAP

void WiFi::stopAP()
{
    if (!(m_wifiMode & WIFI_MODE_AP))
        return;

    m_wifiMode = (wifi_mode_t)(m_wifiMode & (~WIFI_MODE_AP));

    esp_netif_dhcps_stop(apInterface);

    if (m_wifiMode != WIFI_MODE_NULL)
    {
        esp_err_t errRc = ::esp_wifi_set_mode(m_wifiMode);
        if (errRc != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "esp_wifi_set_mode: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
            abort();
        }
    }
    else
    {
        esp_err_t errRc = ::esp_wifi_set_mode(WIFI_MODE_STA);
        if (errRc != ESP_OK)
        {
            ESP_LOGE(LOG_TAG, "esp_wifi_set_mode: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
            abort();
        }
    }
}

/**
 * @brief Set the event handler to use to process detected events.
 * @param[in] wifiEventHandler The class that will be used to process events.
 */
void WiFi::setWifiEventHandler(WiFiEventHandler *wifiEventHandler)
{
    ESP_LOGD(LOG_TAG, ">> setWifiEventHandler: 0x%d", (uint32_t) wifiEventHandler);
    this->m_pWifiEventHandler = wifiEventHandler;
    ESP_LOGD(LOG_TAG, "<< setWifiEventHandler");
} // setWifiEventHandler


/**
 * @brief Set the IP info and enable DHCP if ip != 0. If called with ip == 0 then DHCP is enabled.
 * If called with bad values it will do nothing.
 *
 * Do not call this method if we are being an access point ourselves.
 *
 * For example, prior to calling `connectAP()` we could invoke:
 *
 * @code{.cpp}
 * myWifi.setIPInfo("192.168.1.99", "192.168.1.1", "255.255.255.0");
 * @endcode
 *
 * @param [in] ip IP address value.
 * @param [in] gw Gateway value.
 * @param [in] netmask Netmask value.
 * @return N/A.
 */
void WiFi::setIPInfo(const std::string &ip, const std::string &gw, const std::string &netmask)
{
    setIPInfo(ip.c_str(), gw.c_str(), netmask.c_str());
} // setIPInfo


void WiFi::setIPInfo(const char *ip, const char *gw, const char *netmask)
{
    uint32_t new_ip;
    uint32_t new_gw;
    uint32_t new_netmask;

    auto success = (bool) inet_pton(AF_INET, ip, &new_ip);
    success = success && inet_pton(AF_INET, gw, &new_gw);
    success = success && inet_pton(AF_INET, netmask, &new_netmask);

    if (!success)
        return;

    setIPInfo(new_ip, new_gw, new_netmask);
} // setIPInfo


/**
 * @brief Set the IP Info based on the IP address, gateway and netmask.
 * @param [in] ip The IP address of our ESP32.
 * @param [in] gw The gateway we should use.
 * @param [in] netmask Our TCP/IP netmask value.
 */
void WiFi::setIPInfo(uint32_t ip, uint32_t gw, uint32_t netmask)
{
    init((wifi_mode_t)(m_wifiMode | WIFI_MODE_STA));

    this->ip = ip;
    this->gw = gw;
    this->netmask = netmask;

    if (ip != 0 && gw != 0 && netmask != 0)
    {
        esp_netif_ip_info_t ipInfo;
        ipInfo.ip.addr = ip;
        ipInfo.gw.addr = gw;
        ipInfo.netmask.addr = netmask;
        ::esp_netif_dhcpc_stop(staInterface);
        ::esp_netif_set_ip_info(staInterface, &ipInfo);
    }
    else
    {
        this->ip = 0;
        ::esp_netif_dhcpc_start(staInterface);
    }
}
// setIPInfo

esp_netif_t *WiFi::getStationIf()
{
    return staInterface;
}

esp_netif_t *WiFi::getAccessPointIf()
{
    return apInterface;
}

void WiFi::setStationHostname(std::string hostname)
{
    m_stationHostname = std::move(hostname);
}

/**
 * @brief Return a string representation of the WiFi access point record.
 *
 * @return A string representation of the WiFi access point record.
 */
std::string WiFiAPRecord::toString()
{
    std::string auth;
    switch (getAuthMode())
    {
    case WIFI_AUTH_OPEN:
        auth = "WIFI_AUTH_OPEN";
        break;
    case WIFI_AUTH_WEP:
        auth = "WIFI_AUTH_WEP";
        break;
    case WIFI_AUTH_WPA_PSK:
        auth = "WIFI_AUTH_WPA_PSK";
        break;
    case WIFI_AUTH_WPA2_PSK:
        auth = "WIFI_AUTH_WPA2_PSK";
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        auth = "WIFI_AUTH_WPA_WPA2_PSK";
        break;
    default:
        auth = "<unknown>";
        break;
    }
//	std::stringstream s;
//	s<< "ssid: " << m_ssid << ", auth: " << auth << ", rssi: " << m_rssi;
    static char info_str[6 + 32 + 8 + 22 + 8 + 3 + 1]; // Keep this off the stack
    sprintf(info_str, "ssid: %s, auth: %s, rssi: %d", m_ssid.c_str(), auth.c_str(), (int) m_rssi);
    return {info_str};
} // toString


MDNS::MDNS()
{
    esp_err_t errRc = ::mdns_init();
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "mdns_init: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }
}


MDNS::~MDNS() = default;


/**
 * @brief Define the service for mDNS.
 *
 * @param [in] service
 * @param [in] proto
 * @param [in] port
 * @return N/A.
 */
void MDNS::serviceAdd(const std::string &instance, const std::string &service, const std::string &proto, uint16_t port)
{
    serviceAdd(instance.c_str(), service.c_str(), proto.c_str(), port);
} // serviceAdd


void MDNS::serviceInstanceSet(const std::string &service, const std::string &proto, const std::string &instance)
{
    serviceInstanceSet(service.c_str(), proto.c_str(), instance.c_str());
} // serviceInstanceSet


void MDNS::servicePortSet(const std::string &service, const std::string &proto, uint16_t port)
{
    servicePortSet(service.c_str(), proto.c_str(), port);
} // servicePortSet


void MDNS::serviceRemove(const std::string &service, const std::string &proto)
{
    serviceRemove(service.c_str(), proto.c_str());
} // serviceRemove


/**
 * @brief Set the mDNS hostname.
 *
 * @param [in] hostname The host name to set against the mDNS.
 * @return N/A.
 */
void MDNS::setHostname(const std::string &hostname)
{
    setHostname(hostname.c_str());
} // setHostname


/**
 * @brief Set the mDNS instance.
 *
 * @param [in] instance The instance name to set against the mDNS.
 * @return N/A.
 */
void MDNS::setInstance(const std::string &instance)
{
    setInstance(instance.c_str());
} // setInstance


/**
 * @brief Define the service for mDNS.
 *
 * @param [in] service
 * @param [in] proto
 * @param [in] port
 * @return N/A.
 */
void MDNS::serviceAdd(const char *instance, const char *service, const char *proto, uint16_t port)
{
    esp_err_t errRc = ::mdns_service_add(instance, service, proto, port, nullptr, 0);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "mdns_service_add: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }
} // serviceAdd


void MDNS::serviceInstanceSet(const char *service, const char *proto, const char *instance)
{
    esp_err_t errRc = ::mdns_service_subtype_add_for_host(nullptr, service, proto, nullptr, instance);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "mdns_service_instance_set: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }
} // serviceInstanceSet


void MDNS::servicePortSet(const char *service, const char *proto, uint16_t port)
{
    esp_err_t errRc = ::mdns_service_port_set(service, proto, port);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "mdns_service_port_set: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }
} // servicePortSet


void MDNS::serviceRemove(const char *service, const char *proto)
{
    esp_err_t errRc = ::mdns_service_remove(service, proto);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "mdns_service_remove: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }
} // serviceRemove


/**
 * @brief Set the mDNS hostname.
 *
 * @param [in] hostname The host name to set against the mDNS.
 * @return N/A.
 */
void MDNS::setHostname(const char *hostname)
{
    esp_err_t errRc = ::mdns_hostname_set(hostname);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "mdns_set_hostname: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }
} // setHostname


/**
 * @brief Set the mDNS instance.
 *
 * @param [in] instance The instance name to set against the mDNS.
 * @return N/A.
 */
void MDNS::setInstance(const char *instance)
{
    esp_err_t errRc = ::mdns_instance_name_set(instance);
    if (errRc != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "mdns_set_instance: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
        abort();
    }
} // setInstance
