/*
 * wifi_manager_task.h
 *
 *  Created on: Aug 24, 2026
 *      Author: Olegd
 */

#ifndef MAIN_WIFI_WIFI_MANAGER_TEST_H_
#define MAIN_WIFI_WIFI_MANAGER_TEST_H_

#include "esp_err.h"

void wifi_manager_test_print_status(void);

esp_err_t wifi_manager_start_test_matrix(void);
esp_err_t wifi_manager_start_status_reader(void);

#endif /* MAIN_WIFI_WIFI_MANAGER_TEST_H_ */
