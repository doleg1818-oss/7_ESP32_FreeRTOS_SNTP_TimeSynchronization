/*
 * sntp_time_manager.c
 *
 *  Created on: Sep 23, 2026
 *      Author: Olegd
 */

#include "sntp_time_manager.h"

#include "esp_err.h"
#include "stdlib.h"
#include "string.h"
#include "sys/time.h"

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include "freertos/FreeRTOS.h"

#include "inttypes.h"

static const char *TAG = "SNTP TIME MANAGER";

#define SNTP_TIME_SERVER_MAX_LEN 	64U
#define SNTP_TIME_TIMEZONE_MAX_LEN	64U

#define SNTP_MIN_SNTP_INTERVAL_MS 	15000U

typedef struct{
	bool initialized;
	sntp_time_state_t state;
	uint32_t sync_count;
	time_t last_sinc_timestamp;
	uint32_t sync_interval_ms;
	
	char server[SNTP_TIME_SERVER_MAX_LEN];
	char timezone[SNTP_TIME_TIMEZONE_MAX_LEN];
}sntp_time_manager_context_t;

static sntp_time_manager_context_t ctx = {
	.initialized = false,
	.state = SNTP_TIME_STATE_UNINITIALIZED,
	.sync_count = 0,
	.last_sinc_timestamp = 0,
	.sync_interval_ms = 0
};

static portMUX_TYPE ctx_lock = portMUX_INITIALIZER_UNLOCKED;


static void sntp_time_sync_callback(struct timeval *tv)
{
	static const char *TAG = "SNTP CALLBACK";
	
	if(tv == NULL)
	{
		return;
	}
	
	portENTER_CRITICAL(&ctx_lock);  
	
	ctx.state = SNTP_TIME_STATE_SYNCED;
	ctx.sync_count++;
	ctx.last_sinc_timestamp = tv->tv_sec;
	
	uint32_t sync_count = ctx.sync_count;
	
	portEXIT_CRITICAL(&ctx_lock);
	
	ESP_LOGI(TAG, "Time sincronized. Count =%" PRIu32 ", timestemp=%lld", sync_count, (long long)tv->tv_sec);
}

static bool sntp_time_manager_config_is_valid(const sntp_time_manager_config_t *config)
{
	if(config == NULL)
	{
		return false;
	}
	if(config->server == NULL)
	{
		return false;
	}
	if(config->timezone == NULL)
	{
		return false;
	}
	if(config->server[0] == '\0')
	{
		return false;
	}
	if(config->timezone[0] == '\0')
	{
		return false;
	}
	if(strlen(config->server) >= SNTP_TIME_SERVER_MAX_LEN)
	{
		return false;
	}
	if(strlen(config->timezone) >= SNTP_TIME_TIMEZONE_MAX_LEN)
	{
		return false;	
	}
	if(config->sync_interval_ms < SNTP_MIN_SNTP_INTERVAL_MS)
	{
		return false;
	}
	return true;
}

esp_err_t sntp_time_manager_init(const sntp_time_manager_config_t *config)
{
	if(sntp_time_manager_config_is_valid(config) == false)
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	portENTER_CRITICAL(&ctx_lock); 
	if(ctx.initialized)						// Якщо не ініціаілізований
	{
		portEXIT_CRITICAL(&ctx_lock);
		return ESP_ERR_INVALID_STATE; 
	}
	portEXIT_CRITICAL(&ctx_lock);
	
	// копіювання з конфігураційної структури в структуру контексту
	strlcpy(ctx.server, config->server, sizeof(ctx.server));
	strlcpy(ctx.timezone, config->timezone, sizeof(ctx.timezone));
	ctx.sync_interval_ms = config->sync_interval_ms;
	
	// configure timezone
	if(setenv("TZ", ctx.timezone, 1))
	{
		ESP_LOGE(TAG, "Failed to configure timezone");
		return ESP_FAIL;
	}
	tzset();
	
	// Prepare manager state 
	portENTER_CRITICAL(&ctx_lock); 
	ctx.initialized = true;
	ctx.state = SNTP_TIME_STATE_WAITING_FOR_SYNC;
	ctx.sync_count = 0;
	ctx.last_sinc_timestamp = 0;
	portEXIT_CRITICAL(&ctx_lock); 
	
	// SNTP Configuration
	esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ctx.server);
	sntp_config.sync_cb = sntp_time_sync_callback;
	esp_err_t err = esp_netif_sntp_init(&sntp_config);
		
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to Initialization SNTP, err = %s", esp_err_to_name(err));
		
		portENTER_CRITICAL(&ctx_lock); 
		ctx.initialized = false;
		ctx.state = SNTP_TIME_STATE_UNINITIALIZED;
		portEXIT_CRITICAL(&ctx_lock); 
		
		return err;
	}
	
	// Configure periodic syncronization
	esp_sntp_set_sync_interval(ctx.sync_interval_ms);
	
	ESP_LOGI(TAG, "SNTP manager initialized");
	ESP_LOGI(TAG, "Server: %s", ctx.server);
	ESP_LOGI(TAG, "Timezone: %s", ctx.timezone);
	ESP_LOGI(TAG, "Sinc interval: %" PRIu32 " ms", ctx.sync_interval_ms);
	
	return ESP_OK;
}

// Synchronisation
esp_err_t sntp_time_manager_wait_for_sync(uint32_t timeout_ms)
{
	portENTER_CRITICAL(&ctx_lock); 
	bool initialized = ctx.initialized;
	portEXIT_CRITICAL(&ctx_lock); 
	
	if(initialized == false)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	ESP_LOGI(TAG, "Waiting for initial time sincronization");
	
	esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Time sincronization failed. err=%s", esp_err_to_name(err));
		return err;
	}
	ESP_LOGI(TAG, "Initial time sincronization complited");
	
	return ESP_OK;
}

bool sntp_time_manager_is_synced(void)
{
	portENTER_CRITICAL(&ctx_lock); 
	bool synced = (ctx.state == SNTP_TIME_STATE_SYNCED);
	portEXIT_CRITICAL(&ctx_lock); 
	
	return synced;
}

// time
esp_err_t sntp_time_manager_get_timestamp(time_t *timestamp)
{
	if(timestamp == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	} 
	
	if(sntp_time_manager_is_synced() == false)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	time_t now = time(NULL);
	
	if(now == (time_t)-1)
	{
		return ESP_FAIL;
	}
	
	*timestamp = now;
	
	return ESP_OK;
}
esp_err_t sntp_time_manager_get_utc_time(struct tm *timeinfo)
{
	if(timeinfo == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	time_t timestamp;
	
	esp_err_t err = sntp_time_manager_get_timestamp(&timestamp);
	if(err != ESP_OK)
	{
		return err;
	}
	
	if(gmtime_r(&timestamp, timeinfo) == NULL)
	{
		return ESP_FAIL;
	}	
	return ESP_OK;
}
esp_err_t sntp_time_manager_get_localtime(struct tm *timeinfo)
{
	if(timeinfo == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	time_t timestamp;
	
	esp_err_t err = sntp_time_manager_get_timestamp(&timestamp);
	if(err != ESP_OK)
	{
		return err;
	}
	
	if(localtime_r(&timestamp, timeinfo) == NULL)
	{
		return ESP_FAIL;
	}
	return ESP_OK;
}

// Status
esp_err_t sntp_time_manager_get_status(sntp_time_satatus_t *status)
{
	if(status == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	portENTER_CRITICAL(&ctx_lock); 
	status->initialized = ctx.initialized;
	status->state = ctx.state;
	status->sync_count = ctx.sync_count;
	status->last_sinc_timestamp = ctx.last_sinc_timestamp;
	status->sync_interval_ms = ctx.sync_interval_ms;
	portEXIT_CRITICAL(&ctx_lock); 
	
	return ESP_OK;
}

const char * sntp_time_manager_state_to_string(sntp_time_state_t state)
{
	switch(state)
	{
		case SNTP_TIME_STATE_UNINITIALIZED:
			return "UNINITIALIZED";
		case SNTP_TIME_STATE_WAITING_FOR_SYNC:
			return "WAITING_FOR_SYNC";
		case SNTP_TIME_STATE_SYNCED:
			return "SYNCED";
		default:
			return "UNKNOWN";
	}
}

void sntp_time_deinit(void)
{
	portENTER_CRITICAL(&ctx_lock); 
	bool initialized = ctx.initialized;
	portEXIT_CRITICAL(&ctx_lock); 
	
	if(initialized == false)
	{
		return;
	}
	
	esp_netif_sntp_deinit();
	
	portENTER_CRITICAL(&ctx_lock); 
	memset(&ctx, 0, sizeof(ctx));
	ctx.initialized = false;
	portEXIT_CRITICAL(&ctx_lock);
	
	ESP_LOGI(TAG, "SNTP Time meneger deinitialized"); 
}

esp_err_t sntp_time_manager_get_sync_age(uint32_t *age_seconds)
{
	if(age_seconds == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	portENTER_CRITICAL(&ctx_lock); 
	bool synced = (ctx.state == SNTP_TIME_STATE_SYNCED);
	time_t last_sinc = ctx.last_sinc_timestamp;
	portEXIT_CRITICAL(&ctx_lock);
	
	if(synced == false)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	time_t now = time(NULL);
	
	if(now == (time_t)-1)
	{
		return ESP_FAIL;
	}
	if(now < last_sinc)
	{
		return ESP_FAIL;
	}
	
	*age_seconds = (uint32_t)(now - last_sinc);
	
	return ESP_OK;
}

void test_sntp(void)
{
	uint32_t count = 0;
	
	while(1)
	{
		time_t timestamp;
	   
	 	if(sntp_time_manager_get_timestamp(&timestamp) == ESP_OK)
	   	{
			ESP_LOGI(TAG, "Timestamp: %lld", (long long)timestamp);
	   	}
	   
	   	struct tm local_time;
	   	if(sntp_time_manager_get_localtime(&local_time) == ESP_OK)
	   	{
			char buffer[64] = {0,};
		   	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
		 	ESP_LOGI(TAG, "Local time: %s", buffer);
		 	
		 	uint32_t sinc_age = 0;
		 	if(sntp_time_manager_get_sync_age(&sinc_age) == ESP_OK)
		 	{
				ESP_LOGI(TAG, "Last SNTP sinc age: %" PRIu32, sinc_age);	 
			}
	   	}
	  	
	   	count++;
	   	if(count >= 20)
	   	{
			sntp_time_satatus_t status;
		   	if(sntp_time_manager_get_status(&status) == ESP_OK)
		   	{
				ESP_LOGI(TAG, "Time state=%s", sntp_time_manager_state_to_string(status.state));
			   	ESP_LOGI(TAG, "Sinc count=%" PRIu32, status.sync_count);
			   	ESP_LOGI(TAG, "Last sync=%lld", (long long)status.last_sinc_timestamp);
		   	}
		   	count = 0;
	   	}
	   	vTaskDelay(pdMS_TO_TICKS(1000));	
	}
}



















