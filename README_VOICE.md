# MAX98367A 播放"我爱你，中国"语音指南

## 当前实现
当前代码播放一个测试音调（440Hz正弦波），用于验证MAX98367A硬件工作正常。

## 如何播放真实的中文语音

由于ESP32无法直接生成中文语音，需要使用预录制的音频文件。以下是完整步骤：

### 方法一：使用在线TTS服务录制音频

1. **获取音频文件**
   - 访问在线TTS服务（如：百度语音、讯飞语音、微软Azure TTS）
   - 输入文字"我爱你，中国"
   - 下载生成的音频文件（通常是MP3或WAV格式）

2. **转换音频格式**
   使用FFmpeg工具转换为ESP32兼容格式：
   ```bash
   ffmpeg -i input.mp3 -ar 44100 -ac 2 -f s32le output.raw
   ```
   参数说明：
   - `-ar 44100`: 采样率44100Hz
   - `-ac 2`: 双声道(立体声)
   - `-f s32le`: 32位有符号小端格式

3. **转换为C数组**
   使用Python脚本：
   ```python
   with open('output.raw', 'rb') as f:
       data = f.read()
   
   with open('audio_data.h', 'w') as f:
       f.write('#ifndef AUDIO_DATA_H\n#define AUDIO_DATA_H\n\n')
       f.write('#include <stdint.h>\n\n')
       f.write(f'const uint32_t audio_data_len = {len(data)};\n')
       f.write('const int32_t audio_data[] = {\n')
       
       for i in range(0, len(data), 4):
           if i > 0 and i % 32 == 0:
               f.write('\n')
           value = int.from_bytes(data[i:i+4], byteorder='little', signed=True)
           f.write(f'0x{value & 0xFFFFFFFF:08X}, ')
       
       f.write('\n};\n\n#endif\n')
   ```

4. **集成到代码**
   ```c
   #include "audio_data.h"
   
   void play_voice_task(void *pvParameters)
   {
       i2s_tx_init();
       max98367a_set_gain(2.0f);
       
       size_t bytes_written;
       esp_err_t ret = i2s_channel_write(tx_handle, 
                                         (void*)audio_data, 
                                         audio_data_len, 
                                         &bytes_written, 
                                         portMAX_DELAY);
       
       if (ret == ESP_OK) {
           ESP_LOGI("AUDIO", "播放完成: %d 字节", bytes_written);
       }
       
       vTaskDelete(NULL);
   }
   ```

### 方法二：使用SPIFFS/SD卡存储音频

如果音频文件较大，建议存储在SPIFFS或SD卡中：

```c
#include "esp_spiffs.h"

void play_from_spiffs(const char *filename)
{
    FILE *f = fopen("/spiffs/voice.raw", "rb");
    if (f == NULL) {
        ESP_LOGE("AUDIO", "无法打开文件");
        return;
    }
    
    int32_t buffer[512];
    size_t bytes_read, bytes_written;
    
    i2s_tx_init();
    max98367a_set_gain(2.0f);
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        max98367a_apply_gain(buffer, bytes_read);
        i2s_channel_write(tx_handle, buffer, bytes_read, 
                         &bytes_written, portMAX_DELAY);
    }
    
    fclose(f);
}
```

### 方法三：使用网络流式播放

从网络服务器实时下载并播放：

```c
#include "esp_http_client.h"

void play_from_url(const char *url)
{
    esp_http_client_config_t config = {
        .url = url,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_open(client, 0);
    
    int32_t buffer[512];
    int read_len;
    size_t bytes_written;
    
    i2s_tx_init();
    max98367a_set_gain(2.0f);
    
    while ((read_len = esp_http_client_read(client, (char*)buffer, sizeof(buffer))) > 0) {
        max98367a_apply_gain(buffer, read_len);
        i2s_channel_write(tx_handle, buffer, read_len, 
                         &bytes_written, portMAX_DELAY);
    }
    
    esp_http_client_cleanup(client);
}
```

## 音频格式要求

| 参数 | 值 |
|------|-----|
| 采样率 | 44100 Hz |
| 位深度 | 32 bit |
| 声道数 | 2 (立体声) |
| 格式 | 有符号整数 (int32_t) |

## 推荐的TTS服务

1. **百度语音** - https://ai.baidu.com/tech/speech
2. **讯飞语音** - https://www.xfyun.cn/
3. **微软Azure TTS** - https://azure.microsoft.com/zh-cn/services/cognitive-services/text-to-speech/
4. **阿里云TTS** - https://ai.aliyun.com/nls/tts

## 注意事项

1. ESP32的Flash容量有限，大音频文件建议使用SD卡或网络流
2. 音频数据会占用较多内存，注意检查可用堆空间
3. 使用`max98367a_set_gain()`函数调整音量，避免音量过大失真
4. 播放前后添加短暂静音，避免爆音

## 调试技巧

- 使用Audacity等工具检查转换后的音频格式
- 先测试简短的音频（1-2秒），确认流程正确
- 检查I2S配置是否与音频格式匹配
- 监控ESP32的日志输出，查看是否有错误信息
