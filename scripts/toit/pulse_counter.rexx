/* Copyright (C) 2022 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/pcnt.toit.
 */
previous = PULSE_COUNT()
DO sample = 1 TO 5
  CALL WAIT_MS 500
  count = PULSE_COUNT()
  IF count <= previous THEN EXIT 1
  SAY 'Pulse count:' count
  previous = count
END
EXIT 0
