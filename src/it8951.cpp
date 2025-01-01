#include "it8951.h"

#include <cmath>
#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "support.h"

static const char* TAG = "IT8951";

#define _WS_CONCAT3(x, y, z) x##y##z
#define WS_CONCAT3(x, y, z) _WS_CONCAT3(x, y, z)

#define SPI_HOST WS_CONCAT3(SPI, CONFIG_IT8951_SPI_HOST, _HOST)

// INIT mode, for every init or some time after A2 mode refresh
#define IT8951_MODE_INIT 0
// GC16 mode, for every time to display 16 grayscale image
#define IT8951_MODE_GC16 2

// Built in I80 Command Code
#define IT8951_TCON_SYS_RUN 0x0001
#define IT8951_TCON_STANDBY 0x0002
#define IT8951_TCON_SLEEP 0x0003
#define IT8951_TCON_REG_RD 0x0010
#define IT8951_TCON_REG_WR 0x0011

#define IT8951_TCON_MEM_BST_RD_T 0x0012
#define IT8951_TCON_MEM_BST_RD_S 0x0013
#define IT8951_TCON_MEM_BST_WR 0x0014
#define IT8951_TCON_MEM_BST_END 0x0015

#define IT8951_TCON_LD_IMG 0x0020
#define IT8951_TCON_LD_IMG_AREA 0x0021
#define IT8951_TCON_LD_IMG_END 0x0022

// I80 User defined command code
#define USDEF_I80_CMD_DPY_AREA 0x0034
#define USDEF_I80_CMD_GET_DEV_INFO 0x0302
#define USDEF_I80_CMD_DPY_BUF_AREA 0x0037
#define USDEF_I80_CMD_VCOM 0x0039

#define FRONT_GRAY_VALUE 0x00
#define BACK_GRAY_VALUE 0xf0

/*-----------------------------------------------------------------------
 IT8951 mode defines
------------------------------------------------------------------------*/

// Pixel mode (Bit per Pixel)
#define IT8951_2BPP 0
#define IT8951_3BPP 1
#define IT8951_4BPP 2
#define IT8951_8BPP 3

// Endian Type
#define IT8951_LDIMG_L_ENDIAN 0
#define IT8951_LDIMG_B_ENDIAN 1
/*-----------------------------------------------------------------------
IT8951 Registers defines
------------------------------------------------------------------------*/
// Register Base Address
#define DISPLAY_REG_BASE 0x1000  // Register RW access

// Base Address of Basic LUT Registers
#define LUT0EWHR (DISPLAY_REG_BASE + 0x00)   // LUT0 Engine Width Height Reg
#define LUT0XYR (DISPLAY_REG_BASE + 0x40)    // LUT0 XY Reg
#define LUT0BADDR (DISPLAY_REG_BASE + 0x80)  // LUT0 Base Address Reg
#define LUT0MFN (DISPLAY_REG_BASE + 0xC0)    // LUT0 Mode and Frame number Reg
#define LUT01AF (DISPLAY_REG_BASE + 0x114)   // LUT0 and LUT1 Active Flag Reg

// Update Parameter Setting Register
#define UP0SR (DISPLAY_REG_BASE + 0x134)      // Update Parameter0 Setting Reg
#define UP1SR (DISPLAY_REG_BASE + 0x138)      // Update Parameter1 Setting Reg
#define LUT0ABFRV (DISPLAY_REG_BASE + 0x13C)  // LUT0 Alpha blend and Fill rectangle Value
#define UPBBADDR (DISPLAY_REG_BASE + 0x17C)   // Update Buffer Base Address
#define LUT0IMXY (DISPLAY_REG_BASE + 0x180)   // LUT0 Image buffer X/Y offset Reg
#define LUTAFSR (DISPLAY_REG_BASE + 0x224)    // LUT Status Reg (status of All LUT Engines)
#define BGVR (DISPLAY_REG_BASE + 0x250)       // Bitmap (1bpp) image color table

// System Registers
#define SYS_REG_BASE 0x0000

// Address of System Registers
#define I80CPCR (SYS_REG_BASE + 0x04)

// Memory Converter Registers
#define MCSR_BASE_ADDR 0x0200
#define MCSR (MCSR_BASE_ADDR + 0x0000)
#define LISAR (MCSR_BASE_ADDR + 0x0008)

bool IT8951::setup(float vcom) {
    ESP_LOGI(TAG, "Initializing SPI");

    gpio_config_t i_conf = {
        .pin_bit_mask = 1ull << CONFIG_IT8951_DISPLAY_READY_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&i_conf));

    i_conf = {
        .pin_bit_mask = 1ull << CONFIG_IT8951_RESET_PIN | 1ull << CONFIG_IT8951_CS_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&i_conf));

    spi_setup(SPI_MASTER_FREQ_10M);

    ESP_LOGI(TAG, "Initializing controller");

    DeviceInfo device_info;
    controller_setup(device_info, (uint16_t)(fabs(vcom) * 1000));

    // Per documentation. We need to initialize the controller at a low clock
    // speed. We get errors if we initialize the controller with the below
    // clock speed.

    spi_setup(SPI_MASTER_FREQ_20M);

    _width = device_info.width;
    _height = device_info.height;
    _memory_address = device_info.memory_address_low | (device_info.memory_address_heigh << 16);

    auto lut_version = (char*)device_info.lut_version;
    auto four_byte_align = false;

    if (strcmp(lut_version, "M641") == 0) {
        // 6inch e-Paper HAT(800,600), 6inch HD e-Paper HAT(1448,1072), 6inch HD touch e-Paper HAT(1448,1072)
        _a2_mode = 4;
        four_byte_align = true;
    } else if (strcmp(lut_version, "M841_TFAB512") == 0) {
        // Another firmware version for 6inch HD e-Paper HAT(1448,1072), 6inch HD touch e-Paper HAT(1448,1072)
        _a2_mode = 6;
        four_byte_align = true;
    } else if (strcmp(lut_version, "M841") == 0) {
        // 9.7inch e-Paper HAT(1200,825)
        _a2_mode = 6;
    } else if (strcmp(lut_version, "M841_TFA2812") == 0) {
        // 7.8inch e-Paper HAT(1872,1404)
        _a2_mode = 6;
    } else if (strcmp(lut_version, "M841_TFA5210") == 0) {
        // 10.3inch e-Paper HAT(1872,1404)
        _a2_mode = 6;
    } else {
        // default set to 6 as A2 mode
        _a2_mode = 6;
    }

    if (four_byte_align) {
        ESP_LOGE(TAG, "Four byte alignment is not supported");
        return false;
    }

    return true;
}

void IT8951::spi_setup(int clock_speed_hz) {
    if (!_spi) {
        spi_bus_config_t bus_config = {
            .mosi_io_num = CONFIG_IT8951_MOSI_PIN,
            .miso_io_num = CONFIG_IT8951_MISO_PIN,
            .sclk_io_num = CONFIG_IT8951_SCLK_PIN,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
        };

        ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
    } else {
        spi_bus_remove_device(_spi);
    }

    spi_device_interface_config_t device_interface_config = {
        .clock_speed_hz = clock_speed_hz,
        .spics_io_num = -1,
        .queue_size = 1,
    };

    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &device_interface_config, &_spi));

    int freq_khz;
    ESP_ERROR_CHECK(spi_device_get_actual_freq(_spi, &freq_khz));
    ESP_LOGI(TAG, "SPI device frequency %d KHz", freq_khz);
    ESP_ERROR_ASSERT(freq_khz * 1000 <= device_interface_config.clock_speed_hz);

    if (_buffer0) {
        return;
    }

    size_t bus_max_transfer_sz;
    ESP_ERROR_CHECK(spi_bus_get_max_transaction_len(SPI_HOST, &bus_max_transfer_sz));

    _buffer_len = std::min(bus_max_transfer_sz, size_t(2048));

    ESP_LOGI(TAG, "Allocating %d bytes for xfer buffers (max %d)", _buffer_len, bus_max_transfer_sz);

    _buffer0 = (uint8_t*)heap_caps_malloc(_buffer_len, MALLOC_CAP_DMA);
    ESP_ERROR_ASSERT(_buffer0);
    _buffer1 = (uint8_t*)heap_caps_malloc(_buffer_len, MALLOC_CAP_DMA);
    ESP_ERROR_ASSERT(_buffer1);
}

void IT8951::transaction_start() { gpio_set_level((gpio_num_t)CONFIG_IT8951_CS_PIN, 0); }

void IT8951::transaction_end() { gpio_set_level((gpio_num_t)CONFIG_IT8951_CS_PIN, 1); }

uint8_t IT8951::read_byte() {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_RXDATA,
        .length = 8,
    };

    ESP_ERROR_CHECK(spi_device_transmit(_spi, &t));

    return t.rx_data[0];
}

uint16_t IT8951::read_word() {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_RXDATA,
        .length = 16,
    };

    ESP_ERROR_CHECK(spi_device_transmit(_spi, &t));

    return (uint16_t)t.rx_data[0] << 8 | t.rx_data[1];
}

void IT8951::read_array(uint8_t* data, size_t len, bool swap) {
    spi_transaction_t t = {
        .length = 8 * len,
        .rx_buffer = data,
    };

    ESP_ERROR_CHECK(spi_device_transmit(_spi, &t));

    if (swap) {
        for (size_t i = 0; i < len; i += 2) {
            auto tmp = data[i];
            data[i] = data[i + 1];
            data[i + 1] = tmp;
        }
    }
}

void IT8951::write_byte(uint8_t value) {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {value},
    };

    ESP_ERROR_CHECK(spi_device_transmit(_spi, &t));
}

void IT8951::write_word(uint16_t value) {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 16,
        .tx_data = {(uint8_t)(value >> 8), (uint8_t)(value)},
    };

    ESP_ERROR_CHECK(spi_device_transmit(_spi, &t));
}

void IT8951::write_array(uint8_t* data, size_t len, bool swap) {
    spi_transaction_t t = {
        .length = 8 * len,
        .tx_buffer = data,
    };

    if (swap) {
        for (size_t i = 0; i < len; i += 2) {
            auto tmp = data[i];
            data[i] = data[i + 1];
            data[i + 1] = tmp;
        }
    }

    ESP_ERROR_CHECK(spi_device_transmit(_spi, &t));
}

void IT8951::delay(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

uint32_t IT8951::millis() { return esp_timer_get_time() / 1000; }

void IT8951::wait_until_idle() {
    if (gpio_get_level((gpio_num_t)CONFIG_IT8951_DISPLAY_READY_PIN)) {
        return;
    }

    const uint32_t start = millis();
    while (!gpio_get_level((gpio_num_t)CONFIG_IT8951_DISPLAY_READY_PIN)) {
        ESP_ERROR_ASSERT(millis() - start < this->idle_timeout());

        delay(20);
    }
}

uint16_t IT8951::read_data() {
    transaction_start();

    wait_until_idle();
    write_word(0x1000);
    wait_until_idle();
    read_word();  // Skip a word.
    wait_until_idle();
    auto result = read_word();

    transaction_end();

    return result;
}

void IT8951::read_data(uint8_t* data, size_t len) {
    transaction_start();

    wait_until_idle();
    write_word(0x1000);
    wait_until_idle();
    read_word();  // Skip a word.
    wait_until_idle();
    read_array(data, len, true);

    transaction_end();
}

void IT8951::write_command(uint16_t command) {
    transaction_start();

    wait_until_idle();
    write_word(0x6000);
    wait_until_idle();
    write_word(command);

    transaction_end();
}

void IT8951::write_data(uint16_t data) {
    transaction_start();

    wait_until_idle();
    write_word(0x0000);
    wait_until_idle();
    write_word(data);

    transaction_end();
}

void IT8951::write_data(uint8_t* data, size_t len) {
    transaction_start();

    wait_until_idle();
    write_word(0x0000);
    wait_until_idle();
    write_array(data, len, true);

    transaction_end();
}

uint16_t IT8951::read_reg(uint16_t reg) {
    transaction_start();

    write_command(IT8951_TCON_REG_RD);
    write_data(reg);
    auto result = read_data();

    transaction_end();

    return result;
}

void IT8951::write_reg(uint16_t reg, uint16_t value) {
    transaction_start();

    write_command(IT8951_TCON_REG_WR);
    write_data(reg);
    write_data(value);

    transaction_end();
}

void IT8951::enable_enhance_driving_capability() {
    auto value = read_reg(0x0038);

    ESP_LOGD(TAG, "The reg value before writing is %x", value);

    write_reg(0x0038, 0x0602);

    value = read_reg(0x0038);

    ESP_LOGD(TAG, "The reg value after writing is %x", value);
}

void IT8951::set_system_run() { write_command(IT8951_TCON_SYS_RUN); }

void IT8951::set_sleep() { write_command(IT8951_TCON_SLEEP); }

void IT8951::reset() {
    gpio_set_level((gpio_num_t)CONFIG_IT8951_RESET_PIN, 1);
    delay(200);
    gpio_set_level((gpio_num_t)CONFIG_IT8951_RESET_PIN, 0);
    delay(10);
    gpio_set_level((gpio_num_t)CONFIG_IT8951_RESET_PIN, 1);
    delay(200);
}

void IT8951::get_system_info(DeviceInfo& device_info) {
    write_command(USDEF_I80_CMD_GET_DEV_INFO);

    read_data((uint8_t*)&device_info, sizeof(DeviceInfo));

    ESP_LOGI(TAG, "Panel(W,H) = (%d,%d)", device_info.width, device_info.height);
    ESP_LOGI(TAG, "Memory Address = %X", device_info.memory_address_low | (device_info.memory_address_heigh << 16));
    ESP_LOGI(TAG, "FW Version = %s", (uint8_t*)device_info.firmware_version);
    ESP_LOGI(TAG, "LUT Version = %s", (uint8_t*)device_info.lut_version);
}

uint16_t IT8951::get_vcom() {
    write_command(USDEF_I80_CMD_VCOM);
    write_data(0x0000);
    return read_data();
}

void IT8951::set_vcom(uint16_t vcom) {
    write_command(USDEF_I80_CMD_VCOM);
    write_data(0x0001);
    write_data(vcom);
}

void IT8951::controller_setup(DeviceInfo& device_info, uint16_t vcom) {
    transaction_end();

    reset();

    set_system_run();

    get_system_info(device_info);

    // Enable Pack write
    write_reg(I80CPCR, 0x0001);

    // Set VCOM by handle
    if (vcom != get_vcom()) {
        set_vcom(vcom);
        ESP_LOGI(TAG, "vcom = -%.02fV\n", (float)get_vcom() / 1000);
    }
}

void IT8951::clear_screen() {
    IT8951Area area = {
        .x = 0,
        .y = 0,
        .w = _width,
        .h = _height,
    };

    load_image_start(area, _memory_address, IT8951_ROTATE_0, IT8951_PIXEL_FORMAT_1BPP);

    auto write_len = area.w / 8 * area.h;

    for (uint32_t offset = 0; offset < write_len; offset += _buffer_len) {
        auto buffer = get_buffer();

        memset(buffer, 0xff, _buffer_len);

        load_image_flush_buffer(std::min(size_t(write_len - offset), size_t(_buffer_len)));
    }

    load_image_end();

    display_area(area, _memory_address, IT8951_PIXEL_FORMAT_1BPP, IT8951_DISPLAY_MODE_INIT);
}

void IT8951::load_image_start(IT8951Area& area, uint32_t target_memory_address, it8951_rotate_t rotate,
                              it8951_pixel_format_t pixel_format) {
    wait_display_ready();

    uint16_t pixel_format_value;

    switch (pixel_format) {
        case IT8951_PIXEL_FORMAT_1BPP:
        case IT8951_PIXEL_FORMAT_8BPP:
            pixel_format_value = IT8951_8BPP;
            break;
        case IT8951_PIXEL_FORMAT_2BPP:
            pixel_format_value = IT8951_2BPP;
            break;
        case IT8951_PIXEL_FORMAT_4BPP:
            pixel_format_value = IT8951_4BPP;
            break;
        default:
            assert(false);
            break;
    }

    set_target_memory_address(target_memory_address);

    auto x = area.x;
    auto w = area.w;

    if (pixel_format == IT8951_PIXEL_FORMAT_1BPP) {
        x /= 8;
        w /= 8;
    }

    // Send image load area start command.

    write_command(IT8951_TCON_LD_IMG_AREA);
    write_data((IT8951_LDIMG_B_ENDIAN << 8 | pixel_format_value << 4 | (uint16_t)rotate));
    write_data(x);
    write_data(area.y);
    write_data(w);
    write_data(area.h);

    // This is the start of the write_data method. The data itself
    // will be written in chunks.

    transaction_start();

    wait_until_idle();
    write_word(0x0000);
    wait_until_idle();
}

void IT8951::load_image_flush_buffer(size_t len) {
    ESP_ERROR_ASSERT(len <= _buffer_len);

    if (_buffer_transaction_pending) {
        spi_transaction_t* result_transaction;
        ESP_ERROR_CHECK(spi_device_get_trans_result(_spi, &result_transaction, portMAX_DELAY));

        ESP_ERROR_ASSERT(result_transaction == &_buffer_transaction);

        _buffer_transaction_pending = false;
    }

    if (!len) {
        return;
    }

    _buffer_transaction = {
        .length = 8 * len,
        .tx_buffer = _current_buffer == 0 ? _buffer0 : _buffer1,
    };

    ESP_ERROR_CHECK(spi_device_queue_trans(_spi, &_buffer_transaction, portMAX_DELAY));

    _buffer_transaction_pending = true;
    _current_buffer = (_current_buffer + 1) % 2;
}

void IT8951::load_image_end() {
    load_image_flush_buffer(0);

    _current_buffer = 0;

    transaction_end();

    write_command(IT8951_TCON_LD_IMG_END);
}

void IT8951::display_area(IT8951Area& area, uint32_t target_memory_address, it8951_pixel_format_t pixel_format,
                          it8951_display_mode_t mode) {
    wait_display_ready();

    if (pixel_format == IT8951_PIXEL_FORMAT_1BPP) {
        // Set Display mode to 1 bpp mode - Set 0x18001138 Bit[18](0x1800113A Bit[2])to 1

        write_reg(UP1SR + 2, read_reg(UP1SR + 2) | (1 << 2));
        write_reg(BGVR, (FRONT_GRAY_VALUE << 8) | BACK_GRAY_VALUE);
    }

    if (!target_memory_address) {
        write_command(USDEF_I80_CMD_DPY_AREA);
        write_data(area.x);
        write_data(area.y);
        write_data(area.w);
        write_data(area.h);
        write_data(get_mode_value(mode));
    } else {
        write_command(USDEF_I80_CMD_DPY_BUF_AREA);
        write_data(area.x);
        write_data(area.y);
        write_data(area.w);
        write_data(area.h);
        write_data(get_mode_value(mode));
        write_data(target_memory_address);
        write_data(target_memory_address >> 16);
    }

    if (pixel_format == IT8951_PIXEL_FORMAT_1BPP) {
        wait_display_ready();

        write_reg(UP1SR + 2, read_reg(UP1SR + 2) & ~(1 << 2));
    }
}

void IT8951::set_target_memory_address(uint32_t target_memory_address) {
    uint16_t WordH = (uint16_t)((target_memory_address >> 16) & 0x0000FFFF);
    uint16_t WordL = (uint16_t)(target_memory_address & 0x0000FFFF);

    write_reg(LISAR + 2, WordH);
    write_reg(LISAR, WordL);
}

void IT8951::wait_display_ready() {
    const uint32_t start = millis();

    while (true) {
        if (!read_reg(LUTAFSR)) {
            return;
        }

        if (millis() - start > this->idle_timeout()) {
            ESP_LOGE(TAG, "Device not ready for more than 30 seconds; exiting");
            esp_restart();
            return;
        }
        delay(20);
    }
}

uint16_t IT8951::get_mode_value(it8951_display_mode_t mode) {
    switch (mode) {
        case IT8951_DISPLAY_MODE_INIT:
            return IT8951_MODE_INIT;
        case IT8951_DISPLAY_MODE_A2:
            return _a2_mode;
        default:
            return IT8951_MODE_GC16;
    }
}
