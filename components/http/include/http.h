#ifndef MAIN_HTTP_H_
#define MAIN_HTTP_H_
//-------------------------------------------------------------
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <sys/param.h>
#include <esp_http_server.h>
#include "esp_vfs.h"
//-------------------------------------------------------------
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE  4096
struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};
//-------------------------------------------------------------
//-------------------------------------------------------------
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
//-------------------------------------------------------------
#endif /* MAIN_HTTP_H_ */
