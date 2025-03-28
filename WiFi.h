/*
 * WiFi.h
 *
 *  Created on: Feb 25, 2017
 *      Author: kolban
 */

#ifndef MAIN_WIFI_H_
#define MAIN_WIFI_H_
#include "sdkconfig.h"

#include <string>
#include <vector>
#include <mdns.h>
#include <esp_err.h>
#include <lwip/ip_addr.h>
#include "FreeRTOS.h"
#include "WiFiEventHandler.h"

/**
 * @brief Manage mDNS server.
 */
class MDNS {
public:
	MDNS();
	~MDNS();
    void serviceAdd(const std::string& instance, const std::string& service, const std::string& proto, uint16_t port);
	void serviceInstanceSet(const std::string& service, const std::string& proto, const std::string& instance);
	void servicePortSet(const std::string& service, const std::string& proto, uint16_t port);
	void serviceRemove(const std::string& service, const std::string& proto);
	void setHostname(const std::string& hostname);
	void setInstance(const std::string& instance);
	// If we the above functions with a basic char*, a copy would be created into an std::string,
	// making the whole thing require twice as much processing power and speed
	void serviceAdd(const char *instance, const char* service, const char* proto, uint16_t port);
	void serviceInstanceSet(const char* service, const char* proto, const char* instance);
	void servicePortSet(const char* service, const char* proto, uint16_t port);
	void serviceRemove(const char* service, const char* proto);
	void setHostname(const char* hostname);
	void setInstance(const char* instance);

private:
};

class WiFiAPRecord {
public:
	friend class WiFi;

	/**
	 * @brief Get the auth mode.
	 * @return The auth mode.
	 */
	wifi_auth_mode_t getAuthMode() {
		return m_authMode;
	}

	/**
	 * @brief Get the RSSI.
	 * @return the RSSI.
	 */
	[[nodiscard]] int8_t getRSSI() const {
		return m_rssi;
	}

	/**
	 * @brief Get the SSID.
	 * @return the SSID.
	 */
	std::string getSSID() {
		return m_ssid;
	}

	std::string toString();

private:
	uint8_t        m_bssid[6];
	int8_t         m_rssi;
	std::string    m_ssid;
	wifi_auth_mode_t m_authMode;

};

/**
 * @brief %WiFi driver.
 *
 * Encapsulate control of %WiFi functions.
 *
 * Here is an example fragment that illustrates connecting to an access point.
 * @code{.cpp}
 * #include <WiFi.h>
 * #include <WiFiEventHandler.h>
 *
 * class TargetWiFiEventHandler: public WiFiEventHandler {
 *    esp_err_t staGotIp(system_event_sta_got_ip_t event_sta_got_ip) {
 *       ESP_LOGD(tag, "MyWiFiEventHandler(Class): staGotIp");
 *       // Do something ...
 *       return ESP_OK;
 *    }
 * };
 *
 * WiFi wifi;
 *
 * TargetWiFiEventHandler *eventHandler = new TargetWiFiEventHandler();
 * wifi.setWifiEventHandler(eventHandler);
 * wifi.connectAP("myssid", "mypassword");
 * @endcode
 */
class WiFi {
private:
	static void         eventHandler(void *ctx, esp_event_base_t base, long event_id, void *event_data);
	void                init(wifi_mode_t mode);
	uint32_t            ip;
	uint32_t            gw;
	uint32_t            netmask;
	WiFiEventHandler*   m_pWifiEventHandler;
	uint8_t             m_dnsCount = 0;
	volatile bool       m_eventLoopStarted;
	volatile bool       m_initCalled;
	uint8_t             m_apConnectionStatus;   // ESP_OK = we are connected to an access point.  Otherwise receives wifi_err_reason_t.
  	FreeRTOS::Semaphore m_connectFinished = FreeRTOS::Semaphore("ConnectFinished");
    esp_netif_t *       staInterface;
    esp_netif_t *       apInterface;
    std::string         m_stationHostname;
    wifi_mode_t         m_wifiMode = WIFI_MODE_NULL;
    bool                m_testConnection = false; // For testing wifi credentials

public:
	WiFi();
	~WiFi();
	void                      addDNSServer(const std::string& ip);
	void                      addDNSServer(const char* ip);
	void                      addDNSServer(ip_addr_t ip);
	void                      setDNSServer(int numdns, const std::string& ip);
	void                      setDNSServer(int numdns, const char* ip);
	void                      setDNSServer(int numdns, ip_addr_t ip);
	static struct in_addr     getHostByName(const std::string& hostName);
	static struct in_addr     getHostByName(const char* hostName);
	esp_err_t                 connectSTA(const std::string& ssid, const std::string& password, bool waitForConnection = true, bool testConnection = false);
    esp_err_t                 disconnectSTA();
	static void               dump();
	[[nodiscard]] bool        isConnectedToAP() const;
    esp_netif_t *             getStationIf();
    esp_netif_t *             getAccessPointIf();
	static std::string        getApMac();
	esp_netif_ip_info_t       getApIpInfo();
	static std::string        getApSSID();
	std::string               getApIp();
	std::string               getApNetmask();
	std::string               getApGateway();
	static std::string        getMode();
	esp_netif_ip_info_t       getStaIpInfo();
	static std::string        getStaMac();
	static std::string        getStaSSID();
	std::string               getStaIp();
	std::string               getStaNetmask();
	std::string               getStaGateway();
    void                      setStationHostname(std::string hostname);

	std::vector<WiFiAPRecord> scan();
	void                      startAP(const std::string& ssid, const std::string& passwd, wifi_auth_mode_t auth = WIFI_AUTH_OPEN);
	void                      startAP(const std::string& ssid, const std::string& passwd, wifi_auth_mode_t auth, uint8_t channel, bool ssid_hidden, uint8_t max_connection);
    void                      stopAP();
	void                      setIPInfo(const std::string& ip, const std::string& gw, const std::string& netmask);
	void                      setIPInfo(const char* ip, const char* gw, const char* netmask);
	void                      setIPInfo(uint32_t ip, uint32_t gw, uint32_t netmask);
	void                      setWifiEventHandler(WiFiEventHandler* wifiEventHandler);


};

#endif /* MAIN_WIFI_H_ */
