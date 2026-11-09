/* Copyright (C) 2022 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/wifi/scan.toit.
 */
count = 0
DO index = 0 TO 7
  ap = WIFI_SCAN_ROW(index)
  IF ap = '' THEN LEAVE
  IF count = 0 THEN SAY LEFT('SSID', 32) LEFT('BSSID', 17) LEFT('RSSI', 5) LEFT('Channel', 7) 'Auth'
  SAY LEFT(ap.ssid, 32) LEFT(ap.bssid, 17) LEFT(ap.rssi, 5) LEFT(ap.channel, 7) ap.auth
  count = count + 1
END
IF count = 0 THEN SAY 'Scan done, but no APs found'
EXIT 0
