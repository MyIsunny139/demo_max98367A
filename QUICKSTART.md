# 快速开始：播放"我爱你，中国"语音

## 🎯 目标
使用MAX98367A模块播放"我爱你，中国"的中文语音。

## 📋 前置条件

### 硬件连接
- MAX98367A的引脚已正确连接到ESP32：
  - DIN → GPIO 8
  - BCLK → GPIO 3
  - LRC → GPIO 46

### 软件工具
- Python 3.x
- FFmpeg（用于音频转换）

## 🚀 快速实现步骤

### 步骤1: 获取音频文件

**方法A - 在线TTS（推荐）**
1. 访问 https://tts.ai-app.cn/ 或其他在线TTS网站
2. 输入："我爱你，中国"
3. 选择合适的音色（如女声/男声）
4. 下载生成的MP3文件，命名为 `voice.mp3`

**方法B - 自己录制**
使用手机或电脑录音，保存为MP3或WAV格式

### 步骤2: 转换音频格式

在项目根目录执行：

```bash
# Windows PowerShell
cd E:\soft\ESP-project\esp32-demo-Assembly\demo_max98367A\tools
python audio_to_c_array.py voice.mp3
```

这会生成 `audio_data.h` 文件。

### 步骤3: 移动生成的文件

```bash
# Windows PowerShell
Move-Item audio_data.h ..\main\
```

### 步骤4: 修改代码

打开 `main/demo_max98367A.c`，用以下内容替换 `app_main()` 函数：

```c
#include "audio_data.h"

void play_voice_task(void *pvParameters)
{
    ESP_LOGI("VOICE", "播放: 我爱你，中国");
    
    // 初始化I2S
    i2s_tx_init();
    max98367a_set_gain(2.5f);
    
    // 播放音频
    size_t bytes_written;
    esp_err_t ret = i2s_channel_write(tx_handle, 
                                     (void*)audio_data, 
                                     audio_data_len, 
                                     &bytes_written, 
                                     portMAX_DELAY);
    
    if (ret == ESP_OK) {
        ESP_LOGI("VOICE", "播放完成: %d 字节", bytes_written);
    } else {
        ESP_LOGE("VOICE", "播放失败: %s", esp_err_to_name(ret));
    }
    
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreate(play_voice_task, "play_voice", 4096, NULL, 5, NULL);
}
```

### 步骤5: 编译和烧录

```bash
# 在ESP-IDF终端中执行
idf.py build
idf.py flash monitor
```

## ✅ 验证

如果一切正常，你应该听到：
1. ESP32启动
2. 喇叭播放"我爱你，中国"的语音
3. 串口监视器显示播放日志

## 🔧 调试技巧

### 问题1: 没有声音
- 检查MAX98367A电源是否正常（3.3V或5V）
- 检查引脚连接是否正确
- 尝试增大音量：`max98367a_set_gain(3.0f);`

### 问题2: 声音失真/破音
- 降低音量：`max98367a_set_gain(1.5f);`
- 检查音频源文件质量
- 确认采样率设置正确（44100Hz）

### 问题3: 编译错误
```
error: 'audio_data' undeclared
```
- 确认 `audio_data.h` 已复制到 `main/` 目录
- 确认代码中包含了 `#include "audio_data.h"`

### 问题4: 内存不足
```
E (123) VOICE: 内存分配失败
```
音频文件太大，解决方案：
1. 使用更短的音频（1-3秒）
2. 或使用SPIFFS/SD卡存储（参考 README_VOICE.md）

## 📊 音频文件大小参考

| 时长 | 数据大小 | 说明 |
|------|---------|------|
| 1秒  | ~352 KB | ✅ 推荐 |
| 2秒  | ~704 KB | ✅ 可行 |
| 3秒  | ~1 MB   | ⚠️ 接近极限 |
| 5秒+ | >1.7 MB | ❌ 需要外部存储 |

## 🎵 音量调节

```c
max98367a_set_gain(1.0f);  // 原始音量
max98367a_set_gain(2.0f);  // 2倍音量（推荐）
max98367a_set_gain(3.0f);  // 3倍音量（较大）
max98367a_set_gain(0.5f);  // 0.5倍音量（较小）
```

## 📚 更多信息

- 详细使用指南：`README_VOICE.md`
- 示例代码：`main/voice_player_example.c`
- 转换工具：`tools/audio_to_c_array.py`

## 🌟 高级功能

### 循环播放
```c
void play_voice_task(void *pvParameters)
{
    i2s_tx_init();
    max98367a_set_gain(2.5f);
    
    while (1) {
        size_t bytes_written;
        i2s_channel_write(tx_handle, (void*)audio_data, 
                         audio_data_len, &bytes_written, portMAX_DELAY);
        
        vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒
    }
}
```

### 按键触发播放
```c
#define BUTTON_PIN GPIO_NUM_0

void button_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);
    
    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) {
            play_voice();  // 播放语音
            vTaskDelay(pdMS_TO_TICKS(1000));  // 防抖
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

## 💡 提示

1. 首次使用建议用简短音频（1-2秒）测试
2. 音频质量会影响最终效果，建议使用高质量TTS
3. 如果需要多段语音，可以生成多个数组（audio_data1, audio_data2等）
4. 播放前检查可用堆内存：`ESP_LOGI("MEM", "Free heap: %d", esp_get_free_heap_size());`
