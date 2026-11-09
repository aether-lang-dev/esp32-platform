/* Copyright (C) 2021 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/uart.toit.
 */
DO sequence = 0 TO 2
  message = 'sending' sequence
  IF UART_ECHO(message) <> message THEN EXIT 1
  SAY 'Received:' message
  CALL WAIT_MS 50
END
EXIT 0
