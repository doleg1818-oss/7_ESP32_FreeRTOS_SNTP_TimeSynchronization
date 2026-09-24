


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

#include "secrets.h"
#include "wifi/wifi_manager.h"
#include "wifi/wifi_manager_test.h"

#include "time/sntp_time_manager.h"

#include "esp_netif_sntp.h"
#include "time.h"

#include "sys/time.h"
#include "esp_sntp.h"


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

  
  	if (wait_for_online(30000) == false)
  	{
    	ESP_LOGE(TAG, "WiFi did not become ONLINE !");
    	return;
  	}
  	ESP_LOGI(TAG, "WiFi Online");

  	wifi_manager_test_print_status();
  
   //////////////////////////////////////////////////////
   
   sntp_time_manager_config_t time_config = {
	   .server = "pool.ntp.org",
	   .timezone = "EET-2EEST,M3.5.0/3,M10.5.0/4",
	   .sync_interval_ms = 20000U
   };
   err = sntp_time_manager_init(&time_config);
   if(err != ESP_OK)
   {
	   ESP_LOGE(TAG, "Failed to init Time Manager");
	   return;
   }
   
   err = sntp_time_manager_wait_for_sync(10000U);
   if(err != ESP_OK)
   {
	   ESP_LOGE(TAG, "Failed initial time syncronization");
	   return;
   }
   
   uint32_t count = 0;
   
   while(1)
   {
	   time_t timestamp;
	   
	   if(sntp_time_manager_get_timestamp(&timestamp) == ESP_OK)
	   {
		   ESP_LOGI(TAG, "Timestamp: %lld", (long long)timestamp);
	   }
	   
	   struct tm local_time;
	   if(sntp_time_manager_get_localtime(&local_time) == ESP_OK)
	   {
			char buffer[64] = {0,};
		   	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
		 	ESP_LOGI(TAG, "Local time: %s", buffer);
		 	
		 	uint32_t sinc_age = 0;
		 	if(sntp_time_manager_get_sync_age(&sinc_age) == ESP_OK)
		 	{
				ESP_LOGI(TAG, "Last SNTP sinc age: %" PRIu32, sinc_age);	 
			}
	   }
	   
	   count++;
	   if(count >= 20)
	   {
		   sntp_time_satatus_t status;
		   if(sntp_time_manager_get_status(&status) == ESP_OK)
		   {
			   ESP_LOGI(TAG, "Time state=%s", sntp_time_manager_state_to_string(status.state));
			   ESP_LOGI(TAG, "Sinc count=%" PRIu32, status.sync_count);
			   ESP_LOGI(TAG, "Last sync=%lld", (long long)status.last_sinc_timestamp);
		   }
		   count = 0;
	   }
	   vTaskDelay(pdMS_TO_TICKS(1000));
   }
   
   
   

  // Wifi tests
  // wifi_manager_start_test_matrix();
  // wifi_manager_start_status_reader();
}
