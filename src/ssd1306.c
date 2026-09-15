#include "ssd1306.h"

#include <string.h>

#include "board.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define OLED_WIDTH 128u
#define OLED_PAGES 8u
#define UI_WIDTH 64u
#define UI_HEIGHT 128u
static uint8_t framebuffer[OLED_WIDTH * OLED_PAGES];

static bool command(uint8_t value) {
    uint8_t packet[] = {0x00, value};
    return i2c_write_blocking(OLED_I2C, OLED_I2C_ADDRESS, packet, sizeof packet, false) == sizeof packet;
}

bool ssd1306_init(void) {
    i2c_init(OLED_I2C, OLED_I2C_BAUD);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_SDA_PIN);
    gpio_pull_up(OLED_SCL_PIN);
    sleep_ms(100);

    const uint8_t init[] = {0xAE, 0x20, 0x00, 0x40, 0xA1, 0xC8, 0x81, 0x7F,
                            0xA6, 0xA8, 0x3F, 0xD3, 0x00, 0xD5, 0x80, 0xD9,
                            0xF1, 0xDA, 0x12, 0xDB, 0x40, 0x8D, 0x14, 0xAF};
    for (unsigned i = 0; i < sizeof init; ++i)
        if (!command(init[i])) return false;
    ssd1306_clear();
    ssd1306_show();
    return true;
}

void ssd1306_clear(void) { memset(framebuffer, 0, sizeof framebuffer); }

// Compact 5x7 diagnostics glyphs: only characters needed by the bring-up screen.
static const uint8_t glyphs[][5] = {
    {0,0,0,0,0}, {0x3e,0x51,0x49,0x45,0x3e}, {0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31}, {0x18,0x14,0x12,0x7f,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36}, {0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c}, {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f}, {0,0x41,0x7f,0x41,0},
    {0x20,0x40,0x41,0x3f,0x01}, {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f}, {0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,0x09,0x09,0x09,0x06}, {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01}, {0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f}, {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x08,0x08,0x08,0x08,0x08},
    {0,0x60,0x60,0,0},
    {0x08,0x08,0x3e,0x08,0x08}
};

static int glyph_index(char c) {
    if (c >= '0' && c <= '9') return 1 + c - '0';
    if (c >= 'A' && c <= 'Z') return 11 + c - 'A';
    if (c == '-') return 37;
    if (c == '.') return 38;
    if (c == '+') return 39;
    return 0;
}

static void ui_pixel_unchecked(uint8_t x, uint8_t y, bool on) {
    // The requested counterclockwise transform is:
    // physical_x = 127 - logical_y, physical_y = logical_x.
    uint8_t physical_x = (uint8_t)(OLED_WIDTH - 1u - y);
    uint8_t physical_y = x;
    uint8_t *cell = &framebuffer[(physical_y >> 3) * OLED_WIDTH + physical_x];
    uint8_t mask = (uint8_t)(1u << (physical_y & 7u));
    if (on) *cell |= mask;
    else *cell &= (uint8_t)~mask;
}

void ssd1306_ui_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= UI_WIDTH || y >= UI_HEIGHT) return;
    ui_pixel_unchecked(x, y, on);
}

void ssd1306_ui_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool on) {
    for (uint8_t row = y; row < UI_HEIGHT && row < (uint8_t)(y + height); ++row)
        for (uint8_t col = x; col < UI_WIDTH && col < (uint8_t)(x + width); ++col)
            ui_pixel_unchecked(col, row, on);
}

void ssd1306_ui_text(uint8_t x, uint8_t y, const char *text, bool on) {
    while (*text && x + 5u <= UI_WIDTH) {
        const uint8_t *glyph = glyphs[glyph_index(*text++)];
        for (uint8_t col = 0; col < 5u; ++col)
            for (uint8_t row = 0; row < 7u; ++row)
                if (glyph[col] & (1u << row)) ssd1306_ui_pixel((uint8_t)(x + col), (uint8_t)(y + row), on);
        x = (uint8_t)(x + 6u);
    }
}

void ssd1306_ui_text_scaled(uint8_t x, uint8_t y, const char *text, bool on, uint8_t scale) {
    if (scale == 0) return;
    while (*text && x + 5u * scale <= UI_WIDTH) {
        const uint8_t *glyph = glyphs[glyph_index(*text++)];
        for (uint8_t col = 0; col < 5u; ++col)
            for (uint8_t row = 0; row < 7u; ++row)
                if (glyph[col] & (1u << row))
                    for (uint8_t dx = 0; dx < scale; ++dx)
                        for (uint8_t dy = 0; dy < scale; ++dy)
                            ssd1306_ui_pixel((uint8_t)(x + col * scale + dx),
                                             (uint8_t)(y + row * scale + dy), on);
        x = (uint8_t)(x + 6u * scale);
    }
}

void ssd1306_text(uint8_t row, const char *text) {
    if (row >= OLED_PAGES) return;
    uint8_t col = 0;
    while (*text && col + 5 < OLED_WIDTH) {
        const uint8_t *glyph = glyphs[glyph_index(*text++)];
        for (unsigned i = 0; i < 5; ++i) framebuffer[row * OLED_WIDTH + col++] = glyph[i];
        framebuffer[row * OLED_WIDTH + col++] = 0;
    }
}

void ssd1306_show(void) {
    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        command(0xB0 | page); command(0x00); command(0x10);
        uint8_t packet[OLED_WIDTH + 1] = {0x40};
        memcpy(&packet[1], &framebuffer[page * OLED_WIDTH], OLED_WIDTH);
        i2c_write_blocking(OLED_I2C, OLED_I2C_ADDRESS, packet, sizeof packet, false);
    }
}
