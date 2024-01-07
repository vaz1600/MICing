/* I2S Digital Microphone Recording Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include <sys/stat.h>
//#include "esp_heap_task_info.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "nvs_flash.h"

#include "sdkconfig.h"
#include "main.h"
#include "recorder.h"
#include "wifi.h"
#include "http.h"
#include "lsm_303.h"

static const char* TAG = "main";

httpd_handle_t server = NULL;

// ----------------------------------------------------------
static void dump_heap_info(void)
{
	printf("------------------------\n");
	printf("Heap free size: %u bytes\n", xPortGetFreeHeapSize());
	printf("Heap low watermark: %u bytes\n", xPortGetMinimumEverFreeHeapSize());
	printf("------------------------\n");
}

//static void esp_dump_per_task_heap_info(void);

void accel_task(void *args)
{
	uint8_t click_count = 0;
	uint8_t click_detected = 0;
	uint8_t latency = 0;
	uint8_t phase = 0;
	int16_t ax;
	int16_t ay;
	int16_t az;
	int16_t check = 10000;
	uint8_t clicks = 0;
	esp_err_t ret;

	LSM303_init();

	while(1)
	{
		vTaskDelay(15 / portTICK_PERIOD_MS);

		LSM303_read(&ax, &ay, &az);

		// определяем клик
		if (az < -20000)
		{
			click_count++;
		}
		else
		{
			if (click_count > 1)
			{
				click_count = 0;
				click_detected = 1;
			}

			if(click_count > 0)
				click_count--;
		}

		// тут считаем количество кликов подряд
		switch(phase)
		{
			// idle
			case 0:
				if(click_detected == 1)
				{
					click_detected = 0;
					phase = 1;
					latency = 0;
					check += 1000;

					clicks++;
				}
				break;

			// wait for next click
			case 1:
				if(click_detected == 1)
				{
					click_detected = 0;

					clicks++;
					latency = 0;

					check += 1000;
				}
				else
				{
					if(latency > 50)
					{
						phase = 0;
						latency = 0;

						check = 10000;
						printf("clicks %d\n", clicks);

						if(clicks == 1)
							dump_heap_info();
						else if(clicks == 2)
							recorder_Start();
						else if(clicks == 3)
							recorder_Stop();
						else if(clicks == 4)
						{
							ret = esp_netif_init();
							ESP_LOGI(TAG, "esp_netif_init: %d", ret);

							ret = esp_event_loop_create_default();
							ESP_LOGI(TAG, "esp_event_loop_create_default: %d", ret);

							ret = wifi_init_ap();
							ESP_LOGI(TAG, "wifi_init_ap: %d", ret);

							if(ret == ESP_OK)
							{
								ESP_LOGI(TAG, "Starting webserver");
								server = start_webserver();
							}
						}
						else if(clicks == 5)
						{
							stop_webserver(server);
							ESP_LOGI(TAG, "webserver stop");

							wifi_deinit_ap();
							ret = esp_event_loop_delete_default();
							ESP_LOGI(TAG, "esp_event_loop_delete_default: %d", ret);
							ret = esp_netif_deinit();
							ESP_LOGI(TAG, "esp_netif_deinit: %d", ret);
						}
						clicks = 0;
					}
					else
						latency++;
				}
				break;
		}
	}
}



void app_main(void)
{
    time_t t = time(NULL);
    struct tm tmr;

    //Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ret = nvs_flash_erase();
		ESP_LOGI(TAG, "nvs_flash_erase: 0x%04x", ret);
		ret = nvs_flash_init();
		ESP_LOGI(TAG, "nvs_flash_init: 0x%04x", ret);
	}
	ESP_LOGI(TAG, "nvs_flash_init: 0x%04x", ret);

	// если время не установлено (0), надо задать какое-то значение
	// так как ESP в интернетик не ходит
	t = time(NULL);
	localtime_r(&t, &tmr);

	if(t == 0)
	{
		tmr.tm_year = 123; //Количество лет с 1900 года.
		tmr.tm_mon = 11;
		tmr.tm_mday = 29;
		tmr.tm_hour = 9;
		tmr.tm_min = 43;
		tmr.tm_sec = 2;
		t = mktime(&tmr);

		struct timeval now = { .tv_sec = t };
		settimeofday(&now, NULL);
	}
    // если писать что-то вроде 'set systime', то почему-то ломается Arduino IDE
    // и драйвера COM-пота
    ESP_LOGI(TAG, "ESP time: %s", asctime(&tmr));

    // инициализация портов
    gpio_set_direction(CONFIG_LED_GPIO, GPIO_MODE_OUTPUT); // led
    gpio_set_direction(GPIO_NUM_25, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
 	 .base_path = "/spiffs",
 	 .partition_label = NULL,
 	 .max_files = 5,
 	 .format_if_mount_failed = true
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
 	   if (ret == ESP_FAIL)
 	   {
 		   ESP_LOGE(TAG, "Failed to mount or format filesystem");
 	   }
 	   else if (ret == ESP_ERR_NOT_FOUND)
 	   {
 		   ESP_LOGE(TAG, "Failed to find SPIFFS partition");
 	   }
 	   else
 	   {
 		   ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
 	   }
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
 	   ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
 	   ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    xTaskCreate(&recorder_Task, "rec_Task", 10240, NULL, 5, NULL);

    vTaskDelay(150 / portTICK_PERIOD_MS);


    // wi-fi
    ret = esp_netif_init();
    ESP_LOGI(TAG, "esp_netif_init: %d", ret);

    ret = esp_event_loop_create_default();
    ESP_LOGI(TAG, "esp_event_loop_create_default: %d", ret);

    //ret = wifi_init_ap();
    ret = wifi_init_sta();
    ESP_LOGI(TAG, "wifi_init_ap: %d", ret);

    if(ret == ESP_OK)
    {
    	ESP_LOGI(TAG, "Starting webserver");
    	server = start_webserver();
    }
    // контроль напряжения аккумулятора?

    accel_task(0);
//    stop_webserver(&server);
//    wifi_deinit_ap();
//    ret = esp_event_loop_delete_default();
//    ESP_LOGI(TAG, "esp_event_loop_delete_default: %d", ret);
//    ret = esp_netif_deinit();
//    ESP_LOGI(TAG, "esp_netif_init: %d", ret);
}
