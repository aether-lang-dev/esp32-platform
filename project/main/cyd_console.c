/* Output-only text console for Freenove FNK0114B. Call from the runner task. */
#include "cyd_console.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "font8x8_basic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    WIDTH = 240, HEIGHT = 320, SCALE = 2, CELL = 8 * SCALE,
    COLS = WIDTH / CELL, ROWS = HEIGHT / CELL,
    LCD_SCLK = 14, LCD_MOSI = 13, LCD_CS = 15, LCD_DC = 2, LCD_BACKLIGHT = 21,
};

static const char *const TAG = "cyd_console";
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t transfer_done;
static uint16_t *pixels;
static unsigned top, row, column;
static bool online;
static char cells[ROWS][COLS];

static bool transfer_complete(esp_lcd_panel_io_handle_t io,
                              esp_lcd_panel_io_event_data_t *event, void *context) {
    (void)io;
    (void)event;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(context, &wake);
    return wake == pdTRUE;
}

static bool draw_cells(unsigned screen_row, const char *line, unsigned filled) {
    for (unsigned y = 0; y < CELL; ++y) {
        for (unsigned col = 0; col < COLS; ++col) {
            unsigned bits = font8x8_basic[(unsigned char)line[col]][y / SCALE];
            for (unsigned x = 0; x < CELL; ++x) {
                bool ink = (bits & (1u << (x / SCALE))) != 0;
                bool fill = (filled & (1u << col)) != 0;
                pixels[y * WIDTH + col * CELL + x] = ink != fill ? 0xffff : 0;
            }
        }
    }
    unsigned y = screen_row * CELL;
    return esp_lcd_panel_draw_bitmap(panel, 0, y, WIDTH, y + CELL, pixels) == ESP_OK &&
           xSemaphoreTake(transfer_done, pdMS_TO_TICKS(250)) == pdTRUE;
}

static bool draw_row(unsigned screen_row) {
    return draw_cells(screen_row, cells[(top + screen_row) % ROWS], 0);
}

#ifdef RUNNER_USB
_Static_assert((int)CYD_COLS == COLS && (int)CYD_ROWS == ROWS, "layout dimensions");
static bool showing_buttons;
static CydLayout visible_layout;
static uint32_t visible_version;

static void center(char *line, unsigned width, const char *label) {
    size_t length = strlen(label);
    memcpy(line + (width - length) / 2, label, length);
}

bool cyd_console_layout(const CydLayout *layout, uint32_t version) {
    if (!online) return false;
    for (unsigned i = 0; i < ROWS; ++i) {
        char line[COLS];
        memset(line, ' ', sizeof line);
        if (i == 1) center(line, COLS, layout->title);
        if (i == (layout->legacy ? 16u : 18u)) center(line, COLS, layout->status);
        unsigned filled = 0;
        for (unsigned j = 0; j < layout->count; ++j) {
            const CydButton *button = &layout->buttons[j];
            if (i < button->row || i >= button->row + button->height) continue;
            filled |= ((1u << button->width) - 1) << button->col;
            if (i == button->row + (button->height - 1) / 2) {
                center(line + button->col, button->width, button->label);
            }
        }
        if (!draw_cells(i, line, filled)) {
            online = false;
            return false;
        }
    }
    visible_layout = *layout;
    visible_version = version;
    showing_buttons = true;
    return true;
}

unsigned cyd_console_button_at(const CydTouch *tap, uint32_t version) {
    if (!online || !showing_buttons || version != visible_version || !tap) return 0;
    return cyd_layout_hit(&visible_layout, tap);
}
#endif

static esp_err_t start_panel(void) {
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_BACKLIGHT, 0), TAG, "backlight off");
    ESP_RETURN_ON_ERROR(gpio_set_direction(LCD_BACKLIGHT, GPIO_MODE_OUTPUT), TAG, "backlight GPIO");
    transfer_done = xSemaphoreCreateBinary();
    pixels = heap_caps_malloc(WIDTH * CELL * sizeof *pixels, MALLOC_CAP_DMA);
    if (!transfer_done || !pixels) return ESP_ERR_NO_MEM;

    spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK, .mosi_io_num = LCD_MOSI, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = WIDTH * CELL * sizeof *pixels,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");
    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = LCD_DC, .cs_gpio_num = LCD_CS, .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 1,
    };
    esp_lcd_panel_io_handle_t handle;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &handle), TAG, "SPI panel I/O");
    const esp_lcd_panel_io_callbacks_t callbacks = {.on_color_trans_done = transfer_complete};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(handle, &callbacks, transfer_done),
                        TAG, "SPI completion callback");
    const esp_lcd_panel_dev_config_t device = {
        .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(handle, &device, &panel), TAG, "ST7789 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, false), TAG, "panel inversion");
    memset(cells, ' ', sizeof cells);
    for (unsigned i = 0; i < ROWS; ++i) {
        if (!draw_row(i)) return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "panel on");
    return gpio_set_level(LCD_BACKLIGHT, 1);
}

bool cyd_console_init(void) {
    online = start_panel() == ESP_OK;
    return online;
}

static void next_line(void) {
    column = 0;
    if (row + 1 < ROWS) {
        ++row;
    } else {
        top = (top + 1) % ROWS;
        memset(cells[(top + row) % ROWS], ' ', COLS);
    }
}

void cyd_console_line(const char *text) {
    if (!online) return;
#ifdef RUNNER_USB
    bool redraw = showing_buttons;
    showing_buttons = false;
#else
    const bool redraw = false;
#endif
    const unsigned first_row = row;
    unsigned lines = 0;
    for (const unsigned char *ch = (const unsigned char *)text; *ch; ++ch) {
        if (*ch == '\r') continue;
        if (column == COLS || *ch == '\n') {
            next_line();
            ++lines;
        }
        if (*ch != '\n') cells[(top + row) % ROWS][column++] = *ch >= 32 && *ch < 127 ? *ch : '?';
    }
    next_line();
    ++lines;
    const unsigned dirty = redraw || first_row + lines >= ROWS ? 0 : first_row;
    const unsigned last = redraw ? ROWS - 1 : row;
    for (unsigned i = dirty; i <= last; ++i) {
        if (!draw_row(i)) {
            online = false;
            puts("RUNNER CYD console transfer failed");
            break;
        }
    }
}
