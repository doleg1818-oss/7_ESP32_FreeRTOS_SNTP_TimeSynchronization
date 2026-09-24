/*
 * http_recovery_task.h
 *
 *  Created on: Sep 19, 2026
 *      Author: Olegd
 */

#ifndef MAIN_HTTP_HTTP_RECOVERY_TASK_H_
#define MAIN_HTTP_HTTP_RECOVERY_TASK_H_

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "http_client_task.h"


esp_err_t http_recovery_task_init(void);
esp_err_t http_recovery_task_chedule(const http_client_request_t *request, TickType_t timeout_ticks);

void http_recovery_manager_notify_retry_sceduled(void);
 
#endif /* MAIN_HTTP_HTTP_RECOVERY_TASK_H_ */
