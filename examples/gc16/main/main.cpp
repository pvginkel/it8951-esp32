#include <stdio.h>

#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "it8951.h"

extern "C" void app_main(void) {
    IT8951 display;

    // Initialize the IT8951 controller. The value is the voltage that is
    // shown on the cable. It's important this value is correct!

    display.setup(-1.15f);

    display.clear_screen();

    // Allocate a buffer to hold a single scan line.

    const size_t scan_line = (display.get_width() + 1) / 2;
    const auto scan_line_buffer = (uint8_t*)malloc(scan_line);

    while (true) {
        // Show bars with all colors on the screen. One bar is added per loop iteration.

        const int bars = 16;

        memset(scan_line_buffer, 0xff, scan_line);

        for (int i = 0; i < bars; i++) {
            // Write the colors for the bar added in this loop. A single byte holds
            // two pixels.

            const auto bar_size = scan_line / bars;
            const auto offset = bar_size * i;
            const auto color = 15 - i;

            memset(&scan_line_buffer[offset], color | color << 4, bar_size);

            //
            // Create a screen image in the controller using the single scan line.
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
            // Note also that there is no requirement for the full buffer to be sent. This
            // is used in this example to build up a screen sized image in the controller
            // based on a single scan line.
            //

            IT8951Area area = {
                .x = 0,
                .y = 0,
                .w = display.get_width(),
                .h = display.get_height(),
            };

            display.load_image_start(area, display.get_memory_address(), IT8951_ROTATE_0, IT8951_PIXEL_FORMAT_4BPP);

            const size_t buffer_len = display.get_buffer_len();

            for (auto y = 0; y < display.get_height(); y++) {
                for (size_t offset = 0; offset < scan_line; offset += buffer_len) {
                    const auto copy = std::min(buffer_len, scan_line - offset);

                    memcpy(display.get_buffer(), scan_line_buffer + offset, copy);

                    display.load_image_flush_buffer(copy);
                }
            }

            display.load_image_end();

            display.display_area(area, display.get_memory_address(), IT8951_PIXEL_FORMAT_4BPP,
                                 IT8951_DISPLAY_MODE_GC16);

            // Wait a bit before showing the next bar.

            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}
