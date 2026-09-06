#ifndef MD_ENCODER_H
#define MD_ENCODER_H

#include <stdint.h>

typedef enum {
    ENC_BUTTON_EVT_NONE,
    ENC_BUTTON_EVT_RELEASED,
    ENC_BUTTON_EVT_DOUBLE_PRESS
} md_encoder_btn_evt_t;

typedef void (*md_encoder_rt_cb_t)(int32_t delta);
typedef void (*md_encoder_btn_cb_t)(md_encoder_btn_evt_t evt);

int md_encoder_rt_init(md_encoder_rt_cb_t cb, int32_t detent);
int md_encoder_btn_init(md_encoder_btn_cb_t cb, uint16_t debounce);

#endif 