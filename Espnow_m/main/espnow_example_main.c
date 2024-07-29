// /* ESPNOW Example

//    This example code is in the Public Domain (or CC0 licensed, at your option.)

//    Unless required by applicable law or agreed to in writing, this
//     software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//    CONDITIONS OF ANY KIND, either express or implied.
// */
// /*
//    This example shows how to use ESPNOW.
//    Prepare two device, one for sending ESPNOW data and another for receiving
//    ESPNOW data.
// */
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "espnow_example.h"

#define ESPNOW_MAXDELAY 512

static const char *TAG = "espnow_master";

static QueueHandle_t s_example_espnow_queue;

static uint8_t s_example_broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint16_t s_example_espnow_seq[EXAMPLE_ESPNOW_DATA_MAX] = { 0, 0 };

static void example_espnow_deinit(example_espnow_send_param_t *send_param);

int count = 0;
/* WiFi should start before using ESPNOW */
static void example_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );
    ESP_ERROR_CHECK( esp_wifi_start());
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
#endif
}

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
///////////////////////////////////////////////////////////////////////////////////////////////
static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    example_espnow_event_t evt;
    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }
    evt.id = EXAMPLE_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////
static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t * mac_addr = recv_info->src_addr;  ///note
    uint8_t * des_addr = recv_info->des_addr;  ///note
    count ++;
    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }
    // recv_cb->data = malloc(len);
    // if (recv_cb->data == NULL) {
    //     ESP_LOGE(TAG, "Malloc receive data fail");
    //     return;
    // }
    if (IS_BROADCAST_ADDR(des_addr)) {
        ESP_LOGD(TAG, "Receive broadcast ESPNOW data");
    } else {
        ESP_LOGD(TAG, "Receive unicast ESPNOW data");
    }
    evt.id = EXAMPLE_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        //free(recv_cb->data);
        if (recv_cb->data != NULL) {
            free(recv_cb->data);
            recv_cb->data = NULL;
        }
    }
    ////RSSI
    int8_t rssi = recv_info->rx_ctrl->rssi;
    ESP_LOGI(TAG, "RSSI: %d", rssi);
}
/* Parse received ESPNOW data. */
int example_espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq, uint32_t *magic, uint8_t **payload, uint16_t *payload_len)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;
    if (data_len < sizeof(example_espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }
    // Lưu các giá trị trạng thái, số thứ tự và magic
    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    // Tính toán CRC
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);
    // So sánh CRC
    if (crc_cal == crc) {
        *payload = buf->payload;
        *payload_len = data_len - sizeof(example_espnow_data_t);
        return buf->type;
    }
    return -1;
}
/* Prepare ESPNOW data to be sent. */
void example_espnow_data_prepare(example_espnow_send_param_t *send_param, const char* message)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)send_param->buffer;
    assert(send_param->len >= sizeof(example_espnow_data_t));

    buf->type = IS_BROADCAST_ADDR(send_param->dest_mac) ? EXAMPLE_ESPNOW_DATA_BROADCAST : EXAMPLE_ESPNOW_DATA_UNICAST;
    buf->state = send_param->state;
    buf->seq_num = s_example_espnow_seq[buf->type]++;
    buf->crc = 0;
    buf->magic = send_param->magic;
    
    size_t message_len = strlen(message);
    if (message_len >= (send_param->len - sizeof(example_espnow_data_t))) {
        ESP_LOGE(TAG, "Message is too long for the payload buffer");
        // Cắt bớt thông điệp để phù hợp với bộ đệm payload
        message_len = send_param->len - sizeof(example_espnow_data_t) - 1;
    }
    strncpy((char*)buf->payload, message, message_len); // +1 để bao gồm ký tự kết thúc chuỗi '\0'
    buf->payload[message_len] = '\0';
    send_param->len = sizeof(example_espnow_data_t) + message_len + 1;
    
    ESP_LOGI(TAG, "Prepare to send data from SLAVE: %s", buf->payload);
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

static void example_espnow_task(void *pvParameter)
{
    example_espnow_event_t evt;
    uint8_t recv_state = 0;
    uint16_t recv_seq = 0;
    uint32_t recv_magic = 0;
    uint8_t *payload = NULL;
    uint16_t payload_len = 0;
    int ret;
    //vTaskDelay(5000 / portTICK_PERIOD_MS);
    while (xQueueReceive(s_example_espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
        switch (evt.id) {
            case EXAMPLE_ESPNOW_RECV_CB:
            {
                example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
                ret = example_espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state, &recv_seq, &recv_magic, &payload, &payload_len);
                free(recv_cb->data);
                if (ret == EXAMPLE_ESPNOW_DATA_BROADCAST) {
                    ESP_LOGE(TAG, "Received %dth q broadcast data from " MACSTR ", state: %d, seq: %d, magic: %lu, message: %s",recv_seq, MAC2STR(recv_cb->mac_addr), recv_state, recv_seq, recv_magic, (char *)payload);
                    ESP_LOGI(TAG, "DATA FULL RECV BROADCAST: %s\n",(char *)recv_cb->data);
                    /* If MAC address does not exist in peer list, add it to peer list. */
                    if (esp_now_is_peer_exist(recv_cb->mac_addr) == false) {
                        esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                        if (peer == NULL) {
                            ESP_LOGE(TAG, "Malloc peer information fail");
                            example_espnow_deinit(NULL);
                            vTaskDelete(NULL);
                        }
                        memset(peer, 0, sizeof(esp_now_peer_info_t));
                        peer->channel = CONFIG_ESPNOW_CHANNEL;
                        peer->ifidx = ESPNOW_WIFI_IF;
                        peer->encrypt = false;
                        memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                        ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                        free(peer);
                    }
                    ///SEND UNICAST WHEN RECV BROADCAST FROM MASTER///
                    /* Prepare to send unicast ESPNOW data. */
                    //-----------------------------------------------------------------------------
                    example_espnow_send_param_t *send_param;
                    send_param = malloc(sizeof(example_espnow_send_param_t));
                    //example_espnow_send_param_t *send_param = (example_espnow_send_param_t *)pvParameter;
                    if (send_param == NULL) {
                        ESP_LOGE(TAG, "Malloc send parameter fail");
                        example_espnow_deinit(NULL);
                        vTaskDelete(NULL);
                    }
                    memset(send_param, 0, sizeof(example_espnow_send_param_t));
                    send_param->unicast = true;
                    send_param->broadcast = false;
                    send_param->state = recv_state;
                    send_param->magic = recv_magic;
                    //const char* message = "helloslave_for_____m_master";
                    send_param->len = 250;
                    send_param->buffer = malloc(send_param->len);
                    if (send_param->buffer == NULL) {
                        ESP_LOGE(TAG, "Malloc send buffer fail");
                        free(send_param);
                        example_espnow_deinit(NULL);
                        vTaskDelete(NULL);
                    }
                    //copy dia chi mac dich tu recv cb vao send param de gui
                    memcpy(send_param->dest_mac, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                    example_espnow_data_prepare(send_param, "AGREE TO CONNECT___");
                    
                    ESP_LOGI(TAG, "Send data w to "MACSTR"", MAC2STR(send_param->dest_mac));
                    ESP_LOGI(TAG, "////////////////////////////////////\n");
                    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                        ESP_LOGE(TAG, "Send error");
                        example_espnow_deinit(send_param);
                        vTaskDelete(NULL);
                    }
                    free(send_param->buffer);
                    free(send_param);
                    //---------------------------------------------------------
                } 
                else if (ret == EXAMPLE_ESPNOW_DATA_UNICAST) {
                    ESP_LOGE(TAG, "Received %dth unicast data from " MACSTR ", state: %d, seq: %d, magic: %lu, message: %s",recv_seq, MAC2STR(recv_cb->mac_addr), recv_state, recv_seq, recv_magic, (char *)payload);                
                }
                else {
                    ESP_LOGI(TAG, "Receive error data from: "MACSTR", len: %d", MAC2STR(recv_cb->mac_addr), recv_cb->data_len);
                }
                break;
            }
            case EXAMPLE_ESPNOW_SEND_CB:
            {
                example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
                ESP_LOGD(TAG, "Send data to "MACSTR"", MAC2STR(send_cb->mac_addr));
                break;
            }
            default:
                ESP_LOGE(TAG, "Unknown event id error: %d", evt.id);
                break;
        }
    }
}


static esp_err_t example_espnow_init(void)
{
    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    /* Add broadcast peer information to peer list. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vSemaphoreDelete(s_example_espnow_queue);
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);
    //get_peer_list();
    xTaskCreate(example_espnow_task, "example_espnow_task", 4096, NULL, 4, NULL);
    
    return ESP_OK;
    
}

static void example_espnow_deinit(example_espnow_send_param_t *send_param)
{
    vSemaphoreDelete(s_example_espnow_queue);
    if (send_param) {
        free(send_param->buffer);
        free(send_param);
    }
    esp_now_deinit();
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    example_wifi_init();
    ESP_ERROR_CHECK(example_espnow_init());

}
