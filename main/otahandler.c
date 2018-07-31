#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_http_client.h"

#include "otahandler.h"

#define BUFFSIZE 1024
static const char *TAG = "otahandler";

static char serverAddress[200] = {0};

static char ota_write_data[BUFFSIZE + 1] = { 0 };
static char receiveBuffer[BUFFSIZE +1] = { 0 };
static uint32_t content_length = 0;
static uint32_t data_received = 0;

static EventGroupHandle_t egOtareader;
const int OTAREADDONE = BIT0;
const int OTAREADCOMPLETE = BIT1;

otaMode_t _otamode = OTAMODE_HOT;




static void __attribute__((noreturn)) task_fatal_error()
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();
    (void)vTaskDelete(NULL);
    while (1) {
        ;
    }
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    int read_len = 0;
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGW(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGW(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGW(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGW(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            
            break;
        case HTTP_EVENT_ON_DATA:
            data_received+=evt->data_len;
            if(data_received < content_length) {
                xEventGroupSetBits(egOtareader, OTAREADDONE);
            } else if (data_received == content_length) {
                xEventGroupSetBits(egOtareader, OTAREADDONE | OTAREADCOMPLETE);
            } else {
                ESP_LOGI(TAG, "Read done");
                xEventGroupSetBits(egOtareader, OTAREADCOMPLETE);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGW(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static void otahandler_task(void *pvParameter) {
    esp_err_t err;
    int binary_file_length = 0;
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA example...");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    esp_http_client_config_t config = {
        .url = &serverAddress,
        .event_handler = _http_event_handler,
        .buffer_size = BUFFSIZE,
    };

    ESP_LOGW(TAG, "Serveraddress: %s", config.url);

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if((err = esp_http_client_open(client, 0)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        free(receiveBuffer);
        return;
    }
    
    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x", update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        task_fatal_error();
    }
    ESP_LOGI(TAG, "esp_ota_begin succeeded");
    
    content_length =  esp_http_client_fetch_headers(client);
    ESP_LOGW(TAG, "conten_length from esp_http_client_fetch_headers: %i", content_length);
    int total_read_len = 0, read_len = 0;    
    int buffer_length = 0;

    do {
        if(data_received < content_length) {
            memset(receiveBuffer, 0, BUFFSIZE);
            read_len = 0;
            if(content_length - data_received < BUFFSIZE) {
                read_len = esp_http_client_read(client, receiveBuffer, content_length - data_received);
            } else {
                read_len = esp_http_client_read(client, receiveBuffer, BUFFSIZE);
            }
        } else {
            xEventGroupSetBits(egOtareader, OTAREADCOMPLETE);
        }
        ESP_LOGI(TAG, "reading data");
        xEventGroupWaitBits(egOtareader, OTAREADCOMPLETE | OTAREADDONE, false, false, portMAX_DELAY);
        if((xEventGroupGetBits(egOtareader) & OTAREADDONE) == true) {
            xEventGroupClearBits(egOtareader, OTAREADDONE);
            ESP_LOGW(TAG, "%i of %i bytes received\n\r", data_received, content_length);
            
            memcpy(ota_write_data, receiveBuffer, read_len);
            ESP_LOGW(TAG, "wrigint to ota partiotion...");
            err = esp_ota_write( update_handle, (const void *)ota_write_data, read_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error: esp_ota_write failed (%s)!", esp_err_to_name(err));
                task_fatal_error();
            }
            binary_file_length += read_len;
            ESP_LOGI(TAG, "Have written image length %d", binary_file_length);            
        }
    } while((xEventGroupGetBits(egOtareader) & OTAREADCOMPLETE) == false);
    if(data_received != content_length) {
        ESP_LOGE(TAG, "Something went wrong, not all data received");
        task_fatal_error();
    }

    ESP_LOGI(TAG, "... read finished!");
    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d", esp_http_client_get_status_code(client), esp_http_client_get_content_length(client));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);

    if (esp_ota_end(update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed!");
        task_fatal_error();
    }
    if(_otamode == OTAMODE_HOT) {
        err = esp_ota_set_boot_partition(update_partition);
    } else {
        err = ESP_OK;
        ESP_LOGW(TAG, "Ota Done, but partition not switched for testing reasons");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        task_fatal_error();
    }
    ESP_LOGI(TAG, "Prepare to restart system!");
    if(_otamode == OTAMODE_TEST) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    esp_restart();
}

void startOTA(char *Servername, uint16_t Port, char* Filename, otaMode_t otamode) {
    egOtareader = xEventGroupCreate();
    sprintf(serverAddress, "%s:%i/%s", Servername, Port, Filename);
    _otamode = otamode;
    xTaskCreate(&otahandler_task, "otahandler_task", 8192, NULL, 5, NULL);
}




