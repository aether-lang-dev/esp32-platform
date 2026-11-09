# Toit ESP32 hardware examples

These are standalone ESP-IDF ports of the remaining Toit hardware examples.
Each target builds one firmware application for a classic ESP32. Use ESP-IDF
6.1 and select a fresh build directory and configuration per target.

```sh
demo=ble_scan
mkdir -p "build/$demo"
idf.py -B "build/$demo" -DIDF_TARGET=esp32 -DTOIT_EXAMPLE="$demo" \
  -DSDKCONFIG="$PWD/build/$demo/sdkconfig" build
```

Use the same arguments with `menuconfig`, `flash` or `monitor`.

| Target | Behavior |
| --- | --- |
| `wifi_connect` | Connect to a configured AP and report SSID, BSSID, RSSI, channel and authentication mode. |
| `ntp` | Connect to Wi-Fi, synchronize the ESP32 clock with ESP-IDF SNTP and print the adjustment and UTC time. |
| `espnow` | Broadcast a numbered greeting every second on channel 1 and print incoming messages. Run on two boards. Broadcasts are unencrypted. |
| `ethernet` | Power the LAN8720 on an Olimex ESP32-POE, install the Ethernet provider, wait for DHCP, then close the interface and power down the PHY. |
| `touch_wake` | Calibrate GPIO32 with 16 samples, sleep, and wake on touch or after 30 seconds. |
| `ble_scan` | Passively scan for Battery Service advertisements for three seconds. |
| `ble_scan_active` | Scan for two seconds and merge distinct advertisement blocks with scan responses by device address. |
| `ble_advertise` | Advertise the Toit device name, Battery Service UUID and manufacturer bytes for 1,000 seconds. |
| `ble_connect` | Connect to an advertising Battery Service and read its Battery Level characteristic. |
| `ble_heart_rate` | Advertise the sample GATT service and send simulated readings every 500 ms to a subscribed central. Incoming writes go to serial. |

For `wifi_connect` or `ntp`, set the station SSID and password in the
**Toit Wi-Fi example** menu. These settings are embedded in the image.
Use a private test network and keep the generated configuration and
firmware private. The NTP server is configurable in the same menu.

For `ethernet`, the board profile uses GPIO12 for PHY power, GPIO17 for the
RMII clock, GPIO23 for MDC, and GPIO18 for MDIO. The provider, client, and
simple entry point share one firmware process. Use `ethernet_qemu` for the
same provider and client lifecycle with an emulated OpenETH driver.

To verify `ethernet_qemu`, build it with the command above, then merge and boot
the image using Espressif's Xtensa QEMU:

```sh
(cd build/ethernet_qemu && esptool.py --chip esp32 merge_bin \
  --fill-flash-size 4MB -o flash.bin @flash_args)
timeout 90 python3 test_ethernet_qemu.py /path/to/qemu-system-xtensa \
  build/ethernet_qemu/flash.bin
```

The test requires both a DHCP address and a clean provider shutdown.
See [provenance](../../PROVENANCE.md) for the upstream files and licences.
