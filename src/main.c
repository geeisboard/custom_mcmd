#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include "digee_ctrl.h"
#include "clocks.h"
#include <zephyr/drivers/sensor.h>


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(i2c1)
#define UPDATE_DELAY 100

int main(void)
{   
    
    // k_msleep(2000); /* wait for serial monitor to connect */
    printf("Application started\n");

    digee_init();
    midi_clock_timer_init();

    uint32_t last_update = 0;

    while (1) {
        // midi_clock_poll();
        
        uint32_t now = k_uptime_get_32();
        if ((now - last_update) >= UPDATE_DELAY) {
            last_update = now;
            digee_update();
            digee_ui_update();
            
        }
    }
    return 0;
}