/*
 * telemetry_task.c
 *
 *  Created on: Sep 15, 2026
 *      Author: Olegd
 */


#include "telemetry_task.h"

#include "esp_err.h"
#include "inttypes.h"
#include "string.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../data/device_data_serializer.h"
#include "../http/http_client_task.h"
#include "../http/http_manager.h"
#include "../time/sntp_time_manager.h"

#define TELEMETRY_TASK_STACK_SIZE		4096U
#define TELEMETRY_TASK_PRIOITY			4U

#define TELEMETRY_PERIOD_MS				30000U   // 1000U

#define TELEMETRY_URL "https://192.168.0.240:8443/api/data" 
//#define TELEMETRY_URL "https://192.168.0.240:8443/api/data-loss-test"   test case: "when server lost/forgot send ststus:200 after receive HTTP request from ESP32 "


static TaskHandle_t telemetry_task_handle = NULL;

static const char *TAG = "TELEMERTY TASK";

static void telemetry_worker_task(void *arg)
{
	uint32_t request_id = 1;
	
	while(1)
	{
		time_t timestamp;
		esp_err_t err = sntp_time_manager_get_timestamp(&timestamp);
		if(err != ESP_OK)
		{
			ESP_LOGE(TAG, "System time is not syncronized. Skip telemetry");
			vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
			continue;
		}
		
		uint32_t current_meaasge_id = request_id++;
		
		// Emitation device data
	  	device_data_t device_data = {
		  .message_id = current_meaasge_id,
		  .device_id = 17,
		  .temperature = 23,
		  .humidity = 60,
		  .battery_voltage = 3.91f,
		  .timestamp = (uint64_t)timestamp, 
		  .alarm = false};
	
		char *json = NULL;
	 	err = device_data_serialize_json(&device_data, &json);
	  	if(err != ESP_OK)
	  	{
		  	ESP_LOGE(TAG, "Failed to serialize data");
		  	vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
		  	continue;
	  	}
	  	ESP_LOGI(TAG, "serialize JSON: %s", json);
	  		
		// Створення екземплярів 
	  	http_client_request_t request = {0};
	  	request.request_id = current_meaasge_id;
	  	request.type = HTTP_CLIENT_REQUEST_HTTPS_POST_JSON;
	  	
	  	request.retry_count = 0;
	  	request.max_retries = 3;
	  	
	  	strlcpy(request.url, TELEMETRY_URL, sizeof(request.url));
	  	// Перевірка на довжину 
	  	if(strlen(json) >= sizeof(request.json))
	  	{
		 	ESP_LOGE(TAG, "JSON is to long");
		  	device_data_free_json(json);
		 	vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
		  	continue;
	  	}
		strlcpy(request.json, json, sizeof(request.json));
		
		// Засадмітити передачу до сервера
	  	//err = http_client_task_submit(&request, pdMS_TO_TICKS(100));
	  	err = http_manager_submit(&request, pdMS_TO_TICKS(100));
	  	
	  	device_data_free_json(json);
		json = NULL;
		
	  	if(err != ESP_OK)
	  	{
		 	ESP_LOGE(TAG, "Failue to submit request id=%" PRIu32, request.request_id);
	 	}
	 	else
	 	{
			ESP_LOGI(TAG, "Submited request id =%" PRIu32, request.request_id);		
		}
		
		vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
	}
}

esp_err_t telemetry_task_start(void)
{
	if(telemetry_task_handle != NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	BaseType_t status = xTaskCreate(telemetry_worker_task, "telemetry_worker_task",TELEMETRY_TASK_STACK_SIZE, NULL, TELEMETRY_TASK_PRIOITY, &telemetry_task_handle);
	if(status != pdPASS)
	{
		telemetry_task_handle = NULL;
		ESP_LOGE(TAG, "Failed to create telemetry_worker_task");
		return ESP_ERR_NO_MEM;
	}
	ESP_LOGI(TAG, "Telemetry task started");
	return ESP_OK;
}




















