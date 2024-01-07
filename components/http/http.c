#include "http.h"
#include "wifi.h"
#include "esp_timer.h"
#include "esp_spiffs.h"

//-------------------------------------------------------------
static const char *TAG = "http";
//-------------------------------------------------------------
#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)
//-------------------------------------------------------------
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    else if(IS_FILE_EXT(filename, ".js"))
    {
        return httpd_resp_set_type(req, "test/javascript");
    }
    else if(IS_FILE_EXT(filename, ".css"))
    {
            return httpd_resp_set_type(req, "test/css");
    }
	else if (IS_FILE_EXT(filename, ".wav"))
	{
		return httpd_resp_set_type(req, "audio/wav");
	}
    return httpd_resp_set_type(req, "text/plain");
}
//-------------------------------------------------------------
static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
  const size_t base_pathlen = strlen(base_path);
  size_t pathlen = strlen(uri);
  const char *quest = strchr(uri, '?');
  if (quest) {
      pathlen = MIN(pathlen, quest - uri);
  }
  const char *hash = strchr(uri, '#');
  if (hash) {
      pathlen = MIN(pathlen, hash - uri);
  }
  if (base_pathlen + pathlen + 1 > destsize) {
    return NULL;
  }
  strcpy(dest, base_path);
  strlcpy(dest + base_pathlen, uri, pathlen + 1);
  return dest + base_pathlen;
}


const char dirpath[] = "/sdcard/esp_wav";

const char index_header[] = "<!DOCTYPE html> \
<html lang='en' class=''>\
<head>\
	<meta charset='utf-8'>\
	<meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no' />\
	<title>ESP-ShareDisk</title>\
	<style>\
		div {border: greenyellow solid 0px; color: white; text-align: center;}\
		fieldset {background: #4f4f4f;}\
		body {\
			text-align: center;\
			font-family: verdana, sans-serif;\
			font-size: large;\
			background: #252525;\
		}\
		td {\
			padding: 0px;\
		}\
		a {\
			color: #1fa3ec;\
			text-decoration: none;\
		}\
		.commandform {\
			margin-bottom: 10px;\
		}\
		.button {\
			border: 0;\
			border-radius: 0.3rem;\
			color: #faffff;\
			line-height: 2.4rem;\
			font-size: 1.2rem;\
			width: 100%;\
			-webkit-transition-duration: 0.4s;\
			transition-duration: 0.4s;\
			cursor: pointer;\
		}\
		.b-blue {\
			background: #1fa3ec;\
		}\
		.b-blue:hover {\
			background: #0e70a4;\
		}\
		.b-red {\
			background: #d43535;\
		}\
		.b-red:hover {\
			background: #931f1f;\
		}\
		#content {\
			text-align: left;\
			display: inline-block;\
			color: #eaeaea;\
			min-width: 340px;\
		}\
		#sdcardnotavailablebox {\
			padding: 20px;\
			background-color: #E09900;\
			text-align: center;\
		}\
		#sdcardfilesbox {\
			text-align: center;\
		}\
		#filestable {\
			width: 100%;\
			border-collapse: collapse;\
		}\
		#filestable td,\
		#filestable th {\
			border: 1px solid gray;\
		}\
	</style>\
</head>\
<body>\
	<div id='content'>\
		<div>\
			<h1>GooDee</h1>\
		</div>\
		<div style='<% sdFilesStyles %>'>\
			<table id='filestable'>\
		<tr>\
			<th>Name</th>\
			<th>Size (kB)</th>\
			<th>Action</th>\
		</tr>";

const char index_tail[] = "</table>\
		<div>\
			<h3>Commands:</h3>\
		</div>\
		<div>\
			<form action='reboot' method='get' class='commandform'><button class='button b-red'>Reboot</button></form>\
		</div>\
	</div>\
</body>\
</html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;
    FILE *fd = NULL;
    char filepath[64];
    char filename[64];

    filepath[0] = 0;

   //  strcpy(filepath, ((struct file_server_data *)req->user_ctx)->base_path);
   //  strcat(filepath, "/");
   //  strcat(filepath, quest+1);
     strcpy(filepath, "/spiffs");
     strcat(filepath, req->uri);
     strcpy(filename, req->uri);

    printf("index req %s\n", filepath);

    if(strcmp(filepath, "/spiffs/") == 0)
    {
    	strcat(filepath, "index_l.html");
    	strcpy(filename, "index_l.html");

    	printf("index c req %s\n", filepath);
    }

    //fd = fopen("/spiffs/index.html", "r");
    fd = fopen(filepath, "r");

	if (!fd)
	{
	  ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
	  /* Respond with 500 Internal Server Error */
	  httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
	  return ESP_FAIL;
	}

	set_content_type_from_file(req, filename);
	httpd_resp_set_hdr(req, "Connection", "close");

	char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
	size_t chunksize;
	do {
	chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

	if (chunksize > 0)
	{
	  if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
	  {
		fclose(fd);
		ESP_LOGE(TAG, "File sending failed!");
		httpd_resp_sendstr_chunk(req, NULL);
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
		return ESP_FAIL;
	  }
	}
	} while (chunksize != 0);

	fclose(fd);
	ESP_LOGI(TAG, "File sending complete");
	httpd_resp_send_chunk(req, NULL, 0);

#if 0
    struct dirent *entry;
    struct stat entry_stat;

    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return ESP_FAIL;
    }

    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, index_header);

/*
	<tr>
	<td>fileName</td>
	<td>fileEntry</td>
	<td><a href="download?file=" download="rec_0.wav">💾</a></td>
	</tr>
	*/

    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL) {
        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

		if(entry->d_type == DT_DIR)
			continue;

		strlcpy(entrypath, dirpath, sizeof(entrypath));
		strcat(entrypath, "/");
		strcat(entrypath, entry->d_name);

        if (stat(entrypath, &entry_stat) == -1) {
            ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
            continue;
        }
        sprintf(entrysize, "%ld", entry_stat.st_size);
        ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);

        /* Send chunk of HTML file containing table entries with file name and size */
        httpd_resp_sendstr_chunk(req, "<tr><td>");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, entrysize);
        httpd_resp_sendstr_chunk(req, "</td><td><a href='download?");
        httpd_resp_sendstr_chunk(req, entrypath);
        httpd_resp_sendstr_chunk(req, "' download='");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "'>💾</a></td></tr>\r\n");
    }
    closedir(dir);

    httpd_resp_sendstr_chunk(req, index_tail);
#endif
    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

//-------------------------------------------------------------
static esp_err_t index_handler2(httpd_req_t *req)
{
  char filepath[FILE_PATH_MAX];
  FILE *fd = NULL;
  struct stat file_stat;
  uint32_t time_1;
  uint32_t time_2;



  //const char *filename = 0;
  filepath[0] = 0;

  strcpy(filepath, ((struct file_server_data *)req->user_ctx)->base_path);

  strcat(filepath, "/index.html");

  stat(filepath, &file_stat);

  fd = fopen(filepath, "r");

  if (!fd)
  {
      ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
      /* Respond with 500 Internal Server Error */
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
      return ESP_FAIL;
  }

  httpd_resp_set_type(req, "text/html");

  time_1 = esp_timer_get_time();

  char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
  size_t chunksize;
  do {
    chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

    if (chunksize > 0)
    {
      time_1 = esp_timer_get_time();
      if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
      {
        fclose(fd);
        ESP_LOGE(TAG, "File sending failed!");
        httpd_resp_sendstr_chunk(req, NULL);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
        return ESP_FAIL;
      }
    }
  } while (chunksize != 0);

  time_2 = esp_timer_get_time();

  fclose(fd);
  ESP_LOGI(TAG, "File sending complete");
  httpd_resp_send_chunk(req, NULL, 0);


  ESP_LOGI(TAG, "responce %d us", time_2-time_1);

  return ESP_OK;
}

//-------------------------------------------------------------
static esp_err_t download_get_handler(httpd_req_t *req)
{
  char filepath[FILE_PATH_MAX];
  FILE *fd = NULL;
  struct stat file_stat;
  uint32_t time_1;
  uint32_t time_2;
  char len[64];
//  const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
//                                             req->uri, sizeof(filepath));

  const char *quest = strchr(req->uri, '?');

  filepath[0] = 0;

//  strcpy(filepath, ((struct file_server_data *)req->user_ctx)->base_path);
//  strcat(filepath, "/");
//  strcat(filepath, quest+1);
  strcpy(filepath, "/sdcard/esp_wav/");
  strcat(filepath, quest+1);

  //strcpy(filepath, quest+1);

  ESP_LOGI(TAG, "get file: %s", filepath);

//  if (!filename) {
//      ESP_LOGE(TAG, "Filename is too long");
//      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
//      return ESP_FAIL;
//  }
//  if (strcmp(filename,"/") == 0) {
//    strcat(filepath, "index.html");
//  }

  stat(filepath, &file_stat);
  fd = fopen(filepath, "r");

  if (!fd)
  {
      ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
      /* Respond with 500 Internal Server Error */
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
      return ESP_FAIL;
  }

  //httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_type(req, "audio/wav");

  sprintf(len, "%ld", file_stat.st_size);
  httpd_resp_set_hdr(req, "Content-Length", len);
  httpd_resp_set_hdr(req, "Connection", "close");

//  ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
//  set_content_type_from_file(req, filename);



  char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
  size_t chunksize;
  do {
    chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

    //time_1 = esp_timer_get_time();

    if (chunksize > 0)
    {

      if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
      {
        fclose(fd);
        ESP_LOGE(TAG, "File sending failed!");
        httpd_resp_sendstr_chunk(req, NULL);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
        return ESP_FAIL;
      }
    }
    //time_2 = esp_timer_get_time();
    //ESP_LOGI(TAG, "fread takes %u us", time_2-time_1);

  } while (chunksize != 0);


  fclose(fd);
  ESP_LOGI(TAG, "File sending complete");
  httpd_resp_send_chunk(req, NULL, 0);


  return ESP_OK;
}

//-------------------------------------------------------------
static esp_err_t reboot_handler(httpd_req_t *req)
{
    const char resp[] = "esp reboot...";

    httpd_resp_set_type(req, "text/html");

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

//    ESP_LOGI(TAG, "disconnect...");
//    esp_wifi_disconnect();
//
//    net_stop();



    //esp_restart();

	return ESP_OK;
}

//-------------------------------------------------------------
static esp_err_t post_handler(httpd_req_t *req)
{
    const char resp[] = "ok";

    printf("post %s\n", req->uri);

    httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_set_type(req, "text/html");
	httpd_resp_set_hdr(req, "Connection", "close");

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

	return ESP_OK;
}

static struct file_server_data *server_data = NULL;

//-------------------------------------------------------------
httpd_handle_t start_webserver(void)
{
	httpd_handle_t server = NULL;

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	//static struct file_server_data *server_data = NULL;
	if (server_data) {
	  ESP_LOGE(TAG, "File server already started");
	  return ESP_ERR_INVALID_STATE;
	}
	server_data = calloc(1, sizeof(struct file_server_data));
	if (!server_data) {
	  ESP_LOGE(TAG, "Failed to allocate memory for server data");
	  return ESP_ERR_NO_MEM;
	}
	strlcpy(server_data->base_path, "/sdcard",
		   sizeof(server_data->base_path));
	config.uri_match_fn = httpd_uri_match_wildcard;

	config.lru_purge_enable = true;

	ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK)
    {
      ESP_LOGI(TAG, "Error starting server!");
      return NULL;
    }

    ESP_LOGI(TAG, "Registering URI handlers");

    httpd_uri_t index_html = {
        .uri       = "/",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = server_data    // Pass server data as context
    };

    httpd_register_uri_handler(server, &index_html);

    httpd_uri_t file_download = {
        .uri       = "/download",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = download_get_handler,
        .user_ctx  = server_data    // Pass server data as context
    };

    httpd_register_uri_handler(server, &file_download);

    httpd_uri_t reboot = {
        .uri       = "/reboot",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = reboot_handler,
        .user_ctx  = NULL    // Pass server data as context
    };

    httpd_register_uri_handler(server, &reboot);



    httpd_uri_t post_process = {
        .uri       = "/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_POST,
        .handler   = post_handler,
        .user_ctx  = NULL    // Pass server data as context
    };

    httpd_register_uri_handler(server, &post_process);

    return server;
}
//-------------------------------------------------------------
void stop_webserver(httpd_handle_t server)
{
	esp_err_t err;

    err = httpd_stop(server);
    ESP_LOGI(TAG, "httpd stop ret = %d", err);

    free(server_data);
    server_data = NULL;
}
//-------------------------------------------------------------
