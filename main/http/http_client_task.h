/*
 * http_client_task.h
 *
 *  Created on: Sep 14, 2026
 *      Author: Olegd
 */

#ifndef MAIN_HTTP_HTTP_CLIENT_TASK_H_
#define MAIN_HTTP_HTTP_CLIENT_TASK_H_

#include "stdint.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "http_client.h"
#include "esp_log.h"

#define HTTP_CLIENT_REQUEST_QUEUE_LENGTH	4U

#define HTTP_CLIENT_REQUEST_URL_SIZE		192U
#define HTTP_CLIENT_REQUEST_JSON_SIZE		512U

typedef enum{
	HTTP_CLIENT_REQUEST_HTTP_GET = 0,
	HTTP_CLIENT_REQUEST_HTTP_POST_JSON,
	HTTP_CLIENT_REQUEST_HTTPS_POST_JSON
}http_client_request_type_t;

typedef struct{
	uint32_t request_id;
	http_client_request_type_t type;
	
	uint8_t retry_count;
	uint8_t max_retries;
	
	char url[HTTP_CLIENT_REQUEST_URL_SIZE];
	char json[HTTP_CLIENT_REQUEST_JSON_SIZE];
}http_client_request_t;

typedef struct{
	http_client_request_t request; 
	
	esp_err_t err;
	
	// transport diagnostics
	http_transport_error_t transport_error;
	
	http_response_t response;
}http_client_result_t;


esp_err_t http_client_task_submit(const http_client_request_t *request, TickType_t timeout_ticks);
esp_err_t http_client_task_receive_result(http_client_result_t *result ,TickType_t timeout_ticks);
esp_err_t http_client_task_init(void);

#endif /* MAIN_HTTP_HTTP_CLIENT_TASK_H_ */
