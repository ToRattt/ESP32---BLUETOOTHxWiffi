#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"
#include <string.h>
#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_netif_types.h"
#include "esp_wifi_types.h"

#include "freertos/projdefs.h"

#include "esp_mac.h"
#include "esp_wifi.h"

#include "http_parser.h"

#include "esp_system.h"
#include "lwip/err.h"
#include "lwip/sys.h"


#include "esp_mac.h"
#include <stdlib.h>
#include "esp_netif.h"
#include "nvs.h"
#include "portmacro.h"
#include <string.h>
#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_netif_types.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "http_parser.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "freertos/event_groups.h"
#include "esp_mac.h"
#include <stdlib.h>
#include <esp_http_server.h>
#include "esp_netif.h"
#include "nvs.h"
#include "portmacro.h"

#define EXAMPLE_ESP_WIFI_SSID      "ESP32 - WIFFI"
#define EXAMPLE_ESP_WIFI_PASS      "123456789"
#define EXAMPLE_ESP_WIFI_CHANNEL   1
#define EXAMPLE_MAX_STA_CONN       4
#define MIN(x,y) ((x) <(y) ? (x) : (y))
#define EXAMPLE_ESP_MAXIMUM_RETRY 10

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static const char *TAG_WIFI="wifi ";

static const char *TAG_HTTP="HTTP SERVER";
static int s_retry_num=0;
static EventGroupHandle_t s_wifi_event_group;

char *TAG = "BLE-Server";
uint8_t ble_addr_type;
void ble_app_advertise(void);


static esp_err_t root_get_handler(httpd_req_t *req)
{
const char resp[] ="<!DOCTYPE html>"
"<html>"
    "<head>"
        "<meta charset=\"UTF-8\">"
    "</head>"
    "<body>"
        "<form id=\"wifiForm\">"
            "<input type=\"text\" id=\"ssid\" name=\"ssid\" placeholder=\"SSID\" required>"
            "<h1></h1>"
            "<input type=\"password\" id=\"password\" name=\"password\" placeholder=\"Password\" required>"
            "<h1></h1>"
            "<button type=\"button\" onclick=\"sendWiFiInfo()\">Send WiFi Info</button>"
        "</form>"

        "<script>"
            "function sendWiFiInfo() {"
                "var ssid = document.getElementById(\"ssid\").value;"
                "var pass = document.getElementById(\"password\").value;"
                "var data = \"ssid=\" + encodeURIComponent(ssid) + \"&password=\" + encodeURIComponent(pass);"

                "var xhttp = new XMLHttpRequest();"
                "xhttp.open(\"POST\", \"/post\", true);"
                "xhttp.setRequestHeader(\"Content-type\", \"application/x-www-form-urlencoded\");"
                "xhttp.send(data);"
            "}"
        "</script>"
    "</body>"
"</html>";
httpd_resp_send(req,resp,HTTPD_RESP_USE_STRLEN);


return ESP_OK;
}


static const httpd_uri_t submit={
	.uri="/get",
	.method =HTTP_GET,
	.handler=root_get_handler,
	.user_ctx=NULL,

};


static esp_err_t set_post_handler(httpd_req_t *req){

	char buf[100];
	int ret, data_len=req->content_len;
	while (data_len >0){
		if((ret=httpd_req_recv(req, buf, MIN(data_len,sizeof(buf))))<=0){
			if(ret==HTTPD_SOCK_ERR_TIMEOUT){
				continue;
			}
			return ESP_FAIL;
			}
			data_len -= ret;
		}
		buf[req->content_len]='\0';

		char ssid[32]={0};
		char password[64]={0};
		sscanf(buf,"ssid=%31[^&]&password=%63s",ssid,password);
		ESP_LOGI(TAG_HTTP, "Parsed SSID: %s", ssid);
    	ESP_LOGI(TAG_HTTP, "Parsed Password: %s", password);
		nvs_handle_t nvs_handle;
		ESP_ERROR_CHECK(nvs_open("storage",NVS_READWRITE,&nvs_handle));
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle,"ssid",ssid));
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle,"password",password));
		ESP_ERROR_CHECK(nvs_commit(nvs_handle));
		nvs_close(nvs_handle);
		/*esp_err_t err=nvs_open("storage", NVS_READWRITE,&nvs_handle);
		if(err==ESP_OK){
			nvs_set_str(nvs_handle, "ssid", ssid);
			nvs_set_str(nvs_handle, "password", password);
			nvs_commit(nvs_handle);
			nvs_close(nvs_handle);
		}*/
		httpd_resp_send(req, "WiFi configuration received", HTTPD_RESP_USE_STRLEN);

		vTaskDelay(1000 / portTICK_PERIOD_MS);
		esp_restart();
	return ESP_OK;

}
static const httpd_uri_t post = {
    .uri = "/post",
    .method = HTTP_POST,
    .handler = set_post_handler,
    .user_ctx = NULL,
    };



esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{


    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}


static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG_WIFI, "Registering URI handlers");
        httpd_register_uri_handler(server, &submit);
        httpd_register_uri_handler(server, &post);
        //httpd_register_uri_handler(server, &echo);
        //httpd_register_uri_handler(server, &ctrl);

        return server;
    }

    ESP_LOGI(TAG_WIFI, "Error starting server!");
    return NULL;
}




static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG_HTTP, "Stopping webserver");
        stop_webserver(*server);
        *server=NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
         start_webserver();
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG_WIFI, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG_WIFI, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id ==WIFI_EVENT_STA_DISCONNECTED){
		if(s_retry_num <EXAMPLE_ESP_MAXIMUM_RETRY){
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG_WIFI,"retry to connect to the AP");
			
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
			
		}
		ESP_LOGI(TAG, "connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
		ESP_LOGI(TAG_WIFI,"got ip:" IPSTR,IP2STR(&event->ip_info.ip));
		s_retry_num=0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}   
}

void wifi_init_softap(void)
{
    //ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);

}

void wifi_init_sta(const char* ssid, const char* password)
{
    s_wifi_event_group = xEventGroupCreate();

    //ESP_ERROR_CHECK(esp_netif_init());

   // ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //esp_event_handler_instance_t instance_any_id;
    //esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = " ",
            .password = " ",
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            
        },
    };
    
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(10000));

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "connected to ap SSID:%s password:%s",
                 ssid, password);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_WIFI, "Failed to connect to SSID:%s, password:%s",
        		ssid, password);
    } else {
        ESP_LOGE(TAG_WIFI, "UNEXPECTED EVENT");
    }
}
// Write data to ESP32 defined as server
static int device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // printf("Data from the client: %.*s\n", ctxt->om->om_len, ctxt->om->om_data);

    char * data = (char *)ctxt->om->om_data;
    
    char *ssid = strtok(data, "&");
    char *password = strtok(NULL, "&");

    printf("ssid: %s, password: %s\n", ssid, password);
    nvs_handle_t nvs_handle;
	ESP_ERROR_CHECK(nvs_open("storage",NVS_READWRITE,&nvs_handle));
	ESP_ERROR_CHECK(nvs_set_str(nvs_handle,"ssid",ssid));
	ESP_ERROR_CHECK(nvs_set_str(nvs_handle,"password",password));
	ESP_ERROR_CHECK(nvs_commit(nvs_handle));
	nvs_close(nvs_handle);
    wifi_init_sta(ssid, password);
    return 0;
}

// Read data from ESP32 defined as server
static int device_read(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, "Data from the server", strlen("Data from the server"));
    return 0;
}

// Array of pointers to other service definitions
// UUID - Universal Unique Identifier
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x180),                 // Define UUID for device type
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0xFEF4),           // Define UUID for reading
          .flags = BLE_GATT_CHR_F_READ,
          .access_cb = device_read},
         {.uuid = BLE_UUID16_DECLARE(0xDEAD),           // Define UUID for writing
          .flags = BLE_GATT_CHR_F_WRITE,
          .access_cb = device_write},
         {0}}},
    {0}};

// BLE event handling
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    // Advertise if connected
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        if (event->connect.status != 0)
        {
            ble_app_advertise();
        }
        break;
    // Advertise again after completion of the event
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT DISCONNECTED");
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI("GAP", "BLE GAP EVENT");
        ble_app_advertise();
        break;
    default:
        break;
    }
    return 0;
}

// Define the BLE connection
void ble_app_advertise(void)
{
    // GAP - device name definition
    struct ble_hs_adv_fields fields;
    const char *device_name;
    memset(&fields, 0, sizeof(fields));
    device_name = ble_svc_gap_device_name(); // Read the BLE device name
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    // GAP - device connectivity definition
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // connectable or non-connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // discoverable or non-discoverable
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

// The application
void ble_app_on_sync(void)
{
    ble_hs_id_infer_auto(0, &ble_addr_type); // Determines the best address type automatically
    ble_app_advertise();                     // Define the BLE connection
}

// The infinite task
void host_task(void *param)
{
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
}

void app_main(){

    //nvs_flash_init();                          // 1 - Initialize NVS flash using
    // esp_nimble_hci_and_controller_init();      // 2 - Initialize ESP controller
    //nimble_port_init();                        // 3 - Initialize the host stack
    //ble_svc_gap_device_name_set("BLE-Server"); // 4 - Initialize NimBLE configuration - server name
    //ble_svc_gap_init();                        // 4 - Initialize NimBLE configuration - gap service
    //ble_svc_gatt_init();                       // 4 - Initialize NimBLE configuration - gatt service
    //ble_gatts_count_cfg(gatt_svcs);            // 4 - Initialize NimBLE configuration - config gatt services
    //ble_gatts_add_svcs(gatt_svcs);             // 4 - Initialize NimBLE configuration - queues gatt services.
    //ble_hs_cfg.sync_cb = ble_app_on_sync;      // 5 - Initialize application
    //nimble_port_freertos_init(host_task);      // 6 - Run the thread

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
   
     
     char ssid[32]={0};
     char password[64]={0};
     nvs_handle_t nvs_handle;
	ret = nvs_open("storage", NVS_READONLY, &nvs_handle);
	 
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG_WIFI, "No WiFi credentials found in NVS");
      
    nimble_port_init();                        // 3 - Initialize the host stack
    ble_svc_gap_device_name_set("BLE-Server"); // 4 - Initialize NimBLE configuration - server name
    ble_svc_gap_init();                        // 4 - Initialize NimBLE configuration - gap service
    ble_svc_gatt_init();                       // 4 - Initialize NimBLE configuration - gatt service
    ble_gatts_count_cfg(gatt_svcs);            // 4 - Initialize NimBLE configuration - config gatt services
    ble_gatts_add_svcs(gatt_svcs);             // 4 - Initialize NimBLE configuration - queues gatt services.
    ble_hs_cfg.sync_cb = ble_app_on_sync;      // 5 - Initialize application
    nimble_port_freertos_init(host_task);   
    
    wifi_init_softap();
    start_webserver();
    } else {
    // Nếu tìm thấy, thực hiện bước tiếp theo
       size_t ssid_len = sizeof(ssid);
       size_t password_len = sizeof(password);
       ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len));
       ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "password", password, &password_len));
       nvs_close(nvs_handle);
    
       ESP_LOGI(TAG_WIFI, "Connecting to WiFi SSID: %s", ssid);
       wifi_init_sta(ssid, password);
}
}
