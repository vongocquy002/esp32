/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef ESPNOW_EXAMPLE_H
#define ESPNOW_EXAMPLE_H

//#include "esp_log.h"

/* ESPNOW can work in both station and softap mode. It is configured in menuconfig. */
#if CONFIG_ESPNOW_WIFI_MODE_STATION
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#else
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP
#endif

#define ESPNOW_QUEUE_SIZE     6
#define MAX_MAC_ADDRS         10
//static const char *TAG = "espnow_master";

#define IS_BROADCAST_ADDR(addr) (memcmp(addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

typedef enum {
    EXAMPLE_ESPNOW_SEND_CB,
    EXAMPLE_ESPNOW_RECV_CB,
} example_espnow_event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} example_espnow_event_send_cb_t;
typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} example_espnow_event_recv_cb_t;

/// MAC LIST//---------------------------------------**************************************************
typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
} mac_entry_t;
typedef struct {
    mac_entry_t known_slaves[MAX_MAC_ADDRS]; // Danh sách các MAC đã biết
    int known_slave_count; // Số lượng MAC đã biết
    mac_entry_t unknown_slaves[MAX_MAC_ADDRS]; // Danh sách các MAC chưa biết
    int unknown_slave_count; // Số lượng MAC chưa biết
} mac_management_t;
// Khởi tạo danh sách MAC quản lý
mac_management_t mac_list = { .known_slave_count = 0, .unknown_slave_count = 0 };

/*Predefined MAC list*/
static example_espnow_event_send_cb_t predefined_mac_list[MAX_MAC_ADDRS] = {
    { .mac_addr = {0x24, 0x6F, 0x28, 0x00, 0x00, 0x01} },
    { .mac_addr = {0x24, 0x6F, 0x28, 0x00, 0x00, 0x02} },
    { .mac_addr = {0x24, 0x6F, 0x28, 0x00, 0x00, 0x03} },
    { .mac_addr = {0x24, 0x6F, 0x28, 0x00, 0x00, 0x04} },
    { .mac_addr = {0x34, 0x85, 0x18, 0x03, 0x95, 0x08} }
};
/*Function to initialize the known slave list*/
void initialize_known_slaves(void) {
    for (int i = 0; i < MAX_MAC_ADDRS; i++) {
        if (memcmp(predefined_mac_list[i].mac_addr, "\0\0\0\0\0\0", ESP_NOW_ETH_ALEN) != 0) {
            memcpy(mac_list.known_slaves[mac_list.known_slave_count].mac_addr, predefined_mac_list[i].mac_addr, ESP_NOW_ETH_ALEN);
            mac_list.known_slave_count++;
        }
    }
}
bool add_mac_to_unknown_list(const uint8_t *mac_addr) {
    for (int i = 0; i < mac_list.unknown_slave_count; i++) {
        if (memcmp(mac_list.unknown_slaves[i].mac_addr, mac_addr, ESP_NOW_ETH_ALEN) == 0) {
            return false; // MAC đã tồn tại trong danh sách chưa biết
        }
    }
    if (mac_list.unknown_slave_count < MAX_MAC_ADDRS) {
        memcpy(mac_list.unknown_slaves[mac_list.unknown_slave_count].mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
        mac_list.unknown_slave_count++;
        return true;
    }

    return false; // Danh sách chưa biết đã đầy
}


//--------------------------------------------------------------
typedef union {
    example_espnow_event_send_cb_t send_cb;
    example_espnow_event_recv_cb_t recv_cb;
} example_espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct {
    example_espnow_event_id_t id;
    example_espnow_event_info_t info;
} example_espnow_event_t;

enum {
    EXAMPLE_ESPNOW_DATA_BROADCAST,
    EXAMPLE_ESPNOW_DATA_UNICAST,
    EXAMPLE_ESPNOW_DATA_MAX,
};

/* User defined field of ESPNOW data in this example. */
typedef struct {
    uint8_t type;                         //Broadcast or unicast ESPNOW data.
    uint8_t state;                        //Indicate that if has received broadcast ESPNOW data or not.
    uint16_t seq_num;                     //Sequence number of ESPNOW data.
    uint16_t crc;                         //CRC16 value of ESPNOW data.
    uint32_t magic;                       //Magic number which is used to determine which device to send unicast ESPNOW data.
    uint8_t payload[0];                   //Real payload of ESPNOW data.
} __attribute__((packed)) example_espnow_data_t;

/* Parameters of sending ESPNOW data. */
typedef struct {
    bool unicast;                         //Send unicast ESPNOW data.
    bool broadcast;                       //Send broadcast ESPNOW data.
    uint8_t state;                        //Indicate that if has received broadcast ESPNOW data or not.
    uint32_t magic;                       //Magic number which is used to determine which device to send unicast ESPNOW data.
    uint16_t count;                       //Total count of unicast ESPNOW data to be sent.
    uint16_t delay;                       //Delay between sending two ESPNOW data, unit: ms.
    int len;                              //Length of ESPNOW data to be sent, unit: byte.
    uint8_t *buffer;                      //Buffer pointing to ESPNOW data.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN];   //MAC address of destination device.
} example_espnow_send_param_t;

/*Check MAC receive*/



/*-----------------------------------------------------------------------*/


#endif
