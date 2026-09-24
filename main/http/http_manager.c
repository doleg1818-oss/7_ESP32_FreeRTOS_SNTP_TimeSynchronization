/*
 * http_manager.c
 *
 *  Created on: Sep 16, 2026
 *      Author: Olegd
 */

#include "http_manager.h"

#include "esp_err.h"
#include "stdbool.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

static const char *TAG = "HTTP MANAGER";

static bool manager_initialized = false;
static SemaphoreHandle_t stats_mutex = NULL;
static http_manager_stats_t manager_stats = {0};


esp_err_t http_manager_init(void)
{
	if(manager_initialized)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	stats_mutex = xSemaphoreCreateMutex();
	if(stats_mutex == NULL)
	{
		ESP_LOGE(TAG, "Failed to create statictics mutex");
		return ESP_ERR_NO_MEM;
	}
	
	memset(&manager_stats, 0, sizeof(manager_stats));
	
	manager_stats.last_http_status = -1;
	manager_stats.last_error = ESP_OK;
	
	manager_initialized = true;
	
	ESP_LOGI(TAG, "HTTP manager initialized");
	
	return ESP_OK;
}

http_manager_result_type_t http_manager_classify_result(const http_client_result_t *result)
{
	if(result == NULL)
	{
		return HTTP_MANAGER_RESULT_UNKNOWN_ERROR;
	}
	
	// Якщо http не завершився нормаьно: TCP/TLS/connect/rimeout 
	if(result->err != ESP_OK)
	{
		return HTTP_MANAGER_RESULT_TRANSPORT_ERROR;
	}	
	
	int status_code = result->response.status_code;
	
	if(status_code >= 200 && status_code < 300)
	{
		return HTTP_MANAGER_RESULT_SUCCSESS;
	}
	if(status_code >= 300 && status_code < 400)
	{
		return HTTP_MANAGER_RESULT_REDIRECT;
	}
	if(status_code >= 400 && status_code < 500)
	{
		return HTTP_MANAGER_RESULT_HTTP_CLIENT_ERROR;
	}
	if(status_code >= 500 && status_code < 600)
	{
		return HTTP_MANAGER_RESULT_HTTP_SERVER_ERROR;
	}
	
	return HTTP_MANAGER_RESULT_UNKNOWN_ERROR;
}

esp_err_t http_manager_process_result(const http_client_result_t *result, http_manager_result_type_t *result_type)
{
	if(result == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	if(!manager_initialized)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	http_manager_result_type_t type = http_manager_classify_result(result);
	
	if(xSemaphoreTake(stats_mutex, portMAX_DELAY) != pdTRUE)
	{
		return ESP_FAIL;
	}
	
	manager_stats.total++;
	manager_stats.last_request_id = result->request.request_id;
	manager_stats.last_error = result->err;
	manager_stats.last_http_status = result->response.status_code;
	
	switch(type)
	{
		case HTTP_MANAGER_RESULT_SUCCSESS:
		{
			manager_stats.successful++;
			break;
		}
		
		case HTTP_MANAGER_RESULT_TRANSPORT_ERROR:
		{
			manager_stats.transport_errors++;
			break;
		}
		
		case HTTP_MANAGER_RESULT_REDIRECT:
		{
			manager_stats.redirects++;
			break;
		}
		
		case HTTP_MANAGER_RESULT_HTTP_CLIENT_ERROR:
		{
			manager_stats.http_client_errors++;	
			break;
		}
		
		case HTTP_MANAGER_RESULT_HTTP_SERVER_ERROR:
		{
			manager_stats.http_server_errors++;
			break;
		}
		
		default:
		{
			manager_stats.unknown_errors++;
			break;
		}
	}
	
	xSemaphoreGive(stats_mutex);
	
	// Копіювання
	if(result_type != NULL)
	{
		*result_type = type;
	}
	
	return ESP_OK;
}

esp_err_t http_manager_get_stats(http_manager_stats_t *status)
{
	if(status == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	if(!manager_initialized)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	if(xSemaphoreTake(stats_mutex, portMAX_DELAY) != pdTRUE)
	{
		return ESP_FAIL;
	}
	
	// Скопіювати manager_stats в status
	*status = manager_stats;
	
	xSemaphoreGive(stats_mutex);
	return ESP_OK;
}

const char * http_manager_result_type_to_status(http_manager_result_type_t type)
{
	switch(type)
	{
		case HTTP_MANAGER_RESULT_SUCCSESS:
			return "SUCCESS";
			
		case HTTP_MANAGER_RESULT_TRANSPORT_ERROR:
			return "TRANSPORT ERROR";
			
		case HTTP_MANAGER_RESULT_REDIRECT:
			return "REDIRECT";
			
		case HTTP_MANAGER_RESULT_HTTP_CLIENT_ERROR:
			return "CLIENT_ERROR";
			
		case HTTP_MANAGER_RESULT_HTTP_SERVER_ERROR:
			return "SERVER ERROR";
		
		case HTTP_MANAGER_RESULT_UNKNOWN_ERROR:
		default:
			return "UNKNOWN ERROR";
	}
}

esp_err_t http_manager_submit(const http_client_request_t *request, TickType_t timeout_ticks)
{
	if(request == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	if(!manager_initialized)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	// Заразувати сам факт спроби submit
	if(xSemaphoreTake(stats_mutex, portMAX_DELAY) != pdTRUE)
	{
		return ESP_FAIL;
	}
	
	manager_stats.submit_attempts++;
	
	xSemaphoreGive(stats_mutex);
	
	// Реальний submeet 
	esp_err_t err = http_client_task_submit(request, timeout_ticks);
	
	// Запис результату submeet
	if(xSemaphoreTake(stats_mutex, portMAX_DELAY) != pdTRUE)
	{
		return ESP_FAIL;
	}
	 
	if(err == ESP_OK)	// Якщо реально передався request 
	{
		manager_stats.request_submitted++;
	}
	else if(err == ESP_ERR_TIMEOUT)
	{
		manager_stats.request_dropped++;
	}
	else 
	{
		manager_stats.submit_errors++;
	}
	
	manager_stats.last_error = err;
	
	xSemaphoreGive(stats_mutex);
	
	return err;
}










