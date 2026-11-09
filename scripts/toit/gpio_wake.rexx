/* Copyright (C) 2022 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/triggers/gpio.toit.
 */
cause = WAKE_CAUSE()
IF cause = 2 THEN DO
  SAY 'Woken up from external pin'
  CALL WAIT_MS 1000
END
ELSE SAY 'Woken up for other reasons:' cause
SAY 'Sleeping for up to 30 seconds'
EXIT 0
