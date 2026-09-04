#pragma once

#include <stdint.h>
#include "errcode.h"

typedef struct pd_adc_stream *pd_adc_stream_handle_t;

typedef struct {
    uint8_t channel;
    uint32_t sample_rate_hz;
    uint32_t frame_samples;
} pd_adc_stream_config_t;

errcode_t pd_adc_stream_init(const pd_adc_stream_config_t *config, pd_adc_stream_handle_t *handle);
errcode_t pd_adc_stream_start(pd_adc_stream_handle_t handle);
errcode_t pd_adc_stream_read(pd_adc_stream_handle_t handle, uint32_t *samples,
    uint32_t capacity, uint32_t *sample_count, uint32_t timeout);
uint32_t pd_adc_stream_overruns(pd_adc_stream_handle_t handle);
