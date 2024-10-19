/*
 * SPDX-FileCopyrightText: 2024 Nicolai Electronics
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/gpio_num.h"
#include "tanmatsu_coprocessor.h"

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

void coprocessor_keyboard_callback(tanmatsu_coprocessor_handle_t handle, tanmatsu_coprocessor_keys_t* prev_keys,
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

void app_main(void) {
    gpio_install_isr_service(0);

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

    tanmatsu_coprocessor_set_display_backlight(coprocessor_handle, 128);

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

        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
