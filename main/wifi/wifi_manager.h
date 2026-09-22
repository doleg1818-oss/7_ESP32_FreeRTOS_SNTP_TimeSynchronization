/*
 * wifi_manager.h
 *
 *  Created on: Aug 15, 2026
 *      Author: Olegd
 */

#ifndef MAIN_WIFI_WIFI_MANAGER_H_
#define MAIN_WIFI_WIFI_MANAGER_H_

#include "stdbool.h"
#include "stdint.h"
#include "esp_err.h"

typedef enum{
	WIFI_STATE_INIT = 0,
	WIFI_STATE_CONNECTING,
	WIFI_STATE_SELECTING_AP,
	WIFI_STATE_CONNECTED,
	WIFI_STATE_ONLINE,
	WIFI_STATE_DISCONNECTED	
}wifi_state_t;

typedef struct
{
	const char *ssid;
	const char *password;
	
	uint32_t reconnect_base_delay_ms;
	uint32_t reconnect_max_delay_ms;
	uint32_t reconnect_jitter_ms;
	
	uint32_t max_reconnect_attempts;
	uint32_t same_ap_reconnect_limit;
	
	bool auto_connect;
}wifi_manager_config_t;

esp_err_t wifi_manager_init(const wifi_manager_config_t *config);
const char *wifi_manager_state_to_string(wifi_state_t state);

#define WIFI_MANAGER_BSSID_LEN		6U
typedef struct{
	bool initialized;
	
	wifi_state_t state;
	bool online;
	
	bool auto_reconnect_enabled;
	
	uint32_t reconnect_attempts;
	uint32_t same_ap_reconnect_attempts;
	
	bool last_disconnect_reason_valid;
	uint16_t last_disconnect_reason;
	
	bool selected_ap_valid;
	
	int8_t selected_ap_rssi;
	uint8_t selected_ap_channel;
	uint8_t selected_ap_bssid[WIFI_MANAGER_BSSID_LEN];
	
	uint32_t disconnect_count;
	uint32_t recovery_count;
	uint32_t scan_count;
	
}wifi_manager_status_t;

// Commands
bool wifi_manager_connect(void);	
bool wifi_manager_disconnect(void);
bool wifi_manager_scan(void);

// Status
wifi_state_t wifi_manager_get_state(void);
bool wifi_manager_is_online(void);

esp_err_t wifi_manager_get_status(wifi_manager_status_t *status);



#endif /* MAIN_WIFI_WIFI_MANAGER_H_ */
