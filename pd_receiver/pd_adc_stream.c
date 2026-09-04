#include "pd_adc_stream.h"

#include <stdbool.h>
#include <stddef.h>
#include "adc.h"
#include "tcxo.h"

/*
 * Public-SDK validation backend.
 *
 * The bundled V154 HAL reports at most two samples when auto scan is stopped.
 * This backend deliberately exposes that limitation through the stream API so
 * the application architecture is final. Replace only this file when a
 * continuous FIFO/DMA driver is supplied by the silicon/board vendor.
 */
#define PUBLIC_HAL_WINDOW_US 64U
#define PUBLIC_HAL_MAX_SAMPLES 2U
#define WS63_ADC_CHANNEL_COUNT 6U

struct pd_adc_stream {
    pd_adc_stream_config_t config;
    uint32_t samples[PUBLIC_HAL_MAX_SAMPLES];
    volatile uint32_t count;
    uint32_t overruns;
    bool started;
};

static struct pd_adc_stream g_stream;

static void adc_callback(uint8_t channel, uint32_t *buffer, uint32_t length, bool *next) // 扫描完后cb
{
    uint32_t copy_count;

    if (next != NULL) {
        *next = false;
    }
    if ((channel != g_stream.config.channel) || (buffer == NULL)) {
        return;
    }

    copy_count = (length > PUBLIC_HAL_MAX_SAMPLES) ? PUBLIC_HAL_MAX_SAMPLES : length;
    for (uint32_t i = 0; i < copy_count; ++i) {
        g_stream.samples[i] = buffer[i];
    }
    if (length > copy_count) {
        g_stream.overruns += length - copy_count;
    }
    g_stream.count = copy_count;
}

errcode_t pd_adc_stream_init(const pd_adc_stream_config_t *config,
                             pd_adc_stream_handle_t *handle) // = continuous_adc_init
{
    if ((config == NULL) || (handle == NULL) || (config->channel >= WS63_ADC_CHANNEL_COUNT) ||
        (config->sample_rate_hz == 0U) || (config->frame_samples == 0U)) {
        return ERRCODE_INVALID_PARAM;
    }

    g_stream.config = *config;
    g_stream.count = 0U;
    g_stream.overruns = 0U;
    g_stream.started = false; // TODO: 部分属性未在原始config内

    /* The WS63 V154 port ignores this selector; match the SDK ADC sample. */
    if (uapi_adc_init(ADC_CLOCK_500KHZ) != ERRCODE_SUCC) { // TODO: SDK最高时钟500KHz, 不支持1MHz, 需要底层修改
        return ERRCODE_FAIL;
    }
    uapi_adc_power_en(AFE_GADC_MODE, true);
    *handle = &g_stream;
    return ERRCODE_SUCC;
}

errcode_t pd_adc_stream_start(pd_adc_stream_handle_t handle) // = esp32 api adc_continuous_start
{
    if (handle != &g_stream) {
        return ERRCODE_INVALID_PARAM;
    }
    handle->started = true;
    return ERRCODE_SUCC;
}

errcode_t pd_adc_stream_read(pd_adc_stream_handle_t handle,
                             uint32_t *samples,
                             uint32_t capacity,
                             uint32_t *sample_count,
                             uint32_t timeout) // = esp32 api adc_continuous_read
{
    adc_scan_config_t scan_config = {0};
    errcode_t ret;
    uint32_t count;

    (void)timeout;
    if ((handle != &g_stream) || !handle->started || (samples == NULL) || (sample_count == NULL) || (capacity == 0U)) {
        return ERRCODE_INVALID_PARAM;
    }

    handle->count = 0U;
    scan_config.type = 0U;
    /* V154 currently ignores freq; zero is a valid public enum value. */
    scan_config.freq = 0U;
    ret = uapi_adc_auto_scan_ch_enable(handle->config.channel, scan_config, adc_callback);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    (void)uapi_tcxo_delay_us(PUBLIC_HAL_WINDOW_US);
    ret = uapi_adc_auto_scan_ch_disable(handle->config.channel);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    count = handle->count;
    if (count > capacity) {
        handle->overruns += count - capacity;
        count = capacity;
    }
    for (uint32_t i = 0; i < count; ++i) {
        samples[i] = handle->samples[i];
    }
    *sample_count = count;
    return ERRCODE_SUCC;
}

uint32_t pd_adc_stream_overruns(pd_adc_stream_handle_t handle)
{
    return (handle == &g_stream) ? handle->overruns : 0U;
}
