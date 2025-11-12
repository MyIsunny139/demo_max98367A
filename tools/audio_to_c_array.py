#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
音频文件转ESP32数组工具
将音频文件转换为C语言数组格式，用于MAX98367A播放

使用方法：
1. 确保已安装FFmpeg
2. 运行: python audio_to_c_array.py input_audio.mp3
3. 生成的audio_data.h文件可直接包含到ESP32项目中

支持的输入格式：MP3, WAV, M4A等（任何FFmpeg支持的格式）
"""

import sys
import os
import subprocess
import struct

def convert_audio_to_raw(input_file, output_file="temp_audio.raw"):
    """
    使用FFmpeg将音频转换为32bit 44100Hz立体声RAW格式
    """
    print(f"正在转换音频文件: {input_file}")
    
    cmd = [
        'ffmpeg',
        '-i', input_file,
        '-ar', '44100',      # 采样率
        '-ac', '2',          # 立体声
        '-f', 's32le',       # 32位有符号小端
        '-y',                # 覆盖已存在的文件
        output_file
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"FFmpeg错误: {result.stderr}")
            return False
        print(f"转换完成: {output_file}")
        return True
    except FileNotFoundError:
        print("错误: 未找到FFmpeg，请确保已安装FFmpeg并添加到系统PATH")
        return False

def raw_to_c_array(raw_file, header_file="audio_data.h", array_name="audio_data"):
    """
    将RAW音频数据转换为C数组
    """
    print(f"正在生成C头文件: {header_file}")
    
    # 读取RAW数据
    with open(raw_file, 'rb') as f:
        audio_data = f.read()
    
    data_len = len(audio_data)
    print(f"音频数据大小: {data_len} 字节 ({data_len / 1024:.2f} KB)")
    
    # 估算播放时长
    # 44100Hz * 2声道 * 4字节 = 352800字节/秒
    duration_sec = data_len / (44100 * 2 * 4)
    print(f"预计播放时长: {duration_sec:.2f} 秒")
    
    # 生成C头文件
    with open(header_file, 'w', encoding='utf-8') as f:
        f.write('/**\n')
        f.write(' * 自动生成的音频数据文件\n')
        f.write(f' * 数据大小: {data_len} 字节\n')
        f.write(f' * 播放时长: {duration_sec:.2f} 秒\n')
        f.write(' * 采样率: 44100 Hz\n')
        f.write(' * 位深度: 32 bit\n')
        f.write(' * 声道数: 2 (立体声)\n')
        f.write(' */\n\n')
        f.write('#ifndef AUDIO_DATA_H\n')
        f.write('#define AUDIO_DATA_H\n\n')
        f.write('#include <stdint.h>\n\n')
        
        # 数据长度
        f.write(f'const uint32_t {array_name}_len = {data_len};\n\n')
        
        # 音频数据数组
        f.write(f'const int32_t {array_name}[] = {{\n')
        
        # 每行8个数据，便于阅读
        values_per_line = 8
        sample_count = data_len // 4
        
        for i in range(sample_count):
            offset = i * 4
            # 读取4字节作为int32
            value = struct.unpack('<i', audio_data[offset:offset+4])[0]
            
            if i % values_per_line == 0:
                f.write('    ')
            
            f.write(f'0x{value & 0xFFFFFFFF:08X}')
            
            if i < sample_count - 1:
                f.write(', ')
            
            if (i + 1) % values_per_line == 0:
                f.write('\n')
        
        f.write('\n};\n\n')
        f.write('#endif // AUDIO_DATA_H\n')
    
    print(f"C头文件生成完成: {header_file}")
    
    # 输出使用示例
    print("\n" + "="*60)
    print("使用方法：")
    print("="*60)
    print("1. 将生成的 audio_data.h 复制到ESP32项目的main目录")
    print("2. 在代码中包含头文件:")
    print('   #include "audio_data.h"')
    print("\n3. 播放音频:")
    print(f'''
   void play_audio_task(void *pvParameters)
   {{
       i2s_tx_init();
       max98367a_set_gain(2.0f);
       
       size_t bytes_written;
       esp_err_t ret = i2s_channel_write(tx_handle, 
                                         (void*){array_name}, 
                                         {array_name}_len, 
                                         &bytes_written, 
                                         portMAX_DELAY);
       
       if (ret == ESP_OK) {{
           ESP_LOGI("AUDIO", "播放完成: %d 字节", bytes_written);
       }}
       
       vTaskDelete(NULL);
   }}
''')
    print("="*60)

def main():
    if len(sys.argv) < 2:
        print("用法: python audio_to_c_array.py <音频文件> [输出头文件名] [数组名]")
        print("\n示例:")
        print("  python audio_to_c_array.py voice.mp3")
        print("  python audio_to_c_array.py voice.mp3 my_audio.h")
        print("  python audio_to_c_array.py voice.mp3 my_audio.h my_voice_data")
        sys.exit(1)
    
    input_file = sys.argv[1]
    header_file = sys.argv[2] if len(sys.argv) > 2 else "audio_data.h"
    array_name = sys.argv[3] if len(sys.argv) > 3 else "audio_data"
    
    if not os.path.exists(input_file):
        print(f"错误: 文件不存在: {input_file}")
        sys.exit(1)
    
    # 检查文件大小
    file_size = os.path.getsize(input_file)
    print(f"输入文件大小: {file_size / 1024:.2f} KB")
    
    # 临时RAW文件
    temp_raw = "temp_audio.raw"
    
    try:
        # 步骤1: 转换音频格式
        if not convert_audio_to_raw(input_file, temp_raw):
            sys.exit(1)
        
        # 检查转换后的大小
        raw_size = os.path.getsize(temp_raw)
        if raw_size > 1024 * 1024:  # 大于1MB
            print(f"\n警告: 音频数据较大 ({raw_size / 1024:.2f} KB)")
            print("建议:")
            print("  1. 使用更短的音频片段")
            print("  2. 或使用SPIFFS/SD卡存储")
            response = input("是否继续? (y/n): ")
            if response.lower() != 'y':
                print("操作已取消")
                os.remove(temp_raw)
                sys.exit(0)
        
        # 步骤2: 转换为C数组
        raw_to_c_array(temp_raw, header_file, array_name)
        
        print("\n✓ 转换成功!")
        
    finally:
        # 清理临时文件
        if os.path.exists(temp_raw):
            os.remove(temp_raw)
            print(f"已清理临时文件: {temp_raw}")

if __name__ == '__main__':
    main()
