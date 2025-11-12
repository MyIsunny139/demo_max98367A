# ESP32 + MAX98367A 中文语音播报项目

## 项目简介
本项目基于 ESP32 和 MAX98367A 数字功放模块，实现“我爱你，中国”语音的本地循环播放。适合语音播报、智能硬件等场景。

---

## 硬件连接说明

### 主要元件
- ESP32 开发板（推荐 ESP32-S3 系列）
- MAX98367A I2S 数字功放模块
- 喇叭（8Ω 推荐 1W 及以上）

### 典型接线表

| MAX98367A 引脚 | ESP32 引脚 | 说明         |
|:--------------:|:----------:|:-------------|
| DIN            | GPIO8      | I2S 数据输入  |
| BCLK           | GPIO3      | I2S 时钟      |
| LRC            | GPIO46     | I2S 左右声道  |
| VDD            | 3.3V/5V    | 电源正        |
| GND            | GND        | 电源地        |

> ⚠️ 具体引脚可在 `MAX98367A.h` 头文件中修改。

---

## 软件配置与使用

### 1. 环境准备
- ESP-IDF v5.x 已正确安装
- Python 3.x
- FFmpeg（用于音频格式转换）

### 2. 音频文件准备
1. 使用在线 TTS（如百度、讯飞、微软 Azure）生成“我爱你，中国”语音，下载为 MP3 或 WAV。
2. 运行 `tools/audio_to_c_array.py` 脚本，将音频文件转换为 C 语言数组：
   ```bash
   python tools/audio_to_c_array.py voice.mp3
   ```
3. 将生成的 `audio_data.h` 复制到 `main/` 目录。

### 3. 分区表与 Flash 配置
- 默认分区表已支持大于 1MB 的固件（`partitions.csv`，factory 分区 2M）。
- Flash 大小需设置为 4MB 或更大（`idf.py menuconfig` → Serial Flasher Config → Flash size）。

### 4. 编译与烧录
```bash
idf.py build
idf.py flash monitor
```

---

## 主要代码说明

- `main/demo_max98367A.c` ：主程序，循环播放 audio_data.h 中的语音数据。
- `components/MAX98367A/` ：MAX98367A 驱动代码。
- `tools/audio_to_c_array.py` ：音频转 C 数组工具脚本。
- `partitions.csv` ：分区表，factory 分区已设为 2M。

---

## 常见问题

### 1. 固件过大，编译报错
- 解决：增大分区表 factory 分区（已默认 2M），并将 Flash 设为 4MB 及以上。

### 2. 没有声音/声音失真
- 检查硬件接线，确认 MAX98367A 电源、GND、I2S 信号线无误。
- 调整音量：`max98367a_set_gain(1.0f~3.0f)`。
- 检查音频文件格式，确保为 44100Hz 32bit 立体声。

### 3. 如何更换语音内容？
- 重新生成音频文件，使用工具转换为 C 数组，替换 `audio_data.h`。

---

## 参考资料
- [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/)
- [MAX98367A 数据手册](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98367A-MAX98367B.pdf)
- [FFmpeg 官网](https://ffmpeg.org/)

---

## 联系与支持
如有问题可在本项目 issue 区留言，或联系作者。
