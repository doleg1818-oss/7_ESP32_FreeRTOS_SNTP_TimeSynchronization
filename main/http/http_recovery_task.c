/*
 * http_recovery_task.c
 *
 *  Created on: Sep 19, 2026
 *      Author: Olegd
 */

#include "http_recovery_task.h"

#include "esp_err.h"
#include "freertos/projdefs.h"
#include "inttypes.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "http_manager.h"

#include "http_recovery_manager.h"

#define HTTP_RECOVERY_TASK_STACK_SIZE 		4096U
#define HTTP_RECOVERY_TASK_PRIORITY			4U

#define HTTP_RECOVERY_QUEUE_LENGTH			4U

 

static const char *TAG = "HTTP RECOVERY TASK";

static QueueHandle_t retry_queue = NULL;
static TaskHandle_t http_recovery_task_handle = NULL;


static void http_recovery_worker_task(void *arg)
{
	http_client_request_t retry_request;
	
	while(1)
	{
		// Отримати діні з черги для повторної відправки
		BaseType_t status = xQueueReceive(retry_queue, &retry_request, portMAX_DELAY);
		if(status != pdTRUE)
		{
			continue;
		}	
		
		uint32_t backoff = http_recovery_manager_get_backoff_ms(retry_request.retry_count);
		
		ESP_LOGI(TAG, ">>>>>>>>>>>>>>>>>> retry_count %u", retry_request.retry_count);
		ESP_LOGI(TAG, ">>>>>>>>>>>>>>>>>> backoff %" PRIu32, backoff);
			
		ESP_LOGI(TAG, " >>>>>>>>>>>>>>>>>> SATART Retry sheduled: request_id=%" PRIu32 ", retry=%u/%u",
			retry_request.request_id, 
			(unsigned)retry_request.retry_count,
			(unsigned)retry_request.max_retries);
			
		// Поки fixed delay
		vTaskDelay(pdMS_TO_TICKS(backoff));
		
		
		ESP_LOGI(TAG, " >>>>>>>>>>>>>>>>>> STOP Retry sheduled: request_id=%" PRIu32 ", retry=%u/%u",
			retry_request.request_id, 
			(unsigned)retry_request.retry_count,
			(unsigned)retry_request.max_retries);
		
		esp_err_t err = http_manager_submit(&retry_request, portMAX_DELAY);
		if(err != ESP_OK)
		{
			ESP_LOGE(TAG, "Retry submit failed: request_id=%" PRIu32 " ,error=%s", retry_request.request_id, esp_err_to_name(err));
		}
	}
}
esp_err_t http_recovery_task_init(void)
{
	if((retry_queue != NULL) || (http_recovery_task_handle != NULL)) 
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	retry_queue = xQueueCreate(HTTP_RECOVERY_QUEUE_LENGTH, sizeof(http_client_request_t));
	if(retry_queue == NULL)
	{
		ESP_LOGE(TAG, "Failue create retry_queue");
		return ESP_ERR_NO_MEM;
	}
	
	BaseType_t status = xTaskCreate(http_recovery_worker_task, "http_recovery_worker_task", HTTP_RECOVERY_TASK_STACK_SIZE, NULL, HTTP_RECOVERY_TASK_PRIORITY, &http_recovery_task_handle);
	if(status != pdPASS)
	{
		vQueueDelete(retry_queue);
		retry_queue = NULL;
		
		ESP_LOGE(TAG, "Failue to create http_recovery_worker_task");
		
		return ESP_ERR_NO_MEM;
	}
	
	ESP_LOGI(TAG, "HTTP recovery task initialized");
	
	return ESP_OK;
}

esp_err_t http_recovery_task_chedule(const http_client_request_t *request, TickType_t timeout_ticks)
{
	if(request == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	if(retry_queue == NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	BaseType_t stattus = xQueueSend(retry_queue, request, timeout_ticks);
	if(stattus != pdTRUE)
	{
		return ESP_ERR_TIMEOUT;
	}
	
	http_recovery_manager_notify_retry_sceduled();
	
	return ESP_OK;
}




































