/*
 * http_recovery_manager.c
 *
 *  Created on: Sep 18, 2026
 *      Author: Olegd
 */



/*
	----------------------------------- Recovery policy -----------------------------------
	  REASON							  ACTION					DESCRIBE
	200 ... 299								NONE				OK (Almost)
	400,401,403,404							DO_NOT_RETRY		ПРоблема на стороні клієнта 
	408										RETRY				Сервер не дочикався повного запиту (request timeout)
	429										RETRY				Забагато запитів
	500										RETRY				Внутріння помилка сервера
	502,503,504								RETRY				
	Server OFF/TLS connection timeout		RETRY
	Wrong sertificate						DO_NOT_RETRY
	Unknown TLS/configuration error			DO_NOT_RETRY
*/


#include "esp_err.h"
#include "esp_tls_errors.h"

#include "http/http_client_task.h"
#include "http_recovery_manager.h"

#include "esp_random.h"

#include "esp_http_client.h"

#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define HTTP_RECOVERY_BACKOFF_BASE_MS 		1000U
#define HTTP_RECOVERY_BACKOFF_MAX_MS 		8000U

#define HTTP_RECOVERY_JITTER_MAX_MS			500U


static http_recovery_stats_t recovery_stats; // Статистика відправлень
static SemaphoreHandle_t recovery_stats_mutex;


esp_err_t http_recovery_manager_init(void)
{
	if(recovery_stats_mutex != NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	recovery_stats_mutex = xSemaphoreCreateMutex();
	if(recovery_stats_mutex == NULL)
	{
		return ESP_ERR_NO_MEM;
	}	
	
	memset(&recovery_stats, 0, sizeof(recovery_stats));
	
	return ESP_OK;
}

void http_recovery_manager_notify_retry_sceduled(void)
{
	if(recovery_stats_mutex == NULL)
    {
        return;
    }
	
	if(xSemaphoreTake(recovery_stats_mutex, portMAX_DELAY) == pdTRUE)	
	{
		recovery_stats.retries_scheduled++;
		xSemaphoreGive(recovery_stats_mutex);	
	}
}

esp_err_t http_recovery_manager_get_stats(http_recovery_stats_t *ststs)
{
	if(ststs == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	if(recovery_stats_mutex == NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	if(xSemaphoreTake(recovery_stats_mutex, portMAX_DELAY) != pdTRUE)
	{
		return ESP_FAIL;
	}
	*ststs = recovery_stats;
	xSemaphoreGive(recovery_stats_mutex);
	
	return ESP_OK;
}

void http_recovery_manager_notify_recovered(void)
{
	if(recovery_stats_mutex == NULL)
	{
		return;
	}
	
	if(xSemaphoreTake(recovery_stats_mutex, portMAX_DELAY) == pdTRUE)
	{
		recovery_stats.recovered_requests++;
		xSemaphoreGive(recovery_stats_mutex);
	}
}


http_recovery_action_t http_recovery_action_manager_get_cation(const http_client_result_t *result)
{
	if(result == NULL) // Немає result для аналізу
	{
		return HTTP_RECOVERY_ACTION_DO_NOT_RETRY;
	}
	
	// 1. Спочатку перевірка transport/TLS помилки
 	if(result->err != ESP_OK)
 	{
		// Cesrteficate verification failed.
		// Повторним запитом це не виправити
		if(result->transport_error.tls_verify_flags != 0)
		{
			return HTTP_RECOVERY_ACTION_DO_NOT_RETRY;
		}
		
		// HTTP client дочекався timeout, але response data так і не з'явилися.
		if(result->err == ESP_ERR_HTTP_EAGAIN)
    	{
        	return HTTP_RECOVERY_ACTION_RETRY;
    	}
		
		// Сервер недоступний / connection timeout.
		// Сервер може зявитися пізніше, тому retry має сенс
		if(result->transport_error.tls_error == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT)
		{
			return HTTP_RECOVERY_ACTION_RETRY;
		}
		
		// Не вдалося підключитися до host
		if(result->transport_error.tls_error == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST)
		{
			return HTTP_RECOVERY_ACTION_RETRY;
		}
		
		// DNS Failure
		if(result->transport_error.tls_error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME)
		{
			return HTTP_RECOVERY_ACTION_RETRY;
		}		
		 
		// Інші transport error поки не класифіковані.
		// Fail closed: Не записувати автоматичний retry поки не розібрані ці невідомі помилки
		return HTTP_RECOVERY_ACTION_DO_NOT_RETRY;
	}
	
	// 2. Transport operation закінчився нормальною Аналіз HTTP status code:
	int status_code = result->response.status_code;
	
	// HTTP sucsess
	if(status_code >= 200 && status_code <=299)
	{
		return HTTP_RECOVERY_ACTION_NONE;
	}
	
	// Request timeout 
	if(status_code == 408)
	{
		return HTTP_RECOVERY_ACTION_RETRY;
	}
	
	// Too many requests
	if(status_code == 429)
	{
		return HTTP_RECOVERY_ACTION_RETRY;
	}
	
	
	// Типові transient server error
	switch(status_code)
	{
		case 500:
		case 502:
		case 503:
		case 504:
		{
			return HTTP_RECOVERY_ACTION_RETRY;
		}
		default:
		{
			break;	
		}
	}
	
	// 3xx, інші 4xx, невідомі статус коди. 
	// Просте повторення того самого request як правело нічого не поміняє
	return HTTP_RECOVERY_ACTION_DO_NOT_RETRY;
}
const char *http_recovery_manager_action_to_string(http_recovery_action_t action)
{
	switch(action)
	{
		case HTTP_RECOVERY_ACTION_NONE:
			return "NONE";
		case HTTP_RECOVERY_ACTION_RETRY:
			return "RETRY";
		case HTTP_RECOVERY_ACTION_DO_NOT_RETRY:
			return "DO_NOT_RETRY";
		default:
			return "UNKKNOWN";		
	}
}

esp_err_t http_recovery_manager_prepare_retry(const http_client_result_t *result, http_client_request_t *retry_request)
{
	if(result == NULL || retry_request == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	if(recovery_stats_mutex == NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	http_recovery_action_t action = http_recovery_action_manager_get_cation(result);
	
	// Цей result взагалі не повинен RETRY
	if(action != HTTP_RECOVERY_ACTION_RETRY)
	{
		return ESP_ERR_NOT_SUPPORTED;
	}
	
	// Усі retry вже використані
	if(result->request.retry_count >= result->request.max_retries)
	{
		if(xSemaphoreTake(recovery_stats_mutex, portMAX_DELAY) == pdTRUE)	
		{
			recovery_stats.retries_exhausted++;
			
			xSemaphoreGive(recovery_stats_mutex);	
		}
		
		return ESP_ERR_INVALID_STATE;
	}
	
	*retry_request = result->request;
	
	// це буде наступна retry attempt
	retry_request->retry_count++;
	
	if(xSemaphoreTake(recovery_stats_mutex, portMAX_DELAY) == pdTRUE)	
	{
		recovery_stats.retries_prepared++;
			
		xSemaphoreGive(recovery_stats_mutex);	
	}
	
	return ESP_OK;
}


const char *TAG = "TEST";

uint32_t http_recovery_manager_get_backoff_ms(uint8_t retry_count)  // 1,2,3,4,5,6,7...
{
	if(retry_count == 0U)  
	{
		return 0U;
	}
	
	uint32_t delay_ms = HTTP_RECOVERY_BACKOFF_BASE_MS;   			// delay_ms = 1000 
	
	for(uint8_t retry = 1U; retry < retry_count; retry++)
	{
		if(delay_ms >= HTTP_RECOVERY_BACKOFF_MAX_MS /2U)		// Захист від виходу за верхгю межу
		{
			delay_ms = HTTP_RECOVERY_BACKOFF_MAX_MS;
			break;
		} 	
		delay_ms = delay_ms*2U;  		 // 1,2,4,8
	}
	
	uint32_t jitter_ms = http_recovery_manager_get_jitter_ms(retry_count);
	
	return delay_ms + jitter_ms;
}

uint32_t http_recovery_manager_get_jitter_ms(uint8_t retry_count)
{
	return esp_random() % (HTTP_RECOVERY_JITTER_MAX_MS +1U);	 
}
 
 




























































