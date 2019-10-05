/*
 * @Description: esp32 idf master分支实现 微信配网以及近场发送自定义消息到微信公众号
 * @Author: xuhong https://github.com/xuhongv 【i love china】
 * @Date: 2019-10-02 15:57:42
 * @LastEditTime: 2019-10-05 21:45:01
 * @LastEditors: Please set LastEditors
 */

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
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "xAirkiss.h"


/**
 *    由于 esp-idf esp32芯片 sdk 乐鑫没开源微信近场发现的功能，于是动动手指做起来！
 *    这是微信airkiss配网以及近场发现的功能的demo示范，亲测可以配网成功以及近场发现！
 *    有任何技术问题邮箱： 870189248@qq.com 
 *    本人GitHub仓库：https://github.com/xuhongv
 *    本人博客：https://blog.csdn.net/xh870189248
 **/


static const char *TAG = "esp32-idf-airkiss-example";

#define CONFIG_AIRKISS_APPID "gh_ee12f1dffa4e"               //微信公众号的原始ID
#define CONFIG_AIRKISS_DEVICEID "https://github.com/xuhongv" //设备id 也就是自定义消息
#define CONFIG_DUER_AIRKISS_KEY ""                           //默认是空即可

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int AIRKISS_DONE_BIT = BIT1;

static void TaskAirKiss(void *parm);

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{

    ESP_LOGI(TAG, "esp_event_base_t :%s , event_id: %d ", event_base, event_id);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        xTaskCreate(TaskAirKiss, "TaskAirKiss", 4096, NULL, 3, NULL);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        //打印下路由器分配的ip
        ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @description: 初始化wifi事件监听
 * @param {type} 
 * @return: 
 */
void initialise_wifi()
{
    s_wifi_event_group = xEventGroupCreate();
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @description:  微信配网已经近场发现的事件监听
 * @param {type} 
 * @return: 
 */
void TaskAirKissListener(void *p)
{
    xAirKissMsg sMsg;
    while (1)
    {
        if (xAirKissReceiveMsg(&sMsg))
        {
            ESP_LOGI(TAG, "xAirKissReceiveMsg %d", sMsg.type);
            switch (sMsg.type)
            {
            case xAirKiss_MSG_TYPE_SATRT: // 开始配网 嗅探
                ESP_LOGI(TAG, "xAirKissReceiveMsg xAirKiss_MSG_TYPE_SATRT");
                break;
            case xAirKiss_MSG_TYPE_CHANNLE_LOCKED: // 已经监听到了airkiss的数据，锁定了信道
                break;
            case xAirKiss_MSG_TYPE_GET_SSID_PASSWORD: // 成功获取路由器名字和密码
                ESP_LOGI(TAG, " get ssid[len:%d]: %s", sMsg.ssidLen, (char *)sMsg.ssid);
                ESP_LOGI(TAG, " get password[len:%d]: %s", sMsg.passwordLen, (char *)sMsg.password);
                //连接路由器
                wifi_config_t wifi_config;
                bzero(&wifi_config, sizeof(wifi_config_t));
                memcpy(wifi_config.sta.ssid, sMsg.ssid, sMsg.ssidLen);
                memcpy(wifi_config.sta.password, sMsg.password, sMsg.passwordLen);
                ESP_ERROR_CHECK(esp_wifi_disconnect());
                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
            case xAirKiss_MSG_TYPE_TIMEOUT: // 失败获取路由器名字和密码，超时
                break;
            case xAirKiss_MSG_TYPE_SEND_ACK_TO_WEICHAT_OVER: // 配网成功后，设备端发送配网成功的ack到微信端
            {
                //这里开始近场发现，注意的 CONFIG_AIRKISS_DEVICEID 务必是非json格式，个人建议是 base64 加密之后字符串
                airkiss_lan_pack_param_t *air_info = calloc(1, sizeof(airkiss_lan_pack_param_t));
                air_info->appid = CONFIG_AIRKISS_APPID;
                air_info->deviceid = CONFIG_AIRKISS_DEVICEID;
                airkiss_start_local_find(air_info);
            }
            break;
            case xAirKiss_MSG_TYPE_LOCAL_FIND_START: //开始近场发现
                ESP_LOGI(TAG, "xAirKissReceiveMsg xAirKiss_MSG_TYPE_LOCAL_FIND_START");
                break;
            case xAirKiss_MSG_TYPE_LOCAL_FIND_SENDING: //开始近场发现 发送消息中
                ESP_LOGI(TAG, "xAirKissReceiveMsg xAirKiss_MSG_TYPE_LOCAL_FIND_SENDING");
                break;
            case xAirKiss_MSG_TYPE_LOCAL_FIND_STOP: //结束近场发现
                ESP_LOGI(TAG, "xAirKissReceiveMsg xAirKiss_MSG_TYPE_LOCAL_FIND_STOP");
                xEventGroupSetBits(s_wifi_event_group, AIRKISS_DONE_BIT);
                //结束所有配网队列
                airkiss_stop_all();
                //删除本任务
                vTaskDelete(NULL);
                break;
            default:
                break;
            }
        }
    }
}
/**
 * @description: 微信配网流程
 * @param {type} 
 * @return: 
 */
static void TaskAirKiss(void *parm)
{
    EventBits_t uxBits;

    //开始配网
    airkiss_config_info_t air_info = AIRKISS_CONFIG_INFO_DEFAULT();
    air_info.lan_pack.appid = CONFIG_AIRKISS_APPID;
    air_info.lan_pack.deviceid = CONFIG_AIRKISS_DEVICEID;
    air_info.aes_key = CONFIG_DUER_AIRKISS_KEY;
    airkiss_start(&air_info);

    ESP_LOGI(TAG, "xAirkiss_version: %s", xAirkiss_version());

    xTaskCreate(TaskAirKissListener, "TaskAirKissListener", 1024 * 2, NULL, 8, NULL); // 创建任务

    while (1)
    {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | AIRKISS_DONE_BIT, true, false, portMAX_DELAY);
        if (uxBits & WIFI_CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "WiFi Connected to ap");
            //通知微信公众号配网成功
            airkiss_nofity_connect_ok();
        }
        if (uxBits & AIRKISS_DONE_BIT)
        {
            ESP_LOGI(TAG, "airkiss over");

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
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    printf("\n\n-------------------------------- Get Systrm Info Start ------------------------------------------\n");
    //获取IDF版本
    printf("     SDK version:%s\n", esp_get_idf_version());
    //获取芯片可用内存
    printf("     esp_get_free_heap_size : %d  \n", esp_get_free_heap_size());
    //获取从未使用过的最小内存
    printf("     esp_get_minimum_free_heap_size : %d  \n", esp_get_minimum_free_heap_size());
    uint8_t mac[6];
    //获取mac地址（station模式）
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf(" Station esp_read_mac(): %02x:%02x:%02x:%02x:%02x:%02x \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    //获取mac地址（ap模式）
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    printf(" AP esp_read_mac(): %02x:%02x:%02x:%02x:%02x:%02x \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    //获取mac地址（蓝牙模式）
    esp_read_mac(mac, ESP_MAC_BT);
    printf(" BT esp_read_mac(): %02x:%02x:%02x:%02x:%02x:%02x \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    //获取mac地址（以太网）
    esp_read_mac(mac, ESP_MAC_ETH);
    printf(" Eth esp_read_mac(): %02x:%02x:%02x:%02x:%02x:%02x \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    printf("\n\n-------------------------------- Get Systrm Info End ------------------------------------------\n");

    initialise_wifi();
}
