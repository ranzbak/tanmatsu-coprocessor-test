/*
 * SPDX-FileCopyrightText: 2024 Nicolai Electronics
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <slint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <cstdio>
#include <memory>
// #include "bsp_lvgl.h"
// #include "core/lv_group.h"
// #include "core/lv_obj.h"
// #include "core/lv_obj_event.h"
// #include "core/lv_obj_pos.h"
// #include "core/lv_obj_style.h"
// #include "core/lv_obj_style_gen.h"
// #include "core/lv_obj_tree.h"
// #include "display/lv_display.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"
extern "C" {
    #include "driver/i2c_master.h"
    #include "dsi_panel_nicolaielectronics_st7701.h"
    #include "tanmatsu_coprocessor.h"
}
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
// #include "font/lv_font.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
// #include "layouts/flex/lv_flex.h"
// #include "libs/freetype/lv_freetype.h"
// #include "lv_demos.h"
// #include "lv_examples.h"
// #include "misc/lv_area.h"
// #include "misc/lv_event.h"
// #include "misc/lv_style.h"
// #include "misc/lv_style_gen.h"
// #include "nvs.h"
// #include "others/gridnav/lv_gridnav.h"
#include "sdkconfig.h"
#include "soc/gpio_num.h"

// #include "widgets/button/lv_button.h"
// #include "widgets/checkbox/lv_checkbox.h"
// #include "widgets/image/lv_image.h"
// #include "widgets/label/lv_label.h"
// #include "widgets/list/lv_list.h"
// #include "widgets/msgbox/lv_msgbox.h"
// #include "widgets/textarea/lv_textarea.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "tanmatsu_coprocessor.h"
#include "slint-esp.h"
#include "appwindow.h"
#include "keyboard.hpp"
#include <thread>

// clang-format off
// #include "lv_api_map_v8.h"
// clang-format on


#define PANEL_MIPI_DSI_PHY_PWR_LDO_CHAN       3  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define PANEL_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL         1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL        !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_RST               (gpio_num_t) -1  // 14 Doesn't work for some reason'
#define EXAMPLE_DISPLAY_TYPE                  DISPLAY_TYPE_ST7701

static const char* TAG = "example";

i2c_master_bus_config_t i2c_master_config_internal = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = (gpio_num_t) 9,
    .scl_io_num = (gpio_num_t) 10,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags = {
        .enable_internal_pullup = true
    }
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
    int retval = 0;
    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            esp_err_t ret = i2c_master_probe(bus_handle, address, 50);
            if (ret == ESP_OK) {
                if (address == 0x5f) {
                    retval = 1;
                }
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }

    return retval;
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

void coprocessor_faults_callback(tanmatsu_coprocessor_handle_t handle, tanmatsu_coprocessor_pmic_faults_t* prev_faults,
                                 tanmatsu_coprocessor_pmic_faults_t* faults) {
    printf("Faults changed: %s %s %s %s %s %s %s %s %s\r\n", faults->watchdog ? "WATCHDOG" : "",
           faults->boost ? "BOOST" : "", faults->chrg_input ? "CHRG_INPUT" : "",
           faults->chrg_thermal ? "CHRG_THERMAL" : "", faults->chrg_safety ? "CHRG_SAFETY" : "",
           faults->batt_ovp ? "BATT_OVP" : "", faults->ntc_cold ? "NTC_COLD" : "", faults->ntc_hot ? "NTC_HOT" : "",
           faults->ntc_boost ? "NTC_BOOST" : "");
}

static void show_error(std::string error) {
    // lv_obj_t* box = lv_msgbox_create(NULL);

    // lv_msgbox_add_title(box, "Error");
    // lv_msgbox_add_text(box, error);
    // // lv_obj_t* close_button = lv_msgbox_add_close_button(box);
    // // lv_group_focus_obj(close_button);
    // lv_group_focus_freeze(lv_group_get_default(), true);
    printf("Error: %s\r\n", error.c_str());
}


uint8_t charging_current = 0;
bool charging_enabled = true;

void coprocessor_keyboard_callback(
    tanmatsu_coprocessor_handle_t handle, 
    tanmatsu_coprocessor_keys_t *prev_keys,
    tanmatsu_coprocessor_keys_t *keys
) {
    if (keys->key_a != prev_keys->key_a) {
        printf("Key A: %s\r\n", keys->key_a? "pressed" : "released");
    }

    if (keys->key_b!= prev_keys->key_b) {
        printf("Key B: %s\r\n", keys->key_b? "pressed" : "released");
    }
}

char rtc_txt[40];

extern "C" void app_main(void) {

    gpio_install_isr_service(0);

    example_bsp_enable_dsi_phy_power();

    gpio_config_t pmod_conf = {
        .pin_bit_mask = BIT64(14),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = (gpio_pullup_t) 0,
        .pull_down_en = (gpio_pulldown_t) 0,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&pmod_conf);

    gpio_set_level((gpio_num_t) 14, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t) 14, true);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    esp_lcd_touch_handle_t touch_handle = nullptr ;
    size_t h_res = 0;
    size_t v_res = 0;
    lcd_color_rgb_pixel_format_t color_fmt = LCD_COLOR_PIXEL_FORMAT_RGB565;
    st7701_initialize(EXAMPLE_PIN_NUM_LCD_RST);
    mipi_dpi_panel = st7701_get_panel();
    esp_lcd_panel_swap_xy(mipi_dpi_panel, 1);
    // ESP_ERROR_CHECK(esp_lcd_panel_mirror(mipi_dpi_panel, true, true));
    st7701_get_parameters(&h_res, &v_res, &color_fmt);

    printf("Starting I2C bus...\r\n");
    example_initialize_i2c_bus();

    printf("Scanning I2C bus...\r\n");
    if (!i2c_scan(i2c_bus_handle_internal)) {
        show_error("Coprocessor not visible on I2C bus");
        return;
    }
    
    printf("Initializing coprocessor...\r\n");
    tanmatsu_coprocessor_config_t coprocessor_config = {
        .int_io_num = (gpio_num_t) 6,
        .i2c_bus = i2c_bus_handle_internal,
        .i2c_address = 0x5F,
        .concurrency_semaphore = i2c_concurrency_semaphore,
        .on_keyboard_change = MyKeyboardHandler::keyboardCallback,
        .on_input_change = coprocessor_input_callback,
        .on_faults_change = coprocessor_faults_callback,
    };
    if (tanmatsu_coprocessor_initialize(&coprocessor_config, &coprocessor_handle) != ESP_OK) {
        show_error("Failed to initialize coprocessor driver");
        return;
    }

    uint32_t rtc;
    if (tanmatsu_coprocessor_get_real_time(coprocessor_handle, &rtc) != ESP_OK) {
        show_error("Failed to read RTC value");
        return;
    }

    struct timeval rtc_timeval = {
        .tv_sec = rtc,
        .tv_usec = 0,
    };

    settimeofday(&rtc_timeval, NULL);

    if (tanmatsu_coprocessor_set_display_backlight(coprocessor_handle, 255) != ESP_OK) {
        show_error("Failed to set display backlight brightness");
        return;
    }

    if (tanmatsu_coprocessor_set_pmic_charging_control(coprocessor_handle, !charging_enabled, charging_current) !=
        ESP_OK) {
        show_error("Failed to configure battery charging");
        return;
    }

    if (tanmatsu_coprocessor_set_pmic_otg_control(coprocessor_handle, true) != ESP_OK) {
        show_error("Failed to enable OTG booster");
        return;
    }

    // Setup Slint
    /* Allocate a drawing buffer */
    // char* buffer = malloc(h_res * v_res * sizeof(lcd_color_rgb_pixel_format_t));
    static std::vector<slint::platform::Rgb565Pixel> buffer(h_res * v_res);


    /* Initialize Slint's ESP platform support*/
    slint_esp_init(slint::PhysicalSize({ h_res, v_res }), mipi_dpi_panel, touch_handle, buffer);
    /* Instantiate the UI */
    auto ui = AppWindow::create();

    MyKeyboardHandler::setSlintWindowHandle(&(ui->window()));

    // register call backs for the UI
    ui->on_request_button_clicked([&]{
        printf("Button clicked\r\n");
    });
    ui->set_txt_edit("Hello, World! Lorum ipsum dolor sit amet, consectetur adipiscing elit.");
    ui->set_rtc_val("no time");

    slint::ComponentWeakHandle<AppWindow> weak_ui_handle(ui);

    // std::thread rtc_thread([=]{
    //     slint::invoke_from_event_loop([&]() {
    //         if (auto wui = weak_ui_handle.lock()) {
    //             // snprintf(rtc_txt, sizeof(rtc_txt), "Raw RTC: %d", (int)rtc);
    //             // wui->set_rtc_val(rtc_txt);
    //             printf("Updating RTC...\r\n");
    //         }
    //     });
    // });

    // auto kb_handle = ui->
    /* Show it on the screen and run the event loop */
    ui->run();

    while (true) {
    }
}
