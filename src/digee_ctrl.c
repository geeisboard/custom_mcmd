#include "digee_ctrl.h"
#include "clocks.h"
#include "digee_ui.h"
// #include "encoder.h"  
#include "md_encoder.h"
#include "buttons.h"
#include "display_hal.h"
#include "midi.h"
#include "zephyr/devicetree.h"
#include "zephyr/kernel.h"
#include "zephyr/sys/atomic_c.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/timing/timing.h>

#define BPM_MAX 240
#define BPM_MIN 40
#define ENC_DETENT 2

#define START_BPM 120
#define START_BINARY 0 // 0 representing 16 btw lol
#define DIVIDE true
#define PAUSE true

static const char version [] = "v1.0.0";
atomic_t encoder_delta = 0;

static midi_dev_t   midi_master;
static midi_dev_t   midi_poly;
static midi_clock_dev_t master_clock;
static midi_clock_dev_t poly_clock;

atomic_t ui_dirty = true;

digee_state_t digee_state = {
    .binary_ui = START_BINARY, // Visual record of the binary, updates on screen immediately, must remain 0 when it represents 16 as we write to it directly with bit shifting
    .divide_s_ui = true,
    .ACTIVE_S = MCMD_ACTIVE_PAUSED,
    .MATH_S = MCMD_MATH_DIVIDED,
    .enc_evt = 0, 
    .clock = {
        .binary = START_BINARY, // Clock record of the binary, updates on loop
        .bpm = START_BPM, // Clock bpm
        .divide_s = DIVIDE,
        .paused = PAUSE
    }
};

// KEEP FOR NOW, WE CAN REMOVE LATER, CONFIRM WORKS WITHOUT THE IF STATEMENT
void digee_toggle_binary_bit(button_id_t btn)
{
    // Bit shift the number to the UI Binary
    digee_state.binary_ui ^= (1U << btn);

    // THIS NEEDS TO MOVE SOMEWHERE LESS INTENSE
    // If paused we can update prev value immediately
    // if (digee_state.clock.paused) {
    //     digee_state.clock.binary = digee_state.binary_ui;
    // }
}

static void on_button_event(button_id_t id, button_evt_t evt)
{
    if (evt != BUTTON_EVT_RELEASED) return;
    digee_state.binary_ui ^= (1U << id);
    atomic_set(&ui_dirty, 1);
}

static void on_encoder_step(int32_t delta)
{
    // printf("Encoder turned\n");
    // printf("%i\n", delta);
    atomic_add(&encoder_delta, delta);
    atomic_set(&ui_dirty, 1);
}

static void on_encoder_push(md_encoder_btn_evt_t evt)
{
    digee_state.enc_evt = evt;
    // printf("Encoder pressed\n");
    // printf("Event: %i\n", evt);
    atomic_set(&ui_dirty, 1);
}

static void on_96step_callback(uint8_t step)
{   
    // Only trigger clock calculation if the binary has changed, OR the division state has changed
    if (digee_state.binary_ui == digee_state.clock.binary && digee_state.divide_s_ui == digee_state.clock.divide_s) return;
    // Move UI changes to the clock
    digee_state.clock.divide_s = digee_state.divide_s_ui;
    digee_state.clock.binary = digee_state.binary_ui;
    // printf("96 ticks completed, triggering sync\n");
    // Start clocks again with new timing
    clock_start(&digee_state.clock);
}

/* Main update tick — call from application thread or main loop. */
void digee_update() 
{
    md_encoder_btn_evt_t current = digee_state.enc_evt;
    digee_state.enc_evt = ENC_BUTTON_EVT_NONE;

    switch(current) {
        case ENC_BUTTON_EVT_NONE:
            break;

        case ENC_BUTTON_EVT_RELEASED:
            switch (digee_state.ACTIVE_S) {
                case MCMD_ACTIVE_PAUSED:
                    digee_state.ACTIVE_S = MCMD_ACTIVE_PLAY;
                    break;

                case MCMD_ACTIVE_PLAYING:
                    digee_state.ACTIVE_S = MCMD_ACTIVE_PAUSE;
                    break;

                default:
                    break;
            }
            break;
        
        case ENC_BUTTON_EVT_DOUBLE_PRESS:
            switch (digee_state.MATH_S) {
                case MCMD_MATH_DIVIDED:
                    digee_state.MATH_S = MCMD_MATH_MULTIPLY;
                    break;

                case MCMD_MATH_MULTIPLIED:
                    digee_state.MATH_S = MCMD_MATH_DIVIDE;
                    break;

                default: 
                    break;
            }
            break;

        default:
            break;
    }

    // If active state is greater than 1, we have an action to fulfill
    if(digee_state.ACTIVE_S > 1) {
        atomic_set(&ui_dirty, 1);
        switch(digee_state.ACTIVE_S) {
            case MCMD_ACTIVE_PAUSE:
                // STOP CLOCKS
                // When pausing, assign clock binary the UI binary since we're not waiting for a 96 step synchronisation
                digee_state.clock.binary = digee_state.binary_ui;
                digee_state.clock.paused = true;
                clock_stop(&digee_state.clock);
                digee_state.ACTIVE_S -= 2;
                printf("PAUSING\n");
                break;
            case MCMD_ACTIVE_PLAY:
                // START CLOCKS
                digee_state.clock.paused = false;
                clock_start(&digee_state.clock);
                digee_state.ACTIVE_S -= 2;
                printf("PLAYING\n");
                break;
            default:
                break;
        }  
    }

    if(digee_state.MATH_S > 1) {
        atomic_set(&ui_dirty, 1);
        switch(digee_state.MATH_S) {
            case MCMD_MATH_DIVIDE:
                digee_state.MATH_S = MCMD_MATH_DIVIDED;
                digee_state.divide_s_ui = true;
                // If paused, we can update clock divide state immediately to 'divide' (true)
                if (digee_state.ACTIVE_S==MCMD_ACTIVE_PAUSED) digee_state.clock.divide_s=true;
                break;

            case MCMD_MATH_MULTIPLY:
                digee_state.MATH_S = MCMD_MATH_MULTIPLIED;
                digee_state.divide_s_ui = false;
                // If paused, we can update clock divide state immediately to 'multiply' (false)
                if (digee_state.ACTIVE_S==MCMD_ACTIVE_PAUSED) digee_state.clock.divide_s=false;
                break;

            default:
                break;
        }
    }
    
    int32_t delta = atomic_clear(&encoder_delta);
    
    if (delta != 0) {
        // printf("checking bpm incrementation: %i\n", delta);
        digee_state.clock.bpm += delta; // Increment bpm immediately
        // Constrain bpm to within MIN/MAX ranges
        if (digee_state.clock.bpm > BPM_MAX) digee_state.clock.bpm = BPM_MAX;
        if (digee_state.clock.bpm < BPM_MIN) digee_state.clock.bpm = BPM_MIN;
        clock_update(&digee_state.clock);
        atomic_set(&ui_dirty, 1);     
    }
}

/* Initialise device state and hardware. Call once at boot. */
void digee_init(void) 
{
    // display_blanking_on();
    display_hal_init();
    k_msleep(200);
    
    ui_boot(version);
    printf("Boot animation complete.\n");
    printf("Initialising...\n");
    // ASSIGN PORTS > INIT CLOCK INFO > MIDI DEVICES > INIT CLOCKS > ASSIGN 96 STEP LOOP CALLBACK
    const struct device *uart_master = DEVICE_DT_GET(DT_ALIAS(midi_master_uart));
    const struct device *uart_poly = DEVICE_DT_GET(DT_ALIAS(midi_poly_uart));
    clock_ctrl_init(&digee_state.clock);
    midi_init(&midi_master, uart_master);
    midi_init(&midi_poly, uart_poly);
    clock_init(&master_clock, &midi_master, MASTER_CLOCK);
    clock_init(&poly_clock, &midi_poly, POLY_CLOCK);
    clock_set_step_callback(on_96step_callback);
    // Init sensors like buttons and encoder
    buttons_init(on_button_event);
    md_encoder_btn_init(on_encoder_push, 50);
    md_encoder_rt_init(on_encoder_step, ENC_DETENT);
    digee_ui_update(); // Draw default values to screen
    printf("Initialisation complete.\n");
}

void digee_ui_update() 
{
    if (atomic_cas(&ui_dirty, 1, 0)) {
        ui_draw_full(&digee_state);
    }
}

void digee_change_playstate()
{
    // Flip clock state
    digee_state.clock.paused = !digee_state.clock.paused;

    if (digee_state.clock.paused) {
        // STOP CLOCKS
        // When pausing, assign clock binary the UI binary since we're not waiting for a 96 step synchronisation
        digee_state.clock.binary = digee_state.binary_ui;
        clock_stop(&digee_state.clock);
    } else {
        // START CLOCKS
        clock_start(&digee_state.clock);
    }
}

void digee_update_clocks() 
{
    clock_update(&digee_state.clock);
}

void digee_change_dividestate()
{
    // digee_state.equation_flag = true;
    digee_state.divide_s_ui = !digee_state.divide_s_ui;
    ui_dirty = true;
}

/* ------------------------------------------------------------------ */
/* State accessors                                                      */
/* ------------------------------------------------------------------ */

/* Returns a read-only pointer to current device state.
 * UI layer uses this to read values for rendering. */
const digee_state_t *digee_get_state() {
    return &digee_state;
}