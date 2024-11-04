#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

// #include <memory>
#include <slint.h>
#include <slint_window.h>
// #include "appwindow.h"
#include "tanmatsu_coprocessor.h"
#include "freertos/queue.h"

// Forward declarations
struct key_event_t;

class MyKeyboardHandler {
private:

    // static slint::ComponentHandle<AppWindow> slint_window_handle;
    static slint::Window *window_handle;

    static void keyToState(uint8_t pressed, uint8_t key); 

public:
    // This is a static class so we don't want instances of this class
    MyKeyboardHandler() = delete;

    // static void setSlintWindowHandle(slint::ComponentHandle<AppWindow> handle);
    static void setSlintWindowHandle(slint::Window *handle);

    // static slint::Window getSlintWindowHandle();

    static void keyboardCallback(tanmatsu_coprocessor_handle_t handle, 
                                       tanmatsu_coprocessor_keys_t* prev_keys,
                                       tanmatsu_coprocessor_keys_t* keys);
};

#endif // KEYBOARD_HPP