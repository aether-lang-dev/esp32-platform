/* Copyright (C) 2021 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/i2c.toit.
 */
SAY 'Scanning I2C'
count = 0
DO address = 8 TO 119
  IF I2C_PROBE(address) THEN DO
    count = count + 1
    SAY '  address' address
  END
END
SAY 'Found:' count 'devices'
EXIT 0
