#pragma once

#include "driver/spi_master.h"

struct IT8951Area {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
};

enum it8951_rotate_t : uint8_t {
    IT8951_ROTATE_0 = 0,
    IT8951_ROTATE_90 = 1,
    IT8951_ROTATE_180 = 2,
    IT8951_ROTATE_270 = 3,
};

enum it8951_pixel_format_t : uint8_t {
    IT8951_PIXEL_FORMAT_1BPP,
    IT8951_PIXEL_FORMAT_2BPP,
    IT8951_PIXEL_FORMAT_4BPP,
    IT8951_PIXEL_FORMAT_8BPP,
};

enum it8951_display_mode_t {
    IT8951_DISPLAY_MODE_INIT,
    IT8951_DISPLAY_MODE_A2,
    IT8951_DISPLAY_MODE_GC16,
};

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
    bool setup(float vcom);
    uint8_t* get_buffer() { return _current_buffer == 0 ? _buffer0 : _buffer1; }
    size_t get_buffer_len() { return _buffer_len; }
    uint16_t get_width() { return _width; }
    uint16_t get_height() { return _height; }
    uint32_t get_memory_address() { return _memory_address; }
    void enable_enhance_driving_capability();
    void set_system_run();
    void set_sleep();
    void clear_screen();
    void load_image_start(IT8951Area& area, uint32_t target_memory_address, it8951_rotate_t rotate,
                          it8951_pixel_format_t pixel_format);
    void load_image_flush_buffer(size_t len);
    void load_image_end();
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
