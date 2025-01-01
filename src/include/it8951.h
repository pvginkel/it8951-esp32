#pragma once

#include "driver/spi_master.h"

/**
 * @brief Area identifying the size of images and display areas.
 */
struct IT8951Area {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
};

/**
 * @brief Hardware rotation for images.
 */
enum it8951_rotate_t : uint8_t {
    IT8951_ROTATE_0 = 0,
    IT8951_ROTATE_90 = 1,
    IT8951_ROTATE_180 = 2,
    IT8951_ROTATE_270 = 3,
};

/**
 * @brief Pixel format of images.
 */
enum it8951_pixel_format_t : uint8_t {
    IT8951_PIXEL_FORMAT_1BPP,  ///< Monochrome images, required for A2 fast updates.
    IT8951_PIXEL_FORMAT_2BPP,  ///< Four gray scale mode; use only when memory space is limited.
    IT8951_PIXEL_FORMAT_4BPP,  ///< High fidelity gray scale mode.
    IT8951_PIXEL_FORMAT_8BPP,  ///< Don't use. 4 bit per pixel is the highest supported anyway.
};

/**
 * @brief Display mode to show images on the screen.
 */
enum it8951_display_mode_t {
    IT8951_DISPLAY_MODE_INIT,  ///< Init mode to refresh the screen. Use `clear_screen()` instead.
    IT8951_DISPLAY_MODE_A2,    ///< Fast display mode. Requires 1 bit per pixel images.
    IT8951_DISPLAY_MODE_GC16,  ///< 16 color gray scale mode.
};

/**
 * @brief Driver for the IT8951 controller.
 */
class IT8951 {
    struct DeviceInfo {
        uint16_t width;
        uint16_t height;
        uint16_t memory_address_low;
        uint16_t memory_address_heigh;
        uint8_t firmware_version[16];
        uint8_t lut_version[16];
    };

public:
    /**
     * @brief Setup the controller.
     * @param vcom The VCOM value. This has to be set correctly and is the number printed on the cable.
     * @return Whether the controller was setup correctly.
     */
    bool setup(float vcom);

    /**
     * @brief Get the current SPI transfer buffer. Called after `load_image_start()`.
     * @return The current SPI transfer buffer.
     */
    uint8_t* get_buffer() { return _current_buffer == 0 ? _buffer0 : _buffer1; }

    /**
     * @brief Gets the size of the SPI transfer buffers.
     */
    size_t get_buffer_len() { return _buffer_len; }

    /**
     * @brief Gets the width of the screen.
     *
     * Note that this is not necessarily a multiple of 2 or 8. This width needs
     * to be rounded to scan line sizes based on the pixel format of the image.
     * For 1 bit per pixel, a scan line will be `(get_width() + 7) / 8`. For
     * 4 bit per pixel, it will be `(get_width() + 1) / 2`.
     */
    uint16_t get_width() { return _width; }

    /**
     * @brief Gets the height of the screen.
     */
    uint16_t get_height() { return _height; }

    /**
     * @brief Gets the memory address where images can be stored on the controller.
     *
     * IT8951 controllers have quite some memory. More than one image can be
     * stored on it. This allows for advanced functionality. If you don't need this,
     * You can just use this base memory address as the target for images.
     */
    uint32_t get_memory_address() { return _memory_address; }

    /**
     * @brief Enable enhanced driver capability mode. Enable this if the screen behaves
     * funny without it.
     */
    void enable_enhance_driving_capability();

    /**
     * @brief Wake the controller from sleep mode.
     *
     * The screen needs to be cleared after the system is woken up from
     * sleep mode. Otherwise you get strange artifacts on the screen. If you
     * get the controller to work without clearing the screen, raise a GitHub
     * issue so that the documentation can be updated.
     */
    void set_system_run();

    /**
     * @brief Put the controller in sleep mode.
     */
    void set_sleep();

    /**
     * @brief Clear the screen.
     *
     * This must be done when the controller is started, when the controller
     * wakes from sleep mode and every once in a while when using A2 fast update
     * mode.
     */
    void clear_screen();

    /**
     * @brief Start copying an image to the controller.
     * @param area The dimensions of the image.
     * @param target_memory_address The target memory address to store the image at.
     * @param rotate The hardware rotation associated with the image.
     * @param pixel_format The pixel format of the data.
     */
    void load_image_start(IT8951Area& area, uint32_t target_memory_address, it8951_rotate_t rotate,
                          it8951_pixel_format_t pixel_format);

    /**
     * @brief Transfer an SPI buffer to the controller.
     * @param len The number of bytes in the SPI buffer to transfer.
     */
    void load_image_flush_buffer(size_t len);

    /**
     * @brief Signal that the whole image has been copied.
     */
    void load_image_end();

    /**
     * @brief Display an image on the screen.
     * @param area The area to show the image.
     * @param target_memory_address The location where the image is stored.
     * @param pixel_format The pixel format of the image.
     * @param mode The mode used to show the image.
     */
    void display_area(IT8951Area& area, uint32_t target_memory_address, it8951_pixel_format_t pixel_format,
                      it8951_display_mode_t mode);

private:
    void reset();
    void spi_setup(int clock_speed_hz);
    void transaction_start();
    void transaction_end();
    uint8_t read_byte();
    uint16_t read_word();
    void read_array(uint8_t* data, size_t len, bool swap);
    void write_byte(uint8_t value);
    void write_word(uint16_t value);
    void write_array(uint8_t* data, size_t len, bool swap);
    void delay(int ms);
    uint32_t millis();
    void wait_until_idle();
    uint16_t read_data();
    void read_data(uint8_t* data, size_t len);
    void write_command(uint16_t command);
    void write_data(uint16_t data);
    void write_data(uint8_t* data, size_t len);
    uint16_t read_reg(uint16_t reg);
    void write_reg(uint16_t reg, uint16_t value);
    uint32_t idle_timeout() { return 30'000; }
    void controller_setup(DeviceInfo& device_info, uint16_t vcom);
    void get_system_info(DeviceInfo& device_info);
    uint16_t get_vcom();
    void set_vcom(uint16_t vcom);
    void set_target_memory_address(uint32_t target_memory_address);
    void wait_display_ready();
    uint16_t get_mode_value(it8951_display_mode_t mode);

    size_t _buffer_len{0};
    uint8_t _current_buffer{0};
    uint8_t* _buffer0{nullptr};
    uint8_t* _buffer1{nullptr};
    spi_transaction_t _buffer_transaction{};
    bool _buffer_transaction_pending{false};
    spi_device_handle_t _spi{nullptr};
    uint32_t _memory_address{0};
    uint16_t _width{0};
    uint16_t _height{0};
    int _a2_mode{0};
};
