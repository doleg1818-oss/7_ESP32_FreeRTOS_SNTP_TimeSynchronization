/*
 * device_data_serializer.c
 *
 *  Created on: Aug 27, 2026
 *      Author: Olegd
 */

#include "device_data_serializer.h"

#include "cJSON.h"


esp_err_t device_data_serialize_json(const device_data_t *data, char **json_out)
{
	if((data == NULL) || (json_out == NULL))
	{
		return ESP_ERR_INVALID_ARG;
	}
	
	*json_out = NULL;
	
	cJSON *root = cJSON_CreateObject();
	if(root == NULL)
	{
		return ESP_ERR_NO_MEM;
	}
	
	
	if(cJSON_AddNumberToObject(root, "message_id", data->message_id) == NULL)
	{
		cJSON_Delete(root);
		return ESP_ERR_NO_MEM;
	}
	if(cJSON_AddNumberToObject(root, "device_id", data->device_id) == NULL)
	{
		cJSON_Delete(root);
		return ESP_ERR_NO_MEM;
	}

	if(cJSON_AddNumberToObject(root, "timestamp", data->timestamp) == NULL)
	{
		cJSON_Delete(root);
		return ESP_ERR_NO_MEM;
	}
	
	if(cJSON_AddNumberToObject(root, "temperature", data->temperature) == NULL)
	{
		cJSON_Delete(root);
		return ESP_ERR_NO_MEM;
	}
	if(cJSON_AddNumberToObject(root, "humidity", data->humidity) == NULL)
	{
		cJSON_Delete(root);
		return ESP_ERR_NO_MEM;
	}
	if(cJSON_AddNumberToObject(root, "battery_voltage", data->battery_voltage) == NULL)
	{
		cJSON_Delete(root);
		return ESP_ERR_NO_MEM;
	}
	if(cJSON_AddBoolToObject(root, "alarm", data->alarm) == NULL)
	{
		cJSON_Delete(root);
		return ESP_ERR_NO_MEM;
	}
	
	char *json = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	
	if(json == NULL)
	{
		return ESP_ERR_NO_MEM;
	}
	
	*json_out = json;
	
	return ESP_OK;
}

void device_data_free_json(char *json)
{
	if(json != NULL)
	{
		cJSON_free(json);
	}
}



