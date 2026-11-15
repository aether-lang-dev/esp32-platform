#include "cyd_console.h"
#include <string.h>

bool cyd_layout_add(CydLayout *layout, const CydButton *button) {
    if (layout->legacy || layout->count == CYD_BUTTON_LIMIT ||
        !button->id || button->id > CYD_BUTTON_ID_MAX ||
        button->col >= CYD_COLS || !button->width || button->width > CYD_COLS - button->col ||
        button->row < CYD_BUTTON_ROW_MIN || button->row >= CYD_BUTTON_ROW_END ||
        !button->height || button->height > CYD_BUTTON_ROW_END - button->row ||
        !button->label[0] || strlen(button->label) > button->width) return false;

    for (unsigned i = 0; i < layout->count; ++i) {
        const CydButton *other = &layout->buttons[i];
        if (other->id == button->id ||
            (button->col < other->col + other->width && other->col < button->col + button->width &&
             button->row < other->row + other->height && other->row < button->row + button->height)) {
            return false;
        }
    }
    layout->buttons[layout->count++] = *button;
    return true;
}

unsigned cyd_layout_hit(const CydLayout *layout, const CydTouch *tap) {
    /* FNK0114B portrait reference points from Freenove. Keep the
     * fractional part until comparing against cell boundaries. */
    int x = 210 * 2781 - ((int)tap->x - 683) * 180;
    int y = 30 * 3025 + ((int)tap->y - 527) * 260;
    if (x < 0 || y < 0) return 0;
    unsigned col = x / (16 * 2781);
    unsigned row = y / (16 * 3025);
    for (unsigned i = 0; i < layout->count; ++i) {
        const CydButton *button = &layout->buttons[i];
        if (col >= button->col && col < button->col + button->width &&
            row >= button->row && row < button->row + button->height) return button->id;
    }
    return 0;
}
