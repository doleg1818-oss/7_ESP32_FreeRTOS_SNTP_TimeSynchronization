/*
 * http_get_client.h
 *
 *  Created on: Aug 25, 2026
 *      Author: Olegd
 */

#ifndef MAIN_HTTP_HTTP_CLIENT_H_
#define MAIN_HTTP_HTTP_CLIENT_H_

#include "esp_err.h"

#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

#define HTTP_GET_RESPONSE_BODY_SIZE 1024U
#define HTTP_GET_CONTENT_TYPE_SIZE  64U

typedef struct{
	int status_code;
	int64_t content_length;
	size_t body_length;
	bool body_truncated;
	
	char content_type[HTTP_GET_CONTENT_TYPE_SIZE];
	char body[HTTP_GET_RESPONSE_BODY_SIZE];
}http_response_t;

typedef struct{
	int socket_errno; 			// Low level soked error 
	esp_err_t tls_error;		// Помилка ESP_LTS layer
	int tls_error_code;			// Детальніший код mbedLTS
	int tls_verify_flags;		// Проблеми перевірки сертифікату
}http_transport_error_t;


esp_err_t http_client_get(const char *urr, http_response_t *response, http_transport_error_t *transport_error);
esp_err_t http_client_post_json(const char *url, const char *json, http_response_t *response, http_transport_error_t *transport_error);
esp_err_t http_client_post_json_https(const char *url, const char *json, http_response_t *response, http_transport_error_t *transport_error);


#endif /* MAIN_HTTP_HTTP_CLIENT_H_ */
