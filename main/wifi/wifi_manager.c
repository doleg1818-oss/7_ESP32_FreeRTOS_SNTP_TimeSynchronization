/*
 * wifi_manager.c
 *
 *  Created on: Aug 21, 2026
 *      Author: Olegd
 */

#include "wifi_manager.h"


#include "stdbool.h"
#include "inttypes.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "stdlib.h"


static const char *TAG = "WIFI STA";
static const char *MANAGER_TAG = "WIFI MANAGER TASK";

#define WIFI_MANAGER_QUEUE_LENGTH 			10

#define WIFI_MANAGER_TASK_SIZE				4096
#define WIFI_MANAGER_TASK_PRIORITY			5
#define WIFI_MANAGER_TASK_CORE				1


// Для визначення для від кого прийшла коменда на RECONNECT
typedef enum{
	WIFI_SCAN_PURPOSE_NONE = 0,
	WIFI_SCAN_PURPOSE_CONNECT,			// Якщо ESP32 сам переключається через втрату звязку за AP
	WIFI_SCAN_PURPOSE_USER				// Якщо USER сам перемикає
} wifi_scan_purpose_t;

// Описує методи відновлення підключення до AP
typedef enum{
	WIFI_RECOVERY_ACTION_NONE = 0,
	WIFI_RECOVERY_ACTION_DIRECT_RECONNECT,
	WIFI_RECOVERY_ACTION_RESCAN,
	WIFI_RECOVERY_ACTION_STOP
}wifi_recovery_action_t;

typedef enum {
	// Internal events from ESP_IDF
	WIFI_MANAGER_MSG_STA_START = 0,
	WIFI_MANAGER_MSG_CONNECTED,
	WIFI_MANAGER_MSG_GOT_IP,
	WIFI_MANAGER_MSG_DISCONNECTED,
	WIFI_MANAGER_MSG_SCAN_DONE,
	
	// External commands
	WIFI_MANAGER_CMD_CONNECT,
	WIFI_MANAGER_CMD_DISCONNECT,
	WIFI_MANAGER_CMD_SCAN,
	WIFI_MANAGER_CMD_RECONNECT
}wifi_manager_message_id_t;

// Queue structure
typedef struct{
	wifi_manager_message_id_t id;
	wifi_err_reason_t disconnect_reason;
}wifi_manager_message_t;


#define WIFI_MANAGER_SSID_MAX_LEN 			33U
#define WIFI_MANAGER_PASSWORD_MAX_LEN 		65U

typedef struct
{
	char ssid[WIFI_MANAGER_SSID_MAX_LEN];
	char password[WIFI_MANAGER_PASSWORD_MAX_LEN];
	
	uint32_t reconnect_base_delay_ms;
	uint32_t reconnect_max_delay_ms;
	uint32_t reconnect_jitter_ms;
	
	uint32_t max_reconnect_attempts;
	uint32_t same_ap_reconnect_limit;
	
	bool auto_connect;
}wifi_manager_internal_config_t;

typedef struct {
	wifi_manager_internal_config_t wifi_config; 		// Configureaton
	wifi_state_t state;									// State mashine
	
	// Connection state
	wifi_ap_record_t selected_ap;
	bool selected_ap_valid;
	
	bool auto_reconnect_enabled; 	
	
	// Recovery
	uint32_t reconnect_attempts;				// General reconnect counter
	uint32_t same_ap_reconnect_attempts;
	
	wifi_recovery_action_t pending_recovery_action;
	
	wifi_err_reason_t last_disconnect_reason;
	bool last_disconnect_reason_valid;
	
	// scan
	wifi_scan_purpose_t scan_purpose;
	
	// ESP_IDF FreeRTOS resources
	esp_netif_t *sta_netif;
	
	QueueHandle_t queue;
	TimerHandle_t reconnect_timer;
	TaskHandle_t task_handle;
	
	// Manager lifecycle
	bool initialized;
	
	// Public status snapshot
	SemaphoreHandle_t status_mutex;
	wifi_manager_status_t status_snapshot;
	
	uint32_t disconnect_count;
	uint32_t recovery_count;
	uint32_t scan_count;
	
	// хендлери для видалення
	esp_event_handler_instance_t wifi_event_instance;
	esp_event_handler_instance_t ip_event_instance;
	
}wifi_manager_context_t;

static wifi_manager_context_t ctx = {
	.state = WIFI_STATE_INIT,
	
	.selected_ap_valid = false,
	.auto_reconnect_enabled = false,
	
	.reconnect_attempts = 0,
	.same_ap_reconnect_attempts = 0,
	
	.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE,
	
	.last_disconnect_reason = WIFI_REASON_UNSPECIFIED,
	
	.last_disconnect_reason_valid = false,
	
	.scan_purpose = WIFI_SCAN_PURPOSE_NONE,
	
	.initialized = false,
	
	.disconnect_count = 0,
	.recovery_count = 0,
	.scan_count = 0
	
};


static void wifi_manager_build_status(wifi_manager_status_t *status)
{
	memset(status, 0, sizeof(*status));
	
	status->initialized = ctx.initialized;
	status->state = ctx.state;
	status->online = (ctx.state == WIFI_STATE_ONLINE);
	status->auto_reconnect_enabled = ctx.auto_reconnect_enabled;
	status->reconnect_attempts = ctx.reconnect_attempts;
	status->same_ap_reconnect_attempts = ctx.same_ap_reconnect_attempts;
	status->last_disconnect_reason_valid = ctx.last_disconnect_reason_valid;
	status->last_disconnect_reason = ctx.last_disconnect_reason;
	status->selected_ap_valid = ctx.selected_ap_valid;
	status->disconnect_count = ctx.disconnect_count;
	status->recovery_count = ctx.recovery_count;
	status->scan_count = ctx.scan_count;
	
	if(ctx.selected_ap_valid)
	{
		status->selected_ap_rssi = ctx.selected_ap.rssi;
		status->selected_ap_channel = ctx.selected_ap.primary;
		memcpy(status->selected_ap_bssid, ctx.selected_ap.bssid, WIFI_MANAGER_BSSID_LEN);
	}
}

static bool wifi_manager_publish_status(void)
{
	if(ctx.status_mutex == NULL)
	{
		return false;
	}
	
	wifi_manager_status_t snapshot;
	
	// Build locally first. 
	// М'ютекс не утримується під час читання ctx, що належить менеджеру.
	wifi_manager_build_status(&snapshot);
	
	if(xSemaphoreTake(ctx.status_mutex, portMAX_DELAY) != pdTRUE)
	{
		return false;
	}
	
	ctx.status_snapshot = snapshot; 
	
	xSemaphoreGive(ctx.status_mutex);
	
	return true;
}

static const char *wifi_disconnect_reason_to_string(wifi_err_reason_t reason)
{
	switch(reason)
	{
		case WIFI_REASON_AUTH_EXPIRE:	
			return "AUTH_EXPIRE";
			
		case WIFI_REASON_AUTH_LEAVE:	
			return "AUTH_LEAVE";
			
		case WIFI_REASON_ASSOC_EXPIRE:	
			return "ASSOC_EXPIRE";			
			
		case WIFI_REASON_ASSOC_TOOMANY:	
			return "ASSOC_TOOMANY";			
			
		case WIFI_REASON_NOT_AUTHED:	
			return "NOT_AUTHED";			
			
		case WIFI_REASON_NOT_ASSOCED:	
			return "NOT_ASSOCED";
			
		case WIFI_REASON_ASSOC_LEAVE:	
			return "ASSOC_LEAVE";
			
		case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:	
			return "4WAY_HANDSHAKE_TIMEOUT";
			
		case WIFI_REASON_BEACON_TIMEOUT:	
			return "BEACON_TIMEOUT";
			
		case WIFI_REASON_NO_AP_FOUND:	
			return "NO_AP_FOUND";	
			
		case WIFI_REASON_TIMEOUT:
			return "TIMEOUT";
		
		case WIFI_REASON_CONNECTION_FAIL:
			return "CONNECTION_FAIL";
		
		default:
			return "UNKNOWN";
	}
}

const char *wifi_manager_state_to_string(wifi_state_t state)
{
	switch(state)
	{
		case WIFI_STATE_INIT:
			return "INIT";
			
		case WIFI_STATE_CONNECTING:
			return "CONNECTING";
			
		case WIFI_STATE_SELECTING_AP:
			return "SELECTING_AP";
			
		case WIFI_STATE_CONNECTED:
			return "CONNECTED";
			
		case WIFI_STATE_ONLINE:
			return "ONLINE";
			
		case WIFI_STATE_DISCONNECTED:
			return "DISCONNECTED";
		
		default:
			return "UNKNOWN";
	}
}

// Записує вхідні дані в Queue і відсилає до wifi_manager_task
static bool wifi_manager_send_message(wifi_manager_message_t *message)
{
	if(message == NULL)
	{
		return false;
	}
	
	if(ctx.queue == NULL)
	{
		ESP_LOGE(MANAGER_TAG, "WiFi manager queue is NULL");
		return false;
	}
	
	BaseType_t status = xQueueSend(ctx.queue, message, 0);   	
	if(status != pdPASS)
	{
		ESP_LOGW(MANAGER_TAG, "Failed to send message to wifi manager");
		return false;
	}
	return true;
}

/* Отримує причину відключення, в залежності від причини відключення 
вертає дію яку потрібно зробити після цього відключення відповідно до enum wifi_recovery_action_t
*/ 
static wifi_recovery_action_t wifi_get_recovery_action(wifi_err_reason_t reason)
{
	switch(reason)
	{
		// AP був доступний, але звязок з ним пропав
		// Спочатку є сенс спробувати старий BSSID
		case WIFI_REASON_BEACON_TIMEOUT:
			return WIFI_RECOVERY_ACTION_DIRECT_RECONNECT;
			
		// Driver не знаходить AP 
		// Немає сенсу повторювати той самий BSSID
		case WIFI_REASON_NO_AP_FOUND:
			return WIFI_RECOVERY_ACTION_RESCAN;
			
		// Тимчасові connection failues.
		// Дозволино кілька direct retries, після чого робиться повний rescan
		case WIFI_REASON_TIMEOUT:
		case WIFI_REASON_CONNECTION_FAIL:
		case WIFI_REASON_AUTH_EXPIRE:
		case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
			
			if(ctx.same_ap_reconnect_attempts >= ctx.wifi_config.same_ap_reconnect_limit)
			{
				return WIFI_RECOVERY_ACTION_RESCAN;
			}
			return WIFI_RECOVERY_ACTION_DIRECT_RECONNECT;
		
		// для Internal disconnect recovery не потрібний 
		case WIFI_REASON_ASSOC_LEAVE:
			return WIFI_RECOVERY_ACTION_STOP;
			
		//Для невідомої transient problem
		default:
			if(ctx.same_ap_reconnect_attempts >= ctx.wifi_config.same_ap_reconnect_limit)
			{
				return WIFI_RECOVERY_ACTION_RESCAN;
			}	
			return WIFI_RECOVERY_ACTION_DIRECT_RECONNECT;
	}
}



////////////////////////////////////////  API FUNCTIONS /////////////////////////////////////////////
/* Ці функції зроблені для того щоб можна було керувати WiFi ззовні
wifi_manager_scan
wifi_manager_connect
wifi_manager_disconnect
wifi_manager_get_state
wifi_manager_is_online
*/ 

esp_err_t wifi_manager_get_status(wifi_manager_status_t *status)
{
	if(status == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	if(ctx.status_mutex == NULL)
	{
		return ESP_ERR_INVALID_STATE;
	}
	
	if(xSemaphoreTake(ctx.status_mutex, portMAX_DELAY) != pdTRUE)
	{
		return ESP_FAIL;
	}
	
	*status = ctx.status_snapshot;
	
	xSemaphoreGive(ctx.status_mutex);
	
	return ESP_OK;
}

wifi_state_t wifi_manager_get_state(void)
{
	wifi_manager_status_t status;
	
	if(wifi_manager_get_status(&status) != ESP_OK)
	{
		return WIFI_STATE_INIT;
	}
	
	return status.state;
}

bool wifi_manager_is_online(void)
{
	wifi_manager_status_t status;
	
	if(wifi_manager_get_status(&status) != ESP_OK)
	{
		return false;
	}
	
	return status.online;
}

bool wifi_manager_scan(void)
{
	wifi_manager_message_t message = {
		.id = WIFI_MANAGER_CMD_SCAN
	};
	return wifi_manager_send_message(&message);
}

bool wifi_manager_disconnect(void)
{
	wifi_manager_message_t message = {
		.id = WIFI_MANAGER_CMD_DISCONNECT
	};
	return wifi_manager_send_message(&message);
}

bool wifi_manager_connect(void)
{
	wifi_manager_message_t message = {
		.id = WIFI_MANAGER_CMD_CONNECT
	};
	return wifi_manager_send_message(&message);
}
//////////////////////////////////////////////////////////////////////////////////////////////////

static bool wifi_manager_config_is_valid(const wifi_manager_config_t *config)
{
	if(config == NULL)
	{
		return false;
	}
	if(config->ssid == NULL)
	{
		return false;
	}
	if(config->password == NULL)
	{
		return false;
	}
	if(config->ssid[0] == '\0')
	{
		return false;
	}
	if(strlen(config->ssid) >= WIFI_MANAGER_SSID_MAX_LEN)
	{
		return false;
	}
	if(strlen(config->password) >= WIFI_MANAGER_PASSWORD_MAX_LEN)
	{
		return false;
	}
	if(config->reconnect_base_delay_ms == 0)
	{
		return false;
	}
	if(config->reconnect_max_delay_ms < config->reconnect_base_delay_ms)
	{
		return false;
	}
	if(config->max_reconnect_attempts == 0)
	{
		return false;
	}
	
	return true;
}
static void wifi_manager_copy_config(const wifi_manager_config_t *config)
{
	strlcpy(ctx.wifi_config.ssid, config->ssid, sizeof(ctx.wifi_config.ssid));
	strlcpy(ctx.wifi_config.password, config->password, sizeof(ctx.wifi_config.password));
	ctx.wifi_config.reconnect_base_delay_ms = config->reconnect_base_delay_ms;
	ctx.wifi_config.reconnect_max_delay_ms = config->reconnect_max_delay_ms;
	ctx.wifi_config.reconnect_jitter_ms = config->reconnect_jitter_ms;
	ctx.wifi_config.max_reconnect_attempts = config->max_reconnect_attempts;
	ctx.wifi_config.same_ap_reconnect_limit = config->same_ap_reconnect_limit;
	ctx.wifi_config.auto_connect = config->auto_connect;
}

// Зберігає стан State Mashine в глобальну змінну
static void wifi_manager_set_state(wifi_state_t new_state)
{
	if(ctx.state == new_state)   // if no changing
	{
		return;
	}
	ESP_LOGI(MANAGER_TAG, "STATE: %s -> %s", wifi_manager_state_to_string(ctx.state), wifi_manager_state_to_string(new_state));
	ctx.state = new_state;
}

// Сканує AP і в залежності від purpose просто сканує і виводить список просканованих AP або сканує івибирає TARGET AP мережу
// і записує її в глобальну змінну selected_ap
static esp_err_t wifi_manager_process_scan_results(wifi_scan_purpose_t purpose)
{
	static const char *TAG = "WIFI SCAN";
	uint16_t ap_count = 0;
	
	esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(err));
		return err;
	}
	ESP_LOGI(TAG, "Scan complited. FOund %d", ap_count);
	
	if(ap_count == 0)
	{
		return ESP_FAIL;
	}
	
	wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(wifi_ap_record_t));
	if(ap_records == NULL)
	{
		ESP_LOGE(TAG, "Failed to allocate memory");
		return ESP_ERR_NO_MEM;
	}
	
	uint16_t record_to_ger = ap_count;
	err = esp_wifi_scan_get_ap_records(&record_to_ger, ap_records);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "esp_wifi_scan_get_ap_records Failed: %s", esp_err_to_name(err));
		free(ap_records);
		return err;
	}
	
	// Print all
	for(uint16_t i = 0; i < record_to_ger; i++)
	{
		ESP_LOGI(TAG, 
			"[%u] SSID: %-32s, | RSSI: %" PRId8
			" | CH: %u | BSSID: " MACSTR,
			i,
			(char*)ap_records[i].ssid,
			ap_records[i].rssi,
			ap_records[i].primary,
			MAC2STR(ap_records[i].bssid));
	}	
	
	// Якщо команда на сканування була від user тоді не підключатись на знайденої AP
	// Тобто функція просто скнанує всі вдоступні AP
	if(purpose == WIFI_SCAN_PURPOSE_USER)		
	{
		free(ap_records);
		return ESP_OK;	
	}
	
	// Якщо команда була від Event handle тоді підключитися до потрібної AP
	bool found = false;
	for(uint16_t i = 0; i < record_to_ger; i++)
	{
		if(ap_records[i].ssid[0] == '\0')						// Ігнорувати PA з прихованим SSID		
		{
			continue;
		}
		if(strcmp((char*)ap_records[i].ssid, ctx.wifi_config.ssid) != 0)  	// Ігнорувати не потрібні AP
		{
			continue;
		}
		
		if(found == false)	
		{
			ctx.selected_ap = ap_records[i];						// Зберегти в глобальну змінну
			found = true;
			continue;
		}
		
		// Записати з найкращим сигналом
		if(ap_records[i].rssi > ctx.selected_ap.rssi)
		{
			ctx.selected_ap = ap_records[i];
		}
	}
	free(ap_records);
	
	if(found == false)
	{
		ESP_LOGW(TAG, "Target AP \"%s\" not found", ctx.wifi_config.ssid);
		return ESP_FAIL;
	}
	
	ctx.selected_ap_valid = true;		// Виставити флаг, що потрібна AP знайдена
	
	ESP_LOGI(TAG, "Selected AP:");
	ESP_LOGI(TAG, " SSID ; %s", (char*)ctx.selected_ap.ssid);
	ESP_LOGI(TAG, " RSSI : %d dBm", ctx.selected_ap.rssi);
	ESP_LOGI(TAG, " CH : %u", ctx.selected_ap.primary);
	ESP_LOGI(TAG, " BSSID : " MACSTR, MAC2STR(ctx.selected_ap.bssid));
	
	return ESP_OK;
}


// Scan APs with non block mode(When scan will done WIFI_EVENT_SCAN_DONE event wil be genereted in wifi_event_handler)
static esp_err_t wifi_manager_start_scan(wifi_scan_purpose_t purpose)   
{
	if(purpose == WIFI_SCAN_PURPOSE_NONE)				//  Запустити скан з реальною причиною
	{
		return ESP_ERR_INVALID_ARG;	
	}
	// wifi_scan_purpose - глобальний поточний стан сканування
	if(ctx.scan_purpose != WIFI_SCAN_PURPOSE_NONE)		// чи не виконується зараз скан
	{
		ESP_LOGW(MANAGER_TAG, "Scan already in progress");
		return ESP_ERR_INVALID_STATE;
	}
	
	wifi_scan_config_t scan_config = {
		.ssid = NULL,
		.bssid = NULL,
		.channel = 0,
		.show_hidden = true,
		.scan_type = WIFI_SCAN_TYPE_ACTIVE,
		.scan_time.active.min = 100,
		.scan_time.active.max = 300
	};
	
	ctx.scan_purpose = purpose;			// Встановити глобальний статув "Від кого було викликано сканування"
	
	ESP_LOGI(MANAGER_TAG, "Starting asinchronous WiFi scan");
	esp_err_t err = esp_wifi_scan_start(&scan_config, false);		// Сканує всі найблищі AP в свою память
	if(err != ESP_OK)
	{
		ESP_LOGE(MANAGER_TAG, "esp_wifi_scan_start failed %s", esp_err_to_name(err));
		ctx.scan_purpose = WIFI_SCAN_PURPOSE_NONE;
		return err;
	}
	ctx.scan_count++;
	
	return ESP_OK;
}
// Запускає загальний процес асинхронного сканування і підключення до AP
static bool wifi_manager_start_ap_selection(void)
{
	// якщо wifi_state не WIFI_STATE_INIT і не WIFI_STATE_DISCONNECTED
	if((ctx.state != WIFI_STATE_INIT) && (ctx.state != WIFI_STATE_DISCONNECTED))
	{
		ESP_LOGW(MANAGER_TAG, "Connect sequence ignored in stste %s",  wifi_manager_state_to_string(ctx.state));	
		return false;
	}
						
	wifi_manager_set_state(WIFI_STATE_SELECTING_AP);
						
	ctx.selected_ap_valid = false;
	
	// Scan networks and select target AP	
	esp_err_t err = wifi_manager_start_scan(WIFI_SCAN_PURPOSE_CONNECT);
	if(err != ESP_OK)
	{
		ESP_LOGE(MANAGER_TAG, "Connected to selected AP failed. err: %s", esp_err_to_name(err));
		wifi_manager_set_state(WIFI_STATE_DISCONNECTED);
		return false;
	}
	return true;
}
// Підключитися до визначеної AP з відомим SSID i Password
static esp_err_t wifi_manager_connect_selected_ap(void)
{
	if(ctx.selected_ap_valid == false)
	{
		ESP_LOGE(MANAGER_TAG, "AP not found");
		return ESP_FAIL;
	}
	
	wifi_config_t sta_config = {0};		// Create local STA configuration
	
	strlcpy((char*)sta_config.sta.ssid, ctx.wifi_config.ssid, sizeof(sta_config.sta.ssid));
	strlcpy((char*)sta_config.sta.password, ctx.wifi_config.password, sizeof(sta_config.sta.password));
	
	// Connect only to AP selected by scan algorithms
	sta_config.sta.bssid_set = true;
	memcpy(sta_config.sta.bssid, ctx.selected_ap.bssid, sizeof(sta_config.sta.bssid));
	
	// Start searching from the chanel where selected AP was found
	sta_config.sta.channel = ctx.selected_ap.primary;
	sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	
	ESP_LOGI(MANAGER_TAG, 
		"Configure selected AP: SSID=%s, RSSI=%d, CH=%u BSSID= " MACSTR,
		ctx.selected_ap.ssid,
		ctx.selected_ap.rssi,
		ctx.selected_ap.primary,
		MAC2STR(ctx.selected_ap.bssid));
	
	esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
	if(err != ESP_OK)
	{
		ESP_LOGE(MANAGER_TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
		return err;
	}
	
	ctx.same_ap_reconnect_attempts = 0;

	wifi_manager_set_state(WIFI_STATE_CONNECTING);
	
	ESP_LOGI(MANAGER_TAG, "Connecting to selected AP");
	
	err= esp_wifi_connect();
	if(err != ESP_OK)
	{
		ESP_LOGE(MANAGER_TAG, "Failed to connect to AP. err:%s", esp_err_to_name(err));
		wifi_manager_set_state(WIFI_STATE_DISCONNECTED);
		return err;
	}
	return ESP_OK;
}



// Налаштовує і вмикає таймер наспрацювання на певний період.
static bool wifi_manager_schedule_reconnect(uint32_t delay_ms)
{
	if(ctx.reconnect_timer == NULL)	// Чи був таймер ініціалізований
	{
		ESP_LOGE(TAG, "wifi_reconnect_timer == NULL");
		return false;
	}
	
	TickType_t delay_ticks = pdMS_TO_TICKS(delay_ms);
	if(delay_ticks == 0)
	{
		delay_ticks = 1;
	}
	
	BaseType_t status = xTimerChangePeriod(ctx.reconnect_timer, delay_ticks, 0 );  // Set period and run Timer
	if(status != pdPASS)
	{
		ESP_LOGE(TAG, "xTimerChangePeriod failed");
		return false;
	}
	return true;
}
// Колбек запускає WIFI_MANAGER_CMD_RECONNECT процедуру для перепідключення.
static void wifi_reconnect_timer_callback(TimerHandle_t timer)
{
	(void)timer;
	
	wifi_manager_message_t message = {
		.id = WIFI_MANAGER_CMD_RECONNECT
	};
	
	if(wifi_manager_send_message(&message) == false)
	{
		ESP_LOGW(MANAGER_TAG, "Failed send rRECONNECT command from timer");
	}
}



// Generet gandom part, for ctreate unicume part of tame reconnect
static uint32_t wifi_get_jitter(uint32_t max_jitter)
{
	return esp_random()%(max_jitter + 1);
}
/* Generate "dalay" depend "attempt" 
attempt    dalay
0			2s
1			4s
3			8s
4			16s
5			30s
*/ 

static uint32_t wifi_get_backoff_delay(uint32_t attempt)
{
	uint32_t delay = ctx.wifi_config.reconnect_base_delay_ms;  
	for(uint32_t i = 1; i < attempt; i++)
	{ 
		if(delay >= ctx.wifi_config.reconnect_max_delay_ms/2) 
		{
			delay = ctx.wifi_config.reconnect_max_delay_ms;
			break;
		}
		delay = delay*2;
	}
	if(delay > ctx.wifi_config.reconnect_max_delay_ms)
	{
		delay = ctx.wifi_config.reconnect_max_delay_ms;
	}
	
	return delay;
}
// Create unic random time for reconnect.
static uint32_t wifi_get_retry_delay(uint32_t attempt)
{
	uint32_t backoff = wifi_get_backoff_delay(attempt);
	uint32_t jitter = wifi_get_jitter(ctx.wifi_config.reconnect_jitter_ms);
	//ESP_LOGI("TEST RANDOM ","backoff :%" PRIu32 " ms" "jitter :%" PRIu32 "ms" , backoff, jitter);
	return backoff + jitter;
}
static bool wifi_manager_schedule_recovery(wifi_recovery_action_t action)
{
	if((action == WIFI_RECOVERY_ACTION_NONE) || (action == WIFI_RECOVERY_ACTION_STOP))
	{
		return false;
	}
	
	ctx.pending_recovery_action = action;
	uint32_t delay;
	
	// Вирахувати delay для наступного підключення до AP 
	if(ctx.reconnect_attempts >= ctx.wifi_config.max_reconnect_attempts)	// Якщо перевищено кількісь піддключень 
	{
		delay = ctx.wifi_config.reconnect_max_delay_ms + wifi_get_jitter(ctx.wifi_config.reconnect_jitter_ms);
		ESP_LOGW(MANAGER_TAG, "Enter long-term recovery. Next recovery in %" PRIu32" ms", delay);
	}
	else  // генерувати стандартну послідовність 2,4,8,16,30 
	{
		delay = wifi_get_retry_delay(ctx.reconnect_attempts + 1);
		ESP_LOGI(MANAGER_TAG, "Recovery sheduler in %" PRIu32 " ms", delay);
	}
	return wifi_manager_schedule_reconnect(delay);			// Запустити таймер на підключення до AP
}
// Повторно пробує підключається до мережі, до якої було підключення і зникло за певних причин
static void wifi_manager_reconnect_to_current_ap(void)
{
	if(ctx.state == WIFI_STATE_CONNECTING)
	{
		ESP_LOGW(TAG, "Already connecting, skip request");
		return;
	}
	if(ctx.state == WIFI_STATE_ONLINE)
	{
		ESP_LOGW(TAG, "Already online, skip request");
		return;
	}
					
	ctx.same_ap_reconnect_attempts++;
	
	ESP_LOGI(TAG, "MANAGER: Startint WiFi connection, attempt :%lu same pa attemprs :%lu", 
		(unsigned long)ctx.reconnect_attempts, (unsigned long)ctx.same_ap_reconnect_attempts);
	wifi_manager_set_state(WIFI_STATE_CONNECTING);
	
	ESP_LOGI(TAG, "MANAGER: Starting WiFi connection ...");
	
	esp_err_t err = esp_wifi_connect(); 
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "MANAGER: reconnect FAILED err: %s", esp_err_to_name(err));
		wifi_manager_set_state(WIFI_STATE_DISCONNECTED);
		
		ctx.selected_ap_valid = false;
		
		if(ctx.auto_reconnect_enabled)
		{
			if(wifi_manager_schedule_recovery(WIFI_RECOVERY_ACTION_RESCAN) == false)
			{
				ESP_LOGE(TAG, "Failed to shedule recowery");
			}
		}
	}	
}



static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	(void)arg;
	
	static const char *TAG = "WIFI_EVENT_HANDLER";
	
	if(event_base == WIFI_EVENT)
	{
		switch(event_id)
		{
			case WIFI_EVENT_STA_START:
			{
				ESP_LOGI(TAG, "EVENT: STA_STARTED");
				
				wifi_manager_message_t message = {
					.id = WIFI_MANAGER_MSG_STA_START
				};
				wifi_manager_send_message(&message);
				break;
			}
			case WIFI_EVENT_STA_CONNECTED:
			{
				ESP_LOGI(TAG, "EVENT: STA_CONNECTED");
				
				wifi_manager_message_t message = {
					.id = WIFI_MANAGER_MSG_CONNECTED
				};
				wifi_manager_send_message(&message);
				
				break;
			}
					
			case WIFI_EVENT_STA_DISCONNECTED:
			{
				ESP_LOGI(TAG, "EVENT: WIFI_EVENT_STA_DISCONNECTED");
				// Get reason of disconnect
				wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
				
				wifi_manager_message_t message = {
					.id = WIFI_MANAGER_MSG_DISCONNECTED,
					.disconnect_reason = WIFI_REASON_UNSPECIFIED
				};
				
				if(event != NULL)
				{
					message.disconnect_reason = event->reason;		// Save reason of disconnect
					
					ESP_LOGW(TAG, "STA_DISCONNECTED: reason=%d (%s)",
						message.disconnect_reason,
						wifi_disconnect_reason_to_string(message.disconnect_reason));
				}
					
				ESP_LOGW(TAG, "EVENT: STA_DISCONNECTED reason :%u" , message.disconnect_reason);
				
				wifi_manager_send_message(&message);
				break;
			}
			
			case WIFI_EVENT_SCAN_DONE:
				ESP_LOGI(TAG, "EVENT: WIFI_EVENT_SCAN_DONE");
				
				wifi_manager_message_t massege = {
					.id = WIFI_MANAGER_MSG_SCAN_DONE
				};
				wifi_manager_send_message(&massege);
				break;
			
			
			default:
			{
				break;
			}
		}
		return;
	}
	
	if(event_base == IP_EVENT)
	{
		if(event_id == IP_EVENT_STA_GOT_IP)
		{
			ip_event_got_ip_t *event = (ip_event_got_ip_t*)event_data;
			
			if(event != NULL)
			{
				ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
				ESP_LOGI(TAG, "NETMASK: " IPSTR, IP2STR(&event->ip_info.netmask));
				ESP_LOGI(TAG, "GATEWAY: " IPSTR, IP2STR(&event->ip_info.gw));
			}
			ESP_LOGI(TAG, "EVENT: GOT IP");
			
			wifi_manager_message_t message = {
				.id = WIFI_MANAGER_MSG_GOT_IP,
				};
			wifi_manager_send_message(&message);
		}
	}
}

static void wifi_manager_task(void *parameters)
{
	(void)parameters;

	wifi_manager_message_t message;
	//wifi_err_reason_t last_disconnect_reason = 0;
 
	ESP_LOGI(TAG, "Manager task started");
	
	for(;;)
	{ 
		// Waiting on the Queue from wifi_event_handler
		BaseType_t status = xQueueReceive(ctx.queue ,&message, portMAX_DELAY);
		if(status != pdPASS)
		{
			continue;
		} 
		 
		switch(message.id)
		{
			// COMMANDS FROM wifi_event_handler 
			
			case WIFI_MANAGER_MSG_STA_START:
			{
				ESP_LOGI(MANAGER_TAG, "MANAGER: STA started");
				
				if((ctx.state != WIFI_STATE_INIT) && (ctx.state != WIFI_STATE_DISCONNECTED))
				{
					ESP_LOGW(MANAGER_TAG, "STA_Start ignoreg in state %s", wifi_manager_state_to_string(ctx.state));
					break;
				}
				
				ctx.reconnect_attempts = 0;
				ctx.same_ap_reconnect_attempts = 0;
				ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
				
				if(ctx.wifi_config.auto_connect)
				{
					ESP_LOGI(MANAGER_TAG, "Auto-connect enable");
					ctx.auto_reconnect_enabled = true;
					wifi_manager_start_ap_selection();
				}
				else
				{
					ESP_LOGI(MANAGER_TAG, "Auto-connect disabled");
					ctx.auto_reconnect_enabled = false;
					wifi_manager_set_state(WIFI_STATE_DISCONNECTED);
				}
				break;
			}
			
			case WIFI_MANAGER_MSG_CONNECTED:
			{
				ESP_LOGI(MANAGER_TAG, "MANAGER: CONNECTED event");
				
				// ATA is associated with AP
				// But we don't have an IP yet
				if(ctx.state == WIFI_STATE_CONNECTING)	
				{
					wifi_manager_set_state(WIFI_STATE_CONNECTED);
				}		
				else 
				{
					ESP_LOGW(MANAGER_TAG, "CONNECTED ignored in state=%s", wifi_manager_state_to_string(ctx.state));
				}
				break;
			}

				
			case WIFI_MANAGER_MSG_DISCONNECTED:
			{
				ctx.last_disconnect_reason = message.disconnect_reason;
				ctx.last_disconnect_reason_valid = true;
				ctx.disconnect_count++;
				
				ESP_LOGW(MANAGER_TAG, "MANAGER: disconnet, reason %u (%s)", ctx.last_disconnect_reason, wifi_disconnect_reason_to_string(ctx.last_disconnect_reason));
			
				wifi_manager_set_state(WIFI_STATE_DISCONNECTED);
				
				// Manual connect
				if(ctx.auto_reconnect_enabled == false)
				{
					ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
					ESP_LOGI(MANAGER_TAG,"Autoreconnect disabled. Stay disconnected");
					break;
				}
				
				// Вибрати recowery strategy на основі причини disconnect
				wifi_recovery_action_t action = wifi_get_recovery_action(ctx.last_disconnect_reason);
				
				// Якщо driver каже, що AP більше не знайдений, попередній selected AP вважати stale.(несвіжим)
				if(ctx.last_disconnect_reason == WIFI_REASON_NO_AP_FOUND)
				{
					ctx.selected_ap_valid = false;
				}
				
				if(action == WIFI_RECOVERY_ACTION_STOP)
				{
					ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
					ESP_LOGW(MANAGER_TAG, "Recowery stopped for reason =%s", wifi_disconnect_reason_to_string(ctx.last_disconnect_reason));
					break;
				}
				
				// Defensive chack:
				// direct reconnect неможливий без valid AP
				if((action == WIFI_RECOVERY_ACTION_DIRECT_RECONNECT) && (ctx.selected_ap_valid == false))
				{
					action = WIFI_RECOVERY_ACTION_RESCAN;
				}
				
				if(action == WIFI_RECOVERY_ACTION_DIRECT_RECONNECT)
				{
					ESP_LOGI(MANAGER_TAG, "Recovery strategy: DIRECT_RECONNECT");
				}
				else if(action == WIFI_RECOVERY_ACTION_RESCAN)
				{
					ESP_LOGI(MANAGER_TAG, "Recovery strategy: RESCAN");
				}
				
				if(wifi_manager_schedule_recovery(action) == false)
				{
					ESP_LOGE(MANAGER_TAG, "Failed to chedule recovery");
				}
				
				break;
			}
			
			case WIFI_MANAGER_MSG_GOT_IP:
			{
				ESP_LOGI(MANAGER_TAG, "MANAGER: GOT_IP event");
			
				// GOT_ID is vilid after wifi connection
				if(ctx.state == WIFI_STATE_CONNECTED) 
				{
					wifi_manager_set_state(WIFI_STATE_ONLINE);
					ESP_LOGI(MANAGER_TAG, "MANAGER: network is ONLINE");
					
					ctx.reconnect_attempts = 0; 
					ctx.same_ap_reconnect_attempts = 0;
					ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
				}
				else
				{
					ESP_LOGW(MANAGER_TAG, "GOT_IP ignored in state=%s", wifi_manager_state_to_string(ctx.state));
				}
				break;
			}
			
			case WIFI_MANAGER_MSG_SCAN_DONE:  
			{
				ESP_LOGI(MANAGER_TAG, "MANAGER: WIFI_MANAGER_MSG_SCAN_DONE event");
				
				wifi_scan_purpose_t complected_scan = ctx.scan_purpose;
				ctx.scan_purpose = WIFI_SCAN_PURPOSE_NONE;
				
				if(complected_scan == WIFI_SCAN_PURPOSE_NONE)
				{
					ESP_LOGW(MANAGER_TAG, "Unexpected SCAN_DONE ignored");
					break;
				}
				
				// Показати або підключитись до знайденого AP
				esp_err_t err = wifi_manager_process_scan_results(complected_scan);
				
				// User requested scan	(Scan command from API)
				if(complected_scan == WIFI_SCAN_PURPOSE_USER)
				{
					ESP_LOGW(MANAGER_TAG, "Don't connect, only print AP list");
					if(err != ESP_OK)
					{
						ESP_LOGE(MANAGER_TAG, "User scan processing failed");
					}
					break;
				}
				
				// Scan was part of CONNECT sequence
				if(complected_scan == WIFI_SCAN_PURPOSE_CONNECT)
				{		
					if(err != ESP_OK)  // Якщо не було знайдено target AP
					{
						ESP_LOGW(MANAGER_TAG, "Target AP not found");
						
						wifi_manager_set_state(WIFI_STATE_DISCONNECTED);
						
						if(ctx.auto_reconnect_enabled == false)
						{
							ESP_LOGI(MANAGER_TAG, "Auto recovery disabled");
							break;
						}
						
						if(wifi_manager_schedule_recovery(WIFI_RECOVERY_ACTION_RESCAN) == false)
						{
							ESP_LOGE(MANAGER_TAG, "Failed to shedule RESCAN recovery");
						}
						break;
					}
					
					// Сканування пройшло іспішно, але користувач може надіслати DISCONNECT підчас сканування.
					if(ctx.auto_reconnect_enabled == false)
					{
						ESP_LOGI(MANAGER_TAG, "Connect sequence cancelled");
						ctx.selected_ap_valid = false;
						wifi_manager_set_state(WIFI_STATE_DISCONNECTED);
						break;
					}
					
					// Знайдено потрібну AP, Автоматично підключитися до BSSID
					err = wifi_manager_connect_selected_ap();
					if(err != ESP_OK)
					{
						ESP_LOGW(MANAGER_TAG, "Target AP not found");
						ctx.selected_ap_valid = false;
						
						wifi_manager_set_state(WIFI_STATE_DISCONNECTED);
						
						if(ctx.auto_reconnect_enabled == false)
						{
							ESP_LOGI(MANAGER_TAG, "Auto recovety disabled");
							break;
						}

						if(wifi_manager_schedule_recovery(WIFI_RECOVERY_ACTION_RESCAN) == false)
						{
							ESP_LOGE(MANAGER_TAG, "Failed to scedule scan recovery");
						}
						break;
					}

				}
				break;	
			}
			




			// COMMANDS FROM Outside //////////////////////////////////////////////////////////////////////////
			case WIFI_MANAGER_CMD_CONNECT:     // Команда запуску процесу сканування всіх AP
			{
				ESP_LOGI(MANAGER_TAG, "WIFI_MANAGER_SMD_CONNECT command received");

				// Підключати тільки тоді коли стан WIFI_STATE_DISCONNECTED
				if(ctx.state != WIFI_STATE_DISCONNECTED)
				{
					ESP_LOGW(MANAGER_TAG, "Connect ignored: in state %s", wifi_manager_state_to_string(ctx.state));
					break;
				}
				
				// Manual connect endbles automatic recowert again
				ctx.auto_reconnect_enabled = true;
				ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
				ctx.reconnect_attempts = 0;   
				ctx.same_ap_reconnect_attempts = 0;
				
				// Cansel pending automatic reconnect
				if(ctx.reconnect_timer != NULL)
				{
					xTimerStop(ctx.reconnect_timer, 0);
				}
				wifi_manager_start_ap_selection();
				break;	
			}
				
				
			case WIFI_MANAGER_CMD_DISCONNECT:		// Команда відключитися від AP
			{
				ESP_LOGI(MANAGER_TAG, "WIFI_MANAGER_SMD_DISCONNECT command received");
			
				ctx.auto_reconnect_enabled = false;
				ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
					
				// Cansel pending reconnect if one exists
				if(ctx.reconnect_timer != NULL)
				{
					xTimerStop(ctx.reconnect_timer, 0);
				}
				
				if(ctx.state == WIFI_STATE_SELECTING_AP)
				{
					ESP_LOGI(MANAGER_TAG, "Disconnection cansel request during AP scan");
					break;
				}
				
				if((ctx.state == WIFI_STATE_CONNECTING) || (ctx.state == WIFI_STATE_CONNECTED) ||(ctx.state == WIFI_STATE_ONLINE))
				{
					esp_err_t err = esp_wifi_disconnect();
					if(err != ESP_OK)
					{
						ESP_LOGE(MANAGER_TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(err));
					}
				}
				else if(ctx.state == WIFI_STATE_DISCONNECTED)
				{
					ESP_LOGI(MANAGER_TAG, "Already disconnected");
				}
				else
				{
					ESP_LOGW(MANAGER_TAG, "Disconnected ignored in state %s", wifi_manager_state_to_string(ctx.state));
						
				}
				break;
			}
				
			case WIFI_MANAGER_CMD_SCAN:			// Коменда простого стканування без підключення до AP
			{
				ESP_LOGI(MANAGER_TAG, "MANAGER: WIFI_MANAGER_CMD_SCAN event");
				
				if(ctx.scan_purpose != WIFI_SCAN_PURPOSE_NONE)
				{
					ESP_LOGW(MANAGER_TAG, "Scan ignored: scan already in progress");
					break;
				}
				
				// SCAN дозволени тільки коли є стан WIFI_STATE_ONLINE або WIFI_STATE_DISCONNECTED
				if((ctx.state != WIFI_STATE_ONLINE) && (ctx.state != WIFI_STATE_DISCONNECTED))
				{
					ESP_LOGW(MANAGER_TAG, "Scan ignored: in state %s", wifi_manager_state_to_string(ctx.state));
					break;
				}
				esp_err_t err = wifi_manager_start_scan(WIFI_SCAN_PURPOSE_USER);
				if(err != ESP_OK)
				{
					ESP_LOGE(MANAGER_TAG, "Failed to scan");
				}
				break;
			}
			
		
			case WIFI_MANAGER_CMD_RECONNECT:
			{
				ESP_LOGI(MANAGER_TAG, "WIFI_MANAGER_CMD_RECONNECT received");
				
				if(ctx.auto_reconnect_enabled == false)
				{
					ESP_LOGI(MANAGER_TAG, "Recovery ignored: disabled");
					ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
					break;
				}
				
				if(ctx.state != WIFI_STATE_DISCONNECTED)		// Має бути відключений від AP
				{
					ESP_LOGI(MANAGER_TAG, "Recovery ignored in state = %s", wifi_manager_state_to_string(ctx.state));
					break;
				}
			
				
				wifi_recovery_action_t action = ctx.pending_recovery_action;
				
				if((action != WIFI_RECOVERY_ACTION_NONE) && (action != WIFI_RECOVERY_ACTION_STOP))
				{
					if(ctx.reconnect_attempts < ctx.wifi_config.max_reconnect_attempts)
					{
						ctx.reconnect_attempts++;
					}
					ctx.recovery_count++;
				}
				
				
				// Поточна команда вже забрала action
				ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
				
				switch(action)
				{
					case WIFI_RECOVERY_ACTION_DIRECT_RECONNECT:
					{
						if(ctx.selected_ap_valid == false)
						{
							ESP_LOGI(MANAGER_TAG, "Selected AP invalid -> RESCAN");
							wifi_manager_start_ap_selection();
							break;
						}
						wifi_manager_reconnect_to_current_ap();
						break;
					}
							
					case WIFI_RECOVERY_ACTION_RESCAN:
					{
						ESP_LOGI(MANAGER_TAG, "Execute recovery: RESCAN");
						wifi_manager_start_ap_selection();
						break;
					}
					
					case WIFI_RECOVERY_ACTION_STOP:	
					case WIFI_RECOVERY_ACTION_NONE:
					default:
					{
						ESP_LOGW(MANAGER_TAG, "No pending recowery action");
						break;
					}
				}
				break;
			}
		}
		// Зробити публікацію цілісного статуса 
		wifi_manager_publish_status();
	}
}

static void wifi_manager_cleanup(void)
{
	if(ctx.reconnect_timer != NULL)
	{
		xTimerDelete(ctx.reconnect_timer, portMAX_DELAY);
		ctx.reconnect_timer = NULL;
	}
	
	// Delete handlers of WIFI_EVENT and IP_EVENT
	if(ctx.wifi_event_instance != NULL)
	{
		esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ctx.wifi_event_instance);
		ctx.wifi_event_instance = NULL;
	}
	if(ctx.ip_event_instance != NULL)
	{
		esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ctx.ip_event_instance);
		ctx.ip_event_instance = NULL;
	}
	
	if(ctx.task_handle != NULL)
	{
		vTaskDelete(ctx.task_handle);
		ctx.task_handle = NULL;
	}	
	if(ctx.queue != NULL)
	{
		vQueueDelete(ctx.queue);
		ctx.queue = NULL;
	}
	if(ctx.status_mutex != NULL)
	{
		vSemaphoreDelete(ctx.status_mutex);
		ctx.status_mutex = NULL;
	}

	(void)esp_wifi_stop();
	(void)esp_wifi_deinit();
	
	if(ctx.sta_netif != NULL)
	{
		esp_netif_destroy_default_wifi(ctx.sta_netif);
		ctx.sta_netif = NULL;
	}
	ctx.initialized = false;
}

esp_err_t wifi_manager_init(const wifi_manager_config_t *config)
{
	static const char *TAG = "INIT WIFI";
	ESP_LOGI(TAG, "INITIALIZING WiFi Manager");
	
	if(ctx.initialized)
	{
		ESP_LOGW(TAG, "WiFi manager already initialized");
		return ESP_ERR_INVALID_STATE;
	}
	if(wifi_manager_config_is_valid(config) == false)
	{
		ESP_LOGE(TAG, "Invalid WiFi manager configuration");
		return ESP_ERR_INVALID_ARG;
	}
	
	// Coppy apication configuration into private manager store
	wifi_manager_copy_config(config);
	
	ctx.auto_reconnect_enabled = ctx.wifi_config.auto_connect;
	ctx.reconnect_attempts = 0;
	ctx.same_ap_reconnect_attempts = 0;
	ctx.pending_recovery_action = WIFI_RECOVERY_ACTION_NONE;
	ctx.scan_purpose = WIFI_SCAN_PURPOSE_NONE;
	ctx.wifi_config.reconnect_jitter_ms = 500;
	
	ctx.selected_ap_valid = false;
	
	ctx.state = WIFI_STATE_INIT;
	
	esp_err_t err;
	
	// TCP/IP networt interface infrastructure
	err = esp_netif_init();	
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
		return err;
	}
	
	// Create default init loop
	err = esp_event_loop_create_default();	
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
		return err;
	}
	
	// Create STA network interfaace
	ctx.sta_netif = esp_netif_create_default_wifi_sta(); 
	if(ctx.sta_netif == NULL)
	{
		ESP_LOGE(TAG, "Failed to esp_netif_create_default_wifi_sta");
		return ESP_FAIL;
	}

	// Initialize WiFi
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	err = esp_wifi_init(&wifi_init_config);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to WIFI_INIT_CONFIG_DEFAULT err: %s", esp_err_to_name(err));
		goto fail;
	}
	
	// Register event handler
	err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &ctx.wifi_event_instance);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to esp_event_handler_instance_register err: %s", esp_err_to_name(err));
		goto fail;
	}
	
	err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &ctx.ip_event_instance);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to esp_event_handler_instance_register err: %s", esp_err_to_name(err));
		goto fail;
	}
	
	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "esp_wifi_set_mode failed. err: %s", esp_err_to_name(err));
		goto fail;
	}

	// Create mamager queue
	ctx.queue = xQueueCreate(WIFI_MANAGER_QUEUE_LENGTH , sizeof(wifi_manager_message_t));
	if(ctx.queue == NULL)
	{
		ESP_LOGE(TAG, "Failed to create queue");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}
	
	ctx.status_mutex = xSemaphoreCreateMutex();
	if(ctx.status_mutex == NULL)
	{
		ESP_LOGE(TAG, "Failed create status_mutex");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}
	
	// Create reconnect timer  pdFALSE - mean ONECHOT TIMER
	ctx.reconnect_timer = xTimerCreate("wifi_reconnect_timer", pdMS_TO_TICKS(1000),pdFALSE, NULL, wifi_reconnect_timer_callback);
	if(ctx.reconnect_timer == NULL)
	{
		ESP_LOGE(TAG, "Failed create reconnect timer");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}
	
	// Create manager task
	BaseType_t status = xTaskCreatePinnedToCore(wifi_manager_task, "wifi_manager_task", WIFI_MANAGER_TASK_SIZE, NULL, WIFI_MANAGER_TASK_PRIORITY, &ctx.task_handle, WIFI_MANAGER_TASK_CORE);
	if(status != pdPASS)
	{
		ESP_LOGE(TAG, "Failed create wifi_manager_task");
		err = ESP_ERR_NO_MEM;
		goto fail;
	}
	
	// Start wifi
	err = esp_wifi_start();
	if(err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed create esp_wifi_start err:%s", esp_err_to_name(err));
		goto fail;
	}
	
	ctx.initialized = true;
	
	wifi_manager_publish_status();    			// Зробити публікацію цілісного статуса 
	
	ESP_LOGI(TAG, "WiFi STA initialized");
	
	return ESP_OK;
	
	fail:
	ESP_LOGE(TAG, "WiFi manager initialization failed ! cleanup ...");
	wifi_manager_cleanup();
	
	return err;
}




