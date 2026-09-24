/*
 * http_client_task.c
 *
 *  Created on: Sep 14, 2026
 *      Author: Olegd
 */
 
 
#include "http_client_task.h"
#include "esp_err.h"
#include "inttypes.h"
#include "string.h"

static const char *TAG = "HTTP TASK";

#define HTTP_CLIENT_TASK_STACK_SIZE 		8192U
#define HTTP_CLIENT_TASK_PRIORITY 			5U

#define HTTP_CLIENT_RESULT_QUEUE_LENGTH 	4U
#define HTTP_CLIENT_REQUEST_QUEUE_LENGTH    4U

static QueueHandle_t request_queue = NULL;
static QueueHandle_t result_queue = NULL;

static TaskHandle_t http_task_handle = NULL;

void http_client_worker_task(void *arg);


esp_err_t http_client_task_submit(const http_client_request_t *request, TickType_t timeout_ticks)
{
	if(request == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	if(request_queue == NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	// Перевірка місця в черзі
	UBaseType_t waiting_before = uxQueueMessagesWaiting(request_queue);
	UBaseType_t spase_before = uxQueueSpacesAvailable(request_queue);
	ESP_LOGI(TAG, "Submit request id=%" PRIu32 ", queue waiting=%u, free=%u", request->request_id,  (unsigned)waiting_before, (unsigned)spase_before);
	
	
	BaseType_t status = xQueueSend(request_queue, request, timeout_ticks);  		// Надіслати чергу до http_client_worker_task 
	if(status != pdTRUE)
	{
		return ESP_ERR_TIMEOUT;
	}
	return ESP_OK;
}
 
esp_err_t http_client_task_receive_result(http_client_result_t *result, TickType_t timeout_ticks)
{
	if(result  == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	if(result_queue == NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	BaseType_t status = xQueueReceive(result_queue, result, timeout_ticks);
	if(status != pdTRUE)
	{
		return ESP_ERR_TIMEOUT;
	}
	return ESP_OK;
}

esp_err_t http_client_task_init(void)
{
	if(request_queue != NULL || result_queue != NULL || http_task_handle != NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}	

	request_queue = xQueueCreate(HTTP_CLIENT_REQUEST_QUEUE_LENGTH, sizeof(http_client_request_t));
	if(request_queue == NULL)
	{
		ESP_LOGE(TAG, "Failed create request_queue");
		return ESP_ERR_NO_MEM;
	} 
	
	result_queue = xQueueCreate(HTTP_CLIENT_RESULT_QUEUE_LENGTH, sizeof(http_client_result_t));
	if(result_queue == NULL)
	{
		ESP_LOGE(TAG, "Failed create result_queue");
		vQueueDelete(request_queue);
		request_queue = NULL;
		return ESP_ERR_NO_MEM;
	} 
	
	BaseType_t status = xTaskCreate(http_client_worker_task, "http_client_worker_task", HTTP_CLIENT_TASK_STACK_SIZE, NULL, HTTP_CLIENT_TASK_PRIORITY, &http_task_handle);
	if(status != pdPASS)
	{
		ESP_LOGE(TAG, "Failed create http_client_worker_task");
		
		vQueueDelete(request_queue);
		request_queue = NULL;
		
		vQueueDelete(result_queue);
		result_queue = NULL;
		
		return ESP_ERR_NO_MEM;
	}
	
	ESP_LOGI(TAG, "HTTP client rask initialized");
	return ESP_OK;
}

// Чекає на на дані які відправити до сервера.
void http_client_worker_task(void *arg)
{
	http_client_request_t request;
	
	while(1)
	{
		BaseType_t status = xQueueReceive(request_queue, &request, portMAX_DELAY);
		if(status != pdTRUE)
		{
			continue;
		}
	
		ESP_LOGI(TAG, "Processing request id=%" PRIu32, request.request_id);
		
		http_client_result_t result ={0};
		result.request = request;	// Copy structure
		
		switch(request.type)
		{
			case HTTP_CLIENT_REQUEST_HTTP_GET:
			{
				result.err = http_client_get(request.url, &result.response, &result.transport_error);
				break;
			}
			case HTTP_CLIENT_REQUEST_HTTP_POST_JSON:
			{
				result.err = http_client_post_json(request.url, request.json, &result.response, &result.transport_error);
				break;
			}
			case HTTP_CLIENT_REQUEST_HTTPS_POST_JSON:
			{
				result.err = http_client_post_json_https(request.url, request.json, &result.response, &result.transport_error);
				break;
			}
			default:
			{
				ESP_LOGE(TAG, "Unknown request type");
				result.err = ESP_ERR_INVALID_ARG;
				break;
			}
		}
		// Відсилає відповідь сервера до клієнта 
		status = xQueueSend(result_queue, &result, pdMS_TO_TICKS(100));
		if(status != pdTRUE)
		{
			ESP_LOGE(TAG, "result_queue is full, request id=%" PRIu32, result.request.request_id);
		}
	}
}






















