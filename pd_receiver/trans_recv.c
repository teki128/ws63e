#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "app_init.h"
#include "cmsis_os2.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "soc_osal.h"
#include "data_frame.h"
#include "pd_adc_stream.h"
#include "spooky_decoder.h"
#include "wifi_conn.h"

#define UDP_PORT 8889
#define PC_IP "192.168.5.6"
#define RX_ADC_CHANNEL 0U      /* WS63 ADC channel 0 -> GPIO_07 on HH-D02 header */
#define ADC_SAMPLE_RATE 64000U // 通道提供的不够
#define ADC_FRAME_SAMPLES 256U
#define BASELINE_INIT_MV 1800
#define BASELINE_ALPHA_SHIFT 6
#define THRESH_HYST_MV 530
#define RX_QUEUE_DEPTH 10U

static struct spooky_decoder g_decoder;
static uint8_t g_decoder_buffer[255];
static osMessageQueueId_t g_rx_queue;
static pd_adc_stream_handle_t g_adc;
static struct sockaddr_in g_destination;
static int g_socket = -1;

static void frame_received(uint8_t *data, uint8_t size, void *context) // = vlc_rx_cb
{
    (void)context;
    if ((data == NULL) || (size < frame_nbytes) || (data[0] == 0U)) {
        return;
    }
    if (osMessageQueuePut(g_rx_queue, data + 1, 0U, 0U) != osOK) {
        osal_printk("[PD_RX] payload queue full\r\n");
    }
}

static void continuous_adc_task(void *argument) // 具体的init和read在pd_adc_stream.c中实现
{
    uint32_t samples[ADC_FRAME_SAMPLES];
    int32_t baseline = BASELINE_INIT_MV; // TODO: 基线与原基线2048不同
    uint8_t level = 0U;
    pd_adc_stream_config_t config = {
        .channel = RX_ADC_CHANNEL,
        .sample_rate_hz = ADC_SAMPLE_RATE, // TODO: 采样率待修改
        .frame_samples = ADC_FRAME_SAMPLES,
    };
    (void)argument;

    if (spooky_decoder_init(&g_decoder, g_decoder_buffer, sizeof(g_decoder_buffer), frame_received, NULL) !=
        SPOOKY_DECODER_INIT_OK) {
        return;
    }
    if ((pd_adc_stream_init(&config, &g_adc) != ERRCODE_SUCC) || (pd_adc_stream_start(g_adc) != ERRCODE_SUCC)) {
        osal_printk("[PD_RX] ADC stream initialization failed\r\n");
        return;
    }

    osal_printk("[PD_RX] ADC0/GPIO7 started; public-HAL backend is validation-only\r\n");
    for (;;) {
        uint32_t count = 0U;
        if (pd_adc_stream_read(g_adc, samples, ADC_FRAME_SAMPLES, &count, osWaitForever) != ERRCODE_SUCC) {
            (void)osDelay(1U);
            continue;
        }
        for (uint32_t i = 0; i < count; ++i) {
            int32_t diff = (int32_t)samples[i] - baseline;
            int32_t high;
            int32_t low;
            baseline += diff >> BASELINE_ALPHA_SHIFT;
            high = baseline + THRESH_HYST_MV; // TODO: 滞回阈值不同，原阈值为600
            low = baseline - THRESH_HYST_MV;
            if ((level == 0U) && ((int32_t)samples[i] > high)) {
                level = 1U;
            } else if ((level != 0U) && ((int32_t)samples[i] < low)) {
                level = 0U;
            }
            (void)spooky_decoder_step(&g_decoder, level != 0U); // TODO: 需要把电平喂给decoder
        }
    }
}

static void net_send_task(void *argument)
{
    uint8_t payload[frame_load_nbytes];
    (void)argument;
    for (;;) {
        if (osMessageQueueGet(g_rx_queue, payload, NULL, osWaitForever) != osOK) {
            continue;
        }
        if (sendto(g_socket, payload, sizeof(payload), 0, (struct sockaddr *)&g_destination, sizeof(g_destination)) <
            0) {
            osal_printk("[PD_NET] sendto failed: errno=%d\r\n", errno);
        }
    }
}

static osThreadId_t start_thread(const char *name, osThreadFunc_t function, uint32_t stack_size, osPriority_t priority)
{
    osThreadAttr_t attr = {
        .name = name,
        .stack_size = stack_size,
        .priority = priority,
    };
    return osThreadNew(function, NULL, &attr);
}

static void receiver_main(void *argument)
{
    (void)argument;
    g_rx_queue = osMessageQueueNew(RX_QUEUE_DEPTH, frame_load_nbytes, NULL);
    if (g_rx_queue == NULL) {
        return;
    }
    wifi_init_sta();

    (void)memset(&g_destination, 0, sizeof(g_destination));
    g_destination.sin_family = AF_INET;
    g_destination.sin_port = htons(UDP_PORT);
    g_destination.sin_addr.s_addr = inet_addr(PC_IP);
    g_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_socket < 0) {
        osal_printk("[PD_NET] socket failed: errno=%d\r\n", errno);
        return;
    }

    if ((start_thread("pd_rx", continuous_adc_task, 0x2000U, osPriorityAboveNormal) == NULL) ||
        (start_thread("pd_net", net_send_task, 0x1000U, osPriorityNormal) == NULL)) {
        osal_printk("[PD] worker creation failed\r\n");
    }
    return;
}

static void pd_receiver_entry(void)
{
    (void)start_thread("pd_main", receiver_main, 0x2000U, osPriorityNormal);
}

app_run(pd_receiver_entry);
