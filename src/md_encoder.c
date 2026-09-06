#include "md_encoder.h"
#include "zephyr/kernel.h"
#include <stdint.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define ENC_NODE DT_NODELABEL(md_encoder)
#define DOUBLE_CLICK_WINDOW 300

// Spec
static const struct gpio_dt_spec pin_a = GPIO_DT_SPEC_GET_BY_IDX(ENC_NODE, gpios, 0);
static const struct gpio_dt_spec pin_b = GPIO_DT_SPEC_GET_BY_IDX(ENC_NODE, gpios, 1);
static const struct gpio_dt_spec pin_sw = GPIO_DT_SPEC_GET_BY_IDX(ENC_NODE, gpios, 2);

static struct gpio_callback gpio_cb_a;
static struct gpio_callback gpio_cb_b;
static struct gpio_callback gpio_cb_sw;

// BTN Variables
static md_encoder_btn_cb_t enc_btn_cb;
static uint16_t debounce_ms;
static bool pending_single;
static uint64_t last_btn_edge_time_ms;
static struct k_timer click_timer;

// RT Variables
static md_encoder_rt_cb_t enc_rt_cb;
static int32_t enc_detent;
static uint8_t last_state;
static int32_t accum;
static int64_t last_rt_edge_time_ms;

static const int8_t qdec_table[16] = {
    0,  -1,  1,  0,
    1,  0,   0,  -1,
    -1, 0,  0, 1,
   0, 1,  -1,0,
};

static void encoder_rt_edge(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    
    int64_t now = k_uptime_get();
    if ((now - last_rt_edge_time_ms) < 2) {   /* tune: start at 2ms */
        return;
    }
    last_rt_edge_time_ms = now;
    
    uint8_t a = gpio_pin_get_dt(&pin_a);
    uint8_t b = gpio_pin_get_dt(&pin_b);
    uint8_t new_state = (a << 1) | b;

    uint8_t index = (last_state << 2) | new_state;
    accum += qdec_table[index];
    last_state = new_state;
    // printf("encoder turn detected\n");
    // printf("accum=%i\n",accum);
    // printf("detent=%i\n",enc_detent);
    if (accum >= enc_detent) {
        accum = 0;
        if (enc_rt_cb) {
            // printf("returning 1\n");
            enc_rt_cb(1);      /* one detent clockwise */
        }
    } else if (accum <= -enc_detent) {
        accum = 0;
        if (enc_rt_cb) {
            // printf("returning -1\n");
            enc_rt_cb(-1);     /* one detent counter-clockwise */
        }
    }
}

static void encoder_sw_edge(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    int64_t now = k_uptime_get();
    // printf("encoder press detected\n");
    if ((now - last_btn_edge_time_ms) < debounce_ms) {
        return;   /* bounce, ignore */
    }
    last_btn_edge_time_ms = now;

    /* active-low: pin reads 0 while pressed, 1 on release */
    if (gpio_pin_get_dt(&pin_sw) == 0) {
        return;   /* this edge was the press, only act on release */
    }

    if (pending_single) {
        /* second release within the window -> double click */
        k_timer_stop(&click_timer);
        pending_single = false;
        // printf("returning double press\n");
        if (enc_btn_cb) enc_btn_cb(ENC_BUTTON_EVT_DOUBLE_PRESS);
    } else {
        /* first release -> wait to see if a second one follows */
        pending_single = true;
        k_timer_start(&click_timer, K_MSEC(DOUBLE_CLICK_WINDOW), K_NO_WAIT);
    }
}


static void click_timer_expired(struct k_timer *timer)
{
    if (pending_single) {
        pending_single = false;
        // printf("returning single press\n");
        if (enc_btn_cb) enc_btn_cb(ENC_BUTTON_EVT_RELEASED);
    }
}

int md_encoder_btn_init(md_encoder_btn_cb_t cb, uint16_t debounce) {
    if (!cb) return 1;

    if(!device_is_ready(pin_sw.port)) {
        // printf("encoder pin_sw not ready");
        return -1;
    }

    // Assign callback and setup
    enc_btn_cb = cb;
    debounce_ms = debounce;
    pending_single = false;

    k_timer_init(&click_timer, click_timer_expired, NULL);

    int err;

    err = gpio_pin_configure_dt(&pin_sw, GPIO_INPUT);
    if (err) return err;

    err = gpio_pin_interrupt_configure_dt(&pin_sw, GPIO_INT_EDGE_BOTH);
    if (err) return err;

    gpio_init_callback(&gpio_cb_sw, encoder_sw_edge, BIT(pin_sw.pin));
    gpio_add_callback(pin_sw.port, &gpio_cb_sw);

    return 0;
}

int md_encoder_rt_init(md_encoder_rt_cb_t cb, int32_t detent) {
    
    if (!cb) return 1;

    if(!device_is_ready(pin_a.port)) {
        printf("encoder pin_a not ready\n");
        return -1;
    }
    
    if(!device_is_ready(pin_b.port)) {
        printf("encoder pin_b not ready\n");
        return -1;
    }
   
    // Assign callback
    enc_rt_cb = cb;
    enc_detent = detent;
    accum = 0;

    //Note that success returns a 0 (false) in this case because wouldn't that be really confusing and awesome
    int err;

    err = gpio_pin_configure_dt(&pin_a, GPIO_INPUT);
    if (err) return err;

    err = gpio_pin_configure_dt(&pin_b, GPIO_INPUT);
    if (err) return err;

    last_state = (gpio_pin_get_dt(&pin_a) << 1) | gpio_pin_get_dt(&pin_b);

    err = gpio_pin_interrupt_configure_dt(&pin_a, GPIO_INT_EDGE_BOTH);
    if (err) return err;

    err = gpio_pin_interrupt_configure_dt(&pin_b, GPIO_INT_EDGE_BOTH);
    if (err) return err;

    gpio_init_callback(&gpio_cb_a, encoder_rt_edge, BIT(pin_a.pin));
    gpio_init_callback(&gpio_cb_b, encoder_rt_edge, BIT(pin_b.pin));

    gpio_add_callback(pin_a.port, &gpio_cb_a);
    gpio_add_callback(pin_b.port, &gpio_cb_b);
    
    return 0;
}




    
