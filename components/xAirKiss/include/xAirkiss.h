/*
 * @Description: wechat airkiss to net
 * @Author: 徐宏 xuhong
 * @Date: 2019-10-03 16:36:21
 * @LastEditTime: 2019-10-05 21:31:08
 * @LastEditors: Please set LastEditors
 */
#ifndef X_AIRKISS_H_
#define X_AIRKISS_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "string.h"
#include "airkiss.h"

typedef enum
{
     xAirKiss_MSG_TYPE_SATRT = 0,                // 开始 嗅探
     xAirKiss_MSG_TYPE_CHANNLE_LOCKED,           // 已经监听到了airkiss的数据，锁定了信道
     xAirKiss_MSG_TYPE_GET_SSID_PASSWORD,        // 成功获取路由器名字和密码
     xAirKiss_MSG_TYPE_TIMEOUT,                  // 失败获取路由器名字和密码，超时
     xAirKiss_MSG_TYPE_SEND_ACK_TO_WEICHAT_OVER, // 配网成功后，设备端发送配网成功的ack到微信端
     xAirKiss_MSG_TYPE_STOP,                     // 结束 嗅探

     xAirKiss_MSG_TYPE_LOCAL_FIND_START,   //开始近场发现
     xAirKiss_MSG_TYPE_LOCAL_FIND_SENDING, //开始近场发现 发送消息
     xAirKiss_MSG_TYPE_LOCAL_FIND_STOP,    //结束近场发现

} xAirKissMsgType;

typedef struct
{
     char ssid[32];
     uint16_t ssidLen;
     char password[64];
     uint16_t passwordLen;
     xAirKissMsgType type;
} xAirKissMsg;

/**
     * @description: 开始 esp32 配置
     * @param {type} 
     * @return: 
     */
esp_err_t airkiss_start(airkiss_config_info_t *info);

/**
     * @description: 通知微信配网配置成功，此函数会向微信公众号发送消息以表示配网成功
     * @param {type} null
     * @return: 
     */
esp_err_t airkiss_nofity_connect_ok();

/**
     * @description: 开始 esp32 近场发现，主动发送消息到微信端
     * @param {type} 
     * @return: 
     */
esp_err_t airkiss_start_local_find(airkiss_lan_pack_param_t *lan_param);

/**
     * @description: 结束 esp32 微信配网配置和近场发现
     * @param {type} 
     * @return: 
     */
esp_err_t airkiss_stop_all();
/**
     * @description: 主动监听事件
     * @param {type} 
     * @return: 
     */
esp_err_t xAirKissReceiveMsg(xAirKissMsg *msg);

/*
 * 获取xAirKiss库版本信息
 */
const char *xAirkiss_version(void);

#endif /* X_AIRKISS_H_ */
