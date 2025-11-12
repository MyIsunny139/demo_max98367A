#ifndef _WIFI_STA_H_
#define _WIFI_STA_H_

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

//? ==================== WiFi默认配置 ====================
//? 可在调用 wifi_sta_init() 时传入自定义配置覆盖

//? WiFi SSID和密码
#ifndef WIFI_SSID
#define WIFI_SSID       "MY_2.4G"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS       "my666666"
#endif

//? WiFi最大重试次数
#ifndef WIFI_MAX_RETRY
#define WIFI_MAX_RETRY  5
#endif

//? WiFi STA模式配置结构体
typedef struct {
    const char *ssid;           //? WiFi名称（SSID）
    const char *password;       //? WiFi密码
    uint8_t max_retry;          //? 最大重连次数（0表示无限重试）
} wifi_sta_config;

//? WiFi连接状态回调函数类型
typedef void (*wifi_event_callback_t)(void);

//? 初始化并启动WiFi STA模式
//? @param config WiFi配置结构体指针
//? @return ESP_OK 成功, 其他值表示失败
esp_err_t wifi_sta_init(const wifi_sta_config *config);

//? 注册WiFi连接成功回调（可选）
//? @param callback 连接成功后的回调函数
void wifi_sta_set_connected_callback(wifi_event_callback_t callback);

//? 注册WiFi断开连接回调（可选）
//? @param callback 断开连接后的回调函数
void wifi_sta_set_disconnected_callback(wifi_event_callback_t callback);

//? 获取当前WiFi连接状态
//? @return true 已连接, false 未连接
bool wifi_sta_is_connected(void);

//? 断开WiFi连接并停止
void wifi_sta_stop(void);

#endif
