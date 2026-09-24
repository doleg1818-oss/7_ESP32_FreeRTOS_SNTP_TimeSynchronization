/*
 * sntp_time_manager.h
 *
 *  Created on: Sep 23, 2026
 *      Author: Olegd
 */

#ifndef MAIN_TIME_SNTP_TIME_MANAGER_H_
#define MAIN_TIME_SNTP_TIME_MANAGER_H_

#include "stdbool.h"
#include "stdint.h"
#include "time.h"

#include "esp_err.h"


typedef enum{
	SNTP_TIME_STATE_UNINITIALIZED = 0,
	SNTP_TIME_STATE_WAITING_FOR_SYNC,
	SNTP_TIME_STATE_SYNCED
}sntp_time_state_t;

typedef struct{
	const char *server;
	const char *timezone;
	
	uint32_t sync_interval_ms;
}sntp_time_manager_config_t;

typedef struct{
	bool initialized;
	sntp_time_state_t state;
	uint32_t sync_count;
	time_t last_sinc_timestamp;
	uint32_t sync_interval_ms;
}sntp_time_satatus_t;

esp_err_t sntp_time_manager_init(const sntp_time_manager_config_t *config);

// Synchronisation
esp_err_t sntp_time_manager_wait_for_sync(uint32_t timeout_ms);
bool sntp_time_manager_is_synced(void);

// time
esp_err_t sntp_time_manager_get_timestamp(time_t *timestamp);
esp_err_t sntp_time_manager_get_utc_time(struct tm *timeinfo);
esp_err_t sntp_time_manager_get_localtime(struct tm *timestamp);

// Status
esp_err_t sntp_time_manager_get_status(sntp_time_satatus_t *status);
const char * sntp_time_manager_state_to_string(sntp_time_state_t state);

esp_err_t sntp_time_manager_get_sync_age(uint32_t *age_seconds);
void sntp_time_deinit(void);
void test_sntp(void);


#endif /* MAIN_TIME_SNTP_TIME_MANAGER_H_ */
