"""Exercise the compiled recovery task with sampled button levels."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).parent / "main/runner_s60.c").read_text()
start = source.index("static void recovery_button(")
end = source.index("bool runner_s60_init(", start)
task = source[start:end]
harness = r"""
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>

#define GPIO_NUM_4 4
#define GPIO_NUM_9 9
#define ESP_ERROR_CHECK(value) assert((value) == 0)
#define pdMS_TO_TICKS(value) (value)
static jmp_buf finished;
static unsigned sample, low_samples, recovered, toggles;
static void runner_outlet_button(void) { ++toggles; }
static int gpio_get_level(int pin) {
    assert(pin == GPIO_NUM_9);
    return sample >= low_samples;
}
static int gpio_set_level(int pin, int level) {
    assert(pin == GPIO_NUM_4 && level == 0);
    return 0;
}
static void runner_s60_recover(void) {
    assert(sample >= low_samples);
    ++recovered;
    longjmp(finished, 1);
}
static void vTaskDelay(unsigned ticks) {
    assert(ticks == 100);
    if (++sample == 100) longjmp(finished, 1);
}
"""
checks = r"""
int main(void) {
    low_samples = 1;
    if (!setjmp(finished)) recovery_button(NULL);
    assert(recovered == 0 && toggles == 0);
    sample = 0;
    low_samples = 49;
    if (!setjmp(finished)) recovery_button(NULL);
    assert(recovered == 0 && toggles == 1);
    sample = 0;
    low_samples = 55;
    if (!setjmp(finished)) recovery_button(NULL);
    assert(recovered == 1 && sample == 55 && toggles == 1);
    return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    program = Path(directory) / "button.c"
    binary = Path(directory) / "button"
    program.write_text(harness + task + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(program), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("S60 short release toggles outlet; long release recovers without toggling PASS")
