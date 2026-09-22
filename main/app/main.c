

#include "inttypes.h"
#include "stdlib.h"
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "string.h"

#include "../secrets.h"
#include "../wifi/wifi_manager.h"
#include "../wifi/wifi_manager_test.h"

#include "esp_netif_sntp.h"

#define SNTP_SERVER 				"pool.ntp.org"
#define SNTP_SYNC_TIMEOUT_MS		10000U 



static esp_err_t sntp_sync_time(void)
{
	static const char *TAG = "SNTP";
	
	ESP_LOGI(TAG, "Initialization SNTP");
	
	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
	esp_err_t err = esp_netif_sntp_init(&config);
		
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to confirure SNTP, err = %s", esp_err_to_name(err));
		return err;
	}
	
	ESP_LOGI(TAG, "Waiting for sincronization...");
	
	err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_SYNC_TIMEOUT_MS));
	
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "SNTP Failed, err :%s", esp_err_to_name(err));
		esp_netif_sntp_deinit();
		return err;
	}
	
	ESP_LOGI(TAG, "SNTP sincronization complited");
	
	return ESP_OK;
}

static void print_system_time_utc(void)
{
	static const char *TAG = "SNTP";
	
	time_t now;
	struct tm timeinfo;
	
	time(&now);
	gmtime_r(&now, &timeinfo);
	
	char time_buffer[64] = {0,};
	
	strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
	
	ESP_LOGI(TAG, "Unix timestamp: %lld", (long long)now);
	ESP_LOGI(TAG, "UTC time: %s", time_buffer);
}


static bool wait_for_online(uint32_t timeout_ms)
{
	TickType_t start_tick = xTaskGetTickCount();
  	TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

  	while ((xTaskGetTickCount() - start_tick) < timeout_ticks) 
  	{
    	if (wifi_manager_is_online()) 
    	{
      		return true;
    	}
		vTaskDelay(pdMS_TO_TICKS(100));
  	}
  return false;
}

void app_main(void) 
{
	static const char *TAG = "WIFI STA";
	
  	ESP_LOGI(TAG, "Aplication started");

  	// Init NVS
 	esp_err_t err = nvs_flash_init();
  	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) 
  	{
    	ESP_ERROR_CHECK(nvs_flash_erase());
    	ESP_ERROR_CHECK(nvs_flash_init());
  	}

  // Initialize WiFi
  	wifi_manager_config_t config = {
		.ssid = WIFI_SSID,
  		.password = WIFI_PASSWORD,

        .reconnect_base_delay_ms = 2000,
        .reconnect_max_delay_ms = 30000,
        .reconnect_jitter_ms = 500,

        .max_reconnect_attempts = 15,
        .same_ap_reconnect_limit = 2,

        .auto_connect = true};

  	err = wifi_manager_init(&config);
  	if (err != ESP_OK)
  	{
    	ESP_LOGE(TAG, "FAILED INIT WiFi !!! ");
   		return;
  	}
  	ESP_LOGI(TAG, "WiFi STA initialized complited");

  //////////////////////////////////////////////////////
  	if (wait_for_online(30000) == false)
  	{
    	ESP_LOGE(TAG, "WiFi did not become ONLINE !");
    	return;
  	}
  	ESP_LOGI(TAG, "WiFi Online");

  	wifi_manager_test_print_status();
  
   
   err = sntp_sync_time();
   if(err != ESP_OK)
   {
	   ESP_LOGI(TAG, "Failed to synchronize system time");
	   return;
   }
   
   	print_system_time_utc();
	esp_netif_sntp_deinit();


  // Wifi tests
  // wifi_manager_start_test_matrix();
  // wifi_manager_start_status_reader();
}
