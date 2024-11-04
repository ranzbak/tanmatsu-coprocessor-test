#include <slint.h>
#include <slint_window.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/lock.h>
#include <unistd.h>
#include <memory>
#include <iostream>
#include "bsp_lvgl.h"
// #include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tanmatsu_coprocessor.h"

#include "keyboard.hpp"

// Add this line at the global scope (outside of any function)
slint::Window* MyKeyboardHandler::window_handle = nullptr;

// QueueHandle_t key_queue;

void MyKeyboardHandler::keyToState(uint8_t pressed, const uint8_t key) {

    // Key dispatch event function pointer from SLint
    // auto handler = MyKeyboardHandler::slint_window_handle->window();
    // void* handler = MyKeyboardHandler::slint_window_handle;

    const slint::SharedString skey = (slint::SharedString)std::string(1, key);

    // Show debug when pressed or released the key value in hex format
    std::cout << "Key: " << (pressed? "pressed" : "released") << ", " << "0x" << std::hex << (int)key << std::endl;

    // Handle press or release event
    if (pressed) {
        MyKeyboardHandler::window_handle->dispatch_key_press_event(skey);
    } else {
        MyKeyboardHandler::window_handle->dispatch_key_release_event(skey);
    }
}

// void MyKeyboardHandler::setSlintWindowHandle(slint::ComponentHandle<AppWindow> handle) {
void MyKeyboardHandler::setSlintWindowHandle(slint::Window *handle) {
    if (handle == NULL) {
        std::cout << "MyKeyboardHandler::setSlintWindowHandle: NULL handle provided" << std::endl;
        return;
    }
    MyKeyboardHandler::window_handle = handle;
}

// slint::Window MyKeyboardHandler::getSlintWindowHandle() {
//     return MyKeyboardHandler::slint_window_handle;
// }

void MyKeyboardHandler::keyboardCallback(tanmatsu_coprocessor_handle_t handle, tanmatsu_coprocessor_keys_t* prev_keys,
                                    tanmatsu_coprocessor_keys_t* keys) {

    if (handle == NULL) {
        // Ignore keyboard input when no handle is set
        return;
    }

    // Short hand, get pointer to the state function
    auto locKts = MyKeyboardHandler::keyToState;

    // Translate key events from the coprocessor to SLint key events
    if (keys->key_up != prev_keys->key_up) {
        locKts(keys->key_up, 0x26);
    }
    if (keys->key_down != prev_keys->key_down) {
        locKts(keys->key_down, 0x28);
    }
    if (keys->key_left != prev_keys->key_left) {
        locKts(keys->key_left, 0x25);
    }
    if (keys->key_right != prev_keys->key_right) {
        locKts(keys->key_right, 0x27);
    }
    if (keys->key_return != prev_keys->key_return) {
        locKts(keys->key_return, 0x0d);
    }
    if (keys->key_esc != prev_keys->key_esc) {
        locKts(keys->key_esc, 0x1b);
    }
    if (keys->key_tab != prev_keys->key_tab) {
        locKts(keys->key_tab, 0x09);
    }
    if (keys->key_backspace != prev_keys->key_backspace) {
        locKts(keys->key_backspace, 0x08);
    }

    // Keys go brrr
    if (keys->key_a != prev_keys->key_a) {
        locKts(keys->key_a, 65);
    }
    if (keys->key_b != prev_keys->key_b) {
        locKts(keys->key_b, 66);
    }
    if (keys->key_c != prev_keys->key_c) {
        locKts(keys->key_c, 67);
    }
    if (keys->key_d != prev_keys->key_d) {
        locKts(keys->key_d, 68);
    }
    if (keys->key_e != prev_keys->key_e) {
        locKts(keys->key_e, 69);
    }
    if (keys->key_f != prev_keys->key_f) {
        locKts(keys->key_f, 70);
    }
    if (keys->key_g != prev_keys->key_g) {
        locKts(keys->key_g, 71);
    }
    if (keys->key_h != prev_keys->key_h) {
        locKts(keys->key_h, 72);
    }
    if (keys->key_i != prev_keys->key_i) {
        locKts(keys->key_i, 73);
    }
    if (keys->key_j != prev_keys->key_j) {
        locKts(keys->key_j, 74);
    }
    if (keys->key_k != prev_keys->key_k) {
        locKts(keys->key_k, 75);
    }
    if (keys->key_l != prev_keys->key_l) {
        locKts(keys->key_l, 76);
    }
    if (keys->key_m != prev_keys->key_m) {
        locKts(keys->key_m, 77);
    }
    if (keys->key_n != prev_keys->key_n) {
        locKts(keys->key_n, 78);
    }
    if (keys->key_o != prev_keys->key_o) {
        locKts(keys->key_o, 79);
    }
    if (keys->key_p != prev_keys->key_p) {
        locKts(keys->key_p, 80);
    }
    if (keys->key_q != prev_keys->key_q) {
        locKts(keys->key_q, 81);
    }
    if (keys->key_r != prev_keys->key_r) {
        locKts(keys->key_r, 82);
    }
    if (keys->key_s != prev_keys->key_s) {
        locKts(keys->key_s, 83);
    }
    if (keys->key_t != prev_keys->key_t) {
        locKts(keys->key_t, 84);
    }
    if (keys->key_u != prev_keys->key_u) {
        locKts(keys->key_u, 85);
    }
    if (keys->key_v != prev_keys->key_v) {
        locKts(keys->key_v, 86);
    }
    if (keys->key_w != prev_keys->key_w) {
        locKts(keys->key_w, 87);
    }
    if (keys->key_x != prev_keys->key_x) {
        locKts(keys->key_x, 88);
    }
    if (keys->key_y != prev_keys->key_y) {
        locKts(keys->key_y, 89);
    }
    if (keys->key_z != prev_keys->key_z) {
        locKts(keys->key_z, 90);
    }
};