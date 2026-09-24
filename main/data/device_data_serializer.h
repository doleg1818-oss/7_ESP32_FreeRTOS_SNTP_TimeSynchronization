/*
 * device_data_serializer.h
 *
 *  Created on: Aug 27, 2026
 *      Author: Olegd
 */

#ifndef MAIN_DATA_DEVICE_DATA_SERIALIZER_H_
#define MAIN_DATA_DEVICE_DATA_SERIALIZER_H_

#include "stdbool.h"
#include "stdint.h"
#include "esp_err.h"

typedef struct{
	uint32_t message_id;
	uint32_t device_id;
	
	int64_t timestamp;
	
	float temperature;
	uint8_t humidity;
	float battery_voltage;
	bool alarm;
}device_data_t;

esp_err_t device_data_serialize_json(const device_data_t *data, char **json_out);
void device_data_free_json(char *json);

#endif /* MAIN_DATA_DEVICE_DATA_SERIALIZER_H_ */
