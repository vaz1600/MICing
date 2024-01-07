#ifndef MAIN_WIFI_H_
#define MAIN_WIFI_H_
//-------------------------------------------------------------
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"

//-------------------------------------------------------------
esp_err_t wifi_init_sta(void);
esp_err_t wifi_init_ap(void);
esp_err_t wifi_deinit_ap(void);

void net_stop(void);

//-------------------------------------------------------------
#endif /* MAIN_WIFI_H_ */
