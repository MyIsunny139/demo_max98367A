#include "wifi_sta.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <string.h>

#define TAG "wifi_sta"

//? WiFi全局状态
static bool g_wifi_connected = false;
static uint8_t g_retry_count = 0;
static uint8_t g_max_retry = 0;
static wifi_event_callback_t g_connected_cb = NULL;
static wifi_event_callback_t g_disconnected_cb = NULL;

//? WiFi事件处理函数
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            //? STA模式已启动，开始连接WiFi
            ESP_LOGI(TAG, "WiFi STA started, connecting...");
            esp_wifi_connect();
            break;
            
        case WIFI_EVENT_STA_CONNECTED:
            //? ESP32已成功连接到路由器
            ESP_LOGI(TAG, "WiFi connected to AP");
            g_retry_count = 0;  //? 重置重试计数
            break;
            
        case WIFI_EVENT_STA_DISCONNECTED:
            //? WiFi断开连接
            g_wifi_connected = false;
            
            if (g_max_retry == 0 || g_retry_count < g_max_retry)
            {
                esp_wifi_connect();
                g_retry_count++;
                ESP_LOGI(TAG, "Retry connecting to AP (attempt %d/%d)", 
                         g_retry_count, g_max_retry == 0 ? 999 : g_max_retry);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to connect to AP after %d attempts", g_max_retry);
            }
            
            //? 调用断开连接回调
            if (g_disconnected_cb)
            {
                g_disconnected_cb();
            }
            break;
            
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
            //? 获取到IP地址，WiFi连接成功
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
            g_wifi_connected = true;
            g_retry_count = 0;
            
            //? 调用连接成功回调
            if (g_connected_cb)
            {
                g_connected_cb();
            }
            break;
            
        default:
            break;
        }
    }
}

esp_err_t wifi_sta_init(const wifi_sta_config *config)
{
    //? 参数检查
    if (!config || !config->ssid || !config->password)
    {
        ESP_LOGE(TAG, "Invalid WiFi configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    g_max_retry = config->max_retry;
    g_retry_count = 0;
    
    //? 1. 初始化NVS（用于保存WiFi配置）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    //? 2. 初始化TCP/IP协议栈（LWIP）
    ESP_ERROR_CHECK(esp_netif_init());
    
    //? 3. 创建事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    //? 4. 创建默认WiFi STA网络接口
    esp_netif_create_default_wifi_sta();
    
    //? 5. 初始化WiFi驱动
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    
    //? 6. 注册WiFi和IP事件回调
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    //? 7. 配置WiFi参数
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;  //? WPA2加密
    wifi_config.sta.pmf_cfg.capable = true;                   //? 支持PMF（保护管理帧）
    wifi_config.sta.pmf_cfg.required = false;                 //? 不强制要求PMF
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    //? 8. 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi STA initialization complete, connecting to SSID: %s", config->ssid);
    
    return ESP_OK;
}

void wifi_sta_set_connected_callback(wifi_event_callback_t callback)
{
    g_connected_cb = callback;
}

void wifi_sta_set_disconnected_callback(wifi_event_callback_t callback)
{
    g_disconnected_cb = callback;
}

bool wifi_sta_is_connected(void)
{
    return g_wifi_connected;
}

void wifi_sta_stop(void)
{
    if (g_wifi_connected)
    {
        esp_wifi_disconnect();
    }
    esp_wifi_stop();
    g_wifi_connected = false;
    ESP_LOGI(TAG, "WiFi STA stopped");
}
