/*
 * wifi_manager_task.c
 *
 *  Created on: Aug 24, 2026
 *      Author: Olegd
 */

#include "wifi_manager_test.h"

#include "wifi_manager.h"

#include <stdbool.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"


static const char *TAG = "WIFI TEST >>> ";

void wifi_manager_test_print_status(void)
{
	wifi_manager_status_t status;
	esp_err_t err = wifi_manager_get_status(&status);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to get WiFi status");
		return;
	}
	ESP_LOGI(TAG, " =================== PRINT WIFI STATUS ===================");
	
	ESP_LOGI(TAG, "WiFi Status :%s", wifi_manager_state_to_string(status.state));
	ESP_LOGI(TAG, " online: %s", status.online ? "Yes" : "No");
	ESP_LOGI(TAG, " reconnect attempts: %" PRIu32, status.reconnect_attempts);
	
	ESP_LOGI(TAG, " disconnect count :%" PRIu32, status.disconnect_count);
	ESP_LOGI(TAG, " recovery count :%" PRIu32, status.recovery_count);
	ESP_LOGI(TAG, " scan count :%" PRIu32, status.scan_count);
	
	if(status.last_disconnect_reason_valid)
	{
		ESP_LOGI(TAG, "Last disconnect reason: %u", status.last_disconnect_reason);	
	}	
	else
	{
		ESP_LOGI(TAG, "Last disconnect reason: NONE");
	}
	
	if(status.selected_ap_valid)
	{
		ESP_LOGI(TAG, " AP RSSI %d dBm", status.selected_ap_rssi);
		ESP_LOGI(TAG, " AP Chanel %d", status.selected_ap_channel);
	}
	ESP_LOGI(TAG, " ===========================================================");
}

static void print_memory_info(void)
{
	const char *TAG = "MEMORY TEST";
	
	UBaseType_t stack_free_words = uxTaskGetStackHighWaterMark(NULL);
	size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
	size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
	
	ESP_LOGI(TAG,
		"Stask minimum free: %u words, "
		"HEAP free: %u bytes, "
		"Heap minimum free: %u bytes ",
		(unsigned)stack_free_words,
		(unsigned)free_heap,
		(unsigned)min_free_heap);
}

static bool rest_wait_for_state(wifi_state_t expected_state, uint32_t timeout_ms)
{
	const char *TAG = "TEST TASK";
	
	TickType_t start_tick = xTaskGetTickCount();
	TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
	
	while((xTaskGetTickCount() - start_tick) < timeout_ticks)
	{
		if(wifi_manager_get_state() == expected_state)
		{
			return true;
		}
		vTaskDelay(pdMS_TO_TICKS(50));
	}
	
	ESP_LOGE(TAG, "Timeout: expected state =%s, current state =%s",
		wifi_manager_state_to_string(expected_state),
		wifi_manager_state_to_string(wifi_manager_get_state()));
	
	return false;
}




//  Тестується цілісність даних (тобто правельну роботу мютекса)
static void status_read_task(void *parameters)
{
	(void)parameters;
	
	wifi_manager_status_t status;
	
	for(;;)
	{
		if(wifi_manager_get_status(&status) == ESP_OK)
		{
			if(status.online != (status.state == WIFI_STATE_ONLINE))
			{
				for(;;)
				{
					ESP_LOGE("STATUS TEST", "INCONSISTENT SNAPSHOT!");
				}
			}
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

static void test_task(void *parameters)
{
	(void)parameters;
	
	const char *TAG = "TEST TASK >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> ";
	
	vTaskDelay(pdMS_TO_TICKS(10000));
	
	ESP_LOGI(TAG, "======================= MATRIX TEST START =======================");
	
	// Wait for initial connection
	if(rest_wait_for_state(WIFI_STATE_ONLINE, 15000) == false)
	{
		ESP_LOGE(TAG, "Failed Initial connected");
		vTaskDelete(NULL);
		return;
	}
	
	///////////////////////////////////////////////////////////////////////////////////
	ESP_LOGI(TAG, "TEST 1: ONLINE STATE + CONNECT -> MUST be ignored ");
	wifi_manager_connect();
	vTaskDelay(pdMS_TO_TICKS(500));
	
	if(wifi_manager_get_state() == WIFI_STATE_ONLINE)
	{
		ESP_LOGI(TAG, "STATUS: TEST 1: Pass");
	}
	else 
	{
		ESP_LOGE(TAG, "STATUS: TEST 1: Fall");
	}
	
	///////////////////////////////////////////////////////////////////////////////////
	ESP_LOGI(TAG, "TEST 2: ONLINE STATE + SCAN -> MUST be ignored ");
	wifi_manager_scan();
	vTaskDelay(pdMS_TO_TICKS(200));
	ESP_LOGI(TAG, "TEST 2A. Made SCAN againe");
	wifi_manager_scan();
	// Delay for finish first scan
	vTaskDelay(pdMS_TO_TICKS(4000));
	if(wifi_manager_get_state() == WIFI_STATE_ONLINE)
	{
		ESP_LOGI(TAG, "STATUS: TEST 2: Pass ");
	}
	else
	{
		ESP_LOGE(TAG, "STATUS: TEST 2: Fail. State %s:", wifi_manager_state_to_string(wifi_manager_get_state()));
		
	}
	
	///////////////////////////////////////////////////////////////////////////////////
	ESP_LOGI(TAG, "TEST 3: ONLINE + DISCONECT -> allowed");
 	wifi_manager_disconnect();
	if(rest_wait_for_state(WIFI_STATE_DISCONNECTED, 3000))
	{
		ESP_LOGI(TAG, "STATUS: TEST 3: Pass");
	}
	else
	{
		ESP_LOGE(TAG, "STATUS: TEST 3: Fail");
		vTaskDelete(NULL);
		return;
	}
	
	///////////////////////////////////////////////////////////////////////////////////
	ESP_LOGI(TAG, "TEST 4: DISCONNECTED + DISCONECT ");
	wifi_manager_disconnect();
	vTaskDelay(pdMS_TO_TICKS(500));
	if(wifi_manager_get_state() == WIFI_STATE_DISCONNECTED)
	{
		ESP_LOGI(TAG, "STATUS: TEST 4: Pass");
	}
	else
	{
		ESP_LOGE(TAG, "STATUS: TEST 4: Fail");
	}
	
	///////////////////////////////////////////////////////////////////////////////////
	ESP_LOGI(TAG, "TEST 5: DISCONNECTED + SCAN -> allowed");
	wifi_manager_scan();
	vTaskDelay(pdMS_TO_TICKS(4000));
	if(wifi_manager_get_state() == WIFI_STATE_DISCONNECTED)
	{
		ESP_LOGI(TAG, "STATUS: TEST 5: Pass");
	}
	else
	{
		ESP_LOGE(TAG, "STATUS: TEST 5: Fail");
	}
	
	///////////////////////////////////////////////////////////////////////////////////
	ESP_LOGI(TAG, "TEST 6: DISCONNECTED + CONNECT -> allowed");
	wifi_manager_connect();
	if(rest_wait_for_state(WIFI_STATE_SELECTING_AP, 1000))
	{
		ESP_LOGI(TAG, "STATUS: TEST 6: Pass. WiFi is now in SELECTING_AP state");
	}
	else
	{
		ESP_LOGE(TAG, "STATUS: TEST 6: Fail");
	}
	
	ESP_LOGI(TAG, "TEST 6A: SELECTING_AP + CONNECT -> ignore");
	wifi_manager_connect();
	
	ESP_LOGI(TAG, "TEST 6B: SELECTING_AP + SCAN -> ignore");
	wifi_manager_scan();
	
	vTaskDelay(pdMS_TO_TICKS(200));
	
	
	///////////////////////////////////////////////////////////////////////////////////
	ESP_LOGI(TAG, "TEST 7: SELECTING_AP + DISCONNECT -> allowed  (Cansel connection)");
	wifi_manager_disconnect();
	if(rest_wait_for_state(WIFI_STATE_DISCONNECTED, 5000))
	{
		ESP_LOGI(TAG, "STATUS: TEST 7: Pass.");
	}
	else
	{
		ESP_LOGE(TAG, "STATUS: TEST 7: Fail");
	}
	
	
	///////////////////////////////////////////////////////////////////////////////////
	ESP_LOGI(TAG, "TEST 8: DISCONNECT + CONNECT -> allowed");
	wifi_manager_connect();
	if(rest_wait_for_state(WIFI_STATE_CONNECTED, 10000))
	{
		ESP_LOGI(TAG, "STATUS: TEST 8: Pass.");
	}
	else
	{
		ESP_LOGE(TAG, "STATUS: TEST 8: Fail");
	}
	
	ESP_LOGI(TAG, "======================= MATRIX TEST FINICH =======================");
	
	
	vTaskDelay(pdMS_TO_TICKS(5000));
	
	
	for(uint32_t i = 1; i <= 100; i++)
	{
		ESP_LOGI(TAG, "------------------- START test: %" PRIu32" --------------------", i);
		
		wifi_manager_disconnect();
		ESP_LOGI(TAG, ">>>>>>>>>>>>>>> Send DISCONNECT command");
		if(rest_wait_for_state(WIFI_STATE_DISCONNECTED, 2000))
		{
			ESP_LOGI(TAG, "STATUS: TEST CONNECT TO AP: Pass.");
		}
		else
		{
			ESP_LOGE(TAG, "STATUS: TEST DISCONNECT TO AP: Fail");
			vTaskDelete(NULL);
			return;
		}
	
		vTaskDelay(pdMS_TO_TICKS(500));
		
	
		ESP_LOGI(TAG, ">>>>>>>>>>>>>>> Send CONNECT command");
		wifi_manager_connect();
		if(rest_wait_for_state(WIFI_STATE_ONLINE, 45000))
		{
			ESP_LOGI(TAG, "STATUS: TEST CONNECT TO AP: Pass.");
		}
		else
		{
			ESP_LOGE(TAG, "STATUS: TEST CONNECT TO AP: Fail");
			vTaskDelete(NULL);
			return;
		}
		
		vTaskDelay(pdMS_TO_TICKS(500));
		
		print_memory_info();
		
		vTaskDelay(pdMS_TO_TICKS(2000));
		
		ESP_LOGI(TAG, "------------------- FINISH ---------------------");
	}

	ESP_LOGI(TAG, "------------------- DONE ---------------------");
	vTaskDelete(NULL);
}


esp_err_t wifi_manager_start_test_matrix(void)
{
	BaseType_t status = xTaskCreatePinnedToCore(test_task, "test_task", 3072, NULL, 3, NULL, 1);
	if(status != pdPASS)
	{
		return ESP_ERR_NO_MEM;
	}
	return ESP_OK;
}

esp_err_t wifi_manager_start_status_reader(void)
{
	BaseType_t status = xTaskCreatePinnedToCore(status_read_task, "status_read_task", 3072, NULL, 3, NULL, 0);
	if(status != pdPASS)
	{
		return ESP_ERR_NO_MEM;
	}
	return ESP_OK;
}


