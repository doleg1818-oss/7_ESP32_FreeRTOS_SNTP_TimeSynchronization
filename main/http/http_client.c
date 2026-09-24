/*
 * http_get_client.c
 *
 *  Created on: Aug 25, 2026
 *      Author: Olegd
 */


#include "http_client.h"

#include <inttypes.h>
#include <sys/errno.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <string.h>
#include "esp_tls.h"

extern const uint8_t server_cert_pem_start[]  asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[]  asm("_binary_server_cert_pem_end");

static const char *TAG = "HTTP CLIENT";

typedef struct{
	http_response_t *response;
	http_transport_error_t *transport_error;
}http_event_context_t;



static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
	if(event == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}

	http_event_context_t * context = (http_event_context_t*)event->user_data;
	
	if((context == NULL) || (context->response == NULL))
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	http_response_t *response = context->response;

	
	//ESP_LOGI(TAG, "http_event_handler response address =%p <<<", event->user_data); // Print address of structure

	switch(event->event_id)
	{
		case HTTP_EVENT_ERROR:
		{
			ESP_LOGI(TAG, "HTTP_EVENT_ERROR");				// any error
			break;
		}
		case HTTP_EVENT_ON_CONNECTED:   					// Connected to server 
		{
			ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
			break;
		}
		case HTTP_EVENT_HEADERS_SENT:
		{
			ESP_LOGI(TAG, "HTTP_EVENT_HEADERS_SENT");
			break;
		}
		case HTTP_EVENT_ON_HEADER :
		{
			ESP_LOGI(TAG, "Header: %s: %s", event->header_key, event->header_value);
			
			if((event->header_key != NULL) && (event->header_value != NULL))
			{
				if(strcasecmp(event->header_key, "Content-Type") == 0)
				{
					strlcpy(response->content_type, event->header_value, sizeof(response->content_type));
				}
			}
			
			break;
		}
		case HTTP_EVENT_ON_DATA:
		{
			if(response == NULL)
			{
				return ESP_ERR_INVALID_RESPONSE;
			}
			
			size_t free_space = sizeof(response->body) - 1U - response->body_length; // скільки вільного місця в буфері
			size_t copy_length = (size_t)event->data_len;
			if(copy_length > free_space)
			{
				copy_length = free_space;
				response->body_truncated = true;
			}
			if(copy_length > 0)
			{
				// Записати в загальни буфер дані прийняті від сервера
				memcpy(&response->body[response->body_length], event->data, copy_length);
				response->body_length += copy_length;				
				response->body[response->body_length] = '\0';
			}
			
			ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA Received=%d, total=%u", event->data_len, (unsigned)response->body_length);
			break;
		}
		
		case HTTP_EVENT_DISCONNECTED:
		{
			ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
			if((context != NULL) && (context->transport_error != NULL) && (event->data != NULL))
			{
				context->transport_error->tls_error = esp_tls_get_and_clear_last_error(
					(esp_tls_error_handle_t)event->data, &context->transport_error->tls_error_code, &context->transport_error->tls_verify_flags);
			}
			
			break;
		}
		
		
		default:
		{
			break;
		}
	}
	return ESP_OK;
}

esp_err_t http_client_get(const char *url, http_response_t *response, http_transport_error_t *transport_error)
{
	if((url == NULL) || (response == NULL))
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	http_event_context_t event_context = 
	{
		.response = response,
		.transport_error = transport_error
	};
	
	esp_http_client_config_t config = {
		.url = url,
		.method = HTTP_METHOD_GET,
		.event_handler = http_event_handler,
		.user_data = &event_context,
		.timeout_ms = 5000
	};
	
	memset(response, 0, sizeof(*response));
	response->content_length = -1;
		
	ESP_LOGI(TAG, "http_get_client_perform response address =%p <<<", (void *)response); // Print address of structure
	
	esp_http_client_handle_t client = esp_http_client_init(&config);
	if(client == NULL)
	{
		ESP_LOGE(TAG, "Failed to initialized HTTP client");
		return ESP_ERR_NO_MEM;
	}
	
	ESP_LOGI(TAG, "Sending GET request to url: %s", url);
	
	esp_err_t err = esp_http_client_perform(client);
	if(err == ESP_OK)
	{
		response->status_code = esp_http_client_get_status_code(client);
		response->content_length = esp_http_client_get_content_length(client); // Скільки байт сервер відправив до клієнта (Корисне навантаження)
		
		ESP_LOGI(TAG, "HTTP Status: %d, content length = %" PRId64, response->status_code, response->content_length);
	}
	else
	{
		ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
	}
	esp_http_client_cleanup(client);
	
	return err;
}


esp_err_t http_client_post_json(const char *url, const char *json, http_response_t *response, http_transport_error_t *transport_error)
{
	if((url == NULL) || (json == NULL) || (response == NULL))
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	memset(response, 0, sizeof(*response));
	response->content_length = -1;
	
	http_event_context_t event_context = 
	{
		.response = response,
		.transport_error = transport_error
	};
	
	esp_http_client_config_t config = {
		.url = url,
		.method = HTTP_METHOD_POST,
		.event_handler = http_event_handler,
		.user_data = &event_context,
		.timeout_ms = 5000
	};
	
	esp_http_client_handle_t client = esp_http_client_init(&config);

	if(client == NULL)
	{
		ESP_LOGE(TAG, "Failed ti init HTTP client");
		return ESP_ERR_NO_MEM;
	}	
	
	// Повідомити серверу про формат request body
	esp_err_t err = esp_http_client_set_header(client, "Content-Type", "application/json");
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to send Content-type");
		esp_http_client_cleanup(client);
		return err;
	}
	
	// Встановити JSON як request body
	err = esp_http_client_set_post_field(client, json, strlen(json));
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to set POST body");
		esp_http_client_cleanup(client);
		return err;
	}
	
	ESP_LOGI(TAG, "POST URL: %s, ",url);
	ESP_LOGI(TAG, "POST JSON: %s, ",json);
	
	err = esp_http_client_perform(client);
	
	if(err == ESP_OK)
	{
		response->status_code = esp_http_client_get_status_code(client);
		response->content_length = esp_http_client_get_content_length(client);
		ESP_LOGI(TAG, "HTTP status: %d, current_length: %" PRId64, response->status_code, response->content_length);
	}
	else
	{
		ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
	}
	
	esp_http_client_cleanup(client);
	
	return err;
}

esp_err_t http_client_post_json_https(const char *url, const char *json, http_response_t *response, http_transport_error_t *transport_error)
{
	if((url == NULL) || (json == NULL) || (response == NULL))
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	if(transport_error != NULL)
	{
		memset(transport_error, 0, sizeof(*transport_error));
	}
	
	size_t cert_size = server_cert_pem_end - server_cert_pem_start;
	ESP_LOGI(TAG, "Embedded certeficate address: %p", server_cert_pem_start);
	ESP_LOGI(TAG, "Embedded certificate size:%u bytes", (unsigned)cert_size);
	
	memset(response, 0, sizeof(*response));
	response->content_length = -1;
	
	http_event_context_t event_context = 
	{
		.response = response,
		.transport_error = transport_error
	};
	
	esp_http_client_config_t config = {
		.url = url,
		.method = HTTP_METHOD_POST,
		.event_handler = http_event_handler,
		.user_data = &event_context,
		.timeout_ms = 10000,
		
		.cert_pem = (const char*)server_cert_pem_start
	};
	
	esp_http_client_handle_t client = esp_http_client_init(&config);
	if(client == NULL)
	{
		ESP_LOGE(TAG, "Failed to init HTTP client");
		return ESP_ERR_NO_MEM;
	}
	
	esp_err_t err = esp_http_client_set_header(client, "Content-Type", "application/json");
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to set Content-Type");
		esp_http_client_cleanup(client);
		return err;
	}
	
	err = esp_http_client_set_post_field(client, json, strlen(json));
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to set HTTPS POST body");
		esp_http_client_cleanup(client);
		return err;		
	}
	
	ESP_LOGI(TAG, "HTTPS POST URL: %s", url);
	ESP_LOGI(TAG, "HTTPS POST JSON: %s", json);
	
	err = esp_http_client_perform(client);
	
	if(err != ESP_OK)
	{
		if(transport_error != NULL)
		{
			transport_error->socket_errno = esp_http_client_get_errno(client);
		}
		ESP_LOGE(TAG, "HTTP POST Failed %s", esp_err_to_name(err));
	}
	else
	{
		response->status_code = esp_http_client_get_status_code(client);
		response->content_length = esp_http_client_get_content_length(client);
		
		ESP_LOGI(TAG, "HTTP status=%d, content_length=%" PRId64 , response->status_code, response->content_length);
	}
	
	esp_http_client_cleanup(client);
	
	return err;
}







































