/*
 * server_data_parser.h
 *
 *  Created on: Aug 27, 2026
 *      Author: Olegd
 */

#ifndef MAIN_DATA_SERVER_DATA_PARSER_H_
#define MAIN_DATA_SERVER_DATA_PARSER_H_


#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SERVER_DATA_СITY_SIZE 	32U

typedef struct{
	float temperature;
	uint8_t humidity;
	uint16_t pressure;
	bool alarm;
	char city[SERVER_DATA_СITY_SIZE];
}server_data_t;

esp_err_t server_data_parse_json(const char *json, server_data_t *data);






#endif /* MAIN_DATA_SERVER_DATA_PARSER_H_ */
