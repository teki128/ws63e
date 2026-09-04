#pragma once

#include <stdint.h>

#define RELAY 0
#define frame_nbytes (sizeof(data_frame))
#define frame_nbits (8*frame_nbytes)
#define frame_nhalfbits (frame_nbits*2)
typedef union data_frame data_frame;

#define frame_load_nbytes 8
union data_frame
{
    struct{
        uint8_t valid_frame;
        uint8_t load[frame_load_nbytes];
    };
    uint8_t d[1+frame_load_nbytes];
};

data_frame gen_frame(uint8_t *load);
