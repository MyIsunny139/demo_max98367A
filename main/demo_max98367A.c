#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

#include "MAX98367A.h"
#include "audio_data.h"  // 包含音频数据头文件

static const char *TAG = "AUDIO_DEMO";

//? 音频参数配置
#define AUDIO_SAMPLE_RATE   44100
#define TONE_FREQUENCY      440.0f  // A4音符 (可调整频率)
#define TONE_DURATION_MS    3000    // 播放时长(毫秒)
#define AMPLITUDE           (INT32_MAX / 4)  // 音量幅度

// ...已移除正弦波生成函数...

/**
 * @brief 播放预录制的“我爱你，中国”语音
 */
void play_voice_task(void *pvParameters)
{
    ESP_LOGI(TAG, "循环播放: 我爱你，中国");
    i2s_tx_init();
    while (1) {
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(tx_handle, (void*)audio_data, audio_data_len, &bytes_written, portMAX_DELAY);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "播放完成: %d 字节", bytes_written);
        } else {
            ESP_LOGE(TAG, "播放失败: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // 每次播放间隔2秒
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "  MAX98367A 语音播放：我爱你，中国");
    ESP_LOGI(TAG, "======================================");
    xTaskCreate(play_voice_task, "play_voice", 4096, NULL, 5, NULL);
}
