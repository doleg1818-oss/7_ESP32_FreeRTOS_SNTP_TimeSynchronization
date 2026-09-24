/*
 * http_result_task.c
 *
 *  Created on: Sep 15, 2026
 *      Author: Olegd
 */

#include "inttypes.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../http/http_client_task.h"
#include "../http/http_manager.h"

#include "../http/http_recovery_manager.h"
#include "../http/http_recovery_task.h"


#define HTTP_RESULT_TASK_STACK_SIZE			4098U
#define HTTP_RESULT_TASK_PRIORITY			4U

static TaskHandle_t http_result_worker_task_handle = NULL;

static const char *TAG = "HTTP RESULT TASK";



static void http_result_worker_task(void *arg)
{
	static http_client_result_t result = {0};
	  
	while(1)
	{
		esp_err_t err = http_client_task_receive_result(&result, portMAX_DELAY);
		if(err != ESP_OK)
		{
			ESP_LOGE(TAG, "Failed to receive resalt");
			continue;
		}
		
		// Рахування статистики помилок.
		http_manager_result_type_t result_type;
		err = http_manager_process_result(&result, &result_type);
		if(err != ESP_OK)
		{
			ESP_LOGE(TAG, "HTTP Manager failed to process results");
			continue;
		}
		
		// Select and print recovery action
		http_recovery_action_t recovery_action = http_recovery_action_manager_get_cation(&result);
		ESP_LOGI(TAG, "RECOVERY ACTION:  %s", http_recovery_manager_action_to_string(recovery_action));  
		
		if((recovery_action == HTTP_RECOVERY_ACTION_NONE) && (result.request.retry_count > 0U))
		{
			http_recovery_manager_notify_recovered();
		}
		
		// 
		if(recovery_action == HTTP_RECOVERY_ACTION_RETRY)
		{
			http_client_request_t retry_request;
			
			esp_err_t retry_err = http_recovery_manager_prepare_retry(&result, &retry_request);
			if(retry_err == ESP_OK)		// Дозволено RETRY
			{
				ESP_LOGI(TAG, "Retry prepared: request_id %" PRIu32 ", retry=%u/%u", 
					retry_request.request_id,
					(unsigned)retry_request.retry_count,
					(unsigned)retry_request.max_retries);
					
				esp_err_t shedule_err = http_recovery_task_chedule(&retry_request, pdMS_TO_TICKS(100));
				if(shedule_err != ESP_OK)
				{
					ESP_LOGE(TAG, "Faule to schedule retru =%s", esp_err_to_name(shedule_err));
				}	
			}
			else
			{
				ESP_LOGI(TAG, "Retry cannot be prepared: %s ", esp_err_to_name(retry_err));
			}
		}
		
	
		ESP_LOGI(TAG, "Request id = %" PRIu32, result.request.request_id);
		ESP_LOGI(TAG, "Request type =%s",  http_manager_result_type_to_status(result_type));
		
		switch(result_type)
		{
			case HTTP_MANAGER_RESULT_SUCCSESS:
			{
				ESP_LOGI(TAG, "HTTP status=%d", result.response.status_code);
				ESP_LOGI(TAG, "Response body=%s", result.response.body);
				break;
			}
			
			case HTTP_MANAGER_RESULT_TRANSPORT_ERROR:
			{
				ESP_LOGE(TAG, "TRANSPORT_ERROR %s", esp_err_to_name(result.err));
				
				ESP_LOGE(TAG, "Socet error=%d", result.transport_error.socket_errno);
				ESP_LOGE(TAG, "ESP-TLS error =0x%x", (unsigned)result.transport_error.tls_error);
				ESP_LOGE(TAG, "mbedTLS error=0x%x", (unsigned)result.transport_error.tls_error_code);
				ESP_LOGE(TAG, "TLS verify flags=0x%x", (unsigned)result.transport_error.tls_verify_flags);
				
				break;
			}
			
			case HTTP_MANAGER_RESULT_REDIRECT:
			{
				ESP_LOGE(TAG, "HTTP REDIRECT status = %d", result.response.status_code);
				break;
			}
			
			case HTTP_MANAGER_RESULT_HTTP_CLIENT_ERROR:
			{
				ESP_LOGE(TAG, "HTTP CLIENT ERROR status %d", result.response.status_code);
				break;
			}
			
			case HTTP_MANAGER_RESULT_HTTP_SERVER_ERROR:
			{
				ESP_LOGE(TAG, "HTTP SERVER ERROR status %d", result.response.status_code);
				break;
			}
			
			case HTTP_MANAGER_RESULT_UNKNOWN_ERROR:
			{
				ESP_LOGE(TAG, "UNKNOWN HTTP result");
				break;
			}
		}
		
		
		http_manager_stats_t stats;
		
		if(http_manager_get_stats(&stats) == ESP_OK)
		{
			ESP_LOGI(TAG, 
				"Stats     : attempts=%" PRIu32
				", submitted=%" PRIu32
				", dropped=%" PRIu32
				", completed=%" PRIu32
				", succsessful=%" PRIu32
				
				
				", transport=%" PRIu32
				", 4xx=%" PRIu32
				", 5xx=%" PRIu32,
				
				
				stats.submit_attempts,
				stats.request_submitted,
				stats.request_dropped,
				
				stats.total,
				
				stats.successful,
				
				stats.transport_errors,
				stats.http_client_errors,
				stats.http_server_errors);
		}
		
		http_recovery_stats_t recovery_stats;

		if(http_recovery_manager_get_stats(&recovery_stats) == ESP_OK)
		{
    		ESP_LOGI(
        		TAG,
		        "Recovery stats: "
		        "prepared=%" PRIu32
		        ", scheduled=%" PRIu32
		        ", exhausted=%" PRIu32
		        ", recovered=%" PRIu32,
		        recovery_stats.retries_prepared,
		        recovery_stats.retries_scheduled,
		        recovery_stats.retries_exhausted,
		        recovery_stats.recovered_requests);
		}
	}
}

esp_err_t http_result_task_start(void)
{
	if(http_result_worker_task_handle != NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	BaseType_t status = xTaskCreate(http_result_worker_task, "http_result_worker_task", HTTP_RESULT_TASK_STACK_SIZE, NULL, HTTP_RESULT_TASK_PRIORITY, &http_result_worker_task_handle);
	if(status != pdPASS)
	{
		http_result_worker_task_handle = NULL;
		ESP_LOGE(TAG, "Failed to create http_result_worker_task");
		return ESP_ERR_NO_MEM;
	}
	ESP_LOGI(TAG, "HTTP result task started");
	
	return ESP_OK;
}
