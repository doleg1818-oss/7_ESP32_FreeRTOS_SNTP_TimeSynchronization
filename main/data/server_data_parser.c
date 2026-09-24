/*
 * server_data_parser.c
 *
 *  Created on: Aug 27, 2026
 *      Author: Olegd
 */

#include "server_data_parser.h"

#include "esp_err.h"
#include "string.h"
#include "cJSON.h"
#include "esp_log.h"


static const char *TAG = "JSON PARSER";

esp_err_t server_data_parse_json(const char *json, server_data_t *data)
{
	if((json == NULL) || (data == NULL))
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	cJSON *root = cJSON_Parse(json);
	if(root == NULL)
	{
		ESP_LOGE(TAG, "Falue to prepare JSON");
		return ESP_ERR_INVALID_RESPONSE;
	}
	
	if(cJSON_IsObject(root) == false)
	{
		ESP_LOGE(TAG, "JSON root is not an object");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;
	}
	
	cJSON *temperature = cJSON_GetObjectItemCaseSensitive(root, "temperature");
	cJSON *humidity = cJSON_GetObjectItemCaseSensitive(root, "humidity");
	cJSON *pressure = cJSON_GetObjectItemCaseSensitive(root, "pressure");
	cJSON *alarm = cJSON_GetObjectItemCaseSensitive(root, "alarm");
	cJSON *city = cJSON_GetObjectItemCaseSensitive(root, "city");
	
	if((temperature == NULL) || (humidity == NULL) || (pressure == NULL) || (alarm == NULL) || (city == NULL))
	{
		ESP_LOGE(TAG, "Reauired JSON filed is missing");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;
	}
	
	
	if(cJSON_IsNumber(temperature) == false)
	{
		ESP_LOGE(TAG, "temperature is not a number");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;	
	}
	
	if(cJSON_IsNumber(humidity) == false)
	{
		ESP_LOGE(TAG, "humidity is not a number");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;	
	}
	
	if(cJSON_IsNumber(pressure) == false)
	{
		ESP_LOGE(TAG, "pressure is not a number");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;	
	}
	
	if(cJSON_IsBool(alarm) == false)
	{
		ESP_LOGE(TAG, "alarm is not a alarm");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;	
	}
	
	if((cJSON_IsString(city) == false) || (city->valuestring == NULL))
	{
		ESP_LOGE(TAG, "ciry is not a valid string");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;	
	}
	
	// Domain/range validation
	if(temperature->valuedouble < -100.0 || temperature->valuedouble > 100.0)
	{
		ESP_LOGE(TAG, "Temperature out of range");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;	
	}
	if((humidity->valuedouble < 0.0) || (humidity->valuedouble > 100.0))
	{
		ESP_LOGE(TAG, "Humidity out of range");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;	
	}
	if((pressure->valuedouble < 300.0) || (pressure->valuedouble > 1200.0))
	{
		ESP_LOGE(TAG, "Pressure out of range");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;	
	}
	
	if(strlen(city->valuestring) >= SERVER_DATA_СITY_SIZE)
	{
		ESP_LOGE(TAG, "Sity string is to long");
		cJSON_Delete(root);
		return ESP_ERR_INVALID_RESPONSE;
	}
	
	
	data->temperature = (float)temperature->valuedouble;
	data->humidity = (uint8_t)humidity->valuedouble;
	data->pressure = (uint16_t)pressure->valuedouble;
	data->alarm = cJSON_IsTrue(alarm);
	strlcpy(data->city, city->valuestring, sizeof(data->city));
	
	cJSON_Delete(root);

	return ESP_OK;
}

