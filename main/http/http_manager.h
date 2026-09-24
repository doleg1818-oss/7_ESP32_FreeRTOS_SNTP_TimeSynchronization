/*
 * http_manager.h
 *
 *  Created on: Sep 16, 2026
 *      Author: Olegd
 */

#ifndef MAIN_HTTP_HTTP_MANAGER_H_
#define MAIN_HTTP_HTTP_MANAGER_H_


#include "stdint.h"
#include "esp_err.h"

#include "http_client_task.h"

typedef enum {
	HTTP_MANAGER_RESULT_SUCCSESS = 0,
	HTTP_MANAGER_RESULT_TRANSPORT_ERROR,
	HTTP_MANAGER_RESULT_REDIRECT,
	HTTP_MANAGER_RESULT_HTTP_CLIENT_ERROR,
	HTTP_MANAGER_RESULT_HTTP_SERVER_ERROR,
	HTTP_MANAGER_RESULT_UNKNOWN_ERROR
}http_manager_result_type_t;

typedef struct {
	// Submit side
	uint32_t submit_attempts;			// Скільки разів намагалася передати request 
	uint32_t request_submitted;			// Скільки успішно потрапило в request_queue
	uint32_t request_dropped;			// Queue була full і submeet timeout закінчився
	uint32_t submit_errors;				// Інші помилки submeet
	
	// Result side
	uint32_t total;						// Скільки HTTP operations уже завершилося 
	
	uint32_t successful;				// HTTP 2xx
	uint32_t transport_errors;			// TCP/TLS/connect/read/write tmeout ...
	uint32_t redirects;
	uint32_t http_client_errors;		// HTTP 4xx
	uint32_t http_server_errors;		// HTTP 5xx
	uint32_t unknown_errors;
	
	uint32_t last_request_id;
	int last_http_status;
	esp_err_t last_error;
}http_manager_stats_t;

esp_err_t http_manager_init(void);
http_manager_result_type_t http_manager_classify_result(const http_client_result_t *result);
esp_err_t http_manager_process_result(const http_client_result_t *result, http_manager_result_type_t *result_type);
esp_err_t http_manager_get_stats(http_manager_stats_t *status);
const char * http_manager_result_type_to_status(http_manager_result_type_t type);
esp_err_t http_manager_submit(const http_client_request_t *request, TickType_t timeout_ticks);






#endif /* MAIN_HTTP_HTTP_MANAGER_H_ */
