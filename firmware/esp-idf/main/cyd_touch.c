/* FNK0114B XPT2046 input. Its SPI bus is separate from the LCD bus. */
#include "cyd_touch.h"
#include "driver/spi_master.h"

enum { TOUCH_SCLK = 25, TOUCH_MOSI = 32, TOUCH_MISO = 39, TOUCH_CS = 33 };
enum { ADC_MAX = 4095, PRESS_THRESHOLD = 250, RELEASE_THRESHOLD = 125 };
static spi_device_handle_t device;
static bool held;

bool cyd_touch_init(void) {
    const spi_bus_config_t bus = {
        .sclk_io_num = TOUCH_SCLK, .mosi_io_num = TOUCH_MOSI, .miso_io_num = TOUCH_MISO,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 3,
    };
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_DISABLED) != ESP_OK) return false;
    const spi_device_interface_config_t config = {
        .clock_speed_hz = 1000000, .mode = 0, .spics_io_num = TOUCH_CS, .queue_size = 1,
    };
    if (spi_bus_add_device(SPI3_HOST, &config, &device) == ESP_OK) return true;
    spi_bus_free(SPI3_HOST);
    return false;
}

bool cyd_touch_ready(void) {
    return device != NULL;
}

static int sample(unsigned char command) {
    spi_transaction_t transaction = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 24, .tx_data = {command, 0, 0},
    };
    if (spi_device_polling_transmit(device, &transaction) != ESP_OK) return -1;
    return ((transaction.rx_data[1] << 8) | transaction.rx_data[2]) >> 3 & ADC_MAX;
}

static int median(int a, int b, int c) {
    if (a > b) { int swap = a; a = b; b = swap; }
    if (b > c) b = c;
    return a > b ? a : b;
}

bool cyd_touch_take(CydTouch *touch) {
    if (!cyd_touch_ready()) return false;
    int z1 = sample(0xb0);
    int z2 = sample(0xc0);
    if (z1 < 0 || z2 < 0) return false;
    int pressure = z1 + ADC_MAX - z2;
    if (held) {
        if (pressure < RELEASE_THRESHOLD || !z1) held = false;
        return false;
    }
    if (pressure < PRESS_THRESHOLD || !z1) return false;
    int xs[3], ys[3];
    for (unsigned i = 0; i < 3; ++i) {
        xs[i] = sample(0xd0);
        ys[i] = sample(0x90);
        if (xs[i] <= 0 || xs[i] >= ADC_MAX || ys[i] <= 0 || ys[i] >= ADC_MAX) return false;
    }
    held = true;
    *touch = (CydTouch){median(xs[0], xs[1], xs[2]), median(ys[0], ys[1], ys[2]), pressure};
    return true;
}
