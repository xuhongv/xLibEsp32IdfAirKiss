#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "airkiss.h"
#include "mbedtls/base64.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

/**
 *    由于 esp-idf esp32/esp32 s2芯片 sdk 乐鑫没开源微信近场发现的功能，于是动动手指做起来！
 *    这是微信airkiss配网以及近场发现的功能的demo示范，亲测可以配网成功以及近场发现！
 *    有任何技术问题邮箱： 870189248@qq.com 
 *    本人GitHub仓库：https://github.com/xuhongv
 *    本人博客：https://blog.csdn.net/xh870189248
 **/

 
static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int CONFIG_NET_DONE_BIT = BIT1;
static const char *TAG = "airkiss_s2";

static void smartconfig_example_task(void *parm);

const airkiss_config_t ak_config = {
    (airkiss_memset_fn)&memset,
    (airkiss_memcpy_fn)&memcpy,
    (airkiss_memcmp_fn)&memcmp,
    (airkiss_printf_fn)&printf,
};

//airkiss
#define COUNTS_BOACAST 30            //发包次数，微信建议20次以上
#define ACCOUNT_ID "gh_4248324a4d02" //微信公众号
#define LOCAL_UDP_PORT 12476         //固定端口号
int sock_fd;

//近场发现自定义消息
uint8_t deviceInfo[100] = {"{\"name\":\"xuhong\",\"age\":18}"};


static void TaskCreatSocket(void *pvParameters)
{

    char rx_buffer[128];
    uint8_t tx_buffer[512];
    uint8_t lan_buf[300];
    uint16_t lan_buf_len;
    struct sockaddr_in server_addr;
    int sock_server; /* server socked */
    int err;
    int counts = 0;
    size_t len;

    //base64加密要发送的数据
    if (mbedtls_base64_encode(tx_buffer, strlen((char *)tx_buffer), &len, deviceInfo, strlen((char *)deviceInfo)) != 0)
    {
        printf("[xuhong] fail mbedtls_base64_encode %s\n", tx_buffer);
        vTaskDelete(NULL);
    }

    sock_server = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_server == -1)
    {
        printf("failed to create sock_fd!\n");
        vTaskDelete(NULL);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // inet_addr("255.255.255.255");
    server_addr.sin_port = htons(LOCAL_UDP_PORT);

    err = bind(sock_server, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err == -1)
    {
        vTaskDelete(NULL);
    }

    struct sockaddr_in sourceAddr;
    socklen_t socklen = sizeof(sourceAddr);
    while (1)
    {
        memset(rx_buffer, 0, sizeof(rx_buffer));
        int len = recvfrom(sock_server, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&sourceAddr, &socklen);

        ESP_LOGI(TAG, "IP:%s:%d", (char *)inet_ntoa(sourceAddr.sin_addr), htons(sourceAddr.sin_port));
        //ESP_LOGI(TAG, "Received %s ", rx_buffer);

        // Error occured during receiving
        if (len < 0)
        {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        }
        // Data received
        else
        {
            rx_buffer[len] = 0;                                                   // Null-terminate whatever we received and treat like a string
            airkiss_lan_ret_t ret = airkiss_lan_recv(rx_buffer, len, &ak_config); //检测是否为微信发的数据包
            airkiss_lan_ret_t packret;
            switch (ret)
            {
            case AIRKISS_LAN_SSDP_REQ:

                lan_buf_len = sizeof(lan_buf);
                //开始组装打包
                packret = airkiss_lan_pack(AIRKISS_LAN_SSDP_NOTIFY_CMD, ACCOUNT_ID, tx_buffer, 0, 0, lan_buf, &lan_buf_len, &ak_config);
                if (packret != AIRKISS_LAN_PAKE_READY)
                {
                    ESP_LOGE(TAG, "Pack lan packet error!");
                    continue;
                }
                ESP_LOGI(TAG, "Pack lan packet ok !");
                //发送至微信客户端
                int err = sendto(sock_server, (char *)lan_buf, lan_buf_len, 0, (struct sockaddr *)&sourceAddr, sizeof(sourceAddr));
                if (err < 0)
                {
                    ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
                }
                else if (counts++ > COUNTS_BOACAST)
                {
                    shutdown(sock_fd, 0);
                    close(sock_fd);
                    vTaskDelete(NULL);
                }
                break;
            default:
                break;
            }
        }
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
    {
        ESP_LOGI(TAG, "Scan done");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
    {
        ESP_LOGI(TAG, "Found channel");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = {0};
        uint8_t password[65] = {0};

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true)
        {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
        xEventGroupSetBits(s_wifi_event_group, CONFIG_NET_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void smartconfig_example_task(void *parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_AIRKISS));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    while (1)
    {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | CONFIG_NET_DONE_BIT, true, false, portMAX_DELAY);
        if (uxBits & CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if (uxBits & CONFIG_NET_DONE_BIT)
        {
            ESP_LOGI(TAG, "smartconfig over");
            xTaskCreate(TaskCreatSocket, "TaskCreatSocket", 1024 * 4, NULL, 5, NULL);
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

/**
 * @description: 程序入口
 * @param {type} 
 * @return: 
 */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    initialise_wifi();
}
