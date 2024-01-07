#include "wifi.h"
#include <esp_http_server.h>
#include "http.h"

//-------------------------------------------------------------
#if CONFIG_WIFI_SCAN_METHOD_FAST
#define WIFI_SCAN_METHOD WIFI_FAST_SCAN
#elif CONFIG_WIFI_SCAN_METHOD_ALL_CHANNEL
#define WIFI_SCAN_METHOD WIFI_ALL_CHANNEL_SCAN
#endif
//-------------------------------------------------------------
#if CONFIG_WIFI_CONNECT_AP_BY_SIGNAL
#define WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#elif CONFIG_WIFI_CONNECT_AP_BY_SECURITY
#define WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SECURITY
#endif
//-------------------------------------------------------------
#if CONFIG_WIFI_AUTH_OPEN
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_WIFI_AUTH_WEP
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_WIFI_AUTH_WPA_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_WIFI_AUTH_WPA2_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_WIFI_AUTH_WPA_WPA2_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_WIFI_AUTH_WPA2_ENTERPRISE
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_ENTERPRISE
#elif CONFIG_WIFI_AUTH_WPA3_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_WIFI_AUTH_WPA2_WPA3_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_WIFI_AUTH_WAPI_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif
//-------------------------------------------------------------
static const char *TAG = "wifi";
static esp_ip4_addr_t s_ip_addr;
static xSemaphoreHandle s_semph_get_ip_addrs;
static esp_netif_t *s_esp_netif = NULL;
static int s_active_interfaces = 0;

//-------------------------------------------------------------
static bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}
//-------------------------------------------------------------
static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  //gpio_set_level(CONFIG_LED_GPIO, 0);
  httpd_handle_t* server = (httpd_handle_t*) arg;
  if (*server) {
      ESP_LOGI(TAG, "Stopping webserver");
      stop_webserver(*server);
      *server = NULL;
  }
  ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
  esp_err_t err = esp_wifi_connect();
  if (err == ESP_ERR_WIFI_NOT_STARTED) {
      return;
  }
  ESP_LOGI(TAG, "esp_wifi_connect() : %d", err);
}
//-------------------------------------------------------------
static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  if (!is_our_netif(TAG, event->esp_netif)) {
      ESP_LOGW(TAG, "Got IPv4 from another interface \"%s\": ignored", esp_netif_get_desc(event->esp_netif));
      return;
  }
  ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
  memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
  //gpio_set_level(CONFIG_LED_GPIO, 1);
  httpd_handle_t* server = (httpd_handle_t*) arg;
  if (*server == NULL) {
      ESP_LOGI(TAG, "Starting webserver");
      *server = start_webserver();
  }
  xSemaphoreGive(s_semph_get_ip_addrs);
}
//-------------------------------------------------------------
static esp_netif_t *wifi_start(void)
{
  esp_err_t ret;
  static httpd_handle_t server = NULL;
  char *desc;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ret = esp_wifi_init(&cfg);
  ESP_LOGI(TAG, "esp_wifi_init : %d", ret);
  esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
  asprintf(&desc, "%s: %s", TAG, esp_netif_config.if_desc);
  esp_netif_config.if_desc = desc;
  esp_netif_config.route_prio = 128;
  esp_netif_t *netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
  free(desc);
  esp_wifi_set_default_wifi_sta_handlers();
  ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, &server);
  ESP_LOGI(TAG, "esp_event_handler_register(WIFI_EVENT) : %d", ret);
  ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, &server);
  ESP_LOGI(TAG, "esp_event_handler_register(IP_EVENT) : %d", ret);
  ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  ESP_LOGI(TAG, "esp_wifi_set_storage : %d", ret);

  wifi_config_t wifi_config = {
      .sta = {
          .ssid = CONFIG_ESP_WIFI_SSID,
          .password = CONFIG_ESP_WIFI_PASSWORD,
          .scan_method = WIFI_SCAN_METHOD,
          .sort_method = WIFI_CONNECT_AP_SORT_METHOD,
          .threshold.rssi = CONFIG_WIFI_SCAN_RSSI_THRESHOLD,
          .threshold.authmode = WIFI_SCAN_AUTH_MODE_THRESHOLD,
      },
  };
  ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
  ret = esp_wifi_set_mode(WIFI_MODE_STA);
  ESP_LOGI(TAG, "esp_wifi_set_mode : %d", ret);
  ret = esp_wifi_set_ps(WIFI_PS_NONE);
  ESP_LOGI(TAG, "esp_wifi_set_ps : %d", ret);
  ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  ESP_LOGI(TAG, "esp_wifi_set_config : %d", ret);
  ret = esp_wifi_start();
  ESP_LOGI(TAG, "esp_wifi_start : %d", ret);
  esp_wifi_connect();
  return netif;
}
//-------------------------------------------------------------
static esp_netif_t *get_example_netif_from_desc(const char *desc)
{
    esp_netif_t *netif = NULL;
    char *expected_desc;
    asprintf(&expected_desc, "%s: %s", TAG, desc);
    while ((netif = esp_netif_next(netif)) != NULL) {
        if (strcmp(esp_netif_get_desc(netif), expected_desc) == 0) {
            free(expected_desc);
            return netif;
        }
    }
    free(expected_desc);
    return netif;
}
//-------------------------------------------------------------
static void wifi_stop(void)
{
  esp_err_t ret;
  esp_netif_t *wifi_netif = get_example_netif_from_desc("ap");
  ret = esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect);
  ESP_LOGI(TAG, "esp_event_handler_unregister(WIFI_EVENT) : %d", ret);
  ret = esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip);
  ESP_LOGI(TAG, "esp_event_handler_unregister(IP_EVENT : %d", ret);
  ret = esp_wifi_stop();
  if (ret == ESP_ERR_WIFI_NOT_INIT) {
      return;
  }
  ESP_LOGI(TAG, "esp_wifi_stop : %d", ret);
  ret = esp_wifi_deinit();
  ESP_LOGI(TAG, "esp_wifi_deinit : %d", ret);
  ret = esp_wifi_clear_default_wifi_driver_and_handlers(wifi_netif);
  ESP_LOGI(TAG, "esp_wifi_clear_default_wifi_driver_and_handlers : %d", ret);
  esp_netif_destroy(wifi_netif);
  s_esp_netif = NULL;
}
//-------------------------------------------------------------
static void net_start(void)
{
  s_esp_netif = wifi_start();
  s_active_interfaces++;
  s_semph_get_ip_addrs = xSemaphoreCreateCounting(s_active_interfaces, 0);
}
//-------------------------------------------------------------
void net_stop(void)
{
  wifi_stop();
  s_active_interfaces--;
}
//-------------------------------------------------------------
esp_err_t wifi_init_sta(void)
{
  esp_err_t ret;
  if (s_semph_get_ip_addrs != NULL) {
      return ESP_ERR_INVALID_STATE;
  }
  net_start();
  ret = esp_register_shutdown_handler(&net_stop);
  ESP_LOGI(TAG, "esp_register_shutdown_handler(&net_stop) : %d", ret);
  for (int i = 0; i < s_active_interfaces; ++i) {
      xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);
  }
  //gpio_set_level(CONFIG_LED_GPIO, 1);
  esp_netif_t *netif = NULL;
  esp_netif_ip_info_t ip;
  for (int i = 0; i < esp_netif_get_nr_of_ifs(); ++i) {
      netif = esp_netif_next(netif);
    if (is_our_netif(TAG, netif)) {
        ESP_LOGI(TAG, "Connected to %s", esp_netif_get_desc(netif));
        ret = esp_netif_get_ip_info(netif, &ip);
        ESP_LOGI(TAG, "esp_netif_get_ip_info : %d", ret);
        ESP_LOGI(TAG, "- IPv4 address: " IPSTR, IP2STR(&ip.ip));
    }
  }
  return ESP_OK;
}


#define EXAMPLE_ESP_WIFI_SSID	"esp_share"
#define EXAMPLE_ESP_WIFI_PASS 	"1234"
#define EXAMPLE_ESP_WIFI_CHANNEL	3

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

//-------------------------------------------------------------
esp_err_t wifi_init_ap(void)
{
	esp_err_t ret;

	s_esp_netif = esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
											ESP_EVENT_ANY_ID,
											&wifi_event_handler,
											NULL,
											NULL));

//	ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, &server);
//	ESP_LOGI(TAG, "esp_event_handler_register(WIFI_EVENT) : %d", ret);
//	ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, &server);
//	ESP_LOGI(TAG, "esp_event_handler_register(IP_EVENT) : %d", ret);

	wifi_config_t wifi_config = {
		.ap = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
			//.channel = EXAMPLE_ESP_WIFI_CHANNEL,
			.password = EXAMPLE_ESP_WIFI_PASS,
			.max_connection = 1,

			.authmode = WIFI_AUTH_OPEN,
		},
	};


	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",  EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);

//  s_active_interfaces++;
//  s_semph_get_ip_addrs = xSemaphoreCreateCounting(s_active_interfaces, 0);

//  ret = esp_register_shutdown_handler(&net_stop);
//  ESP_LOGI(TAG, "esp_register_shutdown_handler(&net_stop) : %d", ret);
//  for (int i = 0; i < s_active_interfaces; ++i) {
//      xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);
//  }


//  httpd_handle_t* server = NULL;//(httpd_handle_t*) arg;
//  if (*server == NULL) {
//
//      *server = start_webserver();
//  }
//  else
//	  ESP_LOGI(TAG, "*server != NULL");

  return ESP_OK;
}


esp_err_t wifi_deinit_ap(void)
{
	esp_err_t ret;

	//stop_webserver(&server);
	ret = esp_event_handler_instance_unregister(WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			NULL);

	ESP_LOGI(TAG, "esp_event_handler_unregister(IP_EVENT : %d", ret);
	ret = esp_wifi_stop();

	if (ret == ESP_ERR_WIFI_NOT_INIT)
	{
		return ESP_OK;
	}

	ESP_LOGI(TAG, "esp_wifi_stop : %d", ret);
	ret = esp_wifi_deinit();
	ESP_LOGI(TAG, "esp_wifi_deinit : %d", ret);
	ret = esp_wifi_clear_default_wifi_driver_and_handlers(s_esp_netif);
	ESP_LOGI(TAG, "esp_wifi_clear_default_wifi_driver_and_handlers : %d", ret);
	esp_netif_destroy(s_esp_netif);
	//esp_netif_destroy_default_wifi(s_esp_netif);

//	esp_wifi_stop();
//	esp_wifi_deinit();
//
//	esp_netif_destroy_default_wifi

	return ESP_OK;
}
