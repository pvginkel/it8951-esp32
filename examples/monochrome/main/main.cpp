#include <stdio.h>

#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "it8951.h"

static const char* TAG = "main";

extern "C" void app_main(void) {
    IT8951 display;

    // Initialize the IT8951 controller. The value is the voltage that is
    // shown on the cable. It's important this value is correct!

    display.setup(-1.15f);

    // Allocate a screen sized buffer.

    const size_t scan_line = (display.get_width() + 7) / 8;
    const size_t display_buffer_size = scan_line * display.get_height();
    const auto display_buffer = (uint8_t*)malloc(display_buffer_size);
    if (!display_buffer) {
        ESP_LOGE(TAG, "Failed to allocate screen buffer");
        esp_restart();
    }

    while (true) {
        // Clear the screen of any residual image. This is done every few updates
        // and removes the after image on the screen.

        display.clear_screen();

        // Show bars moving across the screen.

        const int bars = 16;

        for (int i = 0; i < bars; i++) {
            // Draw a bar in the screen buffer.

            memset(display_buffer, 0xff, display_buffer_size);

            const auto bar_size = scan_line / bars;
            const auto offset = bar_size * i;

            for (int y = 0; y < display.get_height(); y++) {
                memset(&display_buffer[y * scan_line + offset], 0, bar_size);
            }

            //
            // Send the screen buffer to the controller.
            //
            // Sending an image to the controller works as follows:
            //
            // * Start transferring the image to the controller using load_image_start().
            //   This lets the controller know of the image dimensions, rotation and
            //   pixel format.
            // * Send the image in chunks. While one buffer is being filled, a
            //   second buffer is being transferred using SPI. If you take a reference to
            //   the SPI transfer buffer, call get_buffer() after calling
            //   load_image_flush_buffer() to get the current buffer.
            // * Once the image is fully transferred, call load_image_end() to
            //   signal that the image has been transferred.
            //
            // Once the image has been transferred to the controller, it can be displayed
            // using display_area().
            //
            // Note that some time may pass between load_image_flush_buffer() calls. You
            // can take advantage of this to render an image in chunks, e.g. when using LVGL.
            //

            IT8951Area area = {
                .x = 0,
                .y = 0,
                .w = display.get_width(),
                .h = display.get_height(),
            };

            display.load_image_start(area, display.get_memory_address(), IT8951_ROTATE_0, IT8951_PIXEL_FORMAT_1BPP);

            const size_t buffer_len = display.get_buffer_len();

            for (size_t offset = 0; offset < display_buffer_size; offset += buffer_len) {
                const auto copy = std::min(buffer_len, display_buffer_size - offset);

                memcpy(display.get_buffer(), display_buffer + offset, copy);

                display.load_image_flush_buffer(copy);
            }

            display.load_image_end();

            display.display_area(area, display.get_memory_address(), IT8951_PIXEL_FORMAT_1BPP, IT8951_DISPLAY_MODE_A2);

            // Wait a bit before showing the next bar.

            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}
