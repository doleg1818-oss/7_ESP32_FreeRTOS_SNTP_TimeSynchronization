/*
 * http_recovery_manager.h
 *
 *  Created on: Sep 18, 2026
 *      Author: Olegd
 */

#ifndef MAIN_HTTP_HTTP_RECOVERY_MANAGER_H_
#define MAIN_HTTP_HTTP_RECOVERY_MANAGER_H_

#include "http_client_task.h"
#include "stdint.h"

typedef enum{
	HTTP_RECOVERY_ACTION_NONE = 0,
	HTTP_RECOVERY_ACTION_RETRY,
	HTTP_RECOVERY_ACTION_DO_NOT_RETRY
}http_recovery_action_t;

typedef struct {
	uint32_t retries_prepared;		 	// Скільки було сформовано і дозволено
	uint32_t retries_scheduled;			// Скільки реально попало а retry_queue
	uint32_t retries_exhausted;			// Скільки досягли max_retries
	uint32_t recovered_requests;			// Скільки request стали SUCCESS (хоча б після одного retry)
}http_recovery_stats_t;



http_recovery_action_t http_recovery_action_manager_get_cation(const http_client_result_t *result);
const char *http_recovery_manager_action_to_string(http_recovery_action_t action);
esp_err_t http_recovery_manager_prepare_retry(const http_client_result_t *result, http_client_request_t *retry_request);
uint32_t http_recovery_manager_get_backoff_ms(uint8_t retry_count);
uint32_t http_recovery_manager_get_jitter_ms(uint8_t retry_count);
void http_recovery_manager_notify_retry_sceduled(void);

esp_err_t http_recovery_manager_init(void);
esp_err_t http_recovery_manager_get_stats(http_recovery_stats_t *ststs);
void http_recovery_manager_notify_retry_sceduled(void);
void http_recovery_manager_notify_recovered(void);

#endif /* MAIN_HTTP_HTTP_RECOVERY_MANAGER_H_ */
