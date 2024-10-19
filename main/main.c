/*
 * SPDX-FileCopyrightText: 2024 Nicolai Electronics
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "bsp_lvgl.h"
#include "core/lv_group.h"
#include "core/lv_obj.h"
#include "core/lv_obj_event.h"
#include "core/lv_obj_pos.h"
#include "core/lv_obj_style.h"
#include "core/lv_obj_style_gen.h"
#include "core/lv_obj_tree.h"
#include "display/lv_display.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "dsi_panel_nicolaielectronics_st7701.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "font/lv_font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "layouts/flex/lv_flex.h"
#include "libs/freetype/lv_freetype.h"
#include "lv_demos.h"
#include "lv_examples.h"
#include "misc/lv_area.h"
#include "misc/lv_event.h"
#include "misc/lv_style.h"
#include "misc/lv_style_gen.h"
#include "nvs.h"
#include "others/gridnav/lv_gridnav.h"
#include "sdkconfig.h"
#include "soc/gpio_num.h"
#include "tanmatsu_coprocessor.h"
#include "widgets/button/lv_button.h"
#include "widgets/checkbox/lv_checkbox.h"
#include "widgets/image/lv_image.h"
#include "widgets/label/lv_label.h"
#include "widgets/list/lv_list.h"
#include "widgets/msgbox/lv_msgbox.h"
#include "widgets/textarea/lv_textarea.h"

// clang-format off
#include "lv_api_map_v8.h"
// clang-format on

#define PANEL_MIPI_DSI_PHY_PWR_LDO_CHAN       3  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define PANEL_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL         1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL        !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_RST               -1  // 14 Doesn't work for some reason'
#define EXAMPLE_DISPLAY_TYPE                  DISPLAY_TYPE_ST7701

static const char* TAG = "example";

i2c_master_bus_config_t i2c_master_config_internal = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = 0,
    .scl_io_num = 10,
    .sda_io_num = 9,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};

i2c_master_bus_handle_t i2c_bus_handle_internal = NULL;

tanmatsu_coprocessor_handle_t coprocessor_handle = NULL;

SemaphoreHandle_t i2c_concurrency_semaphore = NULL;

void example_initialize_i2c_bus() {
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_master_config_internal, &i2c_bus_handle_internal));
    i2c_concurrency_semaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(i2c_concurrency_semaphore);
}

static int i2c_scan(i2c_master_bus_handle_t bus_handle) {
    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            esp_err_t ret = i2c_master_probe(bus_handle, address, 50);
            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }

    return 0;
}

void example_bsp_enable_dsi_phy_power(void) {
    // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef PANEL_MIPI_DSI_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = PANEL_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = PANEL_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif
}

/*void coprocessor_keyboard_callback(tanmatsu_coprocessor_handle_t handle, tanmatsu_coprocessor_keys_t* prev_keys,
                                   tanmatsu_coprocessor_keys_t* keys) {
    printf("Keyboard state changed: %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n", keys->raw[0], keys->raw[1],
           keys->raw[2], keys->raw[3], keys->raw[4], keys->raw[5], keys->raw[6], keys->raw[7], keys->raw[8]);
    if (keys->key_esc) {
        printf("ESC ");
    }
    if (!prev_keys->key_f1 && keys->key_f1) {
        printf("F1 ");
        tanmatsu_coprocessor_radio_disable(handle);
    }
    if (!prev_keys->key_f2 && keys->key_f2) {
        printf("F2 ");
        tanmatsu_coprocessor_radio_enable_application(handle);
    }
    if (!prev_keys->key_f3 && keys->key_f3) {
        printf("F3 ");
        tanmatsu_coprocessor_radio_enable_bootloader(handle);
    }
    if (keys->key_tilde) {
        printf("~ ");
    }
    if (keys->key_1) {
        printf("1 ");
        tanmatsu_coprocessor_set_pmic_otg_control(coprocessor_handle, false);
    }
    if (keys->key_2) {
        printf("2 ");
        tanmatsu_coprocessor_set_pmic_otg_control(coprocessor_handle, true);
    }
    if (keys->key_3) {
        printf("3 ");
    }
    if (keys->key_tab) {
        printf("TAB ");
    }
    if (keys->key_q) {
        printf("q ");
    }
    if (keys->key_w) {
        printf("w ");
    }
    if (keys->key_e) {
        printf("e ");
    }
    if (keys->key_fn) {
        printf("FN ");
    }
    if (keys->key_a) {
        printf("a ");
    }
    if (keys->key_s) {
        printf("s ");
    }
    if (keys->key_d) {
        printf("d ");
    }
    if (keys->key_shift_l) {
        printf("SHIFT_L ");
    }
    if (keys->key_z) {
        printf("Z ");
    }
    if (keys->key_x) {
        printf("X ");
    }
    if (keys->key_c) {
        printf("C ");
    }
    if (keys->key_ctrl) {
        printf("CTRL ");
    }
    if (keys->key_meta) {
        printf("META ");
    }
    if (keys->key_alt_l) {
        printf("ALT_L ");
    }
    if (keys->key_backslash) {
        printf("BACKSLASH ");
    }
    if (keys->key_4) {
        printf("4 ");
    }
    if (keys->key_5) {
        printf("5 ");
    }
    if (keys->key_6) {
        printf("6 ");
    }
    if (keys->key_7) {
        printf("7 ");
    }
    if (keys->key_r) {
        printf("r ");
    }
    if (keys->key_t) {
        printf("t ");
    }
    if (keys->key_y) {
        printf("y ");
    }
    if (keys->key_u) {
        printf("u ");
    }
    if (keys->key_f) {
        printf("f ");
    }
    if (keys->key_g) {
        printf("g ");
    }
    if (keys->key_h) {
        printf("h ");
    }
    if (keys->key_j) {
        printf("j ");
    }
    if (keys->key_v) {
        printf("v ");
    }
    if (keys->key_b) {
        printf("b ");
    }
    if (keys->key_n) {
        printf("n ");
    }
    if (keys->key_m) {
        printf("m ");
    }
    if (keys->key_f4) {
        printf("F4 ");
        tanmatsu_coprocessor_set_pmic_charging_control(coprocessor_handle, true, 0);
    }
    if (keys->key_f5) {
        printf("F5 ");
        tanmatsu_coprocessor_set_pmic_charging_control(coprocessor_handle, false, 1);
    }
    if (keys->key_f6) {
        printf("F6 ");
        tanmatsu_coprocessor_set_pmic_charging_control(coprocessor_handle, false, 2);
    }
    if (keys->key_backspace) {
        printf("BACKSPACE ");
        tanmatsu_coprocessor_set_pmic_charging_control(coprocessor_handle, false, 3);
    }
    if (keys->key_9) {
        printf("9 ");
    }
    if (keys->key_0) {
        printf("0 ");
    }
    if (keys->key_minus) {
        printf("- ");
    }
    if (keys->key_equals) {
        printf("= ");
    }
    if (keys->key_o) {
        printf("o ");
    }
    if (keys->key_p) {
        printf("p ");
    }
    if (keys->key_sqbracket_open) {
        printf("[ ");
    }
    if (keys->key_sqbracket_close) {
        printf("] ");
    }
    if (keys->key_l) {
        printf("l ");
    }
    if (keys->key_semicolon) {
        printf("; ");
    }
    if (keys->key_quote) {
        printf("' ");
    }
    if (keys->key_return) {
        printf("RETURN ");
    }
    if (keys->key_dot) {
        printf(". ");
    }
    if (keys->key_slash) {
        printf("/ ");
    }
    if (keys->key_up) {
        printf("UP ");
    }
    if (keys->key_shift_r) {
        printf("SHIFT_R ");
    }
    if (keys->key_alt_r) {
        printf("ALT_R ");
    }
    if (keys->key_left) {
        printf("LEFT ");
    }
    if (keys->key_down) {
        printf("DOWN ");
    }
    if (keys->key_right) {
        printf("RIGHT ");
    }
    if (keys->key_8) {
        printf("8 ");
    }
    if (keys->key_i) {
        printf("i ");
    }
    if (keys->key_k) {
        printf("k ");
    }
    if (keys->key_comma) {
        printf(", ");
    }
    if (keys->key_space_l) {
        printf("SPACE_L ");
    }
    if (keys->key_space_m) {
        printf("SPACE_M ");
    }
    if (keys->key_space_r) {
        printf("SPACE_R ");
    }
    if (keys->key_volume_up) {
        printf("VOLUME_UP ");
    }
    printf("\r\n");
}*/

void coprocessor_input_callback(tanmatsu_coprocessor_handle_t handle, tanmatsu_coprocessor_inputs_t* prev_inputs,
                                tanmatsu_coprocessor_inputs_t* inputs) {
    if (inputs->sd_card_detect != prev_inputs->sd_card_detect) {
        ESP_LOGW(TAG, "SD card %s", inputs->sd_card_detect ? "inserted" : "removed");
    }

    if (inputs->headphone_detect != prev_inputs->headphone_detect) {
        ESP_LOGW(TAG, "Audio jack %s", inputs->headphone_detect ? "inserted" : "removed");
    }

    if (inputs->power_button != prev_inputs->power_button) {
        ESP_LOGW(TAG, "Power button %s", inputs->power_button ? "pressed" : "released");
    }
}

static void show_error(char* error) {
    lv_obj_t* box = lv_msgbox_create(NULL);

    lv_msgbox_add_title(box, "Error");
    lv_msgbox_add_text(box, error);
    lv_obj_t* close_button = lv_msgbox_add_close_button(box);
    lv_group_focus_obj(close_button);
    lv_group_focus_freeze(lv_group_get_default(), true);
}

static void key_event_cb(lv_event_t* e) {
    uint32_t k = lv_event_get_key(e);

    if (k == LV_KEY_ESC) {
        // tbd
    }
}

uint8_t charging_current = 0;
bool charging_enabled = true;

static void enable_charging_cb(lv_event_t* event) {
    lv_obj_t* obj = lv_event_get_target(event);
    bool checked = lv_obj_get_state(obj) & LV_STATE_CHECKED;
    charging_enabled = checked;
    tanmatsu_coprocessor_set_pmic_charging_control(coprocessor_handle, !charging_enabled, charging_current);
}

static void enable_otg_cb(lv_event_t* event) {
    lv_obj_t* obj = lv_event_get_target(event);
    bool checked = lv_obj_get_state(obj) & LV_STATE_CHECKED;
    tanmatsu_coprocessor_set_pmic_otg_control(coprocessor_handle, checked);
}

static void enable_c6_cb(lv_event_t* event) {
    lv_obj_t* obj = lv_event_get_target(event);
    bool checked = lv_obj_get_state(obj) & LV_STATE_CHECKED;
    if (checked) {
        tanmatsu_coprocessor_radio_enable_application(coprocessor_handle);
    } else {
        tanmatsu_coprocessor_radio_disable(coprocessor_handle);
    }
}

static void set_charging_current(lv_obj_t* roller) {
    // Font sizes shouldn't really be more than 3 digits :)
    char buf[4];

    lv_roller_get_selected_str(roller, buf, 3);

    uint32_t value = (uint32_t)strtol(buf, NULL, 10);

    if (value == 512) {
        charging_current = 0;
    } else if (value == 1024) {
        charging_current = 1;
    } else if (value == 1536) {
        charging_current = 2;
    } else if (value == 2048) {
        charging_current = 3;
    }

    tanmatsu_coprocessor_set_pmic_charging_control(coprocessor_handle, !charging_enabled, charging_current);
}

static void on_charging_current_change(lv_event_t* e, uint32_t key) {
    lv_obj_t* target = lv_event_get_target(e);

    if (!(lv_obj_get_state(target) & LV_STATE_DISABLED)) {
        lv_obj_t* roller = lv_event_get_user_data(e);
        lv_obj_send_event(roller, LV_EVENT_KEY, &key);
        set_charging_current(roller);
    }
}

static void dec_charging_current_cb(lv_event_t* e) {
    on_charging_current_change(e, LV_KEY_LEFT);
}

static void inc_charging_current_cb(lv_event_t* e) {
    on_charging_current_change(e, LV_KEY_RIGHT);
}

lv_obj_t* status_label = NULL;

lv_obj_t* get_pmic_info_screen() {
    lv_obj_t* settings_screen = lv_obj_create(NULL);
    lv_obj_set_flex_flow(settings_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(settings_screen, 10, LV_STATE_DEFAULT);

    lv_obj_t* settings = lv_obj_create(settings_screen);
    lv_obj_set_width(settings, lv_pct(100));
    lv_obj_set_flex_grow(settings, 1);
    lv_obj_set_flex_flow(settings, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(settings, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* settings_left = lv_obj_create(settings);
    lv_obj_set_flex_flow(settings_left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(settings_left, lv_pct(50), lv_pct(100));
    lv_gridnav_add(settings_left, LV_GRIDNAV_CTRL_NONE);
    lv_group_add_obj(lv_group_get_default(), settings_left);
    lv_obj_add_event_cb(settings_left, key_event_cb, LV_EVENT_KEY, NULL);

    lv_obj_t* enable_charging = lv_checkbox_create(settings_left);
    lv_checkbox_set_text(enable_charging, "Enable charging");
    lv_obj_add_state(enable_charging, LV_STATE_CHECKED);
    lv_obj_add_event_cb(enable_charging, enable_charging_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_remove_obj(enable_charging);

    lv_obj_t* enable_otg = lv_checkbox_create(settings_left);
    lv_checkbox_set_text(enable_otg, "Enable OTG boost");
    lv_obj_add_state(enable_otg, LV_STATE_CHECKED);
    lv_obj_add_event_cb(enable_otg, enable_otg_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_remove_obj(enable_otg);

    lv_obj_t* enable_c6 = lv_checkbox_create(settings_left);
    lv_checkbox_set_text(enable_c6, "Enable C6");
    lv_obj_add_state(enable_c6, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(enable_c6, enable_c6_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_remove_obj(enable_c6);

    lv_obj_t* charging_current_label = lv_label_create(settings_left);
    lv_label_set_text(charging_current_label, "Max. charging current (mA)");
    lv_obj_set_style_pad_top(charging_current_label, 20, LV_STATE_DEFAULT);

    lv_obj_t* roller1 = lv_roller_create(settings_left);
    lv_roller_set_options(roller1,
                          "512\n"
                          "1024\n"
                          "1536\n"
                          "2048",
                          LV_ROLLER_MODE_INFINITE);

    lv_roller_set_visible_row_count(roller1, 2);
    lv_group_remove_obj(roller1);
    lv_obj_remove_flag(roller1, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t* decrease_charging_current_button = lv_button_create(settings_left);
    lv_obj_t* decrease_charging_current_label = lv_label_create(decrease_charging_current_button);
    lv_label_set_text(decrease_charging_current_label, "-");
    lv_group_remove_obj(decrease_charging_current_button);
    lv_obj_add_flag(decrease_charging_current_button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(decrease_charging_current_button, dec_charging_current_cb, LV_EVENT_CLICKED, roller1);

    lv_obj_t* increase_charging_current_button = lv_button_create(settings_left);
    lv_obj_t* increase_charging_current_label = lv_label_create(increase_charging_current_button);
    lv_label_set_text(increase_charging_current_label, "+");
    lv_group_remove_obj(increase_charging_current_button);
    lv_obj_add_flag(increase_charging_current_button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(increase_charging_current_button, inc_charging_current_cb, LV_EVENT_CLICKED, roller1);

    lv_obj_set_style_margin_left(roller1, 40, LV_STATE_DEFAULT);
    lv_obj_align_to(decrease_charging_current_button, roller1, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_align_to(increase_charging_current_button, roller1, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t* settings_right = lv_obj_create(settings);
    lv_obj_set_flex_flow(settings_right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(settings_right, lv_pct(50), lv_pct(100));

    status_label = lv_label_create(settings_right);
    lv_label_set_text(status_label, "Please wait...");
    lv_obj_set_size(status_label, lv_pct(100), lv_pct(100));

    return settings_screen;
}

void app_main(void) {
    gpio_install_isr_service(0);

    example_bsp_enable_dsi_phy_power();

    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    size_t h_res = 0;
    size_t v_res = 0;
    lcd_color_rgb_pixel_format_t color_fmt = LCD_COLOR_PIXEL_FORMAT_RGB565;
    st7701_initialize(EXAMPLE_PIN_NUM_LCD_RST);
    mipi_dpi_panel = st7701_get_panel();
    st7701_get_parameters(&h_res, &v_res, &color_fmt);

    example_initialize_i2c_bus();
    i2c_scan(i2c_bus_handle_internal);

    tanmatsu_coprocessor_config_t coprocessor_config = {
        .int_io_num = 6,
        .i2c_bus = i2c_bus_handle_internal,
        .i2c_address = 0x5F,
        .concurrency_semaphore = i2c_concurrency_semaphore,
        .on_keyboard_change = coprocessor_keyboard_callback,
        .on_input_change = coprocessor_input_callback,
    };
    ESP_ERROR_CHECK(tanmatsu_coprocessor_initialize(&coprocessor_config, &coprocessor_handle));

    // tanmatsu_coprocessor_set_real_time(coprocessor_handle, 1728690650);

    uint32_t rtc;
    ESP_ERROR_CHECK(tanmatsu_coprocessor_get_real_time(coprocessor_handle, &rtc));

    struct timeval rtc_timeval = {
        .tv_sec = rtc,
        .tv_usec = 0,
    };

    settimeofday(&rtc_timeval, NULL);

    /*gpio_num_t gpio_sao_sda = 12;
     gpio_num_t gpio_sao_scl = 13;
     gpio_num_t gpio_sao_io1 = 15;
     gpio_num_t gpio_sao_io2 = 0;
     gpio_num_t gpio_sao_mtms = 4;
     gpio_num_t gpio_sao_mtdo = 5;
     gpio_num_t gpio_sao_mtck = 2;
     gpio_num_t gpio_sao_mtdi = 3;

     gpio_num_t pmod_gpios[8] = {
         gpio_sao_scl, gpio_sao_io1, gpio_sao_mtms, gpio_sao_mtck,
         gpio_sao_sda, gpio_sao_io2, gpio_sao_mtdo, gpio_sao_mtdi,
     };

     gpio_config_t pmod_conf = {
         .pin_bit_mask = BIT64(pmod_gpios[0]) | BIT64(pmod_gpios[1]) | BIT64(pmod_gpios[2]) | BIT64(pmod_gpios[3]) |
                         BIT64(pmod_gpios[4]) | BIT64(pmod_gpios[5]) | BIT64(pmod_gpios[6]) | BIT64(pmod_gpios[7]),
         .mode = GPIO_MODE_INPUT_OUTPUT,
         .pull_up_en = 0,
         .pull_down_en = 0,
         .intr_type = GPIO_INTR_DISABLE,
     };

     gpio_config(&pmod_conf);

     for (uint8_t i = 0; i < 8; i++) {
         gpio_set_level(pmod_gpios[i], true);
     }

     while (true) {
         for (uint8_t i = 0; i < 8; i++) {
             printf("%u\r\n", i);
             gpio_set_level(pmod_gpios[i], false);
             vTaskDelay(pdMS_TO_TICKS(500));
             gpio_set_level(pmod_gpios[i], true);
         }
     }
     */

    /*while (true) {
        ESP_ERROR_CHECK(tanmatsu_coprocessor_get_real_time(coprocessor_handle, &rtc));
        printf("Real time clock: %" PRIu32 "\r\n", rtc);

        time_t t;
        time(&t);
        printf("Time library:    %s", ctime(&t));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }*/

    lvgl_init(h_res, v_res, mipi_dpi_panel);

    lvgl_lock();
    // lv_group_set_focus_cb(lv_group_get_default(), focus_cb);
    lv_screen_load(get_pmic_info_screen());
    lvgl_unlock();

    tanmatsu_coprocessor_set_display_backlight(coprocessor_handle, 255);

    tanmatsu_coprocessor_set_pmic_charging_control(coprocessor_handle, !charging_enabled, charging_current);
    tanmatsu_coprocessor_set_pmic_otg_control(coprocessor_handle, false);

    while (true) {
        tanmatsu_coprocessor_set_pmic_adc_control(coprocessor_handle, true, false);
        vTaskDelay(pdMS_TO_TICKS(600));

        tanmatsu_coprocessor_pmic_faults_t faults;
        tanmatsu_coprocessor_get_pmic_faults(coprocessor_handle, &faults);

        uint16_t vbat;
        tanmatsu_coprocessor_get_pmic_vbat(coprocessor_handle, &vbat);

        uint16_t vsys;
        tanmatsu_coprocessor_get_pmic_vsys(coprocessor_handle, &vsys);

        uint16_t ts;
        tanmatsu_coprocessor_get_pmic_ts(coprocessor_handle, &ts);

        uint16_t vbus;
        tanmatsu_coprocessor_get_pmic_vbus(coprocessor_handle, &vbus);

        uint16_t ichgr;
        tanmatsu_coprocessor_get_pmic_ichgr(coprocessor_handle, &ichgr);

        bool last, latch;
        tanmatsu_coprocessor_get_pmic_communication_fault(coprocessor_handle, &last, &latch);

        bool chrg_disable_setting;
        uint8_t chrg_speed;
        tanmatsu_coprocessor_get_pmic_charging_control(coprocessor_handle, &chrg_disable_setting, &chrg_speed);

        bool battery_attached, usb_attached, chrg_disabled;
        uint8_t chrg_status;
        tanmatsu_coprocessor_get_pmic_charging_status(coprocessor_handle, &battery_attached, &usb_attached,
                                                      &chrg_disabled, &chrg_status);

        if (faults.watchdog | faults.boost | faults.chrg_input | faults.chrg_thermal | faults.chrg_safety |
            faults.batt_ovp | faults.ntc_cold | faults.ntc_hot | faults.ntc_boost) {
            printf("Faults: %s %s %s %s %s %s %s %s %s\r\n", faults.watchdog ? "WATCHDOG" : "",
                   faults.boost ? "BOOST" : "", faults.chrg_input ? "CHRG_INPUT" : "",
                   faults.chrg_thermal ? "CHRG_THERMAL" : "", faults.chrg_safety ? "CHRG_SAFETY" : "",
                   faults.batt_ovp ? "BATT_OVP" : "", faults.ntc_cold ? "NTC_COLD" : "",
                   faults.ntc_hot ? "NTC_HOT" : "", faults.ntc_boost ? "NTC_BOOST" : "");
        }

        printf(
            "Vbat: %u mV, vsys: %u mV, ts: %2.2f%%, vbus: %u mV, ichgr: %u mA, comm: %s, chrg: %s (%u), %s, %s, "
            "charger status: %s\r\n",
            vbat, vsys, ts / 100.0, vbus, ichgr, last ? "last" : (latch ? "latch" : "ok"),
            chrg_disabled ? ((!chrg_disable_setting) ? "enabling" : "disabled")
                          : ((!chrg_disable_setting) ? "enabled" : "disabling"),
            chrg_speed, battery_attached ? "battery attached" : "no battery", usb_attached ? "usb attached" : "no usb",
            (chrg_status == TANMATSU_CHARGE_STATUS_NOT_CHARGING)              ? "not charging"
            : (chrg_status == TANMATSU_CHARGE_STATUS_PRE_CHARGING)            ? "pre-charging"
            : (chrg_status == TANMATSU_CHARGE_STATUS_FAST_CHARGING)           ? "fast charging"
            : (chrg_status == TANMATSU_CHARGE_STATUS_CHARGE_TERMINATION_DONE) ? "charging done"
                                                                              : "unknown");

        char buffer[1024] = {0};
        sprintf(buffer,
                "Battery voltage: %u mV\r\n"
                "System voltage: %u mV\r\n"
                "USB voltage: %u mV\r\n"
                "Charging current: %u mA\r\n"
                "TS: %2.2f%%\r\n"
                "Communication: %s\r\n"
                "Charging: %s (%u)\r\n"
                "Battery: %s\r\n"
                "USB: %s\r\n"
                "Charging status: %s\r\n",
                vbat, vsys, vbus, ichgr, ts / 100.0, last ? "last" : (latch ? "latch" : "ok"),
                chrg_disabled ? ((!chrg_disable_setting) ? "enabling" : "disabled")
                              : ((!chrg_disable_setting) ? "enabled" : "disabling"),
                chrg_speed, battery_attached ? "attached" : "not detected", usb_attached ? "attached" : "not detected",
                (chrg_status == TANMATSU_CHARGE_STATUS_NOT_CHARGING)              ? "not charging"
                : (chrg_status == TANMATSU_CHARGE_STATUS_PRE_CHARGING)            ? "pre-charging"
                : (chrg_status == TANMATSU_CHARGE_STATUS_FAST_CHARGING)           ? "fast charging"
                : (chrg_status == TANMATSU_CHARGE_STATUS_CHARGE_TERMINATION_DONE) ? "charging done"
                                                                                  : "unknown");

        lvgl_lock();
        if (status_label) {
            lv_label_set_text(status_label, buffer);
        }
        lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
