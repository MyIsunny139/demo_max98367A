#include "wss_client.h"
#include <errno.h>
#include "freertos/queue.h"

#define TAG "wss_client"

//? 外部队列：音频数据队列
extern QueueHandle_t audio_data_queue;      // 麦克风 → WebSocket
extern QueueHandle_t audio_playback_queue;  // WebSocket → 扬声器

//? WebSocket全局socket句柄，用于发送和接收任务共享
static int g_websocket_sock = -1;
//? WebSocket配置,用于回调函数
static const wss_client_config_t *g_config = NULL;

//? 静态缓冲区（避免占用任务栈空间）
static uint8_t g_send_audio_buffer[2048];    // 发送任务音频缓冲区
static uint8_t g_send_frame_buffer[2200];    // 发送任务WebSocket帧缓冲区
static uint8_t g_recv_buffer[2200];          // 接收任务数据缓冲区

//? 组包 WebSocket 二进制帧，返回帧长度
static size_t build_websocket_binary_frame(const uint8_t *data, size_t data_len, uint8_t *frame_buf, size_t buf_size) 
{
    size_t header_len = 6;  // 基础头：2字节 + 4字节掩码
    
    //? 根据数据长度确定帧头大小
    if (data_len <= 125)
    {
        header_len = 6;  // 2 + 4
    }
    else if (data_len <= 65535)
    {
        header_len = 8;  // 2 + 2(扩展长度) + 4
    }
    else
    {
        header_len = 14;  // 2 + 8(扩展长度) + 4
    }
    
    //? 检查缓冲区大小
    if (buf_size < data_len + header_len) 
    {
        ESP_LOGE(TAG, "Buffer too small: need %d, have %d", data_len + header_len, buf_size);
        return 0;
    }
    
    //? 第一个字节：FIN + OpCode
    frame_buf[0] = 0x82;  // FIN=1, RSV=0, OpCode=2 (binary)
    
    //? 第二个字节及扩展长度
    int mask_offset = 2;
    if (data_len <= 125)
    {
        frame_buf[1] = 0x80 | data_len;  // MASK=1 + payload length
    }
    else if (data_len <= 65535)
    {
        frame_buf[1] = 0x80 | 126;  // MASK=1 + 126 (使用扩展长度)
        frame_buf[2] = (data_len >> 8) & 0xFF;  // 高字节
        frame_buf[3] = data_len & 0xFF;         // 低字节
        mask_offset = 4;
    }
    else
    {
        frame_buf[1] = 0x80 | 127;  // MASK=1 + 127 (使用64位扩展长度)
        //? 填充 8 字节长度（大端序）
        for (int i = 0; i < 8; i++)
        {
            frame_buf[2 + i] = (data_len >> (56 - i * 8)) & 0xFF;
        }
        mask_offset = 10;
    }
    
    //? 生成随机掩码
    uint8_t mask[4];
    for (int i = 0; i < 4; ++i) 
    {
        mask[i] = esp_random() & 0xFF;
    }
    memcpy(&frame_buf[mask_offset], mask, 4);
    
    //? 对数据进行掩码处理
    int data_offset = mask_offset + 4;
    for (size_t i = 0; i < data_len; ++i) 
    {
        frame_buf[data_offset + i] = data[i] ^ mask[i % 4];
    }
    
    return data_offset + data_len;
}

//? 组包 WebSocket 文本帧，返回帧长度
static size_t build_websocket_frame(const char *msg, uint8_t *frame_buf, size_t buf_size) 
{
    size_t msg_len = strlen(msg);
    if (buf_size < msg_len + 6) 
    {
        return 0;   // 缓冲区不足
    }
    
    frame_buf[0] = 0x81;                        // FIN + text frame
    frame_buf[1] = 0x80 | (msg_len & 0x7F);     // MASK bit + payload len
    
    //? 生成随机掩码
    uint8_t mask[4];
    for (int i = 0; i < 4; ++i) 
    {
        mask[i] = esp_random() & 0xFF;
    }
    memcpy(&frame_buf[2], mask, 4);
    
    //? 对消息内容进行掩码处理
    for (size_t i = 0; i < msg_len; ++i) 
    {
        frame_buf[6 + i] = msg[i] ^ mask[i % 4];
    }
    
    return msg_len + 6;
}

//? 解析WebSocket URI，提取主机名、端口和路径
static void parse_websocket_uri(const char *uri, char *host, int *port, char *path)
{
    const char *p = uri + strlen("ws://");
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    
    *port = 80;     // 默认端口
    strcpy(path, "/");  // 默认路径
    
    if (colon && (!slash || colon < slash)) 
    {
        strncpy(host, p, colon - p);
        *port = atoi(colon + 1);
        if (slash) 
        {
            strncpy(path, slash, 127);
        }
    } 
    else 
    {
        if (slash) 
        {
            strncpy(host, p, slash - p);
            strncpy(path, slash, 127);
        } 
        else 
        {
            strncpy(host, p, 127);
        }
    }
}

//? WebSocket握手，返回socket文件描述符，失败返回-1
static int websocket_handshake(const char *uri)
{
    char host[128] = {0};
    char path[128] = {0};
    int port = 0;
    
    //? 解析URI
    parse_websocket_uri(uri, host, &port, path);
    ESP_LOGI(TAG, "Connecting to %s:%d%s", host, port, path);
    
    //? DNS解析
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    int retry = 0;
    while (getaddrinfo(host, NULL, &hints, &res) != 0 && retry < WSS_DNS_MAX_RETRY) 
    {
        ESP_LOGW(TAG, "DNS lookup failed, retry %d/%d", retry + 1, WSS_DNS_MAX_RETRY);
        vTaskDelay(pdMS_TO_TICKS(WSS_DNS_RETRY_INTERVAL_MS));
        retry++;
    }
    
    if (retry >= WSS_DNS_MAX_RETRY)
    {
        ESP_LOGE(TAG, "DNS lookup failed after %d retries", WSS_DNS_MAX_RETRY);
        return -1;
    }
    
    //? 创建socket
    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    addr->sin_port = htons(port);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) 
    {
        ESP_LOGE(TAG, "Socket create failed");
        freeaddrinfo(res);
        return -1;
    }
    
    //? 设置socket超时
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    //? 连接到服务器
    ESP_LOGI(TAG, "Attempting to connect to %s:%d", host, port);
    if (connect(sock, (struct sockaddr *)addr, sizeof(struct sockaddr_in)) != 0) 
    {
        ESP_LOGE(TAG, "Socket connect failed, errno: %d", errno);
        close(sock);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    
    ESP_LOGI(TAG, "Socket connected successfully");
    
    //? 发送WebSocket握手请求
    char req[512];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, port);
    
    if (send(sock, req, strlen(req), 0) <= 0)
    {
        ESP_LOGE(TAG, "Failed to send handshake request");
        close(sock);
        return -1;
    }
    
    //? 接收握手响应
    char resp[512];
    int len = recv(sock, resp, sizeof(resp)-1, 0);
    if (len <= 0) 
    {
        ESP_LOGE(TAG, "Handshake failed, no response");
        close(sock);
        return -1;
    }
    resp[len] = 0;
    ESP_LOGI(TAG, "WebSocket connected successfully");
    
    return sock;
}


//? WebSocket发送任务
static void wss_send_task(void *param)
{
    int send_count = 0;
    
    while (1)
    {
        //? 检查socket是否有效
        if (g_websocket_sock < 0)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            send_count = 0;  // 重置计数器
            continue;
        }
        
        //? 从队列读取完整音频帧（阻塞，超时100ms）
        if (xQueueReceive(audio_data_queue, g_send_audio_buffer, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            //? 将音频数据封装成WebSocket二进制帧
            size_t frame_len = build_websocket_binary_frame(g_send_audio_buffer, 2048, g_send_frame_buffer, 2200);
            if (frame_len > 0) 
            {
                int ret = send(g_websocket_sock, g_send_frame_buffer, frame_len, 0);
                if (ret > 0)
                {
                    send_count++;
                    if (send_count % 50 == 0)  // 每 50 个包打印一次日志
                    {
                        ESP_LOGI(TAG, "Sent %d audio frames (frame_len=%zu, data_size=2048)", send_count, frame_len);
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "Send failed after %d packets, errno: %d", send_count, errno);
                    g_websocket_sock = -1;  // 标记连接断开，触发重连
                }
            }
            else
            {
                ESP_LOGE(TAG, "Failed to build WebSocket frame");
            }
            vTaskDelay(pdMS_TO_TICKS(3));
        }
        else
        {
            //? 队列为空，避免空转占用CPU
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    ESP_LOGI(TAG, "wss_send_task ended");
    vTaskDelete(NULL);
}

//? WebSocket接收任务
static void wss_recv_task(void *param)
{
    int recv_count = 0;
    
    while (1)
    {
        //? 检查socket是否有效
        if (g_websocket_sock < 0)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        //? 接收服务器返回的消息（回显的音频数据）
        uint8_t hdr[2];
        int r = recv(g_websocket_sock, hdr, 2, 0);
        if (r <= 0) 
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGW(TAG, "Connection closed by server, errno: %d", errno);
            g_websocket_sock = -1;  // 标记连接断开，触发重连
            continue;  // 继续循环，等待重连
        }
        
        //? 解析WebSocket帧头
        uint8_t opcode = hdr[0] & 0x0F;
        int payload_len = hdr[1] & 0x7F;
        
        //? 处理扩展长度（如果 payload_len == 126 或 127）
        if (payload_len == 126) 
        {
            uint8_t len_ext[2];
            r = recv(g_websocket_sock, len_ext, 2, 0);
            if (r != 2) continue;
            payload_len = (len_ext[0] << 8) | len_ext[1];
        }
        else if (payload_len == 127)
        {
            //? 跳过 8 字节长度（不支持超大帧）
            uint8_t len_ext[8];
            recv(g_websocket_sock, len_ext, 8, 0);
            ESP_LOGW(TAG, "Payload too large, skipping");
            continue;
        }
        
        //? 接收数据（分块接收大数据）
        if (payload_len > 0 && payload_len <= 2200) 
        {
            int received = 0;
            while (received < payload_len)
            {
                r = recv(g_websocket_sock, g_recv_buffer + received, payload_len - received, 0);
                if (r <= 0) 
                {
                    //? 检查是否是临时错误
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        continue;  // 继续尝试接收
                    }
                    ESP_LOGE(TAG, "Recv data failed, errno: %d", errno);
                    g_websocket_sock = -1;
                    break;
                }
                received += r;
            }
            
            if (received == payload_len)
            {
                //? 根据 opcode 处理数据
                if (opcode == 0x01)  // 文本帧
                {
                    if (g_config && g_config->on_message) 
                    {
                        g_recv_buffer[received] = 0;  // 添加字符串结束符
                        g_config->on_message((char *)g_recv_buffer, received);
                    }
                }
                else if (opcode == 0x02)  // 二进制帧（回显的音频数据）
                {
                    recv_count++;
                    
                    //? 只处理完整的2048字节音频帧
                    if (received == 2048) {
                        //? 将完整音频帧放入播放队列
                        if (audio_playback_queue != NULL) {
                            //? 发送到播放队列，如果队列满则等待50ms
                            if (xQueueSend(audio_playback_queue, g_recv_buffer, pdMS_TO_TICKS(50)) != pdPASS) {
                                ESP_LOGW(TAG, "Playback queue full, dropping audio frame");
                            } else {
                                // if (recv_count % 50 == 0) {
                                //     ESP_LOGI(TAG, "Received %d audio frames (2048 bytes each)", recv_count);
                                // }
                            }
                        }
                    } else {
                        ESP_LOGW(TAG, "Received incomplete audio frame: %d bytes (expected 2048)", received);
                    }
                }
                else if (opcode == 0x08)  // 关闭帧
                {
                    ESP_LOGW(TAG, "Server sent close frame");
                    g_websocket_sock = -1;
                }
            }
        }
        else if (payload_len > 2200)
        {
            //? 数据过大，分批丢弃
            ESP_LOGW(TAG, "Payload too large (%d bytes), discarding", payload_len);
            uint8_t tmp[128];
            int remaining = payload_len;
            while (remaining > 0)
            {
                int to_read = (remaining > sizeof(tmp)) ? sizeof(tmp) : remaining;
                r = recv(g_websocket_sock, tmp, to_read, 0);
                if (r <= 0) break;
                remaining -= r;
            }
        }
    }
    
    ESP_LOGI(TAG, "wss_recv_task ended");
    vTaskDelete(NULL);
}

static void wss_client_task(void *param) 
{
    const wss_client_config_t *config = (const wss_client_config_t *)param;
    g_config = config;  // 保存配置供其他任务使用
    
    //? 参数有效性检查
    if (!config || !config->uri) 
    {
        ESP_LOGE(TAG, "Invalid WebSocket configuration");
        vTaskDelete(NULL);
        return;
    }

    //? 检查URI格式，仅支持 ws:// 协议
    if (strncmp(config->uri, "ws://", 5) != 0) 
    {
        ESP_LOGE(TAG, "Only ws:// protocol is supported");
        vTaskDelete(NULL);
        return;
    }

    //? 主循环：支持断线自动重连
    while (1)
    {
        //? 执行WebSocket握手连接，支持重试
        int sock = -1;
        int retry_count = 0;
        
        ESP_LOGI(TAG, "Attempting to connect to WebSocket server...");
        
        while (sock < 0 && retry_count < WSS_HANDSHAKE_MAX_RETRY)
        {
            if (retry_count > 0)
            {
                ESP_LOGW(TAG, "Retry connection %d/%d", retry_count, WSS_HANDSHAKE_MAX_RETRY);
                vTaskDelay(pdMS_TO_TICKS(WSS_HANDSHAKE_RETRY_INTERVAL_MS));
            }
            
            sock = websocket_handshake(config->uri);
            retry_count++;
        }
        
        if (sock < 0)
        {
            ESP_LOGE(TAG, "WebSocket handshake failed after %d retries, will retry in %d ms", 
                     WSS_HANDSHAKE_MAX_RETRY, WSS_RECONNECT_FAILED_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(WSS_RECONNECT_FAILED_DELAY_MS));
            continue;  // 继续外层循环，重新尝试连接
        }
        
        g_websocket_sock = sock;    // 保存socket供其他任务使用
        ESP_LOGI(TAG, "WebSocket connected successfully");
        
        //? 等待连接断开（socket被置为-1表示断开）
        while (g_websocket_sock >= 0)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        //? 连接已断开，关闭socket
        if (sock >= 0)
        {
            close(sock);
            ESP_LOGW(TAG, "WebSocket connection closed, will reconnect in %d ms...", WSS_RECONNECT_DELAY_MS);
        }
        
        //? 等待后自动重连
        vTaskDelay(pdMS_TO_TICKS(WSS_RECONNECT_DELAY_MS));
    }
    
    //? 理论上不会到达这里
    vTaskDelete(NULL);
}

void wss_client_start(const wss_client_config_t *config) 
{
    if (!config || !config->uri)
    {
        ESP_LOGE(TAG, "Invalid config");
        return;
    }
    
    //? 在Core 1上创建WebSocket主任务
    xTaskCreatePinnedToCore(wss_client_task, "wss_client", 8192, (void *)config, 3, NULL, 1);
    ESP_LOGI(TAG, "wss_client_task created");
    
    //? 创建发送任务（持久运行，自动适应连接状态）
    xTaskCreatePinnedToCore(wss_send_task, "wss_send", 4096, NULL, 10, NULL, 1);
    ESP_LOGI(TAG, "wss_send_task created");
    
    //? 创建接收任务（持久运行，自动适应连接状态）
    xTaskCreatePinnedToCore(wss_recv_task, "wss_recv", 4096, NULL, 10, NULL, 0);
    ESP_LOGI(TAG, "wss_recv_task created");
}

