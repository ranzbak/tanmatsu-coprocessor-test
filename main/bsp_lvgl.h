#include "esp_lcd_types.h"
#include "tanmatsu_coprocessor.h"

void lvgl_lock();
void lvgl_unlock();

void lvgl_init(int32_t hres, int32_t vres, esp_lcd_panel_handle_t mipi_dpi_panel);
void coprocessor_keyboard_callback(tanmatsu_coprocessor_handle_t handle, tanmatsu_coprocessor_keys_t* prev_keys,
                                   tanmatsu_coprocessor_keys_t* keys);