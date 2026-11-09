# Aether HTTP over TLS on ESP32

An isolated Aether 0.883.0 build served TLS 1.3 through ESP-IDF 6.1 and
Espressif QEMU on plain ESP32. Three verified handshakes took 61–110 ms.
The standalone firmware was 762,624 bytes and its minimum free internal
heap was 175,260 bytes. The server task used 14,580 of its 24,576 stack bytes.

The signed HTTP control passed its full suite. It used 585,840 firmware
bytes and had 194,448 bytes of internal heap free at its lowest point.
The combined signed HTTPS prototype used 998,080 firmware bytes and
reached 91,584 bytes free before its first trial failed cleanup with
8,260 bytes still charged to the script's account.

The HTTPS server and RexxAE worker run as different FreeRTOS tasks.
Under `AETHER_NO_THREADING`, Aether's execution state and account selection
are static. The tasks consequently share that state. A supported port
needs task-local execution state or a way to integrate both jobs on one
Aether task. Signed HTTPS activation and rollback remain unverified.

The probe also identified concrete port work:

- Use ESP-IDF's `sys/poll.h` and avoid Aether's `tcp_close` collision with lwIP.
- Include AES, the poll backend and HTTP proxy/server dependencies in the
  network-enabled cooperative build.
- Adapt `getrlimit`, `pipe` and `nanosleep` for ESP-IDF. The probe used a
  FreeRTOS delay and omitted the optional wake pipe.
- Give the IPC task room for the linked program's static thread-local data.
  Its default stack overflowed at startup in the combined build.
- Ensure the generated TLS backend callbacks override the HTTP server's weak
  fallbacks when the TLS backend and the guest are separate generated objects.

The experiment used disposable certificates and enabled the non-RF entropy
source. Validate the entropy configuration on hardware before deploying TLS.
[ESP-IDF RNG documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/random.html)
