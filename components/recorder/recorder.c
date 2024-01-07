
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/time.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// drivers
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"

//common configs
#include "sdkconfig.h"

#include "recorder.h"


//private defines --------------------------------------------------------------
static const char* TAG = "recorder";


#define NUM_CHANNELS        (1) // For mono recording only!
#define SD_MOUNT_POINT      "/sdcard"
#define SAMPLE_SIZE         (CONFIG_BIT_SAMPLE * 1024)
#define BYTE_RATE           (CONFIG_SAMPLE_RATE * (CONFIG_BIT_SAMPLE / 8)) * NUM_CHANNELS
#define WAVE_HEADER_SIZE 	44

typedef struct wav_header_ {
    // RIFF Header
    char riff_header[4]; // Contains "RIFF"
    uint32_t wav_size; // Size of the wav portion of the file, which follows the first 8 bytes. File size - 8
    char wave_header[4]; // Contains "WAVE"

    // Format Header
    char fmt_header[4]; // Contains "fmt " (includes trailing space)
    uint32_t fmt_chunk_size; // Should be 16 for PCM
    uint16_t audio_format; // Should be 1 for PCM. 3 for IEEE Float
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate; // Number of bytes per second. sample_rate * num_channels * Bytes Per Sample
    uint16_t sample_alignment; // num_channels * Bytes Per Sample
    uint16_t bit_depth; // Number of bits per sample

    // Data
    char data_header[4]; // Contains "data"
    uint32_t data_bytes; // Number of bytes in data. Number of samples * num_channels * sample byte size
} wav_header_t;

// driver configurations -------------------------------------------------------
// Set the I2S configuration as PDM and 16bits per sample
const i2s_config_t i2s_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_RX,// | I2S_MODE_PDM,
    .sample_rate = CONFIG_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = 0,
};

// Set the pinout configuration (set using menuconfig)
const i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = 27,
    .ws_io_num = 14,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = 16,
};

//private variables ------------------------------------------------------------
sdmmc_host_t host = SDSPI_HOST_DEFAULT();
sdmmc_card_t* card;

static int16_t i2s_readraw_buff[SAMPLE_SIZE];

FILE* f_rec;
uint32_t max_rec_wav_size;
uint32_t rec_bytes;
uint32_t bytes_read;
recorder_state_t rec_state = RECORDER_STATE_NO_INIT;

QueueHandle_t rec_mailbox;

//private functions ------------------------------------------------------------
esp_err_t recorder_MountSd(void)
{
    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 8 * 1024
    };

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_SPI_MISO_GPIO,
        .sclk_io_num = CONFIG_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ESP_LOGI(TAG, "Initializing SD card");
    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SPI_CS_GPIO;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }

        return ret;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}


void recorder_InitMicrophone(void)
{
    // Call driver installation function before any I2S R/W operation.
    ESP_ERROR_CHECK( i2s_driver_install(CONFIG_I2S_CH, &i2s_config, 0, NULL) );
    ESP_ERROR_CHECK( i2s_set_pin(CONFIG_I2S_CH, &pin_config) );
    ESP_ERROR_CHECK( i2s_set_clk(CONFIG_I2S_CH, CONFIG_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO) );
}

void recorder_DeinitMicrophone(void)
{
	ESP_ERROR_CHECK( i2s_driver_uninstall(CONFIG_I2S_CH) );
}

esp_err_t recorder_StartRecord(const char * path, uint32_t max_length)
{
	time_t t = time(NULL);
	struct tm tmr;
	struct stat st;
    char buf[64];
    char filename_str[64] = {0};
    wav_header_t wav_header;

//    if(rec_state == RECORDER_STATE_RECORD)
//    {
//    	return ESP_FAIL;
//    }

	localtime_r(&t, &tmr);

    //strcpy(filename_str, "/sdcard/esp_wav/");
	strcpy(filename_str, path);
    sprintf(buf, "/%02d%02d%02d", (tmr.tm_year - 100), tmr.tm_mon + 1, tmr.tm_mday);
    strcat(filename_str, buf);
    sprintf(buf, "_%02d%02d%02d", tmr.tm_hour, tmr.tm_min, tmr.tm_sec);
    strcat(filename_str, buf);
    strcat(filename_str, ".wav");

    // если такой уже есть, надо удалить (пока так)
	if (stat(filename_str, &st) == 0)
	{
		unlink(filename_str);
	}

	f_rec = fopen(filename_str, "w+");

	if (f_rec == NULL)
	{
		ESP_LOGE(TAG, "Failed to open file for writing");

		return ESP_FAIL;
	}

	// считаем максимальную длину записи в байтах (аргумент в секундах)
	max_rec_wav_size =  BYTE_RATE*max_length;
	rec_bytes = 0;

	// пишем правильные буквы в wav заголовок
    memcpy(wav_header.riff_header, "RIFF", 4);

    wav_header.wav_size = max_rec_wav_size + WAVE_HEADER_SIZE - 8;

    memcpy(wav_header.wave_header, "WAVE", 4);
    memcpy(wav_header.fmt_header, "fmt ", 4);

    wav_header.fmt_chunk_size = 16;
    wav_header.audio_format = 1;
    wav_header.num_channels = 1;
    wav_header.sample_rate = CONFIG_SAMPLE_RATE;
    wav_header.byte_rate = BYTE_RATE;
    wav_header.sample_alignment = 2;
    wav_header.bit_depth = 16;

    memcpy(wav_header.data_header, "data", 4);
    wav_header.data_bytes = max_rec_wav_size;

    fseek(f_rec, 0, SEEK_SET);
    fwrite(&wav_header, 1, WAVE_HEADER_SIZE, f_rec);

    rec_state = RECORDER_STATE_RECORD;

	ESP_LOGI(TAG, "start record to %s", filename_str);

	return ESP_OK;
}


esp_err_t recorder_StopRecord(void)
{
	wav_header_t wav_header;

    if(rec_state == RECORDER_STATE_IDLE)
    	return ESP_FAIL;

	// пишем правильные буквы в wav заголовок
    memcpy(wav_header.riff_header, "RIFF", 4);

    wav_header.wav_size = rec_bytes + WAVE_HEADER_SIZE - 8;

    memcpy(wav_header.wave_header, "WAVE", 4);
    memcpy(wav_header.fmt_header, "fmt ", 4);

    wav_header.fmt_chunk_size = 16;
    wav_header.audio_format = 1;
    wav_header.num_channels = 1;
    wav_header.sample_rate = CONFIG_SAMPLE_RATE;
    wav_header.byte_rate = BYTE_RATE;
    wav_header.sample_alignment = 2;
    wav_header.bit_depth = 16;

    memcpy(wav_header.data_header, "data", 4);
    wav_header.data_bytes = rec_bytes;

    fseek(f_rec, 0, SEEK_SET);
    fwrite(&wav_header, 1, WAVE_HEADER_SIZE, f_rec);
    fclose(f_rec);

    rec_state = RECORDER_STATE_IDLE;

    ESP_LOGI(TAG, "file written (%u) bytes", rec_bytes);

	return ESP_OK;
}

void recorder_Process(void)
{
	i2s_read(CONFIG_I2S_CH, (char *)i2s_readraw_buff, 4096, &bytes_read, 100);

	gpio_set_level(GPIO_NUM_2, 1);
	fwrite(i2s_readraw_buff, 1, bytes_read, f_rec);
	gpio_set_level(GPIO_NUM_2, 0);

	rec_bytes += bytes_read;

	if(rec_bytes >= max_rec_wav_size)
	{
		// завершаем текущую записm
		recorder_StopRecord();

		// продолжаем писать дальше
		recorder_StartRecord("/sdcard/esp_wav", 120);
	}
}

//task ctrl =====================================================================
void recorder_Start(void)
{
	uint8_t cmd = RECORDER_START;
	xQueueSend(rec_mailbox, &cmd, 0);
}

void recorder_Stop(void)
{
	uint8_t cmd = RECORDER_STOP;
	xQueueSend(rec_mailbox, &cmd, 0);
}

recorder_state_t recorder_GetState(void)
{
	return rec_state;
}

//task =========================================================================
void recorder_Task(void *args)
{
	uint8_t cmd;
	TickType_t rec_wait;

	recorder_MountSd();

	rec_mailbox = xQueueCreate(2, sizeof(uint8_t));

	//list_files();
	rec_state = RECORDER_STATE_IDLE;
	rec_wait = portMAX_DELAY;
	while(1)
	{
		// ждем команд
		if (xQueueReceive(rec_mailbox, &cmd, rec_wait))
		{
			switch(cmd)
			{
				case RECORDER_START:
					if(rec_state == RECORDER_STATE_IDLE)
					{
						recorder_InitMicrophone();

						if(recorder_StartRecord("/sdcard/esp_wav", 900) == ESP_OK)
						{
							rec_wait = 0;
						}
						else
						{
							recorder_DeinitMicrophone();
							rec_wait = portMAX_DELAY;
						}
					}
					break;

				case RECORDER_STOP:
					if(recorder_StopRecord() == ESP_OK)
					{
						recorder_DeinitMicrophone();
						rec_wait = portMAX_DELAY;
					}
					break;
			}
		}

		if(rec_state == RECORDER_STATE_RECORD)
			recorder_Process();
	}
}
