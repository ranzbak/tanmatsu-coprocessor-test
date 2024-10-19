#include "bsp_lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/lock.h>
#include <unistd.h>
#include "core/lv_group.h"
#include "display/lv_display.h"
#include "draw/lv_draw_buf.h"
#include "draw/sw/lv_draw_sw.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "indev/lv_indev.h"
#include "lv_demos.h"
#include "lv_init.h"
#include "lvgl.h"
#include "misc/lv_types.h"
#include "portmacro.h"
#include "sdkconfig.h"
#include "tanmatsu_coprocessor.h"

static char const TAG[] = "bsp-lvgl";

#define LVGL_TICK_PERIOD_MS          2
#define EXAMPLE_LVGL_TASK_STACK_SIZE (64 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY   2

// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;

static uint8_t* rotation_buffer = NULL;

QueueHandle_t key_queue;

void lvgl_lock() {
    _lock_acquire(&lvgl_api_lock);
}

void lvgl_unlock() {
    _lock_release(&lvgl_api_lock);
}

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);

    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    lv_color_format_t cf = lv_display_get_color_format(disp);
    uint32_t w_stride = lv_draw_buf_width_to_stride(w, cf);
    uint32_t h_stride = lv_draw_buf_width_to_stride(h, cf);

    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    int32_t disp_w = lv_display_get_horizontal_resolution(disp);
    int32_t disp_h = lv_display_get_vertical_resolution(disp);

    lv_area_t rotated_area;
    if (rotation == LV_DISPLAY_ROTATION_90) {
        lv_draw_sw_rotate(px_map, rotation_buffer, w, h, w_stride, h_stride, rotation, cf);

        rotated_area.x1 = area->y1;
        rotated_area.y2 = disp_w - area->x1 - 1;
        rotated_area.x2 = rotated_area.x1 + h - 1;
        rotated_area.y1 = rotated_area.y2 - w + 1;
        area = &rotated_area;
    }

    if (rotation == LV_DISPLAY_ROTATION_180) {
        lv_draw_sw_rotate(px_map, rotation_buffer, w, h, w_stride, w_stride, rotation, cf);

        rotated_area.x1 = area->x1;
        rotated_area.y2 = disp_h - area->y1 - 1;
        rotated_area.x2 = area->x2;
        rotated_area.y1 = rotated_area.y2 - h + 1;
        area = &rotated_area;
    }

    if (rotation == LV_DISPLAY_ROTATION_270) {
        lv_draw_sw_rotate(px_map, rotation_buffer, w, h, w_stride, h_stride, rotation, cf);

        rotated_area.x1 = disp_h - area->y2 - 1;
        rotated_area.y2 = area->x2;
        rotated_area.x2 = rotated_area.x1 + h - 1;
        rotated_area.y1 = rotated_area.y2 - w + 1;
        area = &rotated_area;
    }

    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, rotation_buffer);
}

static void increase_lvgl_tick(void* arg) {
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void* arg) {
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        lvgl_lock();
        time_till_next_ms = lv_timer_handler();
        lvgl_unlock();

        // in case of task watch dog timeout, set the minimal delay to 10ms
        if (time_till_next_ms < 10) {
            time_till_next_ms = 10;
        }
        usleep(1000 * time_till_next_ms);
    }
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t* edata,
                                    void* user_ctx) {
    lv_display_t* disp = (lv_display_t*)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

typedef struct {
    uint32_t key;
    lv_state_t state;
} key_event_t;

static void read_keyboard(lv_indev_t* indev, lv_indev_data_t* data) {
    key_event_t event;

    UBaseType_t messages_waiting = uxQueueMessagesWaiting(key_queue);
    if (messages_waiting > 1) {
        data->continue_reading = true;
    }
    if (messages_waiting >= 1) {
        if (xQueueReceive(key_queue, &event, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "EVENT, %lu %u", event.key, event.state);
            data->key = event.key;
            data->state = event.state;
        }
    }
}

void key_to_state(uint8_t pressed, uint32_t key) {
    key_event_t event;
    event.key = key;
    event.state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    xQueueSend(key_queue, &event, portMAX_DELAY);
}

void coprocessor_keyboard_callback(tanmatsu_coprocessor_handle_t handle, tanmatsu_coprocessor_keys_t* prev_keys,
                                   tanmatsu_coprocessor_keys_t* keys) {
    if (keys->key_up != prev_keys->key_up) {
        key_to_state(keys->key_up, LV_KEY_UP);
    }
    if (keys->key_down != prev_keys->key_down) {
        key_to_state(keys->key_down, LV_KEY_DOWN);
    }
    if (keys->key_left != prev_keys->key_left) {
        key_to_state(keys->key_left, LV_KEY_LEFT);
    }
    if (keys->key_right != prev_keys->key_right) {
        key_to_state(keys->key_right, LV_KEY_RIGHT);
    }
    if (keys->key_return != prev_keys->key_return) {
        key_to_state(keys->key_return, LV_KEY_ENTER);
    }
    if (keys->key_esc != prev_keys->key_esc) {
        key_to_state(keys->key_esc, LV_KEY_ESC);
    }
    if (keys->key_tab != prev_keys->key_tab) {
        key_to_state(keys->key_tab, LV_KEY_NEXT);
    }
    if (keys->key_backspace != prev_keys->key_backspace) {
        key_to_state(keys->key_backspace, LV_KEY_BACKSPACE);
    }

    // Keys go brrr
    if (keys->key_a != prev_keys->key_a) {
        key_to_state(keys->key_a, 65);
    }
    if (keys->key_b != prev_keys->key_b) {
        key_to_state(keys->key_b, 66);
    }
    if (keys->key_c != prev_keys->key_c) {
        key_to_state(keys->key_c, 67);
    }
    if (keys->key_d != prev_keys->key_d) {
        key_to_state(keys->key_d, 68);
    }
    if (keys->key_e != prev_keys->key_e) {
        key_to_state(keys->key_e, 69);
    }
    if (keys->key_f != prev_keys->key_f) {
        key_to_state(keys->key_f, 70);
    }
    if (keys->key_g != prev_keys->key_g) {
        key_to_state(keys->key_g, 71);
    }
    if (keys->key_h != prev_keys->key_h) {
        key_to_state(keys->key_h, 72);
    }
    if (keys->key_i != prev_keys->key_i) {
        key_to_state(keys->key_i, 73);
    }
    if (keys->key_j != prev_keys->key_j) {
        key_to_state(keys->key_j, 74);
    }
    if (keys->key_k != prev_keys->key_k) {
        key_to_state(keys->key_k, 75);
    }
    if (keys->key_l != prev_keys->key_l) {
        key_to_state(keys->key_l, 76);
    }
    if (keys->key_m != prev_keys->key_m) {
        key_to_state(keys->key_m, 77);
    }
    if (keys->key_n != prev_keys->key_n) {
        key_to_state(keys->key_n, 78);
    }
    if (keys->key_o != prev_keys->key_o) {
        key_to_state(keys->key_o, 79);
    }
    if (keys->key_p != prev_keys->key_p) {
        key_to_state(keys->key_p, 80);
    }
    if (keys->key_q != prev_keys->key_q) {
        key_to_state(keys->key_q, 81);
    }
    if (keys->key_r != prev_keys->key_r) {
        key_to_state(keys->key_r, 82);
    }
    if (keys->key_s != prev_keys->key_s) {
        key_to_state(keys->key_s, 83);
    }
    if (keys->key_t != prev_keys->key_t) {
        key_to_state(keys->key_t, 84);
    }
    if (keys->key_u != prev_keys->key_u) {
        key_to_state(keys->key_u, 85);
    }
    if (keys->key_v != prev_keys->key_v) {
        key_to_state(keys->key_v, 86);
    }
    if (keys->key_w != prev_keys->key_w) {
        key_to_state(keys->key_w, 87);
    }
    if (keys->key_x != prev_keys->key_x) {
        key_to_state(keys->key_x, 88);
    }
    if (keys->key_y != prev_keys->key_y) {
        key_to_state(keys->key_y, 89);
    }
    if (keys->key_z != prev_keys->key_z) {
        key_to_state(keys->key_z, 90);
    }
}

void lvgl_init(int32_t hres, int32_t vres, esp_lcd_panel_handle_t mipi_dpi_panel) {
    lv_init();

    lv_display_t* display = lv_display_create(hres, vres);

    lv_display_set_user_data(display, mipi_dpi_panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    void* buf1 = NULL;
    void* buf2 = NULL;

    size_t draw_buffer_sz = hres * (vres / 10) * sizeof(lv_color_t);
    buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    assert(buf1);
    buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    assert(buf2);
    rotation_buffer = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM);
    assert(rotation_buffer);

    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    // Set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, lvgl_flush_cb);
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);

    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };

    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(mipi_dpi_panel, &cbs, display));

    const esp_timer_create_args_t lvgl_tick_timer_args = {.callback = &increase_lvgl_tick, .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate(lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    // Set up keyboard input
    key_queue = xQueueCreate(20, sizeof(key_event_t));

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, read_keyboard);

    lv_group_t* group = lv_group_create();
    lv_indev_set_group(indev, group);
    lv_group_set_default(group);
}
