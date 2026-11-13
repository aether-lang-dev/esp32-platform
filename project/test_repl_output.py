"""Compile the actual REPL output callback with a local mutex stub."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).parent / "main/runner_repl.c").read_text()
callback = source[source.index("static void output("):source.index("static bool is_cancelled(")]
harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
enum { EVENT_COUNT = 16, EVENT_TEXT = 256 };
typedef struct {
    unsigned sequence;
    char kind[8], text[EVENT_TEXT];
    bool end;
} Event;
static unsigned emitted;
static Event events[EVENT_COUNT];
#define lock 0
#define portMAX_DELAY 0
#define xSemaphoreTake(a,b) ((void)0)
#define xSemaphoreGive(a) ((void)0)
"""
checks = r"""
int main(void) {
    char cats[521], decoded[521];
    for (unsigned i = 0; i < 130; ++i) memcpy(cats + i * 4, "\xf0\x9f\x98\xba", 4);
    cats[520] = 0;
    output(NULL, "say", cats);
    assert(emitted == 3);
    size_t used = 0;
    for (unsigned i = 0; i < emitted; ++i) {
        size_t n = strlen(events[i].text);
        assert(n % 4 == 0);
        memcpy(decoded + used, events[i].text, n);
        used += n;
    }
    assert(used == 520 && !memcmp(cats, decoded, used));
    char malformed[513];
    memset(malformed, 0x80, 512);
    malformed[512] = 0;
    emitted = 0;
    output(NULL, "say", malformed);
    assert(emitted > 0 && events[(emitted - 1) % EVENT_COUNT].end);
    return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "output.c"
    binary = Path(directory) / "output"
    path.write_text(harness + callback + checks)
    subprocess.run(["cc", "-D_POSIX_C_SOURCE=200809L", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=3)
print("REPL output preserves Unicode and makes progress on malformed UTF-8 PASS")
